// Video effects that move the picture inside its frame: zoom (slow push-in, pulse, camera shake),
// rotate (spin, sway, 90°/180° turns, flips) and reflections (a water surface with ripples, a mirror).
//
// Three groups, one effect type each, so a clip can combine one zoom, one rotation and one
// reflection: "builtin.fx_zoom", "builtin.fx_rotate", "builtin.fx_reflect". They run after the
// colour effects and covers (so a blurred region stays on its subject), in the order zoom → rotate →
// reflect. Every frame is a pure function of time (no state): seeking, preview and export match.
//
// Params: {"fx": "<id>", "amount": 1 (0..2), "speed": 1 (0.25..4), "line": 0.62 (reflections: where
// the surface is, 0..1 of the height)}. The renderer adds "clip_length" (seconds) for effects that
// run over the whole clip (slow zoom in/out, slow tilt).
#pragma once

#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio::videofx {

inline constexpr const char* kZoomEffect = "builtin.fx_zoom";
inline constexpr const char* kRotateEffect = "builtin.fx_rotate";
inline constexpr const char* kReflectEffect = "builtin.fx_reflect";

struct VideoFxInfo {
    std::string id;
    std::string name;   // Vietnamese
    std::string group;  // "zoom", "rotate", "reflect"
    bool animated = true;  // moves over time (thumbnails play on hover)
    bool usesLine = false; // reflections: the surface line can be moved
};

const std::vector<VideoFxInfo>& videoFxCatalog();
[[nodiscard]] const VideoFxInfo* findVideoFx(const std::string& id);
// "Phóng to", "Xoay", "Phản chiếu"
std::string groupName(const std::string& group);
// The effect type that holds a group's effect.
std::string effectTypeFor(const std::string& group);

// Applies effect `params` (with "fx") to an RGBA8 frame at `seconds` into a clip lasting `length`.
void applyVideoFx(VideoFrame& frame, const Json& params, double seconds, double length);

// A thumbnail: effect `id` at `seconds` (in a 4 s clip) over a small built-in landscape, filling `out`
// (opaque RGBA8). Or over `picture` (e.g. the selected clip's frame) when given.
void drawVideoFxIcon(const std::string& id, double seconds, VideoFrame& out, const VideoFrame* picture = nullptr);

}  // namespace vstudio::videofx

namespace vstudio {
class ModuleRegistry;
// Registers the three effect modules (called by registerBuiltinModules).
void registerVideoFxModules(ModuleRegistry& r);
}  // namespace vstudio
