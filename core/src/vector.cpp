#include "vstudio/vector.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "vstudio/pixel.hpp"

namespace vstudio::vec {

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kKappa = 0.5522847498;  // cubic approximation of a quarter circle
}  // namespace

// ---------------------------------------------------------------- Affine

Affine Affine::rotate(double r) {
    const double c = std::cos(r), s = std::sin(r);
    return {c, s, -s, c, 0, 0};
}

Affine Affine::operator*(const Affine& o) const {
    return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e,
            b * o.e + d * o.f + f};
}

double Affine::scaleFactor() const { return std::sqrt(std::abs(a * d - b * c)); }

// ---------------------------------------------------------------- Path

Path& Path::moveTo(double x, double y) {
    ops_.push_back(Op::Move);
    pts_.push_back({x, y});
    return *this;
}
Path& Path::lineTo(double x, double y) {
    ops_.push_back(Op::Line);
    pts_.push_back({x, y});
    return *this;
}
Path& Path::quadTo(double cx, double cy, double x, double y) {
    ops_.push_back(Op::Quad);
    pts_.push_back({cx, cy});
    pts_.push_back({x, y});
    return *this;
}
Path& Path::cubicTo(double c1x, double c1y, double c2x, double c2y, double x, double y) {
    ops_.push_back(Op::Cubic);
    pts_.push_back({c1x, c1y});
    pts_.push_back({c2x, c2y});
    pts_.push_back({x, y});
    return *this;
}
Path& Path::close() {
    ops_.push_back(Op::Close);
    return *this;
}

Path& Path::rect(double x, double y, double w, double h) {
    return moveTo(x, y).lineTo(x + w, y).lineTo(x + w, y + h).lineTo(x, y + h).close();
}

Path& Path::roundRect(double x, double y, double w, double h, double r) {
    r = std::clamp(r, 0.0, std::min(w, h) / 2);
    if (r <= 0) return rect(x, y, w, h);
    const double k = r * (1 - kKappa);
    moveTo(x + r, y);
    lineTo(x + w - r, y);
    cubicTo(x + w - k, y, x + w, y + k, x + w, y + r);
    lineTo(x + w, y + h - r);
    cubicTo(x + w, y + h - k, x + w - k, y + h, x + w - r, y + h);
    lineTo(x + r, y + h);
    cubicTo(x + k, y + h, x, y + h - k, x, y + h - r);
    lineTo(x, y + r);
    cubicTo(x, y + k, x + k, y, x + r, y);
    return close();
}

Path& Path::ellipse(double cx, double cy, double rx, double ry) {
    const double kx = rx * kKappa, ky = ry * kKappa;
    moveTo(cx + rx, cy);
    cubicTo(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    cubicTo(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    cubicTo(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    cubicTo(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    return close();
}

Path& Path::polygon(const std::vector<Pt>& pts) {
    if (pts.empty()) return *this;
    moveTo(pts[0].x, pts[0].y);
    for (std::size_t i = 1; i < pts.size(); ++i) lineTo(pts[i].x, pts[i].y);
    return close();
}

Path& Path::star(double cx, double cy, int points, double outer, double inner, double rotation) {
    points = std::max(3, points);
    const bool poly = std::abs(inner - outer) < 1e-9;
    const int n = poly ? points : points * 2;
    std::vector<Pt> pts;
    for (int i = 0; i < n; ++i) {
        const double r = poly || i % 2 == 0 ? outer : inner;
        const double a = rotation - kPi / 2 + 2 * kPi * i / n;
        pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return polygon(pts);
}

Path& Path::heart(double cx, double cy, double size) {
    const double s = size / 2;
    moveTo(cx, cy + s * 0.9);
    cubicTo(cx - s * 0.2, cy + s * 0.7, cx - s, cy + s * 0.25, cx - s, cy - s * 0.25);
    cubicTo(cx - s, cy - s * 0.75, cx - s * 0.45, cy - s * 0.95, cx, cy - s * 0.5);
    cubicTo(cx + s * 0.45, cy - s * 0.95, cx + s, cy - s * 0.75, cx + s, cy - s * 0.25);
    cubicTo(cx + s, cy + s * 0.25, cx + s * 0.2, cy + s * 0.7, cx, cy + s * 0.9);
    return close();
}

Path& Path::append(const Path& other, const Affine& m) {
    for (const Pt& p : other.pts_) pts_.push_back(m.apply(p));
    ops_.insert(ops_.end(), other.ops_.begin(), other.ops_.end());
    return *this;
}

namespace {
using Sub = Path::Polyline;

std::vector<Sub> subpaths(const std::vector<std::uint8_t>& ops, const std::vector<Pt>& src, const Affine& m,
                          double tol) {
    std::vector<Sub> out;
    std::size_t k = 0;
    Pt cur{}, start{};
    auto ensure = [&] {
        if (out.empty() || out.back().closed) {
            out.push_back({});
            out.back().pts.push_back(cur);
        }
    };
    for (const auto op : ops) {
        switch (op) {
            case 0: {  // move
                cur = start = m.apply(src[k++]);
                out.push_back({});
                out.back().pts.push_back(cur);
                break;
            }
            case 1: {  // line
                ensure();
                cur = m.apply(src[k++]);
                out.back().pts.push_back(cur);
                break;
            }
            case 2: {  // quad
                ensure();
                const Pt c = m.apply(src[k]), p = m.apply(src[k + 1]);
                k += 2;
                const double dd = std::hypot(cur.x - 2 * c.x + p.x, cur.y - 2 * c.y + p.y);
                const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(dd / (4 * tol)))), 1, 64);
                for (int i = 1; i <= n; ++i) {
                    const double t = static_cast<double>(i) / n, u = 1 - t;
                    out.back().pts.push_back({u * u * cur.x + 2 * u * t * c.x + t * t * p.x,
                                              u * u * cur.y + 2 * u * t * c.y + t * t * p.y});
                }
                cur = p;
                break;
            }
            case 3: {  // cubic
                ensure();
                const Pt c1 = m.apply(src[k]), c2 = m.apply(src[k + 1]), p = m.apply(src[k + 2]);
                k += 3;
                const double dd = std::max(std::hypot(cur.x - 2 * c1.x + c2.x, cur.y - 2 * c1.y + c2.y),
                                           std::hypot(c1.x - 2 * c2.x + p.x, c1.y - 2 * c2.y + p.y));
                const int n = std::clamp(static_cast<int>(std::ceil(std::sqrt(dd * 0.75 / tol))), 1, 100);
                for (int i = 1; i <= n; ++i) {
                    const double t = static_cast<double>(i) / n, u = 1 - t;
                    const double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
                    out.back().pts.push_back(
                        {a * cur.x + b * c1.x + c * c2.x + d * p.x, a * cur.y + b * c1.y + c * c2.y + d * p.y});
                }
                cur = p;
                break;
            }
            default:  // close
                if (!out.empty() && !out.back().closed) {
                    out.back().closed = true;
                    cur = start;
                }
                break;
        }
    }
    return out;
}

std::vector<std::uint8_t> opBytes(const auto& ops) {
    std::vector<std::uint8_t> b;
    b.reserve(ops.size());
    for (auto o : ops) b.push_back(static_cast<std::uint8_t>(o));
    return b;
}

double signedArea(const std::vector<Pt>& p) {
    double a = 0;
    for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += (p[j].x * p[i].y - p[i].x * p[j].y);
    return a / 2;
}
}  // namespace

std::vector<std::vector<Pt>> Path::flatten(const Affine& m, double tolerance) const {
    std::vector<std::vector<Pt>> out;
    for (auto& s : subpaths(opBytes(ops_), pts_, m, tolerance))
        if (s.pts.size() >= 3) out.push_back(std::move(s.pts));
    return out;
}

std::vector<Path::Polyline> Path::polylines(const Affine& m, double tolerance) const {
    return subpaths(opBytes(ops_), pts_, m, tolerance);
}

std::array<double, 4> Path::bounds(const Affine& m) const {
    std::array<double, 4> b{1e300, 1e300, -1e300, -1e300};
    for (const Pt& p0 : pts_) {
        const Pt p = m.apply(p0);
        b[0] = std::min(b[0], p.x);
        b[1] = std::min(b[1], p.y);
        b[2] = std::max(b[2], p.x);
        b[3] = std::max(b[3], p.y);
    }
    if (pts_.empty()) b = {0, 0, 0, 0};
    return b;
}

// ---------------------------------------------------------------- Colour & paint

Color Color::hex(const std::string& s0, float alpha) {
    std::string s = s0;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6 && s.size() != 8) return {1, 1, 1, alpha};
    try {
        const unsigned long v = std::stoul(s.substr(0, 6), nullptr, 16);
        Color c = rgb(static_cast<int>((v >> 16) & 255), static_cast<int>((v >> 8) & 255), static_cast<int>(v & 255), alpha);
        if (s.size() == 8) c.a *= static_cast<float>(std::stoul(s.substr(6, 2), nullptr, 16)) / 255.f;
        return c;
    } catch (...) {
        return {1, 1, 1, alpha};
    }
}

Color Color::hsv(double h, double s, double v, float a) {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0) / 60.0;
    const double c = v * s, x = c * (1 - std::abs(std::fmod(h, 2.0) - 1)), m = v - c;
    double r = 0, g = 0, b = 0;
    if (h < 1) r = c, g = x;
    else if (h < 2) r = x, g = c;
    else if (h < 3) g = c, b = x;
    else if (h < 4) g = x, b = c;
    else if (h < 5) r = x, b = c;
    else r = c, b = x;
    return {static_cast<float>(r + m), static_cast<float>(g + m), static_cast<float>(b + m), a};
}

Paint Paint::linear(Pt a, Pt b, Color ca, Color cb) {
    Paint p;
    p.kind = Kind::Linear;
    p.p0 = a;
    p.p1 = b;
    p.c0 = ca;
    p.c1 = cb;
    return p;
}

Paint Paint::radial(Pt centre, double radius, Color inner, Color outer) {
    Paint p;
    p.kind = Kind::Radial;
    p.p0 = centre;
    p.p1 = {centre.x + radius, centre.y};
    p.c0 = inner;
    p.c1 = outer;
    return p;
}

Color Paint::at(double x, double y) const {
    if (kind == Kind::Solid) return color;
    double t;
    if (kind == Kind::Linear) {
        const double dx = p1.x - p0.x, dy = p1.y - p0.y, len2 = dx * dx + dy * dy;
        t = len2 > 0 ? ((x - p0.x) * dx + (y - p0.y) * dy) / len2 : 0;
    } else {
        const double r = std::hypot(p1.x - p0.x, p1.y - p0.y);
        t = r > 0 ? std::hypot(x - p0.x, y - p0.y) / r : 0;
    }
    const float k = static_cast<float>(std::clamp(t, 0.0, 1.0));
    return {c0.r + (c1.r - c0.r) * k, c0.g + (c1.g - c0.g) * k, c0.b + (c1.b - c0.b) * k, c0.a + (c1.a - c0.a) * k};
}

// ---------------------------------------------------------------- Rasterizer

namespace {

// Signed area / cover accumulation (as in font-rs): exact coverage of each pixel by the
// polygon, non-zero winding (overlaps add, then clamp to 1).
void accumulateLine(std::vector<float>& acc, int stride, int h, Pt p0, Pt p1) {
    if (std::abs(p0.y - p1.y) < 1e-12) return;
    float dir = 1;
    if (p0.y > p1.y) {
        std::swap(p0, p1);
        dir = -1;
    }
    const double dxdy = (p1.x - p0.x) / (p1.y - p0.y);
    double x = p0.x;
    int y0 = static_cast<int>(std::floor(p0.y));
    if (p0.y < 0) {
        x -= p0.y * dxdy;
        y0 = 0;
    }
    const int yEnd = std::min(h, static_cast<int>(std::ceil(p1.y)));
    for (int y = y0; y < yEnd; ++y) {
        float* row = acc.data() + static_cast<std::size_t>(y) * stride;
        const double dy = std::min<double>(y + 1, p1.y) - std::max<double>(y, p0.y);
        const double xnext = x + dxdy * dy;
        const float d = static_cast<float>(dy) * dir;
        const double xa = std::min(x, xnext), xb = std::max(x, xnext);
        const double x0floor = std::floor(xa);
        const int x0i = static_cast<int>(x0floor);
        const double x1ceil = std::ceil(xb);
        const int x1i = static_cast<int>(x1ceil);
        if (x1i <= x0i + 1) {
            const float xmf = static_cast<float>(0.5 * (x + xnext) - x0floor);
            row[x0i] += d - d * xmf;
            row[x0i + 1] += d * xmf;
        } else {
            const double s = 1.0 / (xb - xa);
            const double x0f = xa - x0floor;
            const double a0 = 0.5 * s * (1 - x0f) * (1 - x0f);
            const double x1f = xb - x1ceil + 1;
            const double am = 0.5 * s * x1f * x1f;
            row[x0i] += d * static_cast<float>(a0);
            if (x1i == x0i + 2) {
                row[x0i + 1] += d * static_cast<float>(1 - a0 - am);
            } else {
                const double a1 = s * (1.5 - x0f);
                row[x0i + 1] += d * static_cast<float>(a1 - a0);
                for (int xi = x0i + 2; xi < x1i - 1; ++xi) row[xi] += d * static_cast<float>(s);
                const double a2 = a1 + (x1i - x0i - 3) * s;
                row[x1i - 1] += d * static_cast<float>(1 - a2 - am);
            }
            row[x1i] += d * static_cast<float>(am);
        }
        x = xnext;
    }
}

}  // namespace

Mask rasterize(const std::vector<std::vector<Pt>>& polys, int x0, int y0, int w, int h) {
    Mask m;
    if (w <= 0 || h <= 0) return m;
    m.x0 = x0;
    m.y0 = y0;
    m.w = w;
    m.h = h;
    const int stride = w + 2;
    std::vector<float> acc(static_cast<std::size_t>(stride) * h, 0.f);
    for (const auto& poly : polys) {
        if (poly.size() < 2) continue;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Pt& a = poly[i];
            const Pt& b = poly[(i + 1) % poly.size()];
            // Local coordinates; x clamped into the box (what lies left of it still counts as "left").
            accumulateLine(acc, stride, h, {std::clamp(a.x - x0, 0.0, double(w)), a.y - y0},
                           {std::clamp(b.x - x0, 0.0, double(w)), b.y - y0});
        }
    }
    m.v.resize(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        float s = 0;
        const float* row = acc.data() + static_cast<std::size_t>(y) * stride;
        float* out = m.v.data() + static_cast<std::size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            s += row[x];
            out[x] = std::min(1.f, std::abs(s));
        }
    }
    return m;
}

Mask Mask::padded(int pad) const {
    Mask o;
    if (empty() || pad <= 0) return *this;
    o.x0 = x0 - pad;
    o.y0 = y0 - pad;
    o.w = w + 2 * pad;
    o.h = h + 2 * pad;
    o.v.assign(static_cast<std::size_t>(o.w) * o.h, 0.f);
    for (int y = 0; y < h; ++y)
        std::copy_n(v.begin() + static_cast<std::ptrdiff_t>(y) * w, w,
                    o.v.begin() + static_cast<std::ptrdiff_t>(y + pad) * o.w + pad);
    return o;
}

void Mask::blur(double radius) {
    const int k = static_cast<int>(std::lround(radius / 2));
    if (k < 1 || empty()) return;
    std::vector<float> tmp(v.size());
    auto pass = [&](bool horizontal) {
        const int n = horizontal ? w : h, lines = horizontal ? h : w;
        const std::size_t step = horizontal ? 1 : static_cast<std::size_t>(w);
        const float inv = 1.f / static_cast<float>(2 * k + 1);
        for (int l = 0; l < lines; ++l) {
            const std::size_t base = horizontal ? static_cast<std::size_t>(l) * w : static_cast<std::size_t>(l);
            float s = 0;
            auto at = [&](int i) { return i < 0 || i >= n ? 0.f : v[base + static_cast<std::size_t>(i) * step]; };
            for (int i = -k; i <= k; ++i) s += at(i);
            for (int i = 0; i < n; ++i) {
                tmp[base + static_cast<std::size_t>(i) * step] = s * inv;
                s += at(i + k + 1) - at(i - k);
            }
        }
        v.swap(tmp);
    };
    for (int i = 0; i < 3; ++i) {
        pass(true);
        pass(false);
    }
}

void Mask::maxWith(const Mask& o, float k) {
    if (o.empty()) return;
    if (empty()) {
        *this = o;
        for (float& x : v) x *= k;
        return;
    }
    // Grow to the union box.
    const int nx0 = std::min(x0, o.x0), ny0 = std::min(y0, o.y0);
    const int nx1 = std::max(x0 + w, o.x0 + o.w), ny1 = std::max(y0 + h, o.y0 + o.h);
    if (nx0 != x0 || ny0 != y0 || nx1 != x0 + w || ny1 != y0 + h) {
        Mask g;
        g.x0 = nx0;
        g.y0 = ny0;
        g.w = nx1 - nx0;
        g.h = ny1 - ny0;
        g.v.assign(static_cast<std::size_t>(g.w) * g.h, 0.f);
        for (int y = 0; y < h; ++y)
            std::copy_n(v.begin() + static_cast<std::ptrdiff_t>(y) * w, w,
                        g.v.begin() + static_cast<std::ptrdiff_t>(y + y0 - ny0) * g.w + (x0 - nx0));
        *this = std::move(g);
    }
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x) {
            float& d = v[static_cast<std::size_t>(y + o.y0 - y0) * w + (x + o.x0 - x0)];
            d = std::max(d, o.v[static_cast<std::size_t>(y) * o.w + x] * k);
        }
}

// ---------------------------------------------------------------- Canvas

namespace {

// Outline of a stroke with round joins and caps: a quad per segment plus a disc at every
// vertex, all wound the same way so the non-zero fill unions them.
std::vector<std::vector<Pt>> strokePolys(const Path& p, double width, const Affine& m) {
    std::vector<std::vector<Pt>> out;
    const double hw = width * m.scaleFactor() / 2;
    if (hw <= 0) return out;
    const int segs = std::clamp(static_cast<int>(std::ceil(hw * 1.5)), 8, 40);
    auto add = [&](std::vector<Pt> poly) {
        if (signedArea(poly) < 0) std::reverse(poly.begin(), poly.end());  // all one way: overlaps never cancel
        out.push_back(std::move(poly));
    };
    auto disc = [&](Pt c) {
        std::vector<Pt> poly;
        poly.reserve(static_cast<std::size_t>(segs));
        for (int i = 0; i < segs; ++i) {
            const double a = 2 * kPi * i / segs;
            poly.push_back({c.x + hw * std::cos(a), c.y + hw * std::sin(a)});
        }
        add(std::move(poly));
    };
    for (auto& sub : p.polylines(m, 0.2)) {
        auto& v = sub.pts;
        // Drop repeated points.
        std::vector<Pt> q;
        for (const Pt& pt : v)
            if (q.empty() || std::hypot(pt.x - q.back().x, pt.y - q.back().y) > 1e-6) q.push_back(pt);
        if (sub.closed && q.size() > 1 && std::hypot(q.front().x - q.back().x, q.front().y - q.back().y) <= 1e-6)
            q.pop_back();
        if (q.empty()) continue;
        if (q.size() == 1) {
            disc(q[0]);
            continue;
        }
        const std::size_t n = sub.closed ? q.size() : q.size() - 1;
        for (std::size_t i = 0; i < n; ++i) {
            const Pt a = q[i], b = q[(i + 1) % q.size()];
            const double len = std::hypot(b.x - a.x, b.y - a.y);
            const double nx = -(b.y - a.y) / len * hw, ny = (b.x - a.x) / len * hw;
            add({{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}});
        }
        for (const Pt& pt : q) disc(pt);
    }
    return out;
}

// The box of the frame a shape with these bounds can touch (empty when off-frame).
Mask boxFor(std::array<double, 4> b, double pad, int fw, int fh) {
    Mask m;
    m.x0 = std::max(0, static_cast<int>(std::floor(b[0] - pad)));
    m.y0 = std::max(0, static_cast<int>(std::floor(b[1] - pad)));
    const int x1 = std::min(fw, static_cast<int>(std::ceil(b[2] + pad)));
    const int y1 = std::min(fh, static_cast<int>(std::ceil(b[3] + pad)));
    m.w = std::max(0, x1 - m.x0);
    m.h = std::max(0, y1 - m.y0);
    return m;
}

}  // namespace

Mask Canvas::fillMask(const Path& p, const Affine& m) const {
    if (p.empty()) return {};
    const Mask box = boxFor(p.bounds(m), 1, f_.width, f_.height);
    if (box.empty()) return {};
    return rasterize(p.flatten(m, 0.2), box.x0, box.y0, box.w, box.h);
}

Mask Canvas::strokeMask(const Path& p, double width, const Affine& m) const {
    if (p.empty() || width <= 0) return {};
    const Mask box = boxFor(p.bounds(m), width * m.scaleFactor() / 2 + 1, f_.width, f_.height);
    if (box.empty()) return {};
    return rasterize(strokePolys(p, width, m), box.x0, box.y0, box.w, box.h);
}

void Canvas::fill(const Path& p, const Paint& paint, const Affine& m) { drawMask(fillMask(p, m), paint); }

void Canvas::stroke(const Path& p, double width, const Paint& paint, const Affine& m) {
    drawMask(strokeMask(p, width, m), paint);
}

void Canvas::drawMask(const Mask& mask, const Paint& paint, int dx, int dy) {
    if (mask.empty() || paint.opacity <= 0 || f_.empty()) return;
    const bool deep = f_.deep();
    const int xa = std::max(0, mask.x0 + dx), xb = std::min(f_.width, mask.x0 + dx + mask.w);
    const int ya = std::max(0, mask.y0 + dy), yb = std::min(f_.height, mask.y0 + dy + mask.h);
    const bool solid = paint.kind == Paint::Kind::Solid;
    const Color sc = paint.color;
    for (int y = ya; y < yb; ++y) {
        const float* mrow = mask.v.data() + static_cast<std::size_t>(y - dy - mask.y0) * mask.w - (mask.x0 + dx);
        for (int x = xa; x < xb; ++x) {
            const float cov = mrow[x];
            if (cov <= 0) continue;
            const Color c = solid ? sc : paint.at(x + 0.5, y + 0.5);
            const float sa = std::min(1.f, cov) * c.a * paint.opacity;
            if (sa <= 0) continue;
            const std::size_t i = (static_cast<std::size_t>(y) * f_.width + x) * 4;
            float d[4];
            if (deep) {
                for (int k = 0; k < 4; ++k) d[k] = halfToFloat(f_.half[i + k]);
            } else {
                for (int k = 0; k < 4; ++k) d[k] = f_.rgba[i + k] / 255.f;
            }
            // Straight-alpha source-over.
            const float da = d[3] * (1 - sa), oa = sa + da;
            const float s[3] = {c.r, c.g, c.b};
            float o[4];
            for (int k = 0; k < 3; ++k) o[k] = oa > 0 ? (s[k] * sa + d[k] * da) / oa : 0.f;
            o[3] = oa;
            if (deep) {
                for (int k = 0; k < 4; ++k) f_.half[i + k] = floatToHalf(o[k]);
            } else {
                for (int k = 0; k < 4; ++k)
                    f_.rgba[i + k] = static_cast<std::uint8_t>(std::lround(std::clamp(o[k], 0.f, 1.f) * 255.f));
            }
        }
    }
}

}  // namespace vstudio::vec
