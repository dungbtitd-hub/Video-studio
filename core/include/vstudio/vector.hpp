// A small anti-aliased vector renderer for what the core draws itself: text
// (glyph outlines), stickers and icons. Paths of lines and Béziers, filled with
// the non-zero rule using exact area coverage (no supersampling), stroked with
// round joins, painted with a colour or a linear/radial gradient, and drawn
// onto RGBA8 or RGBA16F frames. Everything is computed only over the shape's
// bounding box.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "vstudio/media.hpp"

namespace vstudio::vec {

struct Pt {
    double x = 0, y = 0;
};

// x' = a*x + c*y + e, y' = b*x + d*y + f
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    static Affine translate(double x, double y) { return {1, 0, 0, 1, x, y}; }
    static Affine scale(double sx, double sy) { return {sx, 0, 0, sy, 0, 0}; }
    static Affine scale(double s) { return scale(s, s); }
    static Affine rotate(double radians);
    // Applies `o` first, then this.
    [[nodiscard]] Affine operator*(const Affine& o) const;
    [[nodiscard]] Pt apply(Pt p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
    [[nodiscard]] double scaleFactor() const;  // average linear scale (for flattening tolerance, strokes)
};

class Path {
public:
    Path& moveTo(double x, double y);
    Path& lineTo(double x, double y);
    Path& quadTo(double cx, double cy, double x, double y);
    Path& cubicTo(double c1x, double c1y, double c2x, double c2y, double x, double y);
    Path& close();
    // Shapes (closed, counter-clockwise in screen space unless noted).
    Path& rect(double x, double y, double w, double h);
    Path& roundRect(double x, double y, double w, double h, double r);
    Path& ellipse(double cx, double cy, double rx, double ry);
    Path& circle(double cx, double cy, double r) { return ellipse(cx, cy, r, r); }
    Path& polygon(const std::vector<Pt>& pts);
    // Regular star / polygon: `points` tips between radius `outer` and `inner` (inner == outer: polygon).
    Path& star(double cx, double cy, int points, double outer, double inner, double rotation = 0);
    Path& heart(double cx, double cy, double size);  // `size` = width
    Path& append(const Path& other, const Affine& m = {});

    [[nodiscard]] bool empty() const { return ops_.empty(); }
    // Polylines in device space (each closed), flattened to within `tolerance` px.
    [[nodiscard]] std::vector<std::vector<Pt>> flatten(const Affine& m, double tolerance = 0.2) const;
    // The same, keeping whether each subpath was closed (strokes need to know).
    struct Polyline {
        std::vector<Pt> pts;
        bool closed = false;
    };
    [[nodiscard]] std::vector<Polyline> polylines(const Affine& m, double tolerance = 0.2) const;
    // Bounds of the control points (a safe box for the shape) after `m`.
    [[nodiscard]] std::array<double, 4> bounds(const Affine& m = {}) const;  // x0, y0, x1, y1

private:
    enum class Op : std::uint8_t { Move, Line, Quad, Cubic, Close };
    std::vector<Op> ops_;
    std::vector<Pt> pts_;
};

// Colour in straight RGBA 0..1.
struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    static Color rgb(int r, int g, int b, float a = 1) { return {r / 255.f, g / 255.f, b / 255.f, a}; }
    static Color hex(const std::string& s, float alpha = 1);  // "#rrggbb" (or "rrggbb"); bad input = white
    static Color hsv(double h, double s, double v, float a = 1);  // h in degrees
    [[nodiscard]] Color withAlpha(float k) const { return {r, g, b, a * k}; }
};

struct Paint {
    enum class Kind { Solid, Linear, Radial } kind = Kind::Solid;
    Color color;
    // Gradients: from p0 (colour c0) to p1 (c1), device space; radial: centre p0, radius |p1 - p0|.
    Pt p0, p1;
    Color c0, c1;
    float opacity = 1;
    static Paint solid(Color c) { return {Kind::Solid, c}; }
    static Paint linear(Pt a, Pt b, Color ca, Color cb);
    static Paint radial(Pt centre, double radius, Color inner, Color outer);
    [[nodiscard]] Color at(double x, double y) const;
};

// Coverage of a shape over a box of the frame: 0..1 per pixel.
struct Mask {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<float> v;
    [[nodiscard]] bool empty() const { return w <= 0 || h <= 0; }
    [[nodiscard]] float at(int x, int y) const {  // frame coordinates
        x -= x0;
        y -= y0;
        return x < 0 || y < 0 || x >= w || y >= h ? 0.f : v[static_cast<std::size_t>(y) * w + x];
    }
    // Grows the box by `pad` px on every side (room for a blur or an offset).
    [[nodiscard]] Mask padded(int pad) const;
    // Separable box blur, three passes (close to a Gaussian of sigma ~ radius / 1.7).
    void blur(double radius);
    // max(this, other * k): union of shapes with per-shape strength.
    void maxWith(const Mask& other, float k = 1);
};

// Draws onto `target` (RGBA8 or RGBA16F, straight alpha), clipped to it.
class Canvas {
public:
    explicit Canvas(VideoFrame& target) : f_(target) {}
    [[nodiscard]] int width() const { return f_.width; }
    [[nodiscard]] int height() const { return f_.height; }
    [[nodiscard]] Mask fillMask(const Path& p, const Affine& m = {}) const;
    [[nodiscard]] Mask strokeMask(const Path& p, double width, const Affine& m = {}) const;
    void fill(const Path& p, const Paint& paint, const Affine& m = {});
    void stroke(const Path& p, double width, const Paint& paint, const Affine& m = {});
    // Paints `paint` through a coverage mask, shifted by (dx, dy) px.
    void drawMask(const Mask& mask, const Paint& paint, int dx = 0, int dy = 0);

private:
    VideoFrame& f_;
};

// Coverage of polygons (non-zero winding) over the box [x0, x0 + w) x [y0, y0 + h).
Mask rasterize(const std::vector<std::vector<Pt>>& polys, int x0, int y0, int w, int h);

}  // namespace vstudio::vec
