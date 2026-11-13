// Pro colour grading: the "builtin.color_grade" effect.
//
// One effect per clip holds the whole primary + secondary grade, so a grade is
// one undo step, renders identically in preview and export and travels with
// the project as plain JSON (metadata only, sources are never touched).
//
// Order inside the effect (display-referred Rec.709, after Log conversion
// and camera LUTs, before one-tap looks):
//   1. colour wheels: offset, lift (shadows), gamma (midtones), gain (highlights)
//   2. contrast around a pivot
//   3. curves: master (all channels), then red / green / blue
//   4. saturation
//   5. HSL mixer: hue / saturation / luminance for 8 colour bands
//   6. qualifier (secondary): pick a hue / saturation / luminance range with
//      soft edges and change only that (e.g. skin, sky), optionally inverted.
// Steps 1-3 are per channel and are baked into one table per frame, so the
// per-pixel cost is a lookup; steps 5-6 use tables indexed by hue. Rows are
// split across cores for large frames (export); preview frames are small.
//
// Parameters (all optional, missing = neutral):
//   "offset" | "lift" | "gamma" | "gain": [r, g, b, master], each -1..1
//        r,g,b: colour balance from the wheel (luma-neutral), master: the ring.
//   "contrast": 0..2 (1 = none), "pivot": 0..1 (default 0.435, Rec.709 mid-grey)
//   "saturation": 0..2
//   "curves": { "master" | "r" | "g" | "b": [[x, y], ...] } control points 0..1,
//        smooth monotone spline through them (ends default to [0,0] and [1,1]).
//   "hsl": { band: [hue, saturation, luminance] each -1..1 } for bands
//        red, orange, yellow, green, aqua, blue, purple, magenta.
//   "qualifier": { "enabled": true, "hue": [centre deg, width deg],
//        "sat": [lo, hi], "lum": [lo, hi], "softness": 0..0.5, "invert": false,
//        "hue_shift": -1..1, "saturation": -1..1, "exposure": -1..1,
//        "show_mask": false (preview helper: shows the selection in grey) }
#pragma once

#include <array>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio {

inline constexpr const char* kColorGradeEffect = "builtin.color_grade";

struct HslBand {
    std::string id;    // "red", stored in the project
    std::string name;  // Vietnamese UI name
    double hue;        // band centre, degrees
};
const std::vector<HslBand>& hslBands();

void applyColorGrade(VideoFrame& frame, const Json& params);

// True when the parameters leave every picture unchanged (the UI then removes the effect).
bool isNeutralGrade(const Json& params);

// Smooth monotone curve through control points (sorted by x, ends added at 0/1
// when missing), sampled at `n` evenly spaced inputs in 0..1. Exposed for the
// curve editors so the drawn curve is exactly the applied one.
std::vector<float> sampleCurve(const Json& points, int n);

// Wheel helpers for the UIs: a wheel position (angle in degrees, 0 = red,
// counter-clockwise; distance 0..1) to a luma-neutral [r, g, b] balance, and back.
std::array<double, 3> wheelToBalance(double angleDeg, double distance);
void balanceToWheel(const std::array<double, 3>& rgb, double* angleDeg, double* distance);

// Returns `effects` (a clip's effect array) with the effect of `type` set to
// `params` (null removes it). An existing effect keeps its place and id; a new
// grade goes after Log conversion / LUTs at the start of the chain and before
// looks, so it grades Rec.709 pictures. Used by the editor and by live preview.
Json withGradeEffect(const Json& effects, const Json& params, const std::string& effectId);

}  // namespace vstudio
