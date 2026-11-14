#include "vstudio/stickers.hpp"

#include "sticker_kit.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

#include "vstudio/motion.hpp"
#include "vstudio/pixel.hpp"

namespace vstudio::stickers {

using vec::Affine;
using vec::Color;
using vec::Paint;
using vec::Path;
using vec::Pt;

namespace {

using namespace kit;

// ---------------------------------------------------------------- faces

enum class Face {
    Smile, Grin, Laugh, Joy, Wink, Love, Cool, Wow, Sad, Cry, Angry, Sleepy, Kiss, Tongue, Starry, Blush, Party, Meh,
    Think, Shy
};

void faceBase(const D& d, Color light, Color dark) {
    d.rad(circ(0, 0, 0.46), {-0.12, -0.16}, 0.62, light, dark);
    d.stroke(circ(0, 0, 0.46), 0.018, mix(dark, hx("#000000"), 0.25f));
}

void eyes(const D& d, double open, double dx = 0, double dy = 0) {
    for (double s : {-1.0, 1.0}) {
        const D e = d.at(Affine::translate(s * 0.15 + dx, -0.06 + dy) * Affine::scale(1, std::max(0.08, open)));
        e.fill(ell(0, 0, 0.055, 0.08), hx("#3b2410"));
        e.fill(circ(0.018, -0.03, 0.018), hx("#ffffff"));
    }
}
void happyEyes(const D& d) {
    for (double s : {-1.0, 1.0}) d.stroke(arc(s * 0.15, -0.03, 0.065, 0.06, kPi * 1.1, kPi * 1.9), 0.04, hx("#3b2410"));
}
void cheeks(const D& d, float a = 0.55f) {
    for (double s : {-1.0, 1.0}) d.fill(ell(s * 0.27, 0.1, 0.075, 0.045), hx("#ff6f7d", a));
}
void smileMouth(const D& d, double w = 0.2, double y = 0.04) {
    d.stroke(arc(0, y, w, w * 0.85, kPi * 0.15, kPi * 0.85), 0.042, hx("#3b2410"));
}
void openMouth(const D& d, double w, double h, double y, bool teeth = true) {
    const Path m = pie(0, y, w, h, 0, kPi);
    d.fill(m, hx("#5a1d12"));
    d.fill(ell(0, y + h * 0.72, w * 0.55, h * 0.3), hx("#ff6b7d"));
    if (teeth) d.fill(rrect(-w * 0.8, y, w * 1.6, h * 0.22, 0.01), hx("#ffffff"));
}
void tear(const D& d, double x, double y, double s) {
    Path p;
    p.moveTo(x, y - s).cubicTo(x + s * 0.6, y - s * 0.1, x + s * 0.6, y + s * 0.6, x, y + s * 0.6);
    p.cubicTo(x - s * 0.6, y + s * 0.6, x - s * 0.6, y - s * 0.1, x, y - s).close();
    d.fill(p, hx("#5ec8ff"));
    d.fill(circ(x - s * 0.15, y + s * 0.1, s * 0.12), hx("#ffffff", 0.8f));
}

void face(const D& d0, Face f) {
    const double t = d0.t;
    // Idle: a gentle bob and tilt.
    const D d = d0.at(Affine::translate(0, 0.012 * wave(t, 2.2)) * Affine::rotate(0.05 * wave(t, 3.1)));
    const Color yl = hx("#ffe873"), yd = hx("#f7a91e");
    switch (f) {
        case Face::Angry: faceBase(d, hx("#ff9a6b"), hx("#e5322d")); break;
        case Face::Sad:
        case Face::Cry: faceBase(d, hx("#ffe98a"), hx("#f0b030")); break;
        default: faceBase(d, yl, yd);
    }
    const double b = blink(t);
    switch (f) {
        case Face::Smile:
            eyes(d, b);
            smileMouth(d);
            break;
        case Face::Grin:
            eyes(d, b);
            openMouth(d, 0.22, 0.2, 0.06);
            break;
        case Face::Laugh: {
            happyEyes(d);
            const double k = 1 + 0.12 * std::abs(wave(t, 0.5));
            openMouth(d.at(Affine::translate(0, 0.06) * Affine::scale(1, k) * Affine::translate(0, -0.06)), 0.24, 0.22,
                      0.06);
            cheeks(d);
            break;
        }
        case Face::Joy: {
            happyEyes(d);
            openMouth(d, 0.24, 0.2, 0.07);
            for (double s : {-1.0, 1.0}) {
                const double ph = frac(t / 1.1 + (s > 0 ? 0.5 : 0));
                tear(d, s * (0.3 + 0.06 * ph), -0.02 + 0.22 * ph, 0.06 * (1 - 0.3 * ph));
            }
            break;
        }
        case Face::Wink:
            for (double s : {-1.0}) {
                const D e = d.at(Affine::translate(s * 0.15, -0.06) * Affine::scale(1, b));
                e.fill(ell(0, 0, 0.055, 0.08), hx("#3b2410"));
                e.fill(circ(0.018, -0.03, 0.018), hx("#ffffff"));
            }
            d.stroke(arc(0.15, -0.05, 0.06, 0.05, kPi * 1.1, kPi * 1.9), 0.04, hx("#3b2410"));
            smileMouth(d, 0.18);
            d.fill(ell(0.08, 0.2, 0.06, 0.05), hx("#ff6b7d"));
            break;
        case Face::Love: {
            const double beat = 1 + 0.12 * std::max(0.0, wave(t, 0.8));
            for (double s : {-1.0, 1.0})
                d.at(Affine::translate(s * 0.16, -0.07) * Affine::scale(beat)).fill(heartP(0, 0, 0.2), hx("#ff2e55"));
            openMouth(d, 0.17, 0.14, 0.1, false);
            break;
        }
        case Face::Cool: {
            Path g;
            g.append(rrect(-0.33, -0.15, 0.29, 0.17, 0.07)).append(rrect(0.04, -0.15, 0.29, 0.17, 0.07));
            d.fill(g, hx("#1c1c22"));
            d.fill(rrect(-0.06, -0.12, 0.12, 0.035, 0.01), hx("#1c1c22"));
            const double sh = frac(t / 2.6);
            d.fill(poly({{-0.3 + 0.5 * sh, -0.14}, {-0.24 + 0.5 * sh, -0.14}, {-0.3 + 0.5 * sh, 0.0}, {-0.36 + 0.5 * sh, 0.0}}),
                   hx("#ffffff", static_cast<float>(0.35 * std::sin(kPi * sh))));
            d.stroke(arc(0.03, 0.1, 0.14, 0.08, kPi * 0.1, kPi * 0.8), 0.04, hx("#3b2410"));
            break;
        }
        case Face::Wow: {
            for (double s : {-1.0, 1.0}) {
                d.fill(circ(s * 0.15, -0.08, 0.07), hx("#ffffff"));
                d.fill(circ(s * 0.15, -0.08, 0.045), hx("#3b2410"));
            }
            const double k = 1 + 0.1 * wave(t, 1.2);
            d.at(Affine::translate(0, 0.17) * Affine::scale(k)).fill(ell(0, 0, 0.07, 0.09), hx("#5a1d12"));
            break;
        }
        case Face::Sad:
            eyes(d, b, 0, 0.02);
            d.stroke(arc(0, 0.28, 0.14, 0.1, kPi * 1.15, kPi * 1.85), 0.04, hx("#3b2410"));
            for (double s : {-1.0, 1.0}) d.stroke(line({s * 0.22, -0.2}, {s * 0.08, -0.16}), 0.03, hx("#3b2410"));
            break;
        case Face::Cry: {
            happyEyes(d);
            d.stroke(arc(0, 0.27, 0.13, 0.1, kPi * 1.15, kPi * 1.85), 0.04, hx("#3b2410"));
            for (double s : {-1.0, 1.0}) {
                const double ph = frac(t / 0.9 + (s > 0 ? 0.45 : 0));
                tear(d, s * 0.16, 0.02 + 0.32 * ph, 0.05);
            }
            break;
        }
        case Face::Angry: {
            eyes(d, 0.8, 0, 0.02);
            for (double s : {-1.0, 1.0}) d.stroke(line({s * 0.25, -0.22}, {s * 0.07, -0.13}), 0.045, hx("#3b1010"));
            d.stroke(arc(0, 0.27, 0.13, 0.08, kPi * 1.15, kPi * 1.85), 0.045, hx("#3b1010"));
            const double puff = frac(t / 1.4);
            for (double s : {-1.0, 1.0})
                d.fill(circ(s * (0.34 + 0.04 * puff), -0.3 - 0.06 * puff, 0.03 + 0.025 * puff),
                       hx("#ffffff", static_cast<float>(0.7 * std::sin(kPi * puff))));
            break;
        }
        case Face::Sleepy: {
            for (double s : {-1.0, 1.0}) d.stroke(arc(s * 0.15, -0.07, 0.06, 0.04, kPi * 0.1, kPi * 0.9), 0.035, hx("#3b2410"));
            d.fill(ell(0, 0.18, 0.05, 0.035 + 0.01 * wave(t, 2.5)), hx("#5a1d12"));
            for (int i = 0; i < 3; ++i) {
                const double ph = frac(t / 2.4 + i / 3.0);
                const double s = 0.06 + 0.05 * ph;
                const double x = 0.24 + 0.1 * ph, y = -0.22 - 0.18 * ph;
                Path z;
                z.moveTo(x - s / 2, y - s / 2).lineTo(x + s / 2, y - s / 2).lineTo(x - s / 2, y + s / 2).lineTo(x + s / 2, y + s / 2);
                d.stroke(z, 0.022, hx("#5b6cff", static_cast<float>(std::sin(kPi * ph))));
            }
            break;
        }
        case Face::Kiss: {
            eyes(d, b);
            d.stroke(arc(0.0, 0.14, 0.04, 0.035, -kPi * 0.5, kPi * 0.5), 0.035, hx("#3b2410"));
            d.stroke(arc(0.0, 0.21, 0.04, 0.035, -kPi * 0.5, kPi * 0.5), 0.035, hx("#3b2410"));
            const double ph = frac(t / 1.6);
            d.at(Affine::translate(0.2 + 0.15 * ph, 0.1 - 0.4 * ph) * Affine::scale(0.6 + 0.6 * ph))
                .fill(heartP(0, 0, 0.14), hx("#ff2e55", static_cast<float>(std::sin(kPi * ph))));
            cheeks(d, 0.4f);
            break;
        }
        case Face::Tongue: {
            eyes(d, b);
            d.stroke(arc(0, 0.06, 0.18, 0.12, kPi * 0.12, kPi * 0.88), 0.04, hx("#3b2410"));
            const double k = 1 + 0.15 * wave(t, 0.7);
            d.at(Affine::translate(0.04, 0.17) * Affine::scale(1, k)).fill(rrect(-0.06, 0, 0.12, 0.13, 0.06), hx("#ff5f7a"));
            break;
        }
        case Face::Starry: {
            const double r = 1 + 0.15 * wave(t, 0.9);
            for (double s : {-1.0, 1.0})
                d.at(Affine::translate(s * 0.16, -0.07) * Affine::rotate(0.4 * wave(t, 2.0)) * Affine::scale(r))
                    .fill(starP(0, 0, 5, 0.11, 0.05), hx("#ffb000"));
            openMouth(d, 0.2, 0.17, 0.08);
            break;
        }
        case Face::Blush:
            happyEyes(d);
            smileMouth(d, 0.16, 0.06);
            cheeks(d, static_cast<float>(0.55 + 0.25 * wave(t, 1.6)));
            break;
        case Face::Party: {
            happyEyes(d);
            openMouth(d, 0.2, 0.17, 0.07);
            const D h = d.at(Affine::translate(0.2, -0.38) * Affine::rotate(0.4));
            h.fill(poly({{-0.12, 0.08}, {0.12, 0.08}, {0, -0.22}}), hx("#7c4dff"));
            h.fill(poly({{-0.08, 0.0}, {0.08, 0.0}, {0.05, -0.08}, {-0.05, -0.08}}), hx("#ffd54a"));
            h.fill(circ(0, -0.23, 0.035), hx("#ff4f8b"));
            for (int i = 0; i < 7; ++i) {
                const double ph = frac(t / 1.8 + i / 7.0);
                const double a = kTau * i / 7;
                const Color cc = Color::hsv(i * 51.0, 0.75, 1);
                d.at(Affine::translate(std::cos(a) * (0.2 + 0.25 * ph), -0.2 + std::sin(a) * 0.15 * ph + 0.3 * ph * ph) *
                     Affine::rotate(ph * 6 + i))
                    .fill(rrect(-0.018, -0.008, 0.036, 0.016, 0.004), cc.withAlpha(static_cast<float>(std::sin(kPi * ph))));
            }
            break;
        }
        case Face::Meh:
            eyes(d, b);
            d.stroke(line({-0.12, 0.17}, {0.12, 0.17}), 0.04, hx("#3b2410"));
            break;
        case Face::Think: {
            eyes(d.at(Affine::translate(0.03, -0.02)), b);
            d.stroke(line({0.08, -0.22}, {0.24, -0.25}), 0.03, hx("#3b2410"));
            d.stroke(arc(0.04, 0.2, 0.1, 0.04, kPi * 1.2, kPi * 1.8), 0.035, hx("#3b2410"));
            const D hand = d.at(Affine::translate(-0.08, 0.32 + 0.02 * wave(t, 1.5)));
            hand.fill(rrect(-0.1, -0.06, 0.2, 0.12, 0.06), hx("#ffcf40"));
            hand.fill(rrect(-0.03, -0.18, 0.06, 0.14, 0.03), hx("#ffcf40"));
            hand.stroke(rrect(-0.1, -0.06, 0.2, 0.12, 0.06), 0.012, hx("#e09a18"));
            break;
        }
        case Face::Shy: {
            const double look = 0.03 * wave(t, 2.8);
            eyes(d, b, look, 0.02);
            d.stroke(arc(0, 0.1, 0.08, 0.05, kPi * 0.2, kPi * 0.8), 0.035, hx("#3b2410"));
            cheeks(d, 0.7f);
            break;
        }
    }
}

// ---------------------------------------------------------------- hearts

void heart(const D& d0, Color a, Color b, int style) {
    const double t = d0.t;
    const double beat = 1 + 0.08 * std::exp(-std::pow((frac(t / 1.1) - 0.15) / 0.07, 2)) +
                        0.05 * std::exp(-std::pow((frac(t / 1.1) - 0.38) / 0.07, 2));
    const D d = d0.at(Affine::translate(0, 0.02) * Affine::scale(beat));
    switch (style) {
        case 0:  // glossy
            d.lin(heartP(0, 0, 0.88), {0, -0.4}, {0, 0.4}, a, b);
            d.fill(ell(-0.2, -0.17, 0.1, 0.06), hx("#ffffff", 0.55f));
            d.fill(circ(-0.08, -0.22, 0.025), hx("#ffffff", 0.7f));
            break;
        case 1:  // outline
            d.stroke(heartP(0, 0, 0.8), 0.07, a);
            break;
        case 2: {  // double
            d.at(Affine::translate(0.12, 0.08) * Affine::rotate(0.25)).lin(heartP(0, 0, 0.55), {0, -0.3}, {0, 0.3}, b, mix(b, hx("#000000"), 0.2f));
            d.at(Affine::translate(-0.12, -0.08) * Affine::rotate(-0.2)).lin(heartP(0, 0, 0.6), {0, -0.3}, {0, 0.3}, a, b);
            break;
        }
        case 3: {  // broken (the halves drift apart and back)
            const double g = 0.03 + 0.02 * wave(t, 2.0);
            Path l, r;
            l.moveTo(0, -0.22).lineTo(-0.05, -0.08).lineTo(0.04, 0.02).lineTo(-0.03, 0.16).lineTo(0, 0.36).lineTo(-0.6, 0.36).lineTo(-0.6, -0.5).lineTo(0, -0.5).close();
            r.moveTo(0, -0.22).lineTo(-0.05, -0.08).lineTo(0.04, 0.02).lineTo(-0.03, 0.16).lineTo(0, 0.36).lineTo(0.6, 0.36).lineTo(0.6, -0.5).lineTo(0, -0.5).close();
            // Clip by drawing the heart through each half's mask.
            for (int side = 0; side < 2; ++side) {
                const D h = d.at(Affine::translate(side ? g : -g, 0) * Affine::rotate(side ? 0.06 : -0.06));
                vec::Mask hm = h.cv.fillMask(heartP(0, 0, 0.85), h.m), cut = h.cv.fillMask(side ? r : l, h.m);
                for (int y = 0; y < hm.h; ++y)
                    for (int x = 0; x < hm.w; ++x) hm.v[static_cast<std::size_t>(y) * hm.w + x] *= cut.at(hm.x0 + x, hm.y0 + y);
                h.cv.drawMask(hm, Paint::linear(h.m.apply({0, -0.4}), h.m.apply({0, 0.4}), a, b));
            }
            break;
        }
        case 4: {  // with sparkles
            d.lin(heartP(0, 0, 0.78), {0, -0.4}, {0, 0.4}, a, b);
            for (int i = 0; i < 3; ++i) {
                const double ph = frac(t / 1.5 + i / 3.0);
                const Pt c[] = {{0.34, -0.32}, {-0.36, 0.05}, {0.3, 0.25}};
                d0.at(Affine::translate(c[i].x, c[i].y) * Affine::scale(std::sin(kPi * ph)) * Affine::rotate(ph))
                    .fill(starP(0, 0, 4, 0.09, 0.025), hx("#fff6b0"));
            }
            break;
        }
        case 5: {  // arrow through
            d.lin(heartP(0, 0, 0.75), {0, -0.4}, {0, 0.4}, a, b);
            const double s = 0.03 * wave(t, 1.6);
            const D ar = d0.at(Affine::translate(s, -s) * Affine::rotate(-0.6));
            ar.fill(rrect(-0.5, -0.015, 0.9, 0.03, 0.01), hx("#8a5a2b"));
            ar.fill(poly({{0.48, 0}, {0.36, -0.06}, {0.36, 0.06}}), hx("#c9ced6"));
            ar.fill(poly({{-0.5, 0}, {-0.42, -0.07}, {-0.36, -0.07}, {-0.44, 0}, {-0.36, 0.07}, {-0.42, 0.07}}), hx("#ff6b6b"));
            break;
        }
        default: {  // rising hearts
            for (int i = 0; i < 4; ++i) {
                const double ph = frac(t / 2.4 + i / 4.0);
                const double x = -0.2 + 0.4 * ((i * 37) % 5) / 4.0 + 0.06 * wave(t + i, 1.3);
                d0.at(Affine::translate(x, 0.35 - 0.7 * ph) * Affine::scale(0.5 + 0.5 * std::sin(kPi * ph)))
                    .lin(heartP(0, 0, 0.32), {0, -0.15}, {0, 0.15}, a.withAlpha(static_cast<float>(std::sin(kPi * ph))),
                         b.withAlpha(static_cast<float>(std::sin(kPi * ph))));
            }
        }
    }
}

// ---------------------------------------------------------------- flowers

struct FlowerStyle {
    int petals;
    double len, width, tip;   // petal shape
    const char* c0;           // petal base colour
    const char* c1;           // petal tip colour
    const char* centre0;
    const char* centre1;
    double centreR;
    int layers = 1;           // extra inner layers
    bool notch = false;       // sakura-like notched tips
};

void flower(const D& d0, const FlowerStyle& s) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.15 * wave(t, 6.0)) * Affine::scale(1 + 0.025 * wave(t, 2.4)));
    for (int layer = s.layers; layer >= 0; --layer) {
        const double k = 1 - 0.28 * layer, off = layer * (kPi / s.petals);
        const Path pet = petalShape(s.len * k, s.width * k, s.tip);
        Path all;
        for (int i = 0; i < s.petals; ++i) {
            const double breathe = 1 + 0.04 * wave(t, 2.0, i / double(s.petals));
            all.append(pet, Affine::rotate(off + kTau * i / s.petals) * Affine::scale(1, breathe));
        }
        const Color a = hx(s.c0), b = hx(s.c1);
        const Color la = layer ? mix(a, hx("#000000"), 0.08f * layer) : a;
        d.rad(all, {0, 0}, s.len * k, la, b);
        if (s.notch)
            for (int i = 0; i < s.petals; ++i) {
                const D n = d.at(Affine::rotate(off + kTau * i / s.petals) * Affine::translate(0, -s.len * k));
                n.fill(poly({{-0.025, 0.012}, {0.025, 0.012}, {0, 0.06}}), b);
                n.fill(poly({{-0.03, -0.004}, {0.03, -0.004}, {0, 0.05}}), hx("#ffffff", 0.0f));
            }
        // Fine veins.
        for (int i = 0; i < s.petals; ++i) {
            const D v = d.at(Affine::rotate(off + kTau * i / s.petals));
            v.stroke(line({0, -s.centreR}, {0, -s.len * k * 0.7}), 0.008, mix(a, hx("#000000"), 0.12f).withAlpha(0.35f));
        }
    }
    d.rad(circ(0, 0, s.centreR), {-s.centreR * 0.3, -s.centreR * 0.3}, s.centreR * 1.3, hx(s.centre0), hx(s.centre1));
    // Seed dots in the centre.
    for (int i = 0; i < 9; ++i) {
        const double a = i * 2.39996, r = s.centreR * 0.75 * std::sqrt((i + 0.5) / 9);
        d.fill(circ(std::cos(a) * r, std::sin(a) * r, s.centreR * 0.09), mix(hx(s.centre1), hx("#000000"), 0.3f).withAlpha(0.6f));
    }
}

void sunflower(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.1 * wave(t, 5.0)));
    for (int layer = 1; layer >= 0; --layer) {
        Path all;
        const Path pet = petalShape(0.47, 0.075, 0.8);
        for (int i = 0; i < 18; ++i) all.append(pet, Affine::rotate(layer * kPi / 18 + kTau * i / 18));
        d.rad(all, {0, 0}, 0.47, layer ? hx("#f2a516") : hx("#ffd21f"), layer ? hx("#e88a0c") : hx("#ffb30f"));
    }
    d.rad(circ(0, 0, 0.19), {-0.05, -0.05}, 0.22, hx("#7a4a1d"), hx("#3d2410"));
    for (int i = 0; i < 40; ++i) {
        const double a = i * 2.39996, r = 0.17 * std::sqrt((i + 0.5) / 40);
        d.fill(circ(std::cos(a) * r, std::sin(a) * r, 0.012), hx("#2a170a", 0.8f));
    }
}

void tulip(const D& d0, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.45) * Affine::rotate(0.08 * wave(t, 3.0)) * Affine::translate(0, -0.45));
    d.stroke(line({0, 0.45}, {0, -0.02}), 0.04, hx("#3f9b3a"));
    Path leaf;
    leaf.moveTo(0, 0.42).cubicTo(-0.2, 0.3, -0.25, 0.1, -0.22, 0.0).cubicTo(-0.12, 0.12, -0.04, 0.25, 0, 0.42).close();
    d.lin(leaf, {-0.2, 0}, {0, 0.4}, hx("#6ccf4f"), hx("#2f8a2c"));
    Path cup;
    cup.moveTo(-0.18, -0.2).cubicTo(-0.2, 0.0, -0.1, 0.06, 0, 0.06).cubicTo(0.1, 0.06, 0.2, 0.0, 0.18, -0.2);
    cup.lineTo(0.1, -0.1).lineTo(0.05, -0.3).lineTo(0, -0.14).lineTo(-0.05, -0.3).lineTo(-0.1, -0.1).close();
    d.lin(cup, {0, -0.3}, {0, 0.06}, hx(a), hx(b));
    d.fill(ell(-0.08, -0.08, 0.03, 0.07), hx("#ffffff", 0.25f));
}

void rose(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.06 * wave(t, 4.0)) * Affine::scale(1 + 0.02 * wave(t, 2.2)));
    Path leaf;
    leaf.moveTo(0, 0).cubicTo(0.1, -0.06, 0.22, -0.04, 0.3, 0.0).cubicTo(0.22, 0.06, 0.1, 0.07, 0, 0).close();
    d.at(Affine::translate(0.12, 0.22) * Affine::rotate(0.4)).lin(leaf, {0, 0}, {0.3, 0}, hx("#5fc24a"), hx("#2c7d2a"));
    d.at(Affine::translate(-0.12, 0.24) * Affine::rotate(kPi - 0.3)).lin(leaf, {0, 0}, {0.3, 0}, hx("#5fc24a"), hx("#2c7d2a"));
    // Petals as rings of overlapping discs, darker inward, then a spiral heart.
    for (int ring = 0; ring < 3; ++ring) {
        const int n = 5 + ring;
        const double r = 0.28 - ring * 0.08, pr = 0.16 - ring * 0.035;
        for (int i = 0; i < n; ++i) {
            const double a = kTau * i / n + ring * 0.5;
            const Pt c{std::cos(a) * r * 0.55, std::sin(a) * r * 0.55};
            d.rad(circ(c.x, c.y, pr), {c.x * 1.3, c.y * 1.3}, pr * 1.5, hx("#ff5470"), hx("#c2183b"));
            d.stroke(arc(c.x, c.y, pr, pr, a - 1.2, a + 1.2), 0.008, hx("#8e0f2a", 0.5f));
        }
    }
    Path sp;
    for (int i = 0; i <= 60; ++i) {
        const double a = i * 0.22, r = 0.01 + i * 0.0013;
        if (i == 0) sp.moveTo(std::cos(a) * r, std::sin(a) * r);
        else sp.lineTo(std::cos(a) * r, std::sin(a) * r);
    }
    d.fill(circ(0, 0, 0.09), hx("#d01f44"));
    d.stroke(sp, 0.012, hx("#7d0a22"));
}

void hibiscus(const D& d0) {
    flower(d0, {5, 0.45, 0.22, 0.1, "#ff3d5a", "#ff8a65", "#b0122a", "#7a0618", 0.07});
    const double t = d0.t;
    const D st = d0.at(Affine::rotate(0.15 * wave(t, 6.0) - 0.6));
    st.stroke(line({0, 0}, {0.0, -0.3}), 0.02, hx("#ffe0a0"));
    for (int i = 0; i < 5; ++i) st.fill(circ(-0.04 + 0.02 * i, -0.3 - 0.02 * std::sin(i), 0.018), hx("#ffcf3f"));
}

void lotus(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.1) * Affine::scale(1 + 0.02 * wave(t, 2.6)));
    const Path pet = petalShape(0.42, 0.16, 0.9);
    for (int layer = 0; layer < 3; ++layer) {
        const double spread = 1.25 - layer * 0.35;
        const int n = 5 - layer;
        for (int i = 0; i < n; ++i) {
            const double a = (i - (n - 1) / 2.0) * spread / std::max(1, n - 1) * 1.6;
            const double k = 1 - layer * 0.12;
            d.at(Affine::translate(0, 0.12) * Affine::rotate(a) * Affine::scale(k))
                .lin(pet, {0, 0}, {0, -0.42}, hx("#ffe3ef"), layer == 2 ? hx("#ff6fa6") : hx("#ff8ab8"));
        }
    }
    d.fill(ell(0, 0.17, 0.3, 0.05), hx("#3aa35a"));
}

void daisyBunch(const D& d0) {
    const FlowerStyle s{12, 0.2, 0.05, 0.3, "#ffffff", "#f3f0ff", "#ffd54a", "#f0a10f", 0.06};
    flower(d0.at(Affine::translate(-0.2, 0.08) * Affine::scale(0.95)), s);
    flower(d0.at(Affine::translate(0.2, 0.14) * Affine::scale(0.8)), {12, 0.2, 0.05, 0.3, "#ffd6ec", "#ff9ccc", "#ffd54a", "#f0a10f", 0.06});
    flower(d0.at(Affine::translate(0.02, -0.2) * Affine::scale(0.9)), {12, 0.2, 0.05, 0.3, "#d9ecff", "#8fc3ff", "#ffd54a", "#f0a10f", 0.06});
}

void clover(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.1 * wave(t, 4.0)));
    d.stroke(arc(0.15, 0.3, 0.15, 0.2, kPi * 0.6, kPi * 1.0), 0.035, hx("#2f8a2c"));
    for (int i = 0; i < 4; ++i) {
        const D l = d.at(Affine::rotate(kPi / 4 + kTau * i / 4));
        l.lin(heartP(0, -0.16, 0.3).append(Path{}), {0, -0.3}, {0, 0}, hx("#77dd55"), hx("#2c9a3a"));
        l.stroke(line({0, -0.02}, {0, -0.2}), 0.01, hx("#1f6e28", 0.6f));
    }
}

// ---------------------------------------------------------------- stars & sparkles

void starSticker(const D& d0, const char* a, const char* b, bool outline) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.12 * wave(t, 3.0)) * Affine::scale(1 + 0.05 * wave(t, 1.4)));
    const Path s = starP(0, 0.02, 5, 0.46, 0.2);
    if (outline) {
        d.stroke(s, 0.06, hx(a));
        return;
    }
    d.lin(s, {0, -0.45}, {0, 0.4}, hx(a), hx(b));
    d.stroke(s, 0.02, mix(hx(b), hx("#000000"), 0.2f));
    d.fill(starP(-0.06, -0.06, 5, 0.12, 0.05), hx("#ffffff", 0.35f));
}

void sparkle(const D& d0, int count, const char* color) {
    const double t = d0.t;
    const Pt pos[] = {{0, 0}, {0.28, -0.26}, {-0.3, 0.22}, {0.3, 0.3}, {-0.28, -0.3}};
    const double sz[] = {0.5, 0.22, 0.26, 0.16, 0.18};
    for (int i = 0; i < count; ++i) {
        const double k = 0.55 + 0.45 * std::abs(std::sin(kPi * (t / 1.3 + i * 0.37)));
        const D s = d0.at(Affine::translate(pos[i].x, pos[i].y) * Affine::rotate(0.2 * wave(t, 4.0, i * 0.2)) * Affine::scale(k));
        Path p;
        const double r = sz[i] / 2, w = r * 0.18;
        p.moveTo(0, -r).cubicTo(w, -w, w, -w, r, 0).cubicTo(w, w, w, w, 0, r).cubicTo(-w, w, -w, w, -r, 0).cubicTo(-w, -w, -w, -w, 0, -r).close();
        s.rad(p, {0, 0}, r, hx("#ffffff"), hx(color));
    }
}

void shootingStar(const D& d0) {
    const double t = d0.t;
    const double ph = frac(t / 2.2);
    const D d = d0.at(Affine::translate(-0.06 + 0.12 * ph, 0.06 - 0.12 * ph));
    Path trail;
    trail.moveTo(0.12, -0.12).lineTo(-0.45, 0.33).lineTo(-0.38, 0.4).close();
    d.lin(trail, {0.12, -0.12}, {-0.42, 0.37}, hx("#ffe873", 0.9f), hx("#ff8ad8", 0.0f));
    Path trail2;
    trail2.moveTo(0.16, -0.06).lineTo(-0.3, 0.42).lineTo(-0.2, 0.45).close();
    d.lin(trail2, {0.16, -0.06}, {-0.25, 0.43}, hx("#8fd8ff", 0.8f), hx("#8fd8ff", 0.0f));
    d.at(Affine::translate(0.18, -0.18) * Affine::rotate(t * 2)).lin(starP(0, 0, 5, 0.2, 0.09), {0, -0.2}, {0, 0.2}, hx("#fff6b0"), hx("#ffb000"));
}

void burst(const D& d0, const char* a, const char* b, int points) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(t * 0.3));
    d.lin(starP(0, 0, points, 0.48, 0.38), {0, -0.48}, {0, 0.48}, hx(a), hx(b));
    d.stroke(starP(0, 0, points, 0.42, 0.33), 0.012, hx("#ffffff", 0.7f));
}

void glitter(const D& d0) {
    const double t = d0.t;
    for (int i = 0; i < 14; ++i) {
        const double a = i * 2.39996, r = 0.42 * std::sqrt((i + 0.5) / 14);
        const double k = std::max(0.0, std::sin(kPi * frac(t / 1.2 + i * 0.13)));
        const Color c = Color::hsv(40 + i * 25.0, 0.45, 1);
        d0.at(Affine::translate(std::cos(a) * r, std::sin(a) * r) * Affine::scale(k) * Affine::rotate(kPi / 4))
            .fill(starP(0, 0, 4, 0.07, 0.018), c);
    }
}

// ---------------------------------------------------------------- weather & nature

void sun(const D& d0, bool face) {
    const double t = d0.t;
    const D rays = d0.at(Affine::rotate(t * 0.35));
    Path r;
    for (int i = 0; i < 12; ++i)
        r.append(poly({{-0.045, -0.3}, {0.045, -0.3}, {0, -0.48}}), Affine::rotate(kTau * i / 12));
    rays.lin(r, {0, -0.48}, {0, 0.48}, hx("#ffd21f"), hx("#ff9a1f"));
    d0.rad(circ(0, 0, 0.29), {-0.08, -0.08}, 0.36, hx("#fff27a"), hx("#ffae1a"));
    if (face) {
        eyes(d0.at(Affine::scale(0.75)), blink(t));
        smileMouth(d0.at(Affine::scale(0.7) * Affine::translate(0, 0.04)), 0.18);
        cheeks(d0.at(Affine::scale(0.75)), 0.45f);
    }
}

Path cloudPath(double s = 1) {
    Path p;
    p.append(circ(-0.2 * s, 0.05 * s, 0.17 * s));
    p.append(circ(0.0, -0.06 * s, 0.22 * s));
    p.append(circ(0.21 * s, 0.04 * s, 0.17 * s));
    p.append(rrect(-0.36 * s, 0.0, 0.72 * s, 0.2 * s, 0.1 * s));
    return p;
}

void cloud(const D& d0, int kind) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.02 * wave(t, 4.0), -0.08 + 0.01 * wave(t, 2.0)));
    const bool dark = kind == 1 || kind == 3;
    d.lin(cloudPath(), {0, -0.28}, {0, 0.2}, dark ? hx("#b9c4d6") : hx("#ffffff"), dark ? hx("#7c8ba3") : hx("#d9e6f5"));
    if (kind == 1) {  // rain
        for (int i = 0; i < 6; ++i) {
            const double ph = frac(t / 0.8 + i * 0.37);
            const double x = -0.24 + i * 0.095;
            d.stroke(line({x, 0.24 + 0.22 * ph}, {x - 0.02, 0.3 + 0.22 * ph}), 0.022,
                     hx("#4aa8ff", static_cast<float>(std::sin(kPi * ph))));
        }
    } else if (kind == 2) {  // snow
        for (int i = 0; i < 6; ++i) {
            const double ph = frac(t / 2.0 + i * 0.29);
            const double x = -0.24 + i * 0.095 + 0.02 * wave(t, 1.2, i * 0.3);
            d.fill(circ(x, 0.24 + 0.24 * ph, 0.02), hx("#ffffff", static_cast<float>(std::sin(kPi * ph))));
        }
    } else if (kind == 3) {  // storm
        const double flash = std::max(0.0, 1 - std::abs(frac(t / 1.7) - 0.1) * 8);
        d.fill(poly({{0.02, 0.15}, {-0.1, 0.36}, {0.0, 0.36}, {-0.06, 0.52}, {0.12, 0.3}, {0.02, 0.3}, {0.08, 0.15}}),
               mix(hx("#ffd21f"), hx("#ffffff"), static_cast<float>(flash)));
    }
}

void snowflake(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(t * 0.4));
    const Color c = hx("#bfe6ff");
    for (int i = 0; i < 6; ++i) {
        const D a = d.at(Affine::rotate(kTau * i / 6));
        a.stroke(line({0, 0}, {0, -0.44}), 0.045, c);
        a.stroke(line({0, -0.24}, {0.1, -0.33}), 0.035, c);
        a.stroke(line({0, -0.24}, {-0.1, -0.33}), 0.035, c);
        a.stroke(line({0, -0.12}, {0.07, -0.18}), 0.03, c);
        a.stroke(line({0, -0.12}, {-0.07, -0.18}), 0.03, c);
    }
    d.fill(starP(0, 0, 6, 0.09, 0.05), hx("#ffffff"));
}

void bolt(const D& d0) {
    const double t = d0.t;
    const double flash = std::max(0.0, 1 - std::abs(frac(t / 1.5) - 0.1) * 6);
    const D d = d0.at(Affine::scale(1 + 0.06 * flash));
    const Path p = poly({{0.08, -0.48}, {-0.22, 0.05}, {-0.02, 0.05}, {-0.1, 0.48}, {0.24, -0.08}, {0.04, -0.08}, {0.16, -0.48}});
    d.lin(p, {0, -0.48}, {0, 0.48}, mix(hx("#fff27a"), hx("#ffffff"), static_cast<float>(flash)), hx("#ffae1a"));
    d.stroke(p, 0.02, hx("#e58a00"));
}

void rainbow(const D& d0) {
    const double t = d0.t;
    const char* cols[] = {"#ff4b4b", "#ff9f1c", "#ffe14d", "#4cd964", "#3aa0ff", "#7a5cff"};
    const D d = d0.at(Affine::translate(0, 0.12 + 0.01 * wave(t, 3.0)));
    for (int i = 0; i < 6; ++i) d.stroke(arc(0, 0.1, 0.4 - i * 0.045, 0.4 - i * 0.045, kPi, kTau), 0.05, hx(cols[i]));
    d.fill(cloudPath(0.45).append(Path{}), hx("#ffffff"));
    d.at(Affine::translate(-0.4, 0.06)).fill(cloudPath(0.32), hx("#ffffff"));
    d.at(Affine::translate(0.4, 0.06)).fill(cloudPath(0.32), hx("#ffffff"));
}

void moon(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.08 * wave(t, 4.0)));
    Path m;
    m.moveTo(0.05, -0.42);
    m.cubicTo(-0.3, -0.38, -0.42, 0.0, -0.3, 0.22).cubicTo(-0.18, 0.42, 0.15, 0.46, 0.32, 0.28);
    m.cubicTo(0.0, 0.3, -0.18, 0.05, -0.08, -0.2).cubicTo(-0.04, -0.3, 0.0, -0.38, 0.05, -0.42).close();
    d.lin(m, {-0.3, -0.4}, {0.2, 0.4}, hx("#fff3a6"), hx("#ffc93a"));
    for (int i = 0; i < 3; ++i) {
        const Pt c[] = {{0.25, -0.25}, {0.35, 0.02}, {0.12, -0.05}};
        const double k = 0.5 + 0.5 * std::abs(std::sin(kPi * (t / 1.4 + i * 0.33)));
        d0.at(Affine::translate(c[i].x, c[i].y) * Affine::scale(k)).fill(starP(0, 0, 4, 0.07, 0.02), hx("#fff6c8"));
    }
}

void fire(const D& d0) {
    const double t = d0.t;
    auto flame = [&](double s, const char* a, const char* b, double ph) {
        const double fl = 1 + 0.08 * wave(t, 0.37, ph) + 0.05 * wave(t, 0.23, ph + 0.3);
        const double sway = 0.06 * wave(t, 0.9, ph);
        Path f;
        f.moveTo(0, 0.42 * s);
        f.cubicTo(-0.32 * s, 0.4 * s, -0.36 * s, 0.05 * s, -0.16 * s, -0.12 * s);
        f.cubicTo(-0.12 * s, -0.02 * s, -0.05 * s, 0.0, -0.06 * s, -0.08 * s);
        f.cubicTo(-0.08 * s, -0.25 * s, sway * s, -0.38 * s * fl, sway * s, -0.48 * s * fl);
        f.cubicTo(0.2 * s, -0.3 * s, 0.36 * s, -0.1 * s, 0.3 * s, 0.15 * s);
        f.cubicTo(0.26 * s, 0.35 * s, 0.12 * s, 0.42 * s, 0, 0.42 * s).close();
        d0.at(Affine::translate(0, 0.05)).lin(f, {0, -0.45 * s}, {0, 0.42 * s}, hx(a), hx(b));
    };
    flame(1.0, "#ffcf3a", "#ff3b1f", 0);
    flame(0.65, "#fff5b0", "#ff9a1f", 0.4);
    flame(0.32, "#ffffff", "#ffe27a", 0.7);
}

void drop(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02 * wave(t, 1.6)) * Affine::scale(1 + 0.03 * wave(t, 0.8), 1 - 0.03 * wave(t, 0.8)));
    Path p;
    p.moveTo(0, -0.46).cubicTo(0.12, -0.25, 0.32, -0.05, 0.32, 0.14).cubicTo(0.32, 0.33, 0.17, 0.46, 0, 0.46);
    p.cubicTo(-0.17, 0.46, -0.32, 0.33, -0.32, 0.14).cubicTo(-0.32, -0.05, -0.12, -0.25, 0, -0.46).close();
    d.lin(p, {0, -0.46}, {0, 0.46}, hx("#9fe4ff"), hx("#2e8bff"));
    d.fill(ell(-0.12, 0.1, 0.05, 0.12), hx("#ffffff", 0.6f));
}

void leaf(const D& d0, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.5 + 0.15 * wave(t, 3.0)));
    Path p;
    p.moveTo(0, 0.46).cubicTo(-0.36, 0.2, -0.3, -0.25, 0, -0.46).cubicTo(0.3, -0.25, 0.36, 0.2, 0, 0.46).close();
    d.lin(p, {-0.3, 0}, {0.3, 0}, hx(a), hx(b));
    d.stroke(line({0, 0.46}, {0, -0.4}), 0.02, mix(hx(b), hx("#000000"), 0.25f));
    for (int i = 0; i < 4; ++i) {
        const double y = 0.25 - i * 0.16;
        d.stroke(line({0, y}, {0.17, y - 0.12}), 0.012, mix(hx(b), hx("#000000"), 0.25f).withAlpha(0.7f));
        d.stroke(line({0, y}, {-0.17, y - 0.12}), 0.012, mix(hx(b), hx("#000000"), 0.25f).withAlpha(0.7f));
    }
}

// ---------------------------------------------------------------- celebrations

void balloon(const D& d0, const char* a, const char* b, double phase = 0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.45) * Affine::rotate(0.08 * wave(t, 3.0, phase)) * Affine::translate(0, -0.45 + 0.015 * wave(t, 2.0, phase)));
    Path s;
    s.moveTo(0, 0.2);
    for (int i = 1; i <= 8; ++i) s.lineTo(0.03 * std::sin(i * 1.3 + t * 2), 0.2 + i * 0.033);
    d.stroke(s, 0.01, hx("#7d7d7d"));
    Path bl;
    bl.moveTo(0, -0.44).cubicTo(0.24, -0.44, 0.3, -0.2, 0.28, -0.08).cubicTo(0.25, 0.08, 0.1, 0.18, 0, 0.2);
    bl.cubicTo(-0.1, 0.18, -0.25, 0.08, -0.28, -0.08).cubicTo(-0.3, -0.2, -0.24, -0.44, 0, -0.44).close();
    d.rad(bl, {-0.09, -0.25}, 0.45, hx(a), hx(b));
    d.fill(poly({{0, 0.19}, {-0.035, 0.24}, {0.035, 0.24}}), hx(b));
    d.fill(ell(-0.12, -0.24, 0.04, 0.08), hx("#ffffff", 0.55f));
}

void balloons(const D& d0) {
    balloon(d0.at(Affine::translate(-0.17, 0.06) * Affine::rotate(-0.2) * Affine::scale(0.75)), "#ff8fb1", "#e0245e", 0.3);
    balloon(d0.at(Affine::translate(0.18, 0.06) * Affine::rotate(0.2) * Affine::scale(0.75)), "#8fd3ff", "#1f7fe0", 0.6);
    balloon(d0.at(Affine::translate(0, -0.04) * Affine::scale(0.8)), "#fff07a", "#f0a10f", 0);
}

void gift(const D& d0) {
    const double t = d0.t;
    const double hop = std::max(0.0, wave(t, 1.2));
    d0.lin(rrect(-0.34, -0.06, 0.68, 0.5, 0.04), {0, -0.06}, {0, 0.44}, hx("#ff5a7a"), hx("#d81f4a"));
    d0.fill(rrect(-0.06, -0.06, 0.12, 0.5, 0), hx("#ffd54a"));
    const D lid = d0.at(Affine::translate(0, -0.06 * hop) * Affine::rotate(-0.06 * hop));
    lid.lin(rrect(-0.38, -0.2, 0.76, 0.16, 0.04), {0, -0.2}, {0, -0.04}, hx("#ff7590"), hx("#e0305a"));
    lid.fill(rrect(-0.06, -0.2, 0.12, 0.16, 0), hx("#ffd54a"));
    for (double s : {-1.0, 1.0}) {
        Path bow;
        bow.moveTo(0, -0.2).cubicTo(s * 0.1, -0.38, s * 0.26, -0.36, s * 0.2, -0.24).cubicTo(s * 0.16, -0.18, s * 0.06, -0.2, 0, -0.2).close();
        lid.fill(bow, hx("#ffc21a"));
    }
    lid.fill(circ(0, -0.21, 0.04), hx("#f0a10f"));
}

void popper(const D& d0) {
    const double t = d0.t;
    const D cone = d0.at(Affine::translate(-0.25, 0.25) * Affine::rotate(-kPi / 4));
    cone.lin(poly({{-0.11, -0.12}, {0.11, -0.12}, {0, 0.24}}), {-0.1, 0}, {0.1, 0}, hx("#7c4dff"), hx("#4a1fd6"));
    cone.fill(poly({{-0.07, -0.04}, {0.08, -0.04}, {0.06, 0.02}, {-0.05, 0.02}}), hx("#ffd54a"));
    const double ph = frac(t / 1.6);
    for (int i = 0; i < 16; ++i) {
        const double a = -kPi / 4 + (i / 15.0 - 0.5) * 1.3;
        const double r = 0.1 + 0.55 * (1 - std::pow(1 - ph, 2)) * (0.6 + 0.4 * ((i * 7) % 5) / 4.0);
        const double x = -0.25 + std::cos(a) * r, y = 0.25 + std::sin(a) * r + 0.15 * ph * ph;
        const Color c = Color::hsv(i * 47.0, 0.75, 1).withAlpha(static_cast<float>(std::min(1.0, 3 * (1 - ph))));
        const D p = d0.at(Affine::translate(x, y) * Affine::rotate(i + ph * 8));
        if (i % 3 == 0) p.fill(circ(0, 0, 0.022), c);
        else p.fill(rrect(-0.03, -0.012, 0.06, 0.024, 0.005), c);
    }
}

void confetti(const D& d0) {
    const double t = d0.t;
    for (int i = 0; i < 22; ++i) {
        const double ph = frac(t / 2.6 + i * 0.137);
        const double x = -0.45 + 0.9 * frac(i * 0.618) + 0.05 * wave(t, 1.3, i * 0.2);
        const double y = -0.48 + 0.96 * ph;
        const Color c = Color::hsv(i * 37.0, 0.7, 1).withAlpha(static_cast<float>(std::min(1.0, std::sin(kPi * ph) * 3)));
        const D p = d0.at(Affine::translate(x, y) * Affine::rotate(i + t * 3) * Affine::scale(1, std::cos(t * 4 + i)));
        p.fill(rrect(-0.025, -0.012, 0.05, 0.024, 0.004), c);
    }
}

void cake(const D& d0) {
    const double t = d0.t;
    d0.fill(ell(0, 0.4, 0.42, 0.06), hx("#e6e6ee"));
    d0.lin(rrect(-0.34, 0.05, 0.68, 0.33, 0.05), {0, 0.05}, {0, 0.38}, hx("#ffb0c8"), hx("#ff7aa2"));
    d0.lin(rrect(-0.26, -0.15, 0.52, 0.22, 0.05), {0, -0.15}, {0, 0.07}, hx("#ffe9f1"), hx("#ffc2d6"));
    Path drip;
    drip.moveTo(-0.34, 0.08);
    for (int i = 0; i <= 8; ++i) drip.lineTo(-0.34 + i * 0.085, 0.08 + (i % 2 ? 0.08 : 0.03));
    drip.lineTo(0.34, 0.03).lineTo(-0.34, 0.03).close();
    d0.fill(drip, hx("#ffffff"));
    for (int i = 0; i < 5; ++i) d0.fill(circ(-0.2 + i * 0.1, 0.25, 0.022), Color::hsv(i * 70.0, 0.6, 1));
    for (double x : {-0.12, 0.0, 0.12}) {
        d0.lin(rrect(x - 0.02, -0.33, 0.04, 0.18, 0.01), {x - 0.02, 0}, {x + 0.02, 0}, hx("#8fd3ff"), hx("#3f8fe0"));
        const double fl = 1 + 0.15 * wave(t, 0.3, x);
        const D f = d0.at(Affine::translate(x + 0.01 * wave(t, 0.7, x), -0.36) * Affine::scale(1, fl));
        f.lin(ell(0, -0.03, 0.025, 0.05), {0, -0.08}, {0, 0.02}, hx("#fff3a0"), hx("#ff8a1f"));
    }
}

void firework(const D& d0, const char* color) {
    const double t = d0.t;
    for (int k = 0; k < 2; ++k) {
        const double ph = frac(t / 1.8 + k * 0.5);
        const Pt c = k ? Pt{0.12, 0.08} : Pt{-0.1, -0.1};
        const double s = k ? 0.75 : 1;
        const double r = 0.42 * s * (1 - std::pow(1 - ph, 3));
        const float a = static_cast<float>(std::min(1.0, 2.5 * (1 - ph)));
        for (int i = 0; i < 12; ++i) {
            const double ang = kTau * i / 12 + k * 0.2;
            const Pt p{c.x + std::cos(ang) * r, c.y + std::sin(ang) * r + 0.08 * ph * ph};
            const Pt q{c.x + std::cos(ang) * r * 0.65, c.y + std::sin(ang) * r * 0.65 + 0.06 * ph * ph};
            const Color col = k ? hx("#ffd54a") : hx(color);
            d0.stroke(line(q, p), 0.025 * s, col.withAlpha(a));
            d0.fill(circ(p.x, p.y, 0.022 * s), hx("#ffffff", a));
        }
    }
}

void crown(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.05 * wave(t, 2.6)));
    const Path c = poly({{-0.4, 0.25}, {-0.42, -0.2}, {-0.2, 0.0}, {0, -0.3}, {0.2, 0.0}, {0.42, -0.2}, {0.4, 0.25}});
    d.lin(c, {0, -0.3}, {0, 0.25}, hx("#ffe873"), hx("#f0a10f"));
    d.lin(rrect(-0.42, 0.2, 0.84, 0.12, 0.03), {0, 0.2}, {0, 0.32}, hx("#ffd21f"), hx("#d98a00"));
    for (int i = 0; i < 3; ++i) {
        const Pt p[] = {{-0.42, -0.22}, {0, -0.32}, {0.42, -0.22}};
        d.fill(circ(p[i].x, p[i].y, 0.05), hx("#ff4f6d"));
    }
    for (double x : {-0.2, 0.0, 0.2}) d.fill(circ(x, 0.26, 0.035), x == 0 ? hx("#3fa9ff") : hx("#42d77d"));
    sparkle(d0.at(Affine::translate(0.3, -0.35) * Affine::scale(0.3)), 1, "#fff6b0");
}

void trophy(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.02 * wave(t, 1.8)));
    for (double s : {-1.0, 1.0}) d.stroke(arc(s * 0.24, -0.2, 0.1, 0.12, s > 0 ? -kPi / 2 : kPi / 2, s > 0 ? kPi / 2 : kPi * 1.5), 0.045, hx("#f0a10f"));
    Path cup;
    cup.moveTo(-0.26, -0.36).lineTo(0.26, -0.36).cubicTo(0.26, -0.05, 0.15, 0.06, 0, 0.08).cubicTo(-0.15, 0.06, -0.26, -0.05, -0.26, -0.36).close();
    d.lin(cup, {-0.26, 0}, {0.26, 0}, hx("#fff07a"), hx("#e89a0a"));
    d.fill(rrect(-0.04, 0.06, 0.08, 0.16, 0.01), hx("#e8a00a"));
    d.lin(rrect(-0.2, 0.22, 0.4, 0.14, 0.03), {0, 0.22}, {0, 0.36}, hx("#8a5a2b"), hx("#5c3a18"));
    d.fill(starP(0, -0.16, 5, 0.09, 0.04), hx("#ffffff", 0.8f));
    const double sh = frac(t / 2.4);
    d.fill(poly({{-0.2 + 0.4 * sh, -0.36}, {-0.14 + 0.4 * sh, -0.36}, {-0.22 + 0.4 * sh, 0.0}, {-0.27 + 0.4 * sh, 0.0}}),
           hx("#ffffff", static_cast<float>(0.4 * std::sin(kPi * sh))));
}

void medal(const D& d0) {
    const double t = d0.t;
    d0.fill(poly({{-0.2, -0.48}, {-0.05, -0.48}, {0.05, -0.1}, {-0.1, -0.1}}), hx("#3f8fe0"));
    d0.fill(poly({{0.2, -0.48}, {0.05, -0.48}, {-0.05, -0.1}, {0.1, -0.1}}), hx("#e0305a"));
    const D m = d0.at(Affine::translate(0, 0.14) * Affine::rotate(0.15 * wave(t, 2.0)) * Affine::scale(std::cos(0.4 * wave(t, 3.0)), 1));
    m.rad(circ(0, 0, 0.3), {-0.1, -0.1}, 0.4, hx("#fff07a"), hx("#e89a0a"));
    m.stroke(circ(0, 0, 0.23), 0.02, hx("#c47a00"));
    m.fill(starP(0, 0.01, 5, 0.15, 0.065), hx("#ffffff", 0.9f));
}

void bow(const D& d0, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.03 * wave(t, 1.5)));
    for (double s : {-1.0, 1.0}) {
        Path w;
        w.moveTo(0, 0).cubicTo(s * 0.2, -0.32, s * 0.48, -0.25, s * 0.44, 0.0).cubicTo(s * 0.48, 0.25, s * 0.2, 0.32, 0, 0).close();
        d.lin(w, {0, 0}, {s * 0.45, 0}, hx(a), hx(b));
        Path tail;
        tail.moveTo(s * 0.03, 0.03).lineTo(s * 0.22, 0.45).lineTo(s * 0.12, 0.4).lineTo(s * 0.08, 0.47).lineTo(-s * 0.02, 0.06).close();
        d.fill(tail, hx(b));
    }
    d.rad(rrect(-0.08, -0.09, 0.16, 0.18, 0.05), {0, 0}, 0.12, hx(a), hx(b));
}

void lantern(const D& d0) {  // đèn lồng
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, -0.48) * Affine::rotate(0.1 * wave(t, 2.4)) * Affine::translate(0, 0.48));
    d.stroke(line({0, -0.48}, {0, -0.3}), 0.015, hx("#8a5a2b"));
    d.fill(rrect(-0.12, -0.32, 0.24, 0.06, 0.02), hx("#ffc21a"));
    d.rad(ell(0, 0.0, 0.3, 0.27), {0, 0}, 0.35, hx("#ff6a4a"), hx("#c8102e"));
    for (double s : {-0.18, -0.06, 0.06, 0.18}) d.stroke(arc(0, 0, std::abs(s) * 1.5, 0.27, -kPi / 2, kPi / 2), 0.01, hx("#ffd54a", 0.5f));
    d.fill(rrect(-0.12, 0.25, 0.24, 0.06, 0.02), hx("#ffc21a"));
    for (int i = 0; i < 5; ++i) d.stroke(line({-0.06 + i * 0.03, 0.31}, {-0.06 + i * 0.03 + 0.01 * wave(t, 1.0, i * 0.1), 0.46}), 0.012, hx("#ffc21a"));
    d.fill(ell(0, 0, 0.12, 0.2), hx("#fff3a0", static_cast<float>(0.25 + 0.1 * wave(t, 0.8))));
}

void envelope(const D& d0) {  // lì xì
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.06 * wave(t, 2.2)));
    d.lin(rrect(-0.28, -0.42, 0.56, 0.84, 0.05), {0, -0.42}, {0, 0.42}, hx("#ff4a3d"), hx("#c8102e"));
    d.fill(poly({{-0.28, -0.4}, {0.28, -0.4}, {0, -0.12}}), hx("#e0281f"));
    d.rad(circ(0, -0.1, 0.12), {0, -0.1}, 0.14, hx("#fff07a"), hx("#e89a0a"));
    d.fill(starP(0, -0.1, 5, 0.07, 0.03), hx("#c8102e"));
    for (int i = 0; i < 3; ++i) d.stroke(arc(0, 0.2, 0.12 + i * 0.04, 0.05 + i * 0.02, 0, kPi), 0.012, hx("#ffd54a", 0.7f));
    sparkle(d0.at(Affine::translate(0.3, -0.38) * Affine::scale(0.3)), 1, "#ffd54a");
}

void hoaMai(const D& d0) { flower(d0, {5, 0.42, 0.2, 0.15, "#ffe14d", "#ffb800", "#ff9500", "#c86400", 0.08}); }
void hoaDao(const D& d0) { flower(d0, {5, 0.42, 0.2, 0.0, "#ffe5ef", "#ff7fae", "#ff4f8b", "#c2185b", 0.07, 0, true}); }

void tree(const D& d0) {  // Christmas tree
    const double t = d0.t;
    d0.fill(rrect(-0.06, 0.32, 0.12, 0.14, 0.02), hx("#7a4a1d"));
    for (int i = 0; i < 3; ++i) {
        const double y = -0.3 + i * 0.2, w = 0.2 + i * 0.1;
        d0.lin(poly({{0, y - 0.16}, {w, y + 0.18}, {-w, y + 0.18}}), {0, y - 0.16}, {0, y + 0.18}, hx("#4cd36a"), hx("#1f8a3a"));
    }
    for (int i = 0; i < 8; ++i) {
        const double x = -0.22 + 0.44 * frac(i * 0.618), y = -0.2 + 0.5 * frac(i * 0.382);
        if (std::abs(x) > 0.08 + (y + 0.3) * 0.5) continue;
        const double on = 0.5 + 0.5 * wave(t, 1.0, i * 0.3);
        d0.fill(circ(x, y, 0.028), mix(Color::hsv(i * 60.0, 0.7, 0.8), hx("#ffffff"), static_cast<float>(on * 0.5)));
    }
    d0.at(Affine::translate(0, -0.44) * Affine::rotate(0.2 * wave(t, 3.0))).fill(starP(0, 0, 5, 0.08, 0.035), hx("#ffd21f"));
}

// ---------------------------------------------------------------- signs & shapes

void arrow(const D& d0, double angle, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(angle) * Affine::translate(0.05 * std::max(0.0, wave(t, 0.9)), 0));
    const Path p = poly({{-0.42, -0.1}, {0.05, -0.1}, {0.05, -0.26}, {0.44, 0}, {0.05, 0.26}, {0.05, 0.1}, {-0.42, 0.1}});
    d.lin(p, {-0.4, 0}, {0.44, 0}, hx(a), hx(b));
    d.stroke(p, 0.02, mix(hx(b), hx("#000000"), 0.25f));
}

void curvedArrow(const D& d0) {
    const double t = d0.t;
    const double k = drawOn(t);
    Path a;
    const int n = 30;
    for (int i = 0; i <= static_cast<int>(n * k); ++i) {
        const double u = static_cast<double>(i) / n;
        const double x = -0.38 + 0.7 * u, y = 0.25 - 0.55 * std::sin(kPi * u * 0.85);
        if (i == 0) a.moveTo(x, y);
        else a.lineTo(x, y);
    }
    d0.stroke(a, 0.06, hx("#ff4f6d"));
    const double u = std::max(0.02, k);
    const double x = -0.38 + 0.7 * u, y = 0.25 - 0.55 * std::sin(kPi * u * 0.85);
    const double dy = -0.55 * kPi * 0.85 * std::cos(kPi * u * 0.85), dx = 0.7;
    d0.at(Affine::translate(x, y) * Affine::rotate(std::atan2(dy, dx))).fill(poly({{0.1, 0}, {-0.06, -0.09}, {-0.06, 0.09}}), hx("#ff4f6d"));
}

void check(const D& d0, bool ok) {
    const double t = d0.t;
    const double k = drawOn(t);
    d0.rad(circ(0, 0, 0.44), {-0.1, -0.1}, 0.5, ok ? hx("#6be38a") : hx("#ff7b7b"), ok ? hx("#1fae4f") : hx("#e02f2f"));
    if (ok) {
        Path c;
        const Pt p0{-0.2, 0.0}, p1{-0.06, 0.15}, p2{0.22, -0.15};
        c.moveTo(p0.x, p0.y);
        if (k < 0.35) c.lineTo(p0.x + (p1.x - p0.x) * k / 0.35, p0.y + (p1.y - p0.y) * k / 0.35);
        else c.lineTo(p1.x, p1.y).lineTo(p1.x + (p2.x - p1.x) * (k - 0.35) / 0.65, p1.y + (p2.y - p1.y) * (k - 0.35) / 0.65);
        d0.stroke(c, 0.09, hx("#ffffff"));
    } else {
        const double s = std::min(1.0, k * 2), s2 = std::clamp(k * 2 - 1, 0.0, 1.0);
        d0.stroke(line({-0.17, -0.17}, {-0.17 + 0.34 * s, -0.17 + 0.34 * s}), 0.09, hx("#ffffff"));
        if (s2 > 0) d0.stroke(line({0.17, -0.17}, {0.17 - 0.34 * s2, -0.17 + 0.34 * s2}), 0.09, hx("#ffffff"));
    }
}

void mark(const D& d0, bool question) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.12 * wave(t, 1.2)) * Affine::scale(1 + 0.04 * wave(t, 0.6)));
    const Color c = question ? hx("#3fa9ff") : hx("#ff4f4f");
    if (question) {
        d.stroke(arc(0, -0.18, 0.18, 0.18, kPi * 1.0, kPi * 2.35), 0.1, c);
        d.stroke(line({0.0, 0.02}, {0.0, 0.14}), 0.1, c);
        d.fill(circ(0, 0.33, 0.065), c);
    } else {
        d.fill(poly({{-0.08, -0.42}, {0.08, -0.42}, {0.05, 0.16}, {-0.05, 0.16}}), c);
        d.fill(circ(0, 0.33, 0.07), c);
    }
}

void bubble(const D& d0, int kind, const char* fillC) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.025 * wave(t, 1.8)));
    if (kind == 0) {  // speech
        Path b;
        b.append(ell(0, -0.08, 0.44, 0.32));
        b.append(poly({{-0.2, 0.12}, {-0.05, 0.18}, {-0.32, 0.42}}));
        d.fill(b, hx(fillC));
        d.stroke(ell(0, -0.08, 0.44, 0.32), 0.025, hx("#1c1c22"));
        for (int i = 0; i < 3; ++i) {
            const double k = 0.6 + 0.4 * std::max(0.0, wave(t, 1.2, -i * 0.15));
            d.fill(circ(-0.15 + i * 0.15, -0.08, 0.04 * k), hx("#1c1c22"));
        }
    } else if (kind == 1) {  // thought
        Path c;
        c.append(cloudPath(1.15), Affine::translate(0, -0.12));
        d.fill(c, hx(fillC));
        for (int i = 0; i < 2; ++i) d.fill(circ(-0.25 - i * 0.1, 0.28 + i * 0.1, 0.06 - i * 0.02), hx(fillC));
    } else {  // shout
        const Path s = starP(0, 0, 12, 0.48, 0.36, t * 0.2);
        d.fill(s, hx(fillC));
        d.stroke(s, 0.025, hx("#1c1c22"));
    }
}

void pin(const D& d0) {
    const double t = d0.t;
    const double hop = std::abs(std::sin(kPi * t / 1.0));
    d0.fill(ell(0, 0.43, 0.14 - 0.04 * hop, 0.035), hx("#000000", 0.25f));
    const D d = d0.at(Affine::translate(0, -0.08 * hop));
    Path p;
    p.moveTo(0, 0.42).cubicTo(-0.1, 0.25, -0.3, 0.05, -0.3, -0.14).cubicTo(-0.3, -0.33, -0.16, -0.46, 0, -0.46);
    p.cubicTo(0.16, -0.46, 0.3, -0.33, 0.3, -0.14).cubicTo(0.3, 0.05, 0.1, 0.25, 0, 0.42).close();
    d.lin(p, {0, -0.46}, {0, 0.42}, hx("#ff6b6b"), hx("#d81f3a"));
    d.fill(circ(0, -0.14, 0.11), hx("#ffffff"));
}

void bell(const D& d0) {
    const double t = d0.t;
    const double sw = 0.35 * wave(t, 0.8) * (0.5 + 0.5 * wave(t, 3.2));
    const D d = d0.at(Affine::translate(0, -0.4) * Affine::rotate(sw) * Affine::translate(0, 0.4));
    d.at(Affine::translate(0.12 * std::sin(sw * 2), 0)).fill(circ(0, 0.33, 0.07), hx("#c47a00"));
    Path b;
    b.moveTo(-0.36, 0.27).cubicTo(-0.25, 0.15, -0.28, -0.32, 0, -0.34).cubicTo(0.28, -0.32, 0.25, 0.15, 0.36, 0.27).close();
    d.lin(b, {-0.3, 0}, {0.3, 0}, hx("#ffe873"), hx("#f0a10f"));
    d.fill(circ(0, -0.38, 0.05), hx("#f0a10f"));
    d.fill(ell(-0.1, -0.1, 0.04, 0.12), hx("#ffffff", 0.5f));
}

void note(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.04 * wave(t, 1.6)) * Affine::rotate(0.12 * wave(t, 1.6, 0.25)));
    d.fill(ell(-0.2, 0.3, 0.12, 0.09), hx("#7c4dff"));
    d.fill(ell(0.22, 0.2, 0.12, 0.09), hx("#7c4dff"));
    d.fill(rrect(-0.1, -0.38, 0.05, 0.68, 0.01), hx("#7c4dff"));
    d.fill(rrect(0.32, -0.48, 0.05, 0.68, 0.01), hx("#7c4dff"));
    d.fill(poly({{-0.1, -0.38}, {0.37, -0.48}, {0.37, -0.36}, {-0.1, -0.26}}), hx("#7c4dff"));
}

void playButton(const D& d0) {
    const double t = d0.t;
    const double ph = frac(t / 1.5);
    d0.stroke(circ(0, 0, 0.3 + 0.15 * ph), 0.03, hx("#ff3b3b", static_cast<float>(1 - ph)));
    d0.rad(rrect(-0.34, -0.25, 0.68, 0.5, 0.12), {-0.1, -0.1}, 0.5, hx("#ff6464"), hx("#e01b1b"));
    d0.fill(poly({{-0.08, -0.12}, {0.14, 0}, {-0.08, 0.12}}), hx("#ffffff"));
}

void circleMark(const D& d0) {  // hand-drawn circle that draws itself
    const double t = d0.t;
    const double k = drawOn(t);
    Path c;
    const int n = 60;
    for (int i = 0; i <= static_cast<int>(n * 1.08 * k); ++i) {
        const double a = -kPi * 0.8 + kTau * i / n;
        const double r = 0.4 + 0.03 * std::sin(i * 0.3) + 0.02 * i / n;
        if (i == 0) c.moveTo(std::cos(a) * r, std::sin(a) * r * 0.75);
        else c.lineTo(std::cos(a) * r, std::sin(a) * r * 0.75);
    }
    d0.stroke(c, 0.045, hx("#ff3b3b"));
}

void underline(const D& d0) {
    const double t = d0.t;
    const double k = drawOn(t);
    Path c;
    for (int i = 0; i <= static_cast<int>(40 * k); ++i) {
        const double u = i / 40.0;
        const double x = -0.44 + 0.88 * u, y = 0.1 * std::sin(u * kPi * 1.0) - 0.08 * u + 0.05;
        if (i == 0) c.moveTo(x, y);
        else c.lineTo(x, y);
    }
    d0.stroke(c, 0.06, hx("#ffd21f"));
}

void plusSign(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.1 * wave(t, 2.0)) * Affine::scale(1 + 0.05 * wave(t, 1.0)));
    d.rad(circ(0, 0, 0.42), {-0.1, -0.1}, 0.5, hx("#7fd6ff"), hx("#1f8fe0"));
    d.fill(rrect(-0.06, -0.24, 0.12, 0.48, 0.04), hx("#ffffff"));
    d.fill(rrect(-0.24, -0.06, 0.48, 0.12, 0.04), hx("#ffffff"));
}

void magnifier(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.05 * wave(t, 2.4), 0.05 * wave(t, 2.4, 0.25)));
    d.at(Affine::rotate(kPi / 4)).fill(rrect(0.22, -0.05, 0.28, 0.1, 0.04), hx("#5c3a18"));
    d.fill(circ(-0.08, -0.08, 0.27), hx("#cfeeff", 0.5f));
    d.stroke(circ(-0.08, -0.08, 0.27), 0.06, hx("#4a4f5c"));
    d.fill(ell(-0.17, -0.17, 0.05, 0.09), hx("#ffffff", 0.7f));
}

void ring(const D& d0, const char* color) {
    const double t = d0.t;
    for (int i = 0; i < 3; ++i) {
        const double ph = frac(t / 1.8 + i / 3.0);
        d0.stroke(circ(0, 0, 0.08 + 0.38 * ph), 0.04 * (1 - ph) + 0.005, hx(color, static_cast<float>(1 - ph)));
    }
    d0.fill(circ(0, 0, 0.08), hx(color));
}

// ---------------------------------------------------------------- food

void apple(const D& d0, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.06 * wave(t, 2.4)));
    Path p;
    p.moveTo(0, -0.24).cubicTo(0.2, -0.38, 0.44, -0.24, 0.4, 0.04).cubicTo(0.37, 0.3, 0.2, 0.44, 0.08, 0.4);
    p.cubicTo(0.03, 0.38, -0.03, 0.38, -0.08, 0.4).cubicTo(-0.2, 0.44, -0.37, 0.3, -0.4, 0.04);
    p.cubicTo(-0.44, -0.24, -0.2, -0.38, 0, -0.24).close();
    d.rad(p, {-0.15, -0.12}, 0.55, hx(a), hx(b));
    d.stroke(line({0, -0.24}, {0.04, -0.42}), 0.035, hx("#6b3e1a"));
    Path lf;
    lf.moveTo(0.04, -0.34).cubicTo(0.12, -0.46, 0.26, -0.44, 0.3, -0.4).cubicTo(0.22, -0.3, 0.1, -0.3, 0.04, -0.34).close();
    d.fill(lf, hx("#4cc35a"));
    d.fill(ell(-0.2, -0.08, 0.05, 0.1), hx("#ffffff", 0.45f));
}

void cherry(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.08, -0.42) * Affine::rotate(0.1 * wave(t, 2.0)) * Affine::translate(-0.08, 0.42));
    Path s1, s2;
    s1.moveTo(0.08, -0.42).cubicTo(0.0, -0.2, -0.12, 0.0, -0.16, 0.12);
    s2.moveTo(0.08, -0.42).cubicTo(0.12, -0.2, 0.18, 0.0, 0.18, 0.16);
    d.stroke(s1, 0.025, hx("#4f8a2a"));
    d.stroke(s2, 0.025, hx("#4f8a2a"));
    for (const Pt c : {Pt{-0.17, 0.25}, Pt{0.18, 0.28}}) {
        d.rad(circ(c.x, c.y, 0.16), {c.x - 0.05, c.y - 0.05}, 0.22, hx("#ff4f5e"), hx("#a8061e"));
        d.fill(ell(c.x - 0.06, c.y - 0.05, 0.03, 0.05), hx("#ffffff", 0.6f));
    }
    Path lf;
    lf.moveTo(0.08, -0.42).cubicTo(0.18, -0.5, 0.32, -0.46, 0.38, -0.38).cubicTo(0.26, -0.32, 0.14, -0.34, 0.08, -0.42).close();
    d.fill(lf, hx("#5cc85a"));
}

void strawberry(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.07 * wave(t, 2.2)));
    Path p;
    p.moveTo(0, 0.44).cubicTo(-0.25, 0.3, -0.38, 0.0, -0.33, -0.18).cubicTo(-0.28, -0.32, -0.1, -0.32, 0, -0.28);
    p.cubicTo(0.1, -0.32, 0.28, -0.32, 0.33, -0.18).cubicTo(0.38, 0.0, 0.25, 0.3, 0, 0.44).close();
    d.rad(p, {-0.1, -0.1}, 0.55, hx("#ff5a6a"), hx("#c8102e"));
    for (int row = 0; row < 5; ++row)
        for (int col = -3; col <= 3; ++col) {
            const double y = -0.16 + row * 0.11, x = col * 0.09 + (row % 2 ? 0.045 : 0);
            if (std::abs(x) > 0.27 * (1 - (y + 0.16) * 0.95)) continue;
            d.fill(ell(x, y, 0.012, 0.02), hx("#ffe27a"));
        }
    d.fill(starP(0, -0.3, 6, 0.18, 0.07), hx("#3fae3a"));
}

void watermelon(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.05 * wave(t, 2.8)));
    d.fill(pie(0, -0.15, 0.46, 0.5, 0, kPi), hx("#2f9a3a"));
    d.fill(pie(0, -0.15, 0.42, 0.46, 0, kPi), hx("#c8f5a0"));
    d.lin(pie(0, -0.15, 0.38, 0.42, 0, kPi), {0, -0.15}, {0, 0.3}, hx("#ff5a6a"), hx("#ff2d4a"));
    for (int i = 0; i < 7; ++i) {
        const double a = kPi * (0.15 + 0.7 * i / 6.0), r = i % 2 ? 0.22 : 0.3;
        d.at(Affine::translate(std::cos(a) * r, -0.15 + std::sin(a) * r * 1.05) * Affine::rotate(a)).fill(ell(0, 0, 0.025, 0.015), hx("#2a1a10"));
    }
}

void citrus(const D& d0, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(t * 0.25));
    d.fill(circ(0, 0, 0.44), hx(b));
    d.fill(circ(0, 0, 0.39), hx("#fffbe6"));
    Path seg;
    for (int i = 0; i < 8; ++i) seg.append(pie(0, 0, 0.35, 0.35, kTau * i / 8 + 0.06, kTau * (i + 1) / 8 - 0.06, 8));
    d.rad(seg, {0, 0}, 0.36, hx("#ffffff"), hx(a));
}

void coffee(const D& d0) {
    const double t = d0.t;
    for (int i = 0; i < 3; ++i) {
        Path s;
        const double ph = frac(t / 2.0 + i / 3.0);
        for (int k = 0; k <= 12; ++k) {
            const double u = k / 12.0;
            const double x = -0.1 + i * 0.1 + 0.04 * std::sin(u * 6 + t * 3 + i), y = -0.2 - 0.25 * u - 0.05 * ph;
            if (k == 0) s.moveTo(x, y);
            else s.lineTo(x, y);
        }
        d0.stroke(s, 0.025, hx("#ffffff", static_cast<float>(0.6 * std::sin(kPi * ph))));
    }
    d0.stroke(arc(0.3, 0.08, 0.1, 0.11, -kPi / 2, kPi / 2), 0.045, hx("#e0e4ec"));
    Path cup;
    cup.moveTo(-0.3, -0.12).lineTo(0.3, -0.12).lineTo(0.24, 0.34).cubicTo(0.22, 0.4, 0.18, 0.42, 0.12, 0.42);
    cup.lineTo(-0.12, 0.42).cubicTo(-0.18, 0.42, -0.22, 0.4, -0.24, 0.34).close();
    d0.lin(cup, {-0.3, 0}, {0.3, 0}, hx("#ffffff"), hx("#d5dbe6"));
    d0.fill(ell(0, -0.12, 0.3, 0.05), hx("#7a4a1d"));
    d0.at(Affine::translate(0, -0.12) * Affine::scale(1, 0.4)).fill(heartP(0, 0, 0.16), hx("#f5deb3"));
}

void boba(const D& d0) {
    const double t = d0.t;
    d0.at(Affine::rotate(0.15)).fill(rrect(0.04, -0.5, 0.05, 0.4, 0.02), hx("#ff5a8a"));
    Path cup;
    cup.moveTo(-0.26, -0.2).lineTo(0.26, -0.2).lineTo(0.2, 0.44).lineTo(-0.2, 0.44).close();
    d0.lin(cup, {0, -0.2}, {0, 0.44}, hx("#f6d7b0"), hx("#d6a46a"));
    for (int i = 0; i < 9; ++i) {
        const double x = -0.15 + (i % 5) * 0.075 + (i / 5) * 0.035, y = 0.36 - (i / 5) * 0.07 + 0.01 * wave(t, 1.4, i * 0.1);
        d0.fill(circ(x, y, 0.032), hx("#3a2214"));
    }
    d0.fill(ell(0, -0.2, 0.3, 0.07), hx("#ffffff", 0.9f));
    d0.stroke(ell(0, -0.2, 0.3, 0.07), 0.02, hx("#c9ced6"));
}

void iceCream(const D& d0) {
    const double t = d0.t;
    d0.lin(poly({{-0.2, -0.02}, {0.2, -0.02}, {0, 0.46}}), {-0.2, 0}, {0.2, 0}, hx("#f5c27a"), hx("#c8873a"));
    for (int i = 0; i < 4; ++i) d0.stroke(line({-0.17 + i * 0.1, -0.02}, {-0.02 + i * 0.03, 0.38}), 0.012, hx("#a8692a", 0.6f));
    const D s = d0.at(Affine::translate(0, -0.05) * Affine::scale(1 + 0.03 * wave(t, 1.6), 1 - 0.03 * wave(t, 1.6)) * Affine::translate(0, 0.05));
    s.fill(circ(-0.1, -0.12, 0.16), hx("#ffb3cf"));
    s.fill(circ(0.1, -0.12, 0.16), hx("#bdf0d0"));
    s.fill(circ(0, -0.28, 0.17), hx("#fff1c9"));
    s.fill(circ(0.02, -0.46, 0.05), hx("#e0243e"));
    for (int i = 0; i < 6; ++i) s.at(Affine::translate(-0.15 + 0.06 * i, -0.3 + 0.04 * std::sin(i * 2.0)) * Affine::rotate(i)).fill(rrect(-0.02, -0.006, 0.04, 0.012, 0.004), Color::hsv(i * 60.0, 0.7, 1));
}

void donut(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(t * 0.3));
    Path ring;
    ring.append(circ(0, 0, 0.44));
    d.rad(ring, {-0.1, -0.1}, 0.5, hx("#f3c27e"), hx("#c8873a"));
    Path icing;
    for (int i = 0; i <= 36; ++i) {
        const double a = kTau * i / 36, r = 0.38 + 0.03 * std::sin(i * 1.7);
        if (i == 0) icing.moveTo(std::cos(a) * r, std::sin(a) * r);
        else icing.lineTo(std::cos(a) * r, std::sin(a) * r);
    }
    icing.close();
    d.fill(icing, hx("#ff8fc0"));
    d.fill(circ(0, 0, 0.13), hx("#c8873a"));
    d.fill(circ(0, 0, 0.1), hx("#000000", 0.0f));
    for (int i = 0; i < 14; ++i) {
        const double a = i * 2.39996, r = 0.18 + 0.15 * frac(i * 0.618);
        d.at(Affine::translate(std::cos(a) * r, std::sin(a) * r) * Affine::rotate(a * 3)).fill(rrect(-0.03, -0.008, 0.06, 0.016, 0.006), Color::hsv(i * 50.0, 0.7, 1));
    }
}

void pizza(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.06 * wave(t, 2.5)));
    d.fill(poly({{-0.4, -0.38}, {0.4, -0.38}, {0, 0.46}}), hx("#ffd36b"));
    d.fill(rrect(-0.44, -0.46, 0.88, 0.12, 0.06), hx("#d48a3a"));
    for (const Pt c : {Pt{-0.12, -0.18}, Pt{0.14, -0.2}, Pt{0.0, 0.08}}) d.fill(circ(c.x, c.y, 0.07), hx("#e0353a"));
    // A drip of cheese that stretches.
    const double k = 0.5 + 0.5 * wave(t, 1.6);
    d.fill(rrect(0.05, 0.1, 0.05, 0.12 + 0.08 * k, 0.025), hx("#ffd36b"));
}

void cupcake(const D& d0) {
    const double t = d0.t;
    d0.lin(poly({{-0.28, 0.0}, {0.28, 0.0}, {0.2, 0.44}, {-0.2, 0.44}}), {-0.28, 0}, {0.28, 0}, hx("#7fd0ff"), hx("#3a8fd6"));
    for (int i = 0; i < 5; ++i) d0.stroke(line({-0.2 + i * 0.1, 0.02}, {-0.15 + i * 0.075, 0.42}), 0.015, hx("#2a6fb0", 0.5f));
    const D top = d0.at(Affine::scale(1 + 0.025 * wave(t, 1.5)));
    top.fill(ell(0, -0.02, 0.34, 0.12), hx("#ffc2dc"));
    top.fill(ell(0, -0.14, 0.27, 0.11), hx("#ffd6e8"));
    top.fill(ell(0, -0.25, 0.17, 0.09), hx("#ffe6f1"));
    top.fill(circ(0, -0.38, 0.06), hx("#e0243e"));
}

// ---------------------------------------------------------------- animals

void animalFace(const D& d0, int kind) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.04 + 0.01 * wave(t, 2.0)) * Affine::rotate(0.06 * wave(t, 3.0)));
    const double b = blink(t, 3.0, 1.2);
    auto ears = [&](Color c, Color inner, bool pointed, double spread, double y, double s) {
        for (double sx : {-1.0, 1.0}) {
            const double wig = sx > 0 ? 0.08 * std::max(0.0, wave(t, 2.6)) : 0;
            const D e = d.at(Affine::translate(sx * spread, y) * Affine::rotate(sx * 0.35 + wig) * Affine::scale(s));
            if (pointed) {
                e.fill(poly({{-0.13, 0.08}, {0.13, 0.08}, {0, -0.2}}), c);
                e.fill(poly({{-0.07, 0.05}, {0.07, 0.05}, {0, -0.12}}), inner);
            } else {
                e.fill(circ(0, 0, 0.12), c);
                e.fill(circ(0, 0, 0.07), inner);
            }
        }
    };
    auto faceEyes = [&](double y, double s) {
        for (double sx : {-1.0, 1.0}) {
            const D e = d.at(Affine::translate(sx * 0.14, y) * Affine::scale(s, s * b));
            e.fill(circ(0, 0, 0.05), hx("#1c1c22"));
            e.fill(circ(0.015, -0.017, 0.016), hx("#ffffff"));
        }
    };
    switch (kind) {
        case 0: {  // cat
            ears(hx("#ffb35a"), hx("#ffd7e0"), true, 0.25, -0.24, 1.2);
            d.rad(ell(0, 0.04, 0.4, 0.34), {-0.1, -0.1}, 0.5, hx("#ffc77a"), hx("#f29a3a"));
            for (double x : {-0.06, 0.06}) d.stroke(line({x, -0.28}, {x * 1.3, -0.18}), 0.02, hx("#d07a20"));
            faceEyes(0.0, 1);
            d.fill(poly({{-0.03, 0.08}, {0.03, 0.08}, {0, 0.11}}), hx("#ff6f8b"));
            d.stroke(arc(-0.035, 0.12, 0.035, 0.035, 0, kPi), 0.015, hx("#5a3010"));
            d.stroke(arc(0.035, 0.12, 0.035, 0.035, 0, kPi), 0.015, hx("#5a3010"));
            for (double sx : {-1.0, 1.0})
                for (int i = 0; i < 2; ++i) d.stroke(line({sx * 0.16, 0.1 + i * 0.04}, {sx * 0.4, 0.07 + i * 0.07}), 0.01, hx("#5a3010", 0.7f));
            break;
        }
        case 1: {  // dog
            for (double sx : {-1.0, 1.0}) {
                const D e = d.at(Affine::translate(sx * 0.32, -0.12) * Affine::rotate(sx * (0.25 + 0.08 * wave(t, 1.4))));
                e.fill(ell(0, 0.08, 0.1, 0.2), hx("#8a5a2b"));
            }
            d.rad(ell(0, 0.04, 0.34, 0.34), {-0.1, -0.1}, 0.5, hx("#f3d2a2"), hx("#d9a66a"));
            d.fill(ell(0, 0.15, 0.17, 0.13), hx("#fff3e0"));
            faceEyes(-0.04, 1);
            d.fill(ell(0, 0.09, 0.06, 0.04), hx("#2a1a10"));
            const double k = 0.6 + 0.4 * std::abs(wave(t, 0.5));
            d.fill(rrect(-0.04, 0.17, 0.08, 0.1 * k, 0.04), hx("#ff6f8b"));
            break;
        }
        case 2: {  // bear
            ears(hx("#9a6a3a"), hx("#d9a66a"), false, 0.26, -0.24, 1);
            d.rad(circ(0, 0.04, 0.37), {-0.1, -0.1}, 0.5, hx("#b07a44"), hx("#80542a"));
            d.fill(ell(0, 0.14, 0.14, 0.11), hx("#e8c49a"));
            faceEyes(-0.03, 1);
            d.fill(ell(0, 0.09, 0.05, 0.035), hx("#2a1a10"));
            d.stroke(arc(0, 0.12, 0.05, 0.05, kPi * 0.2, kPi * 0.8), 0.015, hx("#2a1a10"));
            break;
        }
        case 3: {  // bunny
            for (double sx : {-1.0, 1.0}) {
                const double flop = sx > 0 ? 0.15 * std::max(0.0, wave(t, 2.2)) : 0;
                const D e = d.at(Affine::translate(sx * 0.12, -0.25) * Affine::rotate(sx * 0.12 + flop));
                e.fill(ell(0, -0.11, 0.075, 0.15), hx("#ffffff"));
                e.fill(ell(0, -0.1, 0.035, 0.1), hx("#ffc0d0"));
            }
            d.rad(ell(0, 0.08, 0.34, 0.3), {-0.1, -0.1}, 0.5, hx("#ffffff"), hx("#e6e6ee"));
            faceEyes(0.04, 1);
            d.fill(ell(0, 0.13, 0.03, 0.022), hx("#ff6f8b"));
            cheeks(d.at(Affine::translate(0, 0.08) * Affine::scale(0.8)), 0.5f);
            break;
        }
        case 4: {  // panda
            ears(hx("#1c1c22"), hx("#3a3a44"), false, 0.27, -0.25, 1);
            d.rad(circ(0, 0.04, 0.37), {-0.1, -0.1}, 0.5, hx("#ffffff"), hx("#e3e6ec"));
            for (double sx : {-1.0, 1.0}) d.at(Affine::translate(sx * 0.14, 0.0) * Affine::rotate(sx * -0.5)).fill(ell(0, 0, 0.08, 0.11), hx("#1c1c22"));
            for (double sx : {-1.0, 1.0}) {
                const D e = d.at(Affine::translate(sx * 0.14, 0.0) * Affine::scale(1, b));
                e.fill(circ(0, 0, 0.035), hx("#ffffff"));
                e.fill(circ(0, 0, 0.02), hx("#1c1c22"));
            }
            d.fill(ell(0, 0.13, 0.05, 0.035), hx("#1c1c22"));
            d.stroke(arc(0, 0.16, 0.05, 0.04, kPi * 0.2, kPi * 0.8), 0.015, hx("#1c1c22"));
            break;
        }
        case 5: {  // chick
            const double hop = std::max(0.0, wave(t, 0.9));
            const D c = d.at(Affine::translate(0, -0.04 * hop));
            c.rad(circ(0, 0.06, 0.36), {-0.1, -0.1}, 0.5, hx("#fff27a"), hx("#ffc21a"));
            for (double sx : {-1.0, 1.0})
                c.at(Affine::translate(sx * 0.33, 0.1) * Affine::rotate(sx * (0.3 + 0.3 * wave(t, 0.3)))).fill(ell(0, 0, 0.06, 0.12), hx("#ffcf2a"));
            faceEyes(-0.02, 0.9);
            c.fill(poly({{-0.06, 0.06}, {0.06, 0.06}, {0, 0.13}}), hx("#ff8a1f"));
            c.fill(poly({{-0.03, -0.28}, {0.0, -0.4}, {0.03, -0.29}, {0.07, -0.37}, {0.06, -0.27}}), hx("#ffc21a"));
            cheeks(c.at(Affine::scale(0.8) * Affine::translate(0, 0.05)), 0.5f);
            break;
        }
        case 6: {  // pig
            ears(hx("#ff9fb8"), hx("#ff7f9f"), true, 0.24, -0.24, 1);
            d.rad(circ(0, 0.05, 0.37), {-0.1, -0.1}, 0.5, hx("#ffc6d4"), hx("#ff97b0"));
            faceEyes(-0.04, 1);
            d.fill(ell(0, 0.13, 0.12, 0.08), hx("#ff8fab"));
            for (double sx : {-1.0, 1.0}) d.fill(ell(sx * 0.04, 0.13, 0.022, 0.032), hx("#c2506e"));
            break;
        }
        case 7: {  // fox
            ears(hx("#ff7a2a"), hx("#3a2214"), true, 0.24, -0.25, 1.25);
            Path f;
            f.moveTo(-0.38, -0.12).cubicTo(-0.3, 0.15, -0.12, 0.32, 0, 0.36).cubicTo(0.12, 0.32, 0.3, 0.15, 0.38, -0.12);
            f.cubicTo(0.2, -0.28, -0.2, -0.28, -0.38, -0.12).close();
            d.lin(f, {0, -0.25}, {0, 0.36}, hx("#ff9a4a"), hx("#e8601a"));
            Path w;
            w.moveTo(-0.34, -0.06).cubicTo(-0.2, 0.0, -0.08, 0.1, 0, 0.36).cubicTo(0.08, 0.1, 0.2, 0.0, 0.34, -0.06);
            w.cubicTo(0.2, 0.25, -0.2, 0.25, -0.34, -0.06).close();
            d.fill(w, hx("#fff3e6"));
            faceEyes(-0.04, 0.9);
            d.fill(circ(0, 0.3, 0.04), hx("#2a1a10"));
            break;
        }
        case 8: {  // frog
            for (double sx : {-1.0, 1.0}) d.fill(circ(sx * 0.17, -0.2, 0.13), hx("#5ad15a"));
            d.rad(ell(0, 0.06, 0.4, 0.3), {-0.1, -0.1}, 0.5, hx("#7ee07a"), hx("#3aa83a"));
            for (double sx : {-1.0, 1.0}) {
                const D e = d.at(Affine::translate(sx * 0.17, -0.2) * Affine::scale(1, b));
                e.fill(circ(0, 0, 0.08), hx("#ffffff"));
                e.fill(circ(0, 0.01, 0.045), hx("#1c1c22"));
            }
            d.stroke(arc(0, 0.04, 0.24, 0.16, kPi * 0.15, kPi * 0.85), 0.025, hx("#1f6e28"));
            cheeks(d.at(Affine::translate(0, 0.02)), 0.5f);
            break;
        }
        default: {  // owl
            d.rad(ell(0, 0.06, 0.36, 0.38), {-0.1, -0.1}, 0.5, hx("#a9825a"), hx("#6e4a2a"));
            for (double sx : {-1.0, 1.0}) d.fill(poly({{sx * 0.1, -0.28}, {sx * 0.32, -0.42}, {sx * 0.3, -0.18}}), hx("#6e4a2a"));
            d.fill(ell(0, 0.18, 0.22, 0.2), hx("#e9d2b0"));
            for (double sx : {-1.0, 1.0}) {
                d.fill(circ(sx * 0.14, -0.06, 0.12), hx("#ffffff"));
                const D e = d.at(Affine::translate(sx * 0.14, -0.06) * Affine::scale(1, b));
                e.fill(circ(0, 0, 0.065), hx("#ffb000"));
                e.fill(circ(0, 0, 0.04), hx("#1c1c22"));
            }
            d.fill(poly({{-0.04, 0.04}, {0.04, 0.04}, {0, 0.12}}), hx("#ff9a1f"));
        }
    }
}

void butterfly(const D& d0, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.03 * wave(t, 3.0), 0.04 * wave(t, 1.5)) * Affine::rotate(0.1 * wave(t, 3.0, 0.25)));
    const double flap = 0.35 + 0.65 * std::abs(std::cos(kPi * t / 0.5));
    for (double s : {-1.0, 1.0}) {
        const D w = d.at(Affine::scale(s * flap, 1));
        Path up, lo;
        up.moveTo(0, -0.02).cubicTo(0.1, -0.4, 0.46, -0.44, 0.44, -0.18).cubicTo(0.42, -0.02, 0.2, 0.02, 0, 0.0).close();
        lo.moveTo(0, 0.02).cubicTo(0.3, 0.02, 0.38, 0.3, 0.24, 0.36).cubicTo(0.12, 0.4, 0.04, 0.2, 0, 0.04).close();
        w.lin(up, {0, 0}, {0.45, -0.3}, hx(a), hx(b));
        w.lin(lo, {0, 0}, {0.3, 0.35}, hx(b), hx(a));
        w.fill(circ(0.28, -0.22, 0.05), hx("#ffffff", 0.7f));
    }
    d.fill(ell(0, 0.02, 0.035, 0.22), hx("#2a1a10"));
    d.stroke(arc(-0.08, -0.3, 0.08, 0.1, -kPi * 0.1, kPi * 0.5), 0.012, hx("#2a1a10"));
    d.stroke(arc(0.08, -0.3, 0.08, 0.1, kPi * 0.5, kPi * 1.1), 0.012, hx("#2a1a10"));
}

void bee(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.08 * wave(t, 2.4), 0.06 * wave(t, 1.2)) * Affine::rotate(0.1 * wave(t, 2.4, 0.25)));
    const double flap = 0.6 + 0.4 * std::abs(std::cos(kPi * t / 0.12));
    for (double s : {-1.0, 1.0})
        d.at(Affine::translate(s * 0.08, -0.18) * Affine::rotate(s * 0.4) * Affine::scale(1, flap)).fill(ell(0, -0.1, 0.11, 0.17), hx("#dff4ff", 0.85f));
    d.rad(ell(0, 0.05, 0.32, 0.24), {-0.1, -0.05}, 0.4, hx("#ffe14d"), hx("#ffb000"));
    for (double x : {-0.1, 0.06}) d.fill(rrect(x, -0.17, 0.07, 0.44, 0.03), hx("#2a1a10"));
    d.fill(poly({{0.3, 0.02}, {0.42, 0.05}, {0.3, 0.09}}), hx("#2a1a10"));
    d.fill(circ(-0.22, -0.0, 0.035), hx("#1c1c22"));
    d.stroke(arc(-0.2, 0.08, 0.05, 0.04, kPi * 0.2, kPi * 0.8), 0.015, hx("#1c1c22"));
}

void fish(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.04 * wave(t, 2.0), 0));
    const double tail = 0.3 * wave(t, 0.6);
    d.at(Affine::translate(0.26, 0) * Affine::rotate(tail)).fill(poly({{0, 0}, {0.2, -0.18}, {0.16, 0}, {0.2, 0.18}}), hx("#ff7a2a"));
    d.rad(ell(-0.04, 0, 0.32, 0.22), {-0.12, -0.08}, 0.4, hx("#ffb35a"), hx("#ff6a1a"));
    for (double x : {-0.02, 0.1}) d.stroke(arc(x, 0, 0.06, 0.18, -kPi / 2, kPi / 2), 0.03, hx("#ffffff", 0.8f));
    d.fill(circ(-0.2, -0.04, 0.045), hx("#1c1c22"));
    d.fill(circ(-0.19, -0.05, 0.015), hx("#ffffff"));
    for (int i = 0; i < 3; ++i) {
        const double ph = frac(t / 2.0 + i / 3.0);
        d0.stroke(circ(-0.4 + 0.02 * i, -0.1 - 0.35 * ph, 0.025 + 0.015 * i), 0.012, hx("#7fd6ff", static_cast<float>(std::sin(kPi * ph))));
    }
}

// ---------------------------------------------------------------- catalog

struct Entry {
    StickerInfo info;
    std::function<void(const D&)> draw;
};

const std::vector<Entry>& entries() {
    static const std::vector<Entry> kAll = [] {
        std::vector<Entry> e;
        auto add = [&](const char* id, const char* name, const char* cat, std::function<void(const D&)> f) {
            e.push_back({{id, name, cat}, std::move(f)});
        };
        const char* F = "Mặt cười";
        const std::pair<const char*, const char*> faces[] = {
            {"smile", "Cười"},     {"grin", "Cười tươi"},    {"laugh", "Cười lớn"},   {"joy", "Cười ra nước mắt"},
            {"wink", "Nháy mắt"},  {"love", "Mắt tim"},      {"cool", "Ngầu"},        {"wow", "Ngạc nhiên"},
            {"sad", "Buồn"},       {"cry", "Khóc"},          {"angry", "Giận"},       {"sleepy", "Buồn ngủ"},
            {"kiss", "Hôn gió"},   {"tongue", "Lè lưỡi"},    {"starry", "Mắt sao"},   {"blush", "Ngượng"},
            {"party", "Tiệc tùng"}, {"meh", "Bình thản"},    {"think", "Suy nghĩ"},   {"shy", "E thẹn"},
        };
        for (int i = 0; i < 20; ++i)
            add((std::string("face_") + faces[i].first).c_str(), faces[i].second, F,
                [i](const D& d) { face(d, static_cast<Face>(i)); });
        addSocialStickers(add);
        const char* H = "Trái tim";
        add("heart_red", "Tim đỏ", H, [](const D& d) { heart(d, hx("#ff5a6e"), hx("#d6002a"), 0); });
        add("heart_pink", "Tim hồng", H, [](const D& d) { heart(d, hx("#ffb0d0"), hx("#ff4f9a"), 0); });
        add("heart_purple", "Tim tím", H, [](const D& d) { heart(d, hx("#c9a2ff"), hx("#7c3aed"), 0); });
        add("heart_gold", "Tim vàng", H, [](const D& d) { heart(d, hx("#fff07a"), hx("#f0a10f"), 0); });
        add("heart_blue", "Tim xanh", H, [](const D& d) { heart(d, hx("#9fe4ff"), hx("#2e7bff"), 0); });
        add("heart_outline", "Tim viền", H, [](const D& d) { heart(d, hx("#ff3b5c"), hx("#ff3b5c"), 1); });
        add("heart_double", "Hai trái tim", H, [](const D& d) { heart(d, hx("#ff5a6e"), hx("#ff9fc0"), 2); });
        add("heart_broken", "Tim vỡ", H, [](const D& d) { heart(d, hx("#ff5a6e"), hx("#b0001f"), 3); });
        add("heart_sparkle", "Tim lấp lánh", H, [](const D& d) { heart(d, hx("#ff8ab8"), hx("#e0006a"), 4); });
        add("heart_arrow", "Tim mũi tên", H, [](const D& d) { heart(d, hx("#ff5a6e"), hx("#d6002a"), 5); });
        add("hearts_rising", "Tim bay", H, [](const D& d) { heart(d, hx("#ff8ab8"), hx("#ff2e55"), 6); });
        const char* L = "Hoa lá";
        add("flower_mai", "Hoa mai", L, hoaMai);
        add("flower_dao", "Hoa đào", L, hoaDao);
        add("flower_sakura", "Anh đào", L, [](const D& d) { flower(d, {5, 0.4, 0.2, 0.0, "#fff0f6", "#ffb3d1", "#ffd1e3", "#ff7fae", 0.07, 0, true}); });
        add("flower_daisy", "Cúc trắng", L, [](const D& d) { flower(d, {16, 0.44, 0.07, 0.3, "#ffffff", "#eef0ff", "#ffd54a", "#f0a10f", 0.12}); });
        add("flower_sunflower", "Hướng dương", L, sunflower);
        add("flower_rose", "Hoa hồng", L, rose);
        add("flower_tulip", "Tulip đỏ", L, [](const D& d) { tulip(d, "#ff6b6b", "#c8102e"); });
        add("flower_tulip_pink", "Tulip hồng", L, [](const D& d) { tulip(d, "#ffb0d0", "#ff4f9a"); });
        add("flower_hibiscus", "Dâm bụt", L, hibiscus);
        add("flower_lotus", "Hoa sen", L, lotus);
        add("flower_purple", "Hoa tím", L, [](const D& d) { flower(d, {8, 0.44, 0.13, 0.6, "#e7d4ff", "#8f5cff", "#ffe27a", "#f0a10f", 0.09, 1}); });
        add("flower_blue", "Hoa xanh", L, [](const D& d) { flower(d, {6, 0.44, 0.17, 0.2, "#e0f2ff", "#3f9bff", "#ffffff", "#bfe3ff", 0.08, 1}); });
        add("flower_orange", "Hoa cam", L, [](const D& d) { flower(d, {10, 0.44, 0.1, 0.8, "#fff1c9", "#ff8a1f", "#a85a10", "#5a2e06", 0.1, 1}); });
        add("flower_bunch", "Bó cúc", L, daisyBunch);
        add("clover", "Cỏ bốn lá", L, clover);
        add("leaf_green", "Lá xanh", L, [](const D& d) { leaf(d, "#8be06a", "#2f9a3a"); });
        add("leaf_autumn", "Lá thu", L, [](const D& d) { leaf(d, "#ffc23a", "#e0501a"); });
        const char* S = "Sao lấp lánh";
        add("star_gold", "Sao vàng", S, [](const D& d) { starSticker(d, "#fff27a", "#ffae1a", false); });
        add("star_pink", "Sao hồng", S, [](const D& d) { starSticker(d, "#ffd0e6", "#ff4f9a", false); });
        add("star_blue", "Sao xanh", S, [](const D& d) { starSticker(d, "#c9f1ff", "#2e9bff", false); });
        add("star_outline", "Sao viền", S, [](const D& d) { starSticker(d, "#ffc21a", "#ffc21a", true); });
        add("sparkle", "Lấp lánh", S, [](const D& d) { sparkle(d, 1, "#ffd54a"); });
        add("sparkles", "Chùm lấp lánh", S, [](const D& d) { sparkle(d, 5, "#ffd54a"); });
        add("sparkles_white", "Lấp lánh trắng", S, [](const D& d) { sparkle(d, 5, "#bfe6ff"); });
        add("shooting_star", "Sao băng", S, shootingStar);
        add("glitter", "Kim tuyến", S, glitter);
        add("burst_yellow", "Nổ vàng", S, [](const D& d) { burst(d, "#fff27a", "#ffae1a", 16); });
        add("burst_red", "Nổ đỏ", S, [](const D& d) { burst(d, "#ff8a8a", "#e01b3b", 12); });
        const char* W = "Thời tiết";
        add("sun", "Mặt trời", W, [](const D& d) { sun(d, false); });
        add("sun_face", "Ông mặt trời", W, [](const D& d) { sun(d, true); });
        add("cloud", "Mây", W, [](const D& d) { cloud(d, 0); });
        add("cloud_rain", "Mưa", W, [](const D& d) { cloud(d, 1); });
        add("cloud_snow", "Tuyết rơi", W, [](const D& d) { cloud(d, 2); });
        add("cloud_storm", "Giông", W, [](const D& d) { cloud(d, 3); });
        add("snowflake", "Bông tuyết", W, snowflake);
        add("bolt", "Tia sét", W, bolt);
        add("rainbow", "Cầu vồng", W, rainbow);
        add("moon", "Trăng sao", W, moon);
        add("fire", "Ngọn lửa", W, fire);
        add("drop", "Giọt nước", W, drop);
        const char* P = "Lễ hội";
        add("balloon_red", "Bóng bay đỏ", P, [](const D& d) { balloon(d, "#ff7a8a", "#d6002a"); });
        add("balloon_blue", "Bóng bay xanh", P, [](const D& d) { balloon(d, "#8fd3ff", "#1f7fe0"); });
        add("balloon_gold", "Bóng bay vàng", P, [](const D& d) { balloon(d, "#fff07a", "#f0a10f"); });
        add("balloons", "Chùm bóng bay", P, balloons);
        add("gift", "Hộp quà", P, gift);
        add("popper", "Pháo giấy", P, popper);
        add("confetti", "Hoa giấy", P, confetti);
        add("cake", "Bánh sinh nhật", P, cake);
        add("firework", "Pháo hoa", P, [](const D& d) { firework(d, "#ff4f8b"); });
        add("firework_blue", "Pháo hoa xanh", P, [](const D& d) { firework(d, "#3fd0ff"); });
        add("crown", "Vương miện", P, crown);
        add("trophy", "Cúp", P, trophy);
        add("medal", "Huy chương", P, medal);
        add("bow_pink", "Nơ hồng", P, [](const D& d) { bow(d, "#ffb0d0", "#ff4f9a"); });
        add("bow_red", "Nơ đỏ", P, [](const D& d) { bow(d, "#ff7a7a", "#c8102e"); });
        add("lantern", "Đèn lồng", P, lantern);
        add("lixi", "Lì xì", P, envelope);
        add("xmas_tree", "Cây thông", P, tree);
        const char* A = "Mũi tên & dấu";
        add("arrow_right", "Mũi tên phải", A, [](const D& d) { arrow(d, 0, "#ffe14d", "#ff9a1f"); });
        add("arrow_left", "Mũi tên trái", A, [](const D& d) { arrow(d, kPi, "#ffe14d", "#ff9a1f"); });
        add("arrow_up", "Mũi tên lên", A, [](const D& d) { arrow(d, -kPi / 2, "#7fe08a", "#1fae4f"); });
        add("arrow_down", "Mũi tên xuống", A, [](const D& d) { arrow(d, kPi / 2, "#ff8a8a", "#e01b3b"); });
        add("arrow_curved", "Mũi tên cong", A, curvedArrow);
        add("check", "Dấu đúng", A, [](const D& d) { check(d, true); });
        add("cross", "Dấu sai", A, [](const D& d) { check(d, false); });
        add("exclamation", "Chấm than", A, [](const D& d) { mark(d, false); });
        add("question", "Chấm hỏi", A, [](const D& d) { mark(d, true); });
        add("bubble_speech", "Bong bóng nói", A, [](const D& d) { bubble(d, 0, "#ffffff"); });
        add("bubble_thought", "Bong bóng nghĩ", A, [](const D& d) { bubble(d, 1, "#ffffff"); });
        add("bubble_shout", "Bong bóng hét", A, [](const D& d) { bubble(d, 2, "#ffe14d"); });
        add("pin", "Ghim vị trí", A, pin);
        add("bell", "Chuông", A, bell);
        add("music", "Nốt nhạc", A, note);
        add("play", "Nút phát", A, playButton);
        add("circle_mark", "Khoanh tròn", A, circleMark);
        add("underline", "Gạch chân", A, underline);
        add("plus", "Dấu cộng", A, plusSign);
        add("search", "Kính lúp", A, magnifier);
        add("ping", "Sóng tỏa", A, [](const D& d) { ring(d, "#ff4f6d"); });
        const char* E = "Đồ ăn";
        add("apple_red", "Táo đỏ", E, [](const D& d) { apple(d, "#ff6b6b", "#c8102e"); });
        add("apple_green", "Táo xanh", E, [](const D& d) { apple(d, "#b5f07a", "#4caf3a"); });
        add("cherry", "Anh đào", E, cherry);
        add("strawberry", "Dâu tây", E, strawberry);
        add("watermelon", "Dưa hấu", E, watermelon);
        add("lemon", "Chanh", E, [](const D& d) { citrus(d, "#fff27a", "#e8c20a"); });
        add("orange", "Cam", E, [](const D& d) { citrus(d, "#ffc23a", "#ff8a1f"); });
        add("coffee", "Cà phê", E, coffee);
        add("boba", "Trà sữa", E, boba);
        add("ice_cream", "Kem", E, iceCream);
        add("donut", "Bánh vòng", E, donut);
        add("pizza", "Pizza", E, pizza);
        add("cupcake", "Bánh nướng", E, cupcake);
        const char* N = "Động vật";
        const std::pair<const char*, const char*> animals[] = {
            {"cat", "Mèo"}, {"dog", "Chó"},   {"bear", "Gấu"}, {"bunny", "Thỏ"}, {"panda", "Gấu trúc"},
            {"chick", "Gà con"}, {"pig", "Heo"}, {"fox", "Cáo"}, {"frog", "Ếch"}, {"owl", "Cú"},
        };
        for (int i = 0; i < 10; ++i)
            add(animals[i].first, animals[i].second, N, [i](const D& d) { animalFace(d, i); });
        add("butterfly", "Bướm", N, [](const D& d) { butterfly(d, "#ffd0f0", "#c04dff"); });
        add("butterfly_blue", "Bướm xanh", N, [](const D& d) { butterfly(d, "#c9f1ff", "#2e7bff"); });
        add("bee", "Ong", N, bee);
        add("fish", "Cá", N, fish);
        addMoreStickers(add);
        return e;
    }();
    return kAll;
}

const Entry* findEntry(const std::string& id) {
    for (const auto& e : entries())
        if (e.info.id == id) return &e;
    return nullptr;
}

// ---------------------------------------------------------------- rendering

Affine inverse(const Affine& m) {
    const double det = m.a * m.d - m.b * m.c;
    if (std::abs(det) < 1e-12) return {};
    const double ia = m.d / det, ib = -m.b / det, ic = -m.c / det, id = m.a / det;
    return {ia, ib, ic, id, -(ia * m.e + ic * m.f), -(ib * m.e + id * m.f)};
}

// Straight-alpha source-over of an RGBA8 `src` placed at (ox, oy) onto `dst`, alpha scaled by `k`.
void blendOnto(VideoFrame& dst, const VideoFrame& src, int ox, int oy, float k) {
    const int x0 = std::max(0, ox), y0 = std::max(0, oy);
    const int x1 = std::min(dst.width, ox + src.width), y1 = std::min(dst.height, oy + src.height);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const std::uint8_t* s = &src.rgba[(static_cast<std::size_t>(y - oy) * src.width + (x - ox)) * 4];
            const float sa = s[3] / 255.f * k;
            if (sa <= 0) continue;
            const std::size_t i = (static_cast<std::size_t>(y) * dst.width + x) * 4;
            float d[4];
            if (dst.deep()) {
                for (int c = 0; c < 4; ++c) d[c] = halfToFloat(dst.half[i + c]);
            } else {
                for (int c = 0; c < 4; ++c) d[c] = dst.rgba[i + c] / 255.f;
            }
            const float da = d[3] * (1 - sa), oa = sa + da;
            float o[4];
            for (int c = 0; c < 3; ++c) o[c] = oa > 0 ? (s[c] / 255.f * sa + d[c] * da) / oa : 0;
            o[3] = oa;
            if (dst.deep()) {
                for (int c = 0; c < 4; ++c) dst.half[i + c] = floatToHalf(o[c]);
            } else {
                for (int c = 0; c < 4; ++c)
                    dst.rgba[i + c] = static_cast<std::uint8_t>(std::lround(std::clamp(o[c], 0.f, 1.f) * 255));
            }
        }
}

// Blurs an RGBA8 frame (premultiplied while blurring, so edges do not darken).
void blurFrame(VideoFrame& f, double radius) {
    if (radius < 1 || f.empty()) return;
    const std::size_t n = static_cast<std::size_t>(f.width) * f.height;
    vec::Mask ch[4];
    for (auto& c : ch) {
        c.w = f.width;
        c.h = f.height;
        c.v.resize(n);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const float a = f.rgba[i * 4 + 3] / 255.f;
        for (int c = 0; c < 3; ++c) ch[c].v[i] = f.rgba[i * 4 + c] / 255.f * a;
        ch[3].v[i] = a;
    }
    for (auto& c : ch) c.blur(radius);
    for (std::size_t i = 0; i < n; ++i) {
        const float a = ch[3].v[i];
        for (int c = 0; c < 3; ++c)
            f.rgba[i * 4 + c] = static_cast<std::uint8_t>(std::lround(std::clamp(a > 0 ? ch[c].v[i] / a : 0.f, 0.f, 1.f) * 255));
        f.rgba[i * 4 + 3] = static_cast<std::uint8_t>(std::lround(std::clamp(a, 0.f, 1.f) * 255));
    }
}

}  // namespace

const std::vector<StickerInfo>& stickerCatalog() {
    static const std::vector<StickerInfo> kInfo = [] {
        std::vector<StickerInfo> v;
        for (const auto& e : entries()) v.push_back(e.info);
        return v;
    }();
    return kInfo;
}

std::vector<std::string> stickerCategories() {
    std::vector<std::string> out;
    for (const auto& s : stickerCatalog())
        if (std::find(out.begin(), out.end(), s.category) == out.end()) out.push_back(s.category);
    return out;
}

const StickerInfo* findSticker(const std::string& id) {
    const Entry* e = findEntry(id);
    return e ? &e->info : nullptr;
}

Json defaultStickerContent(const std::string& id) {
    return Json{{"kind", "sticker"}, {"sticker", id},   {"size", 320.0},
                {"rotation", 0.0},   {"flip", false},   {"alive", true},
                {"in", {{"type", "pop"}, {"duration", 0.5}}},
                {"out", {{"type", "fade"}, {"duration", 0.4}}},
                {"loop", {{"type", ""}, {"speed", 1.0}}}};
}

bool stickerStill(const Json& c, double local, double length) {
    return !c.value("alive", true) && motion::timing(c, local, length).still();
}

std::array<double, 4> stickerBounds(const Json& c) {
    const double s = std::max(1.0, c.value("size", 320.0)) / 2;
    const double r = c.value("rotation", 0.0) * kPi / 180;
    const double e = s * (std::abs(std::cos(r)) + std::abs(std::sin(r)));
    return {-e, -e, e, e};
}

void drawSticker(const Json& c, double local, double length, const Affine& place, VideoFrame& out) {
    const Entry* e = findEntry(c.value("sticker", std::string{}));
    if (!e || out.empty()) return;
    const double size = std::max(1.0, c.value("size", 320.0));
    const motion::Timing T = motion::timing(c, local, length);
    const motion::Pose P = motion::pose(T, 0, 1, static_cast<unsigned>(std::hash<std::string>{}(e->info.id)));
    if (P.opacity <= 0.002) return;
    Affine m = place * Affine::rotate(c.value("rotation", 0.0) * kPi / 180) * P.transform({0, 0}, size) * Affine::scale(size);
    if (c.value("flip", false)) m = m * Affine::scale(-1, 1);
    // Draw on a small scratch frame around the sticker, then fade/blur/wipe it as one picture.
    const double blurPx = P.blur * size * place.scaleFactor();
    const int pad = static_cast<int>(std::ceil(blurPx * 1.6)) + 2;
    double bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
    for (const Pt p : {Pt{-0.56, -0.56}, Pt{0.56, -0.56}, Pt{-0.56, 0.56}, Pt{0.56, 0.56}}) {
        const Pt q = m.apply(p);
        bx0 = std::min(bx0, q.x);
        by0 = std::min(by0, q.y);
        bx1 = std::max(bx1, q.x);
        by1 = std::max(by1, q.y);
    }
    const int ox = std::max(0, static_cast<int>(std::floor(bx0)) - pad), oy = std::max(0, static_cast<int>(std::floor(by0)) - pad);
    const int ex = std::min(out.width, static_cast<int>(std::ceil(bx1)) + pad);
    const int ey = std::min(out.height, static_cast<int>(std::ceil(by1)) + pad);
    if (ex <= ox || ey <= oy) return;
    VideoFrame scratch;
    scratch.allocate(ex - ox, ey - oy);
    {
        vec::Canvas cv(scratch);
        const double t = c.value("alive", true) ? local : 0.0;
        e->draw(D{cv, Affine::translate(-ox, -oy) * m, t});
    }
    blurFrame(scratch, blurPx);
    if (P.wipe != 0 && P.reveal < 1) {
        const Affine inv = inverse(Affine::translate(-ox, -oy) * m);
        constexpr double soft = 0.25;
        for (int y = 0; y < scratch.height; ++y)
            for (int x = 0; x < scratch.width; ++x) {
                const Pt q = inv.apply({x + 0.5, y + 0.5});
                const double u = P.wipe == 1 ? q.x + 0.5 : 0.5 - q.y;
                double k = std::clamp((P.reveal * (1 + soft) - u) / soft, 0.0, 1.0);
                k = k * k * (3 - 2 * k);
                auto& a = scratch.rgba[(static_cast<std::size_t>(y) * scratch.width + x) * 4 + 3];
                a = static_cast<std::uint8_t>(std::lround(a * k));
            }
    }
    blendOnto(out, scratch, ox, oy, static_cast<float>(P.opacity));
}

void drawStickerIcon(const std::string& id, double seconds, VideoFrame& out) {
    const Entry* e = findEntry(id);
    if (!e || out.empty()) return;
    const double s = std::min(out.width, out.height) * 0.92;
    vec::Canvas cv(out);
    e->draw(D{cv, Affine::translate(out.width / 2.0, out.height / 2.0) * Affine::scale(s), seconds});
}

}  // namespace vstudio::stickers
