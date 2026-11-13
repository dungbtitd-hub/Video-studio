// OpenGL ES compositor: lazy registration plus the compositor conformance suite
// (runs on the emulator in CI; SwiftShader provides GLES 3 there).
#include "compositor_conformance.hpp"
#include "vstudio/android/gles_compositor.hpp"

using namespace vstudio;

TEST("gles: registers lazily and wins over the CPU compositor when available") {
    ModuleRegistry r;
    registerBuiltinModules(r);
    android::registerGlesCompositor(r);
    CHECK(!r.loaded("android.gles_compositor"));
    auto c = r.best<ICompositor>("composite");
    if (!c) return;  // no EGL here: the Renderer falls back to the CPU compositor
    CHECK_EQ(c->name(), std::string("android.gles_compositor"));
}

static vtest::CompositorSuite glesSuite("gles", [] { return android::createGlesCompositor(); });
