// OpenGL ES 3 compositor (Android): an ICompositor module that draws the video
// layers on the GPU through an offscreen EGL context. Same picture as the CPU
// reference (core/tests/compositor_conformance.hpp); chosen over it by priority.
#pragma once

#include <memory>

#include "vstudio/compositor.hpp"
#include "vstudio/modules.hpp"

namespace vstudio::android {

// nullptr when EGL / OpenGL ES 3 is not available.
std::shared_ptr<ICompositor> createGlesCompositor();
// Registers module id "android.gles_compositor" (capabilities: composite, gpu)
// for platform "android". Lazy: no EGL call until first use.
void registerGlesCompositor(ModuleRegistry& r, int priority = 50);

}  // namespace vstudio::android
