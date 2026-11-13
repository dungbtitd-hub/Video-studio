// Video Studio Core - compositor contract.
//
// The compositor draws the visible video layers of one timeline instant into
// an output frame. It is a module (kind "compositor"): the core ships a CPU
// reference implementation ("builtin.cpu_compositor"); GPU modules (Metal
// first, then D3D11/Vulkan/GLES) register with a higher priority and the
// Renderer picks the best one that is available on this machine. A GPU module
// that fails at runtime falls back to the CPU one for that frame, so a missing
// or lost GPU never breaks preview or export.
//
// Every implementation must produce the same picture as the CPU reference
// (within rounding; see core/tests/compositor_conformance.hpp).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio {

// Clip transform as stored in Clip::transform (all optional):
// { "scale": 1.0, "x": 0.0, "y": 0.0, "opacity": 1.0 }
// x/y are offsets from centred, in canvas pixels in the project (the Renderer
// converts them to output pixels before compositing); scale multiplies "fit".
struct Transform {
    double scale = 1.0;
    double x = 0.0;
    double y = 0.0;
    double opacity = 1.0;
    static Transform fromJson(const Json& j);
};

// Where a layer lands in the output, in output pixels. Every compositor uses
// this: positions are snapped to 1/256 pixel so that CPU (double) and GPU
// (float) agree exactly on which pixel centres a layer covers: [left, left+width).
struct LayerRect {
    double left = 0, top = 0, width = 0, height = 0;
    bool unscaled = false;  // 1:1 at an integer offset: a plain copy, no filtering
};
LayerRect layerRect(int srcW, int srcH, int dstW, int dstH, const Transform& tr);

// Draws `src` onto `dst` (straight-alpha RGBA) fitted to dst, then transformed.
// This is the reference the GPU compositors are checked against.
void compositeOver(VideoFrame& dst, const VideoFrame& src, const Transform& tr);
// Same for an RGBA16F `dst` (src in either format), in float: the deep reference.
void compositeOverDeep(VideoFrame& dst, const VideoFrame& src, const Transform& tr);

struct CompositeLayer {
    std::shared_ptr<const VideoFrame> frame;  // straight-alpha RGBA
    Transform transform;
};

class ICompositor {
public:
    static constexpr const char* kModuleKind = "compositor";
    virtual ~ICompositor() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    // RGBA16F output and layers. A compositor without it is skipped for deep
    // renders (the Renderer uses the CPU reference), never handed half floats.
    [[nodiscard]] virtual bool supportsDeep() const { return false; }
    // Fills `out` (already allocated to the output size) with opaque black,
    // then draws `layers` bottom to top. Must be safe to call from several
    // threads (preview and export run at the same time). Returns false (and
    // leaves the caller free to fall back) if the device failed.
    virtual bool composite(VideoFrame& out, const std::vector<CompositeLayer>& layers, std::string* error) = 0;
};

std::shared_ptr<ICompositor> createCpuCompositor();

}  // namespace vstudio
