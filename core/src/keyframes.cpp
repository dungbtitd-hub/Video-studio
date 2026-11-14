#include "vstudio/keyframes.hpp"

#include <algorithm>
#include <cmath>

namespace vstudio {

namespace kf {
const std::vector<std::string>& properties() {
    static const std::vector<std::string> p = {kScale, kX, kY, kOpacity, kVolume};
    return p;
}
const std::vector<std::string>& properties(Group g) {
    static const std::vector<std::string> picture = {kScale, kX, kY, kOpacity}, audio = {kVolume};
    return g == Group::Audio ? audio : picture;
}
Group groupOf(const std::string& property) { return property == kVolume ? Group::Audio : Group::Picture; }
bool isProperty(const std::string& name) {
    const auto& p = properties();
    return std::find(p.begin(), p.end(), name) != p.end();
}
double clampValue(const std::string& property, double v) {
    if (!std::isfinite(v)) v = 0;
    if (property == kScale) return std::clamp(v, 0.05, 20.0);
    if (property == kOpacity) return std::clamp(v, 0.0, 1.0);
    if (property == kVolume) return std::clamp(v, 0.0, 4.0);
    return std::clamp(v, -100000.0, 100000.0);  // x / y
}
}  // namespace kf

std::string toString(Ease e) {
    switch (e) {
        case Ease::Linear: return "linear";
        case Ease::Hold: return "hold";
        default: return "smooth";
    }
}

Ease easeFrom(const std::string& s) {
    if (s == "linear") return Ease::Linear;
    if (s == "hold") return Ease::Hold;
    return Ease::Smooth;
}

Json keyframeJson(const Keyframe& k) { return Json{{"t", k.t.ticks()}, {"v", k.v}, {"ease", toString(k.ease)}}; }

KeyframeCurve::KeyframeCurve(const Json& keys) {
    if (!keys.is_array()) return;
    for (const auto& k : keys) {
        if (!k.is_object() || !k.contains("t") || !k.contains("v")) continue;
        Keyframe f;
        f.t = Time::fromTicks(k["t"].get<std::int64_t>());
        f.v = k["v"].get<double>();
        f.ease = easeFrom(k.value("ease", std::string{}));
        keys_.push_back(f);
    }
    std::stable_sort(keys_.begin(), keys_.end(), [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
}

KeyframeCurve::Segment KeyframeCurve::segment(Time t) const {
    if (t <= keys_.front().t) return {&keys_.front(), &keys_.front(), 0.0};
    if (t >= keys_.back().t) return {&keys_.back(), &keys_.back(), 0.0};
    const auto next = std::upper_bound(keys_.begin(), keys_.end(), t, [](Time x, const Keyframe& k) { return x < k.t; });
    const Keyframe& a = *(next - 1);
    const Keyframe& b = *next;
    const double span = static_cast<double>((b.t - a.t).ticks());
    double u = span > 0 ? static_cast<double>((t - a.t).ticks()) / span : 1.0;
    switch (a.ease) {
        case Ease::Hold: u = 0; break;
        case Ease::Smooth: u = u * u * (3 - 2 * u); break;  // ease in/out, no overshoot
        case Ease::Linear: break;
    }
    return {&a, &b, u};
}

double KeyframeCurve::at(Time t) const {
    if (keys_.empty()) return 0;
    const Segment s = segment(t);
    return s.a->v + (s.b->v - s.a->v) * s.u;
}

std::optional<double> keyframeValue(const Clip& c, const std::string& property, Time sourceTime) {
    if (!c.keyframes.is_object()) return std::nullopt;
    const auto it = c.keyframes.find(property);
    if (it == c.keyframes.end()) return std::nullopt;
    KeyframeCurve curve(*it);
    if (curve.empty()) return std::nullopt;
    return curve.at(sourceTime);
}

Transform animatedTransform(const Clip& c, Time sourceTime) {
    Transform t = Transform::fromJson(c.transform);
    if (!c.keyframes.is_object() || c.keyframes.empty()) return t;
    auto curve = [&](const char* prop) {
        const auto it = c.keyframes.find(prop);
        return it == c.keyframes.end() ? KeyframeCurve() : KeyframeCurve(*it);
    };
    const KeyframeCurve scale = curve(kf::kScale), x = curve(kf::kX), y = curve(kf::kY), opacity = curve(kf::kOpacity);
    auto scaleAt = [&](Time when) {
        if (scale.empty()) return t.scale;
        const auto s = scale.segment(when);
        const double a = std::max(0.05, s.a->v), b = std::max(0.05, s.b->v);
        return a * std::pow(b / a, s.u);  // geometric: even-looking zoom speed
    };
    const double baseScale = t.scale;
    t.scale = scaleAt(sourceTime);
    // While the zoom changes, position pivots about the one picture point that is at the same
    // place on screen at both keys: a true "zoom into" that point, every part of the picture
    // moving straight away from it, nothing drifting sideways.
    auto position = [&](const KeyframeCurve& k, double fallback) {
        if (k.empty()) return fallback * (scale.empty() ? 1.0 : t.scale / baseScale);
        const auto s = k.segment(sourceTime);
        const double lin = s.a->v + (s.b->v - s.a->v) * s.u;
        if (scale.empty() || s.a == s.b) return lin;
        const double sa = scaleAt(s.a->t), sb = scaleAt(s.b->t);
        if (std::abs(sb - sa) < 1e-9 * std::max(sa, sb)) return lin;  // no zoom: plain pan
        const double pivot = (s.a->v - s.b->v) / (sb - sa);
        return s.a->v + pivot * (sa - t.scale);
    };
    t.x = position(x, t.x);
    t.y = position(y, t.y);
    if (!opacity.empty()) t.opacity = std::clamp(opacity.at(sourceTime), 0.0, 1.0);
    return t;
}

double animatedVolume(const Clip& c, Time sourceTime) {
    const auto v = keyframeValue(c, kf::kVolume, sourceTime);
    return c.volume * (v ? *v : 1.0);
}

Transform zoomToRegion(const Transform& cur, double cw, double ch, double rx, double ry, double rw, double rh) {
    Transform t = cur;
    if (rw <= 1 || rh <= 1 || cw <= 0 || ch <= 0) return t;
    const double k = std::min(cw / rw, ch / rh);
    t.scale = kf::clampValue(kf::kScale, cur.scale * k);
    const double applied = t.scale / cur.scale;  // k after clamping
    // The region's centre moves to the canvas centre.
    t.x = -(rx + rw / 2 - cw / 2 - cur.x) * applied;
    t.y = -(ry + rh / 2 - ch / 2 - cur.y) * applied;
    return t;
}

std::vector<Time> keyframeTimes(const Clip& c) {
    std::vector<Time> out;
    if (!c.keyframes.is_object()) return out;
    for (auto it = c.keyframes.begin(); it != c.keyframes.end(); ++it) {
        const KeyframeCurve curve(it.value());  // named: keys() refers into it
        for (const auto& k : curve.keys()) out.push_back(k.t);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace vstudio
