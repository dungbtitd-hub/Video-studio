// Keyframes: clip properties that change over time inside one clip, without
// cutting it (zoom in on a subject between two points, fade the music up and
// down). Stored on the clip, evaluated by preview and export alike.
//
// Clip::keyframes = { "<property>": [ {"t": <source ticks>, "v": <value>, "ease": "smooth"}, ... ] }
//   sorted by t. Times are SOURCE times, so keys stay on the same picture when
//   the clip is trimmed, moved, split or sped up (both halves of a split keep
//   the curve and continue it seamlessly).
// Before the first key the first value holds; after the last, the last value.
// ease: "smooth" (ease in/out, the default), "linear", or "hold" (jump at the next key).
// Zoom is interpolated geometrically (2x -> 4x takes as long as 1x -> 2x, so it
// looks even), and position follows the zoom so the chosen point travels
// straight to its place instead of drifting sideways.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vstudio/compositor.hpp"
#include "vstudio/model.hpp"
#include "vstudio/time.hpp"

namespace vstudio {

namespace kf {
inline constexpr const char* kScale = "scale";      // multiplies "fit", 0.05..20
inline constexpr const char* kX = "x";              // canvas pixels from centre
inline constexpr const char* kY = "y";
inline constexpr const char* kOpacity = "opacity";  // 0..1
inline constexpr const char* kVolume = "volume";    // linear gain 0..4 (1 = unchanged)
// Everything that can be animated, in UI order.
const std::vector<std::string>& properties();
// Independent groups: picture keys (scale, x, y, opacity) and audio keys (volume)
// are added, moved and removed separately.
enum class Group { Picture, Audio };
const std::vector<std::string>& properties(Group g);
Group groupOf(const std::string& property);
bool isProperty(const std::string& name);
double clampValue(const std::string& property, double v);
}  // namespace kf

enum class Ease { Smooth, Linear, Hold };

struct Keyframe {
    Time t;  // source time
    double v = 0;
    Ease ease = Ease::Smooth;  // how the value travels to the NEXT key
};

// A property's keys, parsed once (rendering evaluates them every frame / audio block).
class KeyframeCurve {
public:
    KeyframeCurve() = default;
    explicit KeyframeCurve(const Json& keys);
    [[nodiscard]] bool empty() const { return keys_.empty(); }
    [[nodiscard]] double at(Time sourceTime) const;
    // The keys around `t` and the eased progress between them (0..1); a == b outside the keys.
    struct Segment {
        const Keyframe* a;
        const Keyframe* b;
        double u;
    };
    [[nodiscard]] Segment segment(Time sourceTime) const;
    [[nodiscard]] const std::vector<Keyframe>& keys() const { return keys_; }

private:
    std::vector<Keyframe> keys_;
};

Json keyframeJson(const Keyframe& k);
std::string toString(Ease e);
Ease easeFrom(const std::string& s);

// Value of `property` at `sourceTime`, or nullopt when it is not animated.
std::optional<double> keyframeValue(const Clip& c, const std::string& property, Time sourceTime);
// The clip's transform at `sourceTime`: its static transform with animated values applied.
Transform animatedTransform(const Clip& c, Time sourceTime);
// Audio gain at `sourceTime` (clip volume times the animated volume).
double animatedVolume(const Clip& c, Time sourceTime);
// "Zoom to this area": the transform that makes the region (canvas pixels, as the
// clip is shown now with `current`) fill the canvas, keeping its aspect. Desktop
// draws the region with the mouse; it is also the maths behind pinch-to-zoom.
Transform zoomToRegion(const Transform& current, double canvasW, double canvasH, double rx, double ry, double rw,
                       double rh);

// Source times of all keys (any property), sorted and de-duplicated: for "previous/next key".
std::vector<Time> keyframeTimes(const Clip& c);

}  // namespace vstudio
