// Metal compositor: lazy registration plus the compositor conformance suite
// (same picture as the CPU reference). Skips itself on a machine without Metal.
#include "compositor_conformance.hpp"
#include "vstudio/apple/metal_compositor.hpp"

using namespace vstudio;

TEST("metal: registers lazily and wins over the CPU compositor when available") {
    ModuleRegistry r;
    registerBuiltinModules(r);
    apple::registerMetalCompositor(r);
    CHECK(!r.loaded("apple.metal_compositor"));
    auto c = r.best<ICompositor>("composite");
    if (!apple::createMetalCompositor()) {
        CHECK(c == nullptr);  // registry reports the module as unavailable; Renderer falls back to CPU
        return;
    }
    CHECK(c != nullptr);
    CHECK_EQ(c->name(), std::string("apple.metal_compositor"));
}

static vtest::CompositorSuite metalSuite("metal", [] { return apple::createMetalCompositor(); });
