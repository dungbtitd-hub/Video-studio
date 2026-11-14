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

static int cancel_at_once(double progress, void* user) {
    (void)progress;
    ++*(int*)user;
    return 1;
}

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

    /* Looks: catalog, preview on a buffer, refusal for unknown clips. */
    char* looks = vs_looks_json();
    EXPECT(looks != NULL && strstr(looks, "\"teal_orange\""));
    vs_free(looks);
    uint8_t px[4 * 4] = {200, 100, 50, 255, 10, 20, 30, 255, 128, 128, 128, 255, 0, 255, 0, 255};
    EXPECT(vs_look_preview("bw_contrast", 1.0, px, 2, 2) == 0);
    EXPECT(px[0] == px[1] && px[1] == px[2] && px[3] == 255);
    EXPECT(vs_look_preview("nope", 1.0, px, 2, 2) != 0);
    EXPECT(vs_set_clip_look(s, "[\"nope\"]", "vivid", 1.0) != 0);
    EXPECT(vs_set_clip_look(s, "not json", "vivid", 1.0) != 0);
    char* logs = vs_log_formats_json();
    EXPECT(logs && strstr(logs, "\"slog3\"") && strstr(logs, "\"applelog\"") && strstr(logs, "\"hlg\""));
    vs_free(logs);
    EXPECT(vs_set_clip_color_space(s, "[\"nope\"]", "slog3", 0) != 0);
    EXPECT(vs_set_clip_lut(s, "[]", "/nonexistent.cube", 1.0) != 0);
    EXPECT(strstr(vs_last_error(s), "cannot open") != NULL);
    /* Keyframes: unknown clips and bad input are refused with a message. */
    EXPECT(vs_add_keyframe(s, "nope", 0, 0) != 0);
    EXPECT(strstr(vs_last_error(s), "unknown clip") != NULL);
    EXPECT(vs_remove_keyframe(s, "nope", 1, 0) < 0);
    EXPECT(vs_clear_keyframes(s, "nope", 0) != 0);
    EXPECT(vs_set_property_at(s, "nope", 0, "{\"scale\":2}") != 0);
    EXPECT(vs_zoom_to_region(s, "nope", 0, 0.25, 0.25, 0.5, 0.5) != 0);
    EXPECT(vs_clip_state_at(s, "nope", 0) == NULL);
    /* Covers: catalogs, thumbnails, refusals. */
    char* styles = vs_cover_styles_json();
    EXPECT(styles && strstr(styles, "\"mosaic\"") && strstr(styles, "\"frosted\""));
    vs_free(styles);
    char* shapes = vs_cover_shapes_json();
    EXPECT(shapes && strstr(shapes, "\"heart\""));
    vs_free(shapes);
    {
        uint8_t buf[16 * 16 * 4];
        for (int i = 0; i < 16 * 16 * 4; ++i) buf[i] = (uint8_t)((i % 4 == 3) ? 255 : ((i / 4) % 2 ? 230 : 20));
        EXPECT(vs_cover_preview("black", "rect", buf, 16, 16) == 0);
        EXPECT(buf[(8 * 16 + 8) * 4] < 10); /* centre blacked out */
        EXPECT(vs_cover_preview("nope", "rect", buf, 16, 16) != 0);
    }
    EXPECT(vs_add_cover(s, "nope", "{}") == NULL);
    EXPECT(vs_track_cover_start(s, "nope", "x", 0, 0) != 0);
    EXPECT(vs_track_status(s, NULL) == 0);
    /* Transitions: catalog, thumbnails, refusals, cut points of an empty track. */
    char* trs = vs_transitions_json();
    EXPECT(trs && strstr(trs, "\"dip_black\"") && strstr(trs, "\"push_left\""));
    vs_free(trs);
    {
        uint8_t a[8 * 8 * 4], b[8 * 8 * 4], o[8 * 8 * 4];
        for (int i = 0; i < 8 * 8 * 4; ++i) {
            a[i] = (uint8_t)(i % 4 == 3 ? 255 : 0);
            b[i] = 255;
        }
        EXPECT(vs_transition_preview("fade", a, b, o, 8, 8, 1.0) == 0 && o[0] == 255);
        EXPECT(vs_transition_preview("fade", a, b, o, 8, 8, 0.0) == 0 && o[0] == 0);
        EXPECT(vs_transition_preview("nope", a, b, o, 8, 8, 0.5) != 0);
    }
    EXPECT(vs_set_transition(s, "nope", 0, "fade", 0) != 0);
    EXPECT(vs_set_transition_everywhere(s, "nope", 0) == -1);
    EXPECT(vs_set_transition_everywhere(s, "fade", 0) == 0);
    char* cuts = vs_cut_points_json(s);
    EXPECT(cuts && cuts[0] == '{');
    vs_free(cuts);

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

    /* Transfer package: pack, unpack elsewhere, open; garbage is refused. */
    char* packed = vs_transfer_pack(s, "capi_test.vstudiopkg", NULL, NULL);
    EXPECT(packed && strstr(packed, "\"ok\":true") && strstr(packed, "\"missing\":[]"));
    vs_free(packed);
    int calls = 0;
    char* unpacked = vs_transfer_unpack("capi_test.vstudiopkg", "capi_test_unpack", cancel_at_once, &calls);
    EXPECT(unpacked && strstr(unpacked, "\"ok\":true") && strstr(unpacked, "C test"));
    EXPECT(calls == 1); /* final 1.0 report only: no media; cancel after success is ignored */
    const char* dir = strstr(unpacked, "\"package_dir\":\"") + strlen("\"package_dir\":\"");
    char path[1024] = {0};
    for (size_t i = 0; *dir && *dir != '"' && i < sizeof path - 1; ++dir, ++i) {
        if (*dir == '\\') ++dir; /* JSON escape (Windows separators) */
        path[i] = *dir;
    }
    vs_free(unpacked);
    vs_session* opened = vs_session_open(path);
    EXPECT(opened != NULL);
    char* reopened = vs_project_json(opened);
    EXPECT(reopened && strstr(reopened, "\"V1\""));
    vs_free(reopened);
    vs_session_destroy(opened);
    char* refused = vs_transfer_unpack("/nonexistent.vstudiopkg", "capi_test_unpack", NULL, NULL);
    EXPECT(refused && strstr(refused, "\"ok\":false"));
    vs_free(refused);
    remove("capi_test.vstudiopkg");

    vs_free(v);
    vs_session_destroy(s);
    puts("capi: all checks passed");
    return 0;
}
