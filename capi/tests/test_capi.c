/* Plain C consumer of the API: proves the header is C-clean and the ABI works
 * the way dart:ffi will use it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vstudio/vstudio_c.h"

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

    char* json = vs_project_json(s);
    EXPECT(json && strstr(json, "\"V1\""));
    vs_free(json);

    char* mods = vs_modules_json(s);
    EXPECT(mods && strstr(mods, "builtin.color_adjust"));
    vs_free(mods);

    vs_free(v);
    vs_session_destroy(s);
    puts("capi: all checks passed");
    return 0;
}
