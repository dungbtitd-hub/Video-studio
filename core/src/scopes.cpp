#include "vstudio/scopes.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "vstudio/pixel.hpp"

namespace vstudio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint8_t kBg[3] = {14, 16, 20};

// Calls fn(x, y, r, g, b) for a grid of at most ~150k samples (values 0..1).
template <class Fn>
void forSamples(const VideoFrame& f, Fn&& fn) {
    if (f.empty()) return;
    const double px = static_cast<double>(f.width) * f.height;
    const int step = std::max(1, static_cast<int>(std::ceil(std::sqrt(px / 150000.0))));
    for (int y = step / 2; y < f.height; y += step)
        for (int x = step / 2; x < f.width; x += step) {
            const std::size_t i = (static_cast<std::size_t>(y) * f.width + x) * 4;
            if (f.deep())
                fn(x, y, std::clamp(halfToFloat(f.half[i]), 0.0f, 1.0f), std::clamp(halfToFloat(f.half[i + 1]), 0.0f, 1.0f),
                   std::clamp(halfToFloat(f.half[i + 2]), 0.0f, 1.0f));
            else
                fn(x, y, f.rgba[i] / 255.0f, f.rgba[i + 1] / 255.0f, f.rgba[i + 2] / 255.0f);
        }
}

struct Canvas {
    ScopeImage img;
    Canvas(int w, int h) {
        img.width = w;
        img.height = h;
        img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
            img.rgba[i] = kBg[0];
            img.rgba[i + 1] = kBg[1];
            img.rgba[i + 2] = kBg[2];
            img.rgba[i + 3] = 255;
        }
    }
    void add(int x, int y, float r, float g, float b) {  // additive, clamped
        if (x < 0 || y < 0 || x >= img.width || y >= img.height) return;
        std::uint8_t* p = img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
        p[0] = static_cast<std::uint8_t>(std::min(255.0f, p[0] + r * 255.0f));
        p[1] = static_cast<std::uint8_t>(std::min(255.0f, p[1] + g * 255.0f));
        p[2] = static_cast<std::uint8_t>(std::min(255.0f, p[2] + b * 255.0f));
    }
    void hline(int y, int x0, int x1, float a) {
        for (int x = x0; x < x1; ++x) add(x, y, a, a, a);
    }
    void vline(int x, int y0, int y1, float a) {
        for (int y = y0; y < y1; ++y) add(x, y, a, a, a);
    }
};

// Density -> brightness on a log scale, so faint traces still show.
float density(std::uint32_t n, std::uint32_t mx) {
    if (!n || !mx) return 0;
    return static_cast<float>(0.25 + 0.75 * std::log1p(static_cast<double>(n)) / std::log1p(static_cast<double>(mx)));
}

void waveform(const VideoFrame& f, Canvas& c, bool parade) {
    const int w = c.img.width, h = c.img.height;
    const int lanes = parade ? 3 : 1, laneW = w / lanes;
    std::vector<std::uint32_t> acc(static_cast<std::size_t>(w) * h * (parade ? 1 : 1));
    forSamples(f, [&](int x, int, float r, float g, float b) {
        const float v[3] = {r, g, b};
        if (!parade) {
            const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            const int cx = x * laneW / f.width, cy = static_cast<int>((1.0f - l) * (h - 1) + 0.5f);
            ++acc[static_cast<std::size_t>(cy) * w + cx];
            return;
        }
        for (int k = 0; k < 3; ++k) {
            const int cx = k * laneW + x * (laneW - 2) / f.width + 1, cy = static_cast<int>((1.0f - v[k]) * (h - 1) + 0.5f);
            ++acc[static_cast<std::size_t>(cy) * w + cx];
        }
    });
    std::uint32_t mx = 0;
    for (auto n : acc) mx = std::max(mx, n);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float d = density(acc[static_cast<std::size_t>(y) * w + x], mx);
            if (d <= 0) continue;
            if (!parade) {
                c.add(x, y, d * 0.75f, d, d * 0.8f);
                continue;
            }
            const int k = std::min(2, x / std::max(1, laneW));
            c.add(x, y, k == 0 ? d : d * 0.2f, k == 1 ? d : d * 0.2f, k == 2 ? d : d * 0.25f);
        }
    for (int i = 0; i <= 4; ++i) c.hline(std::min(h - 1, i * (h - 1) / 4), 0, w, i == 0 || i == 4 ? 0.16f : 0.08f);
    if (parade)
        for (int k = 1; k < 3; ++k) c.vline(k * laneW, 0, h, 0.16f);
}

void vectorscope(const VideoFrame& f, Canvas& c) {
    const int w = c.img.width, h = c.img.height, side = std::min(w, h) - 4;
    const double cx = w / 2.0, cy = h / 2.0, rad = side / 2.0;
    // Cb/Cr in -0.5..0.5 -> the circle (radius = 0.5 / 0.9 so 100% saturation stays inside).
    auto toXY = [&](double cb, double cr) { return std::array<double, 2>{cx + cb * rad * 1.8, cy - cr * rad * 1.8}; };
    std::vector<std::uint32_t> acc(static_cast<std::size_t>(w) * h);
    std::vector<std::array<float, 3>> col(acc.size(), {0, 0, 0});
    forSamples(f, [&](int, int, float r, float g, float b) {
        const double cb = -0.1146 * r - 0.3854 * g + 0.5 * b, cr = 0.5 * r - 0.4542 * g - 0.0458 * b;
        const auto p = toXY(cb, cr);
        const int x = static_cast<int>(p[0]), y = static_cast<int>(p[1]);
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const std::size_t i = static_cast<std::size_t>(y) * w + x;
        ++acc[i];
        col[i][0] += r;
        col[i][1] += g;
        col[i][2] += b;
    });
    // Graticule: outer circle, cross, skin-tone line, 75% colour targets.
    for (int a = 0; a < 720; ++a) {
        const double t = a * kPi / 360.0;
        c.add(static_cast<int>(cx + std::cos(t) * rad), static_cast<int>(cy - std::sin(t) * rad), 0.12f, 0.12f, 0.12f);
    }
    c.hline(static_cast<int>(cy), static_cast<int>(cx - rad), static_cast<int>(cx + rad), 0.06f);
    c.vline(static_cast<int>(cx), static_cast<int>(cy - rad), static_cast<int>(cy + rad), 0.06f);
    {
        // Skin tones of every complexion lie near one line (about 123 degrees from +Cb).
        const double t = 123.0 * kPi / 180.0;
        for (int s = 0; s < static_cast<int>(rad); ++s)
            c.add(static_cast<int>(cx + std::cos(t) * s), static_cast<int>(cy - std::sin(t) * s), 0.14f, 0.11f, 0.08f);
    }
    const float targets[6][3] = {{0.75f, 0, 0}, {0.75f, 0.75f, 0}, {0, 0.75f, 0}, {0, 0.75f, 0.75f}, {0, 0, 0.75f}, {0.75f, 0, 0.75f}};
    for (const auto& t : targets) {
        const double cb = -0.1146 * t[0] - 0.3854 * t[1] + 0.5 * t[2], cr = 0.5 * t[0] - 0.4542 * t[1] - 0.0458 * t[2];
        const auto p = toXY(cb, cr);
        for (int d = -4; d <= 4; ++d) {
            c.add(static_cast<int>(p[0]) + d, static_cast<int>(p[1]) - 4, t[0] * 0.4f, t[1] * 0.4f, t[2] * 0.4f);
            c.add(static_cast<int>(p[0]) + d, static_cast<int>(p[1]) + 4, t[0] * 0.4f, t[1] * 0.4f, t[2] * 0.4f);
            c.add(static_cast<int>(p[0]) - 4, static_cast<int>(p[1]) + d, t[0] * 0.4f, t[1] * 0.4f, t[2] * 0.4f);
            c.add(static_cast<int>(p[0]) + 4, static_cast<int>(p[1]) + d, t[0] * 0.4f, t[1] * 0.4f, t[2] * 0.4f);
        }
    }
    std::uint32_t mx = 0;
    for (auto n : acc) mx = std::max(mx, n);
    for (std::size_t i = 0; i < acc.size(); ++i) {
        if (!acc[i]) continue;
        const float d = density(acc[i], mx), n = static_cast<float>(acc[i]);
        // Each dot in its own colour, brightened so dark colours still read.
        float r = col[i][0] / n, g = col[i][1] / n, b = col[i][2] / n;
        const float m = std::max({r, g, b, 0.05f});
        r = 0.35f + 0.65f * r / m;
        g = 0.35f + 0.65f * g / m;
        b = 0.35f + 0.65f * b / m;
        c.add(static_cast<int>(i % w), static_cast<int>(i / w), r * d, g * d, b * d);
    }
}

void histogram(const VideoFrame& f, Canvas& c) {
    const Histogram hst = computeHistogram(f);
    const int w = c.img.width, h = c.img.height;
    std::uint32_t mx = 1;
    for (int i = 1; i < 255; ++i)  // ignore clipped ends when scaling, they still draw (capped)
        mx = std::max({mx, hst.r[i], hst.g[i], hst.b[i]});
    auto height = [&](std::uint32_t n) { return std::min(h - 1, static_cast<int>(static_cast<double>(n) / mx * (h - 2))); };
    for (int x = 0; x < w; ++x) {
        const int bin = std::min(255, x * 256 / w);
        const int hr = height(hst.r[bin]), hg = height(hst.g[bin]), hb = height(hst.b[bin]), hl = height(hst.luma[bin]);
        for (int y = 0; y < hr; ++y) c.add(x, h - 1 - y, 0.42f, 0.0f, 0.0f);
        for (int y = 0; y < hg; ++y) c.add(x, h - 1 - y, 0.0f, 0.42f, 0.0f);
        for (int y = 0; y < hb; ++y) c.add(x, h - 1 - y, 0.0f, 0.0f, 0.48f);
        c.add(x, h - 1 - hl, 0.8f, 0.8f, 0.8f);
    }
    for (int i = 1; i < 4; ++i) c.vline(i * w / 4, 0, h, 0.07f);
}

}  // namespace

Histogram computeHistogram(const VideoFrame& f) {
    Histogram h;
    h.r.assign(256, 0);
    h.g.assign(256, 0);
    h.b.assign(256, 0);
    h.luma.assign(256, 0);
    forSamples(f, [&](int, int, float r, float g, float b) {
        ++h.r[static_cast<int>(r * 255.0f + 0.5f)];
        ++h.g[static_cast<int>(g * 255.0f + 0.5f)];
        ++h.b[static_cast<int>(b * 255.0f + 0.5f)];
        ++h.luma[static_cast<int>((0.2126f * r + 0.7152f * g + 0.0722f * b) * 255.0f + 0.5f)];
    });
    return h;
}

ScopeImage renderScope(const VideoFrame& frame, ScopeKind kind, int width, int height) {
    Canvas c(std::clamp(width, 32, 2048), std::clamp(height, 32, 2048));
    switch (kind) {
        case ScopeKind::Waveform: waveform(frame, c, false); break;
        case ScopeKind::Parade: waveform(frame, c, true); break;
        case ScopeKind::Vectorscope: vectorscope(frame, c); break;
        case ScopeKind::Histogram: histogram(frame, c); break;
    }
    return std::move(c.img);
}

const char* scopeName(ScopeKind k) {
    switch (k) {
        case ScopeKind::Waveform: return "Dạng sóng (Waveform)";
        case ScopeKind::Parade: return "RGB Parade";
        case ScopeKind::Vectorscope: return "Vectorscope";
        case ScopeKind::Histogram: return "Histogram";
    }
    return "";
}

}  // namespace vstudio
