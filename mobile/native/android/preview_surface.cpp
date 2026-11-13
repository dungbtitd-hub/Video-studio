// Native preview on Android: the core's video thread writes each preview frame
// straight into the Surface of a Flutter texture (SurfaceProducer). No frame
// crosses into Dart and the UI thread does no per-frame work.
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdint>
#include <cstring>

#include "vstudio/vstudio_c.h"

namespace {

struct Target {
    vs_session* session = nullptr;
    ANativeWindow* window = nullptr;
    int width = 0, height = 0;
    bool loggedFirst = false;
};

void sink(void* user, const uint8_t* rgba, int w, int h, int stride, int64_t /*pts*/) {
    auto* t = static_cast<Target*>(user);
    if (w != t->width || h != t->height) {
        ANativeWindow_setBuffersGeometry(t->window, w, h, WINDOW_FORMAT_RGBA_8888);
        t->width = w;
        t->height = h;
    }
    ANativeWindow_Buffer buf{};
    if (ANativeWindow_lock(t->window, &buf, nullptr) != 0) return;
    const int rows = buf.height < h ? buf.height : h;
    const int cols = buf.width < w ? buf.width : w;
    auto* dst = static_cast<uint8_t*>(buf.bits);
    for (int y = 0; y < rows; ++y)
        std::memcpy(dst + static_cast<size_t>(y) * buf.stride * 4, rgba + static_cast<size_t>(y) * stride,
                    static_cast<size_t>(cols) * 4);
    ANativeWindow_unlockAndPost(t->window);
    if (!t->loggedFirst) {
        t->loggedFirst = true;
        __android_log_print(ANDROID_LOG_INFO, "vstudio", "native preview: first frame %dx%d", w, h);
    }
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL Java_com_videostudio_video_1studio_NativePreview_attach(JNIEnv* env, jclass,
                                                                                          jlong session,
                                                                                          jobject surface) {
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win || !session) return 0;
    auto* t = new Target;
    t->session = reinterpret_cast<vs_session*>(session);
    t->window = win;
    vs_preview_set_sink(t->session, sink, t);
    return reinterpret_cast<jlong>(t);
}

extern "C" JNIEXPORT void JNICALL Java_com_videostudio_video_1studio_NativePreview_detach(JNIEnv*, jclass,
                                                                                        jlong handle) {
    auto* t = reinterpret_cast<Target*>(handle);
    if (!t) return;
    vs_preview_set_sink(t->session, nullptr, nullptr);  // after this the sink is never called again
    ANativeWindow_release(t->window);
    delete t;
}
