// Direct3D 11 compositor (Windows): an ICompositor module that draws the video
// layers on the GPU. Same picture as the CPU reference (checked by
// core/tests/compositor_conformance.hpp); chosen over it by priority.
#pragma once

#include <memory>

#include "vstudio/compositor.hpp"
#include "vstudio/modules.hpp"

namespace vstudio::windows {

// nullptr when there is no usable hardware D3D11 device. `allowWarp` accepts
// the WARP software rasteriser instead (tests on GPU-less machines only: it is
// not faster than the CPU compositor).
std::shared_ptr<ICompositor> createD3D11Compositor(bool allowWarp = false);
// Registers module id "windows.d3d11_compositor" (capabilities: composite, gpu)
// for platform "windows". Lazy: no D3D call until first use.
void registerD3D11Compositor(ModuleRegistry& r, int priority = 50);

}  // namespace vstudio::windows
