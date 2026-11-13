#include "vstudio/text.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

#include "vstudio/fonts.hpp"
#include "vstudio/motion.hpp"

namespace vstudio::text {

using vec::Affine;
using vec::Color;
using vec::Mask;
using vec::Paint;
using vec::Path;
using vec::Pt;

namespace {

constexpr double kPi = std::numbers::pi;

Json obj(const Json& c, const char* key) {
    auto it = c.find(key);
    return it != c.end() && it->is_object() ? *it : Json::object();
}

Color colorOf(const Json& j, const char* key, const char* def, float alpha = 1) {
    return Color::hex(j.value(key, std::string(def)), alpha);
}

struct Glyph {
    std::shared_ptr<const Font> font;
    int glyph = 0;
    double x = 0, y = 0;  // origin, em
    double adv = 0;
    Pt centre;            // em
    int index = 0;        // among visible glyphs
};

struct Layout {
    std::vector<Glyph> glyphs;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // em
    int visible = 0;
};

Layout layout(const Json& c) {
    Layout L;
    auto font = Font::get(c.value("font", std::string(defaultFontId())));
    auto fallback = Font::get(defaultFontId());
    const std::u32string text = decodeUtf8(c.value("text", std::string{}));
    const double spacing = c.value("letter_spacing", 0.0);
    const double lh = std::max(0.5, c.value("line_height", 1.25));
    const std::string align = c.value("align", std::string("center"));
    std::vector<std::u32string> lines(1);
    for (char32_t ch : text) {
        if (ch == U'\n') lines.emplace_back();
        else if (ch != U'\r') lines.back().push_back(ch);
    }
    const double n = static_cast<double>(lines.size());
    const double mid = (font->ascent() + font->descent()) / 2;
    L.y0 = -n * lh / 2;
    L.y1 = n * lh / 2;
    L.x0 = 1e9;
    L.x1 = -1e9;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        const double cy = L.y0 + (static_cast<double>(li) + 0.5) * lh, base = cy - mid;
        std::vector<Glyph> row;
        double pen = 0;
        const Font* prevFont = nullptr;
        int prev = 0;
        for (char32_t ch : lines[li]) {
            Glyph g;
            g.font = font;
            g.glyph = font->glyph(ch);
            if (g.glyph == 0 && ch > U' ') {
                if (const int fg = fallback->glyph(ch)) {
                    g.font = fallback;
                    g.glyph = fg;
                }
            }
            if (prevFont == g.font.get()) pen += g.font->kerning(prev, g.glyph);
            g.x = pen;
            g.y = base;
            g.adv = g.font->advance(g.glyph);
            g.centre = {pen + g.adv / 2, cy};
            pen += g.adv + spacing;
            prevFont = g.font.get();
            prev = g.glyph;
            if (ch == U' ' || ch == U'\t' || ch == 0x00A0 || ch == 0x3000) continue;  // nothing to draw
            row.push_back(std::move(g));
        }
        const double width = std::max(0.0, pen - (lines[li].empty() ? 0 : spacing));
        const double shift = align == "left" ? 0 : align == "right" ? -width : -width / 2;
        for (auto& g : row) {
            g.x += shift;
            g.centre.x += shift;
            g.index = L.visible++;
            L.glyphs.push_back(std::move(g));
        }
        // The block is as wide as its widest line, laid out around the same anchor.
        L.x0 = std::min(L.x0, shift);
        L.x1 = std::max(L.x1, shift + width);
    }
    if (L.x1 < L.x0) L.x0 = L.x1 = 0;
    if (L.x1 - L.x0 < 0.3) {  // empty: keep a small box so the clip can still be grabbed
        const double m = (L.x0 + L.x1) / 2;
        L.x0 = m - 0.15;
        L.x1 = m + 0.15;
    }
    return L;
}

Affine inverse(const Affine& m) {
    const double det = m.a * m.d - m.b * m.c;
    if (std::abs(det) < 1e-12) return {};
    const double ia = m.d / det, ib = -m.b / det, ic = -m.c / det, id = m.a / det;
    return {ia, ib, ic, id, -(ia * m.e + ic * m.f), -(ib * m.e + id * m.f)};
}

// Multiplies the mask by a soft wipe across the block box (block coordinates via `toBlock`).
void applyWipe(Mask& m, int wipe, double reveal, const Affine& toBlock, double x0, double y0, double x1, double y1) {
    if (m.empty() || wipe == 0 || reveal >= 1) return;
    constexpr double soft = 0.25;
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            const Pt p = toBlock.apply({m.x0 + x + 0.5, m.y0 + y + 0.5});
            const double u = wipe == 1 ? (p.x - x0) / std::max(1e-9, x1 - x0) : (y1 - p.y) / std::max(1e-9, y1 - y0);
            double k = std::clamp((reveal * (1 + soft) - u) / soft, 0.0, 1.0);
            k = k * k * (3 - 2 * k);
            m.v[static_cast<std::size_t>(y) * m.w + x] *= static_cast<float>(k);
        }
}

Mask unionBox(const std::vector<std::array<double, 4>>& boxes, int pad, int fw, int fh) {
    Mask m;
    if (boxes.empty()) return m;
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (const auto& b : boxes) {
        x0 = std::min(x0, b[0]);
        y0 = std::min(y0, b[1]);
        x1 = std::max(x1, b[2]);
        y1 = std::max(y1, b[3]);
    }
    m.x0 = std::max(0, static_cast<int>(std::floor(x0)) - pad);
    m.y0 = std::max(0, static_cast<int>(std::floor(y0)) - pad);
    m.w = std::max(0, std::min(fw, static_cast<int>(std::ceil(x1)) + pad) - m.x0);
    m.h = std::max(0, std::min(fh, static_cast<int>(std::ceil(y1)) + pad) - m.y0);
    m.v.assign(static_cast<std::size_t>(std::max(0, m.w)) * std::max(0, m.h), 0.f);
    return m;
}

}  // namespace

const std::vector<Preset>& textPresets() {
    static const std::vector<Preset> kPresets = [] {
        auto none = [] {
            return Json{{"color2", nullptr},
                        {"stroke", {{"color", "#000000"}, {"width", 0.0}}},
                        {"shadow", {{"color", "#000000"}, {"opacity", 0.0}, {"blur", 0.06}, {"x", 0.03}, {"y", 0.05}}},
                        {"glow", {{"color", "#ffffff"}, {"opacity", 0.0}, {"radius", 0.25}}},
                        {"box", {{"color", "#000000"}, {"opacity", 0.0}, {"padding", 0.3}, {"radius", 0.2}}}};
        };
        auto with = [&](std::initializer_list<std::pair<const char*, Json>> kv) {
            Json j = none();
            for (const auto& [k, v] : kv) {
                if (j.contains(k) && j[k].is_object() && v.is_object()) j[k].update(v);
                else j[k] = v;
            }
            return j;
        };
        return std::vector<Preset>{
            {"plain", "Cơ bản", with({{"color", "#ffffff"}})},
            {"outline", "Viền đen", with({{"color", "#ffffff"}, {"stroke", {{"color", "#000000"}, {"width", 0.07}}}})},
            {"shadow", "Bóng đổ", with({{"color", "#ffffff"}, {"shadow", {{"opacity", 0.75}, {"blur", 0.08}}}})},
            {"label_dark", "Nhãn đen", with({{"color", "#ffffff"}, {"box", {{"opacity", 0.65}}}})},
            {"label_light", "Nhãn trắng",
             with({{"color", "#1b1b1f"}, {"box", {{"color", "#ffffff"}, {"opacity", 0.92}}}})},
            {"subtitle", "Phụ đề",
             with({{"color", "#ffffff"}, {"box", {{"opacity", 0.45}, {"padding", 0.2}, {"radius", 0.08}}}})},
            {"yellow", "Vàng viền", with({{"color", "#ffd53d"}, {"stroke", {{"color", "#141414"}, {"width", 0.08}}}})},
            {"neon_pink", "Neon hồng",
             with({{"color", "#fff2fb"}, {"glow", {{"color", "#ff3fb4"}, {"opacity", 1.0}, {"radius", 0.3}}}})},
            {"neon_blue", "Neon xanh",
             with({{"color", "#effbff"}, {"glow", {{"color", "#2fc6ff"}, {"opacity", 1.0}, {"radius", 0.3}}}})},
            {"sunset", "Hoàng hôn",
             with({{"color", "#ffcf4a"}, {"color2", "#ff4f7b"}, {"shadow", {{"opacity", 0.55}, {"blur", 0.1}}}})},
            {"ocean", "Biển xanh",
             with({{"color", "#7ef9ff"}, {"color2", "#2a6dff"}, {"stroke", {{"color", "#ffffff"}, {"width", 0.05}}}})},
            {"candy", "Kẹo ngọt",
             with({{"color", "#ff7eb6"},
                   {"stroke", {{"color", "#ffffff"}, {"width", 0.1}}},
                   {"shadow", {{"color", "#7a2a55"}, {"opacity", 0.6}, {"blur", 0.04}, {"x", 0.05}, {"y", 0.07}}}})},
            {"comic", "Truyện tranh",
             with({{"font", "bungee"},
                   {"color", "#ffe14d"},
                   {"stroke", {{"color", "#111111"}, {"width", 0.1}}},
                   {"shadow", {{"color", "#111111"}, {"opacity", 1.0}, {"blur", 0.0}, {"x", 0.06}, {"y", 0.08}}}})},
            {"handwritten", "Viết tay",
             with({{"font", "patrick-hand"}, {"color", "#ffffff"}, {"shadow", {{"opacity", 0.6}, {"blur", 0.06}}}})},
            {"gold_script", "Chữ ký vàng",
             with({{"font", "pacifico"},
                   {"color", "#fff1a8"},
                   {"color2", "#d99a22"},
                   {"shadow", {{"color", "#3a2400"}, {"opacity", 0.6}, {"blur", 0.08}}}})},
            {"retro", "Cổ điển",
             with({{"font", "lobster"}, {"color", "#e8473d"}, {"stroke", {{"color", "#fff4dc"}, {"width", 0.07}}}})},
        };
    }();
    return kPresets;
}

Json defaultTextContent(const std::string& t) {
    Json c{{"kind", "text"},       {"text", t},     {"font", defaultFontId()}, {"size", 96.0},
           {"color", "#ffffff"},   {"align", "center"}, {"italic", false},     {"letter_spacing", 0.0},
           {"line_height", 1.25},  {"rotation", 0.0}};
    c.update(textPresets()[1].style);  // white with a thin outline: readable on any picture
    c["in"] = {{"type", "fade"}, {"duration", 0.4}};
    c["out"] = {{"type", "fade"}, {"duration", 0.4}};
    c["loop"] = {{"type", ""}, {"speed", 1.0}};
    return c;
}

bool textStill(const Json& c, double local, double length) { return motion::timing(c, local, length).still(); }

std::array<double, 4> textBounds(const Json& c) {
    const Layout L = layout(c);
    const double size = std::max(1.0, c.value("size", 96.0));
    double pad = 0;
    const Json box = obj(c, "box"), stroke = obj(c, "stroke"), glow = obj(c, "glow");
    if (box.value("opacity", 0.0) > 0) pad = std::max(pad, box.value("padding", 0.3));
    pad = std::max(pad, stroke.value("width", 0.0));
    if (glow.value("opacity", 0.0) > 0) pad = std::max(pad, glow.value("radius", 0.25));
    const double x0 = (L.x0 - pad) * size, x1 = (L.x1 + pad) * size, y0 = (L.y0 - pad) * size, y1 = (L.y1 + pad) * size;
    const double r = c.value("rotation", 0.0) * kPi / 180;
    if (r == 0) return {x0, y0, x1, y1};
    const Affine m = Affine::rotate(r);
    std::array<double, 4> b{1e300, 1e300, -1e300, -1e300};
    for (const Pt p : {Pt{x0, y0}, Pt{x1, y0}, Pt{x0, y1}, Pt{x1, y1}}) {
        const Pt q = m.apply(p);
        b = {std::min(b[0], q.x), std::min(b[1], q.y), std::max(b[2], q.x), std::max(b[3], q.y)};
    }
    return b;
}

void drawText(const Json& c, double local, double length, const Affine& place, VideoFrame& out) {
    if (out.empty()) return;
    const Layout L = layout(c);
    const double size = std::max(1.0, c.value("size", 96.0));
    const motion::Timing T = motion::timing(c, local, length);
    const unsigned seed = static_cast<unsigned>(std::hash<std::string>{}(c.value("text", std::string{})));

    // Whole-block and per-letter poses.
    auto per = [](const std::string& id, bool loop) {
        const motion::Info* i = loop ? motion::findLoop(id) : motion::findAnimation(id);
        return i && i->perLetter;
    };
    const int n = L.visible;
    motion::Pose whole;
    std::vector<motion::Pose> letters(L.glyphs.size());
    if (!T.in.empty() && T.inProgress < 1) {
        if (per(T.in, false))
            for (const auto& g : L.glyphs) letters[g.index] = letters[g.index] * motion::inPose(T.in, T.inProgress, g.index, n, seed);
        else whole = whole * motion::inPose(T.in, T.inProgress, 0, 1, seed);
    }
    if (!T.out.empty() && T.outProgress > 0) {
        if (per(T.out, false))
            for (const auto& g : L.glyphs)
                letters[g.index] = letters[g.index] * motion::inPose(T.out, 1 - T.outProgress, g.index, n, seed + 7u);
        else whole = whole * motion::inPose(T.out, 1 - T.outProgress, 0, 1, seed + 7u);
    }
    if (!T.loop.empty() && T.loopAmount > 0) {
        if (per(T.loop, true))
            for (const auto& g : L.glyphs)
                letters[g.index] = letters[g.index] * motion::loopPose(T.loop, T.seconds, T.loopSpeed, g.index, n, T.loopAmount);
        else whole = whole * motion::loopPose(T.loop, T.seconds, T.loopSpeed, 0, 1, T.loopAmount);
    }
    if (whole.opacity <= 0.002) return;

    // Block space: em units, origin at the block centre.
    const Affine base = place * Affine::rotate(c.value("rotation", 0.0) * kPi / 180) * Affine::scale(size);
    const Affine W = base * whole.transform({0, 0}, 1);
    const double unit = size * place.scaleFactor();  // device px per em (at rest)
    const double wUnit = unit * std::sqrt(std::abs(whole.sx * whole.sy));
    const Affine shear = c.value("italic", false) ? Affine{1, 0, -0.2, 1, 0, 0} : Affine{};

    vec::Canvas canvas(out);
    const Json strokeJ = obj(c, "stroke"), shadowJ = obj(c, "shadow"), glowJ = obj(c, "glow"), boxJ = obj(c, "box");
    const double strokeW = std::max(0.0, strokeJ.value("width", 0.0));
    const bool hasStroke = strokeW > 0.001;

    // Coverage of the letters (and of their outlines), letter opacity baked in.
    bool uniform = true;
    double meanOpacity = 0;
    for (std::size_t i = 0; i < letters.size(); ++i) {
        uniform = uniform && std::abs(letters[i].opacity - 1) < 1e-6 && letters[i].blur <= 0;
        meanOpacity += letters[i].opacity;
    }
    meanOpacity = letters.empty() ? 1 : meanOpacity / static_cast<double>(letters.size());
    auto glyphMatrix = [&](const Glyph& g) {
        return W * letters[g.index].transform(g.centre, 1) * Affine::translate(g.x, g.y) * shear;
    };
    Mask fill, line;
    if (uniform) {
        Path all;
        for (const auto& g : L.glyphs) all.append(g.font->outline(g.glyph), glyphMatrix(g));
        fill = canvas.fillMask(all);
        if (hasStroke) line = canvas.strokeMask(all, 2 * strokeW * wUnit);
    } else {
        std::vector<std::array<double, 4>> boxes;
        const int pad = static_cast<int>(std::ceil((strokeW + 0.6) * wUnit)) + 2;
        for (const auto& g : L.glyphs) boxes.push_back(g.font->outline(g.glyph).bounds(glyphMatrix(g)));
        fill = unionBox(boxes, pad, out.width, out.height);
        if (hasStroke) line = fill;
        for (const auto& g : L.glyphs) {
            const auto& lp = letters[g.index];
            if (lp.opacity <= 0.002) continue;
            const Affine m = glyphMatrix(g);
            const double blurPx = lp.blur * wUnit;
            auto add = [&](Mask target, Mask& into) {
                if (blurPx >= 1) {
                    target = target.padded(static_cast<int>(std::ceil(blurPx * 1.5)));
                    target.blur(blurPx);
                }
                into.maxWith(target, static_cast<float>(lp.opacity));
            };
            add(canvas.fillMask(g.font->outline(g.glyph), m), fill);
            // Width in em: `m` already scales the outline to pixels.
            if (hasStroke) add(canvas.strokeMask(g.font->outline(g.glyph), 2 * strokeW, m), line);
        }
    }
    // Whole-block blur (blur in, zoom blur).
    const double blockBlur = whole.blur * wUnit;
    auto blurred = [&](Mask& m) {
        if (blockBlur >= 1 && !m.empty()) {
            m = m.padded(static_cast<int>(std::ceil(blockBlur * 1.5)));
            m.blur(blockBlur);
        }
    };
    blurred(fill);
    blurred(line);
    const Affine toBlock = inverse(W);
    applyWipe(fill, whole.wipe, whole.reveal, toBlock, L.x0, L.y0, L.x1, L.y1);
    applyWipe(line, whole.wipe, whole.reveal, toBlock, L.x0, L.y0, L.x1, L.y1);

    const float op = static_cast<float>(whole.opacity);
    // Background box.
    if (const double bo = boxJ.value("opacity", 0.0); bo > 0) {
        const double pad = boxJ.value("padding", 0.3), r = boxJ.value("radius", 0.2);
        Path box;
        box.roundRect(L.x0 - pad, L.y0 - pad * 0.5, L.x1 - L.x0 + 2 * pad, L.y1 - L.y0 + pad, r);
        Mask bm = canvas.fillMask(box, W);
        blurred(bm);
        applyWipe(bm, whole.wipe, whole.reveal, toBlock, L.x0, L.y0, L.x1, L.y1);
        Paint p = Paint::solid(colorOf(boxJ, "color", "#000000"));
        p.opacity = static_cast<float>(bo * whole.opacity * meanOpacity);
        canvas.drawMask(bm, p);
    }
    // Everything the letters cover, for the glow and the shadow.
    Mask shape = fill;
    if (!line.empty()) shape.maxWith(line);
    if (const double go = glowJ.value("opacity", 0.0); go > 0 && !shape.empty()) {
        const double r = std::max(1.0, glowJ.value("radius", 0.25) * wUnit);
        Mask g = shape.padded(static_cast<int>(std::ceil(r * 1.6)));
        g.blur(r);
        for (float& v : g.v) v = std::min(1.f, v * 1.8f);  // a bright halo, not a faint one
        Paint p = Paint::solid(colorOf(glowJ, "color", "#ffffff"));
        p.opacity = static_cast<float>(go) * op;
        canvas.drawMask(g, p);
    }
    if (const double so = shadowJ.value("opacity", 0.0); so > 0 && !shape.empty()) {
        const double r = shadowJ.value("blur", 0.06) * wUnit;
        Mask s = shape.padded(static_cast<int>(std::ceil(std::max(0.0, r) * 1.6)) + 1);
        if (r >= 1) s.blur(r);
        Paint p = Paint::solid(colorOf(shadowJ, "color", "#000000"));
        p.opacity = static_cast<float>(so) * op;
        // Offsets follow the text's rotation and scale.
        const Pt o0 = W.apply({0, 0}), o1 = W.apply({shadowJ.value("x", 0.03), shadowJ.value("y", 0.05)});
        canvas.drawMask(s, p, static_cast<int>(std::lround(o1.x - o0.x)), static_cast<int>(std::lround(o1.y - o0.y)));
    }
    if (hasStroke) {
        Paint p = Paint::solid(colorOf(strokeJ, "color", "#000000"));
        p.opacity = op;
        canvas.drawMask(line, p);
    }
    Paint fp = Paint::solid(colorOf(c, "color", "#ffffff"));
    if (auto it = c.find("color2"); it != c.end() && it->is_string() && !it->get<std::string>().empty())
        fp = Paint::linear(W.apply({0, L.y0}), W.apply({0, L.y1}), colorOf(c, "color", "#ffffff"),
                           Color::hex(it->get<std::string>()));
    fp.opacity = op;
    canvas.drawMask(fill, fp);
}

}  // namespace vstudio::text
