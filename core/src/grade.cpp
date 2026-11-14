#include "vstudio/grade.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "detail/parallel.hpp"
#include "vstudio/clip_tools.hpp"
#include "vstudio/color_space.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/pixel.hpp"

namespace vstudio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kLr = 0.2126f, kLg = 0.7152f, kLb = 0.0722f;  // Rec.709 luma
constexpr int kHueBins = 360;

struct Wheel {
    double bal[3] = {0, 0, 0};
    double master = 0;
    [[nodiscard]] bool neutral() const { return bal[0] == 0 && bal[1] == 0 && bal[2] == 0 && master == 0; }
};

Wheel wheel(const Json& p, const char* key) {
    Wheel w;
    auto it = p.find(key);
    if (it == p.end() || !it->is_array()) return w;
    for (std::size_t i = 0; i < 3 && i < it->size(); ++i)
        if ((*it)[i].is_number()) w.bal[i] = std::clamp((*it)[i].get<double>(), -1.0, 1.0);
    if (it->size() > 3 && (*it)[3].is_number()) w.master = std::clamp((*it)[3].get<double>(), -1.0, 1.0);
    return w;
}

double smooth(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// 0..1 inside [lo, hi], soft edges of width `soft` outside it.
float rangeMask(float v, float lo, float hi, float soft) {
    if (v >= lo && v <= hi) return 1.0f;
    const float d = v < lo ? lo - v : v - hi;
    if (d >= soft) return 0.0f;
    const float t = 1.0f - d / soft;
    return t * t * (3 - 2 * t);
}

double hueDistance(double a, double b) {
    double d = std::fmod(std::abs(a - b), 360.0);
    return d > 180 ? 360 - d : d;
}

void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), c = mx - mn;
    v = mx;
    s = mx > 1e-6f ? c / mx : 0.0f;
    if (c <= 1e-6f) {
        h = 0;
        return;
    }
    const float inv = 60.0f / c;
    if (mx == r) {
        h = (g - b) * inv;
        if (h < 0) h += 360.0f;
    } else if (mx == g) {
        h = (b - r) * inv + 120.0f;
    } else {
        h = (r - g) * inv + 240.0f;
    }
}

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    if (s <= 0) {
        r = g = b = v;
        return;
    }
    if (h < 0) h += 360.0f * (1 + static_cast<int>(-h / 360.0f));
    if (h >= 360.0f) h -= 360.0f * static_cast<int>(h / 360.0f);
    const float hp = h * (1.0f / 60.0f);
    const int i = std::min(static_cast<int>(hp), 5);
    const float f = hp - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        default: r = v, g = p, b = q; break;
    }
}

// Everything the per-pixel loop needs, built once per frame from the params.
struct Plan {
    // Steps 1-3 (per channel): sampled at `steps`+1 inputs in 0..1 (+1 guard entry).
    int steps = 0;
    std::vector<float> table[3];
    bool tone = false;
    float saturation = 1.0f;
    // HSL mixer, per hue bin (+1 guard): hue shift (deg), saturation factor, luminance.
    bool hsl = false;
    std::vector<float> hShift, hSat, hLum;
    // Qualifier.
    bool qual = false, invert = false, showMask = false;
    std::vector<float> qHue;  // hue mask per bin
    float sLo = 0, sHi = 1, lLo = 0, lHi = 1, soft = 0.05f;
    float qShift = 0, qSat = 1, qExp = 1;

    [[nodiscard]] float lookup(int c, float v) const {
        const float pos = std::clamp(v, 0.0f, 1.0f) * steps;
        const int i = static_cast<int>(pos);
        const float t = pos - i;
        return table[c][i] + (table[c][i + 1] - table[c][i]) * t;
    }
    static float hueTable(const std::vector<float>& t, float h) {
        h -= 360.0f * std::floor(h / 360.0f);  // hue shifts may leave 0..360
        const float pos = h * (kHueBins / 360.0f);
        const int i = std::clamp(static_cast<int>(pos), 0, kHueBins - 1);
        const float f = pos - i;
        return t[i] + (t[i + 1] - t[i]) * f;
    }
};

Plan makePlan(const Json& p, int steps) {
    Plan pl;
    pl.steps = steps;
    const Wheel off = wheel(p, "offset"), lift = wheel(p, "lift"), gam = wheel(p, "gamma"), gain = wheel(p, "gain");
    const double contrast = std::clamp(p.value("contrast", 1.0), 0.0, 2.0);
    const double pivot = std::clamp(p.value("pivot", 0.435), 0.05, 0.95);
    std::vector<float> curves[4];  // master, r, g, b
    static const char* kCurveKeys[4] = {"master", "r", "g", "b"};
    if (auto it = p.find("curves"); it != p.end() && it->is_object())
        for (int k = 0; k < 4; ++k)
            if (auto c = it->find(kCurveKeys[k]); c != it->end() && c->is_array() && c->size() >= 1) {
                // Skip identity curves (only the two end points on the diagonal).
                bool identity = true;
                for (const auto& pt : *c)
                    if (!pt.is_array() || pt.size() != 2 || std::abs(pt[0].get<double>() - pt[1].get<double>()) > 1e-6)
                        identity = false;
                if (!identity) curves[k] = sampleCurve(*c, steps + 1);
            }
    pl.tone = !off.neutral() || !lift.neutral() || !gam.neutral() || !gain.neutral() || contrast != 1.0;
    for (const auto& c : curves) pl.tone = pl.tone || !c.empty();
    if (pl.tone) {
        for (int c = 0; c < 3; ++c) {
            const double o = 0.5 * (off.bal[c] + off.master);
            const double l = 0.25 * (lift.bal[c] + lift.master);
            const double g = std::pow(2.0, gain.master) * (1.0 + 0.5 * gain.bal[c]);
            const double e = std::pow(2.0, -(gam.master + 0.5 * gam.bal[c]));
            auto& t = pl.table[c];
            t.resize(steps + 2);
            for (int i = 0; i <= steps; ++i) {
                double x = static_cast<double>(i) / steps + o;
                x = g * x + l * (1.0 - x);
                x = std::pow(std::max(x, 0.0), e);
                x = (x - pivot) * contrast + pivot;
                x = std::clamp(x, 0.0, 1.0);
                auto curve = [&](const std::vector<float>& cv, double v) {
                    if (cv.empty()) return v;
                    const double pos = v * steps;
                    const int k = std::min(static_cast<int>(pos), steps - 1);
                    return cv[k] + (cv[k + 1] - cv[k]) * (pos - k);
                };
                x = curve(curves[0], x);
                x = curve(curves[1 + c], x);
                t[i] = static_cast<float>(std::clamp(x, 0.0, 1.0));
            }
            t[steps + 1] = t[steps];
        }
    }
    pl.saturation = static_cast<float>(std::clamp(p.value("saturation", 1.0), 0.0, 2.0));

    // HSL mixer: weights fall linearly from a band's centre to its neighbours' centres
    // (smoothed), so they sum to one at every hue and adjacent bands blend.
    if (auto it = p.find("hsl"); it != p.end() && it->is_object()) {
        const auto& bands = hslBands();
        std::vector<std::array<double, 3>> adj(bands.size(), {0, 0, 0});
        for (std::size_t b = 0; b < bands.size(); ++b)
            if (auto v = it->find(bands[b].id); v != it->end() && v->is_array())
                for (std::size_t k = 0; k < 3 && k < v->size(); ++k)
                    if ((*v)[k].is_number()) {
                        adj[b][k] = std::clamp((*v)[k].get<double>(), -1.0, 1.0);
                        pl.hsl = pl.hsl || adj[b][k] != 0;
                    }
        if (pl.hsl) {
            pl.hShift.assign(kHueBins + 1, 0);
            pl.hSat.assign(kHueBins + 1, 1);
            pl.hLum.assign(kHueBins + 1, 0);
            const std::size_t n = bands.size();
            for (int i = 0; i <= kHueBins; ++i) {
                const double h = i * 360.0 / kHueBins;
                double shift = 0, sat = 0, lum = 0;
                for (std::size_t b = 0; b < n; ++b) {
                    const double c = bands[b].hue;
                    const double prev = bands[(b + n - 1) % n].hue, next = bands[(b + 1) % n].hue;
                    const double dPrev = hueDistance(c, prev), dNext = hueDistance(c, next);
                    // Which side of the centre is h on (counter-clockwise = towards next)?
                    const double ccw = std::fmod(h - c + 720.0, 360.0);
                    const bool towardsNext = ccw <= 180.0;
                    const double span = towardsNext ? dNext : dPrev;
                    const double d = hueDistance(h, c);
                    if (d >= span) continue;
                    const double w = smooth(1.0 - d / span);
                    shift += w * adj[b][0] * 30.0;
                    sat += w * adj[b][1];
                    lum += w * adj[b][2];
                }
                pl.hShift[i] = static_cast<float>(shift);
                pl.hSat[i] = static_cast<float>(1.0 + sat);
                pl.hLum[i] = static_cast<float>(lum);
            }
        }
    }

    if (auto it = p.find("qualifier"); it != p.end() && it->is_object() && it->value("enabled", true)) {
        const Json& q = *it;
        auto pair = [&](const char* key, double a, double b) {
            std::array<double, 2> out{a, b};
            if (auto v = q.find(key); v != q.end() && v->is_array() && v->size() == 2) out = {(*v)[0].get<double>(), (*v)[1].get<double>()};
            return out;
        };
        const auto hue = pair("hue", 0, 360), sat = pair("sat", 0, 1), lum = pair("lum", 0, 1);
        pl.soft = static_cast<float>(std::clamp(q.value("softness", 0.1), 0.0, 0.5));
        pl.sLo = static_cast<float>(std::min(sat[0], sat[1]));
        pl.sHi = static_cast<float>(std::max(sat[0], sat[1]));
        pl.lLo = static_cast<float>(std::min(lum[0], lum[1]));
        pl.lHi = static_cast<float>(std::max(lum[0], lum[1]));
        pl.invert = q.value("invert", false);
        pl.showMask = q.value("show_mask", false);
        pl.qShift = static_cast<float>(std::clamp(q.value("hue_shift", 0.0), -1.0, 1.0) * 60.0);
        pl.qSat = static_cast<float>(1.0 + std::clamp(q.value("saturation", 0.0), -1.0, 1.0));
        pl.qExp = static_cast<float>(std::pow(2.0, std::clamp(q.value("exposure", 0.0), -1.0, 1.0)));
        const double width = std::clamp(hue[1], 0.0, 360.0), hueSoft = std::max(1.0, pl.soft * 120.0);
        pl.qHue.resize(kHueBins + 1);
        for (int i = 0; i <= kHueBins; ++i) {
            const double d = hueDistance(i * 360.0 / kHueBins, hue[0]);
            pl.qHue[i] = width >= 360.0 ? 1.0f : static_cast<float>(d <= width / 2 ? 1.0 : 1.0 - smooth((d - width / 2) / hueSoft));
        }
        pl.qual = pl.showMask || pl.qShift != 0 || pl.qSat != 1 || pl.qExp != 1;
    }
    return pl;
}

void gradePixel(const Plan& pl, float& r, float& g, float& b) {
    if (pl.tone) {
        r = pl.lookup(0, r);
        g = pl.lookup(1, g);
        b = pl.lookup(2, b);
    }
    if (pl.saturation != 1.0f) {
        const float l = kLr * r + kLg * g + kLb * b;
        r = l + (r - l) * pl.saturation;
        g = l + (g - l) * pl.saturation;
        b = l + (b - l) * pl.saturation;
    }
    if (!pl.hsl && !pl.qual) return;
    r = std::max(r, 0.0f);
    g = std::max(g, 0.0f);
    b = std::max(b, 0.0f);
    float h, s, v;
    rgbToHsv(r, g, b, h, s, v);
    if (pl.hsl && s > 0) {
        const float shift = Plan::hueTable(pl.hShift, h), sat = Plan::hueTable(pl.hSat, h), lum = Plan::hueTable(pl.hLum, h);
        // Greys have no hue: weight every change by how colourful the pixel is.
        const float w = std::min(1.0f, s * 2.0f);
        h += shift * w;
        v *= 1.0f + lum * 0.5f * w;
        s = std::clamp(s * (1.0f + (sat - 1.0f) * w), 0.0f, 1.0f);
    }
    if (pl.qual) {
        const float luma = kLr * r + kLg * g + kLb * b;
        float m = Plan::hueTable(pl.qHue, h) * rangeMask(s, pl.sLo, pl.sHi, pl.soft) * rangeMask(luma, pl.lLo, pl.lHi, pl.soft);
        if (pl.invert) m = 1.0f - m;
        if (pl.showMask) {
            r = g = b = m;
            return;
        }
        if (m > 0) {
            h += pl.qShift * m;
            s = std::clamp(s * (1.0f + (pl.qSat - 1.0f) * m), 0.0f, 1.0f);
            v *= 1.0f + (pl.qExp - 1.0f) * m;
        }
    }
    hsvToRgb(h, s, v, r, g, b);
}

}  // namespace

const std::vector<HslBand>& hslBands() {
    static const std::vector<HslBand> bands{
        {"red", "Đỏ", 0},      {"orange", "Cam", 30},   {"yellow", "Vàng", 60},   {"green", "Xanh lá", 120},
        {"aqua", "Lơ", 180},   {"blue", "Xanh dương", 225}, {"purple", "Tím", 275}, {"magenta", "Hồng tím", 320},
    };
    return bands;
}

std::vector<float> sampleCurve(const Json& points, int n) {
    std::vector<std::pair<double, double>> pts;
    if (points.is_array())
        for (const auto& pt : points)
            if (pt.is_array() && pt.size() == 2 && pt[0].is_number() && pt[1].is_number())
                pts.emplace_back(std::clamp(pt[0].get<double>(), 0.0, 1.0), std::clamp(pt[1].get<double>(), 0.0, 1.0));
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end(), [](auto& a, auto& b) { return std::abs(a.first - b.first) < 1e-4; }),
              pts.end());
    if (pts.empty() || pts.front().first > 0) pts.insert(pts.begin(), {0.0, 0.0});
    if (pts.back().first < 1) pts.emplace_back(1.0, 1.0);
    // Monotone cubic Hermite (Fritsch-Carlson): no overshoot between points.
    const std::size_t m = pts.size();
    std::vector<double> d(m - 1), t(m);
    for (std::size_t i = 0; i + 1 < m; ++i) d[i] = (pts[i + 1].second - pts[i].second) / (pts[i + 1].first - pts[i].first);
    t[0] = d[0];
    t[m - 1] = d[m - 2];
    for (std::size_t i = 1; i + 1 < m; ++i) t[i] = d[i - 1] * d[i] <= 0 ? 0.0 : (d[i - 1] + d[i]) / 2;
    for (std::size_t i = 0; i + 1 < m; ++i) {
        if (d[i] == 0) {
            t[i] = t[i + 1] = 0;
            continue;
        }
        const double a = t[i] / d[i], b = t[i + 1] / d[i], s = a * a + b * b;
        if (s > 9) {
            const double k = 3 / std::sqrt(s);
            t[i] = k * a * d[i];
            t[i + 1] = k * b * d[i];
        }
    }
    std::vector<float> out(std::max(n, 2));
    std::size_t seg = 0;
    for (int i = 0; i < static_cast<int>(out.size()); ++i) {
        const double x = static_cast<double>(i) / (out.size() - 1);
        while (seg + 2 < m && x > pts[seg + 1].first) ++seg;
        const double x0 = pts[seg].first, x1 = pts[seg + 1].first, h = x1 - x0;
        const double u = std::clamp((x - x0) / h, 0.0, 1.0), u2 = u * u, u3 = u2 * u;
        const double y = (2 * u3 - 3 * u2 + 1) * pts[seg].second + (u3 - 2 * u2 + u) * h * t[seg] +
                         (-2 * u3 + 3 * u2) * pts[seg + 1].second + (u3 - u2) * h * t[seg + 1];
        out[i] = static_cast<float>(std::clamp(y, 0.0, 1.0));
    }
    return out;
}

std::array<double, 3> wheelToBalance(double angleDeg, double distance) {
    const double a = angleDeg * kPi / 180.0, d = std::clamp(distance, 0.0, 1.0);
    std::array<double, 3> c{d * std::cos(a), d * std::cos(a - 2 * kPi / 3), d * std::cos(a - 4 * kPi / 3)};
    const double l = kLr * c[0] + kLg * c[1] + kLb * c[2];  // keep brightness: the ring does that
    for (auto& v : c) v -= l;
    return c;
}

void balanceToWheel(const std::array<double, 3>& c, double* angleDeg, double* distance) {
    const double x = (2 * c[0] - c[1] - c[2]) / 3.0, y = (c[1] - c[2]) / std::sqrt(3.0);
    if (distance) *distance = std::min(1.0, std::hypot(x, y));
    if (angleDeg) {
        double a = std::atan2(y, x) * 180.0 / kPi;
        *angleDeg = a < 0 ? a + 360.0 : a;
    }
}

bool isNeutralGrade(const Json& p) {
    if (!p.is_object()) return true;
    // A qualifier selection is kept even before anything is adjusted inside it.
    if (auto q = p.find("qualifier"); q != p.end() && q->is_object() && q->value("enabled", true)) return false;
    VideoFrame probe;
    probe.width = 16;
    probe.height = 16;
    probe.rgba.resize(16 * 16 * 4);
    for (int i = 0; i < 256; ++i) {  // a spread of colours and greys
        probe.rgba[i * 4] = static_cast<std::uint8_t>((i * 37) % 256);
        probe.rgba[i * 4 + 1] = static_cast<std::uint8_t>((i * 91 + 40) % 256);
        probe.rgba[i * 4 + 2] = static_cast<std::uint8_t>((i * 53 + 80) % 256);
        probe.rgba[i * 4 + 3] = 255;
    }
    const auto before = probe.rgba;
    applyColorGrade(probe, p);
    return probe.rgba == before;
}

void applyColorGrade(VideoFrame& f, const Json& p) {
    if (f.empty() || !p.is_object()) return;
    const Plan pl = makePlan(p, f.deep() ? 4096 : 255);
    if (!pl.tone && pl.saturation == 1.0f && !pl.hsl && !pl.qual) return;
    const std::size_t pixels = static_cast<std::size_t>(f.width) * f.height;
    if (f.deep()) {
        detail::parallelRows(f.height, pixels, [&](int y0, int y1) {
            for (std::size_t i = static_cast<std::size_t>(y0) * f.width * 4; i < static_cast<std::size_t>(y1) * f.width * 4; i += 4) {
                std::uint16_t* px = f.half.data() + i;
                float r = halfToFloat(px[0]), g = halfToFloat(px[1]), b = halfToFloat(px[2]);
                gradePixel(pl, r, g, b);
                px[0] = floatToHalf(std::clamp(r, 0.0f, 1.0f));
                px[1] = floatToHalf(std::clamp(g, 0.0f, 1.0f));
                px[2] = floatToHalf(std::clamp(b, 0.0f, 1.0f));
            }
        });
        return;
    }
    // 8-bit: the tone table is exact at 256 entries.
    const bool toneOnly = pl.saturation == 1.0f && !pl.hsl && !pl.qual;
    std::uint8_t lut8[3][256];
    if (toneOnly)
        for (int c = 0; c < 3; ++c)
            for (int v = 0; v < 256; ++v) lut8[c][v] = static_cast<std::uint8_t>(std::lround(pl.table[c][v] * 255.0f));
    detail::parallelRows(f.height, pixels, [&](int y0, int y1) {
        for (std::size_t i = static_cast<std::size_t>(y0) * f.width * 4; i < static_cast<std::size_t>(y1) * f.width * 4; i += 4) {
            std::uint8_t* px = f.rgba.data() + i;
            if (toneOnly) {
                px[0] = lut8[0][px[0]];
                px[1] = lut8[1][px[1]];
                px[2] = lut8[2][px[2]];
                continue;
            }
            float r = px[0] / 255.0f, g = px[1] / 255.0f, b = px[2] / 255.0f;
            gradePixel(pl, r, g, b);
            px[0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            px[1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            px[2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    });
}

std::shared_ptr<const GpuGrade> gpuGrade(const Json& params, const Json& lookParams, const Json& keyParams) {
    static std::mutex mu;
    static std::vector<std::pair<std::string, std::shared_ptr<const GpuGrade>>> cache;  // most recent first
    const std::string key = params.dump() + "|" + lookParams.dump() + "|" + keyParams.dump();
    {
        std::lock_guard lk(mu);
        for (std::size_t i = 0; i < cache.size(); ++i)
            if (cache[i].first == key) {
                auto hit = cache[i].second;
                std::rotate(cache.begin(), cache.begin() + static_cast<std::ptrdiff_t>(i), cache.begin() + static_cast<std::ptrdiff_t>(i) + 1);
                return hit;
            }
    }
    constexpr int kSteps = 4096;
    const bool graded = !params.is_null();
    const Plan pl = makePlan(graded ? params : Json::object(), kSteps);
    auto g = std::make_shared<GpuGrade>();
    g->params = params;
    g->lookParams = lookParams;
    g->keyParams = keyParams;
    constexpr int W = GpuGrade::kTableWidth;
    g->table.assign(static_cast<std::size_t>(W) * GpuGrade::kTableRows * 4, 0.0f);
    for (int x = 0; x < W; ++x) {
        float* t = g->table.data() + static_cast<std::size_t>(x) * 4;
        const int i = std::min(x, kSteps + 1);
        for (int c = 0; c < 3; ++c)
            t[c] = pl.tone ? pl.table[c][i] : std::min(1.0f, static_cast<float>(i) / kSteps);
        t[3] = 1.0f;
    }
    for (int x = 0; x < W; ++x) {
        float* t = g->table.data() + (static_cast<std::size_t>(W) + x) * 4;
        const int i = std::min(x, kHueBins);
        t[0] = pl.hsl ? pl.hShift[i] : 0.0f;
        t[1] = pl.hsl ? pl.hSat[i] : 1.0f;
        t[2] = pl.hsl ? pl.hLum[i] : 0.0f;
        t[3] = pl.qual ? pl.qHue[i] : 0.0f;
    }
    const LookGpu look = lookParams.is_null() ? LookGpu{} : lookGpu(lookParams, kSteps);
    for (int x = 0; x < W; ++x) {
        float* t = g->table.data() + (static_cast<std::size_t>(W) * 2 + x) * 4;
        const int i = std::min(x, kSteps);
        for (int c = 0; c < 3; ++c)
            t[c] = look.active ? look.curve[c][static_cast<std::size_t>(i)] : static_cast<float>(i) / kSteps;
        t[3] = 1.0f;
    }
    const float u[16] = {pl.saturation, pl.tone ? 1.0f : 0.0f, pl.hsl ? 1.0f : 0.0f, pl.qual ? 1.0f : 0.0f,
                         pl.sLo, pl.sHi, pl.lLo, pl.lHi,
                         pl.soft, pl.invert ? 1.0f : 0.0f, pl.showMask ? 1.0f : 0.0f, pl.qShift,
                         pl.qSat, pl.qExp, graded ? 1.0f : 0.0f, 0.0f};
    std::copy(u, u + 16, g->u);
    if (look.active) std::copy(look.u, look.u + 16, g->u + 16);
    if (!keyParams.is_null()) {
        const cliptools::KeyGpu k = cliptools::chromaKeyGpu(keyParams);
        std::copy(k.u, k.u + 8, g->u + 32);
    }
    std::lock_guard lk(mu);
    cache.insert(cache.begin(), {key, g});
    if (cache.size() > 16) cache.pop_back();
    return g;
}

Json withGradeEffect(const Json& effects, const Json& params, const std::string& effectId) {
    Json out = Json::array();
    bool exists = false;
    if (effects.is_array())
        for (const auto& e : effects)
            exists = exists || (e.is_object() && e.value("type", std::string{}) == kColorGradeEffect);
    bool placed = params.is_null();
    const Json fx = params.is_null() ? Json()
                                     : Json{{"effect_id", effectId}, {"type", kColorGradeEffect}, {"enabled", true}, {"params", params}};
    if (effects.is_array()) {
        for (const auto& e : effects) {
            const std::string type = e.is_object() ? e.value("type", std::string{}) : std::string{};
            if (type == kColorGradeEffect) {
                if (!placed) {  // keep its place and id
                    Json kept = fx;
                    kept["effect_id"] = e.value("effect_id", effectId);
                    kept["enabled"] = e.value("enabled", true);
                    out.push_back(std::move(kept));
                    placed = true;
                }
                continue;
            }
            // A new grade goes right after the leading Log conversion / LUT effects.
            if (!placed && !exists && type != kColorSpaceEffect && type != kLutEffect) {
                out.push_back(fx);
                placed = true;
            }
            out.push_back(e);
        }
    }
    if (!placed) out.push_back(fx);
    return out;
}

}  // namespace vstudio
