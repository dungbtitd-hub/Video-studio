// More built-in stickers, in the groups video apps commonly offer: social buttons (generic designs,
// no brand logos or names), sale labels, comic words, hand-drawn marks, video/channel signs,
// sports & games, Tết and seasonal, travel & daily life. All vectors, drawn in the unit box.
// Motion rules: everything is smooth (eased or sine); one-shot sequences (a click) play once and
// hold their last state, so a sticker never jumps back to its start.
#include <cmath>
#include <string>

#include "sticker_kit.hpp"
#include "vstudio/fonts.hpp"

namespace vstudio::stickers::kit {

// ---------------------------------------------------------------- text inside stickers

Path textPath(const std::string& utf8, double cx, double cy, double size, double maxW, const char* font) {
    const auto f = text::Font::get(font);
    const auto fallback = text::Font::get(text::defaultFontId());
    struct G {
        const text::Font* font;
        int glyph;
        double x;
    };
    std::vector<G> gs;
    double pen = 0;
    int prev = 0;
    const text::Font* prevFont = nullptr;
    for (const char32_t ch : text::decodeUtf8(utf8)) {
        const text::Font* use = f.get();
        int g = f->glyph(ch);
        if (g == 0 && ch > U' ') {
            if (const int fg = fallback->glyph(ch)) {
                use = fallback.get();
                g = fg;
            }
        }
        if (prevFont == use) pen += use->kerning(prev, g);
        gs.push_back({use, g, pen});
        pen += use->advance(g);
        prev = g;
        prevFont = use;
    }
    const double k = pen > 0 ? std::min(size, maxW / pen) : size;
    // Centre on the cap height (about 0.7 em), so accents above capitals do not push the word down.
    const double x0 = cx - pen * k / 2, base = cy + 0.35 * k;
    Path out;
    for (const auto& g : gs) out.append(g.font->outline(g.glyph), Affine::translate(x0 + g.x * k, base) * Affine::scale(k));
    return out;
}

void label(const D& d, const std::string& utf8, double cx, double cy, double size, double maxW, Color fill,
           const char* font) {
    d.fill(textPath(utf8, cx, cy, size, maxW, font), fill);
}

namespace {

// ---------------------------------------------------------------- small helpers

double ease(double x) {  // smoothstep on 0..1, clamped
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
double between(double t, double a, double b) { return ease((t - a) / (b - a)); }
Pt lerp(Pt a, Pt b, double k) { return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k}; }

void shadow(const D& d, const Path& p, double dy = 0.025, float a = 0.28f) {
    d.at(Affine::translate(0, dy)).fill(p, hx("#000000", a));
}

// Pill button with a top-to-bottom gradient, a soft shadow and a shine on the top half.
void pill(const D& d, double x, double y, double w, double h, Color top, Color bottom) {
    const Path p = rrect(x, y, w, h, h / 2);
    shadow(d, p);
    d.lin(p, {0, y}, {0, y + h}, top, bottom);
    d.fill(rrect(x + h * 0.3, y + h * 0.1, w - h * 0.6, h * 0.36, h * 0.18), hx("#ffffff", 0.18f));
}

// Thumbs-up hand, about 1 x 1 (cuff on the left), centred at the origin.
Path thumbPath() {
    Path p;
    p.moveTo(-0.22, -0.02).lineTo(-0.05, -0.14);
    p.cubicTo(0.0, -0.22, 0.02, -0.36, 0.04, -0.45).cubicTo(0.06, -0.54, 0.21, -0.53, 0.21, -0.38);
    p.cubicTo(0.21, -0.28, 0.16, -0.19, 0.14, -0.11).lineTo(0.38, -0.11);
    p.cubicTo(0.49, -0.11, 0.51, 0.0, 0.44, 0.04).cubicTo(0.51, 0.08, 0.5, 0.17, 0.42, 0.19);
    p.cubicTo(0.48, 0.23, 0.46, 0.32, 0.39, 0.33).cubicTo(0.44, 0.37, 0.41, 0.46, 0.33, 0.46);
    p.lineTo(-0.22, 0.46).close();
    p.roundRect(-0.48, -0.06, 0.22, 0.54, 0.05);
    return p;
}
void thumb(const D& d, double cx, double cy, double s, Color c, bool down = false) {
    d.at(Affine::translate(cx, cy) * Affine::rotate(down ? kPi : 0) * Affine::scale(s)).fill(thumbPath(), c);
}

Path bellPath() {
    Path b;
    b.moveTo(-0.36, 0.24).cubicTo(-0.25, 0.12, -0.28, -0.34, 0, -0.36).cubicTo(0.28, -0.34, 0.25, 0.12, 0.36, 0.24);
    b.lineTo(0.36, 0.3).lineTo(-0.36, 0.3).close();
    b.circle(0, 0.38, 0.08);
    b.circle(0, -0.4, 0.05);
    return b;
}
void bellIcon(const D& d, double cx, double cy, double s, Color c, double swing = 0) {
    d.at(Affine::translate(cx, cy - 0.4 * s) * Affine::rotate(swing) * Affine::translate(0, 0.4 * s) * Affine::scale(s))
        .fill(bellPath(), c);
}

// Share: an arrow leaving a curve, pointing up-right.
void shareIcon(const D& d, double cx, double cy, double s, Color c) {
    const D e = d.at(Affine::translate(cx, cy) * Affine::scale(s));
    Path body;
    body.moveTo(-0.4, 0.38).cubicTo(-0.4, 0.0, -0.15, -0.12, 0.12, -0.12);
    e.stroke(body, 0.16, c);
    e.fill(poly({{0.08, -0.4}, {0.46, -0.1}, {0.08, 0.2}}), c);
}

// Speech bubble with a tail at the bottom left.
Path bubblePath(double w, double h) {
    Path p = rrect(-w / 2, -h / 2, w, h, std::min(w, h) * 0.35);
    p.append(poly({{-w * 0.28, h / 2 - 0.01}, {-w * 0.36, h / 2 + h * 0.36}, {-w * 0.06, h / 2 - 0.01}}));
    return p;
}
void commentIcon(const D& d, double cx, double cy, double s, Color c) {
    d.at(Affine::translate(cx, cy) * Affine::scale(s)).fill(bubblePath(0.9, 0.66), c);
}

void checkMark(const D& d, double cx, double cy, double s, Color c, double draw = 1) {
    Path p;
    const Pt a{cx - 0.4 * s, cy}, b{cx - 0.1 * s, cy + 0.3 * s}, e{cx + 0.45 * s, cy - 0.35 * s};
    p.moveTo(a.x, a.y);
    if (draw < 0.4) {
        const Pt q = lerp(a, b, draw / 0.4);
        p.lineTo(q.x, q.y);
    } else {
        const Pt q = lerp(b, e, (draw - 0.4) / 0.6);
        p.lineTo(b.x, b.y).lineTo(q.x, q.y);
    }
    d.stroke(p, 0.16 * s, c);
}

// A pointing hand (mouse cursor style), fingertip at the origin; `press` 0..1 squashes it a little.
void cursorHand(const D& d0, double x, double y, double s, double press, float alpha = 1) {
    if (alpha <= 0.01) return;
    const D d = d0.at(Affine::translate(x, y) * Affine::scale(s * (1 - 0.08 * press)) * Affine::rotate(-0.25));
    std::vector<Path> parts;
    parts.push_back(rrect(-0.07, 0.0, 0.14, 0.52, 0.07));                 // index finger
    parts.push_back(rrect(-0.09, 0.32, 0.46, 0.42, 0.13));                // palm
    parts.push_back(rrect(0.07, 0.28, 0.12, 0.22, 0.06));                 // middle
    parts.push_back(rrect(0.17, 0.31, 0.11, 0.2, 0.055));                 // ring
    parts.push_back(rrect(0.26, 0.35, 0.1, 0.18, 0.05));                  // little
    parts.push_back(rrect(-0.25, 0.38, 0.22, 0.12, 0.06));                // thumb
    for (const auto& p : parts) d.stroke(p, 0.07, hx("#1c1c22", alpha));
    for (const auto& p : parts) d.fill(p, hx("#ffffff", alpha));
}

// Expanding ring after a tap at (x, y), `k` 0..1 (invisible at both ends).
void ripple(const D& d, double x, double y, double r, double k, Color c) {
    if (k <= 0 || k >= 1) return;
    d.stroke(circ(x, y, r * (0.3 + 0.7 * k)), 0.03, c.withAlpha(static_cast<float>(std::sin(kPi * k))));
}

// The usual click story, played once: the hand glides in, taps at `tap`, then leaves.
struct Click {
    double tap = 1.1;
    [[nodiscard]] double cursorIn(double t) const { return between(t, 0.15, tap - 0.15); }
    [[nodiscard]] double press(double t) const { return std::exp(-std::pow((t - tap) / 0.07, 2)); }
    [[nodiscard]] double done(double t) const { return between(t, tap, tap + 0.25); }
    [[nodiscard]] float cursorAlpha(double t) const {
        return static_cast<float>(between(t, 0.0, 0.25) * (1 - between(t, tap + 0.7, tap + 1.1)));
    }
    void hand(const D& d, double t, Pt target) const {
        const Pt from{target.x + 0.2, target.y + 0.16};
        const Pt at = lerp(from, target, cursorIn(t));
        cursorHand(d, at.x, at.y, 0.38, press(t), cursorAlpha(t));
        ripple(d, target.x, target.y, 0.18, (t - tap) / 0.6, hx("#ffffff"));
    }
};

// ---------------------------------------------------------------- social (generic, no brand marks)

// A button that is clicked once: colours and text change smoothly, the icon reacts.
struct ButtonStyle {
    const char* text;
    const char* doneText;
    const char* top;
    const char* bottom;
    const char* doneTop;
    const char* doneBottom;
    int icon;  // 0 bell, 1 thumb, 2 share, 3 plus/check, 4 heart
};

void clickButton(const D& d0, const ButtonStyle& s) {
    const double t = d0.t;
    const Click c;
    const double k = c.done(t), press = c.press(t);
    const D d = d0.at(Affine::scale(1 - 0.06 * press));
    const double x = -0.47, y = -0.15, w = 0.94, h = 0.3;
    pill(d, x, y, w, h, mix(hx(s.top), hx(s.doneTop), static_cast<float>(k)),
         mix(hx(s.bottom), hx(s.doneBottom), static_cast<float>(k)));
    const double ix = 0.3, iy = 0.0;
    // The icon reacts after the tap: a bell rings, a thumb or heart pops, an arrow nudges.
    const double after = std::max(0.0, t - c.tap);
    const double react = after > 0 ? std::exp(-after * 2.2) : 0;
    const Color white = hx("#ffffff");
    d.fill(circ(ix, iy, 0.105), hx("#ffffff", 0.2f));
    switch (s.icon) {
        case 0: bellIcon(d, ix, iy, 0.15, white, 0.5 * react * std::sin(after * 18)); break;
        case 1: thumb(d, ix, iy - 0.01, 0.15 * (1 + 0.35 * react * std::sin(std::min(after * 9, kPi))), white); break;
        case 2: shareIcon(d.at(Affine::translate(0.04 * react * std::sin(after * 14), 0)), ix, iy, 0.14, white); break;
        case 3: {
            const float a = static_cast<float>(1 - k);
            d.fill(rrect(ix - 0.06, iy - 0.015, 0.12, 0.03, 0.01), white.withAlpha(a));
            d.fill(rrect(ix - 0.015, iy - 0.06, 0.03, 0.12, 0.01), white.withAlpha(a));
            if (k > 0) checkMark(d, ix, iy, 0.13, white, k);
            break;
        }
        default: d.fill(heartP(ix, iy, 0.15 * (1 + 0.35 * react * std::sin(std::min(after * 9, kPi)))), white);
    }
    // Text crossfades from the call to action to its done state.
    const double tx = x + (ix - 0.12 - x) / 2 + 0.01, maxW = ix - 0.14 - x - 0.04;
    if (k < 1) label(d, s.text, tx, y + h / 2, 0.14, maxW, white.withAlpha(static_cast<float>(1 - k)));
    if (k > 0) label(d, s.doneText, tx, y + h / 2, 0.14, maxW, white.withAlpha(static_cast<float>(k)));
    c.hand(d0, t, {0.06, 0.02});
}

// A big round icon that bounces now and then (smooth, never still for long).
void roundIcon(const D& d0, const char* a, const char* b, const std::function<void(const D&)>& icon, double period = 2.2) {
    const double t = d0.t;
    const double ph = frac(t / period);
    const double bump = std::sin(kPi * std::clamp(ph / 0.3, 0.0, 1.0));
    const D d = d0.at(Affine::rotate(-0.18 * bump) * Affine::scale(1 + 0.08 * bump));
    const Path c = circ(0, 0, 0.4);
    shadow(d, c, 0.03);
    d.rad(c, {-0.12, -0.14}, 0.55, hx(a), hx(b));
    d.stroke(circ(0, 0, 0.4), 0.025, hx("#ffffff", 0.9f));
    icon(d);
}

void floatingHearts(const D& d, int n, const char* color) {
    for (int i = 0; i < n; ++i) {
        const double ph = frac(d.t / 2.4 + i / static_cast<double>(n));
        const double x = 0.26 + 0.07 * std::sin(kTau * (ph + i * 0.3)), y = 0.1 - 0.5 * ph;
        d.fill(heartP(x, y, 0.16 * (0.6 + 0.4 * ph)), hx(color, static_cast<float>(std::sin(kPi * ph))));
    }
}

void commentTyping(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.03 * wave(t, 2.4)));
    const Path b = bubblePath(0.84, 0.56);
    shadow(d, b);
    d.lin(b, {0, -0.3}, {0, 0.3}, hx("#ffffff"), hx("#e6ecf5"));
    for (int i = 0; i < 3; ++i) {
        const double up = std::max(0.0, std::sin(kTau * (t / 1.2 - i * 0.15)));
        d.fill(circ(-0.18 + 0.18 * i, -0.02 - 0.06 * up, 0.055), hx("#5b6372", static_cast<float>(0.55 + 0.45 * up)));
    }
}

void bellBadge(const D& d0) {
    const double t = d0.t;
    const double ring = 0.35 * wave(t, 0.7) * std::pow(std::max(0.0, std::sin(kPi * frac(t / 3.0))), 2);
    const D d = d0.at(Affine::translate(-0.02, 0.04));
    const D r = d.at(Affine::translate(0, -0.4) * Affine::rotate(ring) * Affine::translate(0, 0.4));
    r.at(Affine::translate(0, 0.025)).fill(bellPath(), hx("#000000", 0.25f));
    r.lin(bellPath(), {-0.3, -0.3}, {0.3, 0.3}, hx("#ffe873"), hx("#f0a10f"));
    const double pop = 1 + 0.2 * std::exp(-std::pow((frac(t / 3.0) - 0.15) / 0.06, 2));
    const D b = d.at(Affine::translate(0.27, -0.27) * Affine::scale(pop));
    b.fill(circ(0, 0, 0.15), hx("#ff2d3d"));
    b.stroke(circ(0, 0, 0.15), 0.03, hx("#ffffff"));
    label(b, "1", 0, 0, 0.2, 0.2, hx("#ffffff"));
}

void liveBadge(const D& d0, const char* text) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.03 * wave(t, 1.6)));
    pill(d, -0.46, -0.16, 0.92, 0.32, hx("#ff4d5a"), hx("#d9001b"));
    const double on = 0.5 + 0.5 * std::cos(kTau * t / 1.2);
    d.fill(circ(-0.3, 0, 0.07 + 0.05 * on), hx("#ffffff", static_cast<float>(0.25 * on)));
    d.fill(circ(-0.3, 0, 0.055), hx("#ffffff", static_cast<float>(0.55 + 0.45 * on)));
    label(d, text, 0.07, 0, 0.17, 0.54, hx("#ffffff"));
}

void bookmark(const D& d0) {
    const double t = d0.t;
    const double fillK = 0.5 + 0.5 * std::sin(kTau * t / 3.0);
    const D d = d0.at(Affine::rotate(0.06 * wave(t, 3.0)));
    const Path p = poly({{-0.26, -0.42}, {0.26, -0.42}, {0.26, 0.42}, {0, 0.22}, {-0.26, 0.42}});
    shadow(d, p);
    d.lin(p, {0, -0.42}, {0, 0.42}, hx("#ffe14d"), hx("#ffb000"));
    d.fill(poly({{-0.26, -0.42}, {0.26, -0.42}, {0.26, -0.42 + 0.84 * (1 - fillK) * 0.5}, {-0.26, -0.42 + 0.84 * (1 - fillK) * 0.5}}),
           hx("#ffffff", 0.25f));
    d.stroke(poly({{-0.26, -0.42}, {0.26, -0.42}, {0.26, 0.42}, {0, 0.22}, {-0.26, 0.42}}), 0.03, hx("#c47a00"));
}

void eyeViews(const D& d0) {
    const double t = d0.t;
    const double open = blink(t, 3.0, 2.0);
    Path eye;
    eye.moveTo(-0.44, 0).cubicTo(-0.25, -0.3, 0.25, -0.3, 0.44, 0).cubicTo(0.25, 0.3, -0.25, 0.3, -0.44, 0).close();
    const D d = d0.at(Affine::scale(1, std::max(0.1, open)));
    shadow(d, eye);
    d.fill(eye, hx("#ffffff"));
    const double look = 0.06 * wave(t, 3.6);
    d.rad(circ(look, 0, 0.16), {look - 0.05, -0.05}, 0.2, hx("#5fb0ff"), hx("#1f5fd0"));
    d.fill(circ(look, 0, 0.075), hx("#0b1530"));
    d.fill(circ(look - 0.05, -0.05, 0.03), hx("#ffffff"));
    d.stroke(eye, 0.03, hx("#1c1c22"));
}

void linkIcon(const D& d, double cx, double cy, double s, Color c) {
    const D e = d.at(Affine::translate(cx, cy) * Affine::scale(s) * Affine::rotate(-kPi / 4));
    e.stroke(rrect(-0.46, -0.16, 0.5, 0.32, 0.16), 0.11, c);
    e.stroke(rrect(-0.04, -0.16, 0.5, 0.32, 0.16), 0.11, c);
}

void linkBio(const D& d0, const char* text) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.03 * wave(t, 2.2)));
    pill(d, -0.47, -0.14, 0.94, 0.28, hx("#ffffff"), hx("#e6ecf5"));
    d.fill(circ(-0.33, 0, 0.1), hx("#2e7bff"));
    linkIcon(d, -0.33, 0, 0.16, hx("#ffffff"));
    label(d, text, 0.08, 0, 0.13, 0.5, hx("#1c1c22"));
}

void swipeUp(const D& d0, const char* text) {
    const double t = d0.t;
    for (int i = 0; i < 3; ++i) {
        const double ph = frac(t / 1.5 + i / 3.0);
        const double y = 0.05 - 0.4 * ph;
        Path c;
        c.moveTo(-0.16, y + 0.1).lineTo(0, y - 0.04).lineTo(0.16, y + 0.1);
        d0.stroke(c, 0.055, hx("#ffffff", static_cast<float>(std::sin(kPi * ph))));
    }
    pill(d0, -0.38, 0.2, 0.76, 0.2, hx("#ffffff"), hx("#e6ecf5"));
    label(d0, text, 0, 0.3, 0.1, 0.62, hx("#1c1c22"));
}

void tapHand(const D& d0) {
    const double t = d0.t;
    const double ph = frac(t / 1.4);
    const double press = std::exp(-std::pow((ph - 0.35) / 0.08, 2));
    for (int i = 0; i < 2; ++i) {
        const double k = frac(ph - 0.35 - i * 0.25 + 1);
        if (k < 0.7) ripple(d0, -0.12, -0.2, 0.32, k / 0.7, hx("#ffd54a"));
    }
    cursorHand(d0, -0.12 + 0.02 * press, -0.2 + 0.04 * press, 0.62, press);
}

void newVideo(const D& d0, const char* text) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.06) * Affine::scale(1 + 0.03 * wave(t, 1.8)));
    const Path p = rrect(-0.47, -0.17, 0.94, 0.34, 0.08);
    shadow(d, p);
    d.lin(p, {0, -0.17}, {0, 0.17}, hx("#ff4d5a"), hx("#c8001b"));
    d.fill(poly({{-0.37, -0.09}, {-0.23, 0}, {-0.37, 0.09}}), hx("#ffffff"));
    label(d, text, 0.1, 0, 0.15, 0.54, hx("#ffffff"));
    // A shine sweeps across every few seconds (inside the label).
    const double s = frac(t / 2.6) * 2.2 - 0.6;
    if (s > -0.3 && s < 0.45) {
        const Path sh = poly({{s - 0.05, -0.17}, {s + 0.05, -0.17}, {s - 0.05, 0.17}, {s - 0.15, 0.17}});
        d.fill(sh, hx("#ffffff", 0.35f));
    }
}

void notifyCard(const D& d0, const char* title) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02 * wave(t, 2.6)));
    const Path card = rrect(-0.47, -0.18, 0.94, 0.36, 0.08);
    shadow(d, card, 0.03, 0.3f);
    d.fill(card, hx("#ffffff"));
    d.fill(circ(-0.33, 0, 0.1), hx("#ff3b4e"));
    bellIcon(d, -0.33, 0.005, 0.12, hx("#ffffff"), 0.3 * wave(t, 0.7) * std::pow(std::max(0.0, std::sin(kPi * frac(t / 2.6))), 3));
    d.fill(textPath(title, 0.06, -0.05, 0.1, 0.58, "be-vietnam-pro-bold"), hx("#1c1c22"));
    d.fill(rrect(-0.2, 0.05, 0.5, 0.04, 0.02), hx("#c7ccd6"));
}

void tagSign(const D& d0, const char* sign, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.12 * wave(t, 2.8)) * Affine::scale(1 + 0.04 * wave(t, 1.4)));
    const Path c = rrect(-0.38, -0.38, 0.76, 0.76, 0.2);
    shadow(d, c);
    d.lin(c, {-0.38, -0.38}, {0.38, 0.38}, hx(a), hx(b));
    label(d, sign, 0, 0.01, 0.62, 0.6, hx("#ffffff"));
}

void socialStack(const D& d0) {  // like / comment / share column with counts
    const double t = d0.t;
    const char* counts[] = {"12K", "860", "1,2K"};
    for (int i = 0; i < 3; ++i) {
        const double y = -0.32 + 0.32 * i;
        const double bump = std::exp(-std::pow((frac(t / 3.0) - 0.15 - 0.12 * i) / 0.05, 2));
        const D d = d0.at(Affine::translate(-0.08, y) * Affine::scale(1 + 0.18 * bump));
        if (i == 0) d.fill(heartP(0, 0, 0.26), hx("#ff2d55"));
        if (i == 1) commentIcon(d, 0, 0, 0.26, hx("#ffffff"));
        if (i == 2) shareIcon(d, 0, 0, 0.22, hx("#ffffff"));
        d0.stroke(textPath(counts[i], 0.28, y, 0.1, 0.24), 0.03, hx("#000000", 0.6f));
        label(d0, counts[i], 0.28, y, 0.1, 0.24, hx("#ffffff"));
    }
}

void stars5(const D& d0) {
    const double t = d0.t;
    for (int i = 0; i < 5; ++i) {
        const double k = between(t, 0.15 + 0.18 * i, 0.4 + 0.18 * i);
        const double x = -0.38 + 0.19 * i;
        const double pop = 1 + 0.3 * std::sin(kPi * k);
        const Path s = starP(x, 0, 5, 0.1 * pop, 0.045 * pop);
        d0.fill(starP(x, 0, 5, 0.1, 0.045), hx("#ffffff", 0.35f));
        d0.fill(s, hx("#ffc21a", static_cast<float>(k)));
    }
}

}  // namespace

void addSocialStickers(const Add& add) {
    const char* G = "Mạng xã hội";
    add("sub_button", "Nút Đăng ký", G, [](const D& d) {
        clickButton(d, {"ĐĂNG KÝ", "ĐÃ ĐĂNG KÝ", "#ff4d4d", "#d90016", "#8a8f99", "#5e636d", 0});
    });
    add("sub_button_en", "Nút Subscribe", G, [](const D& d) {
        clickButton(d, {"SUBSCRIBE", "SUBSCRIBED", "#ff4d4d", "#d90016", "#8a8f99", "#5e636d", 0});
    });
    add("like_button", "Nút Thích", G, [](const D& d) {
        clickButton(d, {"THÍCH", "ĐÃ THÍCH", "#8a8f99", "#5e636d", "#4d9bff", "#1a5fe0", 1});
    });
    add("like_button_en", "Nút Like", G, [](const D& d) {
        clickButton(d, {"LIKE", "LIKED", "#8a8f99", "#5e636d", "#4d9bff", "#1a5fe0", 1});
    });
    add("follow_button", "Nút Theo dõi", G, [](const D& d) {
        clickButton(d, {"THEO DÕI", "ĐÃ THEO DÕI", "#ff5c8a", "#e0004f", "#8a8f99", "#5e636d", 3});
    });
    add("follow_button_en", "Nút Follow", G, [](const D& d) {
        clickButton(d, {"FOLLOW", "FOLLOWING", "#ff5c8a", "#e0004f", "#8a8f99", "#5e636d", 3});
    });
    add("share_button", "Nút Chia sẻ", G, [](const D& d) {
        clickButton(d, {"CHIA SẺ", "ĐÃ CHIA SẺ", "#3fd08a", "#0f9a5a", "#2fc0a0", "#0b7f6a", 2});
    });
    add("love_button", "Nút Thả tim", G, [](const D& d) {
        clickButton(d, {"THẢ TIM", "ĐÃ THẢ TIM", "#8a8f99", "#5e636d", "#ff5c7a", "#e0002e", 4});
    });
    add("thumb_up", "Ngón cái", G, [](const D& d) {
        roundIcon(d, "#6fb2ff", "#1a5fe0", [](const D& e) { thumb(e, 0.0, 0.0, 0.5, hx("#ffffff")); });
    });
    add("thumb_down", "Không thích", G, [](const D& d) {
        roundIcon(d, "#a0a6b2", "#5e636d", [](const D& e) { thumb(e, 0.0, 0.0, 0.5, hx("#ffffff"), true); }, 2.6);
    });
    add("heart_react", "Thả tim", G, [](const D& d) {
        roundIcon(d, "#ff8aa0", "#e0002e", [](const D& e) { e.fill(heartP(0, 0.02, 0.46), hx("#ffffff")); }, 1.2);
        floatingHearts(d, 3, "#ff4d6d");
    });
    add("share_round", "Chia sẻ", G, [](const D& d) {
        roundIcon(d, "#6ee8a8", "#0f9a5a", [](const D& e) { shareIcon(e, 0.02, 0.02, 0.48, hx("#ffffff")); }, 2.6);
    });
    add("comment_typing", "Đang bình luận", G, commentTyping);
    add("bell_badge", "Chuông thông báo", G, bellBadge);
    add("live_badge", "LIVE", G, [](const D& d) { liveBadge(d, "LIVE"); });
    add("live_badge_vi", "Trực tiếp", G, [](const D& d) { liveBadge(d, "TRỰC TIẾP"); });
    add("bookmark", "Lưu lại", G, bookmark);
    add("eye_views", "Lượt xem", G, eyeViews);
    add("link_bio", "Link ở bio", G, [](const D& d) { linkBio(d, "Link ở bio"); });
    add("swipe_up", "Vuốt lên", G, [](const D& d) { swipeUp(d, "VUỐT LÊN"); });
    add("tap_hand", "Chạm vào", G, tapHand);
    add("new_video", "Video mới", G, [](const D& d) { newVideo(d, "VIDEO MỚI"); });
    add("notify_card", "Thông báo mới", G, [](const D& d) { notifyCard(d, "Video mới đã lên!"); });
    add("hashtag", "Hashtag", G, [](const D& d) { tagSign(d, "#", "#7f8bff", "#4a3aff"); });
    add("mention", "Nhắc tên @", G, [](const D& d) { tagSign(d, "@", "#3fd0ff", "#1f7fe0"); });
    add("social_stack", "Cột tương tác", G, socialStack);
    add("stars_rating", "Đánh giá 5 sao", G, stars5);
}

// ---------------------------------------------------------------- the other new groups

namespace {

// Starburst seal with a word, slowly turning (the word stays upright).
void seal(const D& d0, const char* text, const char* a, const char* b, const char* ink = "#ffffff") {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.05 * wave(t, 1.6)));
    const Path s = starP(0, 0, 16, 0.46, 0.39, t * 0.4);
    shadow(d, s);
    d.rad(s, {-0.1, -0.12}, 0.55, hx(a), hx(b));
    d.stroke(circ(0, 0, 0.32), 0.015, hx(ink, 0.6f));
    d.at(Affine::rotate(-0.2)).fill(textPath(text, 0, 0, 0.26, 0.5), hx(ink));
}

// Ribbon banner with folded ends.
void ribbon(const D& d0, const char* text, const char* a, const char* b) {
    const double t = d0.t;
    const double wv = 0.025 * wave(t, 2.0);
    const D d = d0.at(Affine::translate(0, wv));
    for (double s : {-1.0, 1.0}) {
        const Path end = poly({{s * 0.3, -0.04}, {s * 0.5, -0.04}, {s * 0.4, 0.1}, {s * 0.5, 0.24}, {s * 0.3, 0.24}});
        d.fill(end, mix(hx(b), hx("#000000"), 0.2f));
        d.fill(poly({{s * 0.3, 0.16}, {s * 0.38, 0.24}, {s * 0.38, 0.16}}), mix(hx(b), hx("#000000"), 0.45f));
    }
    Path band;
    band.moveTo(-0.38, -0.2).cubicTo(-0.15, -0.27, 0.15, -0.27, 0.38, -0.2).lineTo(0.38, 0.16);
    band.cubicTo(0.15, 0.09, -0.15, 0.09, -0.38, 0.16).close();
    shadow(d, band);
    d.lin(band, {0, -0.25}, {0, 0.16}, hx(a), hx(b));
    label(d, text, 0, -0.05, 0.17, 0.66, hx("#ffffff"));
}

// Price tag on a string, swinging.
void priceTag(const D& d0, const char* text, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, -0.42) * Affine::rotate(0.15 * wave(t, 2.2)) * Affine::translate(0, 0.42));
    d.stroke(line({0, -0.46}, {0, -0.2}), 0.02, hx("#c9ccd4"));
    const Path tag = poly({{0, -0.26}, {0.26, -0.06}, {0.26, 0.42}, {-0.26, 0.42}, {-0.26, -0.06}});
    shadow(d, tag);
    d.lin(tag, {0, -0.26}, {0, 0.42}, hx(a), hx(b));
    d.fill(circ(0, -0.08, 0.045), hx("#ffffff"));
    const std::string str(text);
    const auto sp = str.find(' ');
    if (sp == std::string::npos) {
        label(d, str, 0, 0.2, 0.2, 0.44, hx("#ffffff"));
    } else {
        label(d, str.substr(0, sp), 0, 0.12, 0.17, 0.44, hx("#ffffff"));
        label(d, str.substr(sp + 1), 0, 0.3, 0.17, 0.44, hx("#ffffff"));
    }
}

// Call-to-action button with an arrow and a shine.
void ctaButton(const D& d0, const char* text, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.04 * std::pow(std::max(0.0, std::sin(kTau * t / 1.4)), 2)));
    pill(d, -0.47, -0.15, 0.94, 0.3, hx(a), hx(b));
    label(d, text, -0.06, 0, 0.14, 0.62, hx("#ffffff"));
    const double nudge = 0.03 * std::sin(kTau * t / 0.9);
    d.fill(poly({{0.3 + nudge, -0.07}, {0.39 + nudge, 0}, {0.3 + nudge, 0.07}}), hx("#ffffff"));
    const double s = frac(t / 2.4) * 2.4 - 0.7;
    if (s > -0.25 && s < 0.25) d.fill(poly({{s - 0.03, -0.15}, {s + 0.06, -0.15}, {s - 0.03, 0.15}, {s - 0.12, 0.15}}), hx("#ffffff", 0.3f));
}

// Comic word on a spiky burst, with a thick outline (Bungee).
void comicWord(const D& d0, const char* text, const char* burstA, const char* burstB, const char* ink, double tilt = -0.12) {
    const double t = d0.t;
    const double jolt = std::exp(-std::pow((frac(t / 1.8) - 0.1) / 0.05, 2));
    const D d = d0.at(Affine::rotate(tilt + 0.05 * jolt) * Affine::scale(1 + 0.08 * jolt));
    Path burst;
    const int n = 14;
    for (int i = 0; i < 2 * n; ++i) {
        const double ang = kPi * i / n;
        const double r = i % 2 == 0 ? 0.47 - 0.04 * ((i * 7) % 3) : 0.33;
        if (i == 0) burst.moveTo(std::cos(ang) * r, std::sin(ang) * r * 0.82);
        else burst.lineTo(std::cos(ang) * r, std::sin(ang) * r * 0.82);
    }
    burst.close();
    d.stroke(burst, 0.04, hx("#1c1c22"));
    d.rad(burst, {0, 0}, 0.45, hx(burstA), hx(burstB));
    const Path word = textPath(text, 0, 0, 0.3, 0.62, "bungee");
    d.at(Affine::translate(0.015, 0.025)).fill(word, hx("#1c1c22"));
    d.stroke(word, 0.05, hx("#1c1c22"));
    d.fill(word, hx(ink));
}

void sayBubble(const D& d0, const char* text, const char* fillC, const char* ink) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.025 * wave(t, 2.4)) * Affine::rotate(0.04 * wave(t, 2.4, 0.3)));
    const Path b = bubblePath(0.92, 0.52);
    shadow(d, b);
    d.stroke(b, 0.05, hx("#1c1c22"));
    d.fill(b, hx(fillC));
    label(d, text, 0, 0, 0.15, 0.78, hx(ink));
}

// ---- hand-drawn (each draws itself once, then stays)

Path partial(const std::vector<Pt>& pts, double k) {
    Path p;
    const int n = static_cast<int>(pts.size());
    const double upto = k * (n - 1);
    for (int i = 0; i < n && i <= upto + 1; ++i) {
        Pt q = pts[i];
        if (i > upto) q = lerp(pts[i - 1], pts[i], upto - (i - 1));
        if (i == 0) p.moveTo(q.x, q.y);
        else p.lineTo(q.x, q.y);
    }
    return p;
}
std::vector<Pt> sample(int n, const std::function<Pt(double)>& f) {
    std::vector<Pt> v;
    for (int i = 0; i <= n; ++i) v.push_back(f(i / static_cast<double>(n)));
    return v;
}
void doodle(const D& d, const std::vector<std::vector<Pt>>& strokes, double w, const char* color) {
    // Strokes draw one after another within the same short draw-on time.
    const double k = drawOn(d.t) * strokes.size();
    for (std::size_t i = 0; i < strokes.size(); ++i) {
        const double ki = std::clamp(k - i, 0.0, 1.0);
        if (ki > 0) d.stroke(partial(strokes[i], ki), w, hx(color));
    }
}

void doodleDoubleUnderline(const D& d) {
    auto l = [](double y, double tilt) {
        return sample(30, [=](double u) { return Pt{-0.44 + 0.88 * u, y + tilt * u + 0.02 * std::sin(u * 9)}; });
    };
    doodle(d, {l(-0.03, -0.04), l(0.1, -0.05)}, 0.045, "#ff3b6b");
}
void doodleZigzag(const D& d) {
    std::vector<Pt> v;
    for (int i = 0; i <= 10; ++i) v.push_back({-0.44 + 0.088 * i, i % 2 ? -0.08 : 0.08});
    doodle(d, {v}, 0.045, "#2ec4ff");
}
void doodleWavy(const D& d) {
    doodle(d, {sample(60, [](double u) { return Pt{-0.44 + 0.88 * u, 0.09 * std::sin(u * kTau * 2.5)}; })}, 0.045, "#7c4dff");
}
void doodleLoopArrow(const D& d) {
    auto path = sample(80, [](double u) {
        const double x = -0.42 + 0.78 * u;
        const double a = u * kTau * 1.0;
        const double loop = std::exp(-std::pow((u - 0.45) / 0.14, 2));
        return Pt{x + 0.16 * loop * std::sin(a * 2.0), 0.12 - 0.22 * u + 0.18 * loop * (1 - std::cos(a * 2.0)) * -0.6};
    });
    const Pt e = path.back(), p = path[path.size() - 6];
    const double ang = std::atan2(e.y - p.y, e.x - p.x);
    std::vector<Pt> h1{{e.x + 0.12 * std::cos(ang + 2.5), e.y + 0.12 * std::sin(ang + 2.5)}, e,
                       {e.x + 0.12 * std::cos(ang - 2.5), e.y + 0.12 * std::sin(ang - 2.5)}};
    doodle(d, {path, h1}, 0.045, "#ff8a1f");
}
void doodleEmphasis(const D& d) {
    std::vector<std::vector<Pt>> s;
    for (int i = 0; i < 3; ++i) {
        const double a = -kPi / 2 - 0.6 + 0.6 * i;
        s.push_back({{0.1 * std::cos(a) - 0.1, 0.1 * std::sin(a) + 0.2}, {0.42 * std::cos(a) - 0.1, 0.42 * std::sin(a) + 0.2}});
    }
    doodle(d, s, 0.05, "#ffd21f");
}
void doodleStar(const D& d) {
    std::vector<Pt> v;
    for (int i = 0; i <= 5; ++i) {
        const double a = -kPi / 2 + i * 4 * kPi / 5;
        v.push_back({0.42 * std::cos(a), 0.42 * std::sin(a) + 0.03});
    }
    doodle(d, {v}, 0.04, "#ffc21a");
}
void doodleHeart(const D& d) {
    doodle(d, {sample(80, [](double u) {
               const double a = u * kTau;
               const double x = 16 * std::pow(std::sin(a), 3);
               const double y = 13 * std::cos(a) - 5 * std::cos(2 * a) - 2 * std::cos(3 * a) - std::cos(4 * a);
               return Pt{x / 38, -y / 38 + 0.03};
           })},
           0.04, "#ff3b6b");
}
void doodleCrown(const D& d) {
    doodle(d, {{{-0.36, 0.22}, {-0.4, -0.2}, {-0.18, 0.02}, {0, -0.3}, {0.18, 0.02}, {0.4, -0.2}, {0.36, 0.22}, {-0.36, 0.22}}},
           0.04, "#ffc21a");
}
void doodleSpiral(const D& d) {
    doodle(d, {sample(90, [](double u) {
               const double a = u * kTau * 3;
               const double r = 0.04 + 0.38 * u;
               return Pt{r * std::cos(a), r * std::sin(a)};
           })},
           0.035, "#2ec4ff");
}
void doodleX(const D& d) { doodle(d, {{{-0.3, -0.3}, {0.3, 0.3}}, {{0.3, -0.3}, {-0.3, 0.3}}}, 0.07, "#ff3b3b"); }
void doodleHighlight(const D& d) {
    const double k = drawOn(d.t);
    d.fill(rrect(-0.46, -0.1, 0.92 * k, 0.2, 0.04), hx("#ffe14d", 0.55f));
}
void doodleBrackets(const D& d) {
    doodle(d, {{{-0.22, -0.4}, {-0.38, -0.4}, {-0.38, 0.4}, {-0.22, 0.4}}, {{0.22, -0.4}, {0.38, -0.4}, {0.38, 0.4}, {0.22, 0.4}}},
           0.045, "#ffffff");
}
void doodleBang(const D& d) {
    doodle(d, {{{-0.12, -0.38}, {-0.14, 0.12}}, {{0.14, -0.38}, {0.12, 0.12}}}, 0.08, "#ff3b3b");
    const double k = std::clamp(drawOn(d.t) * 2 - 1, 0.0, 1.0);
    for (double x : {-0.15, 0.11}) d.fill(circ(x, 0.3, 0.05 * k), hx("#ff3b3b"));
}
void doodleSparkle(const D& d) {
    const double s = 1 + 0.08 * wave(d.t, 1.4);
    Path p;
    p.moveTo(0, -0.42 * s).quadTo(0.04, -0.04, 0.42 * s, 0).quadTo(0.04, 0.04, 0, 0.42 * s);
    p.quadTo(-0.04, 0.04, -0.42 * s, 0).quadTo(-0.04, -0.04, 0, -0.42 * s);
    const double k = drawOn(d.t);
    d.stroke(p, 0.04, hx("#ffffff", static_cast<float>(k)));
}

// ---- video & channel

void countdown(const D& d0) {
    const double t = d0.t;
    const char* words[] = {"3", "2", "1", "GO!"};
    const double step = 0.9;
    const int i = std::min(3, static_cast<int>(t / step));
    const double ph = std::min(1.0, (t - i * step) / step);
    d0.rad(circ(0, 0, 0.42), {-0.1, -0.1}, 0.5, hx("#3a3f4b"), hx("#1c1f26"));
    d0.stroke(circ(0, 0, 0.42), 0.03, hx("#ffffff", 0.3f));
    if (i < 3) {
        Path a = pie(0, 0, 0.36, 0.36, -kPi / 2, -kPi / 2 + kTau * ph, 48);
        d0.fill(a, hx("#ff3b4e", 0.85f));
        d0.fill(circ(0, 0, 0.3), hx("#1c1f26"));
    } else {
        d0.fill(circ(0, 0, 0.36), hx("#24c26a"));
    }
    // Each number grows in and fades while the next one grows in (no hard swap).
    for (int j = std::max(0, i - 1); j <= std::min(3, i + 1); ++j) {
        const double local = (t - j * step) / step;
        const double in = j == 0 ? 1.0 : ease((local + 0.12) / 0.24), out = j < 3 ? 1 - ease((local - 0.85) / 0.24) : 1;
        const float a = static_cast<float>(in * out);
        if (a <= 0.01) continue;
        const double sc = (0.6 + 0.4 * in) * (1 + 0.5 * (1 - out));  // the old number grows away as it fades
        d0.at(Affine::scale(sc)).fill(textPath(words[j], 0, 0, j < 3 ? 0.48 : 0.28, 0.56, "bungee"), hx("#ffffff", a));
    }
}

void clapper(const D& d0) {
    const double t = d0.t;
    const double clap = between(t, 0.35, 0.6);  // closes once and stays shut
    const D d = d0.at(Affine::translate(0, 0.09));
    const Path board = rrect(-0.42, -0.12, 0.84, 0.46, 0.04);
    shadow(d, board);
    d.fill(board, hx("#2a2d35"));
    for (int i = 0; i < 3; ++i) d.fill(rrect(-0.34, -0.02 + 0.13 * i, 0.68, 0.03, 0.01), hx("#ffffff", 0.5f));
    const D top = d.at(Affine::translate(-0.42, -0.14) * Affine::rotate(-0.38 * (1 - clap)));
    top.fill(rrect(0, -0.12, 0.84, 0.12, 0.02), hx("#2a2d35"));
    for (int i = 0; i < 5; ++i) top.fill(poly({{0.04 + 0.17 * i, -0.12}, {0.13 + 0.17 * i, -0.12}, {0.07 + 0.17 * i, 0}, {-0.02 + 0.17 * i, 0}}), hx("#ffffff"));
}

void recBadge(const D& d0) {
    const double t = d0.t;
    const double on = 0.5 + 0.5 * std::cos(kTau * t / 1.0);
    const Path p = rrect(-0.4, -0.16, 0.8, 0.32, 0.06);
    d0.fill(p, hx("#000000", 0.55f));
    d0.stroke(p, 0.02, hx("#ffffff", 0.8f));
    d0.fill(circ(-0.24, 0, 0.07), hx("#ff2d3d", static_cast<float>(0.3 + 0.7 * on)));
    label(d0, "REC", 0.08, 0, 0.17, 0.4, hx("#ffffff"));
}

void mic(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.25 + 0.06 * wave(t, 2.2)));
    d.stroke(arc(0, -0.04, 0.2, 0.2, 0, kPi), 0.04, hx("#9aa1ad"));
    d.stroke(line({0, 0.16}, {0, 0.36}), 0.05, hx("#9aa1ad"));
    d.fill(rrect(-0.14, 0.33, 0.28, 0.06, 0.03), hx("#9aa1ad"));
    const Path head = rrect(-0.13, -0.44, 0.26, 0.5, 0.13);
    d.lin(head, {-0.13, 0}, {0.13, 0}, hx("#5b6372"), hx("#2a2d35"));
    for (int i = 0; i < 4; ++i) d.fill(rrect(-0.1, -0.36 + 0.07 * i, 0.2, 0.025, 0.012), hx("#ffffff", 0.35f));
    for (int i = 0; i < 2; ++i) {
        const double ph = frac(t / 1.6 + i * 0.5);
        d0.stroke(arc(0.05, -0.18, 0.28 + 0.12 * ph, 0.28 + 0.12 * ph, -0.6, 0.4), 0.025, hx("#ffd54a", static_cast<float>(std::sin(kPi * ph))));
    }
}

void partTag(const D& d0, const char* text, const char* a, const char* b) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.03 * wave(t, 2.6), 0));
    const Path p = poly({{-0.46, -0.16}, {0.34, -0.16}, {0.46, 0}, {0.34, 0.16}, {-0.46, 0.16}});
    shadow(d, p);
    d.lin(p, {-0.46, 0}, {0.46, 0}, hx(a), hx(b));
    label(d, text, -0.04, 0, 0.15, 0.7, hx("#ffffff"));
}

void endCard(const D& d0, const char* text) {
    const double t = d0.t;
    const double k = drawOn(t);
    d0.stroke(arc(0, 0, 0.44, 0.24, -kPi / 2, -kPi / 2 + kTau * k, 64), 0.025, hx("#ffd54a"));
    d0.fill(textPath(text, 0, 0, 0.2, 0.7, "pacifico"), hx("#ffffff", static_cast<float>(ease((t - 0.1) / 0.5))));
}

// ---- sports & games

void football(const D& d0) {
    const double t = d0.t;
    const double ph = frac(t / 1.1);
    const double h = 4 * ph * (1 - ph);
    const double squash = 1 - 0.12 * std::exp(-std::pow(std::min(ph, 1 - ph) / 0.05, 2));
    d0.fill(ell(0, 0.43, 0.22 * (1 - 0.4 * h), 0.04), hx("#000000", static_cast<float>(0.3 - 0.15 * h)));
    const D d = d0.at(Affine::translate(0, 0.17 - 0.3 * h) * Affine::scale(1 / squash, squash) * Affine::rotate(t * 2.5));
    d.rad(circ(0, 0, 0.25), {-0.08, -0.08}, 0.35, hx("#ffffff"), hx("#c9ccd4"));
    d.fill(starP(0, 0, 5, 0.09, 0.075, -kPi / 2), hx("#1c1c22"));
    for (int i = 0; i < 5; ++i) {
        const double a = -kPi / 2 + i * kTau / 5;
        d.fill(starP(0.22 * std::cos(a), 0.22 * std::sin(a), 5, 0.06, 0.05, a + kPi / 2), hx("#1c1c22"));
        d.stroke(line({0.09 * std::cos(a), 0.09 * std::sin(a)}, {0.17 * std::cos(a), 0.17 * std::sin(a)}), 0.012, hx("#1c1c22"));
    }
    d.stroke(circ(0, 0, 0.25), 0.012, hx("#1c1c22"));
}

void basketball(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(t * 1.5));
    d.rad(circ(0, 0, 0.4), {-0.12, -0.12}, 0.55, hx("#ffa04a"), hx("#e0601a"));
    const Color ink = hx("#5a2406");
    d.stroke(line({-0.385, 0}, {0.385, 0}), 0.02, ink);
    d.stroke(line({0, -0.385}, {0, 0.385}), 0.02, ink);
    d.stroke(arc(-0.48, 0, 0.3, 0.36, -0.85, 0.85), 0.02, ink);
    d.stroke(arc(0.48, 0, 0.3, 0.36, kPi - 0.85, kPi + 0.85), 0.02, ink);
    d.stroke(circ(0, 0, 0.4), 0.02, ink);
}

void gamepad(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.08 * wave(t, 2.4)));
    Path b;
    b.moveTo(-0.3, -0.18).lineTo(0.3, -0.18).cubicTo(0.46, -0.18, 0.5, 0.0, 0.47, 0.18);
    b.cubicTo(0.44, 0.34, 0.3, 0.32, 0.22, 0.2).lineTo(-0.22, 0.2).cubicTo(-0.3, 0.32, -0.44, 0.34, -0.47, 0.18);
    b.cubicTo(-0.5, 0.0, -0.46, -0.18, -0.3, -0.18).close();
    shadow(d, b);
    d.lin(b, {0, -0.18}, {0, 0.3}, hx("#8f7bff"), hx("#5a3fe0"));
    d.fill(rrect(-0.33, -0.03, 0.18, 0.06, 0.02), hx("#ffffff"));
    d.fill(rrect(-0.27, -0.09, 0.06, 0.18, 0.02), hx("#ffffff"));
    const char* cols[] = {"#ff5a6e", "#ffd54a", "#3fe0a0", "#3fd0ff"};
    const Pt pos[] = {{0.3, -0.07}, {0.37, 0.0}, {0.3, 0.07}, {0.23, 0.0}};
    for (int i = 0; i < 4; ++i) {
        const double press = std::exp(-std::pow((frac(t / 2.0) - 0.2 * i) / 0.05, 2));
        d.fill(circ(pos[i].x, pos[i].y + 0.01 * press, 0.035), hx(cols[i]));
    }
}

void coin(const D& d0) {
    const double t = d0.t;
    const double sx = std::cos(kTau * t / 1.6);
    const D d = d0.at(Affine::translate(0, 0.03 * wave(t, 0.8)) * Affine::scale(std::abs(sx) * 0.96 + 0.04, 1));
    d.rad(circ(0, 0, 0.4), {-0.1, -0.12}, 0.5, hx("#fff07a"), hx("#e8a000"));
    d.stroke(circ(0, 0, 0.32), 0.03, hx("#c47a00"));
    if (sx > 0) label(d, "$", 0, 0.02, 0.42, 0.4, hx("#c47a00"), "bungee");
    else d.fill(starP(0, 0, 5, 0.18, 0.08), hx("#c47a00"));
}

void pixelHeart(const D& d0) {
    const double t = d0.t;
    const char* rows[] = {".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..."};
    const double s = 0.12 * (1 + 0.07 * std::max(0.0, std::sin(kTau * t / 1.0)));
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 7; ++c)
            if (rows[r][c] == 'X') {
                const double x = (c - 3.5) * s, y = (r - 3) * s;
                d0.fill(rrect(x, y, s * 0.94, s * 0.94, s * 0.1), r == 1 && c == 1 ? hx("#ffffff") : hx(r < 3 ? "#ff4d6d" : "#d6002a"));
            }
}

void wordBanner(const D& d0, const char* line1, const char* line2, const char* a, const char* b, const char* font,
                double rot = 0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(rot) * Affine::scale(1 + 0.05 * wave(t, 1.3)));
    Path w = textPath(line1, 0, -0.12, 0.3, 0.86, font);
    w.append(textPath(line2, 0, 0.17, 0.3, 0.86, font));
    d.at(Affine::translate(0.02, 0.03)).fill(w, hx("#1c1c22", 0.8f));
    d.stroke(w, 0.05, hx("#1c1c22"));
    d.lin(w, {0, -0.26}, {0, 0.3}, hx(a), hx(b));
    for (int i = 0; i < 3; ++i) {
        const double ph = frac(t / 1.8 + i / 3.0);
        d.fill(starP(-0.4 + 0.4 * i, -0.36 - 0.04 * (i % 2), 4, 0.06 * std::sin(kPi * ph), 0.018), hx("#ffffff"));
    }
}

void levelUp(const D& d0) {
    const double t = d0.t;
    const double rise = 0.05 * wave(t, 1.2);
    const D d = d0.at(Affine::translate(0, rise));
    const Path ar = poly({{0, -0.46}, {0.22, -0.22}, {0.09, -0.22}, {0.09, 0.02}, {-0.09, 0.02}, {-0.09, -0.22}, {-0.22, -0.22}});
    shadow(d, ar);
    d.lin(ar, {0, -0.46}, {0, 0.02}, hx("#7dff9a"), hx("#16b34a"));
    const Path w = textPath("LEVEL UP", 0, 0.22, 0.2, 0.9, "bungee");
    d.stroke(w, 0.04, hx("#1c1c22"));
    d.lin(w, {0, 0.12}, {0, 0.3}, hx("#fff27a"), hx("#ffae1a"));
}

// ---- Tết & seasonal

void banhChung(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02 * wave(t, 2.4)) * Affine::rotate(0.05 * wave(t, 3.2)));
    const Path sq = rrect(-0.36, -0.36, 0.72, 0.72, 0.05);
    shadow(d, sq);
    d.lin(sq, {-0.36, -0.36}, {0.36, 0.36}, hx("#5fc24a"), hx("#1f7a2a"));
    for (int i = 0; i < 4; ++i) {
        const double o = -0.27 + 0.18 * i;
        d.stroke(line({o, -0.36}, {o + 0.04, 0.36}), 0.008, hx("#0f4d17", 0.5f));
    }
    for (double o : {-0.12, 0.12}) {
        d.fill(rrect(o - 0.02, -0.38, 0.04, 0.76, 0.01), hx("#f2e6c9"));
        d.fill(rrect(-0.38, o - 0.02, 0.76, 0.04, 0.01), hx("#f2e6c9"));
    }
}

void firecrackers(const D& d0) {
    const double t = d0.t;
    d0.stroke(line({0, -0.48}, {0, 0.42}), 0.015, hx("#c47a00"));
    for (int i = 0; i < 6; ++i) {
        const double y = -0.36 + 0.14 * i;
        for (double s : {-1.0, 1.0}) {
            const double sw = 0.12 * wave(t, 1.0, i * 0.12 + (s > 0 ? 0.5 : 0));
            const D d = d0.at(Affine::translate(0, y) * Affine::rotate(s * (0.9 + sw)));
            d.lin(rrect(-0.04, 0.02, 0.08, 0.2, 0.02), {-0.04, 0}, {0.04, 0}, hx("#ff5a4a"), hx("#c8001b"));
            d.fill(rrect(-0.045, 0.02, 0.09, 0.03, 0.01), hx("#ffd54a"));
        }
    }
    d0.fill(rrect(-0.06, -0.48, 0.12, 0.1, 0.03), hx("#ffd54a"));
}

void tetDiamond(const D& d0, const char* text) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, -0.42) * Affine::rotate(0.08 * wave(t, 2.6)) * Affine::translate(0, 0.42));
    const Path dm = poly({{0, -0.42}, {0.4, 0}, {0, 0.42}, {-0.4, 0}});
    shadow(d, dm);
    d.lin(dm, {0, -0.42}, {0, 0.42}, hx("#ff4d3d"), hx("#c8001b"));
    d.stroke(poly({{0, -0.35}, {0.33, 0}, {0, 0.35}, {-0.33, 0}}), 0.015, hx("#ffd54a"));
    label(d, text, 0, 0, 0.2, 0.42, hx("#ffd54a"), "lobster");
}

void tetBanner(const D& d0, const char* text) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02 * wave(t, 2.2)));
    const Path b = rrect(-0.47, -0.27, 0.94, 0.54, 0.08);
    shadow(d, b);
    d.lin(b, {0, -0.27}, {0, 0.27}, hx("#e8202a"), hx("#a8000f"));
    d.stroke(rrect(-0.43, -0.23, 0.86, 0.46, 0.05), 0.014, hx("#ffd54a"));
    const std::string s(text);
    const auto cut = s.find(' ', s.find(' ') + 1);  // "Chúc Mừng" / "Năm Mới"
    Path w = textPath(cut == std::string::npos ? s : s.substr(0, cut), 0, -0.09, 0.2, 0.78, "lobster");
    if (cut != std::string::npos) w.append(textPath(s.substr(cut + 1), 0, 0.12, 0.2, 0.78, "lobster"));
    d.lin(w, {0, -0.2}, {0, 0.2}, hx("#fff3a8"), hx("#ffc21a"));
    for (double sx : {-1.0, 1.0}) {
        const double tw = 0.5 + 0.5 * std::sin(kTau * (t / 1.6 + (sx > 0 ? 0.5 : 0)));
        d.fill(starP(sx * 0.42, -0.3, 4, 0.07 * tw, 0.018), hx("#ffe14d"));
    }
}

void yearGold(const D& d0, const char* text) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.04 * wave(t, 1.6)));
    const Path w = textPath(text, 0, 0, 0.46, 0.9, "bungee");
    d.at(Affine::translate(0.02, 0.03)).fill(w, hx("#7a3d00", 0.7f));
    d.lin(w, {0, -0.18}, {0, 0.18}, hx("#fff3a8"), hx("#e8a000"));
    for (int i = 0; i < 4; ++i) {
        const double ph = frac(t / 2.0 + i / 4.0);
        const Pt p{-0.36 + 0.24 * i, (i % 2 ? 0.22 : -0.24)};
        d.fill(starP(p.x, p.y, 4, 0.07 * std::sin(kPi * ph), 0.02), hx("#ffffff"));
    }
}

void starLantern(const D& d0) {  // đèn ông sao
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, -0.46) * Affine::rotate(0.1 * wave(t, 2.6)) * Affine::translate(0, 0.46));
    d.stroke(line({0, -0.48}, {0, -0.32}), 0.015, hx("#7a4a1a"));
    const Path s = starP(0, 0.02, 5, 0.38, 0.17);
    d.rad(s, {0, 0.02}, 0.42, hx("#fff3a8", 1), hx("#ff3b2e"));
    d.fill(circ(0, 0.02, 0.1 + 0.02 * wave(t, 0.7)), hx("#fff7d0", 0.7f));
    d.stroke(s, 0.02, hx("#c8001b"));
    for (double x : {-0.06, 0.0, 0.06}) d.stroke(line({x, 0.32}, {x * 1.6, 0.48}), 0.012, hx("#ffd54a"));
}

void mooncake(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.05 * wave(t, 3.0)));
    Path p;
    for (int i = 0; i < 48; ++i) {
        const double a = kTau * i / 48;
        const double r = 0.4 + 0.03 * std::cos(a * 12);
        if (i == 0) p.moveTo(r * std::cos(a), r * std::sin(a));
        else p.lineTo(r * std::cos(a), r * std::sin(a));
    }
    p.close();
    shadow(d, p);
    d.rad(p, {-0.1, -0.12}, 0.5, hx("#f7c66b"), hx("#b36b16"));
    d.stroke(circ(0, 0, 0.27), 0.025, hx("#8a4a0a", 0.7f));
    d.stroke(starP(0, 0, 4, 0.16, 0.06, 0), 0.02, hx("#8a4a0a", 0.7f));
}

void pumpkin(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.03 * wave(t, 1.4), 1 - 0.03 * wave(t, 1.4)));
    for (double x : {-0.2, 0.2, 0.0}) d.rad(ell(x, 0.06, 0.24, 0.32), {x - 0.05, -0.05}, 0.4, hx("#ffb347"), hx("#e0601a"));
    d.fill(rrect(-0.04, -0.36, 0.08, 0.12, 0.03), hx("#5a8a2a"));
    const double glow = 0.75 + 0.25 * std::sin(kTau * t / 0.9);
    const Color in = hx("#ffe873", static_cast<float>(glow));
    d.fill(poly({{-0.2, -0.02}, {-0.1, -0.02}, {-0.15, -0.1}}), in);
    d.fill(poly({{0.2, -0.02}, {0.1, -0.02}, {0.15, -0.1}}), in);
    d.fill(poly({{-0.22, 0.12}, {-0.12, 0.2}, {-0.04, 0.13}, {0.04, 0.2}, {0.12, 0.13}, {0.22, 0.12}, {0.12, 0.27}, {-0.12, 0.27}}), in);
}

void ghost(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.05 * wave(t, 3.0), 0.05 * wave(t, 1.5)));
    Path g;
    g.moveTo(-0.3, 0.38).lineTo(-0.3, -0.08).cubicTo(-0.3, -0.5, 0.3, -0.5, 0.3, -0.08).lineTo(0.3, 0.38);
    for (int i = 0; i < 4; ++i) {
        const double x0 = 0.3 - 0.15 * i;
        const double w = 0.02 * wave(t, 0.8, i * 0.25);
        g.quadTo(x0 - 0.075, 0.3 + w, x0 - 0.15, 0.38);
    }
    g.close();
    shadow(d, g, 0.04, 0.2f);
    d.lin(g, {0, -0.4}, {0, 0.38}, hx("#ffffff"), hx("#dbe4f5"));
    const double open = blink(t, 3.2, 1.0);
    for (double s : {-1.0, 1.0}) d.fill(ell(s * 0.1, -0.08, 0.045, 0.07 * open), hx("#1c1c22"));
    d.fill(ell(0, 0.08, 0.05, 0.06), hx("#1c1c22"));
}

void santaHat(const D& d0) {
    const double t = d0.t;
    const double sw = 0.2 * wave(t, 2.0);
    Path h;
    h.moveTo(-0.36, 0.22).cubicTo(-0.3, -0.2, 0.0, -0.4, 0.25 + 0.05 * sw, -0.3 + 0.08 * sw);
    h.cubicTo(0.3, -0.1, 0.3, 0.1, 0.36, 0.22).close();
    shadow(d0, h);
    d0.lin(h, {0, -0.4}, {0, 0.22}, hx("#ff4d4d"), hx("#c8001b"));
    d0.fill(rrect(-0.42, 0.16, 0.84, 0.18, 0.09), hx("#ffffff"));
    d0.rad(circ(0.28 + 0.05 * sw, -0.32 + 0.08 * sw, 0.09), {0.26, -0.34}, 0.12, hx("#ffffff"), hx("#dbe4f5"));
}

void snowman(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.42) * Affine::rotate(0.06 * wave(t, 2.4)) * Affine::translate(0, -0.42));
    d.rad(circ(0, 0.2, 0.24), {-0.07, 0.12}, 0.3, hx("#ffffff"), hx("#cfdcf0"));
    d.rad(circ(0, -0.18, 0.17), {-0.05, -0.24}, 0.22, hx("#ffffff"), hx("#cfdcf0"));
    d.fill(rrect(-0.17, -0.06, 0.34, 0.07, 0.03), hx("#e0202a"));
    d.fill(rrect(0.06, -0.05, 0.07, 0.18, 0.03), hx("#e0202a"));
    for (double s : {-1.0, 1.0}) d.fill(circ(s * 0.06, -0.22, 0.025), hx("#1c1c22"));
    d.fill(poly({{0, -0.17}, {0.13, -0.15}, {0, -0.13}}), hx("#ff8a1f"));
    for (int i = 0; i < 3; ++i) d.fill(circ(0, 0.08 + 0.09 * i, 0.022), hx("#1c1c22"));
    for (int i = 0; i < 6; ++i) {
        const double ph = frac(t / 3.0 + i / 6.0);
        d0.fill(circ(-0.42 + 0.17 * i, -0.48 + 0.96 * ph, 0.018), hx("#ffffff", static_cast<float>(std::sin(kPi * ph))));
    }
}

// ---- travel & daily life

void plane(const D& d0) {
    const double t = d0.t;
    const double k = drawOn(t);
    Path trail;
    for (int i = 0; i <= static_cast<int>(40 * k); ++i) {
        const double u = i / 40.0;
        const double x = -0.44 + 0.6 * u, y = 0.32 - 0.36 * u + 0.08 * std::sin(u * kTau);
        if (i == 0) trail.moveTo(x, y);
        else if (i % 3 != 2) trail.lineTo(x, y);
        else trail.moveTo(x, y);
    }
    d0.stroke(trail, 0.018, hx("#ffffff", 0.8f));
    const D d = d0.at(Affine::translate(0.2, -0.12 + 0.03 * wave(t, 2.0)) * Affine::rotate(-0.5) * Affine::scale(0.55));
    d.fill(poly({{-0.45, 0.05}, {0.4, -0.05}, {0.48, 0}, {0.4, 0.05}, {-0.45, 0.1}}), hx("#ffffff"));
    d.fill(poly({{0.05, 0.0}, {-0.15, 0.42}, {-0.25, 0.42}, {-0.1, 0.02}}), hx("#dbe4f5"));
    d.fill(poly({{0.05, 0.0}, {-0.15, -0.38}, {-0.25, -0.38}, {-0.1, -0.02}}), hx("#ffffff"));
    d.fill(poly({{-0.38, 0.05}, {-0.45, -0.18}, {-0.5, -0.18}, {-0.46, 0.08}}), hx("#2e7bff"));
}

void suitcase(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.42) * Affine::rotate(0.05 * wave(t, 1.6)) * Affine::translate(0, -0.42));
    d.stroke(rrect(-0.1, -0.42, 0.2, 0.14, 0.05), 0.04, hx("#5b3a1a"));
    const Path b = rrect(-0.34, -0.3, 0.68, 0.64, 0.08);
    shadow(d, b);
    d.lin(b, {-0.34, 0}, {0.34, 0}, hx("#ff9a4a"), hx("#e0601a"));
    for (double x : {-0.18, 0.18}) d.fill(rrect(x - 0.03, -0.3, 0.06, 0.64, 0.01), hx("#5b3a1a", 0.6f));
    d.fill(circ(-0.22, 0.38, 0.05), hx("#2a2d35"));
    d.fill(circ(0.22, 0.38, 0.05), hx("#2a2d35"));
    d.fill(starP(0.0, -0.05, 5, 0.08, 0.035), hx("#ffffff", 0.8f));
}

void palm(const D& d0) {
    const double t = d0.t;
    d0.fill(ell(0, 0.42, 0.3, 0.06), hx("#ffd98a"));
    Path trunk;
    trunk.moveTo(-0.04, 0.42).cubicTo(0.0, 0.1, 0.06, -0.1, 0.02, -0.2).lineTo(0.08, -0.2);
    trunk.cubicTo(0.12, -0.1, 0.06, 0.1, 0.06, 0.42).close();
    d0.fill(trunk, hx("#9a5a2a"));
    const D top = d0.at(Affine::translate(0.05, -0.2) * Affine::rotate(0.08 * wave(t, 2.4)));
    for (int i = 0; i < 6; ++i) {
        const double a = -kPi / 2 + (i - 2.5) * 0.55;
        const D f = top.at(Affine::rotate(a + kPi / 2 + 0.05 * wave(t, 1.2, i * 0.2)));
        Path leafP;
        leafP.moveTo(0, 0).cubicTo(0.08, -0.1, 0.06, -0.3, 0.0, -0.36).cubicTo(-0.02, -0.25, -0.05, -0.1, 0, 0).close();
        f.fill(leafP, hx(i % 2 ? "#3fae4f" : "#2f8a3a"));
    }
    d0.fill(circ(0.02, -0.17, 0.04), hx("#6b3a10"));
    d0.fill(circ(0.09, -0.16, 0.04), hx("#6b3a10"));
}

void cameraPhoto(const D& d0) {
    const double t = d0.t;
    const double flash = std::exp(-std::pow((frac(t / 2.5) - 0.2) / 0.04, 2));
    const Path body = rrect(-0.42, -0.2, 0.84, 0.52, 0.08);
    shadow(d0, body);
    d0.fill(rrect(-0.16, -0.3, 0.3, 0.14, 0.04), hx("#3a3f4b"));
    d0.lin(body, {0, -0.2}, {0, 0.32}, hx("#5b6372"), hx("#2a2d35"));
    d0.fill(circ(0, 0.06, 0.2), hx("#1c1f26"));
    d0.rad(circ(0, 0.06, 0.15), {-0.04, 0.02}, 0.18, hx("#6fb2ff"), hx("#123060"));
    d0.fill(circ(-0.05, 0.01, 0.04), hx("#ffffff", 0.7f));
    d0.fill(rrect(0.24, -0.14, 0.1, 0.06, 0.02), hx("#ffe14d", static_cast<float>(0.4 + 0.6 * flash)));
    if (flash > 0.02) d0.fill(starP(0.29, -0.11, 8, 0.3 * flash, 0.06 * flash), hx("#ffffff", static_cast<float>(flash)));
}

void phoneRing(const D& d0) {
    const double t = d0.t;
    const double buzz = std::pow(std::max(0.0, std::sin(kPi * frac(t / 2.0))), 4);
    const D d = d0.at(Affine::rotate(0.08 * buzz * std::sin(t * 60)));
    const Path p = rrect(-0.2, -0.4, 0.4, 0.8, 0.07);
    shadow(d, p);
    d.fill(p, hx("#2a2d35"));
    d.lin(rrect(-0.17, -0.33, 0.34, 0.64, 0.04), {0, -0.33}, {0, 0.31}, hx("#6fb2ff"), hx("#b36bff"));
    d.fill(rrect(-0.05, -0.37, 0.1, 0.02, 0.01), hx("#5b6372"));
    d.fill(heartP(0, -0.02, 0.18), hx("#ffffff", 0.9f));
    for (double s : {-1.0, 1.0})
        d0.stroke(arc(0, 0, 0.3, 0.3, s > 0 ? -0.5 : kPi - 0.5, s > 0 ? 0.5 : kPi + 0.5, 12), 0.025,
                  hx("#ffd54a", static_cast<float>(buzz)));
}

void shoppingBag(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, -0.38) * Affine::rotate(0.1 * wave(t, 2.0)) * Affine::translate(0, 0.38));
    d.stroke(arc(0, -0.2, 0.13, 0.17, kPi, kTau), 0.04, hx("#9a1a4a"));
    const Path b = poly({{-0.32, -0.2}, {0.32, -0.2}, {0.36, 0.42}, {-0.36, 0.42}});
    shadow(d, b);
    d.lin(b, {0, -0.2}, {0, 0.42}, hx("#ff6fa8"), hx("#d6005a"));
    label(d, "SALE", 0, 0.12, 0.17, 0.5, hx("#ffffff"), "bungee");
}

void house(const D& d0) {
    const double t = d0.t;
    d0.fill(poly({{-0.46, -0.02}, {0, -0.42}, {0.46, -0.02}}), hx("#e0501a"));
    const Path wall = rrect(-0.34, -0.06, 0.68, 0.46, 0.02);
    shadow(d0, wall);
    d0.lin(wall, {0, -0.06}, {0, 0.4}, hx("#fff3d6"), hx("#f2d9a8"));
    d0.fill(rrect(-0.08, 0.12, 0.16, 0.28, 0.03), hx("#8a4a1a"));
    const double lit = 0.6 + 0.4 * std::sin(kTau * t / 2.2);
    for (double x : {-0.24, 0.16}) d0.fill(rrect(x, 0.02, 0.1, 0.1, 0.01), hx("#ffd54a", static_cast<float>(lit)));
    d0.fill(rrect(0.2, -0.36, 0.08, 0.16, 0.01), hx("#8a3a1a"));
    for (int i = 0; i < 3; ++i) {
        const double ph = frac(t / 2.4 + i / 3.0);
        d0.fill(circ(0.24 + 0.06 * std::sin(ph * 5), -0.4 - 0.1 * ph, 0.03 + 0.03 * ph), hx("#ffffff", static_cast<float>(0.6 * std::sin(kPi * ph))));
    }
}

void car(const D& d0) {
    const double t = d0.t;
    const double bob = 0.01 * wave(t, 0.4);
    const D d = d0.at(Affine::translate(0, bob));
    Path body;
    body.moveTo(-0.44, 0.15).lineTo(-0.44, 0.0).cubicTo(-0.44, -0.06, -0.38, -0.08, -0.3, -0.09).lineTo(-0.18, -0.24);
    body.lineTo(0.16, -0.24).lineTo(0.3, -0.09).cubicTo(0.4, -0.08, 0.46, -0.04, 0.46, 0.04).lineTo(0.46, 0.15).close();
    shadow(d, body);
    d.lin(body, {0, -0.24}, {0, 0.15}, hx("#4dc3ff"), hx("#1f7fe0"));
    d.fill(poly({{-0.15, -0.2}, {-0.02, -0.2}, {-0.02, -0.09}, {-0.24, -0.09}}), hx("#dff4ff"));
    d.fill(poly({{0.02, -0.2}, {0.14, -0.2}, {0.25, -0.09}, {0.02, -0.09}}), hx("#dff4ff"));
    for (double x : {-0.25, 0.27}) {
        const D w = d0.at(Affine::translate(x, 0.16) * Affine::rotate(t * 8));
        w.fill(circ(0, 0, 0.1), hx("#2a2d35"));
        w.fill(circ(0, 0, 0.045), hx("#c9ccd4"));
        w.fill(rrect(-0.008, -0.045, 0.016, 0.09, 0.004), hx("#5b6372"));
    }
    for (int i = 0; i < 3; ++i) {
        const double ph = frac(t / 0.8 + i / 3.0);
        d0.stroke(line({-0.48 - 0.1 * ph, -0.1 + 0.08 * i}, {-0.48 - 0.1 * ph + 0.08, -0.1 + 0.08 * i}), 0.015,
                  hx("#ffffff", static_cast<float>(0.6 * std::sin(kPi * ph))));
    }
}

}  // namespace

void addMoreStickers(const Add& add) {
    const char* B = "Nhãn khuyến mãi";
    add("seal_new", "MỚI", B, [](const D& d) { seal(d, "MỚI", "#ff7a7a", "#e0001b"); });
    add("seal_hot", "HOT", B, [](const D& d) { seal(d, "HOT", "#ffb347", "#ff3b1a"); });
    add("seal_sale", "SALE", B, [](const D& d) { seal(d, "SALE", "#ff6fa8", "#d6005a"); });
    add("seal_50", "Giảm 50%", B, [](const D& d) { seal(d, "-50%", "#fff27a", "#ffae1a", "#7a1a00"); });
    add("seal_free", "Miễn phí", B, [](const D& d) { seal(d, "FREE", "#7dff9a", "#16b34a"); });
    add("seal_top1", "TOP 1", B, [](const D& d) { seal(d, "TOP 1", "#fff3a8", "#e8a000", "#7a3d00"); });
    add("seal_best", "Bán chạy", B, [](const D& d) { seal(d, "BÁN CHẠY", "#7fd6ff", "#1f7fe0"); });
    add("seal_100", "100%", B, [](const D& d) { seal(d, "100%", "#c9a2ff", "#7c3aed"); });
    add("ribbon_offer", "Ưu đãi", B, [](const D& d) { ribbon(d, "ƯU ĐÃI", "#ff5a6e", "#c8001b"); });
    add("ribbon_promo", "Khuyến mãi", B, [](const D& d) { ribbon(d, "KHUYẾN MÃI", "#ffb347", "#e0501a"); });
    add("ribbon_congrats", "Chúc mừng", B, [](const D& d) { ribbon(d, "CHÚC MỪNG", "#4dc3ff", "#1f5fe0"); });
    add("tag_sale", "Thẻ giảm giá", B, [](const D& d) { priceTag(d, "GIẢM GIÁ", "#ff5a6e", "#c8001b"); });
    add("tag_30", "Thẻ -30%", B, [](const D& d) { priceTag(d, "-30%", "#ffb347", "#e0501a"); });
    add("cta_buy", "Mua ngay", B, [](const D& d) { ctaButton(d, "MUA NGAY", "#ff7a3d", "#e0301a"); });
    add("cta_watch", "Xem ngay", B, [](const D& d) { ctaButton(d, "XEM NGAY", "#4d9bff", "#1a5fe0"); });
    add("cta_order", "Đặt hàng", B, [](const D& d) { ctaButton(d, "ĐẶT HÀNG", "#3fd08a", "#0f9a5a"); });

    const char* C = "Chữ hiệu ứng";
    add("word_wow", "WOW", C, [](const D& d) { comicWord(d, "WOW", "#fff27a", "#ffae1a", "#ff3b6b"); });
    add("word_omg", "OMG", C, [](const D& d) { comicWord(d, "OMG", "#ffb0d0", "#ff4f9a", "#ffffff", 0.1); });
    add("word_boom", "BOOM", C, [](const D& d) { comicWord(d, "BOOM", "#ffb347", "#ff3b1a", "#fff27a"); });
    add("word_pow", "POW", C, [](const D& d) { comicWord(d, "POW", "#7fd6ff", "#1f7fe0", "#fff27a", 0.08); });
    add("word_lol", "LOL", C, [](const D& d) { comicWord(d, "LOL", "#b5f07a", "#3fae4f", "#ffffff"); });
    add("word_haha", "HAHA", C, [](const D& d) { comicWord(d, "HAHA", "#fff27a", "#ffc21a", "#ff6a1a", 0.06); });
    add("word_yes", "YES!", C, [](const D& d) { comicWord(d, "YES!", "#7dff9a", "#16b34a", "#ffffff"); });
    add("word_oi", "ÔI!", C, [](const D& d) { comicWord(d, "ÔI!", "#c9a2ff", "#7c3aed", "#fff27a", 0.1); });
    add("word_great", "TUYỆT!", C, [](const D& d) { comicWord(d, "TUYỆT!", "#ff9a9a", "#e0001b", "#ffffff"); });
    add("word_xin", "XỊN!", C, [](const D& d) { comicWord(d, "XỊN!", "#fff3a8", "#e8a000", "#e0001b", 0.08); });
    add("say_hello", "Xin chào", C, [](const D& d) { sayBubble(d, "Xin chào!", "#ffffff", "#1c1c22"); });
    add("say_thanks", "Cảm ơn", C, [](const D& d) { sayBubble(d, "Cảm ơn!", "#ffe14d", "#1c1c22"); });
    add("say_love", "Yêu bạn", C, [](const D& d) { sayBubble(d, "Yêu bạn", "#ffb0d0", "#c8004f");
        d.at(Affine::translate(0.36, -0.24) * Affine::rotate(0.3 + 0.1 * wave(d.t, 1.0))).fill(heartP(0, 0, 0.2), hx("#ff2d55")); });
    add("say_wait", "Chờ chút", C, [](const D& d) { sayBubble(d, "Chờ chút…", "#c9f1ff", "#1c1c22"); });

    const char* H = "Vẽ tay";
    add("doodle_underline2", "Gạch chân đôi", H, doodleDoubleUnderline);
    add("doodle_zigzag", "Răng cưa", H, doodleZigzag);
    add("doodle_wavy", "Lượn sóng", H, doodleWavy);
    add("doodle_arrow", "Mũi tên vẽ", H, doodleLoopArrow);
    add("doodle_emphasis", "Nhấn mạnh", H, doodleEmphasis);
    add("doodle_star", "Ngôi sao vẽ", H, doodleStar);
    add("doodle_heart", "Trái tim vẽ", H, doodleHeart);
    add("doodle_crown", "Vương miện vẽ", H, doodleCrown);
    add("doodle_spiral", "Xoắn ốc", H, doodleSpiral);
    add("doodle_x", "Gạch chéo", H, doodleX);
    add("doodle_highlight", "Bút dạ quang", H, doodleHighlight);
    add("doodle_brackets", "Ngoặc vuông", H, doodleBrackets);
    add("doodle_bang", "Chấm than đôi", H, doodleBang);
    add("doodle_sparkle", "Lấp lánh vẽ", H, doodleSparkle);

    const char* V = "Video & kênh";
    add("countdown", "Đếm ngược 3-2-1", V, countdown);
    add("clapper", "Bảng clap", V, clapper);
    add("rec", "Đang quay REC", V, recBadge);
    add("mic", "Micro", V, mic);
    add("part_1", "Phần 1", V, [](const D& d) { partTag(d, "PHẦN 1", "#4d9bff", "#1a5fe0"); });
    add("part_2", "Phần 2", V, [](const D& d) { partTag(d, "PHẦN 2", "#3fd08a", "#0f9a5a"); });
    add("part_3", "Phần 3", V, [](const D& d) { partTag(d, "PHẦN 3", "#ff7a3d", "#e0301a"); });
    add("to_be_continued", "Còn tiếp", V, [](const D& d) { partTag(d, "CÒN TIẾP…", "#8a8f99", "#3a3f4b"); });
    add("the_end", "Hết", V, [](const D& d) { endCard(d, "Hết"); });
    add("watch_end", "Xem đến cuối", V, [](const D& d) { sayBubble(d, "Xem hết nhé!", "#ffe14d", "#1c1c22"); });

    const char* S = "Thể thao & game";
    add("football", "Bóng đá", S, football);
    add("basketball", "Bóng rổ", S, basketball);
    add("gamepad", "Tay cầm game", S, gamepad);
    add("coin", "Đồng xu", S, coin);
    add("pixel_heart", "Tim pixel", S, pixelHeart);
    add("level_up", "LEVEL UP", S, levelUp);
    add("game_over", "GAME OVER", S, [](const D& d) { wordBanner(d, "GAME", "OVER", "#ff7a7a", "#c8001b", "bungee", -0.08); });
    add("victory", "Chiến thắng", S, [](const D& d) { wordBanner(d, "CHIẾN", "THẮNG", "#fff3a8", "#e8a000", "be-vietnam-pro-extrabold"); });
    add("goal", "VÀO!", S, [](const D& d) { comicWord(d, "VÀO!", "#7dff9a", "#16b34a", "#ffffff"); });

    const char* T = "Tết & mùa lễ";
    add("banh_chung", "Bánh chưng", T, banhChung);
    add("firecrackers", "Pháo đỏ", T, firecrackers);
    add("tet_xuan", "Chữ Xuân", T, [](const D& d) { tetDiamond(d, "Xuân"); });
    add("tet_loc", "Chữ Lộc", T, [](const D& d) { tetDiamond(d, "Lộc"); });
    add("tet_banner", "Chúc Mừng Năm Mới", T, [](const D& d) { tetBanner(d, "Chúc Mừng Năm Mới"); });
    add("year_2027", "2027", T, [](const D& d) { yearGold(d, "2027"); });
    add("star_lantern", "Đèn ông sao", T, starLantern);
    add("mooncake", "Bánh trung thu", T, mooncake);
    add("pumpkin", "Bí ngô", T, pumpkin);
    add("ghost", "Bóng ma", T, ghost);
    add("santa_hat", "Mũ Noel", T, santaHat);
    add("snowman", "Người tuyết", T, snowman);

    const char* L = "Du lịch & đời sống";
    add("plane", "Máy bay", L, plane);
    add("suitcase", "Vali", L, suitcase);
    add("palm", "Cây dừa", L, palm);
    add("camera", "Máy ảnh", L, cameraPhoto);
    add("phone", "Điện thoại", L, phoneRing);
    add("shopping_bag", "Túi mua sắm", L, shoppingBag);
    add("house", "Ngôi nhà", L, house);
    add("car", "Ô tô", L, car);
}

}  // namespace vstudio::stickers::kit
