#include "vstudio/overlays.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

#include "sticker_kit.hpp"
#include "vstudio/motion.hpp"
#include "vstudio/pixel.hpp"

namespace vstudio::overlays {

namespace {

using namespace stickers::kit;

// Stable random number in [0, 1) for particle `i`, property `k` (no state: any frame on its own).
double rnd(unsigned seed, std::int64_t i, int k) {
    std::uint64_t x = (seed + 1) * 0x9E3779B97F4A7C15ull;
    x ^= static_cast<std::uint64_t>(i) * 0xBF58476D1CE4E5B9ull + 0x632BE59BD9B4E019ull;
    x ^= static_cast<std::uint64_t>(k + 1) * 0x94D049BB133111EBull;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
}

double smooth(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

// What an effect draws with: the region (w x h canvas px centred on the origin), the size unit
// `u` (canvas px of a 1080-line frame, times the "size" setting) and the clock.
struct Field {
    const D& d;
    double w, h, u, speed, density, t;
    unsigned seed;
    Color tint;
    bool tinted;
    double unitScale = 1;  // canvas px per 1080-line px (without the size setting)

    [[nodiscard]] double r(std::int64_t i, int k) const { return rnd(seed, i, k); }
    // Particle count for `base` particles on a full 16:9 frame, scaled by the region's area.
    [[nodiscard]] int count(double base) const {
        const double fh = 1080.0 * unitScale, fw = fh * 16 / 9;
        return std::clamp(static_cast<int>(std::lround(base * density * (w / fw) * (h / fh))), 0, 3000);
    }
    // Position along an axis of length `len` (centred) moving at `v` px/s, wrapping with margin `mg`
    // outside the region, so particles leave and come back unseen.
    [[nodiscard]] double wrap(double r0, double v, double len, double mg) const {
        const double span = len + 2 * mg;
        return -len / 2 - mg + frac(r0 + t * v / span) * span;
    }
    [[nodiscard]] Color col(const char* hex, float a = 1) const { return tinted ? tint.withAlpha(a) : hx(hex, a); }
    // True when a particle of radius `rad` (local px) at (x, y) is outside the frame: skip it.
    [[nodiscard]] bool off(double x, double y, double rad) const {
        const Pt p = d.m.apply({x, y});
        const double rr = rad * d.m.scaleFactor() + 2;
        return p.x + rr < 0 || p.y + rr < 0 || p.x - rr > d.cv.width() || p.y - rr > d.cv.height();
    }
};

// Soft round dot: bright centre fading to nothing at `r`.
void softDot(const D& d, double x, double y, double r, Color c) { d.rad(circ(x, y, r), {x, y}, r, c, c.withAlpha(0)); }

// Soft, large shapes (fog, light leaks, vignettes) are drawn at 1/`div` resolution and blended up
// with bilinear filtering: they have no detail to lose, and this is many times cheaper.
void softLayer(VideoFrame& out, const Affine& place, int div, const std::function<void(VideoFrame&, const Affine&)>& draw) {
    VideoFrame low;
    low.allocate(out.width / div + 2, out.height / div + 2);
    draw(low, Affine::scale(1.0 / div) * place);
    std::vector<int> xi(static_cast<std::size_t>(out.width));
    std::vector<float> xf(static_cast<std::size_t>(out.width));
    for (int x = 0; x < out.width; ++x) {
        const double sx = std::max(0.0, (x + 0.5) / div - 0.5);
        xi[static_cast<std::size_t>(x)] = std::min(static_cast<int>(sx), low.width - 2);
        xf[static_cast<std::size_t>(x)] = static_cast<float>(sx - xi[static_cast<std::size_t>(x)]);
    }
    // Premultiplied floats once per low pixel, then four taps per output pixel.
    std::vector<float> pm(static_cast<std::size_t>(low.width) * low.height * 4);
    for (std::size_t i = 0; i < pm.size(); i += 4) {
        const float al = low.rgba[i + 3] * (1 / 255.f);
        for (int k = 0; k < 3; ++k) pm[i + k] = low.rgba[i + k] * (1 / 255.f) * al;
        pm[i + 3] = al;
    }
    for (int y = 0; y < out.height; ++y) {
        const double sy = std::max(0.0, (y + 0.5) / div - 0.5);
        const int iy = std::min(static_cast<int>(sy), low.height - 2);
        const float fy = static_cast<float>(sy - iy);
        const float* r0 = pm.data() + static_cast<std::size_t>(iy) * low.width * 4;
        const float* r1 = r0 + static_cast<std::size_t>(low.width) * 4;
        for (int x = 0; x < out.width; ++x) {
            const int i = xi[static_cast<std::size_t>(x)] * 4;
            if (r0[i + 3] + r0[i + 7] + r1[i + 3] + r1[i + 7] <= 0) continue;
            const float fx = xf[static_cast<std::size_t>(x)];
            const float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
            float c[4];
            for (int k = 0; k < 4; ++k) c[k] = r0[i + k] * w00 + r0[i + 4 + k] * w10 + r1[i + k] * w01 + r1[i + 4 + k] * w11;
            const float a = c[3];
            if (a <= 0.001f) continue;
            const std::size_t o = (static_cast<std::size_t>(y) * out.width + x) * 4;
            if (out.deep()) {
                const float da = halfToFloat(out.half[o + 3]) * (1 - a), oa = a + da;
                for (int k = 0; k < 3; ++k) out.half[o + k] = floatToHalf((c[k] + halfToFloat(out.half[o + k]) * da) / oa);
                out.half[o + 3] = floatToHalf(oa);
                continue;
            }
            std::uint8_t* d = out.rgba.data() + o;
            if (d[3] == 0) {
                const float inv = 255.f / a;
                for (int k = 0; k < 3; ++k) d[k] = static_cast<std::uint8_t>(std::min(255.f, c[k] * inv + 0.5f));
                d[3] = static_cast<std::uint8_t>(a * 255.f + 0.5f);
                continue;
            }
            const float da = d[3] * (1 / 255.f) * (1 - a), oa = a + da, inv = 255.f / oa;
            for (int k = 0; k < 3; ++k) d[k] = static_cast<std::uint8_t>(std::min(255.f, (c[k] + d[k] * (1 / 255.f) * da) * inv + 0.5f));
            d[3] = static_cast<std::uint8_t>(oa * 255.f + 0.5f);
        }
    }
}

// ---------------------------------------------------------------- weather

void snowField(const Field& f, double base, double wind) {
    const int n = f.count(base);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);  // depth: near flakes are bigger, faster, brighter
        const double rad = (4.5 + 11 * z * z + f.r(i, 3) * 2.5) * f.u;
        const double v = (38 + 120 * z) * f.u * f.speed;
        const double y = f.wrap(f.r(i, 1), v, f.h, rad * 2);
        const double sway = 22 * f.u * (0.5 + z) * std::sin(f.t * (0.5 + 0.7 * f.r(i, 4)) + kTau * f.r(i, 5));
        const double x = f.wrap(f.r(i, 0), wind * f.u * (0.4 + 0.6 * z) * f.speed, f.w, rad * 2 + 40 * f.u) + sway;
        if (f.off(x, y, rad)) continue;
        softDot(f.d, x, y, rad, f.col("#ffffff", static_cast<float>(0.5 + 0.45 * z)));
    }
}

Path flakePath() {
    Path p;
    for (int a = 0; a < 6; ++a) {
        const double ang = a * kPi / 3;
        const double cs = std::cos(ang), sn = std::sin(ang);
        auto rot = [&](double x, double y) { return Pt{x * cs - y * sn, x * sn + y * cs}; };
        const Pt o = rot(0, 0), tip = rot(0, -1);
        p.moveTo(o.x, o.y).lineTo(tip.x, tip.y);
        for (const double at : {0.42, 0.68}) {
            const double len = at < 0.5 ? 0.3 : 0.22;
            const Pt b = rot(0, -at), l = rot(-len, -at - len), rr = rot(len, -at - len);
            p.moveTo(l.x, l.y).lineTo(b.x, b.y).lineTo(rr.x, rr.y);
        }
    }
    return p;
}

void snowflakes(const Field& f) {
    static const Path flake = flakePath();
    const int n = f.count(46);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double s = (13 + 26 * z) * f.u;
        const double v = (30 + 60 * z) * f.u * f.speed;
        const double y = f.wrap(f.r(i, 1), v, f.h, s * 1.3);
        const double x = -f.w / 2 + f.r(i, 0) * f.w +
                         34 * f.u * std::sin(f.t * (0.35 + 0.4 * f.r(i, 4)) + kTau * f.r(i, 5));
        if (f.off(x, y, s * 1.3)) continue;
        const double spin = (f.r(i, 6) < 0.5 ? -1 : 1) * (0.25 + 0.5 * f.r(i, 7));
        const D p = f.d.at(Affine::translate(x, y) * Affine::rotate(f.t * spin + kTau * f.r(i, 8)) * Affine::scale(s));
        const float a = static_cast<float>(0.6 + 0.35 * z);
        softDot(p, 0, 0, 1.25, f.col("#dff1ff", 0.22f * a));
        p.stroke(flake, 0.11, f.col("#ffffff", a));
    }
}

void rain(const Field& f) {
    const int n = f.count(240);
    constexpr double slant = 0.14;
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double len = (34 + 64 * z) * f.u;
        const double v = (950 + 850 * z) * f.u * f.speed;
        const double y = f.wrap(f.r(i, 1), v, f.h, len);
        const double x = -f.w / 2 - slant * f.h + f.r(i, 0) * (f.w + slant * f.h) + slant * (y + f.h / 2);
        if (f.off(x, y, len)) continue;
        const Pt head{x, y}, tail{x - slant * len, y - len};
        const Color c = f.col("#d6e8ff", static_cast<float>(0.25 + 0.4 * z));
        f.d.cv.stroke(line(tail, head), (1.0 + 1.6 * z) * f.u, Paint::linear(f.d.m.apply(tail), f.d.m.apply(head), c.withAlpha(0), c), f.d.m);
    }
}

void fogBlobs(const Field& f);
void fog(const Field& f, VideoFrame& out) {
    softLayer(out, f.d.m, 8, [&](VideoFrame& low, const Affine& m) {
        vec::Canvas cv(low);
        const D d{cv, m, f.t};
        fogBlobs(Field{d, f.w, f.h, f.u, f.speed, f.density, f.t, f.seed, f.tint, f.tinted, f.unitScale});
    });
}

void fogBlobs(const Field& f) {
    const int n = std::max(8, f.count(24));
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double rad = (220 + 260 * z) * f.u;
        const double x = f.wrap(f.r(i, 0), (10 + 22 * z) * f.u * f.speed, f.w, rad);
        const double y = -f.h / 2 + (0.2 + 0.85 * std::sqrt(f.r(i, 1))) * f.h + 18 * f.u * std::sin(f.t * 0.2 + kTau * f.r(i, 3));
        if (f.off(x, y, rad)) continue;
        const double breath = 0.75 + 0.25 * std::sin(f.t * (0.15 + 0.1 * f.r(i, 4)) + kTau * f.r(i, 5));
        softDot(f.d, x, y, rad, f.col("#eef3f7", static_cast<float>(0.2 * breath)));
    }
}

// Something that falls while tumbling (petals, leaves, confetti): calls `draw(d, i, z)` placed at
// its position, turned, and squashed as it flips over (sx in -1..1).
void tumbling(const Field& f, int n, double sizeMin, double sizeMax, double vMin, double vMax, double wind, double swayPx,
              const std::function<void(const D&, int, double, double)>& draw) {
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double s = (sizeMin + (sizeMax - sizeMin) * z) * f.u;
        const double v = (vMin + (vMax - vMin) * z) * f.u * f.speed;
        const double y = f.wrap(f.r(i, 1), v, f.h, s * 1.5);
        const double sway = swayPx * f.u * std::sin(f.t * (0.6 + 0.6 * f.r(i, 4)) + kTau * f.r(i, 5));
        const double x = f.wrap(f.r(i, 0), wind * f.u * f.speed, f.w, s * 1.5 + swayPx * f.u) + sway;
        if (f.off(x, y, s * 1.5)) continue;
        const double spin = (f.r(i, 6) < 0.5 ? -1 : 1) * (0.4 + 0.9 * f.r(i, 7));
        const double flip = std::cos(f.t * (1.2 + 1.8 * f.r(i, 8)) + kTau * f.r(i, 9));
        const double sx = (std::abs(flip) * 0.8 + 0.2) * (flip < 0 ? -1 : 1);
        const D p = f.d.at(Affine::translate(x, y) * Affine::rotate(f.t * spin + kTau * f.r(i, 10)) * Affine::scale(s * sx, s));
        draw(p, i, z, flip);
    }
}

void petals(const Field& f) {
    static const Path petal = [] {
        Path p = petalShape(1.0, 0.62, 0.15);
        Path q;
        q.append(p, Affine::translate(0, 0.5));
        return q;
    }();
    tumbling(f, f.count(56), 16, 30, 38, 85, 38, 28, [&](const D& p, int, double, double flip) {
        const float a = 0.95f;
        const Color tip = mix(hx("#ffc2d9", a), hx("#ff8fb8", a), static_cast<float>(0.5 - 0.5 * flip));
        p.lin(petal, {0, 0.5}, {0, -0.5}, hx("#fff0f5", a), tip);
    });
}

void apricot(const Field& f) {  // hoa mai: yellow five-petal flowers and loose petals
    static const Path petal = petalShape(1.0, 0.72, 0.0);
    tumbling(f, f.count(30), 14, 26, 30, 70, 22, 30, [&](const D& p, int i, double, double) {
        if (f.r(i, 11) < 0.45) {
            for (int k = 0; k < 5; ++k)
                p.at(Affine::rotate(k * kTau / 5)).lin(petal, {0, 0}, {0, -1}, hx("#ffb703"), hx("#ffe066"));
            p.fill(circ(0, 0, 0.24), hx("#e76f00"));
            for (int k = 0; k < 6; ++k) {
                const double a = k * kTau / 6 + 0.3;
                p.fill(circ(0.36 * std::cos(a), 0.36 * std::sin(a), 0.06), hx("#b5520a"));
            }
        } else {
            p.at(Affine::translate(0, 0.5) * Affine::scale(0.8)).lin(petal, {0, 0}, {0, -1}, hx("#ffb703"), hx("#ffe58a"));
        }
    });
}

void leaves(const Field& f) {
    static const Path leaf = [] {
        Path p;
        p.moveTo(0, 0.55);
        p.cubicTo(0.55, 0.25, 0.45, -0.3, 0, -0.55);
        p.cubicTo(-0.45, -0.3, -0.55, 0.25, 0, 0.55);
        p.close();
        return p;
    }();
    static const Path vein = [] {
        Path p;
        p.moveTo(0, 0.7).lineTo(0, -0.42);
        p.moveTo(0, 0.1).lineTo(0.22, -0.12);
        p.moveTo(0, 0.1).lineTo(-0.22, -0.12);
        p.moveTo(0, 0.32).lineTo(0.24, 0.12);
        p.moveTo(0, 0.32).lineTo(-0.24, 0.12);
        return p;
    }();
    static const char* colors[] = {"#e85d04", "#dc2f02", "#f48c06", "#ffba08", "#b5651d", "#c1121f"};
    tumbling(f, f.count(26), 28, 52, 50, 110, 30, 45, [&](const D& p, int i, double, double flip) {
        const Color c = hx(colors[static_cast<int>(f.r(i, 11) * 6) % 6]);
        const Color shade = mix(c, hx("#3a1505"), static_cast<float>(0.15 + 0.2 * (0.5 - 0.5 * flip)));
        p.lin(leaf, {-0.4, -0.4}, {0.4, 0.5}, c, shade);
        p.stroke(vein, 0.05, mix(c, hx("#5a2408"), 0.45f).withAlpha(0.8f));
    });
}

// ---------------------------------------------------------------- party

void confetti(const Field& f) {
    static const char* colors[] = {"#ff4d6d", "#ffd23f", "#3ec1d3", "#7bd389", "#9b5de5", "#ff8c42", "#f15bb5", "#00bbf9"};
    tumbling(f, f.count(130), 13, 24, 80, 190, 10, 22, [&](const D& p, int i, double, double flip) {
        const Color c = hx(colors[static_cast<int>(f.r(i, 11) * 8) % 8]);
        const Color cc = flip < 0 ? mix(c, hx("#000000"), 0.25f) : c;
        if (f.r(i, 12) < 0.72) p.fill(rrect(-0.5, -0.25, 1.0, 0.5, 0.06), cc);
        else p.fill(circ(0, 0, 0.32), cc);
    });
}

void glitter(const Field& f) {  // kim tuyến: gold specks that fall slowly and catch the light
    const int n = f.count(110);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double s = (4 + 7 * z) * f.u;
        const double y = f.wrap(f.r(i, 1), (35 + 60 * z) * f.u * f.speed, f.h, s * 3);
        const double x = -f.w / 2 + f.r(i, 0) * f.w + 16 * f.u * std::sin(f.t * (0.7 + 0.5 * f.r(i, 4)) + kTau * f.r(i, 5));
        if (f.off(x, y, s * 3)) continue;
        const double glint = std::pow(0.5 + 0.5 * std::sin(f.t * (2.5 + 3 * f.r(i, 6)) + kTau * f.r(i, 7)), 6);
        const Color c = f.r(i, 8) < 0.7 ? f.col("#ffd166") : hx("#fff4cc");
        const D p = f.d.at(Affine::translate(x, y) * Affine::rotate(f.t * (f.r(i, 9) - 0.5) * 3) * Affine::scale(s));
        p.fill(poly({{0, -1}, {0.6, 0}, {0, 1}, {-0.6, 0}}), mix(c, hx("#a86b00"), static_cast<float>(0.35 * (1 - glint))));
        if (glint > 0.05) {
            softDot(p, 0, 0, 2.6, hx("#fff8dc", static_cast<float>(0.6 * glint)));
            p.fill(starP(0, 0, 4, 2.4 * glint + 0.5, 0.25), hx("#ffffff", static_cast<float>(glint)));
        }
    }
}

void hearts(const Field& f) {
    static const char* colors[] = {"#ff4d6d", "#ff758f", "#c9184a", "#ff8fab", "#e5383b"};
    const int n = f.count(24);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double s = (28 + 46 * z) * f.u;
        const double y = -f.wrap(f.r(i, 1), (45 + 65 * z) * f.u * f.speed, f.h, s);
        const double x = -f.w / 2 + f.r(i, 0) * f.w + 26 * f.u * std::sin(f.t * (0.7 + 0.6 * f.r(i, 4)) + kTau * f.r(i, 5));
        if (f.off(x, y, s)) continue;
        const double beat = 1 + 0.07 * std::sin(f.t * 5 + kTau * f.r(i, 6));
        const double tilt = 0.25 * std::sin(f.t * (0.8 + f.r(i, 7)) + kTau * f.r(i, 8));
        const D p = f.d.at(Affine::translate(x, y) * Affine::rotate(tilt) * Affine::scale(s * beat));
        const Color c = f.tinted ? f.tint : hx(colors[static_cast<int>(f.r(i, 9) * 5) % 5]);
        const float a = static_cast<float>(0.75 + 0.25 * z);
        p.lin(heartP(0, 0, 1.0), {0, -0.45}, {0, 0.45}, mix(c, hx("#ffffff"), 0.3f).withAlpha(a), c.withAlpha(a));
        p.fill(ell(-0.2, -0.16, 0.11, 0.07), hx("#ffffff", 0.45f * a));
    }
}

void balloons(const Field& f) {
    static const char* colors[] = {"#ff4d6d", "#ffd23f", "#3ec1d3", "#7bd389", "#9b5de5", "#ff8c42", "#f15bb5"};
    const int n = f.count(11);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double s = (46 + 40 * z) * f.u;  // balloon half height
        const double y = -f.wrap(f.r(i, 1), (50 + 45 * z) * f.u * f.speed, f.h, s * 4);
        const double x = -f.w / 2 + f.r(i, 0) * f.w + 20 * f.u * std::sin(f.t * (0.5 + 0.4 * f.r(i, 4)) + kTau * f.r(i, 5));
        if (f.off(x, y + s * 1.6, s * 4)) continue;
        const double tilt = 0.12 * std::sin(f.t * (0.7 + 0.5 * f.r(i, 6)) + kTau * f.r(i, 7));
        const D p = f.d.at(Affine::translate(x, y) * Affine::rotate(tilt) * Affine::scale(s));
        const Color c = hx(colors[static_cast<int>(f.r(i, 8) * 7) % 7]);
        // String first (behind), swaying a little later than the balloon.
        Path str;
        str.moveTo(0, 1.02);
        const double ph = f.t * 1.4 + kTau * f.r(i, 9);
        for (int k = 1; k <= 12; ++k) {
            const double v = k / 12.0;
            str.lineTo(0.16 * std::sin(ph - v * 3) * v, 1.02 + v * 2.2);
        }
        p.stroke(str, 0.035, hx("#f1f1f1", 0.85f));
        p.fill(poly({{0, 0.94}, {0.1, 1.08}, {-0.1, 1.08}}), mix(c, hx("#000000"), 0.2f));
        p.rad(ell(0, 0, 0.82, 1.0), {-0.3, -0.38}, 1.45, mix(c, hx("#ffffff"), 0.45f), mix(c, hx("#000000"), 0.18f));
        p.fill(ell(-0.33, -0.42, 0.12, 0.22), hx("#ffffff", 0.55f));
    }
}

void fireworks(const Field& f) {
    static const char* colors[] = {"#ffd23f", "#ff4d6d", "#4cc9f0", "#7bd389", "#f15bb5", "#ffffff", "#ff8c42"};
    const int bursts = std::max(2, f.count(5));
    constexpr int sparks = 36;
    constexpr double rise = 0.6, life = 2.2;
    for (int j = 0; j < bursts; ++j) {
        const double period = 2.3 + 1.0 * f.r(j, 0);
        const double tt = f.t * f.speed + f.r(j, 1) * period;
        const auto cycle = static_cast<std::int64_t>(std::floor(tt / period));
        const double ph = tt - cycle * period;
        const std::int64_t key = cycle * 131 + j;
        const double bx = -f.w / 2 + (0.14 + 0.72 * f.r(key, 2)) * f.w;
        const double by = -f.h / 2 + (0.12 + 0.42 * f.r(key, 3)) * f.h;
        const Color c = f.tinted ? f.tint : hx(colors[static_cast<int>(f.r(key, 4) * 7) % 7]);
        const Color c2 = hx(colors[static_cast<int>(f.r(key, 5) * 7) % 7]);
        if (ph < rise) {  // the rocket climbs, slowing down
            const double k = ph / rise, e = 1 - (1 - k) * (1 - k);
            const double y = f.h / 2 + 20 * f.u + (by - f.h / 2 - 20 * f.u) * e;
            const double y0 = f.h / 2 + 20 * f.u + (by - f.h / 2 - 20 * f.u) * std::max(0.0, e - 0.12);
            const Pt a{bx, y0}, b{bx, y};
            f.d.cv.stroke(line(a, b), 2.6 * f.u, Paint::linear(f.d.m.apply(a), f.d.m.apply(b), hx("#ffe9b0", 0), hx("#ffe9b0", 0.9f)), f.d.m);
            softDot(f.d, bx, y, 5 * f.u, hx("#fff4d6", 0.9f));
            continue;
        }
        const double e = ph - rise;
        if (e > life) continue;
        const double fade = std::pow(1 - e / life, 1.6);
        if (e < 0.5) softDot(f.d, bx, by, 140 * f.u, c.withAlpha(static_cast<float>(0.4 * (1 - e / 0.5))));
        const double reach = (230 + 140 * f.r(key, 6)) * f.u;
        auto at = [&](double ang, double sp, double time) {
            const double d = reach * sp * (1 - std::exp(-2.6 * time)) / (1 - std::exp(-2.6 * life));
            return Pt{bx + std::cos(ang) * d, by + std::sin(ang) * d + 55 * f.u * time * time};
        };
        for (int k = 0; k < sparks; ++k) {
            const double ang = kTau * (k + 0.4 * f.r(key * 64 + k, 7)) / sparks;
            const double sp = 0.75 + 0.25 * f.r(key * 64 + k, 8);
            const Pt head = at(ang, sp, e), tail = at(ang, sp, std::max(0.0, e - 0.3));
            const Color cc = (k % 3 == 0 ? c2 : c).withAlpha(static_cast<float>(fade));
            const double twinkle = e > 1.0 ? 0.6 + 0.4 * std::sin(f.t * 30 + k) : 1.0;
            softDot(f.d, head.x, head.y, 9 * f.u, mix(cc, hx("#ffffff"), 0.5f).withAlpha(static_cast<float>(fade * twinkle)));
            f.d.cv.stroke(line(tail, head), 4 * f.u, Paint::linear(f.d.m.apply(tail), f.d.m.apply(head), cc.withAlpha(0), cc.withAlpha(static_cast<float>(fade * twinkle))), f.d.m);
        }
    }
}

// ---------------------------------------------------------------- light

void bubbles(const Field& f) {
    const int n = f.count(30);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double rad = (15 + 38 * z) * f.u;
        const double y = -f.wrap(f.r(i, 1), (32 + 48 * z) * f.u * f.speed, f.h, rad);
        const double x = -f.w / 2 + f.r(i, 0) * f.w + 22 * f.u * std::sin(f.t * (0.8 + 0.6 * f.r(i, 4)) + kTau * f.r(i, 5));
        if (f.off(x, y, rad)) continue;
        const double wob = 0.05 * std::sin(f.t * 3 + kTau * f.r(i, 6));
        const D p = f.d.at(Affine::translate(x, y) * Affine::scale(rad * (1 + wob), rad * (1 - wob)));
        p.rad(circ(0, 0, 1), {0, 0}, 1, hx("#ffffff", 0.02f), f.col("#bfe9ff", 0.42f));
        p.stroke(circ(0, 0, 0.97), 0.05, hx("#ffffff", 0.5f));
        p.stroke(arc(0, 0, 0.8, 0.8, kPi * 0.15, kPi * 0.55), 0.07, hx("#ffd6f5", 0.45f));
        p.fill(ell(-0.38, -0.42, 0.2, 0.12), hx("#ffffff", 0.75f));
    }
}

void sparkles(const Field& f) {
    const int n = f.count(46);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double s = (13 + 26 * z) * f.u;
        const double x = -f.w / 2 + f.r(i, 0) * f.w;
        const double y = f.wrap(f.r(i, 1), -6 * f.u * f.speed, f.h, s * 2);
        const double period = 1.6 + 1.6 * f.r(i, 4);
        const double k = std::pow(std::sin(kPi * frac(f.t * f.speed / period + f.r(i, 5))), 4);
        if (k < 0.02 || f.off(x, y, s * 2)) continue;
        const D p = f.d.at(Affine::translate(x, y) * Affine::rotate(0.4 * f.t + kTau * f.r(i, 6)) * Affine::scale(s * (0.4 + 0.6 * k)));
        const Color c = f.r(i, 7) < 0.6 ? f.col("#fff1b8") : hx("#ffffff");
        softDot(p, 0, 0, 1.6, c.withAlpha(static_cast<float>(0.45 * k)));
        p.fill(starP(0, 0, 4, 1.0, 0.2), c.withAlpha(static_cast<float>(k)));
        p.fill(starP(0, 0, 4, 0.55, 0.12, -kPi / 4), hx("#ffffff", static_cast<float>(0.8 * k)));
    }
}

void fireflies(const Field& f) {
    const int n = f.count(34);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double rad = (16 + 20 * z) * f.u;
        const double t = f.t * f.speed;
        const double x = -f.w / 2 + f.r(i, 0) * f.w + 60 * f.u * std::sin(t * (0.18 + 0.12 * f.r(i, 4)) + kTau * f.r(i, 5)) +
                         25 * f.u * std::sin(t * 0.53 + kTau * f.r(i, 6));
        const double y = -f.h / 2 + f.r(i, 1) * f.h + 45 * f.u * std::sin(t * (0.15 + 0.1 * f.r(i, 7)) + kTau * f.r(i, 8)) +
                         20 * f.u * std::cos(t * 0.47 + kTau * f.r(i, 9));
        if (f.off(x, y, rad)) continue;
        const double pulse = smooth(0.5 + 0.7 * std::sin(t * (0.9 + 0.8 * f.r(i, 10)) + kTau * f.r(i, 11)));
        if (pulse < 0.02) continue;
        softDot(f.d, x, y, rad, f.col("#e9ff70", static_cast<float>(0.55 * pulse)));
        f.d.fill(circ(x, y, rad * 0.17), hx("#fffde0", static_cast<float>(pulse)));
    }
}

void bokehDiscs(const Field& f);
void bokeh(const Field& f, VideoFrame& out) {
    softLayer(out, f.d.m, 2, [&](VideoFrame& low, const Affine& m) {
        vec::Canvas cv(low);
        const D d{cv, m, f.t};
        bokehDiscs(Field{d, f.w, f.h, f.u, f.speed, f.density, f.t, f.seed, f.tint, f.tinted, f.unitScale});
    });
}

void bokehDiscs(const Field& f) {
    static const char* colors[] = {"#ffd38a", "#ff9f68", "#ffe7b3", "#f7a1c4", "#9ad0ff", "#fff3d1"};
    const int n = f.count(18);
    for (int i = 0; i < n; ++i) {
        const double z = f.r(i, 2);
        const double rad = (40 + 85 * z) * f.u;
        const double x = f.wrap(f.r(i, 0), (6 + 14 * z) * f.u * f.speed, f.w, rad);
        const double y = -f.h / 2 + f.r(i, 1) * f.h + 22 * f.u * std::sin(f.t * 0.25 + kTau * f.r(i, 4));
        if (f.off(x, y, rad)) continue;
        const double breath = 0.5 + 0.5 * std::sin(f.t * (0.3 + 0.3 * f.r(i, 5)) + kTau * f.r(i, 6));
        const float a = static_cast<float>(0.1 + 0.16 * breath);
        const Color c = f.tinted ? f.tint : hx(colors[static_cast<int>(f.r(i, 7) * 6) % 6]);
        f.d.rad(circ(x, y, rad), {x, y}, rad, c.withAlpha(a * 0.7f), c.withAlpha(a));
    }
}

void lightLeakBlobs(const Field& f);
void lightLeak(const Field& f, VideoFrame& out) {
    softLayer(out, f.d.m, 8, [&](VideoFrame& low, const Affine& m) {
        vec::Canvas cv(low);
        const D d{cv, m, f.t};
        lightLeakBlobs(Field{d, f.w, f.h, f.u, f.speed, f.density, f.t, f.seed, f.tint, f.tinted, f.unitScale});
    });
}

void lightLeakBlobs(const Field& f) {
    static const char* colors[] = {"#ff7a2f", "#ffb347", "#ff4f6d"};
    const double big = std::max(f.w, f.h);
    for (int i = 0; i < 3; ++i) {
        const double t = f.t * f.speed * (0.11 + 0.05 * i) + kTau * f.r(i, 0);
        const double x = f.w * 0.48 * std::sin(t + i * 2.1), y = f.h * 0.42 * std::cos(t * 0.8 + i * 1.3);
        const double rad = big * (0.42 + 0.12 * std::sin(t * 1.7 + i));
        const double a = 0.32 + 0.14 * std::sin(f.t * 0.6 + i * 2.0);
        softDot(f.d, x, y, rad, f.col(colors[i], static_cast<float>(a)));
    }
}

void shootingStars(const Field& f) {
    const int n = f.count(70);
    for (int i = 0; i < n; ++i) {  // faint background stars
        const double x = -f.w / 2 + f.r(i, 0) * f.w, y = -f.h / 2 + f.r(i, 1) * f.h;
        const double rad = (1.8 + 3 * f.r(i, 2)) * f.u;
        const double tw = 0.5 + 0.5 * std::sin(f.t * (1 + 2 * f.r(i, 3)) + kTau * f.r(i, 4));
        softDot(f.d, x, y, rad * 2.2, hx("#ffffff", static_cast<float>(0.25 + 0.5 * tw)));
    }
    const int m = std::max(1, f.count(4));
    for (int j = 0; j < m; ++j) {
        const double period = 3.0 + 2.0 * f.r(j, 10);
        const double tt = f.t * f.speed + f.r(j, 11) * period;
        const auto cycle = static_cast<std::int64_t>(std::floor(tt / period));
        const double k = (tt - cycle * period) / 1.0;  // flight takes 1 s
        if (k >= 1) continue;
        const std::int64_t key = cycle * 97 + j;
        const double sx = -f.w / 2 + (0.3 + 0.7 * f.r(key, 12)) * f.w, sy = -f.h / 2 + 0.45 * f.r(key, 13) * f.h;
        const double ang = kPi * (0.78 + 0.12 * f.r(key, 14)), dist = 0.45 * std::max(f.w, f.h);
        const Pt dir{std::cos(ang), std::sin(ang)};
        const double e = smooth(k);
        const Pt head{sx + dir.x * dist * e, sy + dir.y * dist * e};
        const double len = 300 * f.u * std::sin(kPi * k);
        const Pt tail{head.x - dir.x * len, head.y - dir.y * len};
        const float a = static_cast<float>(std::sin(kPi * k));
        const Color c = f.col("#ffffff");
        f.d.cv.stroke(line(tail, head), 4.5 * f.u, Paint::linear(f.d.m.apply(tail), f.d.m.apply(head), c.withAlpha(0), c.withAlpha(a)), f.d.m);
        softDot(f.d, head.x, head.y, 16 * f.u, hx("#e6f2ff", a));
    }
}

// ---------------------------------------------------------------- film looks

// Darkens the edges of the region (and optionally warms it), per pixel straight onto `out`
// (drawn first, so the frame is still transparent).
void vignettePass(VideoFrame& out, const Affine& m, double w, double h, double strength, Color wash, float washA) {
    const Pt p0 = m.apply({-w / 2, -h / 2}), p1 = m.apply({w / 2, h / 2});
    const double cx = (p0.x + p1.x) / 2, cy = (p0.y + p1.y) / 2, hw = (p1.x - p0.x) / 2, hh = (p1.y - p0.y) / 2;
    if (hw <= 0 || hh <= 0) return;
    const int x0 = std::max(0, static_cast<int>(p0.x)), x1 = std::min(out.width, static_cast<int>(std::ceil(p1.x)));
    const int y0 = std::max(0, static_cast<int>(p0.y)), y1 = std::min(out.height, static_cast<int>(std::ceil(p1.y)));
    for (int y = y0; y < y1; ++y) {
        const double dy = (y + 0.5 - cy) / hh;
        for (int x = x0; x < x1; ++x) {
            const double dx = (x + 0.5 - cx) / hw;
            const double v = strength * smooth((std::sqrt(dx * dx * 0.85 + dy * dy) - 0.55) / 0.75);
            // wash under, black vignette over it
            const float va = static_cast<float>(v);
            const float a = va + washA * (1 - va);
            if (a <= 0) continue;
            const float k = washA * (1 - va) / a;
            const float rgb[3] = {wash.r * k, wash.g * k, wash.b * k};
            const std::size_t i = (static_cast<std::size_t>(y) * out.width + x) * 4;
            if (out.deep()) {
                for (int c = 0; c < 3; ++c) out.half[i + c] = floatToHalf(rgb[c]);
                out.half[i + 3] = floatToHalf(a);
            } else {
                for (int c = 0; c < 3; ++c) out.rgba[i + c] = static_cast<std::uint8_t>(std::lround(rgb[c] * 255));
                out.rgba[i + 3] = static_cast<std::uint8_t>(std::lround(a * 255));
            }
        }
    }
}

void oldFilm(const Field& f, VideoFrame& out) {
    softLayer(out, f.d.m, 4, [&](VideoFrame& low, const Affine& m) { vignettePass(low, m, f.w, f.h, 0.62, hx("#6b4a1f"), 0.16f); });
    // Dust and hairs change 12 times a second, like film running through a projector.
    const auto q = static_cast<std::int64_t>(std::floor(f.t * 12 * f.speed));
    const int dust = f.count(16);
    for (int i = 0; i < dust; ++i) {
        const std::int64_t key = q * 1009 + i;
        if (f.r(key, 0) < 0.35) continue;
        const double x = -f.w / 2 + f.r(key, 1) * f.w, y = -f.h / 2 + f.r(key, 2) * f.h;
        const double s = (1.6 + 4.5 * f.r(key, 3) * f.r(key, 3)) * f.u;
        const bool light = f.r(key, 4) < 0.3;
        const D p = f.d.at(Affine::translate(x, y) * Affine::rotate(kTau * f.r(key, 5)) * Affine::scale(s));
        p.fill(ell(0, 0, 1, 0.55 + 0.4 * f.r(key, 6)), light ? hx("#fff8e7", 0.55f) : hx("#1a1208", 0.6f));
    }
    if (f.r(q, 20) < 0.25) {  // a hair now and then
        const double x = -f.w / 2 + f.r(q, 21) * f.w, y = -f.h / 2 + f.r(q, 22) * f.h;
        const D p = f.d.at(Affine::translate(x, y) * Affine::rotate(kTau * f.r(q, 23)) * Affine::scale(30 * f.u));
        Path hair;
        hair.moveTo(-1, 0).cubicTo(-0.4, -0.8, 0.3, 0.7, 1, -0.1);
        p.stroke(hair, 0.045, hx("#1a1208", 0.55f));
    }
    for (int s = 0; s < 2; ++s) {  // scratches drift and flicker
        const auto slow = static_cast<std::int64_t>(std::floor(f.t * 0.7 + s * 0.5));
        if (f.r(slow * 7 + s, 30) < 0.3) continue;
        const double x = -f.w / 2 + f.r(slow * 7 + s, 31) * f.w + 3 * f.u * (f.r(q * 3 + s, 32) - 0.5);
        const float a = static_cast<float>(0.18 + 0.22 * f.r(q * 3 + s, 33));
        f.d.cv.fill(rrect(x, -f.h / 2, 1.6 * f.u, f.h, 0), Paint::solid(hx("#f5ecd7", a)), f.d.m);
    }
}

// ---------------------------------------------------------------- catalog

struct Entry {
    OverlayInfo info;
    std::function<void(const Field&, VideoFrame&)> draw;
    double density = 1;  // default "density"
};

const std::vector<Entry>& entries() {
    static const std::vector<Entry> e = [] {
        std::vector<Entry> v;
        auto add = [&](const char* id, const char* name, const char* cat, bool tint,
                       std::function<void(const Field&, VideoFrame&)> d) {
            v.push_back({{id, name, cat, tint}, std::move(d)});
        };
        const char* season = "Mùa & thời tiết";
        const char* party = "Lễ hội";
        const char* light = "Ánh sáng";
        const char* film = "Phong cách phim";
        add("snow", "Tuyết rơi", season, true, [](const Field& f, VideoFrame&) { snowField(f, 170, 12); });
        add("blizzard", "Bão tuyết", season, true, [](const Field& f, VideoFrame&) { snowField(f, 380, 260); });
        add("snowflakes", "Hoa tuyết bay", season, true, [](const Field& f, VideoFrame&) { snowflakes(f); });
        add("rain", "Mưa", season, true, [](const Field& f, VideoFrame&) { rain(f); });
        add("fog", "Sương mù", season, true, [](const Field& f, VideoFrame& out) { fog(f, out); });
        add("leaves", "Lá thu rơi", season, false, [](const Field& f, VideoFrame&) { leaves(f); });
        add("petals", "Hoa anh đào", season, false, [](const Field& f, VideoFrame&) { petals(f); });
        add("apricot", "Hoa mai Tết", season, false, [](const Field& f, VideoFrame&) { apricot(f); });
        add("confetti", "Pháo giấy", party, false, [](const Field& f, VideoFrame&) { confetti(f); });
        add("glitter", "Kim tuyến", party, true, [](const Field& f, VideoFrame&) { glitter(f); });
        add("balloons", "Bóng bay", party, false, [](const Field& f, VideoFrame&) { balloons(f); });
        add("fireworks", "Pháo hoa", party, true, [](const Field& f, VideoFrame&) { fireworks(f); });
        add("hearts", "Tim bay", party, true, [](const Field& f, VideoFrame&) { hearts(f); });
        add("sparkles", "Lấp lánh", light, true, [](const Field& f, VideoFrame&) { sparkles(f); });
        add("fireflies", "Đom đóm", light, true, [](const Field& f, VideoFrame&) { fireflies(f); });
        add("bokeh", "Bokeh", light, true, [](const Field& f, VideoFrame& out) { bokeh(f, out); });
        add("light_leak", "Loé sáng", light, true, [](const Field& f, VideoFrame& out) { lightLeak(f, out); });
        add("shooting_stars", "Sao băng", light, true, [](const Field& f, VideoFrame&) { shootingStars(f); });
        add("bubbles", "Bong bóng", light, true, [](const Field& f, VideoFrame&) { bubbles(f); });
        add("old_film", "Phim cũ", film, false, [](const Field& f, VideoFrame& out) { oldFilm(f, out); });
        add("vignette", "Viền tối", film, false, [](const Field& f, VideoFrame& out) {
            softLayer(out, f.d.m, 4, [&](VideoFrame& low, const Affine& m) {
                vignettePass(low, m, f.w, f.h, std::clamp(0.6 * f.density, 0.0, 1.0), hx("#000000"), 0.0f);
            });
        });
        return v;
    }();
    return e;
}

const Entry* findEntry(const std::string& id) {
    for (const auto& e : entries())
        if (e.info.id == id) return &e;
    return nullptr;
}

// Multiplies alpha by the fade and the region's feathered edges. Edges that reach the frame
// are not feathered (a full-frame overlay stays full to the border).
void finish(VideoFrame& out, const Affine& m, double w, double h, double feather, double opacity) {
    const Pt p0 = m.apply({-w / 2, -h / 2}), p1 = m.apply({w / 2, h / 2});
    const double fpx = std::max(1.0, feather * std::min(p1.x - p0.x, p1.y - p0.y));
    const bool openL = p0.x <= 0.5, openT = p0.y <= 0.5, openR = p1.x >= out.width - 0.5, openB = p1.y >= out.height - 0.5;
    if (opacity >= 1 && openL && openT && openR && openB) return;
    auto edge = [&](double v, double a, double b, bool openA, bool openB2) {
        const double ka = openA ? (v >= a ? 1.0 : 0.0) : smooth((v - a) / fpx);
        const double kb = openB2 ? (v <= b ? 1.0 : 0.0) : smooth((b - v) / fpx);
        return ka * kb;
    };
    std::vector<float> cols(static_cast<std::size_t>(out.width));
    for (int x = 0; x < out.width; ++x) cols[static_cast<std::size_t>(x)] = static_cast<float>(edge(x + 0.5, p0.x, p1.x, openL, openR));
    for (int y = 0; y < out.height; ++y) {
        const float ky = static_cast<float>(edge(y + 0.5, p0.y, p1.y, openT, openB) * opacity);
        const std::size_t row = static_cast<std::size_t>(y) * out.width * 4;
        for (int x = 0; x < out.width; ++x) {
            const float k = ky * cols[static_cast<std::size_t>(x)];
            const std::size_t i = row + static_cast<std::size_t>(x) * 4 + 3;
            if (out.deep()) {
                if (out.half[i] == 0 || k >= 1) continue;
                out.half[i] = floatToHalf(halfToFloat(out.half[i]) * k);
            } else {
                if (out.rgba[i] == 0 || k >= 1) continue;
                out.rgba[i] = static_cast<std::uint8_t>(std::lround(out.rgba[i] * k));
            }
        }
    }
}

double fadeOf(const Json& c, double local, double length) {
    const motion::Timing T = motion::timing(c, local, length);
    const double in = T.in.empty() ? 1.0 : smooth(T.inProgress);
    const double out = T.out.empty() ? 1.0 : smooth(1 - T.outProgress);
    return in * out;
}

void render(const Entry& e, const Json& c, double local, double opacity, const Affine& place, VideoFrame& out) {
    const double ref = std::max(1.0, c.value("ref", 1080.0));
    const double w = std::max(1.0, c.value("w", ref * 16 / 9)), h = std::max(1.0, c.value("h", ref));
    vec::Canvas cv(out);
    const D d{cv, place, local};
    const std::string tint = c.value("color", std::string{});
    Field f{d,
            w,
            h,
            ref / 1080.0 * std::clamp(c.value("size", 1.0), 0.2, 5.0),
            std::clamp(c.value("speed", 1.0), 0.0, 5.0),
            std::clamp(c.value("density", 1.0), 0.0, 5.0),
            local,
            static_cast<unsigned>(c.value("seed", 1)),
            Color::hex(tint.empty() ? "#ffffff" : tint),
            e.info.tintable && !tint.empty()};
    f.unitScale = ref / 1080.0;
    e.draw(f, out);
    finish(out, place, w, h, std::clamp(c.value("feather", 0.12), 0.0, 0.5), opacity);
}

}  // namespace

const std::vector<OverlayInfo>& overlayCatalog() {
    static const std::vector<OverlayInfo> v = [] {
        std::vector<OverlayInfo> out;
        for (const auto& e : entries()) out.push_back(e.info);
        return out;
    }();
    return v;
}

std::vector<std::string> overlayCategories() {
    std::vector<std::string> out;
    for (const auto& e : entries())
        if (std::find(out.begin(), out.end(), e.info.category) == out.end()) out.push_back(e.info.category);
    return out;
}

const OverlayInfo* findOverlay(const std::string& id) {
    const Entry* e = findEntry(id);
    return e ? &e->info : nullptr;
}

Json defaultOverlayContent(const std::string& id, int canvasW, int canvasH) {
    return Json{{"kind", "overlay"},
                {"overlay", id},
                {"w", std::max(1, canvasW)},
                {"h", std::max(1, canvasH)},
                {"ref", std::max(1, canvasH)},
                {"density", 1.0},
                {"speed", 1.0},
                {"size", 1.0},
                {"opacity", 1.0},
                {"feather", 0.12},
                {"seed", 1},
                {"in", {{"type", "fade"}, {"duration", 0.8}}},
                {"out", {{"type", "fade"}, {"duration", 0.8}}}};
}

void drawOverlay(const Json& c, double local, double length, const vec::Affine& place, VideoFrame& out) {
    const Entry* e = findEntry(c.value("overlay", std::string{}));
    if (!e || out.empty()) return;
    const double opacity = std::clamp(c.value("opacity", 1.0), 0.0, 1.0) * fadeOf(c, local, length);
    if (opacity <= 0.002) return;
    render(*e, c, local, opacity, place, out);
}

std::array<double, 4> overlayBounds(const Json& c) {
    const double ref = std::max(1.0, c.value("ref", 1080.0));
    const double w = std::max(1.0, c.value("w", ref * 16 / 9)), h = std::max(1.0, c.value("h", ref));
    return {-w / 2, -h / 2, w / 2, h / 2};
}

bool overlayStill(const Json& c, double local, double length) {
    if (c.value("overlay", std::string{}) != "vignette") return false;
    const motion::Timing T = motion::timing(c, local, length);
    return (T.in.empty() || T.inProgress >= 1) && (T.out.empty() || T.outProgress <= 0);
}

void drawOverlayIcon(const std::string& id, double seconds, VideoFrame& out) {
    const Entry* e = findEntry(id);
    if (!e || out.empty()) return;
    {
        vec::Canvas cv(out);
        Path bg;
        bg.rect(0, 0, out.width, out.height);
        cv.fill(bg, Paint::linear({0, 0}, {0, static_cast<double>(out.height)}, hx("#1d2b4f"), hx("#0b1020")));
    }
    // A 1080-line frame shown at icon size, with more particles so a small tile still reads.
    const double k = out.height / 1080.0;
    Json c = defaultOverlayContent(id, static_cast<int>(std::lround(out.width / k)), 1080);
    c["density"] = 1.4;
    c["size"] = 1.6;
    VideoFrame layer;
    layer.allocate(out.width, out.height, out.format);
    render(*e, c, seconds, 1.0, Affine::translate(out.width / 2.0, out.height / 2.0) * Affine::scale(k), layer);
    // Straight-alpha "over" onto the backdrop.
    if (out.deep()) return;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        const unsigned a = layer.rgba[i + 3];
        if (!a) continue;
        for (int ch = 0; ch < 3; ++ch) out.rgba[i + ch] = static_cast<std::uint8_t>((layer.rgba[i + ch] * a + out.rgba[i + ch] * (255 - a) + 127) / 255);
    }
}

}  // namespace vstudio::overlays
