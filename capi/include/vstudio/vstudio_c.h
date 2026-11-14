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
/* Removes the clip and pulls the later clips of its track left to close the gap (one undo step). */
VS_API int vs_ripple_delete(vs_session* s, const char* clip_id);
/* One-tap colour looks (video and photos). vs_looks_json: [{"id","name","category"}], free with vs_free.
 * vs_set_clip_look: clip_ids_json is a JSON array of clip ids; look_id "" removes the look; one undo step.
 * vs_look_preview: applies a look to an RGBA buffer in place (for look thumbnails; no session needed). */
VS_API char* vs_looks_json(void);
VS_API int vs_set_clip_look(vs_session* s, const char* clip_ids_json, const char* look_id, double strength);
VS_API int vs_look_preview(const char* look_id, double strength, uint8_t* rgba, int width, int height);

/* ---- Log / HDR input and 3D LUTs (color_space.hpp); undoable ---- */
VS_API char* vs_log_formats_json(void); /* [{"id","name","vendor"}] */
/* input "" removes; the conversion always runs before other effects. */
VS_API int vs_set_clip_color_space(vs_session* s, const char* clip_ids_json, const char* input, double exposure);
/* .cube file, absolute or relative to the project's media folder; "" removes. Bad files fail here. */
VS_API int vs_set_clip_lut(vs_session* s, const char* clip_ids_json, const char* path, double strength);
VS_API char* vs_suggested_log_format(vs_session* s, const char* asset_id); /* "hlg" or "" */
/* ---- keyframes inside one clip (keyframes.hpp); all undoable, times are TIMELINE ticks ----
 * group: 0 = picture (scale, x, y, opacity), 1 = audio (volume); the two are independent.
 * vs_add_keyframe captures the clip's current values at t. vs_remove_keyframe: 1 removed, 0 none there.
 * vs_set_property_at: values_json {"scale":2,"x":-120,"y":40,"volume":0.5}; a property that is
 *   already animated gets a key at t, otherwise the clip's static value changes (one undo step).
 *   x/y are canvas pixels from the centre; scale multiplies "fit"; volume is the gain heard.
 * vs_zoom_to_region: the region (0..1 of the frame) fills the frame at t.
 * vs_clip_state_at: {"inside","scale","x","y","opacity","volume","picture_here","audio_here",
 *   "picture_animated","audio_animated"} (free with vs_free). */
VS_API int vs_add_keyframe(vs_session* s, const char* clip_id, int group, int64_t timeline_time);
VS_API int vs_remove_keyframe(vs_session* s, const char* clip_id, int group, int64_t timeline_time);
VS_API int vs_clear_keyframes(vs_session* s, const char* clip_id, int group);
VS_API int vs_set_property_at(vs_session* s, const char* clip_id, int64_t timeline_time, const char* values_json);
VS_API int vs_zoom_to_region(vs_session* s, const char* clip_id, int64_t timeline_time, double rx, double ry, double rw,
                             double rh);
VS_API char* vs_clip_state_at(vs_session* s, const char* clip_id, int64_t timeline_time);
/* ---- covers: blur / mosaic / bars over a region of a clip (cover.hpp); undoable ----
 * Region x, y, w, h is 0..1 of the clip's SOURCE frame, so it stays on the subject when the clip is zoomed.
 * vs_cover_styles_json: [{"id","name","category","animated"}]; vs_cover_shapes_json: [{"id","name"}].
 * vs_add_cover: params_json may be "{}" (centred box, blur); returns the cover id (free with vs_free).
 * vs_update_cover: patch merged into the params (null removes a key).
 * vs_set_cover_at: {"x","y","w","h","rotation","amount"} at a time: keyed when the cover follows motion.
 * vs_key_cover starts following (keys the region at t); vs_clear_cover_keys stops (keeps the region at t).
 * vs_cover_state_at: the params at t plus "following" (free with vs_free).
 * vs_covers_json: [{"id", "params"}] of a clip.
 * vs_cover_preview: applies a style/shape over the middle of an RGBA buffer (thumbnails). */
VS_API char* vs_cover_styles_json(void);
VS_API char* vs_cover_shapes_json(void);
VS_API char* vs_add_cover(vs_session* s, const char* clip_id, const char* params_json);
VS_API int vs_update_cover(vs_session* s, const char* clip_id, const char* cover_id, const char* patch_json);
VS_API int vs_remove_cover(vs_session* s, const char* clip_id, const char* cover_id);
VS_API int vs_set_cover_at(vs_session* s, const char* clip_id, const char* cover_id, int64_t timeline_time,
                           const char* values_json);
VS_API int vs_key_cover(vs_session* s, const char* clip_id, const char* cover_id, int64_t timeline_time);
VS_API int vs_clear_cover_keys(vs_session* s, const char* clip_id, const char* cover_id, int64_t timeline_time);
VS_API char* vs_cover_state_at(vs_session* s, const char* clip_id, const char* cover_id, int64_t timeline_time);
VS_API char* vs_covers_json(vs_session* s, const char* clip_id);
/* The cover's region as shown on the canvas (0..1 of the output frame) at a time, and the reverse:
 * set it from a rectangle drawn on the preview (keyed when the cover follows motion). */
VS_API int vs_cover_canvas_rect(vs_session* s, const char* clip_id, const char* cover_id, int64_t timeline_time,
                                double* xywh);
VS_API int vs_set_cover_canvas_rect(vs_session* s, const char* clip_id, const char* cover_id, int64_t timeline_time,
                                    double x, double y, double w, double h);
VS_API int vs_cover_preview(const char* style, const char* shape, uint8_t* rgba, int width, int height);
/* Follow a moving subject: tracks the cover's region from timeline_from to timeline_to (<= 0: the clip's
 * end) on a background thread. Poll vs_track_status (0 idle, 1 running, 2 done, -1 failed, -2 cancelled,
 * -3 lost the subject early: keys up to there were kept); on 2/-3 the keys were applied as ONE undo step
 * by that status call, so call it from the thread that edits. */
VS_API int vs_track_cover_start(vs_session* s, const char* clip_id, const char* cover_id, int64_t timeline_from,
                                int64_t timeline_to);
VS_API int vs_track_status(vs_session* s, double* progress);
VS_API void vs_track_cancel(vs_session* s);
/* Tracks (all undoable). A locked track refuses clip edits; enabled = 0 hides a video track,
 * muted silences an audio track. index_in_type: 0 = bottom video layer / first audio track. */
VS_API int vs_remove_track(vs_session* s, const char* track_id);
VS_API int vs_rename_track(vs_session* s, const char* track_id, const char* name);
VS_API int vs_set_track_flags(vs_session* s, const char* track_id, int enabled, int locked, int muted);
VS_API int vs_set_track_volume(vs_session* s, const char* track_id, double volume);
VS_API int vs_move_track(vs_session* s, const char* track_id, int index_in_type);
/* Snapping while dragging (read-only, cheap): returns the time to use; *snapped = 1 if it moved
 * onto a clip edge, the playhead (pass -1 for none) or 0. */
VS_API int64_t vs_snap_clip_move(vs_session* s, const char* clip_id, int64_t proposed_start, int64_t tolerance,
                                 int64_t playhead, int* snapped);
VS_API int64_t vs_snap_time(vs_session* s, int64_t t, int64_t tolerance, const char* exclude_clip_id, int64_t playhead,
                            int* snapped);
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
/* Frame sink for native preview surfaces (Android Surface, iOS CVPixelBuffer): called on the
 * core's video thread with each new frame (RGBA8, rows of stride bytes), so the frame goes to the
 * screen without passing through Dart. Must return quickly. Pass NULL to remove; once
 * vs_preview_set_sink returns, the previous sink is never called again. The newest frame is also
 * delivered at once to a newly set sink. */
typedef void (*vs_frame_sink)(void* user, const uint8_t* rgba, int width, int height, int stride, int64_t pts);
VS_API void vs_preview_set_sink(vs_session* s, vs_frame_sink sink, void* user);
/* Called from the audio device thread. Interleaved float, project rate/channels. */
VS_API void vs_pull_audio(vs_session* s, float* out, int64_t frames);
VS_API int vs_audio_format(vs_session* s, int* sample_rate, int* channels);

/* ---- export (background job) ---- */
VS_API int vs_export_start(vs_session* s, const char* out_path, int width, int height, int fps_num, int fps_den,
                           int64_t video_bitrate);
/* Presets sized for this project (canvas aspect kept): JSON array of
 * {"id","name","description","width","height","fps","video_bitrate","estimated_bytes"}. */
VS_API char* vs_export_presets_json(vs_session* s);
VS_API int vs_export_start_preset(vs_session* s, const char* out_path, const char* preset_id);
VS_API int vs_export_status(vs_session* s, double* progress); /* VS_EXPORT_* */
VS_API void vs_export_cancel(vs_session* s);

/* ---- media library (background thumbnails / waveforms, relink) ----
 * Requests return at once; results are computed on a background worker and
 * cached on disk (keyed by file fingerprint). vs_library_serial() increases
 * whenever a result arrives, so a UI can poll it cheaply once per frame. */
VS_API int vs_request_thumbnails(vs_session* s, const char* asset_id, int count, int height);
VS_API int vs_request_waveform(vs_session* s, const char* asset_id, int buckets_per_second);
VS_API int64_t vs_library_serial(vs_session* s);
/* 1 = copied (dst NULL: only sizes), 0 = not ready, -1 = dst too small (count*width*height*4 bytes). */
VS_API int vs_thumbnails(vs_session* s, const char* asset_id, uint8_t* dst, int64_t dst_capacity, int* count,
                         int* width, int* height);
/* Returns the number of buckets (0 = not ready) and copies min/max pairs (2 bytes per bucket) up to dst_capacity. */
VS_API int64_t vs_waveform(vs_session* s, const char* asset_id, int8_t* dst, int64_t dst_capacity,
                           int* buckets_per_second);
VS_API char* vs_missing_assets_json(vs_session* s);                     /* ["asset id", ...] */
VS_API char* vs_relink_candidates_json(vs_session* s, const char* asset_id, const char* search_dir);
VS_API int vs_relink_asset(vs_session* s, const char* asset_id, const char* path); /* undoable */

/* ---- moving a project between devices (.vstudiopkg: one file with the project and its media) ----
 * Both calls block until done: run them off the UI thread. `progress` (may be NULL) gets 0..1 and
 * returns nonzero to cancel. Result JSON: {"ok","cancelled","error","missing":[asset ids left out],
 * "package_dir": the new project's .vstudio folder (unpack)}. */
typedef int (*vs_transfer_progress)(double progress, void* user);
VS_API char* vs_transfer_pack(vs_session* s, const char* out_file, vs_transfer_progress progress, void* user);
/* Session-free: unpacks into dest_root/<name>.vstudio (new name if taken) and dest_root/media/. */
VS_API char* vs_transfer_unpack(const char* package_file, const char* dest_root, vs_transfer_progress progress,
                                void* user);

#ifdef __cplusplus
}
#endif

#endif /* VSTUDIO_C_H */
