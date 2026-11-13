#include "vstudio/cover.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "vstudio/keyframes.hpp"
#include "vstudio/pixel.hpp"

namespace vstudio {

const std::vector<CoverStyle>& coverStyles() {
    static const std::vector<CoverStyle> styles = {
        // Làm mờ
        {"blur", "Mờ nhẹ", "Làm mờ"},
        {"blur_strong", "Mờ mạnh", "Làm mờ"},
        {"frosted", "Kính mờ", "Làm mờ"},
        {"frosted_dark", "Kính mờ tối", "Làm mờ"},
        {"motion", "Mờ chuyển động", "Làm mờ"},
        {"zoom_blur", "Mờ phóng", "Làm mờ"},
        {"glow", "Mờ sáng", "Làm mờ"},
        {"bokeh", "Mờ bokeh", "Làm mờ"},
        // Khảm
        {"mosaic", "Ô vuông nhỏ", "Khảm"},
        {"mosaic_large", "Ô vuông lớn", "Khảm"},
        {"dots", "Chấm tròn", "Khảm"},
        {"hex", "Lục giác", "Khảm"},
        {"crystal", "Pha lê", "Khảm"},
        {"triangles", "Tam giác", "Khảm"},
        {"glass_blocks", "Khối kính", "Khảm"},
        {"halftone", "In lưới", "Khảm"},
        // Che kín
        {"black", "Thanh đen", "Che kín"},
        {"white", "Thanh trắng", "Che kín"},
        {"color", "Màu tùy chọn", "Che kín"},
        {"gradient", "Chuyển màu", "Che kín"},
        {"stripes", "Sọc cảnh báo", "Che kín"},
        {"checker", "Ô cờ", "Che kín"},
        // Họa tiết (moving)
        {"noise", "Nhiễu TV", "Họa tiết", true},
        {"glitch", "Nhiễu sọc", "Họa tiết", true},
        {"sparkle", "Lấp lánh sao", "Họa tiết", true},
        {"hearts", "Trái tim bay", "Họa tiết", true},
        {"ripple", "Gợn nước", "Họa tiết", true},
        {"snow", "Tuyết rơi", "Họa tiết", true},
        // Nghệ thuật
        {"posterize", "Áp phích", "Nghệ thuật"},
        {"sketch", "Phác họa", "Nghệ thuật"},
        {"emboss", "Chạm nổi", "Nghệ thuật"},
        {"thermal", "Ảnh nhiệt", "Nghệ thuật"},
        {"invert", "Âm bản", "Nghệ thuật"},
        {"night", "Nhìn đêm", "Nghệ thuật"},
        {"neon", "Viền neon", "Nghệ thuật"},
        {"oil", "Sơn dầu", "Nghệ thuật"},
    };
    return styles;
}

const CoverStyle* findCoverStyle(const std::string& id) {
    for (const auto& s : coverStyles())
        if (s.id == id) return &s;
    return nullptr;
}

const std::vector<CoverShape>& coverShapes() {
    static const std::vector<CoverShape> shapes = {
        {"rect", "Chữ nhật"}, {"rounded", "Bo góc"},  {"ellipse", "Elip"},   {"circle", "Tròn"},  {"diamond", "Thoi"},
        {"triangle", "Tam giác"}, {"hexagon", "Lục giác"}, {"star", "Ngôi sao"}, {"heart", "Trái tim"},
    };
    return shapes;
}

bool isCoverShape(const std::string& id) {
    return std::any_of(coverShapes().begin(), coverShapes().end(), [&](const CoverShape& s) { return s.id == id; });
}

const std::vector<std::string>& coverAnimatable() {
    static const std::vector<std::string> props = {"x", "y", "w", "h", "rotation", "amount"};
    return props;
}

Json coverParamsAt(const Json& params, Time sourceTime) {
    Json out = params;
    if (!out.is_object()) return Json::object();
    if (auto it = out.find("anim"); it != out.end()) {
        const Json anim = *it;
        out.erase(it);
        if (anim.is_object())
            for (const auto& prop : coverAnimatable()) {
                if (!anim.contains(prop)) continue;
                const KeyframeCurve curve(anim[prop]);
                if (!curve.empty()) out[prop] = curve.at(sourceTime);
            }
    }
    return out;
}

Json defaultCover(const std::string& style, const std::string& shape) {
    return Json{{"x", 0.35}, {"y", 0.35}, {"w", 0.3}, {"h", 0.3}, {"rotation", 0.0}, {"shape", shape},
                {"style", style}, {"amount", 0.5}, {"feather", 0.15}, {"invert", false}, {"color", "#000000"}};
}

namespace {

constexpr double kPi = std::numbers::pi;

struct Px {
    float r = 0, g = 0, b = 0, a = 1;
};
Px operator+(Px a, Px b) { return {a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a}; }
Px operator*(Px a, float k) { return {a.r * k, a.g * k, a.b * k, a.a * k}; }
Px mix(Px a, Px b, float t) { return a * (1 - t) + b * t; }
float lum(Px p) { return 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b; }

// Float RGBA working image (only the covered area plus a margin).
struct Img {
    int w = 0, h = 0;
    std::vector<Px> p;
    Img() = default;
    Img(int w_, int h_) : w(w_), h(h_), p(static_cast<std::size_t>(w_) * h_) {}
    Px& at(int x, int y) { return p[static_cast<std::size_t>(y) * w + x]; }
    [[nodiscard]] Px get(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return p[static_cast<std::size_t>(y) * w + x];
    }
    [[nodiscard]] Px bilinear(double x, double y) const {
        x -= 0.5;
        y -= 0.5;
        const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        const float fx = static_cast<float>(x - x0), fy = static_cast<float>(y - y0);
        return mix(mix(get(x0, y0), get(x0 + 1, y0), fx), mix(get(x0, y0 + 1), get(x0 + 1, y0 + 1), fx), fy);
    }
};

// Running-sum box blur, one direction, edge clamped.
void boxBlur(Img& img, int r, bool horizontal) {
    if (r < 1) return;
    const int n = horizontal ? img.w : img.h, lines = horizontal ? img.h : img.w;
    std::vector<Px> line(static_cast<std::size_t>(n));
    const float inv = 1.0f / static_cast<float>(2 * r + 1);
    for (int l = 0; l < lines; ++l) {
        auto px = [&](int i) -> Px& { return horizontal ? img.at(i, l) : img.at(l, i); };
        auto src = [&](int i) { return line[static_cast<std::size_t>(std::clamp(i, 0, n - 1))]; };
        for (int i = 0; i < n; ++i) line[static_cast<std::size_t>(i)] = px(i);
        Px sum;
        sum.a = 0;
        for (int i = -r; i <= r; ++i) sum = sum + src(i);
        for (int i = 0; i < n; ++i) {
            px(i) = sum * inv;
            const Px add = src(i + r + 1), sub = src(i - r);
            sum = sum + add + sub * -1.0f;
        }
    }
}

// Three box passes ~ Gaussian.
Img blurred(const Img& src, double radius, bool h = true, bool v = true) {
    Img out = src;
    const int r = std::max(1, static_cast<int>(std::lround(radius / 1.7)));
    for (int pass = 0; pass < 3; ++pass) {
        if (h) boxBlur(out, r, true);
        if (v) boxBlur(out, r, false);
    }
    return out;
}

float hash(int x, int y, int seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u +
                      static_cast<std::uint32_t>(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000);
}

Px parseColor(const Json& p, const char* key, Px fallback) {
    const std::string s = p.value(key, std::string{});
    if (s.size() != 7 || s[0] != '#') return fallback;
    try {
        const unsigned long v = std::stoul(s.substr(1), nullptr, 16);
        return {static_cast<float>((v >> 16) & 255) / 255.f, static_cast<float>((v >> 8) & 255) / 255.f,
                static_cast<float>(v & 255) / 255.f, 1};
    } catch (...) {
        return fallback;
    }
}

Px hsv(float h, float s, float v) {
    h = h - std::floor(h);
    const float i = std::floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: return {v, t, p, 1};
        case 1: return {q, v, p, 1};
        case 2: return {p, v, t, 1};
        case 3: return {p, q, v, 1};
        case 4: return {t, p, v, 1};
        default: return {v, p, q, 1};
    }
}

// ---- shapes: signed distance in pixels (negative inside) ----

using Pt = std::array<double, 2>;

double sdPolygon(double px, double py, const std::vector<Pt>& v) {
    double d = (px - v[0][0]) * (px - v[0][0]) + (py - v[0][1]) * (py - v[0][1]);
    double s = 1;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i, ++i) {
        const double ex = v[j][0] - v[i][0], ey = v[j][1] - v[i][1];
        const double wx = px - v[i][0], wy = py - v[i][1];
        const double k = std::clamp((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0, 1.0);
        const double bx = wx - ex * k, by = wy - ey * k;
        d = std::min(d, bx * bx + by * by);
        const bool c1 = py >= v[i][1], c2 = py < v[j][1], c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * std::sqrt(d);
}

// Unit outlines (x, y in [-1, 1]).
const std::vector<Pt>& unitShape(const std::string& shape) {
    static const std::vector<Pt> diamond = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static const std::vector<Pt> triangle = {{0, -1}, {1, 1}, {-1, 1}};
    static const std::vector<Pt> hexagon = [] {
        std::vector<Pt> v;
        for (int k = 0; k < 6; ++k) v.push_back({std::cos(k * kPi / 3), std::sin(k * kPi / 3)});
        return v;
    }();
    static const std::vector<Pt> star = [] {
        std::vector<Pt> v;
        for (int k = 0; k < 10; ++k) {
            const double a = -kPi / 2 + k * kPi / 5, r = k % 2 ? 0.45 : 1.0;
            v.push_back({r * std::cos(a), r * std::sin(a) * 1.05 + 0.05});
        }
        return v;
    }();
    static const std::vector<Pt> heart = [] {
        std::vector<Pt> v;
        double y0 = 1e9, y1 = -1e9;
        for (int k = 0; k < 64; ++k) {
            const double t = k * 2 * kPi / 64;
            const double x = std::pow(std::sin(t), 3);
            const double y = -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t));
            v.push_back({x, y});
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
        for (auto& p : v) p[1] = (p[1] - y0) / (y1 - y0) * 2 - 1;
        return v;
    }();
    if (shape == "diamond") return diamond;
    if (shape == "triangle") return triangle;
    if (shape == "hexagon") return hexagon;
    if (shape == "star") return star;
    return heart;
}

struct Region {
    double cx, cy, hw, hh, cosr, sinr, feather;
    std::string shape;
    std::vector<Pt> poly;  // scaled outline for polygon shapes

    [[nodiscard]] double sdf(double px, double py) const {
        const double dx = px - cx, dy = py - cy;
        const double lx = dx * cosr + dy * sinr, ly = -dx * sinr + dy * cosr;
        if (shape == "ellipse") {
            const double k = std::hypot(lx / hw, ly / hh);
            return (k - 1) * std::min(hw, hh);
        }
        if (shape == "circle") return std::hypot(lx, ly) - std::min(hw, hh);
        if (shape == "rect" || shape == "rounded") {
            const double r = shape == "rounded" ? 0.3 * std::min(hw, hh) : 0.0;
            const double qx = std::abs(lx) - (hw - r), qy = std::abs(ly) - (hh - r);
            return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r;
        }
        return sdPolygon(lx, ly, poly);
    }
    // Coverage 0..1 at a pixel centre.
    [[nodiscard]] float mask(double px, double py) const {
        const double d = sdf(px, py), f = feather;
        if (d <= -f) return 1;
        if (d >= f) return 0;
        const double t = (d + f) / (2 * f);
        return static_cast<float>(1 - t * t * (3 - 2 * t));
    }
};

// ---- styles: fill `out` (same size as `src`, the working area) ----

struct Ctx {
    const Img& src;
    double S;        // min(frame w, frame h): sizes scale with it
    double amount;   // 0.4..1.6 multiplier from params.amount
    double t;        // seconds, for moving styles
    int ox, oy;      // working area origin in the frame (grids are anchored to the frame)
    double rcx, rcy;  // region centre in working-area pixels
    const Json& p;
};

double cellSize(const Ctx& c, double k) { return std::max(2.0, k * c.S * c.amount); }

// Sample at grid-cell centres of a blurred copy: one colour per cell.
template <class CellFn>
void cells(const Ctx& c, Img& out, double size, CellFn centre) {
    const Img soft = blurred(c.src, size * 0.5);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const auto [sx, sy] = centre(x + c.ox + 0.5, y + c.oy + 0.5, size);
            out.at(x, y) = soft.bilinear(sx - c.ox, sy - c.oy);
        }
}

Px sobel(const Img& g, int x, int y) {
    auto L = [&](int dx, int dy) { return lum(g.get(x + dx, y + dy)); };
    const float gx = -L(-1, -1) - 2 * L(-1, 0) - L(-1, 1) + L(1, -1) + 2 * L(1, 0) + L(1, 1);
    const float gy = -L(-1, -1) - 2 * L(0, -1) - L(1, -1) + L(-1, 1) + 2 * L(0, 1) + L(1, 1);
    const float e = std::sqrt(gx * gx + gy * gy);
    return {e, e, e, 1};
}

Img styled(const std::string& style, const Ctx& c) {
    const Img& src = c.src;
    Img out(src.w, src.h);
    const int frameIndex = static_cast<int>(std::floor(c.t * 25));
    auto each = [&](auto fn) {
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) out.at(x, y) = fn(x, y, src.get(x, y));
    };
    // Blur family
    if (style == "blur") return blurred(src, 0.045 * c.S * c.amount);
    if (style == "blur_strong") return blurred(src, 0.1 * c.S * c.amount);
    if (style == "frosted" || style == "frosted_dark") {
        const bool dark = style == "frosted_dark";
        Img b = blurred(src, 0.045 * c.S * c.amount);
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) {
                const float n = (hash(x + c.ox, y + c.oy, 7) - 0.5f) * 0.08f;
                Px q = b.at(x, y);
                q = dark ? q * 0.45f : mix(q, Px{1, 1, 1, q.a}, 0.18f);
                out.at(x, y) = {q.r + n, q.g + n, q.b + n, q.a};
            }
        return out;
    }
    if (style == "motion") return blurred(src, 0.1 * c.S * c.amount, true, false);
    if (style == "zoom_blur") {
        const double k = std::min(0.9, 0.6 * c.amount);
        each([&](int x, int y, Px) {
            Px acc;
            acc.a = 0;
            const int n = 20;
            for (int i = 0; i < n; ++i) {
                const double s = 1 - k * i / n;
                acc = acc + src.bilinear(c.rcx + (x + 0.5 - c.rcx) * s, c.rcy + (y + 0.5 - c.rcy) * s);
            }
            return acc * (1.0f / n);
        });
        return out;
    }
    if (style == "glow") {
        Img b = blurred(src, 0.05 * c.S * c.amount);
        each([&](int x, int y, Px) {
            const Px q = b.at(x, y);
            auto sc = [](float v) { return 1 - (1 - v) * (1 - v) * 0.7f; };
            return Px{sc(q.r), sc(q.g), sc(q.b), q.a};
        });
        return out;
    }
    if (style == "bokeh") {
        out = blurred(src, 0.06 * c.S * c.amount);
        const double cell = 0.09 * c.S;
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) {
                const double fx = (x + c.ox) / cell, fy = (y + c.oy) / cell;
                const int gx = static_cast<int>(std::floor(fx)), gy = static_cast<int>(std::floor(fy));
                float add = 0;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i) {
                        const int cx = gx + i, cy = gy + j;
                        if (hash(cx, cy, 3) > 0.55f) continue;
                        const double px = cx + hash(cx, cy, 4), py = cy + hash(cx, cy, 5);
                        const double r = 0.25 + 0.35 * hash(cx, cy, 6);
                        const double d = std::hypot(fx - px, fy - py) / r;
                        if (d < 1) add = std::max(add, static_cast<float>(0.35 * (1 - std::pow(d, 8))));
                    }
                Px& q = out.at(x, y);
                q = mix(q, Px{std::min(1.f, q.r * 1.6f + 0.2f), std::min(1.f, q.g * 1.6f + 0.2f),
                              std::min(1.f, q.b * 1.6f + 0.2f), q.a},
                        add);
            }
        return out;
    }
    // Mosaics
    if (style == "mosaic" || style == "mosaic_large") {
        cells(c, out, cellSize(c, style == "mosaic" ? 0.045 : 0.09), [](double x, double y, double s) {
            return std::pair{(std::floor(x / s) + 0.5) * s, (std::floor(y / s) + 0.5) * s};
        });
        return out;
    }
    if (style == "triangles") {
        cells(c, out, cellSize(c, 0.07), [](double x, double y, double s) {
            const double gx = std::floor(x / s), gy = std::floor(y / s), fx = x / s - gx, fy = y / s - gy;
            const bool upper = fx > fy;
            return std::pair{(gx + (upper ? 0.67 : 0.33)) * s, (gy + (upper ? 0.33 : 0.67)) * s};
        });
        return out;
    }
    if (style == "hex") {
        cells(c, out, cellSize(c, 0.05), [](double x, double y, double s) {
            // Pointy-top hex grid, axial rounding.
            const double q = (std::sqrt(3.0) / 3 * x - y / 3) / s, r = (2.0 / 3 * y) / s;
            double cx = q, cz = r, cy = -q - r;
            double rx = std::round(cx), ry = std::round(cy), rz = std::round(cz);
            const double dx = std::abs(rx - cx), dy = std::abs(ry - cy), dz = std::abs(rz - cz);
            if (dx > dy && dx > dz) rx = -ry - rz;
            else if (dy <= dz) rz = -rx - ry;
            return std::pair{s * std::sqrt(3.0) * (rx + rz / 2), s * 1.5 * rz};
        });
        return out;
    }
    if (style == "crystal") {
        cells(c, out, cellSize(c, 0.06), [](double x, double y, double s) {
            const int gx = static_cast<int>(std::floor(x / s)), gy = static_cast<int>(std::floor(y / s));
            double best = 1e18, bx = x, by = y;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    const double px = (gx + i + hash(gx + i, gy + j, 11)) * s, py = (gy + j + hash(gx + i, gy + j, 12)) * s;
                    const double d = (px - x) * (px - x) + (py - y) * (py - y);
                    if (d < best) best = d, bx = px, by = py;
                }
            return std::pair{bx, by};
        });
        return out;
    }
    if (style == "dots" || style == "halftone") {
        const double s = cellSize(c, style == "dots" ? 0.04 : 0.03);
        const Img soft = blurred(src, s * 0.5);
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) {
                const double fx = x + c.ox + 0.5, fy = y + c.oy + 0.5;
                const double cx = (std::floor(fx / s) + 0.5) * s, cy = (std::floor(fy / s) + 0.5) * s;
                const Px col = soft.bilinear(cx - c.ox, cy - c.oy);
                const double d = std::hypot(fx - cx, fy - cy);
                const double r = style == "dots" ? 0.46 * s : 0.62 * s * std::sqrt(std::max(0.f, 1 - lum(col)));
                const float in = static_cast<float>(std::clamp(r - d + 0.5, 0.0, 1.0));
                out.at(x, y) = style == "dots" ? mix(Px{0.04f, 0.04f, 0.05f, col.a}, col, in)
                                               : mix(Px{0.97f, 0.96f, 0.92f, col.a}, Px{0.08f, 0.08f, 0.1f, col.a}, in);
            }
        return out;
    }
    if (style == "glass_blocks") {
        const double s = cellSize(c, 0.07);
        each([&](int x, int y, Px) {
            const double fx = x + c.ox + 0.5, fy = y + c.oy + 0.5;
            const double gx = std::floor(fx / s), gy = std::floor(fy / s);
            const double cx = (gx + 0.5) * s, cy = (gy + 0.5) * s;
            const double jx = (hash(int(gx), int(gy), 21) - 0.5) * s, jy = (hash(int(gx), int(gy), 22) - 0.5) * s;
            Px q = src.bilinear(cx + (fx - cx) * 0.4 + jx - c.ox, cy + (fy - cy) * 0.4 + jy - c.oy);
            const double edge = std::min({fx - gx * s, (gx + 1) * s - fx, fy - gy * s, (gy + 1) * s - fy});
            if (edge < 1.5) q = mix(q, Px{1, 1, 1, q.a}, 0.35f);
            return q;
        });
        return out;
    }
    // Solid / pattern fills
    if (style == "black" || style == "white" || style == "color") {
        const Px col = style == "black" ? Px{0.02f, 0.02f, 0.02f, 1}
                       : style == "white" ? Px{1, 1, 1, 1}
                                          : parseColor(c.p, "color", Px{0, 0, 0, 1});
        each([&](int, int, Px s) { return Px{col.r, col.g, col.b, s.a}; });
        return out;
    }
    if (style == "gradient") {
        const Px a = parseColor(c.p, "color", Px{0.95f, 0.45f, 0.35f, 1});
        const Px b = parseColor(c.p, "color2", Px{0.35f, 0.3f, 0.9f, 1});
        const double half = std::max(1.0, c.p.value("h", 0.3) * 0.5 * c.S);
        each([&](int, int y, Px s) {
            const float u = static_cast<float>(std::clamp((y + 0.5 - c.rcy) / (2 * half) + 0.5, 0.0, 1.0));
            Px q = mix(a, b, u);
            q.a = s.a;
            return q;
        });
        return out;
    }
    if (style == "stripes" || style == "checker") {
        const bool stripes = style == "stripes";
        const double s = std::max(3.0, (stripes ? 0.035 : 0.03) * c.S * c.amount);
        each([&](int x, int y, Px src_) {
            const double fx = x + c.ox, fy = y + c.oy;
            const bool on = stripes ? static_cast<long long>(std::floor((fx + fy) / s)) % 2 == 0
                                    : (static_cast<long long>(std::floor(fx / s)) + static_cast<long long>(std::floor(fy / s))) % 2 == 0;
            Px q = stripes ? (on ? Px{0.98f, 0.78f, 0.05f, 1} : Px{0.07f, 0.07f, 0.08f, 1})
                           : (on ? Px{0.55f, 0.55f, 0.57f, 1} : Px{0.75f, 0.75f, 0.77f, 1});
            q.a = src_.a;
            return q;
        });
        return out;
    }
    // Moving patterns
    if (style == "noise") {
        const int grain = std::max(1, static_cast<int>(0.004 * c.S * c.amount));
        each([&](int x, int y, Px s) {
            const float n = hash((x + c.ox) / grain, (y + c.oy) / grain, frameIndex);
            const float v = 0.15f + 0.8f * n;
            return Px{v, v, v, s.a};
        });
        return out;
    }
    if (style == "glitch") {
        const Img b = blurred(src, 0.01 * c.S);
        const int slice = static_cast<int>(std::floor(c.t * 12));
        const double band = std::max(2.0, 0.03 * c.S);
        const double shift = 0.08 * c.S * c.amount;
        each([&](int x, int y, Px) {
            const int bi = static_cast<int>(std::floor((y + c.oy) / band));
            const double off = (hash(bi, 0, slice) - 0.5) * 2 * shift;
            const double split = 0.01 * c.S * c.amount;
            const double sx = x + 0.5 + off;
            const Px r = b.bilinear(sx + split, y + 0.5), g = b.bilinear(sx, y + 0.5), bl = b.bilinear(sx - split, y + 0.5);
            const float scan = ((y + c.oy) % 3 == 0) ? 0.8f : 1.0f;
            return Px{r.r * scan, g.g * scan, bl.b * scan, g.a};
        });
        return out;
    }
    if (style == "ripple") {
        const double A = 0.02 * c.S * c.amount, lambda = 0.08 * c.S, w = 2 * kPi * 1.2;
        each([&](int x, int y, Px) {
            const double fx = x + c.ox, fy = y + c.oy;
            const double dx = A * std::sin(2 * kPi * fy / lambda + c.t * w), dy = A * std::cos(2 * kPi * fx / lambda + c.t * w);
            return src.bilinear(x + 0.5 + dx, y + 0.5 + dy);
        });
        return out;
    }
    if (style == "sparkle" || style == "hearts" || style == "snow") {
        out = blurred(src, 0.04 * c.S * c.amount);
        if (style == "hearts") each([&](int x, int y, Px) { return mix(out.at(x, y), Px{1, 0.6f, 0.75f, 1}, 0.25f); });
        const double cell = (style == "snow" ? 0.05 : 0.09) * c.S;
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) {
                double fx = (x + c.ox) / cell, fy = (y + c.oy) / cell;
                if (style == "hearts") fy += c.t * 0.6;  // rising
                if (style == "snow") fy -= c.t * 0.8, fx += 0.15 * std::sin(c.t + fy);
                const int gx = static_cast<int>(std::floor(fx)), gy = static_cast<int>(std::floor(fy));
                const double lx = fx - gx - (0.25 + 0.5 * hash(gx, gy, 31)), ly = fy - gy - (0.25 + 0.5 * hash(gx, gy, 32));
                Px& q = out.at(x, y);
                if (style == "sparkle") {
                    const double tw = std::max(0.0, std::sin(c.t * (2 + 3 * hash(gx, gy, 33)) * kPi + 6.3 * hash(gx, gy, 34)));
                    const double arm = std::exp(-std::abs(lx) * 40) * std::max(0.0, 1 - std::abs(ly) * 3.5) +
                                       std::exp(-std::abs(ly) * 40) * std::max(0.0, 1 - std::abs(lx) * 3.5);
                    const double core = std::exp(-(lx * lx + ly * ly) * 300);
                    const float v = static_cast<float>(std::min(1.0, (arm * 0.9 + core) * tw));
                    q = mix(q, Px{1, 0.96f, 0.8f, q.a}, v);
                } else if (style == "hearts") {
                    if (hash(gx, gy, 35) > 0.6f) continue;
                    const double hx = lx * 5.5, hy = -ly * 5.5 + 0.3;
                    const double f = std::pow(hx * hx + hy * hy - 1, 3) - hx * hx * hy * hy * hy;
                    if (f < 0) q = mix(q, Px{0.96f, 0.25f, 0.45f, q.a}, 0.9f);
                } else {
                    const double r = 0.06 + 0.06 * hash(gx, gy, 36);
                    const double d = std::hypot(lx, ly);
                    if (d < r) q = mix(q, Px{1, 1, 1, q.a}, static_cast<float>(std::min(1.0, (r - d) / r * 3)));
                }
            }
        return out;
    }
    // Artistic
    if (style == "posterize" || style == "oil") {
        const bool oil = style == "oil";
        const Img b = blurred(src, (oil ? 0.006 : 0.004) * c.S * c.amount);
        const float levels = oil ? 6 : 4;
        each([&](int x, int y, Px) {
            Px q = b.get(x, y);
            if (oil) {
                const float l = lum(q);
                q = {l + (q.r - l) * 1.3f, l + (q.g - l) * 1.3f, l + (q.b - l) * 1.3f, q.a};
            }
            auto qz = [&](float v) { return std::round(std::clamp(v, 0.f, 1.f) * (levels - 1)) / (levels - 1); };
            return Px{qz(q.r), qz(q.g), qz(q.b), q.a};
        });
        return out;
    }
    if (style == "sketch") {
        each([&](int x, int y, Px s) {
            const float e = std::min(1.f, sobel(src, x, y).r * 2.5f * static_cast<float>(c.amount));
            const float v = 0.97f - e * 0.85f;
            return Px{v, v, v * 0.98f, s.a};
        });
        return out;
    }
    if (style == "emboss") {
        each([&](int x, int y, Px s) {
            const float v = std::clamp(0.5f + (lum(src.get(x + 1, y + 1)) - lum(src.get(x - 1, y - 1))) * 2.5f, 0.f, 1.f);
            return Px{v, v, v, s.a};
        });
        return out;
    }
    if (style == "thermal") {
        each([&](int, int, Px s) {
            const float l = std::clamp(lum(s), 0.f, 1.f);
            static const std::array<Px, 6> pal = {Px{0, 0, 0.1f, 1}, Px{0.2f, 0, 0.6f, 1}, Px{0.75f, 0, 0.55f, 1},
                                                  Px{1, 0.25f, 0, 1}, Px{1, 0.85f, 0, 1}, Px{1, 1, 1, 1}};
            const float f = l * 5;
            const int i = std::min(4, static_cast<int>(f));
            Px q = mix(pal[static_cast<std::size_t>(i)], pal[static_cast<std::size_t>(i) + 1], f - static_cast<float>(i));
            q.a = s.a;
            return q;
        });
        return out;
    }
    if (style == "invert") {
        each([](int, int, Px s) { return Px{1 - s.r, 1 - s.g, 1 - s.b, s.a}; });
        return out;
    }
    if (style == "night") {
        each([&](int x, int y, Px s) {
            const float l = std::min(1.f, lum(s) * 1.8f) + (hash(x + c.ox, y + c.oy, frameIndex) - 0.5f) * 0.1f;
            return Px{l * 0.25f, l, l * 0.3f, s.a};
        });
        return out;
    }
    if (style == "neon") {
        each([&](int x, int y, Px s) {
            const float e = std::min(1.f, sobel(src, x, y).r * 3);
            const Px hue = hsv(static_cast<float>((x + c.ox + y + c.oy) / (0.6 * c.S) + c.t * 0.2), 0.9f, 1);
            Px q = mix(s * 0.12f, hue, e);
            q.a = s.a;
            return q;
        });
        return out;
    }
    return blurred(src, 0.035 * c.S * c.amount);  // unknown style: never leave a region uncovered
}

}  // namespace

void applyCover(VideoFrame& frame, const Json& raw, Time t) {
    if (frame.empty() || !raw.is_object()) return;
    const Json p = raw;
    const double W = frame.width, H = frame.height, S = std::min(W, H);
    const double x = std::clamp(p.value("x", 0.35), -1.0, 2.0), y = std::clamp(p.value("y", 0.35), -1.0, 2.0);
    const double w = std::clamp(p.value("w", 0.3), 0.001, 3.0), h = std::clamp(p.value("h", 0.3), 0.001, 3.0);
    const double rot = p.value("rotation", 0.0) * kPi / 180;
    const bool invert = p.value("invert", false);
    Region reg{(x + w / 2) * W, (y + h / 2) * H, w * W / 2, h * H / 2, std::cos(rot), std::sin(rot), 0,
               p.value("shape", std::string("rect")), {}};
    if (!isCoverShape(reg.shape)) reg.shape = "rect";
    reg.feather = std::max(0.75, std::clamp(p.value("feather", 0.15), 0.0, 1.0) * std::min(reg.hw, reg.hh));
    if (reg.shape != "rect" && reg.shape != "rounded" && reg.shape != "ellipse" && reg.shape != "circle")
        for (const auto& v : unitShape(reg.shape)) reg.poly.push_back({v[0] * reg.hw, v[1] * reg.hh});

    // Covered area (frame pixels): the rotated box plus feather, or the whole frame when inverted.
    const double ex = std::abs(reg.hw * reg.cosr) + std::abs(reg.hh * reg.sinr) + reg.feather;
    const double ey = std::abs(reg.hw * reg.sinr) + std::abs(reg.hh * reg.cosr) + reg.feather;
    int x0 = 0, y0 = 0, x1 = frame.width, y1 = frame.height;
    if (!invert) {
        x0 = std::max(0, static_cast<int>(std::floor(reg.cx - ex)));
        y0 = std::max(0, static_cast<int>(std::floor(reg.cy - ey)));
        x1 = std::min(frame.width, static_cast<int>(std::ceil(reg.cx + ex)));
        y1 = std::min(frame.height, static_cast<int>(std::ceil(reg.cy + ey)));
    }
    if (x0 >= x1 || y0 >= y1) return;
    // Working area: plus a margin so blurs near the edge see real pixels.
    const double amount = 0.4 + 1.2 * std::clamp(p.value("amount", 0.5), 0.0, 1.0);
    const int margin = static_cast<int>(std::ceil(0.12 * S * amount)) + 2;
    const int ax0 = std::max(0, x0 - margin), ay0 = std::max(0, y0 - margin);
    const int ax1 = std::min(frame.width, x1 + margin), ay1 = std::min(frame.height, y1 + margin);
    Img area(ax1 - ax0, ay1 - ay0);
    const bool deep = frame.deep();
    for (int yy = 0; yy < area.h; ++yy)
        for (int xx = 0; xx < area.w; ++xx) {
            const std::size_t i = (static_cast<std::size_t>(yy + ay0) * frame.width + (xx + ax0)) * 4;
            area.at(xx, yy) = deep ? Px{halfToFloat(frame.half[i]), halfToFloat(frame.half[i + 1]),
                                        halfToFloat(frame.half[i + 2]), halfToFloat(frame.half[i + 3])}
                                   : Px{frame.rgba[i] / 255.f, frame.rgba[i + 1] / 255.f, frame.rgba[i + 2] / 255.f,
                                        frame.rgba[i + 3] / 255.f};
        }
    const Ctx ctx{area, S, amount, t.seconds(), ax0, ay0, reg.cx - ax0, reg.cy - ay0, p};
    const Img out = styled(p.value("style", std::string("blur")), ctx);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            float m = reg.mask(xx + 0.5, yy + 0.5);
            if (invert) m = 1 - m;
            if (m <= 0) continue;
            const Px o = area.at(xx - ax0, yy - ay0);
            Px s = out.p[static_cast<std::size_t>(yy - ay0) * out.w + (xx - ax0)];
            s.a = o.a;
            const Px r = mix(o, s, m);
            const std::size_t i = (static_cast<std::size_t>(yy) * frame.width + xx) * 4;
            if (deep) {
                frame.half[i] = floatToHalf(r.r);
                frame.half[i + 1] = floatToHalf(r.g);
                frame.half[i + 2] = floatToHalf(r.b);
            } else {
                auto q = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
                frame.rgba[i] = q(r.r);
                frame.rgba[i + 1] = q(r.g);
                frame.rgba[i + 2] = q(r.b);
            }
        }
}

}  // namespace vstudio
