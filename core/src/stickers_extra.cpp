// More sticker groups: 8-bit game pixel art, the 80s, animal reactions, drinks and daily life,
// birthdays, Christmas and the year's celebrations. All drawn in-house; every motion is a smooth
// loop (or plays once and holds), and each sticker stays inside its unit box.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "sticker_kit.hpp"

namespace vstudio::stickers::kit {

namespace {

double ease(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

// ---------------------------------------------------------------- pixel art

// A sprite: one char per pixel ('.' empty), coloured by `pal`. Centred, `cell` = one pixel's size.
// The whole silhouette is laid down in the outline colour first, so colours meet without seams.
void pixels(const D& d, const std::vector<const char*>& rows, const std::map<char, const char*>& pal, double cell,
            float alpha = 1) {
    const int h = static_cast<int>(rows.size());
    int w = 0;
    for (const char* r : rows) w = std::max(w, static_cast<int>(std::strlen(r)));
    const double x0 = -w * cell / 2, y0 = -h * cell / 2;
    std::map<char, Path> paths;
    Path all;
    for (int y = 0; y < h; ++y) {
        const int n = static_cast<int>(std::strlen(rows[static_cast<std::size_t>(y)]));
        for (int x = 0; x < n; ++x) {
            const char c = rows[static_cast<std::size_t>(y)][x];
            if (c == '.' || c == ' ') continue;
            paths[c].rect(x0 + x * cell, y0 + y * cell, cell, cell);
            all.rect(x0 + x * cell, y0 + y * cell, cell, cell);
        }
    }
    const auto k = pal.find('k');
    d.fill(all, hx(k != pal.end() ? k->second : "#1c1c22", alpha));
    for (const auto& [c, p] : paths) {
        const auto it = pal.find(c);
        if (it != pal.end() && c != 'k') d.fill(p, hx(it->second, alpha));
    }
}

// 5x7 pixel font.
const std::array<std::uint8_t, 7>* glyph5x7(char ch) {
    static const std::map<char, std::array<std::uint8_t, 7>> font = {
        {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
        {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
        {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
        {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
        {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
        {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}}, {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
        {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}}, {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
        {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
        {' ', {0, 0, 0, 0, 0, 0, 0}},
    };
    const auto it = font.find(ch);
    return it == font.end() ? nullptr : &it->second;
}

Path pixelTextPath(const std::string& s, double cx, double cy, double cell) {
    const double w = (static_cast<double>(s.size()) * 6 - 1) * cell;
    const double x0 = cx - w / 2, y0 = cy - 3.5 * cell;
    Path p;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto* g = glyph5x7(s[i]);
        if (!g) continue;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c)
                if ((*g)[static_cast<std::size_t>(r)] & (0x10 >> c)) p.rect(x0 + (i * 6 + c) * cell, y0 + r * cell, cell, cell);
    }
    return p;
}

// Pixel words with a hard drop shadow, `cell` shrunk so the line fits `maxW`.
void pixelText(const D& d, const std::string& s, double cx, double cy, double cell, double maxW, Color fill,
               Color shadow = hx("#1c1c22")) {
    cell = std::min(cell, maxW / (static_cast<double>(s.size()) * 6 - 1));
    d.fill(pixelTextPath(s, cx + cell * 0.8, cy + cell * 0.8, cell), shadow);
    d.fill(pixelTextPath(s, cx, cy, cell), fill);
}

// Pixel panel: a dark box with a stepped (pixel) border.
void pixelPanel(const D& d, double w, double h, double cell, const char* fill, const char* border) {
    auto stepped = [&](double inset) {
        const double x0 = -w / 2 + inset, y0 = -h / 2 + inset, x1 = w / 2 - inset, y1 = h / 2 - inset, c = cell;
        return poly({{x0 + c, y0}, {x1 - c, y0}, {x1 - c, y0 + c}, {x1, y0 + c}, {x1, y1 - c}, {x1 - c, y1 - c},
                     {x1 - c, y1}, {x0 + c, y1}, {x0 + c, y1 - c}, {x0, y1 - c}, {x0, y0 + c}, {x0 + c, y0 + c}});
    };
    d.fill(stepped(0), hx(border));
    d.fill(stepped(cell), hx(fill));
}

void pxCoin(const D& d0) {
    const double t = d0.t;
    const double spin = std::cos(t * 2.4);
    const D d = d0.at(Affine::translate(0, 0.03 * wave(t, 1.6)) * Affine::scale(0.25 + 0.75 * std::abs(spin), 1));
    pixels(d,
           {"....kkkk....", "..kkyyyykk..", ".kyyYYYYyyk.", ".kyYyyyyoyk.", "kyYyyooyyoyk", "kyYyyoyyyoyk",
            "kyYyyoyyyoyk", "kyYyyoyyyoyk", "kyYyyooyyoyk", ".kyyyyyyoyk.", ".kyoooooyyk.", "..kkyyyykk..",
            "....kkkk...."},
           {{'y', "#ffd23f"}, {'Y', "#fff5b8"}, {'o', "#e08a00"}}, 0.065);
    if (spin > 0.85) {  // a glint as it faces us
        const double g = ease((spin - 0.85) / 0.15);
        d0.fill(starP(0.22, -0.26, 4, 0.1 * g, 0.025), hx("#ffffff", static_cast<float>(g)));
    }
}

void pxStar(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.03 * wave(t, 1.2)) * Affine::scale(1 + 0.04 * wave(t, 0.6)));
    pixels(d,
           {"......kk......", "......kk......", ".....kyyk.....", ".....kyyk.....", "kkkkkyYyykkkkk", "kyyyyyYyyyyyyk",
            ".kyyykyykyyyk.", "..kyyyyyyyyk..", "...kyykkyyk...", "...kyyyyyyk...", "..kyyyyyyyyk..", "..kyyykkyyyk..",
            ".kyyk....kyyk.", ".kkk......kkk."},
           {{'y', "#ffd23f"}, {'Y', "#fff5b8"}}, 0.064);
    const float a = static_cast<float>(0.5 + 0.5 * wave(t, 1.4));
    for (int i = 0; i < 3; ++i) {
        const double ang = i * 2.1 + t * 0.6;
        d0.fill(rrect(0.42 * std::cos(ang) - 0.025, 0.42 * std::sin(ang) - 0.025, 0.05, 0.05, 0), hx("#fff5b8", a));
    }
}

void pxGem(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.035 * wave(t, 1.8)));
    pixels(d,
           {"...kkkkkkkk...", "..kcCCccCCck..", ".kcCCccccCCck.", "kccccccccccccck", "kbcccccccccccbk", ".kbccccccccbk.",
            "..kbccccccbk..", "...kbccccbk...", "....kbccbk....", ".....kbbk.....", "......kk......"},
           {{'c', "#4dd8ff"}, {'C', "#c9f6ff"}, {'b', "#1f8fd0"}}, 0.065);
    const double g = std::pow(0.5 + 0.5 * wave(t, 2.0), 8);
    d0.fill(starP(-0.18, -0.22, 4, 0.12 * g + 0.01, 0.025), hx("#ffffff", static_cast<float>(g)));
}

void pxKey(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.12 * wave(t, 2.4)));
    pixels(d,
           {".kkkk..........", "kyyyyk.........", "kyk.kykkkkkkkkk", "kyk.kyyyyyyyyYk", "kyyyykkkkkkykyk",
            ".kkkk.....kykyk", "..........kkkkk"},
           {{'y', "#ffd23f"}, {'Y', "#fff5b8"}}, 0.06);
}

void pxChest(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.06));
    pixels(d,
           {"..kkkkkkkkkkkk..", ".kbbbbbbbbbbbbk.", "kbbbbbbbbbbbbbbk", "kyyyyyykkyyyyyyk", "kbbbbbkyykbbbbbk",
            "kkkkkkkyykkkkkkk", "kbbbbbbkkbbbbbbk", "kbbbbbbbbbbbbbbk", "kyyyyyyyyyyyyyyk", "kbbbbbbbbbbbbbbk",
            "kBBBBBBBBBBBBBBk", "kkkkkkkkkkkkkkkk"},
           {{'b', "#b8743a"}, {'B', "#8a5226"}, {'y', "#ffd23f"}}, 0.058);
    for (int i = 0; i < 3; ++i) {  // treasure glints above the lid
        const double ph = frac(t / 1.8 + i / 3.0);
        const double a = std::sin(kPi * ph);
        d0.fill(starP(-0.2 + 0.2 * i, -0.28 - 0.12 * ph, 4, 0.06 * a + 0.005, 0.015), hx("#fff5b8", static_cast<float>(a)));
    }
}

void pxSword(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.02, 0.02 * wave(t, 1.6)) * Affine::rotate(0.08 * wave(t, 3.0)));
    pixels(d,
           {"....kk.....", "...kwgk....", "...kwgk....", "...kwgk....", "...kwgk....", "...kwgk....", "...kwgk....",
            "...kwgk....", "...kwgk....", "kkkkwgkkkk.", "kyyyyyyyyk.", "kkkkbbkkkk.", "...kbbk....", "...kbbk....",
            "...kyyk....", "...kkkk...."},
           {{'w', "#ffffff"}, {'g', "#9aa3b5"}, {'y', "#ffd23f"}, {'b', "#8a5226"}}, 0.057);
    const double ph = frac(t / 2.2);  // a shine runs up the blade
    if (ph < 0.5) {
        const double y = 0.3 - 1.2 * ph * 1.0, a = std::sin(kPi * ph * 2);
        d.fill(rrect(-0.06, y - 0.02, 0.06, 0.04, 0), hx("#ffffff", static_cast<float>(0.8 * a)));
    }
}

void pxPotion(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.025 * wave(t, 1.5)) * Affine::rotate(0.06 * wave(t, 2.2)));
    pixels(d,
           {"....kkkk....", "...kbbbbk...", "....kkkk....", "....kwwk....", "....kwwk....", "...kwwwwk...",
            "..kwwwwwwk..", ".kwppppppwk.", "kwpPpppppppk", "kwpPpppppppk", "kppppppppppk", "kpppppppppPk",
            ".kppppppppk.", "..kkkkkkkk.."},
           {{'b', "#8a5226"}, {'w', "#dff6ff"}, {'p', "#e040a0"}, {'P', "#ff9ad8"}}, 0.065);
    for (int i = 0; i < 3; ++i) {  // bubbles rise and fade inside
        const double ph = frac(t / 1.4 + i / 3.0);
        const double a = std::sin(kPi * ph);
        d.fill(circ(-0.12 + 0.12 * i, 0.28 - 0.22 * ph, 0.022), hx("#ffd6f2", static_cast<float>(0.9 * a)));
    }
}

void pxRocket(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.01 * wave(t, 0.2), -0.06 + 0.02 * wave(t, 1.3)));
    const double flick = 0.8 + 0.2 * std::sin(t * 18) * std::sin(t * 7);
    d.at(Affine::translate(0, 0.33) * Affine::scale(1, flick) * Affine::translate(0, 0.08))
        .fill(poly({{-0.1, -0.08}, {0.1, -0.08}, {0.05, 0.05}, {0, 0.14}, {-0.05, 0.05}}), hx("#ff8a1f"));
    d.at(Affine::translate(0, 0.33) * Affine::scale(1, flick) * Affine::translate(0, 0.06))
        .fill(poly({{-0.05, -0.06}, {0.05, -0.06}, {0, 0.07}}), hx("#fff27a"));
    pixels(d,
           {".....kk.....", "....krrk....", "...krrrrk...", "...kwwwwk...", "...kwccwk...", "...kwccwk...",
            "...kwwwwk...", "...kwwwwk...", "..krwwwwrk..", ".krrwwwwrrk.", "krrkkkkkkrrk", "kkk.kook.kkk"},
           {{'r', "#ff4d6d"}, {'w', "#f2f4f8"}, {'c', "#4dd8ff"}, {'o', "#ff8a1f"}}, 0.06);
}

void pxCat(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02 * wave(t, 2.0)) * Affine::rotate(0.05 * wave(t, 3.0)));
    constexpr double c = 0.072;
    pixels(d,
           {"kk........kk", "kpk......kpk", "kopkkkkkkpok", "kooooooooook", "kooooooooook", "kooooppooook",
            "kooookkooook", "kooooooooook", ".kooooooook.", "..kkkkkkkk.."},
           {{'o', "#ffb35a"}, {'p', "#ff8fab"}}, c);
    // Eyes blink (rows of 10 cells: centred grid is 12 x 10).
    const double b = blink(t, 3.2, 1.4);
    for (double ex : {-2.5, 2.5}) d.at(Affine::translate(ex * c, -0.5 * c) * Affine::scale(1, b)).fill(rrect(-c / 2, -c / 2, c, c, 0), hx("#1c1c22"));
    for (double sx : {-1.0, 1.0}) {  // whiskers twitch
        const double w = 0.02 * wave(t, 1.1);
        d.stroke(line({sx * 0.45, 0.05 + w}, {sx * 0.52, 0.03 + w}), 0.02, hx("#1c1c22"));
        d.stroke(line({sx * 0.45, 0.11 - w}, {sx * 0.52, 0.13 - w}), 0.02, hx("#1c1c22"));
    }
}

void pxBomb(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.05) * Affine::scale(1 + 0.03 * wave(t, 0.5)));
    pixels(d,
           {"........kk..", ".......kbk..", "....kkkkk...", "..kkddddkk..", ".kddWddddddk.", "kddWdddddddk",
            "kddddddddddk", "kddddddddddk", "kddddddddddk", ".kddddddddk.", "..kkddddkk..", "....kkkk...."},
           {{'d', "#3a3f4b"}, {'W', "#9aa3b5"}, {'b', "#c89a5a"}}, 0.065);
    for (int i = 0; i < 4; ++i) {  // sparks off the fuse
        const double ph = frac(t / 0.7 + i / 4.0), a = std::sin(kPi * ph);
        const double ang = -1.2 + i * 0.7;
        d0.fill(rrect(0.2 + 0.12 * ph * std::cos(ang) - 0.02, -0.38 + 0.12 * ph * std::sin(ang) - 0.02, 0.04, 0.04, 0),
                hx(i % 2 ? "#fff27a" : "#ff8a1f", static_cast<float>(a)));
    }
}

void pxUfo(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0.05 * wave(t, 3.0), -0.08 + 0.03 * wave(t, 1.5)) * Affine::rotate(0.06 * wave(t, 3.0, 0.25)));
    const float beam = static_cast<float>(0.25 + 0.15 * wave(t, 1.2));
    d.lin(poly({{-0.1, 0.12}, {0.1, 0.12}, {0.26, 0.46}, {-0.26, 0.46}}), {0, 0.12}, {0, 0.46}, hx("#c9ff7a", beam), hx("#c9ff7a", 0));
    pixels(d,
           {"....kkkk....", "...kcCcck...", "..kccccccck..", "kkkkkkkkkkkk", "kgglgglgglgk", ".kgggggggggk.", "..kkkkkkkk.."},
           {{'c', "#7dffb0"}, {'C', "#e0fff0"}, {'g', "#9aa3b5"}, {'l', "#ffd23f"}}, 0.07);
}

void pxJoystick(const D& d0) {
    const double t = d0.t;
    const double tilt = 0.22 * wave(t, 1.6);
    const D d = d0.at(Affine::translate(0, 0.02));
    const D top = d.at(Affine::translate(0, 0.14) * Affine::rotate(tilt) * Affine::translate(0, -0.14));
    pixels(top.at(Affine::translate(0, -0.12)), {"..kkkk..", ".krrrrk.", ".krRrrk.", ".krrrrk.", "..kkkk..", "...kk...", "...kk...", "...kk..."},
           {{'r', "#ff4d6d"}, {'R', "#ffb0bd"}}, 0.06);
    pixels(d.at(Affine::translate(0, 0.27)), {".kkkkkkkkkk.", "kggggggggggk", "kgykgggrrggk", "kggggggggggk", "kkkkkkkkkkkk"},
           {{'g', "#4a5060"}, {'y', "#ffd23f"}, {'r', "#ff4d6d"}}, 0.07);
}

void pxBadge(const D& d0, const char* text, const char* color, int mode) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.01 * wave(t, 2.0)));
    const double cell = 0.016;
    pixelPanel(d, 0.96, 0.36, 0.024, "#14141c", "#f2f4f8");
    float a = 1;
    double s = 1;
    if (mode == 1) a = static_cast<float>(0.35 + 0.65 * (0.5 + 0.5 * wave(t, 1.2)));  // blinking prompt
    if (mode == 2) s = 1 + 0.05 * wave(t, 0.9);
    pixelText(d.at(Affine::translate(mode == 1 ? 0.045 : 0, 0) * Affine::scale(s)), text, 0, 0, cell, mode == 1 ? 0.74 : 0.8,
              hx(color, a), hx("#4a5060", a));
    if (mode == 1) {  // a pointer that nudges toward the words
        const double x = -0.42 + 0.015 * (0.5 + 0.5 * wave(t, 0.6));
        d.fill(poly({{x, -0.04}, {x + 0.04, 0}, {x, 0.04}}), hx(color));
    }
}

void pxScore(const D& d0) {
    const double t = d0.t;
    pixelPanel(d0, 0.94, 0.42, 0.02, "#14141c", "#f2f4f8");
    pixelText(d0, "HIGH SCORE", 0, -0.08, 0.015, 0.8, hx("#ff4d6d"));
    const int score = 12000 + static_cast<int>(std::floor(ease(t / 2.5) * 87650 / 50)) * 50;  // counts up, then holds
    char buf[16];
    std::snprintf(buf, sizeof buf, "%06d", score);
    pixelText(d0, buf, 0, 0.08, 0.02, 0.8, hx("#ffd23f"));
}

void pxPlus(const D& d0) {
    const double t = d0.t;
    const double ph = frac(t / 1.6 + 0.2);  // rises and fades, then the next one comes up
    const float a = static_cast<float>(std::min(1.0, 2.5 * std::sin(kPi * ph)));
    pixelText(d0, "+100", 0, 0.2 - 0.36 * ph, 0.035, 0.8, hx("#ffd23f", a), hx("#5a2a00", a));
}

void pxKo(const D& d0) {
    const double t = d0.t;
    const double k = ease(t / 0.4);
    const D d = d0.at(Affine::rotate(-0.08) * Affine::scale(0.6 + 0.4 * k + 0.03 * wave(t, 0.8)));
    d.at(Affine::rotate(0.1 * t)).fill(starP(0, 0, 12, 0.46, 0.32), hx("#1c1c22"));
    d.at(Affine::rotate(0.1 * t)).fill(starP(0, 0, 12, 0.42, 0.29), hx("#ffd23f"));
    d.fill(circ(0, 0, 0.24), hx("#ff8a1f"));
    pixelText(d, "K.O.", 0, 0, 0.042, 0.66, hx("#ffffff"), hx("#1c1c22"));
}

void pxWin(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02 * wave(t, 1.4)));
    pixelText(d, "YOU", 0, -0.13, 0.032, 0.8, hx("#ffd23f"), hx("#5a2a00"));
    pixelText(d, "WIN!", 0, 0.13, 0.036, 0.85, hx("#ffd23f"), hx("#5a2a00"));
    for (int i = 0; i < 4; ++i) {
        const double ph = frac(t / 1.2 + i / 4.0), a = std::sin(kPi * ph);
        const double x = i % 2 ? 0.38 : -0.38, y = i < 2 ? -0.3 : 0.32;
        d.fill(starP(x, y, 4, 0.07 * a + 0.005, 0.018), hx("#fff5b8", static_cast<float>(a)));
    }
}

void pxLives(const D& d0) {
    const double t = d0.t;
    pixelText(d0, "PLAYER 1", 0, -0.14, 0.02, 0.8, hx("#4dd8ff"));
    for (int i = 0; i < 3; ++i) {
        const double beat = 1 + 0.08 * std::max(0.0, wave(t, 0.9, -i * 0.12));
        // The last heart pulses softer: one life left to lose.
        const float a = i == 2 ? static_cast<float>(0.45 + 0.4 * (0.5 + 0.5 * wave(t, 1.3))) : 1.0f;
        pixels(d0.at(Affine::translate(-0.26 + 0.26 * i, 0.12) * Affine::scale(beat)),
               {".kk.kk.", "krrkrrk", "krRrrrk", "krrrrrk", ".krrrk.", "..krk..", "...k..."},
               {{'r', "#ff3b5c"}, {'R', "#ffb0bd"}}, 0.03, a);
    }
}

// ---------------------------------------------------------------- the 80s

void synthSun(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02));
    const double cy = -0.08, r = 0.3;
    // The sun as horizontal bands, the gaps widening toward its foot.
    const double bands[][2] = {{-1, 0.0}, {0.04, 0.1}, {0.13, 0.18}, {0.215, 0.25}, {0.28, 0.3}};
    for (const auto& b : bands) {
        const double y0 = cy + (b[0] < 0 ? -r : b[0]), y1 = cy + std::min(b[1], r);
        std::vector<Pt> pts;
        for (int i = 0; i <= 16; ++i) {
            const double y = y0 + (y1 - y0) * i / 16, dx = std::sqrt(std::max(0.0, r * r - (y - cy) * (y - cy)));
            pts.push_back({dx, y});
        }
        for (int i = 16; i >= 0; --i) {
            const double y = y0 + (y1 - y0) * i / 16, dx = std::sqrt(std::max(0.0, r * r - (y - cy) * (y - cy)));
            pts.push_back({-dx, y});
        }
        d.lin(poly(pts), {0, cy - r}, {0, cy + r}, hx("#ffe14d"), hx("#ff3fa4"));
    }
    // Neon floor: lines rush toward us, verticals fan out from the horizon.
    const double hy = cy + r + 0.005, by = 0.44;
    const Color pink = hx("#ff3fd0"), cyan = hx("#3ff0ff");
    for (int i = -4; i <= 4; ++i) {
        const Pt a{i * 0.03, hy}, b{i * 0.12, by};
        d.stroke(line(a, b), 0.03, cyan.withAlpha(0.18f));
        d.stroke(line(a, b), 0.01, cyan);
    }
    for (int i = 0; i < 5; ++i) {
        const double z = frac(i / 5.0 + t * 0.35);  // 0 at the horizon, 1 at our feet
        const double y = hy + (by - hy) * z * z;
        const double half = 0.12 + 0.36 * z * z;
        const float a = static_cast<float>(std::min(1.0, z * 3));
        d.stroke(line({-half, y}, {half, y}), 0.03, pink.withAlpha(0.18f * a));
        d.stroke(line({-half, y}, {half, y}), 0.01, pink.withAlpha(a));
    }
    d.stroke(line({-0.48, hy}, {0.48, hy}), 0.012, pink);
}

void neon80s(const D& d0) {
    const double t = d0.t;
    const float flick = static_cast<float>(0.82 + 0.18 * (0.5 + 0.5 * std::sin(t * 9) * std::sin(t * 2.3)));
    const Path word = textPath("80s", 0, -0.04, 0.5, 0.86, "lobster");
    d0.stroke(word, 0.07, hx("#ff3fd0", 0.22f * flick));
    d0.stroke(word, 0.035, hx("#ff3fd0", 0.55f * flick));
    d0.stroke(word, 0.014, hx("#ffe0f6", flick));
    Path swoosh;
    swoosh.moveTo(-0.38, 0.26).cubicTo(-0.1, 0.16, 0.15, 0.36, 0.4, 0.22);
    const double draw = 0.5 + 0.5 * wave(t, 2.6);
    d0.stroke(swoosh, 0.06, hx("#3ff0ff", 0.2f));
    d0.stroke(swoosh, 0.018, hx("#c9fbff", static_cast<float>(0.6 + 0.4 * draw)));
}

void retroTv(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.06));
    for (double sx : {-1.0, 1.0})  // rabbit-ear antenna, swaying a little
        d.stroke(line({0, -0.28}, {sx * 0.18 + 0.02 * wave(t, 2.0), -0.48}), 0.02, hx("#4a5060"));
    d.fill(circ(0, -0.28, 0.035), hx("#4a5060"));
    d.lin(rrect(-0.44, -0.28, 0.88, 0.62, 0.08), {0, -0.28}, {0, 0.34}, hx("#c8915a"), hx("#8a5a2b"));
    const Path screen = rrect(-0.38, -0.21, 0.6, 0.48, 0.07);
    d.fill(screen, hx("#14141c"));
    // Colour bars rolling slowly, then scanlines.
    static const char* bars[] = {"#f2f2f2", "#ffe14d", "#3ff0ff", "#4cd964", "#ff3fd0", "#ff3b3b", "#3b5bff"};
    const double bw = 0.56 / 7;
    for (int i = 0; i < 7; ++i) d.fill(rrect(-0.36 + i * bw, -0.19, bw + 0.002, 0.44, 0), hx(bars[i], 0.85f));
    const double roll = frac(t / 2.5);
    d.fill(rrect(-0.36, -0.19 + roll * 0.44 - 0.03, 0.56, 0.06, 0), hx("#ffffff", 0.18f));
    for (int i = 0; i < 11; ++i) d.fill(rrect(-0.36, -0.19 + i * 0.04, 0.56, 0.012, 0), hx("#000000", 0.18f));
    d.stroke(screen, 0.025, hx("#2a2a32"));
    for (int i = 0; i < 2; ++i) {
        const D k = d.at(Affine::translate(0.32, -0.1 + i * 0.16) * Affine::rotate(i == 0 ? t * 0.8 : -0.6));
        k.fill(circ(0, 0, 0.05), hx("#e6e1d6"));
        k.fill(rrect(-0.008, -0.045, 0.016, 0.04, 0.005), hx("#4a5060"));
    }
    for (double sx : {-0.3, 0.3}) d.fill(rrect(sx - 0.04, 0.34, 0.08, 0.06, 0.02), hx("#5a3a1a"));
}

void boombox(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08));
    d.stroke(rrect(-0.28, -0.36, 0.56, 0.2, 0.06), 0.04, hx("#9aa3b5"));
    d.lin(rrect(-0.46, -0.22, 0.92, 0.5, 0.06), {0, -0.22}, {0, 0.28}, hx("#5a6070"), hx("#2a2e38"));
    const double beat = std::max(0.0, wave(t, 0.5));
    for (double sx : {-1.0, 1.0}) {
        const D s = d.at(Affine::translate(sx * 0.27, 0.05) * Affine::scale(1 + 0.08 * beat));
        s.fill(circ(0, 0, 0.15), hx("#14141c"));
        s.rad(circ(0, 0, 0.11), {-0.03, -0.03}, 0.12, hx("#6a7080"), hx("#2a2e38"));
        s.fill(circ(0, 0, 0.035), hx("#9aa3b5"));
    }
    d.fill(rrect(-0.1, -0.08, 0.2, 0.14, 0.02), hx("#14141c"));
    for (double sx : {-0.04, 0.04}) d.fill(circ(sx, -0.01, 0.025), hx("#9aa3b5"));
    for (int i = 0; i < 4; ++i) d.fill(rrect(-0.38 + i * 0.05, -0.18, 0.03, 0.03, 0.005), hx(i == 1 ? "#ff3fd0" : "#ffd23f"));
    for (int i = 0; i < 2; ++i) {  // notes drift up and fade
        const double ph = frac(t / 1.8 + i * 0.5), a = std::sin(kPi * ph);
        const D n = d0.at(Affine::translate((i ? 0.3 : -0.3) + 0.05 * wave(t, 1.0, i * 0.5), -0.2 - 0.22 * ph));
        const Color c = hx(i ? "#3ff0ff" : "#ff3fd0", static_cast<float>(a));
        n.fill(ell(0, 0.04, 0.035, 0.026), c);
        n.fill(rrect(0.022, -0.06, 0.012, 0.1, 0), c);
        n.fill(poly({{0.03, -0.06}, {0.08, -0.03}, {0.08, -0.005}, {0.03, -0.035}}), c);
    }
}

void floppy(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.08 * wave(t, 3.0)) * Affine::scale(0.92));
    d.lin(poly({{-0.42, -0.46}, {0.34, -0.46}, {0.44, -0.36}, {0.44, 0.46}, {-0.42, 0.46}}), {0, -0.46}, {0, 0.46}, hx("#3b6bff"), hx("#1f3fb0"));
    const double slide = 0.04 * (0.5 + 0.5 * wave(t, 2.4));
    d.fill(rrect(-0.24 + slide, -0.46, 0.4, 0.3, 0.015), hx("#d9dde6"));
    d.fill(rrect(0.06 + slide, -0.41, 0.07, 0.2, 0.01), hx("#3b6bff"));
    d.fill(rrect(-0.32, -0.02, 0.64, 0.44, 0.02), hx("#f8f8fb"));
    for (int i = 0; i < 3; ++i) d.fill(rrect(-0.27, 0.06 + i * 0.1, 0.54, 0.012, 0), hx("#c9cfdb"));
    label(d, "MIX 1987", 0, 0.1, 0.09, 0.5, hx("#ff3fd0"), "patrick-hand");
}

void cassette(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.06 + 0.05 * wave(t, 3.0)));
    d.lin(rrect(-0.46, -0.3, 0.92, 0.6, 0.05), {0, -0.3}, {0, 0.3}, hx("#3a3f4b"), hx("#1c1f26"));
    d.lin(rrect(-0.38, -0.24, 0.76, 0.32, 0.03), {-0.38, 0}, {0.38, 0}, hx("#ff3fd0"), hx("#ffb347"));
    d.fill(rrect(-0.24, -0.1, 0.48, 0.14, 0.06), hx("#14141c"));
    label(d, "MIXTAPE", 0, -0.17, 0.08, 0.5, hx("#ffffff"), "bungee");
    for (double sx : {-0.13, 0.13}) {
        const D r = d.at(Affine::translate(sx, -0.03) * Affine::rotate(t * 2.5));
        r.fill(circ(0, 0, 0.05), hx("#f2f2f2"));
        for (int i = 0; i < 6; ++i) r.at(Affine::rotate(i * kPi / 3)).fill(rrect(-0.008, -0.045, 0.016, 0.02, 0), hx("#14141c"));
    }
    d.fill(poly({{-0.26, 0.3}, {0.26, 0.3}, {0.2, 0.16}, {-0.2, 0.16}}), hx("#4a5060"));
    for (double sx : {-0.12, 0.12}) d.fill(circ(sx, 0.23, 0.025), hx("#14141c"));
}

void discoBall(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.06));
    d.stroke(line({0, -0.5}, {0, -0.32}), 0.015, hx("#9aa3b5"));
    d.fill(circ(0, 0, 0.32), hx("#6a7080"));
    // Facets: rows of tiles scrolling sideways as the ball turns.
    for (int r = -4; r <= 4; ++r) {
        const double lat = r * 0.32 / 4.6, rw = std::sqrt(std::max(0.0, 0.32 * 0.32 - lat * lat));
        const int n = 8;
        for (int i = 0; i < n; ++i) {
            const double u = frac(i / static_cast<double>(n) + t * 0.12) * kPi - kPi / 2;  // longitude on the front half
            const double x = rw * std::sin(u), w = rw * kPi / n * std::cos(u) * 0.85;
            if (w < 0.004) continue;
            const double shade = 0.5 + 0.5 * std::cos(u - 0.6) * (1 - std::abs(lat) * 1.5);
            d.fill(rrect(x - w / 2, lat - 0.026, w, 0.052, 0.004), mix(hx("#4a5060"), hx("#f2f4ff"), static_cast<float>(std::clamp(shade, 0.0, 1.0))));
        }
    }
    for (int i = 0; i < 4; ++i) {  // sparkles
        const double ph = frac(t / 1.3 + i / 4.0), a = std::pow(std::sin(kPi * ph), 2);
        const double ang = i * 1.7 + 0.4;
        d.fill(starP(0.38 * std::cos(ang), 0.38 * std::sin(ang), 4, 0.08 * a + 0.004, 0.015), hx(i % 2 ? "#ff9af0" : "#ffffff", static_cast<float>(a)));
    }
}


// ---------------------------------------------------------------- animal reactions

enum class Animal { Cat, Dog, Hamster, Bunny, Panda, Frog, Bear };

const Color kInk = Color::rgb(42, 26, 16);

// Head without eyes or mouth (those make the expression). Centred a little low, so ears fit.
void head(const D& d, Animal a, double t) {
    auto pointedEars = [&](const char* c, const char* inner) {
        for (double sx : {-1.0, 1.0}) {
            const double wig = sx > 0 ? 0.08 * std::max(0.0, wave(t, 2.6)) : 0;
            const D e = d.at(Affine::translate(sx * 0.24, -0.22) * Affine::rotate(sx * 0.32 + wig));
            e.fill(poly({{-0.14, 0.1}, {0.14, 0.1}, {0, -0.2}}), hx(c));
            e.fill(poly({{-0.075, 0.07}, {0.075, 0.07}, {0, -0.12}}), hx(inner));
        }
    };
    auto roundEars = [&](const char* c, const char* inner, double spread, double y, double r) {
        for (double sx : {-1.0, 1.0}) {
            d.fill(circ(sx * spread, y, r), hx(c));
            d.fill(circ(sx * spread, y, r * 0.55), hx(inner));
        }
    };
    switch (a) {
        case Animal::Cat:
            pointedEars("#ffb35a", "#ffd7e0");
            d.rad(ell(0, 0.06, 0.4, 0.34), {-0.1, -0.08}, 0.5, hx("#ffc77a"), hx("#f29a3a"));
            for (double x : {-0.06, 0.0, 0.06}) d.stroke(line({x, -0.27}, {x * 1.2, -0.18}), 0.022, hx("#d07a20"));
            d.fill(poly({{-0.035, 0.1}, {0.035, 0.1}, {0, 0.135}}), hx("#ff6f8b"));
            for (double sx : {-1.0, 1.0})
                for (int i = 0; i < 2; ++i)
                    d.stroke(line({sx * 0.2, 0.12 + i * 0.04}, {sx * 0.43, 0.09 + i * 0.07}), 0.011, kInk.withAlpha(0.6f));
            break;
        case Animal::Dog:
            for (double sx : {-1.0, 1.0}) {
                const D e = d.at(Affine::translate(sx * 0.33, -0.1) * Affine::rotate(sx * (0.25 + 0.08 * wave(t, 1.4))));
                e.fill(ell(0, 0.08, 0.1, 0.21), hx("#8a5a2b"));
            }
            d.rad(ell(0, 0.06, 0.35, 0.34), {-0.1, -0.08}, 0.5, hx("#f3d2a2"), hx("#d9a66a"));
            d.fill(ell(0, 0.17, 0.18, 0.13), hx("#fff3e0"));
            d.fill(ell(0, 0.1, 0.065, 0.045), hx("#2a1a10"));
            break;
        case Animal::Hamster:
            roundEars("#e8a25a", "#ffc0cb", 0.24, -0.22, 0.09);
            d.rad(ell(0, 0.07, 0.4, 0.35), {-0.1, -0.1}, 0.5, hx("#f7c38a"), hx("#e39a52"));
            d.fill(ell(0, 0.17, 0.3, 0.22), hx("#fff6ea"));
            d.fill(ell(0, 0.09, 0.035, 0.025), hx("#ff8fa3"));
            break;
        case Animal::Bunny:
            for (double sx : {-1.0, 1.0}) {
                const double flop = sx > 0 ? 0.12 * std::max(0.0, wave(t, 2.2)) : 0;
                const D e = d.at(Affine::translate(sx * 0.12, -0.2) * Affine::rotate(sx * 0.14 + flop));
                e.fill(ell(0, -0.1, 0.075, 0.16), hx("#ffffff"));
                e.fill(ell(0, -0.09, 0.035, 0.11), hx("#ffc0d0"));
            }
            d.rad(ell(0, 0.1, 0.35, 0.3), {-0.1, -0.08}, 0.5, hx("#ffffff"), hx("#e6e6ee"));
            d.fill(ell(0, 0.14, 0.03, 0.022), hx("#ff6f8b"));
            break;
        case Animal::Panda:
            roundEars("#1c1c22", "#3a3a44", 0.27, -0.22, 0.11);
            d.rad(circ(0, 0.06, 0.37), {-0.1, -0.08}, 0.5, hx("#ffffff"), hx("#e3e6ec"));
            for (double sx : {-1.0, 1.0}) d.at(Affine::translate(sx * 0.14, 0.02) * Affine::rotate(sx * -0.5)).fill(ell(0, 0, 0.085, 0.11), hx("#1c1c22"));
            d.fill(ell(0, 0.13, 0.05, 0.035), hx("#1c1c22"));
            break;
        case Animal::Frog:
            for (double sx : {-1.0, 1.0}) d.fill(circ(sx * 0.17, -0.12, 0.13), hx("#7bd389"));
            d.rad(ell(0, 0.1, 0.42, 0.3), {-0.1, -0.05}, 0.5, hx("#9be3a5"), hx("#4caf50"));
            for (double sx : {-1.0, 1.0}) d.fill(ell(sx * 0.26, 0.17, 0.06, 0.035), hx("#ff8fab", 0.6f));
            break;
        case Animal::Bear:
            roundEars("#9a6a3a", "#d9a66a", 0.26, -0.22, 0.11);
            d.rad(circ(0, 0.06, 0.37), {-0.1, -0.08}, 0.5, hx("#b07a44"), hx("#80542a"));
            d.fill(ell(0, 0.16, 0.15, 0.11), hx("#e8c49a"));
            d.fill(ell(0, 0.11, 0.05, 0.035), hx("#2a1a10"));
            break;
    }
}

// Where the eyes sit for each animal (y, half spacing).
std::pair<double, double> eyeSpot(Animal a) {
    switch (a) {
        case Animal::Frog: return {-0.12, 0.17};
        case Animal::Dog: return {-0.01, 0.14};
        case Animal::Bunny: return {0.05, 0.13};
        default: return {0.02, 0.14};
    }
}

void dotEyes(const D& d, Animal a, double t, double lookX = 0, double lookY = 0, bool panda = false) {
    const auto [y, x] = eyeSpot(a);
    const double b = blink(t, 3.2, 1.3);
    for (double sx : {-1.0, 1.0}) {
        const D e = d.at(Affine::translate(sx * x + lookX, y + lookY) * Affine::scale(1, b));
        if (a == Animal::Frog) {
            d.fill(circ(sx * x, y, 0.075), hx("#ffffff"));
        }
        e.fill(circ(0, 0, panda ? 0.03 : 0.048), hx(panda ? "#ffffff" : "#1c1c22"));
        if (panda) e.fill(circ(0, 0, 0.018), hx("#1c1c22"));
        else e.fill(circ(0.015, -0.017, 0.016), hx("#ffffff"));
    }
}

void shockEyes(const D& d, Animal a, double t) {
    const auto [y, x] = eyeSpot(a);
    const double jitter = 0.006 * std::sin(t * 40) * std::exp(-t * 2);
    for (double sx : {-1.0, 1.0}) {
        d.fill(circ(sx * x + jitter, y - 0.01, 0.085), hx("#ffffff"));
        d.stroke(circ(sx * x + jitter, y - 0.01, 0.085), 0.014, kInk);
        d.fill(circ(sx * x + jitter, y - 0.005, 0.025), hx("#1c1c22"));
    }
}

void happyEyes(const D& d, Animal a, bool sad = false) {  // ^ ^ (or closed, crying)
    const auto [y, x] = eyeSpot(a);
    for (double sx : {-1.0, 1.0})
        d.stroke(sad ? arc(sx * x, y - 0.02, 0.055, 0.04, kPi * 0.1, kPi * 0.9) : arc(sx * x, y + 0.03, 0.055, 0.05, kPi * 1.1, kPi * 1.9), 0.022, kInk);
}

void sleepyEyes(const D& d, Animal a) {
    const auto [y, x] = eyeSpot(a);
    for (double sx : {-1.0, 1.0}) d.stroke(arc(sx * x, y - 0.01, 0.05, 0.03, kPi * 0.1, kPi * 0.9), 0.02, kInk);
}

void heartEyes(const D& d, Animal a, double t) {
    const auto [y, x] = eyeSpot(a);
    const double beat = 1 + 0.15 * std::max(0.0, wave(t, 0.8));
    for (double sx : {-1.0, 1.0}) d.at(Affine::translate(sx * x, y) * Affine::scale(beat)).fill(heartP(0, 0, 0.13), hx("#ff2d55"));
}

void blush(const D& d, double y, double x, float a = 0.55f) {
    for (double sx : {-1.0, 1.0}) d.fill(ell(sx * x, y, 0.06, 0.035), hx("#ff7a9a", a));
}

void mouthSmile(const D& d, double y, double w = 0.07) { d.stroke(arc(0, y - w * 0.6, w, w, kPi * 0.2, kPi * 0.8), 0.018, kInk); }
void mouthCat(const D& d, double y) {
    d.stroke(arc(-0.035, y, 0.035, 0.035, 0, kPi), 0.016, kInk);
    d.stroke(arc(0.035, y, 0.035, 0.035, 0, kPi), 0.016, kInk);
}
void mouthOpen(const D& d, double y, double w, double h) {
    d.fill(ell(0, y, w, h), hx("#5a1a1a"));
    d.fill(ell(0, y + h * 0.45, w * 0.6, h * 0.4), hx("#ff6f8b"));
}
void mouthLaugh(const D& d, double y, double open) {
    Path m;
    m.moveTo(-0.11, y).cubicTo(-0.09, y + 0.12 * open, 0.09, y + 0.12 * open, 0.11, y).close();
    d.fill(m, hx("#5a1a1a"));
    d.fill(ell(0, y + 0.06 * open, 0.05, 0.03 * open), hx("#ff6f8b"));
}

// Short lines that flash around a shocked face.
void shockLines(const D& d, double t) {
    const float a = static_cast<float>(0.4 + 0.6 * (0.5 + 0.5 * wave(t, 0.5)));
    for (double sx : {-1.0, 1.0})
        for (int i = 0; i < 3; ++i) {
            const double ang = -kPi / 2 + sx * (0.75 + i * 0.28);
            d.stroke(line({0.42 * std::cos(ang), 0.06 + 0.4 * std::sin(ang)}, {0.48 * std::cos(ang), 0.06 + 0.46 * std::sin(ang)}), 0.02, hx("#ff3b5c", a));
        }
}

void tearDrops(const D& d, Animal a, double t) {
    const auto [y, x] = eyeSpot(a);
    for (double sx : {-1.0, 1.0})
        for (int i = 0; i < 2; ++i) {
            const double ph = frac(t / 1.1 + i * 0.5 + (sx > 0 ? 0.25 : 0));
            const double a2 = std::sin(kPi * ph);
            const double px = sx * (x + 0.04 + 0.08 * ph), py = y + 0.04 + 0.22 * ph;
            d.fill(Path().moveTo(px, py - 0.04).quadTo(px + 0.03, py, px, py + 0.025).quadTo(px - 0.03, py, px, py - 0.04).close(),
                   hx("#5ac8ff", static_cast<float>(0.9 * a2)));
        }
}

void paws(const D& d, const char* c, double y, double spread, double lift = 0) {
    for (double sx : {-1.0, 1.0}) {
        const D p = d.at(Affine::translate(sx * spread, y - lift) * Affine::rotate(sx * -0.4));
        p.fill(ell(0, 0, 0.075, 0.09), hx(c));
        for (int i = -1; i <= 1; ++i) p.stroke(line({i * 0.025, -0.08}, {i * 0.025, -0.05}), 0.01, kInk.withAlpha(0.5f));
    }
}

void floatHearts(const D& d, double t, double x0 = 0.3, double y0 = -0.2) {
    for (int i = 0; i < 2; ++i) {
        const double ph = frac(t / 1.6 + i * 0.5), a = std::sin(kPi * ph);
        d.at(Affine::translate(x0 + 0.06 * wave(t, 1.2, i * 0.3) - i * 0.06, y0 - 0.22 * ph) * Affine::scale(0.6 + 0.4 * ph))
            .fill(heartP(0, 0, 0.11), hx(i ? "#ff8fab" : "#ff2d55", static_cast<float>(a)));
    }
}

void react(const D& d0, Animal a, const std::function<void(const D&, double)>& face, double bob = 0.012) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02 + bob * wave(t, 2.0)) * Affine::rotate(0.04 * wave(t, 3.2)) * Affine::scale(0.92));
    head(d, a, t);
    face(d, t);
}

void catWow(const D& d0) {
    react(d0, Animal::Cat, [](const D& d, double t) {
        shockEyes(d, Animal::Cat, t);
        for (double sx : {-1.0, 1.0}) d.stroke(arc(sx * 0.14, -0.1, 0.06, 0.03, kPi * 1.15, kPi * 1.85), 0.018, kInk);
        const double gasp = 1 + 0.12 * wave(d.t, 1.1);
        mouthOpen(d, 0.2, 0.05, 0.065 * gasp);
        paws(d, "#ffc77a", 0.24, 0.27, 0.0);
        shockLines(d, t);
        const D w = d.at(Affine::translate(0.3, -0.38) * Affine::rotate(0.2) * Affine::scale(1 + 0.06 * wave(t, 0.8)));
        w.fill(textPath("WOW", 0, 0, 0.2, 0.36, "bungee"), hx("#1c1c22"));
        w.at(Affine::translate(-0.01, -0.01)).fill(textPath("WOW", 0, 0, 0.2, 0.36, "bungee"), hx("#ffd23f"));
    });
}

void catLove(const D& d0) {
    react(d0, Animal::Cat, [](const D& d, double t) {
        heartEyes(d, Animal::Cat, t);
        mouthCat(d, 0.15);
        blush(d, 0.12, 0.25);
        floatHearts(d, t, 0.32, -0.18);
    });
}

void catSleepy(const D& d0) {
    react(d0, Animal::Cat, [](const D& d, double t) {
        sleepyEyes(d, Animal::Cat);
        d.fill(ell(0, 0.17, 0.025, 0.02 + 0.008 * wave(t, 2.4)), hx("#5a1a1a"));
        for (int i = 0; i < 3; ++i) {
            const double ph = frac(t / 2.4 + i / 3.0), a = std::sin(kPi * ph);
            d.fill(textPath("Z", 0.3 + 0.1 * ph, -0.08 - 0.3 * ph, 0.07 + 0.08 * ph, 0.2, "bungee"), hx("#5a6cff", static_cast<float>(a)));
        }
    }, 0.025);
}

void catAngry(const D& d0) {
    react(d0, Animal::Cat, [](const D& d, double t) {
        d.lin(ell(0, -0.05, 0.3, 0.16), {0, -0.2}, {0, 0.1}, hx("#ff3b3b", 0.45f), hx("#ff3b3b", 0));
        const auto [y, x] = eyeSpot(Animal::Cat);
        for (double sx : {-1.0, 1.0}) {
            d.fill(ell(sx * x, y + 0.01, 0.045, 0.03), hx("#1c1c22"));
            d.stroke(line({sx * (x + 0.07), y - 0.07}, {sx * (x - 0.05), y - 0.03}), 0.025, kInk);
        }
        d.stroke(arc(0, 0.22, 0.06, 0.04, kPi * 1.15, kPi * 1.85), 0.02, kInk);
        for (double sx : {-1.0, 1.0}) {  // steam puffs from the ears
            const double ph = frac(t / 1.0 + (sx > 0 ? 0.5 : 0)), a = std::sin(kPi * ph);
            d.fill(circ(sx * (0.36 + 0.06 * ph), -0.3 - 0.1 * ph, 0.03 + 0.05 * ph), hx("#ffffff", static_cast<float>(0.85 * a)));
        }
        const D v = d.at(Affine::translate(0.24, -0.12) * Affine::scale(1 + 0.12 * std::max(0.0, wave(t, 0.6))));
        for (int i = 0; i < 4; ++i) v.at(Affine::rotate(i * kPi / 2)).stroke(arc(0.04, 0.04, 0.03, 0.03, kPi, kPi * 1.5), 0.02, hx("#e0001b"));
    });
}

void dogLaugh(const D& d0) {
    const double t = d0.t;
    react(d0.at(Affine::rotate(0.06 * wave(t, 0.35))), Animal::Dog, [](const D& d, double t2) {
        happyEyes(d, Animal::Dog);
        mouthLaugh(d, 0.15, 0.85 + 0.15 * std::abs(wave(t2, 0.35)));
        tearDrops(d, Animal::Dog, t2);
    });
}

void dogCool(const D& d0) {
    react(d0, Animal::Dog, [](const D& d, double t) {
        const auto [y, x] = eyeSpot(Animal::Dog);
        d.stroke(line({-x - 0.1, y - 0.03}, {x + 0.1, y - 0.03}), 0.02, hx("#1c1c22"));
        for (double sx : {-1.0, 1.0}) d.fill(rrect(sx * x - 0.09, y - 0.05, 0.18, 0.1, 0.04), hx("#1c1c22"));
        const double ph = frac(t / 2.6);  // a shine sweeps the lenses
        if (ph < 0.4) {
            const double sx2 = -x - 0.1 + (2 * x + 0.2) * (ph / 0.4);
            d.fill(poly({{sx2 - 0.02, y + 0.05}, {sx2 + 0.02, y + 0.05}, {sx2 + 0.06, y - 0.05}, {sx2 + 0.02, y - 0.05}}), hx("#ffffff", 0.6f));
        }
        d.stroke(arc(0.03, 0.2, 0.06, 0.04, kPi * 0.1, kPi * 0.7), 0.02, kInk);
    });
}

void dogParty(const D& d0) {
    react(d0, Animal::Dog, [](const D& d, double t) {
        const D hat = d.at(Affine::translate(0.1, -0.24) * Affine::rotate(0.35 + 0.05 * wave(t, 1.2)));
        hat.lin(poly({{-0.09, 0.06}, {0.09, 0.06}, {0, -0.2}}), {-0.09, 0}, {0.09, 0}, hx("#3ec1d3"), hx("#9b5de5"));
        for (int i = 0; i < 3; ++i) hat.fill(circ(-0.03 + i * 0.03, -0.02 - i * 0.05, 0.012), hx("#ffd23f"));
        hat.fill(circ(0, -0.21, 0.035), hx("#ff4d6d"));
        dotEyes(d, Animal::Dog, t);
        // A party horn that unrolls and rolls back.
        const double k = 0.5 + 0.5 * wave(t, 1.0);
        d.fill(rrect(-0.02, 0.17, 0.08, 0.035, 0.015), hx("#ff4d6d"));
        d.stroke(Path().moveTo(0.06, 0.19).lineTo(0.06 + 0.24 * k, 0.19 + 0.02 * k), 0.035, hx("#ffd23f"));
        if (k < 0.6) d.stroke(arc(0.06 + 0.24 * k, 0.24, 0.05 * (1 - k), 0.05 * (1 - k), -kPi / 2, kPi), 0.03, hx("#ffd23f"));
        static const char* cc[] = {"#ff4d6d", "#ffd23f", "#3ec1d3", "#7bd389"};
        for (int i = 0; i < 6; ++i) {
            const double ph = frac(t / 1.5 + i / 6.0), a = std::sin(kPi * ph);
            const double x = -0.4 + 0.16 * i;
            d.at(Affine::translate(x, -0.42 + 0.3 * ph) * Affine::rotate(t * 3 + i)).fill(rrect(-0.02, -0.01, 0.04, 0.02, 0), hx(cc[i % 4], static_cast<float>(a)));
        }
    });
}

void hamsterCry(const D& d0) {
    react(d0, Animal::Hamster, [](const D& d, double t) {
        happyEyes(d, Animal::Hamster, true);
        tearDrops(d, Animal::Hamster, t);
        Path m;
        const double w = 0.01 * wave(t, 0.3);
        m.moveTo(-0.06, 0.2).quadTo(-0.03, 0.17 + w, 0, 0.2).quadTo(0.03, 0.17 - w, 0.06, 0.2);
        d.stroke(m, 0.018, kInk);
        blush(d, 0.13, 0.25, 0.4f);
    }, 0.02);
}

void bunnyShy(const D& d0) {
    react(d0, Animal::Bunny, [](const D& d, double t) {
        dotEyes(d, Animal::Bunny, t, 0, 0.02);
        const float b = static_cast<float>(0.5 + 0.25 * (0.5 + 0.5 * wave(t, 1.6)));
        blush(d, 0.16, 0.22, b);
        mouthSmile(d, 0.22, 0.03);
        paws(d, "#ffffff", 0.3, 0.06);
        floatHearts(d, t, 0.34, -0.12);
    });
}

void pandaSweat(const D& d0) {
    react(d0, Animal::Panda, [](const D& d, double t) {
        dotEyes(d, Animal::Panda, t, 0.012 * wave(t, 1.4), 0, true);
        Path m;
        m.moveTo(-0.08, 0.2);
        for (int i = 1; i <= 8; ++i) m.lineTo(-0.08 + i * 0.02, 0.2 + (i % 2 ? -0.015 : 0.015));
        d.stroke(m, 0.016, hx("#1c1c22"));
        const double ph = frac(t / 1.8), a = std::sin(kPi * ph);
        const double px = 0.26, py = -0.16 + 0.14 * ph;
        d.fill(Path().moveTo(px, py - 0.06).quadTo(px + 0.045, py, px, py + 0.035).quadTo(px - 0.045, py, px, py - 0.06).close(),
               hx("#7fd4ff", static_cast<float>(0.95 * a)));
    });
}

void frogHappy(const D& d0) {
    react(d0, Animal::Frog, [](const D& d, double t) {
        dotEyes(d, Animal::Frog, t);
        Path m;
        const double open = 0.04 + 0.02 * std::abs(wave(t, 0.8));
        m.moveTo(-0.2, 0.1).quadTo(0, 0.1 + open * 3, 0.2, 0.1).quadTo(0, 0.1 + open, -0.2, 0.1).close();
        d.fill(m, hx("#2e6b34"));
    }, 0.03);
}

void chickDance(const D& d0) {
    const double t = d0.t;
    const double hop = std::abs(wave(t, 0.9));
    const D d = d0.at(Affine::translate(0, 0.06 - 0.06 * hop) * Affine::rotate(0.15 * wave(t, 1.8)) * Affine::scale(0.9));
    for (double sx : {-1.0, 1.0}) d.at(Affine::translate(sx * 0.3, 0.1) * Affine::rotate(sx * (0.4 + 0.5 * std::abs(wave(t, 0.45))))).fill(ell(0, 0, 0.07, 0.14), hx("#ffcf2a"));
    d.rad(circ(0, 0.05, 0.34), {-0.1, -0.1}, 0.5, hx("#fff27a"), hx("#ffc21a"));
    d.fill(Path().moveTo(-0.02, -0.29).quadTo(0.02, -0.4, 0.06, -0.3).quadTo(0.05, -0.36, 0.1, -0.33).quadTo(0.06, -0.28, 0.03, -0.27).close(), hx("#ffc21a"));
    happyEyes(d, Animal::Cat);
    d.fill(poly({{-0.05, 0.1}, {0.05, 0.1}, {0, 0.16}}), hx("#ff8a1f"));
    blush(d, 0.12, 0.22, 0.5f);
    for (double sx : {-1.0, 1.0}) d.fill(rrect(sx * 0.1 - 0.02, 0.38, 0.04, 0.06, 0.01), hx("#ff8a1f"));
    for (int i = 0; i < 2; ++i) {
        const double ph = frac(t / 1.6 + i * 0.5), a = std::sin(kPi * ph);
        const D n = d0.at(Affine::translate(i ? 0.36 : -0.36, -0.15 - 0.2 * ph));
        const Color c = hx(i ? "#9b5de5" : "#3ec1d3", static_cast<float>(a));
        n.fill(ell(0, 0.04, 0.035, 0.026), c);
        n.fill(rrect(0.022, -0.06, 0.012, 0.1, 0), c);
    }
}

void bearHi(const D& d0) {
    react(d0, Animal::Bear, [](const D& d, double t) {
        dotEyes(d, Animal::Bear, t);
        mouthSmile(d, 0.2, 0.05);
        blush(d, 0.14, 0.24, 0.4f);
        const D p = d.at(Affine::translate(0.34, 0.24) * Affine::rotate(0.25 + 0.3 * wave(t, 0.7)) * Affine::translate(0, -0.16));
        p.fill(rrect(-0.06, -0.02, 0.12, 0.2, 0.06), hx("#9a6a3a"));
        p.fill(ell(0, -0.06, 0.1, 0.11), hx("#9a6a3a"));
        p.fill(ell(0, -0.05, 0.055, 0.06), hx("#e8c49a"));
        for (int i = -1; i <= 1; ++i) p.fill(circ(i * 0.045, -0.15, 0.022), hx("#e8c49a"));
        const D b = d.at(Affine::translate(-0.28, -0.38) * Affine::scale(1 + 0.04 * wave(t, 1.0)));
        b.fill(rrect(-0.17, -0.08, 0.34, 0.16, 0.08), hx("#ffffff"));
        b.fill(poly({{0.04, 0.06}, {0.13, 0.06}, {0.12, 0.13}}), hx("#ffffff"));
        b.fill(textPath("Chào!", 0, -0.005, 0.11, 0.28), kInk);
    });
}

void dogThink(const D& d0) {
    react(d0, Animal::Dog, [](const D& d, double t) {
        dotEyes(d, Animal::Dog, t, 0.02, -0.025);
        d.stroke(line({0.0, 0.2}, {0.07, 0.19}), 0.018, kInk);
        const D p = d.at(Affine::translate(0.1, 0.3) * Affine::rotate(-0.3 + 0.05 * wave(t, 1.4)));
        p.fill(ell(0, 0, 0.08, 0.07), hx("#d9a66a"));
        const double ph = 0.5 + 0.5 * wave(t, 1.6);
        d.fill(textPath("?", 0.3, -0.32 - 0.03 * ph, 0.2, 0.2, "bungee"), hx("#4d9bff"));
        d.fill(circ(0.2, -0.22, 0.015 + 0.008 * ph), hx("#4d9bff", 0.7f));
    });
}

void catKiss(const D& d0) {
    react(d0, Animal::Cat, [](const D& d, double t) {
        const auto [y, x] = eyeSpot(Animal::Cat);
        d.fill(circ(-x, y, 0.048), hx("#1c1c22"));
        d.fill(circ(-x + 0.015, y - 0.017, 0.016), hx("#ffffff"));
        d.stroke(arc(x, y + 0.02, 0.05, 0.04, kPi * 1.1, kPi * 1.9), 0.02, kInk);  // wink
        d.fill(heartP(0.0, 0.18, 0.06), hx("#ff4d6d"));
        blush(d, 0.12, 0.25);
        const double ph = frac(t / 1.4), a = std::sin(kPi * ph);
        d.at(Affine::translate(0.1 + 0.25 * ph, 0.12 - 0.3 * ph) * Affine::scale(0.5 + 0.6 * ph)).fill(heartP(0, 0, 0.12), hx("#ff2d55", static_cast<float>(a)));
    });
}

void dogOmg(const D& d0) {
    react(d0, Animal::Dog, [](const D& d, double t) {
        shockEyes(d, Animal::Dog, t);
        mouthOpen(d, 0.2, 0.06, 0.07 * (1 + 0.1 * wave(t, 1.0)));
        paws(d, "#d9a66a", 0.26, 0.3);
        shockLines(d, t);
        const D w = d.at(Affine::translate(-0.3, -0.38) * Affine::rotate(-0.2) * Affine::scale(1 + 0.06 * wave(t, 0.8)));
        w.fill(textPath("OMG", 0, 0, 0.2, 0.36, "bungee"), hx("#1c1c22"));
        w.at(Affine::translate(-0.01, -0.01)).fill(textPath("OMG", 0, 0, 0.2, 0.36, "bungee"), hx("#ff4f9a"));
    });
}

// ---------------------------------------------------------------- drinks and daily life

// Wisps of steam rising from (x, y), fading as they go.
void steam(const D& d, double t, double x, double y, int n = 3, double spread = 0.08, double h = 0.22) {
    for (int i = 0; i < n; ++i) {
        const double ph = frac(t / 2.0 + i / static_cast<double>(n));
        const double a = std::sin(kPi * ph);
        const double x0 = x + (i - (n - 1) / 2.0) * spread;
        Path p;
        for (int k = 0; k <= 10; ++k) {
            const double v = k / 10.0;
            const double px = x0 + 0.022 * std::sin(v * kPi * 1.6 + t * 2 + i * 1.7), py = y - h * (ph * 0.45 + v * 0.55);
            if (k == 0) p.moveTo(px, py);
            else p.lineTo(px, py);
        }
        d.stroke(p, 0.022, hx("#ffffff", static_cast<float>(0.6 * a)));
    }
}

void phin(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02));
    // Glass below with coffee over condensed milk.
    d.fill(poly({{-0.2, -0.02}, {0.2, -0.02}, {0.17, 0.44}, {-0.17, 0.44}}), hx("#e6f4ff", 0.35f));
    d.fill(poly({{-0.186, 0.2}, {0.186, 0.2}, {0.172, 0.42}, {-0.172, 0.42}}), hx("#5a2e12"));
    d.fill(poly({{-0.174, 0.36}, {0.174, 0.36}, {0.172, 0.42}, {-0.172, 0.42}}), hx("#f5e6c8"));
    d.stroke(poly({{-0.2, -0.02}, {0.2, -0.02}, {0.17, 0.44}, {-0.17, 0.44}}), 0.015, hx("#ffffff", 0.8f));
    // The phin on top.
    d.fill(rrect(-0.27, -0.06, 0.54, 0.05, 0.02), hx("#c9cfdb"));
    d.lin(poly({{-0.17, -0.34}, {0.17, -0.34}, {0.14, -0.05}, {-0.14, -0.05}}), {-0.17, 0}, {0.17, 0}, hx("#e6e9f0"), hx("#9aa3b5"));
    d.fill(rrect(-0.19, -0.4, 0.38, 0.07, 0.03), hx("#b8bfcc"));
    d.fill(rrect(-0.04, -0.46, 0.08, 0.07, 0.02), hx("#9aa3b5"));
    for (int i = 0; i < 3; ++i) {  // drops falling into the glass
        const double ph = frac(t / 0.9 + i / 3.0);
        const double y = -0.0 + 0.2 * ph * ph;
        d.fill(ell(0, y, 0.014, 0.022), hx("#5a2e12", static_cast<float>(std::min(1.0, (1 - ph) * 3))));
    }
}

void icedCoffee(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.04 * wave(t, 3.0)));
    d.stroke(line({0.06, -0.46}, {0.0, 0.2}), 0.04, hx("#ff4d6d"));
    const Path g = poly({{-0.22, -0.28}, {0.22, -0.28}, {0.18, 0.44}, {-0.18, 0.44}});
    d.lin(poly({{-0.21, -0.12}, {0.21, -0.12}, {0.18, 0.43}, {-0.18, 0.43}}), {0, -0.12}, {0, 0.43}, hx("#c98a5a"), hx("#5a2e12"));
    for (int i = 0; i < 3; ++i) {  // ice cubes bob in the coffee
        const double bob = 0.015 * wave(t, 1.6, i * 0.3);
        d.at(Affine::translate(-0.1 + i * 0.1, -0.08 + (i % 2) * 0.1 + bob) * Affine::rotate(0.3 * (i - 1))).fill(rrect(-0.05, -0.05, 0.1, 0.1, 0.02), hx("#ffffff", 0.55f));
    }
    d.fill(g, hx("#e6f4ff", 0.25f));
    d.stroke(g, 0.015, hx("#ffffff", 0.85f));
    d.fill(rrect(-0.17, -0.25, 0.03, 0.6, 0.015), hx("#ffffff", 0.35f));
}

void latte(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08));
    steam(d, t, 0, -0.2);
    d.fill(ell(0, 0.3, 0.42, 0.09), hx("#e6e9f0"));
    d.stroke(arc(0.3, 0.06, 0.09, 0.09, -kPi / 2, kPi / 2), 0.05, hx("#f8f8fb"));
    d.lin(Path().moveTo(-0.3, -0.06).lineTo(0.3, -0.06).cubicTo(0.3, 0.24, 0.2, 0.3, 0, 0.3).cubicTo(-0.2, 0.3, -0.3, 0.24, -0.3, -0.06).close(),
          {-0.3, 0}, {0.3, 0}, hx("#ffffff"), hx("#d9dde6"));
    d.fill(ell(0, -0.06, 0.3, 0.07), hx("#b8743a"));
    d.at(Affine::translate(0, -0.06) * Affine::scale(1, 0.3) * Affine::rotate(0.15 * wave(t, 3.0))).fill(heartP(0, 0, 0.2), hx("#f7e7cf"));
}

void teaCup(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.1));
    steam(d, t, -0.04, -0.12, 2, 0.1);
    d.fill(ell(0, 0.27, 0.4, 0.07), hx("#d8f0e0"));
    d.stroke(arc(0.29, 0.04, 0.08, 0.08, -kPi / 2, kPi / 2), 0.045, hx("#9be3b5"));
    d.lin(Path().moveTo(-0.3, -0.04).lineTo(0.3, -0.04).cubicTo(0.28, 0.2, 0.18, 0.27, 0, 0.27).cubicTo(-0.18, 0.27, -0.28, 0.2, -0.3, -0.04).close(),
          {0, -0.04}, {0, 0.27}, hx("#b8f0cc"), hx("#6fcf97"));
    d.fill(ell(0, -0.04, 0.3, 0.06), hx("#c98a3a"));
    // The tea bag's string over the rim; its tag swings.
    const double sw = 0.18 * wave(t, 2.0);
    d.stroke(Path().moveTo(0.05, -0.04).quadTo(0.2, -0.12, 0.24, -0.04), 0.012, hx("#ffffff"));
    const D tag = d.at(Affine::translate(0.24, -0.04) * Affine::rotate(sw));
    tag.stroke(line({0, 0}, {0, 0.12}), 0.01, hx("#ffffff"));
    tag.fill(rrect(-0.045, 0.12, 0.09, 0.08, 0.01), hx("#ffd23f"));
}

void icedTea(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.04 * wave(t, 3.0)));
    const Path g = poly({{-0.2, -0.36}, {0.2, -0.36}, {0.17, 0.44}, {-0.17, 0.44}});
    d.lin(poly({{-0.195, -0.2}, {0.195, -0.2}, {0.17, 0.43}, {-0.17, 0.43}}), {0, -0.2}, {0, 0.43}, hx("#f7b267"), hx("#c8601a"));
    for (int i = 0; i < 3; ++i) {
        const double bob = 0.012 * wave(t, 1.5, i * 0.33);
        d.at(Affine::translate(-0.08 + i * 0.08, -0.14 + i * 0.09 + bob) * Affine::rotate(0.4 * i)).fill(rrect(-0.045, -0.045, 0.09, 0.09, 0.02), hx("#ffffff", 0.5f));
    }
    d.fill(g, hx("#e6f4ff", 0.22f));
    d.stroke(g, 0.015, hx("#ffffff", 0.85f));
    const D l = d.at(Affine::translate(0.2, -0.36) * Affine::rotate(0.2 * wave(t, 2.6)));
    l.fill(circ(0, 0, 0.12), hx("#fff27a"));
    l.fill(circ(0, 0, 0.1), hx("#fffbd0"));
    for (int i = 0; i < 6; ++i) l.at(Affine::rotate(i * kPi / 3)).fill(petalShape(0.085, 0.04, 0.6), hx("#ffe14d"));
}

void matcha(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.04 * wave(t, 3.0)));
    d.stroke(line({-0.06, -0.48}, {0.02, 0.1}), 0.04, hx("#3f8a4f"));
    const Path g = poly({{-0.21, -0.3}, {0.21, -0.3}, {0.18, 0.44}, {-0.18, 0.44}});
    d.fill(poly({{-0.205, -0.16}, {0.205, -0.16}, {0.195, 0.08}, {-0.195, 0.08}}), hx("#9be37a"));
    d.lin(poly({{-0.195, 0.08}, {0.195, 0.08}, {0.18, 0.43}, {-0.18, 0.43}}), {0, 0.08}, {0, 0.43}, hx("#fff6ea"), hx("#f1e6d0"));
    // The green swirls down into the milk.
    Path sw;
    sw.moveTo(-0.19, 0.08);
    for (int k = 0; k <= 12; ++k) sw.lineTo(-0.19 + k * 0.032, 0.08 + 0.03 * std::sin(k * 1.1 + t * 2));
    sw.lineTo(0.19, 0.06).lineTo(-0.19, 0.06).close();
    d.fill(sw, hx("#9be37a"));
    d.fill(g, hx("#e6f4ff", 0.2f));
    d.stroke(g, 0.015, hx("#ffffff", 0.85f));
}

void orangeJuice(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02));
    d.stroke(line({0.08, -0.46}, {0.0, 0.1}), 0.035, hx("#3ec1d3"));
    const Path g = poly({{-0.2, -0.3}, {0.2, -0.3}, {0.17, 0.44}, {-0.17, 0.44}});
    d.lin(poly({{-0.195, -0.18}, {0.195, -0.18}, {0.17, 0.43}, {-0.17, 0.43}}), {0, -0.18}, {0, 0.43}, hx("#ffc23a"), hx("#ff8a1f"));
    for (int i = 0; i < 5; ++i) {  // bubbles
        const double ph = frac(t / 1.6 + i / 5.0);
        d.fill(circ(-0.12 + i * 0.06, 0.38 - 0.5 * ph, 0.012), hx("#fff3c0", static_cast<float>(std::sin(kPi * ph) * 0.9)));
    }
    d.fill(g, hx("#e6f4ff", 0.22f));
    d.stroke(g, 0.015, hx("#ffffff", 0.85f));
    const D s = d.at(Affine::translate(-0.2, -0.3));
    s.fill(pie(0, 0, 0.14, 0.14, kPi, 2 * kPi), hx("#ff8a1f"));
    s.fill(pie(0, 0, 0.115, 0.115, kPi, 2 * kPi), hx("#ffd27a"));
    for (int i = 1; i < 4; ++i) s.stroke(line({0, 0}, {0.11 * std::cos(kPi + i * kPi / 4), 0.11 * std::sin(kPi + i * kPi / 4)}), 0.01, hx("#ff8a1f"));
}

void coconut(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08) * Affine::rotate(0.05 * wave(t, 3.0)));
    d.stroke(line({0.0, -0.18}, {0.14, -0.46}), 0.03, hx("#ff4d6d"));
    const D u = d.at(Affine::translate(-0.12, -0.3) * Affine::rotate(-0.4 + 0.1 * wave(t, 2.2)));
    u.stroke(line({0, 0}, {0.05, 0.16}), 0.015, hx("#8a5226"));
    u.fill(pie(0, 0, 0.16, 0.1, kPi, 2 * kPi), hx("#ffd23f"));
    for (int i = 0; i < 3; ++i) u.fill(pie(0, 0, 0.16, 0.1, kPi + i * kPi / 3, kPi + (i + 0.5) * kPi / 3), hx("#ff4d6d"));
    d.rad(circ(0, 0.1, 0.32), {-0.1, 0.0}, 0.4, hx("#a3e07a"), hx("#4caf3a"));
    d.fill(ell(0, -0.16, 0.14, 0.05), hx("#f8f4e8"));
    d.fill(ell(-0.12, 0.02, 0.05, 0.1), hx("#ffffff", 0.3f));
}

void cheers(const D& d0) {
    const double t = d0.t;
    const double clink = std::pow(std::max(0.0, wave(t, 1.6)), 3);  // they meet, then part
    for (double sx : {-1.0, 1.0}) {
        const D m = d0.at(Affine::translate(sx * (0.22 - 0.06 * clink), 0.06) * Affine::rotate(sx * (0.12 + 0.12 * clink)));
        m.stroke(rrect(sx * 0.12, -0.1, sx * 0.1 + 0.001, 0.2, 0.05), 0.04, hx("#ffe9b0"));
        m.lin(rrect(-0.13, -0.18, 0.26, 0.42, 0.04), {0, -0.18}, {0, 0.24}, hx("#ffd23f"), hx("#f0a10f"));
        for (int i = 0; i < 3; ++i) m.fill(rrect(-0.09 + i * 0.07, -0.1, 0.03, 0.3, 0.015), hx("#ffffff", 0.25f));
        m.fill(ell(0, -0.2, 0.15, 0.07), hx("#ffffff"));
        m.fill(circ(-0.08, -0.25, 0.05), hx("#ffffff"));
        m.fill(circ(0.06, -0.26, 0.06), hx("#ffffff"));
    }
    if (clink > 0.3) {
        const float a = static_cast<float>((clink - 0.3) / 0.7);
        for (int i = 0; i < 3; ++i) {
            const double ang = -kPi / 2 + (i - 1) * 0.5;
            d0.stroke(line({0.14 * std::cos(ang), -0.3 + 0.14 * std::sin(ang)}, {0.22 * std::cos(ang), -0.3 + 0.22 * std::sin(ang)}), 0.02, hx("#ffd23f", a));
        }
    }
}

void banhMi(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.25 + 0.04 * wave(t, 3.0)));
    d.lin(Path().moveTo(-0.46, 0.04).cubicTo(-0.46, -0.16, 0.46, -0.16, 0.46, 0.04).close(), {0, -0.14}, {0, 0.04}, hx("#f2b45a"), hx("#c8781e"));
    d.fill(Path().moveTo(-0.42, 0.04).lineTo(0.42, 0.04).lineTo(0.4, 0.08).lineTo(-0.4, 0.08).close(), hx("#ff6b6b"));
    for (int i = 0; i < 6; ++i) d.fill(ell(-0.33 + i * 0.13, 0.09, 0.07, 0.03), hx(i % 2 ? "#7bd389" : "#3fae4f"));
    d.fill(Path().moveTo(-0.38, 0.1).lineTo(0.38, 0.1).lineTo(0.36, 0.13).lineTo(-0.36, 0.13).close(), hx("#ffd27a"));
    d.lin(Path().moveTo(-0.44, 0.12).lineTo(0.44, 0.12).cubicTo(0.44, 0.26, -0.44, 0.26, -0.44, 0.12).close(), {0, 0.12}, {0, 0.24}, hx("#e8a04a"), hx("#b8681a"));
    for (int i = 0; i < 4; ++i) d.stroke(line({-0.27 + i * 0.16, -0.08}, {-0.2 + i * 0.16, -0.04}), 0.02, hx("#ffe0a8"));
}

void pho(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.1));
    steam(d, t, 0, -0.16, 3, 0.12, 0.2);
    d.fill(ell(0, -0.02, 0.44, 0.1), hx("#f2e6d0"));
    d.fill(ell(0, -0.02, 0.4, 0.08), hx("#f5c96a"));
    for (int i = 0; i < 3; ++i) d.fill(ell(-0.2 + i * 0.18, -0.03, 0.08, 0.03), hx("#c86a4a"));
    for (int i = 0; i < 4; ++i) d.fill(ell(-0.25 + i * 0.15, 0.0, 0.035, 0.015), hx("#3fae4f"));
    d.lin(Path().moveTo(-0.44, -0.02).cubicTo(-0.4, 0.3, 0.4, 0.3, 0.44, -0.02).cubicTo(0.3, 0.06, -0.3, 0.06, -0.44, -0.02).close(), {0, 0}, {0, 0.28},
          hx("#ffffff"), hx("#d9dde6"));
    d.stroke(arc(0, 0.12, 0.3, 0.08, 0.2, kPi - 0.2), 0.015, hx("#4d9bff"));
    // Chopsticks lift noodles and lower them again.
    const double lift = 0.06 * (0.5 + 0.5 * wave(t, 2.2));
    const D c = d.at(Affine::translate(0.12, -0.1 - lift) * Affine::rotate(-0.5));
    for (int i = 0; i < 4; ++i) c.stroke(Path().moveTo(0.02 + i * 0.012, 0).quadTo(0.04 + i * 0.012, 0.12 + lift, 0.0 + i * 0.012, 0.18 + lift), 0.012, hx("#fff3d6"));
    c.stroke(line({0, 0.02}, {0.0, -0.36}), 0.025, hx("#8a5226"));
    c.stroke(line({0.06, 0.02}, {0.1, -0.35}), 0.025, hx("#8a5226"));
}

void laptop(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02));
    d.fill(rrect(-0.36, -0.36, 0.72, 0.48, 0.04), hx("#3a3f4b"));
    d.fill(rrect(-0.32, -0.32, 0.64, 0.4, 0.02), hx("#14141c"));
    // Lines of code type themselves in, then hold and start again with a soft fade.
    const double ph = frac(t / 4.0), fade = ph > 0.88 ? 1 - (ph - 0.88) / 0.12 : 1;
    static const double widths[] = {0.3, 0.42, 0.24, 0.36, 0.18};
    static const char* cols[] = {"#4dd8ff", "#ffd23f", "#7bd389", "#ff8fab", "#c9a2ff"};
    for (int i = 0; i < 5; ++i) {
        const double k = std::clamp(ph * 7 - i, 0.0, 1.0);
        if (k <= 0) continue;
        d.fill(rrect(-0.27 + (i % 2) * 0.04, -0.27 + i * 0.07, widths[i] * k, 0.03, 0.01), hx(cols[i], static_cast<float>(fade)));
    }
    if (std::fmod(t, 0.8) < 0.5) d.fill(rrect(0.17, 0.05, 0.03, 0.03, 0), hx("#ffffff", 0.8f));
    d.fill(Path().moveTo(-0.44, 0.14).lineTo(0.44, 0.14).lineTo(0.48, 0.24).lineTo(-0.48, 0.24).close(), hx("#c9cfdb"));
    d.fill(rrect(-0.08, 0.15, 0.16, 0.03, 0.01), hx("#9aa3b5"));
}

void bookGlasses(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08));
    for (double sx : {-1.0, 1.0}) {
        d.fill(Path().moveTo(0, -0.12).quadTo(sx * 0.2, -0.2, sx * 0.44, -0.14).lineTo(sx * 0.44, 0.24).quadTo(sx * 0.2, 0.18, 0, 0.26).close(), hx("#ffffff"));
        for (int i = 0; i < 4; ++i) d.stroke(line({sx * 0.08, -0.08 + i * 0.07}, {sx * 0.36, -0.1 + i * 0.07}), 0.012, hx("#c9cfdb"));
    }
    d.stroke(line({0, -0.12}, {0, 0.26}), 0.012, hx("#9aa3b5"));
    d.fill(Path().moveTo(-0.46, 0.26).quadTo(0, 0.2, 0.46, 0.26).lineTo(0.46, 0.3).quadTo(0, 0.25, -0.46, 0.3).close(), hx("#e0501a"));
    // A page turns slowly back and forth.
    const double k = 0.5 + 0.5 * wave(t, 3.2);
    d.fill(Path().moveTo(0, -0.12).quadTo(0.2 * k, -0.24 - 0.04 * (1 - k), 0.42 * (2 * k - 1), -0.16).lineTo(0.42 * (2 * k - 1), 0.22).quadTo(0.2 * k, 0.14, 0, 0.26).close(),
           hx("#f6f6fa"));
    const D g = d.at(Affine::translate(0, -0.3) * Affine::rotate(0.08 * wave(t, 2.5)));
    for (double sx : {-1.0, 1.0}) {
        g.fill(circ(sx * 0.12, 0, 0.08), hx("#bfe9ff", 0.5f));
        g.stroke(circ(sx * 0.12, 0, 0.08), 0.02, hx("#1c1c22"));
    }
    g.stroke(arc(0, 0.01, 0.04, 0.03, kPi * 1.1, kPi * 1.9), 0.018, hx("#1c1c22"));
}

void headphones(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.04) * Affine::rotate(0.06 * wave(t, 1.6)));
    d.stroke(arc(0, 0.06, 0.3, 0.32, kPi, 2 * kPi), 0.07, hx("#3a3f4b"));
    for (double sx : {-1.0, 1.0}) {
        const double beat = 1 + 0.05 * std::max(0.0, wave(t, 0.5));
        const D e = d.at(Affine::translate(sx * 0.3, 0.1) * Affine::scale(beat));
        e.fill(rrect(-0.08, -0.13, 0.16, 0.26, 0.07), hx("#ff4d6d"));
        e.fill(rrect(sx > 0 ? -0.08 : 0.02, -0.1, 0.06, 0.2, 0.03), hx("#1c1c22"));
        for (int i = 0; i < 2; ++i) {  // sound rings spread out
            const double ph = frac(t / 1.0 + i * 0.5), a = std::sin(kPi * ph) * 0.8;
            d.stroke(arc(sx * 0.3, 0.1, 0.12 + 0.08 * ph, 0.12 + 0.08 * ph, sx > 0 ? -0.6 : kPi - 0.6, sx > 0 ? 0.6 : kPi + 0.6), 0.02, hx("#4dd8ff", static_cast<float>(a)));
        }
    }
}

void alarmClock(const D& d0) {
    const double t = d0.t;
    const double ring = std::max(0.0, wave(t, 2.0));  // rings half the time, quiet the other half
    const D d = d0.at(Affine::translate(0, 0.04) * Affine::rotate(0.12 * ring * std::sin(t * 40)));
    for (double sx : {-1.0, 1.0}) {
        d.fill(pie(sx * 0.2, -0.3, 0.12, 0.12, kPi, 2 * kPi), hx("#ffd23f"));
        d.stroke(line({sx * 0.2, 0.3}, {sx * 0.28, 0.42}), 0.04, hx("#3a3f4b"));
    }
    d.fill(rrect(-0.03, -0.42, 0.06, 0.06, 0.02), hx("#3a3f4b"));
    d.rad(circ(0, 0.02, 0.32), {-0.1, -0.1}, 0.4, hx("#ff6b6b"), hx("#c8102e"));
    d.fill(circ(0, 0.02, 0.25), hx("#ffffff"));
    for (int i = 0; i < 12; ++i) d.at(Affine::translate(0, 0.02) * Affine::rotate(i * kPi / 6)).fill(rrect(-0.008, -0.23, 0.016, 0.04, 0), hx("#3a3f4b"));
    d.at(Affine::translate(0, 0.02) * Affine::rotate(t * 0.3)).stroke(line({0, 0}, {0, -0.12}), 0.03, hx("#1c1c22"));
    d.at(Affine::translate(0, 0.02) * Affine::rotate(t * 2)).stroke(line({0, 0}, {0, -0.19}), 0.018, hx("#1c1c22"));
    d.fill(circ(0, 0.02, 0.025), hx("#1c1c22"));
    for (double sx : {-1.0, 1.0})
        for (int i = 0; i < 2; ++i)
            d0.stroke(arc(sx * 0.2, -0.3, 0.17 + i * 0.06, 0.17 + i * 0.06, sx > 0 ? -1.3 : kPi + 0.5, sx > 0 ? -0.5 : kPi + 1.3), 0.02,
                      hx("#ff8a1f", static_cast<float>(ring)));
}

void plant(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.06));
    for (int i = 0; i < 5; ++i) {
        const double ang = -kPi / 2 + (i - 2) * 0.45 + 0.06 * wave(t, 2.6, i * 0.2);
        const D l = d.at(Affine::translate(0, 0.06) * Affine::rotate(ang + kPi / 2));
        l.lin(petalShape(0.34 - std::abs(i - 2) * 0.04, 0.12, 0.4), {0, 0}, {0, -0.3}, hx("#3fae4f"), hx("#9be37a"));
        l.stroke(line({0, 0}, {0, -0.26}), 0.01, hx("#2e7d32", 0.6f));
    }
    d.lin(poly({{-0.22, 0.08}, {0.22, 0.08}, {0.16, 0.42}, {-0.16, 0.42}}), {-0.22, 0}, {0.22, 0}, hx("#e07a4a"), hx("#b8501a"));
    d.fill(rrect(-0.25, 0.04, 0.5, 0.08, 0.02), hx("#c8603a"));
}

void umbrella(const D& d0) {
    const double t = d0.t;
    for (int i = 0; i < 6; ++i) {  // rain falls around it
        const double ph = frac(t / 0.8 + i / 6.0);
        const double x = -0.42 + i * 0.17, y = -0.46 + 0.5 * ph;
        if (std::abs(x) < 0.36 && y > -0.2) continue;
        d0.stroke(line({x, y}, {x - 0.02, y + 0.07}), 0.015, hx("#5ac8ff", static_cast<float>(std::sin(kPi * ph))));
    }
    const D d = d0.at(Affine::translate(0, 0.04) * Affine::rotate(0.08 * wave(t, 2.4)));
    Path canopy;
    canopy.moveTo(-0.4, 0.0);
    canopy.cubicTo(-0.38, -0.32, 0.38, -0.32, 0.4, 0.0);
    for (int i = 3; i >= -3; --i) {
        const double x0 = (i + 0.5) * 0.4 / 3.5, x1 = (i - 0.5) * 0.4 / 3.5;
        canopy.quadTo((x0 + x1) / 2, -0.06, x1, 0.0);
    }
    canopy.close();
    d.lin(canopy, {-0.4, 0}, {0.4, 0}, hx("#ff4d6d"), hx("#9b5de5"));
    d.stroke(line({0, -0.26}, {0, 0.32}), 0.03, hx("#b8743a"));
    d.stroke(arc(0.06, 0.32, 0.06, 0.06, 0, kPi), 0.03, hx("#b8743a"));
    d.fill(circ(0, -0.28, 0.025), hx("#3a3f4b"));
}

// ---------------------------------------------------------------- birthdays, Christmas, the year's days

// Words with a white outline under them (reads on any picture).
void title(const D& d, const std::string& s, double cx, double cy, double size, double maxW, const char* font, Color fill,
           Color outline = hx("#ffffff"), double ow = 0.05) {
    const Path p = textPath(s, cx, cy, size, maxW, font);
    d.stroke(p, ow, outline);
    d.fill(p, fill);
}

void sparkleAround(const D& d, double t, int n, double rx, double ry, const char* color = "#ffd23f") {
    for (int i = 0; i < n; ++i) {
        const double ph = frac(t / 1.4 + i / static_cast<double>(n)), a = std::pow(std::sin(kPi * ph), 2);
        const double ang = i * kTau / n + 0.5;
        d.fill(starP(rx * std::cos(ang), ry * std::sin(ang), 4, 0.06 * a + 0.004, 0.015), hx(color, static_cast<float>(a)));
    }
}

void flame(const D& d, double x, double y, double t, double s = 1) {
    const double f = 1 + 0.12 * std::sin(t * 13 + x * 40) * std::sin(t * 5.3);
    const D fl = d.at(Affine::translate(x, y) * Affine::rotate(0.12 * std::sin(t * 4 + x * 30)) * Affine::scale(s, s * f));
    fl.fill(Path().moveTo(0, -0.07).cubicTo(0.04, -0.03, 0.035, 0.02, 0, 0.02).cubicTo(-0.035, 0.02, -0.04, -0.03, 0, -0.07).close(), hx("#ff8a1f"));
    fl.fill(Path().moveTo(0, -0.045).cubicTo(0.022, -0.02, 0.02, 0.012, 0, 0.012).cubicTo(-0.02, 0.012, -0.022, -0.02, 0, -0.045).close(), hx("#fff27a"));
}

void bdayBanner(const D& d0) {
    const double t = d0.t;
    static const char* cols[] = {"#ff4d6d", "#ffd23f", "#3ec1d3", "#7bd389", "#9b5de5", "#ff8c42", "#f15bb5"};
    Path rope;
    rope.moveTo(-0.48, -0.36).quadTo(0, -0.14, 0.48, -0.36);
    d0.stroke(rope, 0.015, hx("#ffffff", 0.9f));
    for (int i = 0; i < 7; ++i) {
        const double u = (i + 0.5) / 7.0, x = -0.48 + 0.96 * u;
        const double y = -0.36 + 0.44 * u * (1 - u) * 2 * 0.5 + 0.0;
        const D f = d0.at(Affine::translate(x, y + 0.01) * Affine::rotate(0.12 * wave(t, 1.6, i * 0.14)));
        f.fill(poly({{-0.055, 0}, {0.055, 0}, {0, 0.12}}), hx(cols[i]));
    }
    const D w = d0.at(Affine::translate(0, 0.14) * Affine::scale(1 + 0.03 * wave(t, 1.4)));
    title(w, "Sinh nhật", 0, -0.04, 0.2, 0.84, "lobster", hx("#ff4d6d"));
    title(w, "vui vẻ!", 0, 0.16, 0.17, 0.6, "lobster", hx("#9b5de5"));
}

void bdayScript(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.08) * Affine::scale(1 + 0.03 * wave(t, 1.6)));
    title(d, "Happy", 0, -0.12, 0.26, 0.7, "pacifico", hx("#ff4f9a"));
    title(d, "Birthday", 0, 0.14, 0.24, 0.9, "pacifico", hx("#3ec1d3"));
    sparkleAround(d0, t, 5, 0.44, 0.38);
}

void partyHat(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08) * Affine::rotate(0.14 * wave(t, 1.8)));
    const Path cone = poly({{-0.26, 0.32}, {0.26, 0.32}, {0, -0.32}});
    d.lin(cone, {-0.26, 0}, {0.26, 0}, hx("#ff7aa8"), hx("#d6005a"));
    for (int i = 0; i < 3; ++i) {
        const double y = 0.18 - i * 0.16, hw = 0.26 * (0.32 - y) / 0.64 * 0 + 0.26 * (y + 0.32) / 0.64;
        d.fill(poly({{-hw, y}, {hw, y}, {hw * 0.82, y - 0.05}, {-hw * 0.82, y - 0.05}}), hx("#ffd23f"));
    }
    for (int i = 0; i < 4; ++i) d.fill(circ(-0.12 + i * 0.08, 0.26 - (i % 2) * 0.12, 0.018), hx("#ffffff"));
    d.fill(rrect(-0.29, 0.3, 0.58, 0.07, 0.03), hx("#7bd389"));
    const double bob = 0.06 * wave(t, 0.9);
    d.at(Affine::translate(0, -0.34) * Affine::rotate(bob)).fill(circ(0, -0.04, 0.07), hx("#ffd23f"));
}

void cakeSlice(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.06) * Affine::rotate(0.04 * wave(t, 3.0)));
    d.fill(ell(0, 0.34, 0.42, 0.07), hx("#e6e9f0"));
    d.fill(poly({{-0.34, -0.02}, {0.3, -0.18}, {0.3, 0.28}, {-0.34, 0.32}}), hx("#ffe9c9"));
    for (int i = 0; i < 2; ++i) d.fill(poly({{-0.34, 0.06 + i * 0.13}, {0.3, -0.07 + i * 0.13}, {0.3, -0.03 + i * 0.13}, {-0.34, 0.1 + i * 0.13}}), hx("#ff8fab"));
    d.fill(poly({{-0.34, -0.02}, {0.3, -0.18}, {0.36, -0.22}, {-0.3, -0.08}}), hx("#ffffff"));
    for (int i = 0; i < 4; ++i) d.fill(circ(-0.26 + i * 0.16, -0.06 - i * 0.035, 0.035), hx("#ffffff"));
    d.fill(circ(0.04, -0.22, 0.06), hx("#e0001b"));
    d.fill(circ(0.025, -0.235, 0.015), hx("#ffffff", 0.8f));
    d.stroke(Path().moveTo(0.04, -0.27).quadTo(0.06, -0.34, 0.1, -0.36), 0.012, hx("#3f8a4f"));
    sparkleAround(d0, t, 3, 0.42, 0.36, "#ffffff");
}

void candles(const D& d0) {
    const double t = d0.t;
    static const char* cols[] = {"#ff4d6d", "#3ec1d3", "#ffd23f"};
    for (int i = 0; i < 3; ++i) {
        const double x = -0.22 + i * 0.22, h = 0.5 - (i == 1 ? 0 : 0.08);
        const D c = d0.at(Affine::translate(x, 0.44 - h));
        c.fill(rrect(-0.05, 0, 0.1, h, 0.02), hx(cols[i]));
        for (int k = 0; k < 4; ++k) c.fill(poly({{-0.05, 0.06 + k * 0.1}, {0.05, 0.02 + k * 0.1}, {0.05, 0.05 + k * 0.1}, {-0.05, 0.09 + k * 0.1}}), hx("#ffffff", 0.6f));
        c.stroke(line({0, 0}, {0, -0.04}), 0.012, hx("#3a3f4b"));
        d0.rad(circ(x, 0.44 - h - 0.09, 0.1), {x, 0.44 - h - 0.09}, 0.1, hx("#ffd27a", 0.45f), hx("#ffd27a", 0));
        flame(d0, x, 0.44 - h - 0.06, t + i * 0.7, 1.1);
    }
}

void giftOpen(const D& d0) {
    const double t = d0.t;
    const double ph = frac(t / 3.0), lift = std::pow(std::sin(kPi * ph), 2);  // the lid lifts and settles back
    const D d = d0.at(Affine::translate(0, 0.08));
    for (int i = 0; i < 3; ++i) {  // what comes out rises and fades
        const double a = lift * (0.4 + 0.6 * std::sin(kPi * frac(ph * 2 + i / 3.0)));
        const double x = -0.14 + i * 0.14, y = -0.12 - 0.26 * lift - 0.05 * (i % 2);
        if (i == 1) d.fill(starP(x, y - 0.04, 5, 0.08, 0.035), hx("#ffd23f", static_cast<float>(a)));
        else d.fill(heartP(x, y, 0.1), hx("#ff4d6d", static_cast<float>(a)));
    }
    d.lin(rrect(-0.28, -0.04, 0.56, 0.38, 0.03), {-0.28, 0}, {0.28, 0}, hx("#4dabff"), hx("#1f6fe0"));
    d.fill(rrect(-0.04, -0.04, 0.08, 0.38, 0), hx("#ffd23f"));
    const D lid = d.at(Affine::translate(0, -0.06 - 0.16 * lift) * Affine::rotate(-0.25 * lift));
    lid.lin(rrect(-0.32, -0.08, 0.64, 0.1, 0.03), {-0.32, 0}, {0.32, 0}, hx("#6fc0ff"), hx("#2f80f0"));
    lid.fill(rrect(-0.04, -0.08, 0.08, 0.1, 0), hx("#ffd23f"));
    for (double sx : {-1.0, 1.0}) lid.at(Affine::translate(sx * 0.07, -0.12) * Affine::rotate(sx * 0.5)).fill(ell(0, 0, 0.08, 0.045), hx("#ffd23f"));
}

void cupcakeCandle(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.1));
    d.lin(poly({{-0.24, 0.06}, {0.24, 0.06}, {0.18, 0.36}, {-0.18, 0.36}}), {-0.24, 0}, {0.24, 0}, hx("#ff8fab"), hx("#e0507a"));
    for (int i = 0; i < 5; ++i) d.stroke(line({-0.18 + i * 0.09, 0.08}, {-0.14 + i * 0.07, 0.34}), 0.012, hx("#ffffff", 0.4f));
    d.fill(Path().moveTo(-0.28, 0.08).cubicTo(-0.34, -0.12, -0.1, -0.2, 0, -0.16).cubicTo(0.1, -0.2, 0.34, -0.12, 0.28, 0.08).close(), hx("#fff3f6"));
    for (int i = 0; i < 6; ++i) d.fill(rrect(-0.18 + i * 0.07, -0.06 + (i % 2) * 0.06, 0.03, 0.012, 0.006), hx(i % 2 ? "#3ec1d3" : "#ffd23f"));
    d.fill(rrect(-0.025, -0.36, 0.05, 0.2, 0.01), hx("#9b5de5"));
    d.rad(circ(0, -0.42, 0.1), {0, -0.42}, 0.1, hx("#ffd27a", 0.45f), hx("#ffd27a", 0));
    flame(d, 0, -0.38, t, 1.0);
}

void santa(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.04 + 0.01 * wave(t, 2.0)) * Affine::rotate(0.05 * wave(t, 3.0)));
    d.fill(Path().moveTo(-0.3, 0.0).cubicTo(-0.36, 0.42, 0.36, 0.42, 0.3, 0.0).close(), hx("#ffffff"));
    d.fill(ell(0, -0.02, 0.24, 0.22), hx("#ffd7b8"));
    d.fill(Path().moveTo(-0.28, -0.12).cubicTo(-0.2, -0.46, 0.2, -0.5, 0.3, -0.3).quadTo(0.4, -0.2, 0.42, -0.1).lineTo(0.3, -0.1).close(), hx("#e0001b"));
    d.fill(circ(0.42, -0.1, 0.06), hx("#ffffff"));
    d.fill(rrect(-0.32, -0.16, 0.64, 0.1, 0.05), hx("#ffffff"));
    dotEyes(d.at(Affine::translate(0, -0.04) * Affine::scale(0.8)), Animal::Bear, t);
    d.fill(circ(0, 0.06, 0.04), hx("#ff8f8f"));
    for (double sx : {-1.0, 1.0}) d.fill(Path().moveTo(0, 0.12).cubicTo(sx * 0.08, 0.06, sx * 0.18, 0.08, sx * 0.2, 0.14).cubicTo(sx * 0.12, 0.13, sx * 0.06, 0.15, 0, 0.14).close(), hx("#ffffff"));
    blush(d, 0.04, 0.16, 0.5f);
}

void reindeer(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08) * Affine::rotate(0.05 * wave(t, 2.6)));
    for (double sx : {-1.0, 1.0}) {
        Path a;
        a.moveTo(sx * 0.1, -0.24).quadTo(sx * 0.22, -0.42, sx * 0.3, -0.46);
        a.moveTo(sx * 0.17, -0.36).lineTo(sx * 0.08, -0.46);
        a.moveTo(sx * 0.24, -0.41).lineTo(sx * 0.36, -0.36);
        d.stroke(a, 0.04, hx("#8a5226"));
        d.at(Affine::translate(sx * 0.3, -0.12) * Affine::rotate(sx * (0.8 + 0.1 * wave(t, 1.6)))).fill(ell(0, 0, 0.1, 0.05), hx("#b07a44"));
    }
    d.rad(ell(0, 0.02, 0.27, 0.3), {-0.1, -0.1}, 0.4, hx("#c8915a"), hx("#9a6a3a"));
    d.fill(ell(0, 0.16, 0.17, 0.12), hx("#e8c49a"));
    dotEyes(d, Animal::Bear, t);
    const float glow = static_cast<float>(0.5 + 0.5 * wave(t, 1.0));
    d.fill(circ(0, 0.12, 0.1), hx("#ff3b3b", 0.3f * glow));
    d.rad(circ(0, 0.12, 0.065), {-0.02, 0.1}, 0.07, hx("#ff8a8a"), hx("#e0001b"));
    mouthSmile(d, 0.25, 0.04);
}

void gingerbread(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.1 * wave(t, 1.4)));
    const Color c = hx("#c8783a");
    d.fill(circ(0, -0.26, 0.16), c);
    d.fill(rrect(-0.16, -0.14, 0.32, 0.36, 0.1), c);
    for (double sx : {-1.0, 1.0}) {
        const D arm = d.at(Affine::translate(sx * 0.13, -0.08) * Affine::rotate(sx * (-0.45 + 0.15 * wave(t, 0.7))));
        arm.fill(rrect(sx > 0 ? -0.03 : -0.2, -0.055, 0.23, 0.11, 0.055), c);
        Path zz;
        zz.moveTo(sx * 0.13, -0.04);
        for (int k = 1; k <= 4; ++k) zz.lineTo(sx * (0.13 + (k % 2) * 0.025), -0.04 + k * 0.02);
        arm.stroke(zz, 0.014, hx("#ffffff"));
        d.at(Affine::translate(sx * 0.1, 0.2) * Affine::rotate(sx * -0.2)).fill(rrect(-0.06, 0, 0.12, 0.24, 0.06), c);
    }
    Path icing;
    icing.moveTo(-0.08, -0.2).quadTo(0, -0.14, 0.08, -0.2);
    d.stroke(icing, 0.02, hx("#ffffff"));
    for (double sx : {-1.0, 1.0}) d.fill(circ(sx * 0.06, -0.3, 0.025), hx("#ffffff"));
    for (int i = 0; i < 3; ++i) d.fill(circ(0, -0.06 + i * 0.09, 0.03), hx(i == 1 ? "#3fae4f" : "#e0001b"));
}

void candyCane(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.2 + 0.08 * wave(t, 2.2)));
    Path cane;
    cane.moveTo(0.05, 0.42).lineTo(0.05, -0.18);
    for (int i = 0; i <= 12; ++i) {
        const double a = kPi + kPi * i / 12.0;
        cane.lineTo(-0.07 + 0.12 * std::cos(a) * -1, -0.18 + 0.14 * std::sin(a));
    }
    d.stroke(cane, 0.11, hx("#ffffff"));
    for (int i = 0; i < 6; ++i) d.stroke(line({0.0, 0.36 - i * 0.1}, {0.1, 0.32 - i * 0.1}), 0.035, hx("#e0001b"));
    d.at(Affine::translate(-0.07, -0.18)).stroke(arc(0, 0, 0.12, 0.14, kPi * 1.25, kPi * 1.45), 0.035, hx("#e0001b"));
    d.at(Affine::translate(-0.07, -0.18)).stroke(arc(0, 0, 0.12, 0.14, kPi * 1.65, kPi * 1.85), 0.035, hx("#e0001b"));
    d.fill(ell(0.0, 0.2, 0.012, 0.15), hx("#ffffff", 0.5f));
}

void stocking(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, -0.42) * Affine::rotate(0.12 * wave(t, 2.0)) * Affine::translate(0, 0.42));
    Path sock;
    sock.moveTo(-0.16, -0.24).lineTo(0.12, -0.24).lineTo(0.12, 0.08).quadTo(0.12, 0.16, 0.22, 0.18).quadTo(0.38, 0.21, 0.38, 0.33)
        .quadTo(0.38, 0.46, 0.22, 0.46).lineTo(0.02, 0.46).quadTo(-0.16, 0.46, -0.16, 0.28).close();
    d.fill(sock, hx("#e0001b"));
    d.fill(Path().moveTo(0.26, 0.19).quadTo(0.38, 0.22, 0.38, 0.33).quadTo(0.38, 0.46, 0.24, 0.46).quadTo(0.3, 0.33, 0.26, 0.19).close(), hx("#ffffff"));
    d.fill(Path().moveTo(-0.16, 0.24).quadTo(-0.16, 0.46, 0.02, 0.46).quadTo(-0.06, 0.36, -0.16, 0.24).close(), hx("#ffffff"));
    d.fill(rrect(-0.2, -0.36, 0.36, 0.14, 0.06), hx("#ffffff"));
    for (int i = 0; i < 3; ++i) d.fill(starP(-0.04, -0.08 + i * 0.12, 5, 0.04, 0.018), hx("#ffd23f"));
    d.stroke(Path().moveTo(-0.16, -0.36).quadTo(-0.1, -0.48, -0.02, -0.36), 0.02, hx("#ffd23f"));
}

void ornament(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, -0.48) * Affine::rotate(0.15 * wave(t, 2.2)) * Affine::translate(0, 0.48));
    d.stroke(line({0, -0.48}, {0, -0.24}), 0.012, hx("#ffd23f"));
    d.fill(rrect(-0.06, -0.26, 0.12, 0.07, 0.015), hx("#d9b54a"));
    d.rad(circ(0, 0.08, 0.3), {-0.1, -0.02}, 0.42, hx("#ff6b8a"), hx("#a8002a"));
    for (int i = 0; i < 2; ++i) {
        const double y = 0.0 + i * 0.16;
        Path z;
        z.moveTo(-0.29, y);
        for (int k = 1; k <= 8; ++k) z.lineTo(-0.29 + k * 0.0725, y + (k % 2 ? -0.04 : 0.0));
        d.stroke(z, 0.025, hx("#ffd23f"));
    }
    d.fill(ell(-0.12, -0.06, 0.06, 0.09), hx("#ffffff", 0.35f));
    const double g = std::pow(0.5 + 0.5 * wave(t, 2.0), 8);
    d.fill(starP(-0.13, -0.08, 4, 0.08 * g + 0.005, 0.02), hx("#ffffff", static_cast<float>(g)));
}

void jingleBells(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08) * Affine::scale(1.3));
    for (double sx : {-1.0, 1.0}) {
        const D b = d.at(Affine::translate(sx * 0.04, -0.24) * Affine::rotate(sx * 0.35 + 0.18 * wave(t, 0.8, sx > 0 ? 0.5 : 0)) * Affine::translate(0, 0.3));
        b.lin(Path().moveTo(-0.14, 0.08).cubicTo(-0.14, -0.2, 0.14, -0.2, 0.14, 0.08).lineTo(0.18, 0.12).lineTo(-0.18, 0.12).close(), {-0.14, 0}, {0.14, 0},
              hx("#ffe48a"), hx("#d99a10"));
        b.fill(circ(0, 0.15, 0.04), hx("#b07a10"));
    }
    for (double sx : {-1.0, 1.0}) d.at(Affine::translate(sx * 0.07, -0.3) * Affine::rotate(sx * 0.6)).fill(ell(0, 0, 0.1, 0.05), hx("#3fae4f"));
    d.at(Affine::translate(0, -0.26)).fill(circ(0, 0, 0.06), hx("#e0001b"));
    sparkleAround(d.at(Affine::translate(0, 0.0)), t, 5, 0.32, 0.24);
}

void wreath(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.04 * wave(t, 3.0)));
    for (int i = 0; i < 18; ++i) {
        const double a = i * kTau / 18;
        d.at(Affine::translate(0.3 * std::cos(a), 0.3 * std::sin(a)) * Affine::rotate(a + 0.6)).fill(ell(0, 0, 0.1, 0.05), hx(i % 2 ? "#2e8b4f" : "#3fae4f"));
    }
    for (int i = 0; i < 6; ++i) {
        const double a = i * kTau / 6 + 0.3, tw = 0.5 + 0.5 * wave(t, 1.4, i / 6.0);
        d.fill(circ(0.3 * std::cos(a), 0.3 * std::sin(a), 0.035), mix(hx("#e0001b"), hx("#ff8a8a"), static_cast<float>(tw * 0.5)));
    }
    const D bow = d.at(Affine::translate(0, 0.3));
    for (double sx : {-1.0, 1.0}) {
        bow.at(Affine::rotate(sx * 0.4)).fill(ell(sx * 0.08, 0, 0.09, 0.05), hx("#e0001b"));
        bow.at(Affine::rotate(sx * (0.3 + 0.05 * wave(t, 1.2)))).fill(rrect(sx * 0.02 - 0.02, 0, 0.04, 0.16, 0.01), hx("#c8001b"));
    }
    bow.fill(circ(0, 0, 0.035), hx("#ff3b3b"));
}

void xmasGifts(const D& d0) {
    const double t = d0.t;
    auto box = [&](double x, double y, double w, double h, const char* c, const char* rib, double bob) {
        const D b = d0.at(Affine::translate(x, y + bob));
        b.fill(rrect(-w / 2, -h / 2, w, h, 0.02), hx(c));
        b.fill(rrect(-0.025, -h / 2, 0.05, h, 0), hx(rib));
        b.fill(rrect(-w / 2, -0.025, w, 0.05, 0), hx(rib));
        for (double sx : {-1.0, 1.0}) b.at(Affine::translate(sx * 0.04, -h / 2 - 0.02) * Affine::rotate(sx * 0.5)).fill(ell(0, 0, 0.05, 0.03), hx(rib));
    };
    box(-0.2, 0.24, 0.4, 0.34, "#e0001b", "#ffd23f", 0);
    box(0.22, 0.27, 0.32, 0.28, "#3fae4f", "#ffffff", 0);
    box(0.0, -0.14, 0.3, 0.3, "#4d9bff", "#ff4d6d", -0.015 * std::abs(wave(t, 1.0)));
    sparkleAround(d0, t, 3, 0.42, 0.38);
}

void merryXmas(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.06) * Affine::scale(1 + 0.025 * wave(t, 1.6)));
    title(d, "Merry", 0, -0.13, 0.26, 0.7, "pacifico", hx("#e0001b"));
    title(d, "Christmas", 0, 0.13, 0.24, 0.92, "pacifico", hx("#2e8b4f"));
    for (int i = 0; i < 6; ++i) {  // a few snowflakes drift by
        const double ph = frac(t / 3.0 + i / 6.0);
        const double x = -0.44 + i * 0.17 + 0.03 * wave(t, 1.8, i * 0.2), y = -0.46 + 0.92 * ph;
        if (std::abs(y) < 0.28 && std::abs(x) < 0.4) continue;
        d0.fill(circ(x, y, 0.016), hx("#ffffff", static_cast<float>(std::sin(kPi * ph))));
    }
}

void giangSinh(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.025 * wave(t, 1.6)));
    title(d, "Giáng sinh", 0, -0.1, 0.24, 0.9, "lobster", hx("#e0001b"));
    title(d, "an lành", 0, 0.14, 0.22, 0.7, "lobster", hx("#2e8b4f"));
    const D h = d0.at(Affine::translate(0.3, -0.32) * Affine::rotate(0.4 + 0.08 * wave(t, 1.8)));
    h.fill(Path().moveTo(-0.1, 0.04).cubicTo(-0.06, -0.12, 0.08, -0.14, 0.14, -0.04).lineTo(0.08, 0.04).close(), hx("#e0001b"));
    h.fill(rrect(-0.12, 0.02, 0.22, 0.05, 0.025), hx("#ffffff"));
    h.fill(circ(0.15, -0.04, 0.03), hx("#ffffff"));
}

void snowGlobe(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(0.04 * wave(t, 3.0)));
    d.fill(Path().moveTo(-0.28, 0.32).lineTo(0.28, 0.32).lineTo(0.22, 0.46).lineTo(-0.22, 0.46).close(), hx("#8a5226"));
    d.fill(circ(0, -0.04, 0.38), hx("#bfe9ff", 0.35f));
    d.fill(ell(0, 0.24, 0.3, 0.06), hx("#ffffff"));
    d.fill(poly({{-0.12, 0.22}, {0.12, 0.22}, {0, -0.06}}), hx("#2e8b4f"));
    d.fill(poly({{-0.09, 0.08}, {0.09, 0.08}, {0, -0.18}}), hx("#3fae4f"));
    d.fill(starP(0, -0.2, 5, 0.04, 0.018), hx("#ffd23f"));
    for (int i = 0; i < 14; ++i) {  // snow swirls inside the glass
        const double ph = frac(t / 3.0 + i / 14.0);
        const double ang = i * 2.4 + t * 0.6;
        const double r = 0.3 * std::sqrt(frac(i * 0.618));
        const double x = r * std::cos(ang), y = -0.04 - 0.3 + 0.55 * ph;
        if (x * x + (y + 0.04) * (y + 0.04) > 0.33 * 0.33) continue;
        d.fill(circ(x, y, 0.014), hx("#ffffff", static_cast<float>(std::sin(kPi * ph))));
    }
    d.stroke(circ(0, -0.04, 0.38), 0.02, hx("#ffffff", 0.8f));
    d.fill(ell(-0.18, -0.2, 0.05, 0.1), hx("#ffffff", 0.45f));
}

void heartArrow(const D& d0) {
    const double t = d0.t;
    const double beat = 1 + 0.08 * std::max(0.0, wave(t, 0.8));
    d0.at(Affine::scale(beat)).rad(heartP(0, 0.02, 0.62), {-0.12, -0.12}, 0.5, hx("#ff6b8a"), hx("#c8002a"));
    d0.fill(ell(-0.14, -0.1, 0.06, 0.04), hx("#ffffff", 0.5f));
    const D a = d0.at(Affine::rotate(-0.5));
    a.stroke(line({-0.46, 0}, {0.42, 0}), 0.025, hx("#8a5226"));
    a.fill(poly({{0.42, -0.05}, {0.5, 0}, {0.42, 0.05}}), hx("#9aa3b5"));
    for (int i = 0; i < 2; ++i) a.fill(poly({{-0.46 + i * 0.05, 0}, {-0.42 + i * 0.05, -0.05}, {-0.38 + i * 0.05, -0.05}, {-0.42 + i * 0.05, 0}}), hx("#ff4d6d"));
}

void loveLetter(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08) * Affine::rotate(-0.06 + 0.05 * wave(t, 2.6)));
    const double open = 0.5 + 0.5 * wave(t, 3.0);
    d.fill(rrect(-0.38, -0.22, 0.76, 0.48, 0.03), hx("#ffe0ea"));
    const D card = d.at(Affine::translate(0, -0.14 * open));
    card.fill(rrect(-0.3, -0.24, 0.6, 0.3, 0.02), hx("#ffffff"));
    card.fill(heartP(0, -0.1, 0.12), hx("#ff4d6d"));
    d.fill(poly({{-0.38, -0.2}, {0, 0.06}, {0.38, -0.2}, {0.38, 0.26}, {-0.38, 0.26}}), hx("#ffc2d4"));
    d.stroke(poly({{-0.38, 0.26}, {-0.04, 0.02}}), 0.012, hx("#ff8fab"));
    d.stroke(poly({{0.38, 0.26}, {0.04, 0.02}}), 0.012, hx("#ff8fab"));
    d.fill(heartP(0, 0.06, 0.13), hx("#e0001b"));
    floatHearts(d0, t, 0.36, -0.2);
}

void happyValentine(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.06) * Affine::scale(1 + 0.03 * wave(t, 1.4)));
    title(d, "Happy", 0, -0.13, 0.24, 0.6, "pacifico", hx("#ff4f9a"));
    title(d, "Valentine", 0, 0.12, 0.24, 0.9, "pacifico", hx("#e0001b"));
    for (int i = 0; i < 3; ++i) {
        const double ph = frac(t / 2.0 + i / 3.0), a = std::sin(kPi * ph);
        d0.at(Affine::translate(-0.36 + 0.36 * i, 0.4 - 0.12 * ph)).fill(heartP(0, 0, 0.09), hx("#ff4d6d", static_cast<float>(a)));
    }
}

// A flower on a stem (tulip / rose) for the women's days.
void tulip(const D& d, double x, double y, double ang, const char* c1, const char* c2, double t) {
    const D f = d.at(Affine::translate(x, y) * Affine::rotate(ang + 0.05 * wave(t, 2.4, x)));
    f.stroke(Path().moveTo(0, 0).quadTo(0.02, 0.2, 0, 0.42), 0.025, hx("#3fae4f"));
    f.at(Affine::translate(0.03, 0.24) * Affine::rotate(0.7)).fill(ell(0, 0, 0.09, 0.03), hx("#3fae4f"));
    f.lin(Path().moveTo(-0.08, -0.12).lineTo(-0.04, -0.04).lineTo(0, -0.12).lineTo(0.04, -0.04).lineTo(0.08, -0.12).quadTo(0.1, 0.02, 0, 0.03).quadTo(-0.1, 0.02, -0.08, -0.12).close(),
          {0, -0.12}, {0, 0.03}, hx(c1), hx(c2));
}

void womenDay(const D& d0, const char* day, bool rose) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(-0.14, 0.04));
    if (rose) {
        const D r = d.at(Affine::rotate(0.05 * wave(t, 2.4)));
        r.stroke(Path().moveTo(0, -0.1).quadTo(0.03, 0.15, 0, 0.42), 0.025, hx("#3fae4f"));
        r.at(Affine::translate(0.03, 0.18) * Affine::rotate(0.7)).fill(ell(0, 0, 0.1, 0.04), hx("#3fae4f"));
        r.rad(circ(0, -0.18, 0.16), {-0.04, -0.22}, 0.18, hx("#ff6b8a"), hx("#c8002a"));
        for (int i = 0; i < 3; ++i) r.stroke(arc(0, -0.18, 0.05 + i * 0.035, 0.05 + i * 0.035, i * 1.2, i * 1.2 + 2.6), 0.014, hx("#8a0020", 0.6f));
    } else {
        tulip(d, -0.1, -0.12, -0.25, "#ff8fab", "#e0507a", t);
        tulip(d, 0.0, -0.2, 0.0, "#ffd23f", "#ff8a1f", t);
        tulip(d, 0.1, -0.12, 0.25, "#c9a2ff", "#7c3aed", t);
        d.fill(poly({{-0.14, 0.16}, {0.14, 0.16}, {0.05, 0.36}, {-0.05, 0.36}}), hx("#ffc2d4"));
        d.fill(rrect(-0.06, 0.2, 0.12, 0.04, 0.02), hx("#ff4d6d"));
    }
    title(d0, day, 0.24, 0.12, 0.24, 0.48, "lobster", hx("#ff4f9a"));
    sparkleAround(d0, t, 3, 0.42, 0.4, "#ff8fab");
}

void teacherDay(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.02 * wave(t, 2.0)));
    d.fill(rrect(-0.36, 0.0, 0.5, 0.1, 0.02), hx("#4d9bff"));
    d.fill(rrect(-0.33, -0.1, 0.46, 0.1, 0.02), hx("#ff8a1f"));
    d.fill(rrect(-0.35, -0.2, 0.48, 0.1, 0.02), hx("#3fae4f"));
    for (int i = 0; i < 3; ++i) d.fill(rrect(-0.33 + (i % 2) * 0.02, -0.17 + i * 0.1, 0.42, 0.02, 0.01), hx("#ffffff", 0.7f));
    const D a = d.at(Affine::translate(0.24, -0.08) * Affine::rotate(0.1 * wave(t, 1.8)));
    a.rad(Path().moveTo(0, -0.08).cubicTo(0.16, -0.16, 0.18, 0.1, 0, 0.12).cubicTo(-0.18, 0.1, -0.16, -0.16, 0, -0.08).close(), {-0.04, -0.04}, 0.16,
          hx("#ff6b6b"), hx("#c8102e"));
    a.stroke(line({0, -0.08}, {0.02, -0.15}), 0.015, hx("#8a5226"));
    a.fill(ell(0.06, -0.14, 0.05, 0.025), hx("#3fae4f"));
    title(d0, "20/11", 0, 0.24, 0.17, 0.5, "bungee", hx("#e0501a"));
    title(d0, "Tri ân thầy cô", 0, 0.4, 0.1, 0.8, "be-vietnam-pro-extrabold", hx("#1f5fe0"), hx("#ffffff"), 0.035);
}

void childrenDay(const D& d0) {
    const double t = d0.t;
    const D k = d0.at(Affine::translate(0.06 + 0.04 * wave(t, 3.0), -0.12 + 0.03 * wave(t, 2.0)) * Affine::rotate(0.2 * wave(t, 2.6)));
    k.fill(poly({{0, -0.28}, {0.18, -0.04}, {0, 0.22}, {-0.18, -0.04}}), hx("#ff4d6d"));
    k.fill(poly({{0, -0.28}, {0.18, -0.04}, {0, -0.04}}), hx("#ffd23f"));
    k.fill(poly({{0, 0.22}, {-0.18, -0.04}, {0, -0.04}}), hx("#4d9bff"));
    Path tail;
    tail.moveTo(0, 0.22);
    for (int i = 1; i <= 10; ++i) tail.lineTo(0.03 * std::sin(i * 0.9 + t * 4), 0.22 + i * 0.022);
    k.stroke(tail, 0.012, hx("#ffffff"));
    for (int i = 0; i < 3; ++i) k.at(Affine::translate(0.03 * std::sin((3 + i * 3) * 0.9 + t * 4), 0.28 + i * 0.066) * Affine::rotate(0.8)).fill(ell(0, 0, 0.03, 0.015), hx(i % 2 ? "#ffd23f" : "#ff4d6d"));
    d0.stroke(Path().moveTo(-0.3, 0.44).quadTo(-0.2, 0.1, 0.06, -0.04), 0.008, hx("#ffffff", 0.7f));
    title(d0, "1/6", -0.24, 0.28, 0.24, 0.4, "bungee", hx("#ff8a1f"));
}

void midAutumn(const D& d0) {
    const double t = d0.t;
    d0.fill(circ(0.04, -0.06, 0.36), hx("#fff3b0", 0.25f));
    d0.rad(circ(0.04, -0.06, 0.3), {-0.06, -0.16}, 0.36, hx("#fffbe0"), hx("#ffd23f"));
    for (int i = 0; i < 3; ++i) d0.fill(circ(-0.06 + i * 0.12, -0.12 + (i % 2) * 0.12, 0.03 + 0.01 * i), hx("#f0c030", 0.5f));
    // A rabbit sits and looks up at the moon, ears twitching.
    const D r = d0.at(Affine::translate(-0.22, 0.26));
    for (double sx : {-1.0, 1.0}) r.at(Affine::translate(sx * 0.04, -0.12) * Affine::rotate(sx * 0.15 + (sx > 0 ? 0.1 * std::max(0.0, wave(t, 2.0)) : 0))).fill(ell(0, -0.08, 0.03, 0.09), hx("#ffffff"));
    r.fill(ell(0, -0.06, 0.09, 0.08), hx("#ffffff"));
    r.fill(ell(0.02, 0.06, 0.13, 0.11), hx("#ffffff"));
    r.fill(circ(0.04, -0.07, 0.015), hx("#1c1c22"));
    const D l = d0.at(Affine::translate(0.28, 0.12) * Affine::rotate(0.12 * wave(t, 2.0)));
    l.stroke(line({0, -0.2}, {0, -0.1}), 0.012, hx("#ffd23f"));
    l.rad(ell(0, 0, 0.1, 0.12), {0, 0}, 0.14, hx("#ffd27a"), hx("#e0001b"));
    l.fill(rrect(-0.05, -0.13, 0.1, 0.03, 0.01), hx("#d9a020"));
    l.fill(rrect(-0.05, 0.1, 0.1, 0.03, 0.01), hx("#d9a020"));
    l.stroke(line({0, 0.13}, {0, 0.22}), 0.012, hx("#ffd23f"));
    title(d0, "Trung thu", 0.06, 0.4, 0.15, 0.6, "lobster", hx("#e0501a"));
}

void bat(const D& d0) {
    const double t = d0.t;
    const double flap = std::sin(t * 7);
    const D d = d0.at(Affine::translate(0.03 * wave(t, 2.5), 0.04 * wave(t, 1.3)));
    for (double sx : {-1.0, 1.0}) {
        const D w = d.at(Affine::scale(sx, 1) * Affine::rotate(-0.25 * flap));
        w.fill(Path().moveTo(0.06, -0.04).cubicTo(0.2, -0.22, 0.38, -0.16, 0.46, -0.06).quadTo(0.38, -0.02, 0.36, 0.06).quadTo(0.3, 0.0, 0.24, 0.08).quadTo(0.18, 0.0, 0.1, 0.08).close(),
               hx("#3a2a4a"));
    }
    const D b = d.at(Affine::scale(1.45));
    b.fill(ell(0, 0.02, 0.1, 0.12), hx("#4a3a5a"));
    for (double sx : {-1.0, 1.0}) b.fill(poly({{sx * 0.03, -0.08}, {sx * 0.09, -0.08}, {sx * 0.08, -0.17}}), hx("#4a3a5a"));
    for (double sx : {-1.0, 1.0}) b.fill(circ(sx * 0.035, -0.01, 0.024 * blink(t, 3.1)), hx("#ffd23f"));
    b.fill(poly({{-0.02, 0.05}, {-0.01, 0.08}, {0.0, 0.05}}), hx("#ffffff"));
    b.fill(poly({{0.0, 0.05}, {0.01, 0.08}, {0.02, 0.05}}), hx("#ffffff"));
}

void witchHat(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::translate(0, 0.08) * Affine::rotate(0.08 * wave(t, 2.0)));
    d.fill(ell(0, 0.24, 0.46, 0.1), hx("#3a2a5a"));
    d.fill(Path().moveTo(-0.22, 0.24).lineTo(0.22, 0.24).lineTo(0.06, -0.2).quadTo(0.1, -0.36, 0.26, -0.4).quadTo(0.04, -0.38, -0.02, -0.24).close(), hx("#4a3a7a"));
    d.fill(Path().moveTo(-0.2, 0.18).lineTo(0.2, 0.18).lineTo(0.18, 0.1).lineTo(-0.18, 0.1).close(), hx("#ff8a1f"));
    d.fill(rrect(-0.05, 0.1, 0.1, 0.08, 0.01), hx("#ffd23f"));
    d.fill(rrect(-0.03, 0.12, 0.06, 0.04, 0.005), hx("#ff8a1f"));
    sparkleAround(d0, t, 3, 0.4, 0.36, "#c9a2ff");
}

void gradCap(const D& d0) {
    const double t = d0.t;
    const double ph = frac(t / 2.4), toss = std::sin(kPi * ph);  // tossed up, comes back down
    const D d = d0.at(Affine::translate(0, 0.12 - 0.2 * toss) * Affine::rotate(0.5 * std::sin(kTau * ph)));
    d.fill(Path().moveTo(-0.18, 0.02).lineTo(0.18, 0.02).lineTo(0.18, 0.14).quadTo(0, 0.2, -0.18, 0.14).close(), hx("#2a2e38"));
    d.fill(poly({{0, -0.2}, {0.4, -0.04}, {0, 0.1}, {-0.4, -0.04}}), hx("#1c1f26"));
    d.fill(circ(0, -0.05, 0.025), hx("#ffd23f"));
    const double sw = 0.2 * std::sin(t * 3);
    d.stroke(Path().moveTo(0, -0.05).lineTo(0.28, 0.02).lineTo(0.28 + 0.03 * sw, 0.2), 0.014, hx("#ffd23f"));
    d.at(Affine::translate(0.28 + 0.03 * sw, 0.22)).fill(rrect(-0.025, 0, 0.05, 0.08, 0.01), hx("#ffd23f"));
    sparkleAround(d0, t, 4, 0.42, 0.4);
}

void diploma(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::rotate(-0.2 + 0.05 * wave(t, 2.6)));
    d.lin(rrect(-0.38, -0.1, 0.76, 0.2, 0.1), {0, -0.1}, {0, 0.1}, hx("#fff8e6"), hx("#e8d8b0"));
    d.fill(ell(-0.38, 0, 0.05, 0.1), hx("#e0d0a0"));
    d.fill(ell(0.38, 0, 0.05, 0.1), hx("#f5ecd0"));
    d.fill(rrect(-0.05, -0.105, 0.1, 0.21, 0), hx("#e0001b"));
    for (double sx : {-1.0, 1.0}) d.at(Affine::translate(sx * 0.0, 0.1) * Affine::rotate(sx * 0.3 + 0.08 * wave(t, 1.4))).fill(poly({{-0.03, 0}, {0.03, 0}, {0.04, 0.18}, {0, 0.15}, {-0.04, 0.18}}), hx("#c8001b"));
    title(d0, "Tốt nghiệp", 0, -0.3, 0.16, 0.8, "lobster", hx("#1f5fe0"));
    sparkleAround(d0, t, 3, 0.42, 0.4);
}

void weddingRings(const D& d0) {
    const double t = d0.t;
    const double sw = 0.06 * wave(t, 2.4);
    for (double sx : {-1.0, 1.0}) {
        const D r = d0.at(Affine::translate(sx * 0.12, 0.08) * Affine::rotate(sx * (0.3 + sw)));
        r.stroke(ell(0, 0, 0.2, 0.22), 0.06, hx("#e8b93a"));
        r.stroke(arc(0, 0, 0.2, 0.22, kPi * 1.1, kPi * 1.5), 0.02, hx("#fff3b0"));
    }
    const D g = d0.at(Affine::translate(-0.12, -0.18));
    g.fill(poly({{-0.07, 0}, {0.07, 0}, {0, 0.08}}), hx("#bfe9ff"));
    g.fill(poly({{-0.07, 0}, {-0.04, -0.05}, {0.04, -0.05}, {0.07, 0}}), hx("#e6f8ff"));
    const double gl = std::pow(0.5 + 0.5 * wave(t, 1.8), 6);
    d0.fill(starP(-0.06, -0.26, 4, 0.1 * gl + 0.005, 0.02), hx("#ffffff", static_cast<float>(gl)));
    floatHearts(d0, t, 0.34, -0.18);
}

void weddingText(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.025 * wave(t, 1.8)));
    title(d, "Trăm năm", 0, -0.12, 0.2, 0.8, "lobster", hx("#d6005a"));
    title(d, "hạnh phúc", 0, 0.12, 0.2, 0.86, "lobster", hx("#d6005a"));
    for (double sx : {-1.0, 1.0}) d0.at(Affine::translate(sx * 0.38, 0.34) * Affine::scale(1 + 0.08 * std::max(0.0, wave(t, 0.9)))).fill(heartP(0, 0, 0.12), hx("#ff4d6d"));
}

void newYear(const D& d0) {
    const double t = d0.t;
    const D d = d0.at(Affine::scale(1 + 0.025 * wave(t, 1.6)));
    title(d, "Happy", 0, -0.2, 0.2, 0.6, "pacifico", hx("#ffb000"), hx("#1c1c22"), 0.05);
    title(d, "New Year", 0, 0.04, 0.22, 0.9, "pacifico", hx("#ffd23f"), hx("#1c1c22"), 0.05);
    title(d, "2027", 0, 0.3, 0.17, 0.5, "bungee", hx("#ff4d6d"));
    sparkleAround(d0, t, 6, 0.44, 0.42);
}

void loveMom(const D& d0) {
    const double t = d0.t;
    const double beat = 1 + 0.06 * std::max(0.0, wave(t, 0.9));
    d0.at(Affine::translate(0, 0.02) * Affine::scale(beat)).rad(heartP(0, 0, 0.84), {-0.15, -0.15}, 0.6, hx("#ffb0c8"), hx("#ff4f8a"));
    title(d0, "Yêu mẹ", -0.04, 0.04, 0.2, 0.5, "lobster", hx("#ffffff"), hx("#d6005a"), 0.04);
    tulip(d0, 0.34, -0.3, 0.24, "#ffd23f", "#ff8a1f", t);
}

}  // namespace

void addExtraStickers(const Add& add) {
    const char* G = "Game 8-bit";
    add("px_coin", "Đồng xu", G, pxCoin);
    add("px_star", "Ngôi sao", G, pxStar);
    add("px_gem", "Viên ngọc", G, pxGem);
    add("px_key", "Chìa khóa", G, pxKey);
    add("px_chest", "Rương báu", G, pxChest);
    add("px_sword", "Thanh kiếm", G, pxSword);
    add("px_potion", "Bình thuốc", G, pxPotion);
    add("px_rocket", "Tên lửa", G, pxRocket);
    add("px_cat", "Mèo pixel", G, pxCat);
    add("px_bomb", "Quả bom", G, pxBomb);
    add("px_ufo", "Đĩa bay", G, pxUfo);
    add("px_joystick", "Cần điều khiển", G, pxJoystick);
    add("px_lives", "Mạng chơi", G, pxLives);
    add("px_press_start", "PRESS START", G, [](const D& d) { pxBadge(d, "PRESS START", "#ffffff", 1); });
    add("px_ready", "READY?", G, [](const D& d) { pxBadge(d, "READY?", "#4dd8ff", 2); });
    add("px_high_score", "Điểm cao", G, pxScore);
    add("px_plus100", "+100 điểm", G, pxPlus);
    add("px_ko", "K.O.", G, pxKo);
    add("px_win", "YOU WIN", G, pxWin);

    const char* E = "Thập niên 80";
    add("retro_sun", "Hoàng hôn neon", E, synthSun);
    add("neon_80s", "Chữ neon 80s", E, neon80s);
    add("retro_tv", "TV đời cũ", E, retroTv);
    add("boombox", "Đài cassette", E, boombox);
    add("cassette", "Băng cassette", E, cassette);
    add("floppy", "Đĩa mềm", E, floppy);
    add("disco_ball", "Quả cầu disco", E, discoBall);

    const char* R = "Động vật biểu cảm";
    add("cat_wow", "Mèo WOW", R, catWow);
    add("dog_omg", "Cún OMG", R, dogOmg);
    add("cat_love", "Mèo mê mẩn", R, catLove);
    add("cat_kiss", "Mèo hôn gió", R, catKiss);
    add("dog_laugh", "Cún cười chảy nước mắt", R, dogLaugh);
    add("hamster_cry", "Chuột hamster khóc", R, hamsterCry);
    add("cat_angry", "Mèo giận", R, catAngry);
    add("cat_sleepy", "Mèo buồn ngủ", R, catSleepy);
    add("dog_cool", "Cún ngầu", R, dogCool);
    add("dog_party", "Cún tiệc tùng", R, dogParty);
    add("dog_think", "Cún suy nghĩ", R, dogThink);
    add("bunny_shy", "Thỏ ngại ngùng", R, bunnyShy);
    add("panda_sweat", "Gấu trúc toát mồ hôi", R, pandaSweat);
    add("bear_hi", "Gấu vẫy chào", R, bearHi);
    add("frog_happy", "Ếch vui vẻ", R, frogHappy);
    add("chick_dance", "Gà con nhảy múa", R, chickDance);

    const char* L = "Đồ uống & đời sống";
    add("phin", "Cà phê phin", L, phin);
    add("iced_coffee", "Cà phê sữa đá", L, icedCoffee);
    add("latte", "Cà phê latte", L, latte);
    add("tea_cup", "Tách trà", L, teaCup);
    add("iced_tea", "Trà đá chanh", L, icedTea);
    add("matcha", "Matcha latte", L, matcha);
    add("orange_juice", "Nước cam", L, orangeJuice);
    add("coconut", "Nước dừa", L, coconut);
    add("cheers", "Cụng ly", L, cheers);
    add("banh_mi", "Bánh mì", L, banhMi);
    add("pho", "Tô phở", L, pho);
    add("laptop", "Máy tính", L, laptop);
    add("book_glasses", "Sách và kính", L, bookGlasses);
    add("headphones", "Tai nghe", L, headphones);
    add("alarm_clock", "Đồng hồ báo thức", L, alarmClock);
    add("plant", "Chậu cây", L, plant);
    add("umbrella", "Chiếc ô", L, umbrella);

    const char* B = "Sinh nhật";
    add("bday_banner", "Sinh nhật vui vẻ", B, bdayBanner);
    add("bday_script", "Happy Birthday", B, bdayScript);
    add("party_hat", "Mũ tiệc", B, partyHat);
    add("cake_slice", "Miếng bánh kem", B, cakeSlice);
    add("candles", "Nến sinh nhật", B, candles);
    add("gift_open", "Hộp quà mở", B, giftOpen);
    add("cupcake_candle", "Bánh nến", B, cupcakeCandle);

    const char* X = "Giáng sinh";
    add("santa", "Ông già Noel", X, santa);
    add("reindeer", "Tuần lộc", X, reindeer);
    add("gingerbread", "Bánh gừng", X, gingerbread);
    add("candy_cane", "Kẹo gậy", X, candyCane);
    add("stocking", "Tất Noel", X, stocking);
    add("ornament", "Quả châu", X, ornament);
    add("jingle_bells", "Chuông Noel", X, jingleBells);
    add("wreath", "Vòng hoa", X, wreath);
    add("xmas_gifts", "Quà Noel", X, xmasGifts);
    add("merry_xmas", "Merry Christmas", X, merryXmas);
    add("giang_sinh", "Giáng sinh an lành", X, giangSinh);
    add("snow_globe", "Quả cầu tuyết", X, snowGlobe);

    const char* Y = "Ngày lễ trong năm";
    add("valentine_arrow", "Trái tim mũi tên", Y, heartArrow);
    add("love_letter", "Thư tình", Y, loveLetter);
    add("happy_valentine", "Happy Valentine", Y, happyValentine);
    add("women_83", "Mùng 8/3", Y, [](const D& d) { womenDay(d, "8/3", false); });
    add("women_2010", "Ngày 20/10", Y, [](const D& d) { womenDay(d, "20/10", true); });
    add("teacher_2011", "Tri ân 20/11", Y, teacherDay);
    add("children_16", "Quốc tế thiếu nhi 1/6", Y, childrenDay);
    add("mid_autumn", "Trung thu", Y, midAutumn);
    add("bat", "Dơi Halloween", Y, bat);
    add("witch_hat", "Mũ phù thủy", Y, witchHat);
    add("grad_cap", "Mũ tốt nghiệp", Y, gradCap);
    add("diploma", "Bằng tốt nghiệp", Y, diploma);
    add("wedding_rings", "Nhẫn cưới", Y, weddingRings);
    add("wedding_text", "Trăm năm hạnh phúc", Y, weddingText);
    add("new_year", "Happy New Year", Y, newYear);
    add("love_mom", "Yêu mẹ", Y, loveMom);
}

}  // namespace vstudio::stickers::kit
