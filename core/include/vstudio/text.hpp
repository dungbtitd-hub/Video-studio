// Text clips ("builtin.text"): styled text drawn from glyph outlines, with ~30 in/out
// animations (whole or letter by letter) and loops (motion.hpp).
//
// Content: {"kind": "text", "text": "Xin chào", "font": "<font id>", "size": <canvas px>,
//   "color": "#ffffff", "color2": "#ffcc00" (optional: top-to-bottom gradient),
//   "align": "center" | "left" | "right", "italic": false, "letter_spacing": <em>,
//   "line_height": 1.25, "rotation": <degrees>,
//   "stroke": {"color", "width": <em>}, "shadow": {"color", "opacity", "blur": <em>, "x": <em>, "y": <em>},
//   "glow": {"color", "opacity", "radius": <em>}, "box": {"color", "opacity", "padding": <em>, "radius": <em>},
//   "in": {"type", "duration": <s>}, "out": {"type", "duration": <s>}, "loop": {"type", "speed"}}
// The text block is centred on the content origin; the clip's transform moves and scales it.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/vector.hpp"

namespace vstudio::text {

// One-tap looks: a style patch merged over the content (text, font size and animations kept).
struct Preset {
    std::string id;
    std::string name;
    Json style;
};
const std::vector<Preset>& textPresets();

Json defaultTextContent(const std::string& text = "Nhập chữ");
void drawText(const Json& content, double local, double length, const vec::Affine& place, VideoFrame& out);
std::array<double, 4> textBounds(const Json& content);
bool textStill(const Json& content, double local, double length);

}  // namespace vstudio::text
