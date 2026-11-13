// FFmpeg module: lazy registration plus the shared backend conformance suite.
#include "backend_conformance.hpp"
#include "vstudio/ffmpeg/ffmpeg_module.hpp"

using namespace vstudio;

TEST("ffmpeg: module registers lazily") {
    ModuleRegistry r;
    ffmpeg::registerModule(r);
    CHECK(!r.loaded("ffmpeg"));
    auto b = r.best<IMediaBackend>("decode");
    CHECK(b != nullptr);
    CHECK(r.loaded("ffmpeg"));
    CHECK(b->name().rfind("ffmpeg", 0) == 0);
}

static vtest::BackendSuite suite("ffmpeg", [] { return ffmpeg::createBackend(); });
