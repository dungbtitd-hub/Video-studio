// Clip speed: a constant speed (0.1x..10x) or a speed ramp drawn as a curve, like
// slowing down on a jump and speeding up after it. The curve is stored on the clip
// (Clip::speedCurve, keys in source time); this module holds the ready-made shapes and
// turns a shape drawn over the clip (0..1 of its length) into keys and back.
//
// Speed changes the clip's length, so the clips after it on the same track move with
// it (Editor::setClipSpeed). Sound follows the picture, resampled.
#pragma once

#include <string>
#include <vector>

#include "vstudio/model.hpp"

namespace vstudio::speed {

inline constexpr double kMinSpeed = 0.1, kMaxSpeed = 10.0;

// A point of a curve drawn over the clip: at `u` (0..1 of the source range) play `v` x.
struct CurvePoint {
    double u = 0, v = 1;
};

struct CurvePreset {
    std::string id;
    std::string name;  // Vietnamese
    std::vector<CurvePoint> points;
};

// Ready-made ramps (montage, hero, bullet, jump cut, flash in/out, ramp up/down).
const std::vector<CurvePreset>& curvePresets();
const CurvePreset* findCurvePreset(const std::string& id);

// Keys for a clip from points over its source range (sorted, values clamped).
std::vector<SpeedKey> keysFor(const Clip& c, const std::vector<CurvePoint>& points);
// The clip's curve as points over its source range (for editing).
std::vector<CurvePoint> pointsOf(const Clip& c);

// Base speed as a double, and from a double (to 1/1000).
double baseSpeed(const Clip& c);
Rational toRational(double speed);

// Whether a clip's speed can be changed (media video/audio clips; not photos, text, freezes).
bool canChangeSpeed(const Project& p, const Clip& c, std::string* why = nullptr);

}  // namespace vstudio::speed
