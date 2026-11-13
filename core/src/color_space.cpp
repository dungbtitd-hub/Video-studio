#include "vstudio/color_space.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "detail/paths.hpp"
#include "vstudio/pixel.hpp"

namespace vstudio {

namespace {

using Mat3 = std::array<std::array<double, 3>, 3>;

struct Chroma {
    double x, y;
};
struct Gamut {
    Chroma r, g, b;
};
constexpr Chroma kD65{0.3127, 0.3290};
constexpr Gamut kRec709{{0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}};
constexpr Gamut kRec2020{{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}};
constexpr Gamut kSGamut3Cine{{0.766, 0.275}, {0.225, 0.800}, {0.089, -0.087}};
constexpr Gamut kVGamut{{0.730, 0.280}, {0.165, 0.840}, {0.100, -0.030}};
constexpr Gamut kCinemaGamut{{0.740, 0.270}, {0.170, 1.140}, {0.080, -0.100}};
constexpr Gamut kDGamut{{0.710, 0.310}, {0.210, 0.880}, {0.090, -0.080}};

Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 m{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) m[i][j] += a[i][k] * b[k][j];
    return m;
}

Mat3 inverse(const Mat3& m) {
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                       m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    Mat3 r{};
    r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    return r;
}

// RGB -> XYZ from primaries and white point (SMPTE RP 177).
Mat3 rgbToXyz(const Gamut& g, Chroma w = kD65) {
    auto xyz = [](Chroma c) { return std::array<double, 3>{c.x / c.y, 1.0, (1 - c.x - c.y) / c.y}; };
    const auto r = xyz(g.r), gg = xyz(g.g), b = xyz(g.b), W = xyz(w);
    const Mat3 p{{{r[0], gg[0], b[0]}, {r[1], gg[1], b[1]}, {r[2], gg[2], b[2]}}};
    const Mat3 inv = inverse(p);
    double s[3];
    for (int i = 0; i < 3; ++i) s[i] = inv[i][0] * W[0] + inv[i][1] * W[1] + inv[i][2] * W[2];
    Mat3 m = p;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[i][j] *= s[j];
    return m;
}

Mat3 toRec709(const Gamut& from) { return mul(inverse(rgbToXyz(kRec709)), rgbToXyz(from)); }

// Camera curves take the 10-bit code value / 1023 (legal-range video: our RGB
// 0..1 is code 64..940); Apple Log and HLG are defined on the normalised signal.
double codeValue(double v) { return (64.0 + v * 876.0) / 1023.0; }

double decodeSLog3(double v) {
    const double x = codeValue(v) * 1023.0;
    if (x >= 171.2102946929) return std::pow(10.0, (x - 420.0) / 261.5) * (0.18 + 0.01) - 0.01;
    return (x - 95.0) * 0.01125 / (171.2102946929 - 95.0);
}
double decodeVLog(double v) {
    const double x = codeValue(v);
    if (x < 0.181) return (x - 0.125) / 5.6;
    return std::pow(10.0, (x - 0.598206) / 0.241514) - 0.00873;
}
double decodeCLog3(double v) {
    const double x = codeValue(v);
    double y;
    if (x < 0.097465473) y = -(std::pow(10.0, (0.12783901 - x) / 0.36726845) - 1.0) / 14.98325;
    else if (x <= 0.15277891) y = (x - 0.12512219) / 1.9754798;
    else y = (std::pow(10.0, (x - 0.12240537) / 0.36726845) - 1.0) / 14.98325;
    return y * 0.9;  // Canon's linear is relative to 90% white
}
double decodeNLog(double v) {
    const double x = codeValue(v) * 1023.0;
    if (x < 452.0) return std::pow(x / 650.0, 3.0) - 0.0075;
    return std::exp((x - 619.0) / 150.0);
}
double decodeDLog(double v) {
    const double x = codeValue(v);
    if (x <= 0.14) return (x - 0.0929) / 6.025;
    return (std::pow(10.0, 3.89616 * x - 2.27752) - 0.0108) / 0.9892;
}
double decodeAppleLog(double p) {
    constexpr double r0 = -0.05641088, rt = 0.01, c = 47.28711236, beta = 0.00964052, gamma = 0.08550479,
                     delta = 0.69336945;
    const double pt = c * (rt - r0) * (rt - r0);
    if (p >= pt) return std::pow(2.0, (p - delta) / gamma) - beta;
    if (p >= 0) return std::sqrt(p / c) + r0;
    return r0;
}
double decodeHlg(double e) {
    constexpr double a = 0.17883277, b = 0.28466892, c = 0.55991073;
    const double lin = e <= 0.5 ? e * e / 3.0 : (std::exp((e - c) / a) + b) / 12.0;
    return lin / 0.2647;  // HLG reference (75%) white -> 1.0, grey stays near 0.18
}

struct Format {
    LogFormat info;
    double (*decode)(double);
    Gamut gamut;
};

const std::vector<Format>& formats() {
    static const std::vector<Format> f = {
        {{"slog3", "Sony S-Log3 / S-Gamut3.Cine", "Sony"}, decodeSLog3, kSGamut3Cine},
        {{"vlog", "Panasonic V-Log / V-Gamut", "Panasonic"}, decodeVLog, kVGamut},
        {{"clog3", "Canon Log 3 / Cinema Gamut", "Canon"}, decodeCLog3, kCinemaGamut},
        {{"nlog", "Nikon N-Log (Rec.2020)", "Nikon"}, decodeNLog, kRec2020},
        {{"applelog", "Apple Log (iPhone, Rec.2020)", "Apple"}, decodeAppleLog, kRec2020},
        {{"dlog", "DJI D-Log / D-Gamut", "DJI"}, decodeDLog, kDGamut},
        {{"hlg", "HLG (HDR, Rec.2020)", "HDR"}, decodeHlg, kRec2020},
    };
    return f;
}

const Format* findFormat(const std::string& id) {
    for (const auto& f : formats())
        if (f.info.id == id) return &f;
    return nullptr;
}

double rec709Encode(double l) { return l < 0.018 ? 4.5 * l : 1.099 * std::pow(l, 0.45) - 0.099; }

// Slope-1 at the knee, approaches 1 smoothly: highlights roll off instead of clipping.
double shoulder(double l) {
    constexpr double knee = 0.6;
    if (l <= knee) return l;
    return knee + (1.0 - knee) * (1.0 - std::exp(-(l - knee) / (1.0 - knee)));
}

}  // namespace

const std::vector<LogFormat>& logFormats() {
    static const std::vector<LogFormat> out = [] {
        std::vector<LogFormat> v;
        for (const auto& f : formats()) v.push_back(f.info);
        return v;
    }();
    return out;
}

const LogFormat* findLogFormat(const std::string& id) {
    const Format* f = findFormat(id);
    return f ? &f->info : nullptr;
}

std::string suggestedLogFormat(const MediaAsset& a) {
    const std::string t = a.extra.value("transfer", std::string{});
    if (t == "arib-std-b67") return "hlg";
    return {};
}

void applyColorSpace(VideoFrame& f, const Json& p) {
    const Format* fmt = findFormat(p.value("input", std::string{}));
    if (!fmt || f.empty()) return;
    const double gain = std::pow(2.0, std::clamp(p.value("exposure", 0.0), -4.0, 4.0));
    const bool soft = p.value("highlights", true);
    // Per-channel decode as a table over the signal (4097 entries, interpolated), the
    // matrix in float, then display encoding as a table over l/(l+1) (covers 0..inf).
    constexpr int kN = 4096;
    std::vector<float> dec(kN + 2), enc(kN + 2);
    for (int i = 0; i <= kN; ++i) {
        dec[i] = static_cast<float>(fmt->decode(static_cast<double>(i) / kN));
        const double t = std::min(static_cast<double>(i) / kN, 0.99999);
        const double l = t / (1.0 - t);  // inverse of l/(l+1)
        const double s = soft ? shoulder(l) : std::min(l, 1.0);
        enc[i] = static_cast<float>(rec709Encode(std::clamp(s, 0.0, 1.0)));
    }
    dec[kN + 1] = dec[kN];
    enc[kN + 1] = enc[kN];
    const Mat3 m = toRec709(fmt->gamut);
    float mf[9];
    for (int i = 0; i < 9; ++i) mf[i] = static_cast<float>(m[i / 3][i % 3] * gain);
    auto lookup = [](const std::vector<float>& t, float v) {
        const float pos = std::clamp(v, 0.0f, 1.0f) * kN;
        const int i = static_cast<int>(pos);
        return t[i] + (t[i + 1] - t[i]) * (pos - i);
    };
    auto px = [&](float& r, float& g, float& b) {
        const float lr = lookup(dec, r), lg = lookup(dec, g), lb = lookup(dec, b);
        float o[3];
        for (int k = 0; k < 3; ++k) {
            const float l = std::max(0.0f, mf[k * 3] * lr + mf[k * 3 + 1] * lg + mf[k * 3 + 2] * lb);
            o[k] = lookup(enc, l / (l + 1.0f));
        }
        r = o[0];
        g = o[1];
        b = o[2];
    };
    if (f.deep()) {
        for (std::size_t i = 0; i + 3 < f.half.size(); i += 4) {
            float r = halfToFloat(f.half[i]), g = halfToFloat(f.half[i + 1]), b = halfToFloat(f.half[i + 2]);
            px(r, g, b);
            f.half[i] = floatToHalf(r);
            f.half[i + 1] = floatToHalf(g);
            f.half[i + 2] = floatToHalf(b);
        }
        return;
    }
    for (std::size_t i = 0; i + 3 < f.rgba.size(); i += 4) {
        float r = f.rgba[i] / 255.0f, g = f.rgba[i + 1] / 255.0f, b = f.rgba[i + 2] / 255.0f;
        px(r, g, b);
        f.rgba[i] = static_cast<std::uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
        f.rgba[i + 1] = static_cast<std::uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
        f.rgba[i + 2] = static_cast<std::uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
    }
}

// ------------------------------------------------------------------ LUT

void Lut3D::apply(float& r, float& g, float& b) const {
    const int n = size;
    float in[3] = {r, g, b};
    int i0[3], i1[3];
    float t[3];
    for (int k = 0; k < 3; ++k) {
        const float span = domainMax[k] - domainMin[k];
        const float v = std::clamp((in[k] - domainMin[k]) / (span > 0 ? span : 1.0f), 0.0f, 1.0f) * (n - 1);
        i0[k] = std::min(static_cast<int>(v), n - 2);
        i1[k] = i0[k] + 1;
        t[k] = v - i0[k];
    }
    auto at = [&](int ri, int gi, int bi, int c) {
        return rgb[(static_cast<std::size_t>(bi) * n * n + static_cast<std::size_t>(gi) * n + ri) * 3 + c];
    };
    float out[3];
    for (int c = 0; c < 3; ++c) {
        const float c00 = at(i0[0], i0[1], i0[2], c) + (at(i1[0], i0[1], i0[2], c) - at(i0[0], i0[1], i0[2], c)) * t[0];
        const float c10 = at(i0[0], i1[1], i0[2], c) + (at(i1[0], i1[1], i0[2], c) - at(i0[0], i1[1], i0[2], c)) * t[0];
        const float c01 = at(i0[0], i0[1], i1[2], c) + (at(i1[0], i0[1], i1[2], c) - at(i0[0], i0[1], i1[2], c)) * t[0];
        const float c11 = at(i0[0], i1[1], i1[2], c) + (at(i1[0], i1[1], i1[2], c) - at(i0[0], i1[1], i1[2], c)) * t[0];
        const float c0 = c00 + (c10 - c00) * t[1], c1 = c01 + (c11 - c01) * t[1];
        out[c] = c0 + (c1 - c0) * t[2];
    }
    r = out[0];
    g = out[1];
    b = out[2];
}

std::shared_ptr<const Lut3D> parseCubeLut(const std::string& text, std::string* error) {
    auto fail = [&](const std::string& m) -> std::shared_ptr<const Lut3D> {
        if (error) *error = m;
        return nullptr;
    };
    auto lut = std::make_shared<Lut3D>();
    std::istringstream in(text);
    std::string line;
    std::size_t expected = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream ls(line.substr(first));
        if (std::isalpha(static_cast<unsigned char>(line[first]))) {
            std::string key;
            ls >> key;
            if (key == "TITLE") {
                std::getline(ls, lut->title);
                const auto q0 = lut->title.find('"'), q1 = lut->title.rfind('"');
                lut->title = q0 != std::string::npos && q1 > q0 ? lut->title.substr(q0 + 1, q1 - q0 - 1) : lut->title;
            } else if (key == "LUT_3D_SIZE") {
                ls >> lut->size;
                if (lut->size < 2 || lut->size > 256) return fail("unsupported LUT size");
                expected = static_cast<std::size_t>(lut->size) * lut->size * lut->size;
                lut->rgb.reserve(expected * 3);
            } else if (key == "LUT_1D_SIZE") {
                return fail("1D LUTs are not supported (use a 3D .cube)");
            } else if (key == "DOMAIN_MIN") {
                ls >> lut->domainMin[0] >> lut->domainMin[1] >> lut->domainMin[2];
            } else if (key == "DOMAIN_MAX") {
                ls >> lut->domainMax[0] >> lut->domainMax[1] >> lut->domainMax[2];
            }  // other keywords (LUT_IN_VIDEO_RANGE, ...) are ignored
            continue;
        }
        if (!expected) return fail("data before LUT_3D_SIZE");
        float r, g, b;
        if (!(ls >> r >> g >> b)) return fail("bad LUT line: " + line);
        lut->rgb.insert(lut->rgb.end(), {r, g, b});
        if (lut->rgb.size() > expected * 3) return fail("too many LUT entries");
    }
    if (!expected) return fail("not a 3D .cube LUT");
    if (lut->rgb.size() != expected * 3) return fail("LUT is incomplete");
    return lut;
}

std::shared_ptr<const Lut3D> loadCubeLut(const std::string& path, std::string* error) {
    std::ifstream f(detail::toPath(path), std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return nullptr;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (ss.str().size() > (64u << 20)) {
        if (error) *error = "LUT file too large";
        return nullptr;
    }
    return parseCubeLut(ss.str(), error);
}

std::shared_ptr<const Lut3D> cachedCubeLut(const std::string& path, std::string* error) {
    static std::mutex mu;
    static std::map<std::string, std::pair<std::filesystem::file_time_type, std::shared_ptr<const Lut3D>>> cache;
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(detail::toPath(path), ec);
    {
        std::lock_guard lk(mu);
        if (auto it = cache.find(path); it != cache.end() && !ec && it->second.first == mtime) return it->second.second;
    }
    auto lut = loadCubeLut(path, error);
    if (lut && !ec) {
        std::lock_guard lk(mu);
        if (cache.size() > 32) cache.clear();
        cache[path] = {mtime, lut};
    }
    return lut;
}

std::string resolveResourcePath(const std::string& path, const std::string& base) {
    if (base.empty() || !detail::toPath(path).is_relative()) return path;
    return detail::fromPath(detail::toPath(base) / detail::toPath(path));
}

void applyLut(VideoFrame& f, const Json& p) {
    std::string path = p.value("path", std::string{});
    if (path.empty() || f.empty()) return;
    const float st = static_cast<float>(std::clamp(p.value("strength", 1.0), 0.0, 1.0));
    if (st <= 0) return;
    path = resolveResourcePath(path, p.value("base", std::string{}));
    std::string err;
    auto lut = cachedCubeLut(path, &err);
    if (!lut) throw std::runtime_error(err);
    auto mix = [&](float& r, float& g, float& b) {
        float lr = r, lg = g, lb = b;
        lut->apply(lr, lg, lb);
        r += (lr - r) * st;
        g += (lg - g) * st;
        b += (lb - b) * st;
    };
    if (f.deep()) {
        for (std::size_t i = 0; i + 3 < f.half.size(); i += 4) {
            float r = halfToFloat(f.half[i]), g = halfToFloat(f.half[i + 1]), b = halfToFloat(f.half[i + 2]);
            mix(r, g, b);
            f.half[i] = floatToHalf(r);
            f.half[i + 1] = floatToHalf(g);
            f.half[i + 2] = floatToHalf(b);
        }
        return;
    }
    for (std::size_t i = 0; i + 3 < f.rgba.size(); i += 4) {
        float r = f.rgba[i] / 255.0f, g = f.rgba[i + 1] / 255.0f, b = f.rgba[i + 2] / 255.0f;
        mix(r, g, b);
        f.rgba[i] = static_cast<std::uint8_t>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
        f.rgba[i + 1] = static_cast<std::uint8_t>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
        f.rgba[i + 2] = static_cast<std::uint8_t>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
    }
}

}  // namespace vstudio
