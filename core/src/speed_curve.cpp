#include "vstudio/speed_curve.hpp"

#include <algorithm>
#include <cmath>

#include "vstudio/clip_tools.hpp"

namespace vstudio::speed {

const std::vector<CurvePreset>& curvePresets() {
    static const std::vector<CurvePreset> presets = {
        {"montage", "Dồn nhịp", {{0, 1}, {0.3, 1}, {0.4, 3}, {0.6, 3}, {0.7, 1}, {1, 1}}},
        {"hero", "Anh hùng", {{0, 2}, {0.35, 2}, {0.45, 0.4}, {0.55, 0.4}, {0.65, 2}, {1, 2}}},
        {"bullet", "Đạn bay", {{0, 3}, {0.4, 3}, {0.5, 0.15}, {0.6, 3}, {1, 3}}},
        {"jump", "Nhảy cóc", {{0, 1}, {0.45, 1}, {0.5, 8}, {0.55, 1}, {1, 1}}},
        {"flash_in", "Lao vào", {{0, 6}, {0.3, 1}, {1, 1}}},
        {"flash_out", "Lao ra", {{0, 1}, {0.7, 1}, {1, 6}}},
        {"ramp_up", "Nhanh dần", {{0, 0.5}, {1, 3}}},
        {"ramp_down", "Chậm dần", {{0, 3}, {1, 0.5}}},
    };
    return presets;
}

const CurvePreset* findCurvePreset(const std::string& id) {
    for (const auto& p : curvePresets())
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<SpeedKey> keysFor(const Clip& c, const std::vector<CurvePoint>& points) {
    std::vector<SpeedKey> keys;
    const double len = c.sourceDuration().seconds();
    for (const auto& p : points) {
        const double u = std::clamp(p.u, 0.0, 1.0);
        keys.push_back({c.sourceIn + Time::fromSeconds(u * len), std::clamp(p.v, kMinCurveSpeed, kMaxCurveSpeed)});
    }
    std::sort(keys.begin(), keys.end(), [](const SpeedKey& a, const SpeedKey& b) { return a.t < b.t; });
    // Two keys at the same time would make a jump: keep the later one.
    const auto kept = std::unique(keys.rbegin(), keys.rend(), [](const SpeedKey& a, const SpeedKey& b) { return a.t == b.t; });
    keys.erase(keys.begin(), kept.base());  // unique() on the reversed range packs the kept keys at the back
    return keys;
}

std::vector<CurvePoint> pointsOf(const Clip& c) {
    std::vector<CurvePoint> out;
    const double len = std::max(1e-9, c.sourceDuration().seconds());
    for (const auto& k : c.speedCurve) out.push_back({(k.t - c.sourceIn).seconds() / len, k.v});
    return out;
}

double baseSpeed(const Clip& c) { return static_cast<double>(c.speed.num) / c.speed.den; }

Rational toRational(double s) {
    s = std::clamp(s, kMinSpeed, kMaxSpeed);
    return Rational{std::llround(s * 1000), 1000}.reduced();
}

bool canChangeSpeed(const Project& p, const Clip& c, std::string* why) {
    auto no = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    if (c.generated()) return no("text, stickers and effects have no speed");
    if (cliptools::isFreeze(c)) return no("a freeze frame has no speed");
    const MediaAsset* a = p.findAsset(c.assetId);
    if (!a) return no("clip has no media");
    if (a->type == MediaType::Image) return no("photos have no speed");
    return true;
}

}  // namespace vstudio::speed
