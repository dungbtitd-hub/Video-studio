// Metal compositor (macOS + iOS): an ICompositor module that draws the video
// layers on the GPU. Same picture as the CPU reference (checked by
// core/tests/compositor_conformance.hpp); chosen over it by priority.
#pragma once

#include <memory>

#include "vstudio/compositor.hpp"
#include "vstudio/modules.hpp"

namespace vstudio::apple {

// nullptr when this machine has no usable Metal device.
std::shared_ptr<ICompositor> createMetalCompositor();
// Registers module id "apple.metal_compositor" (capabilities: composite, gpu)
// for platforms "macos" and "ios". Lazy: no Metal call until first use.
void registerMetalCompositor(ModuleRegistry& r, int priority = 50);

}  // namespace vstudio::apple
