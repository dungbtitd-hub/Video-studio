// Overlay effects ("builtin.overlay"): animated layers laid over the video - falling snow,
// snowflakes, rain, petals, confetti, balloons, fireworks, sparkles, light leaks, an old-film
// look... The core draws them itself as vectors at the output size. Every one is a pure
// function of time (no state), so any frame can be drawn on its own: seeking, preview and
// export all show the same picture.
//
// An overlay covers a region: a box `w` x `h` canvas pixels centred on the clip's position
// (its transform x, y, scale), full frame by default. Its inner edges are feathered so
// particles never stop at a hard line; edges that reach the frame are not.
//
// Content: {"kind": "overlay", "overlay": "<id>", "w": <canvas px>, "h": <canvas px>,
//   "ref": <canvas height, sizes are relative to it>, "density": 1, "speed": 1, "size": 1,
//   "opacity": 1, "feather": 0.12 (share of the smaller side), "color": "#rrggbb" (optional tint),
//   "seed": 1, "in": {"type": "fade", "duration": 0.8}, "out": {"type": "fade", "duration": 0.8}}
// The in/out always fade (the effect fades in and out as a whole); "" means no fade.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/vector.hpp"

namespace vstudio::overlays {

struct OverlayInfo {
    std::string id;
    std::string name;      // Vietnamese
    std::string category;  // Vietnamese, in display order (see overlayCategories)
    bool tintable = false; // takes "color"
};

const std::vector<OverlayInfo>& overlayCatalog();
std::vector<std::string> overlayCategories();
[[nodiscard]] const OverlayInfo* findOverlay(const std::string& id);

// Content for a new overlay covering a `canvasW` x `canvasH` frame.
Json defaultOverlayContent(const std::string& id, int canvasW = 1920, int canvasH = 1080);
void drawOverlay(const Json& content, double local, double length, const vec::Affine& place, VideoFrame& out);
std::array<double, 4> overlayBounds(const Json& content);
bool overlayStill(const Json& content, double local, double length);
// Draws overlay `id` at `seconds` filling `out` (a thumbnail) over a dark backdrop, so light
// effects show.
void drawOverlayIcon(const std::string& id, double seconds, VideoFrame& out);

}  // namespace vstudio::overlays
