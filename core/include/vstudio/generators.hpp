// Generated clips: pictures the core draws itself instead of decoding media (text,
// stickers). A clip is generated when `Clip::content` holds {"kind": "<kind>", ...}.
// Each kind is drawn by a module of kind "generator" offering capability
// "generate.<kind>"; the built-in ones are "builtin.text" and "builtin.sticker".
#pragma once

#include <array>
#include <memory>
#include <string>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/vector.hpp"

namespace vstudio {

class ModuleRegistry;

class IClipGenerator {
public:
    static constexpr const char* kModuleKind = "generator";
    virtual ~IClipGenerator() = default;
    // Draws `content` at `local` seconds into a clip lasting `length` seconds onto `out`
    // (transparent, any size). `place` maps content units (canvas pixels, origin at the
    // canvas centre) to `out` pixels; it carries the clip's position and scale, so text
    // stays sharp at any size.
    virtual void draw(const Json& content, double local, double length, const vec::Affine& place,
                      VideoFrame& out) const = 0;
    // True when nothing moves at `local` (between the in and out animations, no loop):
    // the picture can be cached.
    [[nodiscard]] virtual bool still(const Json& content, double local, double length) const = 0;
    // Box at rest in content units {x0, y0, x1, y1} (for handles in the UI).
    [[nodiscard]] virtual std::array<double, 4> bounds(const Json& content) const = 0;
};

// The generator for `kind`: the best module offering "generate.<kind>" in `modules`, else the
// built-in one. nullptr for an unknown kind.
std::shared_ptr<IClipGenerator> generatorFor(ModuleRegistry* modules, const std::string& kind);

// Registers "builtin.text" and "builtin.sticker".
void registerGeneratorModules(ModuleRegistry& r);

// Box of a generated clip on the canvas at `sourceTime` {x, y, w, h} in canvas pixels
// (its rest bounds moved and scaled by the clip's transform).
std::array<double, 4> generatedRectOnCanvas(const Project& p, const Clip& c, Time sourceTime);

}  // namespace vstudio
