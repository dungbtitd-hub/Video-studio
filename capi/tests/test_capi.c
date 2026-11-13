/* Plain C consumer of the API: proves the header is C-clean and the ABI works
 * the way dart:ffi will use it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
static long atomic_inc(volatile long* v) { return InterlockedIncrement(v); }
static long atomic_get(volatile long* v) { return InterlockedCompareExchange(v, 0, 0); }
#else
#include <time.h>
static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
static long atomic_inc(volatile long* v) { return __atomic_add_fetch(v, 1, __ATOMIC_SEQ_CST); }
static long atomic_get(volatile long* v) { return __atomic_load_n(v, __ATOMIC_SEQ_CST); }
#endif

#include "vstudio/vstudio_c.h"

typedef struct {
    volatile long calls;
    int w, h, stride;
    unsigned char first;
} SinkState;

static void sink(void* user, const uint8_t* rgba, int w, int h, int stride, int64_t pts) {
    SinkState* st = (SinkState*)user;
    (void)pts;
    st->w = w;
    st->h = h;
    st->stride = stride;
    st->first = rgba[3];
    atomic_inc(&st->calls);
}

#define EXPECT(c)                                                       \
    do {                                                                \
        if (!(c)) {                                                     \
            fprintf(stderr, "%s:%d: EXPECT(%s) failed\n", __FILE__, __LINE__, #c); \
            return 1;                                                   \
        }                                                               \
    } while (0)

int main(void) {
    EXPECT(vs_ticks_per_second() == 705600000LL);
    EXPECT(vs_session_create("bad", 0, 0, 30, 1) == NULL);
    EXPECT(strlen(vs_last_error(NULL)) > 0);

    vs_session* s = vs_session_create("C test", 1280, 720, 30, 1);
    EXPECT(s != NULL);
    char* v = vs_add_track(s, VS_TRACK_VIDEO, "V1");
    EXPECT(v != NULL);
    EXPECT(vs_undo(s) == 1);
    EXPECT(vs_redo(s) == 1);
    EXPECT(vs_is_dirty(s) == 1);

    /* Importing a missing file fails cleanly with a message. */
    char* bad = vs_import_media(s, "/nonexistent/file.mp4");
    EXPECT(bad == NULL);
    EXPECT(strlen(vs_last_error(s)) > 0);
    EXPECT(vs_remove_clip(s, "nope") != 0);
    EXPECT(vs_ripple_delete(s, "nope") != 0);

    /* Export presets: JSON for the UI; unknown ids are refused. */
    char* presets = vs_export_presets_json(s);
    EXPECT(presets != NULL && strstr(presets, "\"fhd1080\"") && strstr(presets, "\"width\":1920"));
    vs_free(presets);
    EXPECT(vs_export_start_preset(s, "/tmp/x.mp4", "nope") != 0);

    /* Tracks: flags, rename, reorder, remove, all undoable. */
    char* v2 = vs_add_track(s, VS_TRACK_VIDEO, "V2");
    EXPECT(v2 != NULL);
    EXPECT(vs_set_track_flags(s, v2, 1, 1, 0) == 0);
    EXPECT(vs_remove_track(s, v2) != 0); /* locked */
    EXPECT(vs_undo(s) == 1);
    EXPECT(vs_rename_track(s, v2, "Titles") == 0);
    EXPECT(vs_move_track(s, v2, 0) == 0);
    EXPECT(vs_set_track_volume(s, v2, 9.0) != 0);
    EXPECT(vs_remove_track(s, v2) == 0);
    EXPECT(vs_undo(s) == 1);
    int snapped = -1;
    EXPECT(vs_snap_time(s, vs_ticks_per_second() / 20, vs_ticks_per_second() / 10, NULL, -1, &snapped) == 0);
    EXPECT(snapped == 1); /* 0.05 s snaps to 0 */
    vs_free(v2);

    char* json = vs_project_json(s);
    EXPECT(json && strstr(json, "\"V1\""));
    vs_free(json);

    char* mods = vs_modules_json(s);
    EXPECT(mods && strstr(mods, "builtin.color_adjust"));
    vs_free(mods);

    /* Native preview sink: frames arrive on the core's thread without polling. */
    SinkState st = {0, 0, 0, 0, 0};
    vs_preview_set_size(s, 64, 36);
    vs_preview_set_sink(s, sink, &st);
    vs_preview_seek(s, 0);
    for (int i = 0; i < 200 && atomic_get(&st.calls) == 0; ++i) sleep_ms(10);
    EXPECT(st.calls > 0);
    EXPECT(st.w == 64 && st.h == 36 && st.stride == 64 * 4);
    EXPECT(st.first == 255); /* opaque canvas */
    vs_preview_set_sink(s, NULL, NULL);
    const long after = atomic_get(&st.calls);
    vs_preview_seek(s, vs_ticks_per_second());
    sleep_ms(100);
    EXPECT(atomic_get(&st.calls) == after);

    /* Media library: unknown assets fail cleanly; nothing is missing in an empty project. */
    EXPECT(vs_request_thumbnails(s, "nope", 5, 90) != 0);
    EXPECT(vs_thumbnails(s, "nope", NULL, 0, NULL, NULL, NULL) == 0);
    EXPECT(vs_waveform(s, "nope", NULL, 0, NULL) == 0);
    char* missing = vs_missing_assets_json(s);
    EXPECT(missing && strcmp(missing, "[]") == 0);
    vs_free(missing);
    EXPECT(vs_relink_asset(s, "nope", "/tmp/x.mp4") != 0);

    vs_free(v);
    vs_session_destroy(s);
    puts("capi: all checks passed");
    return 0;
}
