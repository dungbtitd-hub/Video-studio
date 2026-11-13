// Android MediaCodec module: lazy registration plus the shared backend
// conformance suite. Runs on a device or emulator (CI pushes it with adb).
#include <dlfcn.h>

#include "backend_conformance.hpp"
#include "vstudio/android/mediacodec_module.hpp"

using namespace vstudio;

// MediaExtractor runs in the media.extractor service, which reads our file back
// through binder calls into this process. An app process always has binder
// threads to answer them; a bare executable has none until it starts them. Without
// them those calls pile up and, after enough extractors, makeExtractor() blocks for
// good (seen on the emulator when all tests ran in one process). API 29+ has
// ABinderProcess_startThreadPool; looked up at run time because minSdk is 26.
static const bool binderThreads = [] {
    if (void* lib = dlopen("libbinder_ndk.so", RTLD_NOW)) {
        if (auto start = reinterpret_cast<void (*)()>(dlsym(lib, "ABinderProcess_startThreadPool"))) {
            start();
            return true;
        }
    }
    return false;
}();

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
