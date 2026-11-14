// Shared drawing kit for the built-in stickers (internal to the core).
// Stickers are drawn in a unit box [-0.5, 0.5]^2, y down; `D::t` is the sticker's own time in seconds.
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

#include "vstudio/vector.hpp"

namespace vstudio::stickers::kit {

using vec::Affine;
using vec::Color;
using vec::Paint;
using vec::Path;
using vec::Pt;

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kTau = 2 * kPi;

// ---------------------------------------------------------------- drawing helpers
// Stickers are drawn in a unit box [-0.5, 0.5]^2, y down.

inline Color hx(const char* s, float a = 1) { return Color::hex(s, a); }

struct D {
    vec::Canvas& cv;
    Affine m;
    double t;
    void fill(const Path& p, Color c) const { cv.fill(p, Paint::solid(c), m); }
    void lin(const Path& p, Pt a, Pt b, Color ca, Color cb) const {
        cv.fill(p, Paint::linear(m.apply(a), m.apply(b), ca, cb), m);
    }
    void rad(const Path& p, Pt c, double r, Color ci, Color co) const {
        cv.fill(p, Paint::radial(m.apply(c), r * m.scaleFactor(), ci, co), m);
    }
    void stroke(const Path& p, double w, Color c) const { cv.stroke(p, w, Paint::solid(c), m); }
    [[nodiscard]] D at(const Affine& a) const { return {cv, m * a, t}; }
};

inline Path circ(double x, double y, double r) {
    Path p;
    p.circle(x, y, r);
    return p;
}
inline Path ell(double x, double y, double rx, double ry) {
    Path p;
    p.ellipse(x, y, rx, ry);
    return p;
}
inline Path rrect(double x, double y, double w, double h, double r) {
    Path p;
    p.roundRect(x, y, w, h, r);
    return p;
}
inline Path poly(std::vector<Pt> pts) {
    Path p;
    p.polygon(pts);
    return p;
}
inline Path heartP(double x, double y, double s) {
    Path p;
    p.heart(x, y, s);
    return p;
}
inline Path starP(double x, double y, int n, double ro, double ri, double rot = -kPi / 2) {
    Path p;
    p.star(x, y, n, ro, ri, rot);
    return p;
}
// Open arc (for strokes): angles in radians, y down (0 = right, pi/2 = down).
inline Path arc(double x, double y, double rx, double ry, double a0, double a1, int segs = 28) {
    Path p;
    for (int i = 0; i <= segs; ++i) {
        const double a = a0 + (a1 - a0) * i / segs;
        if (i == 0) p.moveTo(x + rx * std::cos(a), y + ry * std::sin(a));
        else p.lineTo(x + rx * std::cos(a), y + ry * std::sin(a));
    }
    return p;
}
// Closed pie / half-disc.
inline Path pie(double x, double y, double rx, double ry, double a0, double a1, int segs = 28) {
    Path p;
    p.moveTo(x, y);
    for (int i = 0; i <= segs; ++i) {
        const double a = a0 + (a1 - a0) * i / segs;
        p.lineTo(x + rx * std::cos(a), y + ry * std::sin(a));
    }
    p.close();
    return p;
}
inline Path line(Pt a, Pt b) {
    Path p;
    p.moveTo(a.x, a.y).lineTo(b.x, b.y);
    return p;
}
// A petal from (0, 0) pointing up to (0, -len).
inline Path petalShape(double len, double w, double tip /* 0 round .. 1 pointed */) {
    Path p;
    const double k = 1 - tip;
    p.moveTo(0, 0);
    p.cubicTo(w * 0.9, -len * 0.25, w * (0.55 + 0.45 * k), -len * (0.85 + 0.15 * k), 0, -len);
    p.cubicTo(-w * (0.55 + 0.45 * k), -len * (0.85 + 0.15 * k), -w * 0.9, -len * 0.25, 0, 0);
    p.close();
    return p;
}
inline double wave(double t, double period, double phase = 0) { return std::sin(kTau * (t / period + phase)); }
inline double frac(double x) { return x - std::floor(x); }
// 1 most of the time, a quick smooth dip every `period` s (blinks).
// Draw-on strokes: start a little drawn, finish after ~1 s, then stay (never restart with a jump).
inline double drawOn(double t) {
    const double k = std::clamp(0.12 + t / 1.1, 0.0, 1.0);
    return 1 - (1 - k) * (1 - k);
}
inline double blink(double t, double period = 3.4, double at = 1.7) {
    const double ph = std::fmod(t, period);
    return 1 - 0.92 * std::exp(-std::pow((ph - at) / 0.06, 2));
}
inline Color mix(Color a, Color b, float k) {
    return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k};
}

// Text in the sticker's font: centred on (cx, cy), `size` = font size (box units), shrunk to fit `maxW`.
// Returns the path (already placed) so callers can fill, stroke or shade it.
Path textPath(const std::string& utf8, double cx, double cy, double size, double maxW,
              const char* font = "be-vietnam-pro-extrabold");
void label(const D& d, const std::string& utf8, double cx, double cy, double size, double maxW, Color fill,
           const char* font = "be-vietnam-pro-extrabold");

// One more sticker: id, Vietnamese name, group, drawing.
using Add = std::function<void(const char*, const char*, const char*, std::function<void(const D&)>)>;
// The groups added after the first ones (stickers_more.cpp): social first, the rest at the end.
void addSocialStickers(const Add& add);
void addMoreStickers(const Add& add);
// 8-bit game, the 80s, animal reactions, daily life, birthdays, Christmas, the year's celebrations (stickers_extra.cpp).
void addExtraStickers(const Add& add);

}  // namespace vstudio::stickers::kit
