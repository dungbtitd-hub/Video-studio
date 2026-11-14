#include "vstudio/video_fx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>

#include "vstudio/modules.hpp"

namespace vstudio::videofx {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double smooth(double t) {
    t = clamp01(t);
    return t * t * (3 - 2 * t);
}
double easeOut(double t) {
    t = clamp01(t);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

// Runs rows [0, h) on a few threads for large frames (pure per-row work).
void forRows(int h, int pixels, const std::function<void(int, int)>& rows) {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const int n = pixels >= 250000 ? static_cast<int>(std::min(4u, hw)) : 1;
    if (n <= 1) return rows(0, h);
    std::vector<std::thread> pool;
    const int step = (h + n - 1) / n;
    for (int i = 1; i < n; ++i)
        if (i * step < h) pool.emplace_back(rows, i * step, std::min(h, (i + 1) * step));
    rows(0, std::min(h, step));
    for (auto& t : pool) t.join();
}

// Bilinear RGBA8 sample at (x, y) in pixel-centre coordinates. `clampEdges`: outside reads the
// nearest edge (zoomed pictures never show a seam); otherwise outside is transparent.
inline void sample(const VideoFrame& f, double x, double y, bool clampEdges, std::uint8_t* o) {
    const int w = f.width, h = f.height;
    if (!clampEdges && (x < -0.5 || y < -0.5 || x > w - 0.5 || y > h - 0.5)) {
        o[0] = o[1] = o[2] = o[3] = 0;
        return;
    }
    x = std::clamp(x, 0.0, w - 1.0);
    y = std::clamp(y, 0.0, h - 1.0);
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0, fy = y - y0;
    const std::uint8_t* a = f.rgba.data() + (static_cast<std::size_t>(y0) * w + x0) * 4;
    const std::uint8_t* b = f.rgba.data() + (static_cast<std::size_t>(y0) * w + x1) * 4;
    const std::uint8_t* c = f.rgba.data() + (static_cast<std::size_t>(y1) * w + x0) * 4;
    const std::uint8_t* d = f.rgba.data() + (static_cast<std::size_t>(y1) * w + x1) * 4;
    const double wa = (1 - fx) * (1 - fy), wb = fx * (1 - fy), wc = (1 - fx) * fy, wd = fx * fy;
    for (int k = 0; k < 4; ++k) o[k] = static_cast<std::uint8_t>(a[k] * wa + b[k] * wb + c[k] * wc + d[k] * wd + 0.5);
}

// The picture placed in its own frame: scaled by s about the centre, turned by `angle`, moved by
// (dx, dy) pixels, optionally mirrored. Output pixel p reads source C + R(-angle) (p - C - d) / s.
struct Place {
    double s = 1, angle = 0, dx = 0, dy = 0;
    bool flipH = false, flipV = false, clampEdges = true;
};

void warp(VideoFrame& f, const Place& pl) {
    if (std::abs(pl.s - 1) < 1e-9 && std::abs(pl.angle) < 1e-9 && std::abs(pl.dx) < 1e-9 && std::abs(pl.dy) < 1e-9 &&
        !pl.flipH && !pl.flipV)
        return;
    const VideoFrame src = f;
    const int w = f.width, h = f.height;
    const double cx = (w - 1) / 2.0, cy = (h - 1) / 2.0;
    const double c = std::cos(-pl.angle) / pl.s, sn = std::sin(-pl.angle) / pl.s;
    const double fh = pl.flipH ? -1 : 1, fv = pl.flipV ? -1 : 1;
    forRows(h, w * h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const double py = y - cy - pl.dy;
            std::uint8_t* o = f.rgba.data() + static_cast<std::size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, o += 4) {
                const double px = x - cx - pl.dx;
                const double sx = (c * px - sn * py) * fh + cx, sy = (sn * px + c * py) * fv + cy;
                sample(src, sx, sy, pl.clampEdges, o);
            }
        }
    });
}

// Smallest scale at which the turned picture still covers its frame (no empty corners).
double coverScale(double angle, int w, int h) {
    const double c = std::abs(std::cos(angle)), s = std::abs(std::sin(angle));
    return std::max((w * c + h * s) / w, (w * s + h * c) / h);
}
// Largest scale at which the turned picture fits inside its frame (90° turns of a wide video).
double fitScale(double angle, int w, int h) {
    const double c = std::abs(std::cos(angle)), s = std::abs(std::sin(angle));
    return std::min(w / (w * c + h * s), h / (w * s + h * c));
}

// Smooth pseudo-random wobble in -1..1 (sum of incommensurate sines): camera shake without jumps.
double wobble(double t, double seed) {
    return (std::sin(t * 7.3 + seed) * 0.5 + std::sin(t * 13.1 + seed * 2.3) * 0.3 + std::sin(t * 23.7 + seed * 4.1) * 0.2);
}

void zoomFx(VideoFrame& f, const std::string& id, double a, double sp, double t, double len) {
    const double u = len > 0.05 ? clamp01(t / len) : 0;
    Place pl;
    if (id == "zoom_in") pl.s = 1 + 0.3 * a * smooth(u);
    else if (id == "zoom_out") pl.s = 1 + 0.3 * a * (1 - smooth(u));
    else if (id == "zoom_punch") pl.s = 1 + 0.3 * a * easeOut(t * sp / 0.6);
    else if (id == "zoom_pulse") {
        const double ph = std::fmod(t * sp / 0.8, 1.0);
        pl.s = 1 + 0.08 * a * std::pow(std::sin(kPi * ph), 4);  // a soft beat
    } else if (id == "zoom_breathe")
        pl.s = 1 + 0.07 * a * (0.5 - 0.5 * std::cos(2 * kPi * t * sp / 4.0));
    else if (id == "zoom_shake") {
        const double k = 0.012 * a * std::max(f.width, f.height);
        pl.dx = k * wobble(t * sp, 1.0);
        pl.dy = k * wobble(t * sp, 5.0);
        pl.angle = 0.6 * kDeg * a * wobble(t * sp, 9.0);
        pl.s = coverScale(pl.angle, f.width, f.height) * (1 + 0.035 * a);  // borders never show
    } else if (id == "zoom_drift") {  // slow pan across a slightly zoomed picture
        pl.s = 1 + 0.15 * a;
        pl.dx = (0.5 - smooth(u)) * (pl.s - 1) * f.width;
    } else
        return;
    warp(f, pl);
}

void rotateFx(VideoFrame& f, const std::string& id, double a, double sp, double t, double len) {
    const int w = f.width, h = f.height;
    const double u = len > 0.05 ? clamp01(t / len) : 0;
    Place pl;
    if (id == "rot_sway") {
        pl.angle = 4 * kDeg * a * std::sin(2 * kPi * t * sp / 2.4);
        pl.s = coverScale(4 * kDeg * a, w, h);
    } else if (id == "rot_spin") {
        pl.angle = 2 * kPi * t * sp / 6.0;
        pl.s = std::hypot(w, h) / std::min(w, h);  // constant: covers at every angle
    } else if (id == "rot_tilt") {
        pl.angle = 8 * kDeg * a * smooth(u);
        pl.s = coverScale(pl.angle, w, h);
    } else if (id == "rot_swirl") {
        const double e = smooth(u);
        pl.angle = 15 * kDeg * a * e;
        pl.s = coverScale(pl.angle, w, h) * (1 + 0.2 * a * e);
    } else if (id == "rot_90" || id == "rot_270") {
        pl.angle = (id == "rot_90" ? 0.5 : -0.5) * kPi;
        pl.s = fitScale(pl.angle, w, h);
        pl.clampEdges = false;  // the turned picture keeps its shape: bars on the sides
    } else if (id == "rot_180")
        pl.angle = kPi;
    else if (id == "flip_h")
        pl.flipH = true;
    else if (id == "flip_v")
        pl.flipV = true;
    else
        return;
    warp(f, pl);
}

// Reflections: the picture above a horizontal line (or left of a vertical one) is mirrored below it.
void reflectFx(VideoFrame& f, const std::string& id, double a, double sp, double t, double line) {
    const int w = f.width, h = f.height;
    const VideoFrame src = f;
    const double unit = std::max(w, h) / 1000.0;  // ripple sizes scale with the picture
    if (id == "mirror_lr" || id == "mirror_4") {
        const bool quad = id == "mirror_4";
        forRows(h, w * h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                std::uint8_t* o = f.rgba.data() + static_cast<std::size_t>(y) * w * 4;
                const int sy = quad && y >= h / 2 ? h - 1 - y : y;
                for (int x = 0; x < w; ++x, o += 4) {
                    const int sx = x >= w / 2 ? w - 1 - x : x;
                    std::copy_n(src.rgba.data() + (static_cast<std::size_t>(sy) * w + sx) * 4, 4, o);
                }
            }
        });
        return;
    }
    const bool sky = id == "reflect_sky";
    const bool rippled = id == "reflect_water" || id == "reflect_lake";
    const bool lake = id == "reflect_lake", mirror = id == "reflect_mirror";
    const double y0 = std::clamp(line, 0.1, 0.9) * h;
    // Water: a little darker and bluer, fading with depth; a mirror stays almost clean.
    const double dark = mirror ? 0.94 : lake ? 0.8 : 0.86;
    const double tint[3] = {lake ? 0.84 : 0.9, lake ? 0.97 : 0.97, lake ? 1.12 : 1.06};
    const double amp = (rippled ? 3.0 : 0.0) * unit * a * (lake ? 2.0 : 1.0), ph = t * sp * 2.2;
    // Per-column terms, computed once: sideways wobble, and the phase of the light streaks.
    std::vector<double> colWave(w), colSin(w), colCos(w);
    for (int x = 0; x < w; ++x) {
        colWave[x] = std::sin(x * 0.03 / unit + ph * 1.7);
        const double b = std::sin(x * 0.011 / unit + ph) * 3.0;
        colSin[x] = std::sin(b);
        colCos[x] = std::cos(b);
    }
    forRows(h, w * h, [&](int ya, int yb) {
        for (int y = ya; y < yb; ++y) {
            const bool below = sky ? y < y0 : y >= y0;
            if (!below) continue;
            const double depth = sky ? (y0 - y) / std::max(1.0, y0) : (y - y0) / std::max(1.0, h - y0);
            std::uint8_t* o = f.rgba.data() + static_cast<std::size_t>(y) * w * 4;
            const double my = 2 * y0 - y;  // mirrored row
            // Ripples grow with depth; rows wave sideways, and the surface bobs a little.
            const double grow = 0.25 + depth;
            const double rowShift = amp * grow * std::sin(y * 0.09 / unit * (1 + 0.6 * depth) - ph * 3.1);
            const double bob = amp * 0.6 * grow * std::sin(y * 0.05 / unit - ph * 2.3);
            const double colAmp = amp * 0.35 * grow;
            const double fade = 1 - 0.22 * depth;
            const double ay = y * 0.6 / unit - ph * 4.0, sa = std::sin(ay), ca = std::cos(ay);
            const double shineK = rippled ? 9.0 * 22.0 * a * (1 - depth) : 0.0;
            double gain[3];
            for (int k = 0; k < 3; ++k) gain[k] = dark * fade * (mirror ? 1.0 : tint[k]);
            for (int x = 0; x < w; ++x, o += 4) {
                std::uint8_t px[4];
                sample(src, x + rowShift + colAmp * colWave[x], my + bob, true, px);
                // thin light streaks on the water: sin(ay + b(x)) near its peak
                const double shine = shineK > 0 ? std::max(0.0, sa * colCos[x] + ca * colSin[x] - 0.92) * shineK : 0.0;
                for (int k = 0; k < 3; ++k) o[k] = static_cast<std::uint8_t>(std::clamp(px[k] * gain[k] + shine, 0.0, 255.0));
                o[3] = px[3];
            }
        }
    });
}

}  // namespace

const std::vector<VideoFxInfo>& videoFxCatalog() {
    static const std::vector<VideoFxInfo> list = {
        {"zoom_in", "Phóng to dần", "zoom"},
        {"zoom_out", "Thu nhỏ dần", "zoom"},
        {"zoom_punch", "Phóng nhanh", "zoom"},
        {"zoom_pulse", "Nhịp đập", "zoom"},
        {"zoom_breathe", "Thở chậm", "zoom"},
        {"zoom_drift", "Trôi ngang", "zoom"},
        {"zoom_shake", "Rung máy", "zoom"},
        {"rot_sway", "Lắc lư", "rotate"},
        {"rot_tilt", "Nghiêng dần", "rotate"},
        {"rot_swirl", "Xoáy vào", "rotate"},
        {"rot_spin", "Xoay tròn", "rotate"},
        {"rot_90", "Xoay 90° phải", "rotate", false},
        {"rot_270", "Xoay 90° trái", "rotate", false},
        {"rot_180", "Xoay 180°", "rotate", false},
        {"flip_h", "Lật ngang", "rotate", false},
        {"flip_v", "Lật dọc", "rotate", false},
        {"reflect_water", "Mặt nước", "reflect", true, true},
        {"reflect_lake", "Hồ gợn sóng", "reflect", true, true},
        {"reflect_mirror", "Gương phẳng", "reflect", false, true},
        {"reflect_sky", "Phản chiếu lên trên", "reflect", false, true},
        {"mirror_lr", "Đối xứng trái phải", "reflect", false},
        {"mirror_4", "Kính vạn hoa", "reflect", false},
    };
    return list;
}

const VideoFxInfo* findVideoFx(const std::string& id) {
    for (const auto& f : videoFxCatalog())
        if (f.id == id) return &f;
    return nullptr;
}

std::string groupName(const std::string& group) {
    if (group == "zoom") return "Phóng to";
    if (group == "rotate") return "Xoay";
    if (group == "reflect") return "Phản chiếu";
    return {};
}

std::string effectTypeFor(const std::string& group) {
    if (group == "zoom") return kZoomEffect;
    if (group == "rotate") return kRotateEffect;
    if (group == "reflect") return kReflectEffect;
    return {};
}

void applyVideoFx(VideoFrame& frame, const Json& params, double seconds, double length) {
    if (frame.empty() || frame.deep() || !params.is_object()) return;
    const std::string id = params.value("fx", std::string{});
    const VideoFxInfo* info = findVideoFx(id);
    if (!info) return;
    const double a = std::clamp(params.value("amount", 1.0), 0.0, 2.0);
    const double sp = std::clamp(params.value("speed", 1.0), 0.1, 4.0);
    const double t = std::max(0.0, seconds);
    if (info->group == "zoom") zoomFx(frame, id, a, sp, t, length);
    else if (info->group == "rotate") rotateFx(frame, id, a, sp, t, length);
    else reflectFx(frame, id, a, sp, t, params.value("line", id == "reflect_sky" ? 0.4 : 0.62));
}

namespace {
// A small landscape: sky, sun, two mountain ridges, a meadow and a tree - every effect reads on it.
void drawScene(VideoFrame& f) {
    const int w = f.width, h = f.height;
    auto mixc = [](const double a[3], const double b[3], double k, double* o) {
        for (int i = 0; i < 3; ++i) o[i] = a[i] + (b[i] - a[i]) * k;
    };
    const double skyTop[3] = {48, 92, 170}, skyLow[3] = {250, 180, 120}, ridge1[3] = {70, 80, 120},
                 ridge2[3] = {40, 70, 70}, grass[3] = {70, 140, 70}, grassLow[3] = {40, 100, 50};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double u = (x + 0.5) / w, v = (y + 0.5) / h;
            double c[3];
            mixc(skyTop, skyLow, clamp01(v / 0.6), c);
            const double sd = std::hypot((u - 0.68) * w / h, v - 0.3);
            if (sd < 0.09) c[0] = 255, c[1] = 236, c[2] = 170;
            else if (sd < 0.16) {
                const double k = (0.16 - sd) / 0.07 * 0.5;
                c[0] += (255 - c[0]) * k, c[1] += (230 - c[1]) * k, c[2] += (170 - c[2]) * k;
            }
            const double r1 = 0.45 + 0.06 * std::sin(u * 9 + 1) + 0.03 * std::sin(u * 23);
            const double r2 = 0.52 + 0.05 * std::sin(u * 6 + 3) + 0.02 * std::sin(u * 31 + 2);
            if (v > r1) std::copy_n(ridge1, 3, c);
            if (v > r2) std::copy_n(ridge2, 3, c);
            if (v > 0.62) mixc(grass, grassLow, clamp01((v - 0.62) / 0.38), c);
            // a tree on the left
            const double tx = (u - 0.22) * w / h;
            if (std::abs(tx) < 0.012 && v > 0.48 && v < 0.66) c[0] = 90, c[1] = 60, c[2] = 40;
            if (std::hypot(tx, (v - 0.44) * 0.9) < 0.07) c[0] = 40, c[1] = 120, c[2] = 60;
            std::uint8_t* o = f.rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            for (int i = 0; i < 3; ++i) o[i] = static_cast<std::uint8_t>(std::clamp(c[i], 0.0, 255.0));
            o[3] = 255;
        }
}
}  // namespace

void drawVideoFxIcon(const std::string& id, double seconds, VideoFrame& out, const VideoFrame* picture) {
    if (out.empty()) return;
    const int w = out.width, h = out.height;
    out.allocate(w, h);
    if (picture && !picture->empty() && !picture->deep()) {
        // Fill the thumbnail with the picture (cover), nearest-free bilinear.
        const double k = std::max(static_cast<double>(w) / picture->width, static_cast<double>(h) / picture->height);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                sample(*picture, (x - w / 2.0) / k + picture->width / 2.0 - 0.5, (y - h / 2.0) / k + picture->height / 2.0 - 0.5,
                       true, out.rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4);
    } else {
        drawScene(out);
    }
    applyVideoFx(out, Json{{"fx", id}}, seconds, 4.0);
    // Opaque over a dark backdrop (90° turns leave bars).
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        const unsigned a = out.rgba[i + 3];
        for (int k = 0; k < 3; ++k) out.rgba[i + k] = static_cast<std::uint8_t>((out.rgba[i + k] * a + 24 * (255 - a)) / 255);
        out.rgba[i + 3] = 255;
    }
}

}  // namespace vstudio::videofx

namespace vstudio {

namespace {
class VideoFxEffect final : public IVideoEffect {
public:
    void apply(VideoFrame& f, const Json& p, Time local) const override {
        videofx::applyVideoFx(f, p, local.seconds(), p.value("clip_length", 0.0));
    }
};
}  // namespace

void registerVideoFxModules(ModuleRegistry& r) {
    for (const char* type : {videofx::kZoomEffect, videofx::kRotateEffect, videofx::kReflectEffect})
        r.add<IVideoEffect>({.id = type, .displayName = "Video motion effect", .capabilities = {"effect.motion"}},
                            [] { return std::make_shared<VideoFxEffect>(); });
}

}  // namespace vstudio
