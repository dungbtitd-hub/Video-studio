#include "vstudio/clip_tools.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "detail/parallel.hpp"
#include "detail/paths.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/pixel.hpp"
#include "vstudio/project_io.hpp"

namespace vstudio::cliptools {

bool isFreeze(const Clip& c) {
    auto it = c.extra.find("freeze");
    return it != c.extra.end() && it->is_boolean() && it->get<bool>();
}

// ---------------------------------------------------------------- chroma key

namespace {

struct Rgb {
    double r = 0, g = 1, b = 0;
};

Rgb parseColor(const Json& j) {
    Rgb c;
    if (j.is_string()) {
        const std::string s = j.get<std::string>();
        if (s.size() == 7 && s[0] == '#') {
            const unsigned v = static_cast<unsigned>(std::stoul(s.substr(1), nullptr, 16));
            c.r = ((v >> 16) & 255) / 255.0;
            c.g = ((v >> 8) & 255) / 255.0;
            c.b = (v & 255) / 255.0;
        }
    } else if (j.is_array() && j.size() >= 3) {
        c.r = j[0].get<double>() / 255.0;
        c.g = j[1].get<double>() / 255.0;
        c.b = j[2].get<double>() / 255.0;
    }
    return c;
}

// BT.709 chroma (Cb, Cr) of an RGB colour.
inline void chroma(double r, double g, double b, double& cb, double& cr) {
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    cb = (b - y) / 1.8556;
    cr = (r - y) / 1.5748;
}

inline double smoothstep(double a, double b, double x) {
    if (b <= a) return x < a ? 0.0 : 1.0;
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

}  // namespace

Json defaultChromaKey() { return Json{{"color", "#00ff00"}, {"similarity", 0.4}, {"smoothness", 0.1}, {"spill", 0.5}}; }

void applyChromaKey(VideoFrame& f, const Json& p) {
    if (f.empty() || f.deep()) return;
    const Rgb key = parseColor(p.value("color", Json("#00ff00")));
    double kcb, kcr;
    chroma(key.r, key.g, key.b, kcb, kcr);
    const double klen = std::max(1e-6, std::hypot(kcb, kcr));
    const double ux = kcb / klen, uy = kcr / klen;
    // Similarity 0..1 is a chroma distance relative to the key colour's own saturation.
    const double sim = std::clamp(p.value("similarity", 0.4), 0.0, 1.0) * klen;
    const double smooth = std::clamp(p.value("smoothness", 0.1), 0.0, 1.0) * klen;
    const double spill = std::clamp(p.value("spill", 0.5), 0.0, 1.0);
    const std::size_t w = static_cast<std::size_t>(f.width);
    detail::parallelRows(f.height, w * f.height, [&](int y0, int y1) {
    std::uint8_t* px = f.rgba.data() + static_cast<std::size_t>(y0) * w * 4;
    const std::size_t n = static_cast<std::size_t>(y1 - y0) * w;
    for (std::size_t i = 0; i < n; ++i, px += 4) {
        const double r = px[0] / 255.0, g = px[1] / 255.0, b = px[2] / 255.0;
        double cb, cr;
        chroma(r, g, b, cb, cr);
        const double d = std::hypot(cb - kcb, cr - kcr);
        const double a = smoothstep(sim, sim + smooth + 1e-4, d);
        double nr = r, ng = g, nb = b;
        if (spill > 0) {
            // Removes the key's tint from what is kept (green fringe on hair and edges).
            const double proj = cb * ux + cr * uy;
            if (proj > 0) {
                const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                const double k = spill * proj;
                const double ncb = cb - k * ux, ncr = cr - k * uy;
                nr = y + 1.5748 * ncr;
                nb = y + 1.8556 * ncb;
                ng = (y - 0.2126 * nr - 0.0722 * nb) / 0.7152;
            }
        }
        px[0] = static_cast<std::uint8_t>(std::clamp(nr, 0.0, 1.0) * 255 + 0.5);
        px[1] = static_cast<std::uint8_t>(std::clamp(ng, 0.0, 1.0) * 255 + 0.5);
        px[2] = static_cast<std::uint8_t>(std::clamp(nb, 0.0, 1.0) * 255 + 0.5);
        px[3] = static_cast<std::uint8_t>(px[3] * a + 0.5);
    }
    });
}

std::string guessKeyColor(const VideoFrame& f) {
    if (f.empty() || f.deep()) return "#00ff00";
    // Average of the saturated pixels on the frame border, by hue bucket; the biggest bucket wins.
    double sum[12][3] = {};
    int count[12] = {};
    auto take = [&](int x, int y) {
        const std::uint8_t* p = &f.rgba[(static_cast<std::size_t>(y) * f.width + x) * 4];
        const int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
        if (mx < 40 || mx - mn < 50) return;
        double h;
        if (mx == p[0]) h = std::fmod((p[1] - p[2]) / double(mx - mn), 6.0);
        else if (mx == p[1]) h = (p[2] - p[0]) / double(mx - mn) + 2;
        else h = (p[0] - p[1]) / double(mx - mn) + 4;
        const int bucket = (static_cast<int>(std::floor((h < 0 ? h + 6 : h) * 2)) + 12) % 12;
        for (int k = 0; k < 3; ++k) sum[bucket][k] += p[k];
        ++count[bucket];
    };
    const int step = std::max(1, std::min(f.width, f.height) / 64);
    for (int x = 0; x < f.width; x += step) take(x, 0), take(x, f.height - 1);
    for (int y = 0; y < f.height; y += step) take(0, y), take(f.width - 1, y);
    const int best = static_cast<int>(std::max_element(count, count + 12) - count);
    if (count[best] == 0) return "#00ff00";
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", int(sum[best][0] / count[best]), int(sum[best][1] / count[best]),
                  int(sum[best][2] / count[best]));
    return buf;
}

// ---------------------------------------------------------------- stabilisation

namespace {

// Gradient of the mean intensity of each row and column: brightness changes cancel out,
// edges (what moves) remain.
void profiles(const VideoFrame& f, std::vector<double>& rows, std::vector<double>& cols) {
    rows.assign(static_cast<std::size_t>(f.height), 0.0);
    cols.assign(static_cast<std::size_t>(f.width), 0.0);
    for (int y = 0; y < f.height; ++y) {
        const std::uint8_t* p = &f.rgba[static_cast<std::size_t>(y) * f.width * 4];
        for (int x = 0; x < f.width; ++x, p += 4) {
            const double l = 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2];
            rows[static_cast<std::size_t>(y)] += l;
            cols[static_cast<std::size_t>(x)] += l;
        }
    }
    for (auto& v : rows) v /= std::max(1, f.width);
    for (auto& v : cols) v /= std::max(1, f.height);
    auto grad = [](std::vector<double>& v) {
        for (std::size_t i = 0; i + 1 < v.size(); ++i) v[i] = v[i + 1] - v[i];
        if (!v.empty()) v.back() = 0;
    };
    grad(rows);
    grad(cols);
}

// Shift of `cur` against `prev` (content moved by +s), sub-pixel; searched within +-range.
double bestShift(const std::vector<double>& prev, const std::vector<double>& cur, int range) {
    const int n = static_cast<int>(prev.size());
    if (n < 8) return 0;
    range = std::min(range, n / 4);
    std::vector<double> cost(static_cast<std::size_t>(2 * range + 1));
    for (int s = -range; s <= range; ++s) {
        double sum = 0;
        int cnt = 0;
        for (int i = range; i < n - range; ++i) {
            const double d = cur[static_cast<std::size_t>(i + s)] - prev[static_cast<std::size_t>(i)];
            sum += d * d;
            ++cnt;
        }
        cost[static_cast<std::size_t>(s + range)] = cnt ? sum / cnt : 1e30;
    }
    const int bi = static_cast<int>(std::min_element(cost.begin(), cost.end()) - cost.begin());
    double s = bi - range;
    if (bi > 0 && bi < 2 * range) {  // parabola through the three costs around the minimum
        const double a = cost[static_cast<std::size_t>(bi - 1)], b = cost[static_cast<std::size_t>(bi)],
                     c = cost[static_cast<std::size_t>(bi + 1)];
        const double den = a - 2 * b + c;
        if (den > 1e-12) s += 0.5 * (a - c) / den;
    }
    return s;
}

double sigmaFor(double strength) { return 0.15 + 0.85 * std::clamp(strength, 0.0, 1.0); }  // seconds

// Smoothed camera path at t (Gaussian over +-3 sigma) minus the raw path.
void correction(const Json& path, double t, double sigma, double& dx, double& dy) {
    dx = dy = 0;
    const std::size_t n = path.size();
    if (n == 0) return;
    auto tAt = [&](std::size_t i) { return path[i][0].get<double>(); };
    // First sample at or after t - 3 sigma (binary search).
    std::size_t lo = 0, hi = n;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (tAt(mid) < t - 3 * sigma) lo = mid + 1;
        else hi = mid;
    }
    double sw = 0, sx = 0, sy = 0;
    for (std::size_t i = lo; i < n && tAt(i) <= t + 3 * sigma; ++i) {
        const double u = (tAt(i) - t) / sigma, w = std::exp(-0.5 * u * u);
        sw += w;
        sx += w * path[i][1].get<double>();
        sy += w * path[i][2].get<double>();
    }
    // Raw path at t (linear between samples).
    std::size_t k = 0;
    {
        std::size_t a = 0, b = n;
        while (a < b) {
            const std::size_t mid = (a + b) / 2;
            if (tAt(mid) <= t) a = mid + 1;
            else b = mid;
        }
        k = a;
    }
    double rx, ry;
    if (k == 0) rx = path[0][1].get<double>(), ry = path[0][2].get<double>();
    else if (k >= n) rx = path[n - 1][1].get<double>(), ry = path[n - 1][2].get<double>();
    else {
        const double t0 = tAt(k - 1), t1 = tAt(k), u = t1 > t0 ? (t - t0) / (t1 - t0) : 0;
        rx = path[k - 1][1].get<double>() + (path[k][1].get<double>() - path[k - 1][1].get<double>()) * u;
        ry = path[k - 1][2].get<double>() + (path[k][2].get<double>() - path[k - 1][2].get<double>()) * u;
    }
    if (sw <= 0) return;
    dx = rx - sx / sw;
    dy = ry - sy / sw;
}

}  // namespace

std::vector<ShakeSample> measureShake(const std::function<std::optional<VideoFrame>()>& next, const std::atomic<bool>* cancel,
                                      const std::function<void(double)>& progress, double expectedFrames) {
    std::vector<ShakeSample> out;
    std::vector<double> prow, pcol, row, col;
    double x = 0, y = 0;
    for (int i = 0;; ++i) {
        if (cancel && cancel->load()) return {};
        auto f = next();
        if (!f || f->empty()) break;
        if (f->deep()) convertFrame(*f, PixelFormat::RGBA8);
        profiles(*f, row, col);
        if (i > 0 && row.size() == prow.size() && col.size() == pcol.size()) {
            x += bestShift(pcol, col, std::max(4, f->width / 12)) / f->width;
            y += bestShift(prow, row, std::max(4, f->height / 12)) / f->height;
        }
        out.push_back({f->pts.seconds(), x, y});
        prow.swap(row);
        pcol.swap(col);
        if (progress && expectedFrames > 0) progress(std::min(1.0, (i + 1) / expectedFrames));
    }
    return out;
}

std::vector<ShakeSample> analyzeClipShake(IMediaDecoder& d, const Clip& c, Rational fps, const std::atomic<bool>* cancel,
                                          const std::function<void(double)>& progress) {
    if (fps.num <= 0) fps = Rational{30, 1};
    const std::int64_t first = c.sourceIn.toFrame(fps, Rounding::Up), last = c.sourceOut.toFrame(fps, Rounding::Up);
    std::int64_t i = first;
    d.setMaxOutputSize(320, 320);
    return measureShake(
        [&]() -> std::optional<VideoFrame> {
            if (i >= last) return std::nullopt;
            const Time t = Time::fromFrame(i++, fps);
            auto f = d.videoFrameAt(t);
            if (f) f->pts = t;
            return f;
        },
        cancel, progress, static_cast<double>(last - first));
}

Json stabilizeParams(const std::vector<ShakeSample>& path, double strength) {
    Json arr = Json::array();
    auto r5 = [](double v) { return std::round(v * 1e5) / 1e5; };
    for (const auto& s : path) arr.push_back(Json::array({r5(s.t), r5(s.x), r5(s.y)}));
    Json p{{"strength", std::clamp(strength, 0.0, 1.0)}, {"path", arr}};
    // Zoom that hides the moved edges for the largest correction (capped at 25 %).
    double m = 0;
    const double sigma = sigmaFor(strength);
    for (const auto& s : path) {
        double dx, dy;
        correction(p["path"], s.t, sigma, dx, dy);
        m = std::max({m, std::abs(dx), std::abs(dy)});
    }
    p["zoom"] = r5(std::min(1.25, 1.0 + 2.0 * m));
    return p;
}

StabilizeCorrection stabilizeAt(const Json& p, double t) {
    StabilizeCorrection c;
    if (!p.contains("path") || !p["path"].is_array()) return c;
    c.zoom = std::clamp(p.value("zoom", 1.0), 1.0, 1.25);
    correction(p["path"], t, sigmaFor(p.value("strength", 0.7)), c.dx, c.dy);
    // Never move further than the zoom margin allows.
    const double lim = (c.zoom - 1.0) / 2.0;
    c.dx = std::clamp(c.dx, -lim, lim);
    c.dy = std::clamp(c.dy, -lim, lim);
    return c;
}

namespace {

class ChromaKeyEffect final : public IVideoEffect {
public:
    void apply(VideoFrame& f, const Json& p, Time) const override { applyChromaKey(f, p); }
};

class StabilizeEffect final : public IVideoEffect {
public:
    void apply(VideoFrame& f, const Json& p, Time) const override {
        if (f.empty() || f.deep()) return;
        const double t = Time::fromTicks(p.value("source_time", std::int64_t{0})).seconds();
        const StabilizeCorrection c = stabilizeAt(p, t);
        if (c.zoom == 1.0 && c.dx == 0 && c.dy == 0) return;
        const int w = f.width, h = f.height;
        std::vector<std::uint8_t> out(f.rgba.size());
        const double cx = w / 2.0, cy = h / 2.0;
        detail::parallelRows(h, static_cast<std::size_t>(w) * h, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
            const double sy = cy + (y + 0.5 - cy) / c.zoom + c.dy * h - 0.5;
            const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, h - 1), y1 = std::min(y0 + 1, h - 1);
            const double fy = std::clamp(sy - y0, 0.0, 1.0);
            for (int x = 0; x < w; ++x) {
                const double sx = cx + (x + 0.5 - cx) / c.zoom + c.dx * w - 0.5;
                const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, w - 1), x1 = std::min(x0 + 1, w - 1);
                const double fx = std::clamp(sx - x0, 0.0, 1.0);
                const std::uint8_t* a = &f.rgba[(static_cast<std::size_t>(y0) * w + x0) * 4];
                const std::uint8_t* b = &f.rgba[(static_cast<std::size_t>(y0) * w + x1) * 4];
                const std::uint8_t* cc = &f.rgba[(static_cast<std::size_t>(y1) * w + x0) * 4];
                const std::uint8_t* d = &f.rgba[(static_cast<std::size_t>(y1) * w + x1) * 4];
                std::uint8_t* o = &out[(static_cast<std::size_t>(y) * w + x) * 4];
                for (int k = 0; k < 4; ++k) {
                    const double top = a[k] + (b[k] - a[k]) * fx, bot = cc[k] + (d[k] - cc[k]) * fx;
                    o[k] = static_cast<std::uint8_t>(top + (bot - top) * fy + 0.5);
                }
            }
        }
        });
        f.rgba.swap(out);
    }
};

}  // namespace

// ---------------------------------------------------------------- favourites

Favorites::Favorites(std::string path) : path_(std::move(path)) {
    std::ifstream in(detail::toPath(path_), std::ios::binary);
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        const Json j = Json::parse(ss.str());
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it->is_array())
                for (const auto& id : *it)
                    if (id.is_string()) items_[it.key()].push_back(id.get<std::string>());
    } catch (...) {
        items_.clear();  // broken file: start empty, rewritten on the next change
    }
}

bool Favorites::contains(const std::string& category, const std::string& id) const {
    auto it = items_.find(category);
    return it != items_.end() && std::find(it->second.begin(), it->second.end(), id) != it->second.end();
}

bool Favorites::toggle(const std::string& category, const std::string& id) {
    auto& v = items_[category];
    auto it = std::find(v.begin(), v.end(), id);
    const bool on = it == v.end();
    if (on) v.insert(v.begin(), id);
    else v.erase(it);
    save();
    return on;
}

std::vector<std::string> Favorites::list(const std::string& category) const {
    auto it = items_.find(category);
    return it == items_.end() ? std::vector<std::string>{} : it->second;
}

Json Favorites::toJson() const {
    Json j = Json::object();
    for (const auto& [k, v] : items_) j[k] = v;
    return j;
}

void Favorites::save() const {
    if (path_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(detail::toPath(path_).parent_path(), ec);
    try {
        writeFileAtomic(path_, toJson().dump(1));
    } catch (...) {
        // Favourites are a convenience: a read-only disk must not break the editor.
    }
}

void registerClipToolModules(ModuleRegistry& r) {
    r.add<IVideoEffect>({.id = kChromaKeyEffect, .displayName = "Chroma key", .capabilities = {"effect.key"}},
                        [] { return std::make_shared<ChromaKeyEffect>(); });
    r.add<IVideoEffect>({.id = kStabilizeEffect, .displayName = "Stabilise", .capabilities = {"effect.motion"}},
                        [] { return std::make_shared<StabilizeEffect>(); });
}

}  // namespace vstudio::cliptools
