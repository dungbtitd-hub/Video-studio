// Covering part of the picture: blur a logo, pixelate a face, black out a
// number plate... One effect ("builtin.cover") per covered region, so a clip
// can carry several, each with its own shape and style.
//
// params (all optional except the region):
//   x, y, w, h   region in the SOURCE frame, 0..1 (left, top, width, height),
//                so the cover stays on the subject when the clip is zoomed or moved
//   rotation     degrees, about the region centre
//   shape        "rect" | "rounded" | "ellipse" | "circle" | "diamond" | "triangle" |
//                "hexagon" | "star" | "heart"
//   style        see coverStyles(): blur, mosaic, solid bars, patterns, artistic
//   amount       0..1 strength (blur radius, cell size...), resolution independent
//   feather      0..1 soft edge, as a fraction of the region's smaller half size
//   invert       true: cover everything EXCEPT the region
//   color        "#rrggbb" for the solid/colour styles
//   anim         {"x": [keys], "y": ..., "w", "h", "rotation", "amount"} keyframes in
//                source time (same format as Clip::keyframes) so a cover can follow a
//                moving subject; values not animated use the static value above
//
// Everything is computed only over the region's bounding box (plus the blur
// margin), so a small cover on a 4K frame stays cheap.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/time.hpp"

namespace vstudio {

inline constexpr const char* kCoverEffect = "builtin.cover";

struct CoverStyle {
    std::string id;
    std::string name;      // Vietnamese UI name
    std::string category;  // "Làm mờ", "Khảm", "Che kín", "Họa tiết", "Nghệ thuật"
    bool animated = false;  // changes over time (noise, sparkles...)
};
const std::vector<CoverStyle>& coverStyles();
const CoverStyle* findCoverStyle(const std::string& id);

struct CoverShape {
    std::string id;
    std::string name;
};
const std::vector<CoverShape>& coverShapes();
bool isCoverShape(const std::string& id);

// Properties of a cover that can be keyframed.
const std::vector<std::string>& coverAnimatable();

// The params with keyframed values evaluated at `sourceTime` ("anim" removed).
Json coverParamsAt(const Json& params, Time sourceTime);

// Default params for a new cover: centred box, the given style and shape.
Json defaultCover(const std::string& style, const std::string& shape = "rect");

// Where the clip's source frame lands on the canvas at `sourceTime` (left, top, width,
// height in canvas pixels): converts cover regions to and from what the preview shows.
std::optional<std::array<double, 4>> sourceRectOnCanvas(const Project& p, const Clip& c, Time sourceTime);

// Applies one cover to the frame (RGBA8 or RGBA16F). `t` drives animated styles.
void applyCover(VideoFrame& frame, const Json& params, Time t);

}  // namespace vstudio
