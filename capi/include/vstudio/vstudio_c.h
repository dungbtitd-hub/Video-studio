/* Video Studio Core - stable C API.
 *
 * Thin, ABI-stable layer used by Flutter (dart:ffi) and, later, by plugins.
 * Rules:
 *   - All functions are called from one thread (the UI isolate) except
 *     vs_pull_audio, which the audio device calls from its own thread.
 *   - Strings returned as `char*` are heap-allocated UTF-8: free them with vs_free.
 *   - Times are int64 ticks; vs_ticks_per_second() gives the tick rate.
 *   - Functions returning int use 0 = success, non-zero = error (see vs_last_error).
 *   - Edits only change the timeline model: they return in microseconds and
 *     never touch media files. Decoding happens on background threads.
 */
#ifndef VSTUDIO_C_H
#define VSTUDIO_C_H

#include <stdint.h>

#if defined(_WIN32)
#if defined(VSTUDIO_CAPI_BUILD)
#define VS_API __declspec(dllexport)
#else
#define VS_API __declspec(dllimport)
#endif
#else
#define VS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vs_session vs_session;

enum { VS_TRACK_VIDEO = 0, VS_TRACK_AUDIO = 1 };
enum { VS_TRIM_START = 0, VS_TRIM_END = 1 };
enum { VS_EXPORT_IDLE = 0, VS_EXPORT_RUNNING = 1, VS_EXPORT_DONE = 2, VS_EXPORT_FAILED = -1, VS_EXPORT_CANCELLED = -2 };

VS_API const char* vs_version(void);
VS_API int64_t vs_ticks_per_second(void);
VS_API void vs_free(void* p);
/* JSON array of available modules: [{"id","kind","version","capabilities",...}] */
VS_API char* vs_modules_json(vs_session* s);

/* ---- session / project ---- */
VS_API vs_session* vs_session_create(const char* name, int width, int height, int fps_num, int fps_den);
/* Opens a .vstudio package folder. Returns NULL on error (message via vs_last_error(NULL)). */
VS_API vs_session* vs_session_open(const char* package_dir);
VS_API void vs_session_destroy(vs_session* s);
/* Last error of this session, or of the last failed create/open when s is NULL. Valid until the next call. */
VS_API const char* vs_last_error(vs_session* s);
VS_API int vs_save(vs_session* s, const char* package_dir);
VS_API int vs_autosave(vs_session* s); /* writes recovery/autosave.json if there are unsaved edits */
VS_API int vs_is_dirty(vs_session* s);
/* Full project as schema v0 JSON (for the UI to draw the timeline). */
VS_API char* vs_project_json(vs_session* s);
VS_API int64_t vs_project_revision(vs_session* s);
VS_API int64_t vs_duration(vs_session* s);

/* ---- edits ---- */
VS_API char* vs_import_media(vs_session* s, const char* path); /* probes the file; returns asset id */
VS_API char* vs_add_track(vs_session* s, int type, const char* name);
VS_API char* vs_append_clip(vs_session* s, const char* track_id, const char* asset_id);
VS_API char* vs_insert_clip(vs_session* s, const char* track_id, const char* asset_id, int64_t timeline_in,
                            int64_t source_in, int64_t source_out);
VS_API int vs_move_clip(vs_session* s, const char* clip_id, const char* to_track_id, int64_t timeline_in);
VS_API int vs_trim_clip(vs_session* s, const char* clip_id, int edge, int64_t timeline_time);
VS_API char* vs_split_clip(vs_session* s, const char* clip_id, int64_t timeline_time);
VS_API int vs_split_at(vs_session* s, int64_t timeline_time); /* returns number of clips split, <0 on error */
VS_API int vs_remove_clip(vs_session* s, const char* clip_id);
VS_API int vs_set_clip_volume(vs_session* s, const char* clip_id, double volume);
VS_API int vs_add_clip_effect(vs_session* s, const char* clip_id, const char* effect_module_id, const char* params_json);
VS_API int vs_undo(vs_session* s); /* 1 = undone, 0 = nothing to undo */
VS_API int vs_redo(vs_session* s);

/* ---- preview (background threads) ---- */
VS_API void vs_preview_set_size(vs_session* s, int width, int height);
VS_API void vs_preview_seek(vs_session* s, int64_t t); /* latest request wins */
VS_API void vs_play(vs_session* s, int64_t from);
VS_API void vs_pause(vs_session* s);
VS_API int vs_is_playing(vs_session* s);
VS_API int64_t vs_playhead(vs_session* s);
/* Copies the newest rendered frame (RGBA8) if it is newer than *serial.
 * Returns 1 and updates serial, w, h and pts when copied, 0 if nothing new,
 * -1 if dst_capacity is too small (w and h are set to the needed size). */
VS_API int vs_preview_take_frame(vs_session* s, uint8_t* dst, int64_t dst_capacity, int64_t* serial, int* w, int* h,
                                 int64_t* pts);
/* Called from the audio device thread. Interleaved float, project rate/channels. */
VS_API void vs_pull_audio(vs_session* s, float* out, int64_t frames);
VS_API int vs_audio_format(vs_session* s, int* sample_rate, int* channels);

/* ---- export (background job) ---- */
VS_API int vs_export_start(vs_session* s, const char* out_path, int width, int height, int fps_num, int fps_den,
                           int64_t video_bitrate);
VS_API int vs_export_status(vs_session* s, double* progress); /* VS_EXPORT_* */
VS_API void vs_export_cancel(vs_session* s);

#ifdef __cplusplus
}
#endif

#endif /* VSTUDIO_C_H */
