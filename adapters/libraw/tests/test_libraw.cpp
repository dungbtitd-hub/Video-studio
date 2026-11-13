// LibRaw module: lazy registration plus the shared camera RAW suite.
#include "still_conformance.hpp"
#include "vstudio/libraw/libraw_module.hpp"

using namespace vstudio;

TEST("libraw: module registers lazily and loads on the first RAW file") {
    ModuleRegistry r;
    libraw::registerModule(r);
    CHECK(!r.loaded("libraw"));
    struct Video final : IMediaBackend {
        std::string name() const override { return "v"; }
        std::optional<ProbeResult> probe(const std::string&, std::string*) override { return std::nullopt; }
        std::unique_ptr<IMediaDecoder> createDecoder() override { return nullptr; }
        std::unique_ptr<IMediaEncoder> createEncoder() override { return nullptr; }
    };
    auto b = withStillDecoders(std::make_shared<Video>(), r);
    std::string err;
    b->probe("clip.mp4", &err);
    CHECK(!r.loaded("libraw"));  // nothing loaded for video
    CHECK(b->probe(vtest::stills::sample("ramp.dng", 1024, 64), &err).has_value());
    CHECK(r.loaded("libraw"));
}

static vtest::StillSuite suite("libraw", [] { return libraw::createDecoder(); });
