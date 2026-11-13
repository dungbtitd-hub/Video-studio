// Android MediaCodec module: lazy registration plus the shared backend
// conformance suite. Runs on a device or emulator (CI pushes it with adb).
#include "backend_conformance.hpp"
#include "vstudio/android/mediacodec_module.hpp"

using namespace vstudio;

TEST("android: module registers lazily and only on Android") {
    ModuleRegistry r;
    android::registerModule(r);
    CHECK(!r.loaded("android.mediacodec"));
    auto b = r.best<IMediaBackend>("decode");
    CHECK(b != nullptr);
    CHECK(r.loaded("android.mediacodec"));
    CHECK_EQ(b->name(), std::string("android.mediacodec"));
}

static vtest::BackendSuite suite("android", [] { return android::createBackend(); });
