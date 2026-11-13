// Direct3D 11 compositor: lazy registration plus the compositor conformance
// suite. Hosted CI runners have no GPU, so the suite falls back to WARP (the
// D3D software rasteriser), which still runs the same shaders and pipeline.
#include "compositor_conformance.hpp"
#include "vstudio/windows/d3d11_compositor.hpp"

using namespace vstudio;

TEST("d3d11: registers lazily; the registry uses hardware only") {
    ModuleRegistry r;
    registerBuiltinModules(r);
    windows::registerD3D11Compositor(r);
    CHECK(!r.loaded("windows.d3d11_compositor"));
    auto c = r.best<ICompositor>("composite");
    if (!windows::createD3D11Compositor()) {
        CHECK(c == nullptr);  // unavailable: the Renderer falls back to the CPU compositor
        return;
    }
    CHECK(c != nullptr);
    CHECK_EQ(c->name(), std::string("windows.d3d11_compositor"));
}

static vtest::CompositorSuite d3d11Suite("d3d11", [] {
    auto c = windows::createD3D11Compositor();
    return c ? c : windows::createD3D11Compositor(/*allowWarp=*/true);
});
