#include "vstudio/transitions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>

#include "vstudio/pixel.hpp"

namespace vstudio {

const std::vector<TransitionInfo>& transitionCatalog() {
    static const std::vector<TransitionInfo> all = {
        {"fade", "Hòa tan", "Cơ bản"},
        {"dip_black", "Qua màu đen", "Cơ bản"},
        {"dip_white", "Qua màu trắng", "Cơ bản"},
        {"flash", "Lóe sáng", "Cơ bản"},
        {"blur", "Mờ nhòe", "Cơ bản"},
        {"luma", "Theo độ sáng", "Cơ bản"},
        {"slide_left", "Trượt sang trái", "Chuyển động"},
        {"slide_right", "Trượt sang phải", "Chuyển động"},
        {"slide_up", "Trượt lên", "Chuyển động"},
        {"slide_down", "Trượt xuống", "Chuyển động"},
        {"push_left", "Đẩy sang trái", "Chuyển động"},
        {"push_right", "Đẩy sang phải", "Chuyển động"},
        {"push_up", "Đẩy lên", "Chuyển động"},
        {"push_down", "Đẩy xuống", "Chuyển động"},
        {"zoom_in", "Phóng to", "Chuyển động"},
        {"zoom_out", "Thu nhỏ", "Chuyển động"},
        {"spin", "Xoay", "Chuyển động"},
        {"wipe_left", "Lau sang trái", "Lau"},
        {"wipe_right", "Lau sang phải", "Lau"},
        {"wipe_up", "Lau lên", "Lau"},
        {"wipe_down", "Lau xuống", "Lau"},
        {"wipe_diagonal", "Lau chéo", "Lau"},
        {"split_h", "Mở cửa ngang", "Lau"},
        {"split_v", "Mở cửa dọc", "Lau"},
        {"blinds", "Rèm sáo", "Lau"},
        {"clock", "Kim đồng hồ", "Lau"},
        {"circle_open", "Mở tròn", "Hình dạng"},
        {"circle_close", "Khép tròn", "Hình dạng"},
        {"diamond", "Hình thoi", "Hình dạng"},
        {"checker", "Ô bàn cờ", "Hình dạng"},
        {"pixelate", "Vỡ điểm ảnh", "Hiệu ứng"},
        {"glitch", "Nhiễu số", "Hiệu ứng"},
        {"ripple", "Gợn sóng", "Hiệu ứng"},
    };
    return all;
}

const TransitionInfo* findTransition(const std::string& id) {
    for (const auto& t : transitionCatalog())
        if (t.id == id) return &t;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Where transitions play
// ---------------------------------------------------------------------------

namespace {

const Time kTouch = Time::fromMilliseconds(1);  // clips closer than this touch

bool touches(const Clip& a, const Clip& b) {
    const std::int64_t gap = (b.timelineIn - a.timelineOut()).ticks();
    return gap <= kTouch.ticks() && gap >= -kTouch.ticks();
}

bool validTransition(const Json& j) {
    return j.is_object() && j.contains("type") && j["type"].is_string() && findTransition(j["type"].get<std::string>());
}

Time requested(const Json& j) {
    Time d = defaultTransitionDuration();
    if (auto it = j.find("duration"); it != j.end() && it->is_number_integer()) d = Time::fromTicks(it->get<std::int64_t>());
    return std::clamp(d, minTransitionDuration(), maxTransitionDuration());
}

struct Window {
    Time start, end, at;  // `at`: the cut (or the free edge)
    const Clip* from = nullptr;
    const Clip* to = nullptr;
    const Json* tr = nullptr;
};

// Windows never reach past half a clip, so the two ends of a clip never meet.
std::optional<Window> inWindow(const Track& track, std::size_t i) {
    const Clip& c = track.clips[i];
    if (!validTransition(c.transitionIn)) return std::nullopt;
    const Clip* prev = i > 0 && touches(track.clips[i - 1], c) ? &track.clips[i - 1] : nullptr;
    Window w;
    w.at = c.timelineIn;
    w.to = &c;
    w.from = prev;
    w.tr = &c.transitionIn;
    if (prev) {
        const Time d = std::min({requested(c.transitionIn), prev->duration(), c.duration()});
        w.start = w.at - Time::fromTicks(d.ticks() / 2);
        w.end = w.start + d;
    } else {
        const Time d = std::min(requested(c.transitionIn), Time::fromTicks(c.duration().ticks() / 2));
        w.start = w.at;
        w.end = w.at + d;
    }
    if (w.end <= w.start) return std::nullopt;
    return w;
}

std::optional<Window> outWindow(const Track& track, std::size_t i) {
    const Clip& c = track.clips[i];
    if (!validTransition(c.transitionOut)) return std::nullopt;
    if (i + 1 < track.clips.size() && touches(c, track.clips[i + 1])) return std::nullopt;  // the cut decides
    const Time d = std::min(requested(c.transitionOut), Time::fromTicks(c.duration().ticks() / 2));
    Window w;
    w.at = c.timelineOut();
    w.start = w.at - d;
    w.end = w.at;
    w.from = &c;
    w.tr = &c.transitionOut;
    if (w.end <= w.start) return std::nullopt;
    return w;
}

// Index of the clip containing t (or the last one starting before it).
std::optional<std::size_t> indexAt(const Track& track, Time t) {
    const auto it = std::upper_bound(track.clips.begin(), track.clips.end(), t,
                                     [](Time v, const Clip& c) { return v < c.timelineIn; });
    if (it == track.clips.begin()) return std::nullopt;
    return static_cast<std::size_t>(it - track.clips.begin()) - 1;
}

std::optional<std::size_t> indexOf(const Track& track, const Clip& c) {
    if (!track.clips.empty() && &c >= track.clips.data() && &c < track.clips.data() + track.clips.size())
        return static_cast<std::size_t>(&c - track.clips.data());
    for (std::size_t i = 0; i < track.clips.size(); ++i)
        if (track.clips[i].id == c.id) return i;
    return std::nullopt;
}

ActiveTransition active(const Window& w, Time t) {
    ActiveTransition a;
    a.from = w.from;
    a.to = w.to;
    a.type = (*w.tr)["type"].get<std::string>();
    a.start = w.start;
    a.end = w.end;
    a.progress = std::clamp(static_cast<double>((t - w.start).ticks()) / static_cast<double>((w.end - w.start).ticks()),
                            0.0, 1.0);
    return a;
}

double smooth(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

}  // namespace

std::optional<ActiveTransition> transitionAt(const Track& track, Time t) {
    const auto k = indexAt(track, t);
    if (!k) {
        // Before the first clip: only the first clip's cut window could reach here (it cannot: no clip before it).
        return std::nullopt;
    }
    auto inside = [&](const std::optional<Window>& w) { return w && t >= w->start && t < w->end; };
    // The next clip's cut reaches back into this one.
    if (*k + 1 < track.clips.size())
        if (auto w = inWindow(track, *k + 1); w && w->from && inside(w)) return active(*w, t);
    if (t >= track.clips[*k].timelineOut()) return std::nullopt;  // in a gap
    if (auto w = inWindow(track, *k); inside(w)) return active(*w, t);
    if (auto w = outWindow(track, *k); inside(w)) return active(*w, t);
    return std::nullopt;
}

std::vector<CutPoint> cutPoints(const Track& track) {
    std::vector<CutPoint> out;
    for (std::size_t i = 0; i < track.clips.size(); ++i) {
        const Clip& c = track.clips[i];
        const bool joined = i > 0 && touches(track.clips[i - 1], c);
        CutPoint in;
        in.at = c.timelineIn;
        in.from = joined ? &track.clips[i - 1] : nullptr;
        in.to = &c;
        if (validTransition(c.transitionIn)) {
            in.transition = c.transitionIn;
            if (auto w = inWindow(track, i)) in.duration = w->end - w->start;
        }
        out.push_back(std::move(in));
        if (i + 1 < track.clips.size() && touches(c, track.clips[i + 1])) continue;
        CutPoint end;
        end.at = c.timelineOut();
        end.from = &c;
        if (validTransition(c.transitionOut)) {
            end.transition = c.transitionOut;
            if (auto w = outWindow(track, i)) end.duration = w->end - w->start;
        }
        out.push_back(std::move(end));
    }
    return out;
}

double transitionGain(const Track& track, const Clip& clip, Time t) {
    const auto i = indexOf(track, clip);
    if (!i) return 1.0;
    double g = 1.0;
    auto frac = [](Time a, Time b) {
        return b > Time::zero() ? static_cast<double>(a.ticks()) / static_cast<double>(b.ticks()) : 1.0;
    };
    // Start: after a cut it rises over the second half of the window; from nothing, over all of it.
    if (auto w = inWindow(track, *i); w && t < w->end) g *= smooth(frac(t - w->at, w->end - w->at));
    // End: before the next clip's cut it falls over the first half; to nothing, over all of it.
    std::optional<Window> out;
    if (*i + 1 < track.clips.size()) {
        if (auto w = inWindow(track, *i + 1); w && w->from == &clip) out = w;
    }
    if (!out) out = outWindow(track, *i);
    if (out && t >= out->start) g *= smooth(frac(out->at - t, out->at - out->start));
    return g;
}

// ---------------------------------------------------------------------------
// Pixels
// ---------------------------------------------------------------------------

namespace {

constexpr double kPi = std::numbers::pi;

struct Px {
    float r = 0, g = 0, b = 0, a = 0;  // premultiplied
};
inline Px operator+(Px p, Px q) { return {p.r + q.r, p.g + q.g, p.b + q.b, p.a + q.a}; }
inline Px operator*(Px p, float k) { return {p.r * k, p.g * k, p.b * k, p.a * k}; }
inline Px lerp(Px p, Px q, float k) { return p * (1 - k) + q * k; }
inline Px over(Px top, Px under) { return top + under * (1 - top.a); }

// Premultiplied float reads from an RGBA8 or RGBA16F frame; outside is transparent.
struct Img {
    const VideoFrame& f;
    [[nodiscard]] Px at(int x, int y) const {
        if (x < 0 || y < 0 || x >= f.width || y >= f.height) return {};
        const std::size_t i = (static_cast<std::size_t>(y) * f.width + x) * 4;
        Px p;
        if (f.deep()) {
            p = {halfToFloat(f.half[i]), halfToFloat(f.half[i + 1]), halfToFloat(f.half[i + 2]),
                 std::clamp(halfToFloat(f.half[i + 3]), 0.0f, 1.0f)};
        } else {
            constexpr float k = 1.0f / 255.0f;
            p = {f.rgba[i] * k, f.rgba[i + 1] * k, f.rgba[i + 2] * k, f.rgba[i + 3] * k};
        }
        p.r *= p.a;
        p.g *= p.a;
        p.b *= p.a;
        return p;
    }
    // Bilinear with the border repeated: for distortions that must not show black edges.
    [[nodiscard]] Px clamped(double x, double y) const {
        return sample(std::clamp(x, 0.5, f.width - 0.5), std::clamp(y, 0.5, f.height - 0.5));
    }
    // Bilinear at pixel coordinates (centres at +0.5).
    [[nodiscard]] Px sample(double x, double y) const {
        x -= 0.5;
        y -= 0.5;
        const double fx0 = std::floor(x), fy0 = std::floor(y);
        if (fx0 < -2 || fy0 < -2 || fx0 > f.width + 1 || fy0 > f.height + 1) return {};
        const int x0 = static_cast<int>(fx0), y0 = static_cast<int>(fy0);
        const float fx = static_cast<float>(x - fx0), fy = static_cast<float>(y - fy0);
        return lerp(lerp(at(x0, y0), at(x0 + 1, y0), fx), lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), fx), fy);
    }
};

void write(VideoFrame& out, int x, int y, Px p) {
    const float a = std::clamp(p.a, 0.0f, 1.0f);
    const float inv = a > 1e-6f ? 1.0f / a : 0.0f;
    const std::size_t i = (static_cast<std::size_t>(y) * out.width + x) * 4;
    if (out.deep()) {
        out.half[i] = floatToHalf(p.r * inv);
        out.half[i + 1] = floatToHalf(p.g * inv);
        out.half[i + 2] = floatToHalf(p.b * inv);
        out.half[i + 3] = floatToHalf(a);
    } else {
        auto q = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        out.rgba[i] = q(p.r * inv);
        out.rgba[i + 1] = q(p.g * inv);
        out.rgba[i + 2] = q(p.b * inv);
        out.rgba[i + 3] = q(a);
    }
}

template <class F>
void each(VideoFrame& out, F&& f) {
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) write(out, x, y, f(x, y));
}

// Ease in and out: smooth start and end.
double cubic(double p) { return p < 0.5 ? 4 * p * p * p : 1 - std::pow(-2 * p + 2, 3) / 2; }

float soft(double edge0, double edge1, double x) { return static_cast<float>(smooth((x - edge0) / (edge1 - edge0))); }

std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
double rnd(std::uint32_t a, std::uint32_t b = 0) { return hash(a * 0x9E3779B9U ^ hash(b + 0x632BE5ABU)) / 4294967296.0; }

// Small blurred copies for the blur transition: 1/4 and 1/16 size, box filtered then smoothed.
struct Small {
    int w = 0, h = 0, k = 1;
    std::vector<Px> v;
    [[nodiscard]] Px at(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return v[static_cast<std::size_t>(y) * w + x];
    }
    // At full-size pixel coordinates.
    [[nodiscard]] Px sample(double fx, double fy) const {
        const double x = fx / k - 0.5, y = fy / k - 0.5;
        const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        const float ax = static_cast<float>(x - x0), ay = static_cast<float>(y - y0);
        return lerp(lerp(at(x0, y0), at(x0 + 1, y0), ax), lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), ax), ay);
    }
};

Small shrink(int srcW, int srcH, int k, const std::function<Px(int, int)>& src) {
    Small s;
    s.k = k;
    s.w = std::max(1, (srcW + k - 1) / k);
    s.h = std::max(1, (srcH + k - 1) / k);
    s.v.resize(static_cast<std::size_t>(s.w) * s.h);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            Px sum;
            int n = 0;
            for (int j = 0; j < k; ++j)
                for (int i = 0; i < k; ++i) {
                    const int sx = x * k + i, sy = y * k + j;
                    if (sx >= srcW || sy >= srcH) continue;
                    sum = sum + src(sx, sy);
                    ++n;
                }
            s.v[static_cast<std::size_t>(y) * s.w + x] = n ? sum * (1.0f / n) : Px{};
        }
    // Two 3x3 smoothing passes: the upsampled result has no visible blocks.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<Px> t(s.v.size());
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x) {
                Px sum;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i) sum = sum + s.at(x + i, y + j) * ((i ? 1.0f : 2.0f) * (j ? 1.0f : 2.0f));
                t[static_cast<std::size_t>(y) * s.w + x] = sum * (1.0f / 16.0f);
            }
        s.v.swap(t);
    }
    return s;
}

struct Pyramid {
    Small l1, l2;
    Pyramid(const Img& img, int w, int h) {
        const int k1 = std::max(2, std::min(w, h) / 90);  // ~1/4 of a 360p frame, scale independent
        l1 = shrink(w, h, k1, [&](int x, int y) { return img.at(x, y); });
        l2 = shrink(l1.w, l1.h, 4, [&](int x, int y) { return l1.at(x, y); });
        l2.k = k1 * 4;
    }
    // amount 0 = sharp, 1 = strongest.
    [[nodiscard]] Px at(const Img& img, int x, int y, double amount) const {
        if (amount <= 0.001) return img.at(x, y);
        const double fx = x + 0.5, fy = y + 0.5;
        if (amount < 0.5) return lerp(img.at(x, y), l1.sample(fx, fy), static_cast<float>(amount * 2));
        return lerp(l1.sample(fx, fy), l2.sample(fx, fy), static_cast<float>(amount * 2 - 1));
    }
};

}  // namespace

void applyTransition(const std::string& type, const VideoFrame& a, const VideoFrame& b, double progress,
                     VideoFrame& out, Time t) {
    if (out.empty()) return;
    const Img A{a}, B{b};
    const int W = out.width, H = out.height;
    const double p = std::clamp(progress, 0.0, 1.0);
    const double e = smooth(p), c = cubic(p);
    const float ef = static_cast<float>(e);
    const double aspect = static_cast<double>(W) / H;
    auto uv = [&](int x, int y) { return std::pair{(x + 0.5) / W, (y + 0.5) / H}; };
    // Reveal B where `s` (0..1) is below a threshold sweeping 0 -> 1 with a soft edge of `f`.
    auto reveal = [&](double s, double f, double prog = -1) {
        const double thr = (prog < 0 ? c : prog) * (1 + 2 * f) - f;
        return 1.0f - soft(thr - f, thr + f, s);
    };
    auto mixMask = [&](auto mask) {
        each(out, [&](int x, int y) { return lerp(A.at(x, y), B.at(x, y), mask(x, y)); });
    };

    if (type == "dip_black" || type == "dip_white") {
        const float v = type == "dip_black" ? 0.0f : 1.0f;
        const Px col{v, v, v, 1};
        if (p < 0.5) {
            const float k = static_cast<float>(smooth(p * 2));
            each(out, [&](int x, int y) { return lerp(A.at(x, y), col, k); });
        } else {
            const float k = static_cast<float>(smooth(p * 2 - 1));
            each(out, [&](int x, int y) { return lerp(col, B.at(x, y), k); });
        }
    } else if (type == "flash") {
        const float f = static_cast<float>(std::pow(std::sin(kPi * p), 2) * 0.92);
        each(out, [&](int x, int y) {
            Px q = lerp(A.at(x, y), B.at(x, y), ef);
            q.r += (q.a - q.r) * f;
            q.g += (q.a - q.g) * f;
            q.b += (q.a - q.b) * f;
            return q;
        });
    } else if (type == "blur") {
        const double amount = std::sin(kPi * p);
        const Pyramid pa(A, W, H), pb(B, W, H);
        each(out, [&](int x, int y) { return lerp(pa.at(A, x, y, amount), pb.at(B, x, y, amount), ef); });
    } else if (type == "luma") {
        mixMask([&](int x, int y) {
            const Px q = A.at(x, y);
            const double l = q.a > 0 ? (0.299 * q.r + 0.587 * q.g + 0.114 * q.b) / q.a : 0;
            return reveal(1 - l, 0.2, e);  // bright parts give way first
        });
    } else if (type.rfind("slide_", 0) == 0 || type.rfind("push_", 0) == 0) {
        const bool push = type[0] == 'p';
        const std::string dir = type.substr(push ? 5 : 6);
        // Direction the pictures travel, in frame sizes.
        const double dx = dir == "left" ? -1 : dir == "right" ? 1 : 0;
        const double dy = dir == "up" ? -1 : dir == "down" ? 1 : 0;
        const double bx = -dx * (1 - c) * W, by = -dy * (1 - c) * H;  // B comes from the far side
        const double ax = dx * c * W, ay = dy * c * H;
        each(out, [&](int x, int y) {
            const Px nb = B.sample(x + 0.5 - bx, y + 0.5 - by);
            const Px na = push ? A.sample(x + 0.5 - ax, y + 0.5 - ay) : A.at(x, y);
            return over(nb, na);
        });
    } else if (type == "zoom_in" || type == "zoom_out" || type == "spin") {
        double sa, sb, ra = 0, rb = 0;
        if (type == "zoom_in") {
            sa = 1 + 0.8 * c;   // into the outgoing picture...
            sb = 1.25 - 0.25 * c;  // ...and the next one settles from slightly close
        } else if (type == "zoom_out") {
            sa = 1 - 0.15 * c;
            sb = 1.6 - 0.6 * c;
        } else {
            sa = sb = 1 + 1.1 * std::sin(kPi * p);  // zoomed enough that the corners stay covered
            ra = kPi / 4 * c;                        // a quarter turn shared by both pictures
            rb = ra - kPi / 4;
        }
        const double cx = W / 2.0, cy = H / 2.0;
        auto at = [&](const Img& img, double s, double r, int x, int y) {
            const double px = x + 0.5 - cx, py = y + 0.5 - cy;
            const double cr = std::cos(-r), sr = std::sin(-r);
            return img.sample(cx + (px * cr - py * sr) / s, cy + (px * sr + py * cr) / s);
        };
        if (type == "zoom_out") {
            // The outgoing picture shrinks away on top of the incoming one, which settles from close up.
            each(out, [&](int x, int y) { return over(at(A, sa, ra, x, y) * (1 - ef), at(B, sb, rb, x, y)); });
        } else {
            each(out, [&](int x, int y) { return lerp(at(A, sa, ra, x, y), at(B, sb, rb, x, y), ef); });
        }
    } else if (type.rfind("wipe_", 0) == 0) {
        const std::string dir = type.substr(5);
        mixMask([&](int x, int y) {
            const auto [u, v] = uv(x, y);
            const double s = dir == "left" ? 1 - u : dir == "right" ? u : dir == "up" ? 1 - v : dir == "down" ? v : (u + v) / 2;
            return reveal(s, 0.04);
        });
    } else if (type == "split_h" || type == "split_v") {
        const bool h = type == "split_h";
        mixMask([&](int x, int y) {
            const auto [u, v] = uv(x, y);
            return reveal(std::abs((h ? u : v) - 0.5) * 2, 0.04);
        });
    } else if (type == "blinds") {
        constexpr int kSlats = 8;
        mixMask([&](int x, int y) {
            const double v = uv(x, y).second * kSlats;
            return reveal(v - std::floor(v), 0.12);
        });
    } else if (type == "clock") {
        mixMask([&](int x, int y) {
            const auto [u, v] = uv(x, y);
            double ang = std::atan2((u - 0.5) * aspect, -(v - 0.5)) / (2 * kPi);  // clockwise from 12 o'clock
            if (ang < 0) ang += 1;
            return reveal(ang, 0.015);
        });
    } else if (type == "circle_open" || type == "circle_close" || type == "diamond") {
        const double far = type == "diamond" ? 0.5 * aspect + 0.5 : std::hypot(0.5 * aspect, 0.5);
        mixMask([&](int x, int y) {
            const auto [u, v] = uv(x, y);
            const double dx = (u - 0.5) * aspect, dy = v - 0.5;
            const double d = (type == "diamond" ? std::abs(dx) + std::abs(dy) : std::hypot(dx, dy)) / far;
            return type == "circle_close" ? reveal(1 - d, 0.04) : reveal(d, 0.04);
        });
    } else if (type == "checker") {
        const int nx = 8, ny = std::max(1, static_cast<int>(std::lround(8 / aspect)));
        mixMask([&](int x, int y) {
            const int cx = std::min(nx - 1, x * nx / W), cy = std::min(ny - 1, y * ny / H);
            const double delay = 0.5 * rnd(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy));
            return static_cast<float>(smooth((p - delay) / 0.5));
        });
    } else if (type == "pixelate") {
        const double cell = 1 + std::sin(kPi * p) * std::max(W, H) / 18.0;
        each(out, [&](int x, int y) {
            const double sx = (std::floor((x + 0.5) / cell) + 0.5) * cell, sy = (std::floor((y + 0.5) / cell) + 0.5) * cell;
            return lerp(A.clamped(sx, sy), B.clamped(sx, sy), ef);
        });
    } else if (type == "glitch") {
        const double amp = std::pow(std::sin(kPi * p), 2) * 0.07 * W;
        const auto seed = static_cast<std::uint32_t>(t.ticks() / (Time::kTicksPerSecond / 24));  // changes 24x a second
        const int bands = std::max(8, H / 18);
        each(out, [&](int x, int y) {
            const auto band = static_cast<std::uint32_t>(y * bands / H);
            const double r = rnd(band, seed);
            const double off = (r - 0.5) * 2 * amp * (r > 0.55 ? 1.0 : 0.25);
            const float m = soft(0.3, 0.7, p + (rnd(band + 999, seed) - 0.5) * 0.25);
            auto at = [&](double dx) {
                return lerp(A.clamped(x + 0.5 + off + dx, y + 0.5), B.clamped(x + 0.5 + off + dx, y + 0.5), m);
            };
            const Px mid = at(0), red = at(amp * 0.25), blue = at(-amp * 0.25);  // colour fringes
            return Px{red.r, mid.g, blue.b, mid.a};
        });
    } else if (type == "ripple") {
        const double amp = 0.025 * std::sin(kPi * p) * std::min(W, H);
        const double cx = W / 2.0, cy = H / 2.0, scale = 1.0 / std::min(W, H);
        each(out, [&](int x, int y) {
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            const double r = std::hypot(dx, dy);
            const double d = amp * std::sin(r * scale * 40 - p * 18);
            const double nx = r > 0 ? dx / r : 0, ny = r > 0 ? dy / r : 0;
            const double sx = x + 0.5 + nx * d, sy = y + 0.5 + ny * d;
            return lerp(A.clamped(sx, sy), B.clamped(sx, sy), ef);
        });
    } else {  // "fade" and anything unknown: cross dissolve
        each(out, [&](int x, int y) { return lerp(A.at(x, y), B.at(x, y), ef); });
    }
}

}  // namespace vstudio
