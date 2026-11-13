#include "vstudio/motion.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vstudio::motion {

namespace {

constexpr double kPi = std::numbers::pi;

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
double smooth(double x) {
    x = clamp01(x);
    return x * x * (3 - 2 * x);
}
double outCubic(double x) {
    x = clamp01(x);
    return 1 - (1 - x) * (1 - x) * (1 - x);
}
double outBack(double x) {
    x = clamp01(x);
    constexpr double c1 = 1.70158, c3 = c1 + 1;
    return 1 + c3 * std::pow(x - 1, 3) + c1 * std::pow(x - 1, 2);
}
double elasticRaw(double x) { return std::pow(2, -10 * x) * std::sin((10 * x - 0.75) * (2 * kPi / 3)) + 1; }
double outElastic(double x) {
    x = clamp01(x);
    // Exactly 0 at 0 and 1 at 1 (the textbook curve is off by ~1e-3 at the end).
    return elasticRaw(x) - (1 - x) * elasticRaw(0) - x * (elasticRaw(1) - 1);
}
double outBounce(double x) {
    x = clamp01(x);
    constexpr double n = 7.5625, d = 2.75;
    if (x < 1 / d) return n * x * x;
    if (x < 2 / d) return n * (x -= 1.5 / d) * x + 0.75;
    if (x < 2.5 / d) return n * (x -= 2.25 / d) * x + 0.9375;
    return n * (x -= 2.625 / d) * x + 0.984375;
}
// Fades in over the first `share` of the motion.
double fadeIn(double p, double share) { return smooth(p / share); }
// Letter i of n starts after a delay: the starts spread over `span` of the whole time.
double stagger(double p, int i, int n, double span) {
    if (n <= 1) return clamp01(p);
    return clamp01((p - span * i / (n - 1)) / (1 - span));
}
double hash01(unsigned a, unsigned b) {
    unsigned h = a * 374761393u + b * 668265263u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / double(0xFFFFFF);
}

const std::vector<Info> kAnimations = {
    {"fade", "Hiện dần"},
    {"zoom_in", "Phóng to dần"},
    {"zoom_out", "Thu nhỏ vào"},
    {"pop", "Bật nảy"},
    {"blur_in", "Mờ thành rõ"},
    {"zoom_blur", "Phóng mờ"},
    {"slide_up", "Trượt lên"},
    {"slide_down", "Trượt xuống"},
    {"slide_left", "Trượt sang trái"},
    {"slide_right", "Trượt sang phải"},
    {"spin_in", "Xoay vào"},
    {"flip_in", "Lật ngang"},
    {"bounce_in", "Rơi nảy"},
    {"elastic", "Đàn hồi"},
    {"stretch", "Kéo giãn"},
    {"swing", "Đung đưa"},
    {"wipe_right", "Lau sang phải"},
    {"wipe_up", "Lau lên"},
    {"typewriter", "Đánh máy", true},
    {"fade_letters", "Hiện từng chữ", true},
    {"rise_letters", "Từng chữ bay lên", true},
    {"drop_letters", "Từng chữ rơi", true},
    {"pop_letters", "Bật từng chữ", true},
    {"blur_letters", "Mờ từng chữ", true},
    {"wave_in", "Gợn sóng", true},
    {"rotate_letters", "Xoay từng chữ", true},
    {"jump_letters", "Nhảy từng chữ", true},
    {"shutter", "Mở rèm từng chữ", true},
    {"scatter", "Tụ lại", true},
    {"split", "Tách đôi", true},
    {"tracking", "Khép chữ", true},
};

const std::vector<Info> kLoops = {
    {"pulse", "Nhịp đập"},     {"heartbeat", "Tim đập"}, {"float", "Bồng bềnh"},
    {"swing", "Lắc lư"},       {"shake", "Rung"},        {"jelly", "Thạch rung"},
    {"blink", "Nhấp nháy"},    {"wave", "Sóng chữ", true}, {"bounce", "Nhún nhảy", true},
};

}  // namespace

Pose Pose::operator*(const Pose& o) const {
    Pose r;
    r.dx = dx + o.dx;
    r.dy = dy + o.dy;
    r.sx = sx * o.sx;
    r.sy = sy * o.sy;
    r.rotation = rotation + o.rotation;
    r.opacity = opacity * o.opacity;
    r.blur = blur + o.blur;
    r.wipe = wipe ? wipe : o.wipe;
    r.reveal = std::min(reveal, o.reveal);
    return r;
}

vec::Affine Pose::transform(vec::Pt c, double unit) const {
    using vec::Affine;
    return Affine::translate(c.x + dx * unit, c.y + dy * unit) * Affine::rotate(rotation) * Affine::scale(sx, sy) *
           Affine::translate(-c.x, -c.y);
}

const std::vector<Info>& animations() { return kAnimations; }
const std::vector<Info>& loops() { return kLoops; }

const Info* findAnimation(const std::string& id) {
    for (const auto& a : kAnimations)
        if (a.id == id) return &a;
    return nullptr;
}

const Info* findLoop(const std::string& id) {
    for (const auto& a : kLoops)
        if (a.id == id) return &a;
    return nullptr;
}

Pose inPose(const std::string& id, double p, int i, int n, unsigned seed) {
    p = clamp01(p);
    Pose r;
    if (p >= 1) return r;
    const double e = outCubic(p);
    if (id == "fade") {
        r.opacity = smooth(p);
    } else if (id == "zoom_in") {
        r.sx = r.sy = 0.3 + 0.7 * e;
        r.opacity = fadeIn(p, 0.5);
    } else if (id == "zoom_out") {
        r.sx = r.sy = 1 + 1.2 * (1 - e);
        r.opacity = fadeIn(p, 0.6);
    } else if (id == "pop") {
        r.sx = r.sy = std::max(0.0, outBack(p));
        r.opacity = fadeIn(p, 0.3);
    } else if (id == "blur_in") {
        r.blur = 0.35 * (1 - smooth(p));
        r.opacity = fadeIn(p, 0.7);
    } else if (id == "zoom_blur") {
        r.sx = r.sy = 1 + 0.6 * (1 - e);
        r.blur = 0.3 * (1 - smooth(p));
        r.opacity = fadeIn(p, 0.6);
    } else if (id == "slide_up" || id == "slide_down") {
        r.dy = (id == "slide_up" ? 0.8 : -0.8) * (1 - e);
        r.opacity = fadeIn(p, 0.6);
    } else if (id == "slide_left" || id == "slide_right") {
        r.dx = (id == "slide_left" ? 1.5 : -1.5) * (1 - e);
        r.opacity = fadeIn(p, 0.6);
    } else if (id == "spin_in") {
        r.rotation = -kPi / 2 * (1 - e);
        r.sx = r.sy = 0.4 + 0.6 * e;
        r.opacity = fadeIn(p, 0.5);
    } else if (id == "flip_in") {
        r.sx = smooth(p);
        r.opacity = fadeIn(p, 0.3);
    } else if (id == "bounce_in") {
        r.dy = -1.4 * (1 - outBounce(p));
        r.opacity = fadeIn(p, 0.25);
    } else if (id == "elastic") {
        r.sx = r.sy = std::max(0.0, outElastic(p));
        r.opacity = fadeIn(p, 0.25);
    } else if (id == "stretch") {
        r.sx = 1 + 0.8 * (1 - e);
        r.sy = e;
        r.opacity = fadeIn(p, 0.4);
    } else if (id == "swing") {
        r.rotation = 0.7 * (1 - p) * (1 - p) * std::cos(2.5 * kPi * p);
        r.opacity = fadeIn(p, 0.3);
    } else if (id == "wipe_right" || id == "wipe_up") {
        r.wipe = id == "wipe_right" ? 1 : 2;
        r.reveal = smooth(p);
    } else if (id == "typewriter") {
        r.opacity = smooth(stagger(p, i, n, 0.92));
    } else if (id == "fade_letters") {
        r.opacity = smooth(stagger(p, i, n, 0.6));
    } else if (id == "rise_letters") {
        const double l = stagger(p, i, n, 0.6);
        r.dy = 0.6 * (1 - outCubic(l));
        r.opacity = fadeIn(l, 0.7);
    } else if (id == "drop_letters") {
        const double l = stagger(p, i, n, 0.55);
        r.dy = -0.9 * (1 - outBounce(l));
        r.opacity = fadeIn(l, 0.4);
    } else if (id == "pop_letters") {
        const double l = stagger(p, i, n, 0.55);
        r.sx = r.sy = std::max(0.0, outBack(l));
        r.opacity = fadeIn(l, 0.3);
    } else if (id == "blur_letters") {
        const double l = stagger(p, i, n, 0.6);
        r.blur = 0.3 * (1 - smooth(l));
        r.opacity = smooth(l);
    } else if (id == "wave_in") {
        const double l = stagger(p, i, n, 0.6);
        r.dy = 0.5 * std::sin(l * kPi * 2.5) * (1 - l);
        r.opacity = fadeIn(l, 0.5);
    } else if (id == "rotate_letters") {
        const double l = stagger(p, i, n, 0.5);
        r.rotation = kPi * (1 - outCubic(l));
        r.sx = r.sy = 0.3 + 0.7 * outCubic(l);
        r.opacity = fadeIn(l, 0.5);
    } else if (id == "jump_letters") {
        const double l = stagger(p, i, n, 0.6);
        r.dy = -0.6 * std::sin(kPi * l);
        r.opacity = fadeIn(l, 0.35);
    } else if (id == "shutter") {
        const double l = stagger(p, i, n, 0.5);
        r.sy = outCubic(l);
        r.opacity = fadeIn(l, 0.3);
    } else if (id == "scatter") {
        const double a = 2 * kPi * hash01(static_cast<unsigned>(i), seed);
        const double d = 1.5 + 1.5 * hash01(static_cast<unsigned>(i) + 101u, seed);
        r.dx = std::cos(a) * d * (1 - e);
        r.dy = std::sin(a) * d * (1 - e);
        r.rotation = (hash01(static_cast<unsigned>(i) + 202u, seed) - 0.5) * kPi * (1 - e);
        r.blur = 0.2 * (1 - e);
        r.opacity = fadeIn(p, 0.6);
    } else if (id == "split") {
        const double side = n <= 1 ? 0 : (2.0 * i < n - 1 ? -1 : (2.0 * i > n - 1 ? 1 : 0));
        r.dx = side * 2.0 * (1 - e);
        r.opacity = fadeIn(p, 0.6);
    } else if (id == "tracking") {
        r.dx = (i - (n - 1) / 2.0) * 0.7 * (1 - e);
        r.blur = 0.12 * (1 - smooth(p));
        r.opacity = fadeIn(p, 0.7);
    } else {
        r.opacity = smooth(p);  // unknown: a plain fade, never a jump
    }
    return r;
}

Pose loopPose(const std::string& id, double t, double speed, int i, int, double a) {
    Pose r;
    if (a <= 0) return r;
    const double w = 2 * kPi * std::max(0.05, speed);
    if (id == "pulse") {
        r.sx = r.sy = 1 + 0.06 * a * std::sin(w * 1.2 * t);
    } else if (id == "heartbeat") {
        const double ph = t * std::max(0.05, speed) - std::floor(t * std::max(0.05, speed));
        const double hb = std::exp(-std::pow((ph - 0.12) / 0.06, 2)) + 0.6 * std::exp(-std::pow((ph - 0.34) / 0.06, 2));
        r.sx = r.sy = 1 + 0.1 * a * hb;
    } else if (id == "float") {
        r.dy = 0.08 * a * std::sin(w * 0.5 * t);
        r.rotation = 0.03 * a * std::sin(w * 0.35 * t);
    } else if (id == "swing") {
        r.rotation = 0.12 * a * std::sin(w * 0.8 * t);
    } else if (id == "shake") {
        r.dx = 0.03 * a * (std::sin(w * 7.3 * t) + std::sin(w * 11.1 * t + 1)) / 2;
        r.dy = 0.03 * a * (std::sin(w * 8.7 * t + 2) + std::sin(w * 12.9 * t)) / 2;
        r.rotation = 0.02 * a * std::sin(w * 9.1 * t);
    } else if (id == "jelly") {
        const double s = std::sin(w * 1.6 * t);
        r.sx = 1 + 0.07 * a * s;
        r.sy = 1 - 0.07 * a * s;
    } else if (id == "blink") {
        r.opacity = 1 - 0.55 * a * (0.5 - 0.5 * std::cos(w * t));
    } else if (id == "wave") {
        r.dy = 0.12 * a * std::sin(w * 0.9 * t - i * 0.6);
    } else if (id == "bounce") {
        const double s = std::max(0.0, std::sin(w * 1.1 * t - i * 0.5));
        r.dy = -0.18 * a * s * s;
    }
    return r;
}

Timing timing(const Json& c, double local, double length) {
    Timing t;
    t.seconds = local;
    auto spec = [&](const char* key, std::string& type, double def) {
        type.clear();
        if (!c.is_object()) return 0.0;
        auto it = c.find(key);
        if (it == c.end() || !it->is_object()) return 0.0;
        type = it->value("type", std::string{});
        if (type.empty()) return 0.0;
        return std::max(0.0, it->value("duration", def));
    };
    double din = spec("in", t.in, kDefaultIn), dout = spec("out", t.out, kDefaultOut);
    length = std::max(length, 1e-6);
    if (din + dout > length) {  // short clip: both shrink so they never overlap
        const double k = length / (din + dout);
        din *= k;
        dout *= k;
    }
    t.inProgress = din > 0 ? clamp01(local / din) : 1;
    t.outProgress = dout > 0 ? clamp01((local - (length - dout)) / dout) : 0;
    if (auto it = c.is_object() ? c.find("loop") : c.end(); c.is_object() && it != c.end() && it->is_object()) {
        t.loop = it->value("type", std::string{});
        t.loopSpeed = it->value("speed", 1.0);
    }
    if (!t.loop.empty()) {
        constexpr double ramp = 0.35;
        t.loopAmount = smooth((local - din) / ramp) * smooth((length - dout - local) / ramp);
    }
    return t;
}

Pose pose(const Timing& t, int i, int n, unsigned seed) {
    Pose p;
    if (!t.in.empty() && t.inProgress < 1) p = p * inPose(t.in, t.inProgress, i, n, seed);
    if (!t.out.empty() && t.outProgress > 0) p = p * inPose(t.out, 1 - t.outProgress, i, n, seed + 7u);
    if (!t.loop.empty() && t.loopAmount > 0) p = p * loopPose(t.loop, t.seconds, t.loopSpeed, i, n, t.loopAmount);
    return p;
}

}  // namespace vstudio::motion
