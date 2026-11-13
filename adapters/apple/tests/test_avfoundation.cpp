// Apple AVFoundation module: lazy registration plus the shared backend
// conformance suite (runs on the macOS CI job).
#include "backend_conformance.hpp"
#include "vstudio/apple/avfoundation_module.hpp"

using namespace vstudio;

TEST("apple: module registers lazily") {
    ModuleRegistry r;
    apple::registerModule(r);
    CHECK(!r.loaded("apple.avfoundation"));
    auto b = r.best<IMediaBackend>("decode");
    CHECK(b != nullptr);
    CHECK(r.loaded("apple.avfoundation"));
    CHECK_EQ(b->name(), std::string("apple.avfoundation"));
}

static vtest::BackendSuite suite("apple", [] { return apple::createBackend(); });
