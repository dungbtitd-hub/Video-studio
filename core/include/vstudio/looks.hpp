// One-tap colour looks for video and photo clips.
//
// A look is a named set of parameters for the "builtin.color_look" effect
// (tone curve, white balance, lift/gamma/gain, saturation/vibrance, split
// toning, fade, vignette) plus a strength. Applying a look adds or replaces
// that effect on the clip, as one undoable edit, so it renders the same in
// preview, export and on every platform. The effect works on any frame, so
// still images get the same looks as video.
#pragma once

#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio {

inline constexpr const char* kColorLookEffect = "builtin.color_look";

struct ColorLook {
    std::string id;        // stable; stored in the project with the effect
    std::string name;      // Vietnamese UI name
    std::string category;  // "Điện ảnh", "Cổ điển", "Đen trắng", ...
    Json params;           // builtin.color_look parameters (without strength)
};

const std::vector<ColorLook>& colorLooks();
const ColorLook* findColorLook(const std::string& id);

// The effect itself (also registered as a module). Exposed so UIs can render
// look thumbnails from a small frame without touching the project.
void applyColorLook(VideoFrame& frame, const Json& params);

// The look in the form GPU compositors run it, right after the grade (grade.hpp GpuGrade):
// the tone curve per channel at steps + 1 inputs, and 4 vec4 uniforms:
//   [on, strength, saturation (0 for mono), vibrance (0 for mono)], [colourOn, splitOn, vignette, 0],
//   [shadows r, g, b, 0], [highlights r, g, b, 0].
struct LookGpu {
    bool active = false;
    std::vector<float> curve[3];
    float u[16] = {};
};
LookGpu lookGpu(const Json& params, int steps);

}  // namespace vstudio
