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
/* Video effects (video_fx.hpp): zoom, rotate, reflection. vs_video_fx_json: [{"id","name","group",
 * "groupName","animated","usesLine"}]; groups "zoom", "rotate", "reflect", one effect of each per clip.
 * vs_set_clip_video_fx: fx_id "" removes the group's effect; amount 0..2, speed 0.25..4, line 0.1..0.9
 * (reflections, < 0 = default); one undo step. vs_video_fx_icon: the effect at `seconds` on `picture`
 * (RGBA, picture_w x picture_h; NULL = a built-in landscape) into opaque RGBA out. */
VS_API char* vs_video_fx_json(void);
VS_API int vs_set_clip_video_fx(vs_session* s, const char* clip_ids_json, const char* group, const char* fx_id,
                                double amount, double speed, double line);
VS_API int vs_video_fx_icon(const char* fx_id, double seconds, const uint8_t* picture, int picture_w, int picture_h,
                            uint8_t* out, int width, int height);

/* ---- Log / HDR input and 3D LUTs (color_space.hpp); undoable ---- */
VS_API char* vs_log_formats_json(void); /* [{"id","name","vendor"}] */
/* input "" removes; the conversion always runs before other effects. */
VS_API int vs_set_clip_color_space(vs_session* s, const char* clip_ids_json, const char* input, double exposure);
/* .cube file, absolute or relative to the project's media folder; "" removes. Bad files fail here. */
VS_API int vs_set_clip_lut(vs_session* s, const char* clip_ids_json, const char* path, double strength);
VS_API char* vs_suggested_log_format(vs_session* s, const char* asset_id); /* "hlg" or "" */
/* ---- pro colour grade (grade.hpp): wheels, curves, HSL mixer, qualifier; one effect per clip ----
 * vs_set_clip_grade: params_json is the grade object; "" / "null" / neutral params remove it. One undo step.
 * vs_clip_grade_json: the clip's grade params ("{}" when none).
 * vs_hsl_bands_json: [{"id","name","hue"}].
 * vs_grade_curve: samples the curve through points_json [[x,y],...] at n inputs into out (what is applied).
 * vs_wheel_to_balance / vs_balance_to_wheel: wheel position (deg, 0..1) <-> luma-neutral rgb[3].
 * vs_set_effect_preview: live preview of an uncommitted change while dragging (no undo step, project
 *   untouched): type e.g. "builtin.color_grade"; clip_id "" clears. Commit with vs_set_clip_grade on release.
 * vs_grade_preview: applies grade params to an RGBA buffer in place (thumbnails; no session needed). */
VS_API int vs_set_clip_grade(vs_session* s, const char* clip_ids_json, const char* params_json);
VS_API char* vs_clip_grade_json(vs_session* s, const char* clip_id);
VS_API char* vs_hsl_bands_json(void);
VS_API int vs_grade_curve(const char* points_json, int n, float* out);
VS_API void vs_wheel_to_balance(double angle_deg, double distance, double* rgb3);
VS_API void vs_balance_to_wheel(const double* rgb3, double* angle_deg, double* distance);
VS_API int vs_set_effect_preview(vs_session* s, const char* clip_id, const char* type, const char* params_json);
VS_API int vs_grade_preview(const char* params_json, uint8_t* rgba, int width, int height);
/* Eyedropper: average colour (0..1 RGB) of a 5x5 spot of the newest preview frame at x, y (0..1 of
 * the frame). Returns 0, or -1 when no frame has been shown yet. */
VS_API int vs_preview_pick_color(vs_session* s, double x, double y, double* rgb3);
/* Scope of the newest preview frame into rgba_out (width*height*4): kind 0 waveform, 1 RGB parade,
 * 2 vectorscope, 3 histogram. A couple of milliseconds; returns -1 before the first frame. */
VS_API int vs_preview_scope(vs_session* s, int kind, int width, int height, uint8_t* rgba_out);
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
/* ---- transitions between clips (transitions.hpp); undoable ----
 * vs_transitions_json: [{"id","name","category"}].
 * vs_set_transition: edge 0 = the clip's start (the cut from the clip touching it on the left, or a fade in
 * from nothing), 1 = its end (the cut to the next touching clip, or a fade out to nothing). type "" removes.
 * duration in ticks (<= 0: the default 0.5 s), clamped to 0.1..5 s and, when played, to the clips' lengths.
 * vs_set_transition_everywhere: on every cut between touching clips; returns how many, or -1.
 * vs_cut_points_json: {"<track id>": [{"at", "from", "to", "type", "duration"}]} for every video track:
 * every place a transition can go (from/to null at free ends; type null when none; duration as played).
 * vs_transition_preview: blends a and b (RGBA, width x height) into out at progress 0..1 (thumbnails). */
VS_API char* vs_transitions_json(void);
VS_API int vs_set_transition(vs_session* s, const char* clip_id, int edge, const char* type, int64_t duration);
VS_API int vs_set_transition_everywhere(vs_session* s, const char* type, int64_t duration);
VS_API char* vs_cut_points_json(vs_session* s);
VS_API int vs_transition_preview(const char* type, const uint8_t* a, const uint8_t* b, uint8_t* out, int width,
                                 int height, double progress);
/* ---- text and stickers (text.hpp, stickers.hpp, generators.hpp) ----
 * A text or sticker clip has no media: its "content" (in vs_project_json clips) says what it draws,
 * {"kind": "text" | "sticker", ...}. Move/scale it like any clip with vs_set_property_at (x, y, scale).
 * vs_fonts_json: [{"id","name","style","bundled"}]. vs_register_font: adds a TrueType/OpenType font
 * (e.g. a company font) under `id`; 0 ok, -1 not a usable font.
 * vs_text_animations_json: {"animations": [{"id","name","per_letter"}], "loops": [...]}; an animation
 * plays as "in" and, backwards, as "out".
 * vs_text_presets_json: [{"id","name","style"}]: one-tap looks, merge "style" into the content.
 * vs_stickers_json: [{"id","name","category"}] in display order.
 * vs_default_text_json / vs_default_sticker_json: content for a new clip.
 * vs_add_generated_clip: content_json at timeline time `at` for `duration` ticks, on track_id or (NULL/"")
 * the topmost free overlay track (a new one when needed); returns the clip id. One undo step.
 * vs_update_clip_content: JSON merge patch into the clip's content (null removes a key). Undoable.
 * vs_generated_rect: the clip's box on the canvas at a time (0..1 of the frame: x, y, w, h).
 * vs_sticker_icon: draws sticker `id` (its idle motion at `seconds`) into transparent RGBA out.
 * vs_content_preview: draws content at `local` seconds of a clip lasting `length`, fitted into out.
 * Overlay effects (overlays.hpp: snow, rain, confetti, light leaks...) are generated clips too, kind
 * "overlay": vs_overlays_json: [{"id","name","category","tintable"}]; vs_default_overlay_json: content
 * covering the whole frame (vs_add_generated_clip sizes it to the canvas); its "w"/"h" (canvas px) set
 * the region it covers. vs_overlay_icon: a thumbnail over a dark backdrop (opaque RGBA). */
VS_API char* vs_fonts_json(void);
VS_API int vs_register_font(const char* id, const char* name, const uint8_t* bytes, int64_t size);
VS_API char* vs_text_animations_json(void);
VS_API char* vs_text_presets_json(void);
VS_API char* vs_stickers_json(void);
VS_API char* vs_default_text_json(const char* text);
VS_API char* vs_default_sticker_json(const char* sticker_id);
VS_API char* vs_add_generated_clip(vs_session* s, const char* content_json, int64_t at, int64_t duration,
                                   const char* track_id);
VS_API int vs_update_clip_content(vs_session* s, const char* clip_id, const char* patch_json);
VS_API int vs_generated_rect(vs_session* s, const char* clip_id, int64_t timeline_time, double* xywh);
VS_API int vs_sticker_icon(const char* sticker_id, double seconds, uint8_t* out, int width, int height);
VS_API int vs_content_preview(const char* content_json, double local, double length, uint8_t* out, int width,
                              int height);
VS_API char* vs_overlays_json(void);
VS_API char* vs_default_overlay_json(const char* overlay_id);
VS_API int vs_overlay_icon(const char* overlay_id, double seconds, uint8_t* out, int width, int height);
/* Sets the region an overlay covers (0..1 of the frame) as seen at timeline_time. One undo step. */
VS_API int vs_set_overlay_region(vs_session* s, const char* clip_id, int64_t timeline_time, double x, double y,
                                 double w, double h);
/* ---- subtitles (subtitles.hpp) ----
 * A subtitle track is a video track with a "subtitle" object (style, position, max_width, lang) in
 * vs_project_json; each line is a clip with content {"kind": "subtitle", "text"} (optional "style" /
 * "position" overrides for that line). Move/trim/split/remove lines like any clip; edit a line's text
 * with vs_update_clip_content. vs_content_preview draws a style sample ({"kind":"subtitle", ...style}).
 * vs_subtitle_styles_json: [{"id","name","style"}]; vs_subtitle_positions_json: [{"id","name","position"}];
 * vs_subtitle_languages_json: [{"code","name","english"}]; vs_translation_providers_json: connector presets
 * [{"id","name","provider","url","model","needs_key","hint"}].
 * vs_add_subtitle_track: settings_json NULL or a patch; returns the track id.
 * vs_add_subtitle: a line at `at` (moved past a line there, cut short before the next) on track_id or
 * (NULL/"") the topmost subtitle track, created when needed. Returns the clip id.
 * vs_import_subtitles: file_text is the content of an .srt/.vtt/.ass file; returns the new track id.
 * vs_export_subtitles: format "srt" or "vtt"; returns the file text.
 * vs_set_subtitle_settings: JSON merge patch into the track's subtitle object.
 * vs_shift_subtitles: moves every line by delta ticks. All edits are one undo step each. */
VS_API char* vs_subtitle_styles_json(void);
VS_API char* vs_subtitle_positions_json(void);
VS_API char* vs_subtitle_languages_json(void);
VS_API char* vs_translation_providers_json(void);
VS_API char* vs_add_subtitle_track(vs_session* s, const char* name, const char* settings_json);
VS_API char* vs_add_subtitle(vs_session* s, const char* track_id, int64_t at, const char* text, int64_t duration);
VS_API char* vs_import_subtitles(vs_session* s, const char* file_text, const char* name, const char* lang,
                                 int64_t offset);
VS_API char* vs_export_subtitles(vs_session* s, const char* track_id, const char* format);
VS_API int vs_set_subtitle_settings(vs_session* s, const char* track_id, const char* patch_json);
VS_API int vs_shift_subtitles(vs_session* s, const char* track_id, int64_t delta);
/* AI translation. The core never opens a network connection: the app sends each request and hands
 * the reply back, so any AI service (or an in-house server speaking the open protocol in
 * docs/subtitles.md) can be used.
 * vs_translation_begin: config_json {"provider","url","api_key","model","headers"}; from may be "" (detect);
 * batch_size lines per request (0 = 40). NULL on error (vs_last_error).
 * vs_translation_request_json(t, i): {"method","url","headers":[[name, value]...],"body"}; NULL + error text in
 * vs_translation_error when the config is unusable.
 * vs_translation_accept(t, i, reply_body): 0 ok, -1 the reply was unusable (retry; reason in vs_translation_error).
 * vs_translation_apply: mode 0 replace the texts, 1 new track (source hidden), 2 bilingual (both shown,
 * stacked). Returns the track written. Lines whose batch failed keep their source text. */
typedef struct vs_translation vs_translation;
VS_API vs_translation* vs_translation_begin(vs_session* s, const char* track_id, const char* config_json,
                                            const char* from, const char* to, const char* notes, int batch_size);
VS_API int vs_translation_count(vs_translation* t);
VS_API int vs_translation_done(vs_translation* t);
VS_API char* vs_translation_request_json(vs_translation* t, int index);
VS_API int vs_translation_accept(vs_translation* t, int index, const char* reply_body);
VS_API const char* vs_translation_error(vs_translation* t);
VS_API char* vs_translation_apply(vs_session* s, vs_translation* t, int mode);
VS_API void vs_translation_free(vs_translation* t);
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
/* Video formats: JSON array of {"id","name","description"}; ids "h264", "hevc", "hevc10"
 * (H.265 Main 10, rendered in 16-bit float). */
VS_API char* vs_export_formats_json(vs_session* s);
/* vs_export_presets_json with bitrate and size for a format (NULL or "" = "h264"). */
VS_API char* vs_export_presets_format_json(vs_session* s, const char* format_id);
/* As vs_export_start_preset in a format (NULL or "" = "h264"). A device without an encoder for
 * the format ends the export with VS_EXPORT_FAILED (vs_export_status) and the reason in vs_last_error. */
VS_API int vs_export_start_preset_format(vs_session* s, const char* out_path, const char* preset_id,
                                         const char* format_id);
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

/* ---- live preview during a gesture (corner drag, pinch, slider) ----
 * Each vs_live_* call shows the result of the matching edit (vs_set_property_at, vs_set_overlay_region,
 * vs_update_clip_content) on the preview right away, without changing the project or the undo
 * history. At the end of the gesture make the real edit (one undo step), or call vs_live_end to drop
 * the preview (cancelled gesture). Any real edit replaces the live preview. */
VS_API int vs_live_property_at(vs_session* s, const char* clip_id, int64_t t, const char* values_json);
VS_API int vs_live_overlay_region(vs_session* s, const char* clip_id, int64_t t, double x, double y, double w,
                                  double h);
VS_API int vs_live_clip_content(vs_session* s, const char* clip_id, const char* patch_json);
VS_API void vs_live_end(vs_session* s);

/* ---- recent projects (the start screen's "continue" strip) ----
 * vs_save also writes a small card (name, length, clips) and a poster of the current preview frame into
 * the package, and a successful export marks the exported content, so these calls are instant: they read
 * small files only, never media. Session-free.
 * vs_recent_projects_json: [{"path","name","savedAt" (unix s),"duration" (s),"clips","width","height",
 * "exported","hasPoster"}] from the recent-list file and/or every package in scan_dir (either may be NULL),
 * newest first. unfinished_only keeps projects changed since their last export; exclude (the open project,
 * may be NULL) is left out.
 * vs_recent_touch / vs_recent_remove: put a package first in / take it out of the recent-list file.
 * vs_project_poster: the poster as RGBA8. 1 = copied (dst NULL: only sizes), 0 = none, -1 = dst too small. */
VS_API char* vs_recent_projects_json(const char* recent_file, const char* scan_dir, int unfinished_only, int max,
                                     const char* exclude);
VS_API int vs_recent_touch(const char* recent_file, const char* package_dir);
VS_API int vs_recent_remove(const char* recent_file, const char* package_dir);
VS_API int vs_project_poster(const char* package_dir, uint8_t* dst, int64_t dst_capacity, int* width, int* height);

/* ---- sound and clip tools: one JSON command entry point (audio_fx.hpp, clip_tools.hpp) ----
 * command_json {"cmd": ...}; returns {"ok": true, ...} or {"ok": false, "error"} (free with vs_free).
 *   eq_presets                                  -> {"presets": [{"id","name","gains"}]}
 *   clip_audio {clip}                           -> {"audio", "volume", "reversed", "freeze"}
 *   set_clip_audio {clips: [...], patch}        fades (s), eq {preset|gains}, denoise {amount}; null removes
 *   detach_audio {clip}                         -> {"clip"}: the sound on an audio track
 *   place_audio {asset, at (ticks), track?}     -> {"clip"}: a voice-over take on a free "Thu âm" track
 *   set_track_mix {track, pan (-1..1), solo}; set_track_volume {track, volume}; set_clip_volume {clip, volume}
 *   set_reversed {clips, on}; freeze_frame {clip, at, duration?} -> {"clip"}
 *   speed_presets -> {"presets": [{"id","name","points": [[u, v]...]}]}
 *   clip_speed {clip} -> {"can","why","speed","points": [[u (0..1 of the clip), v]...],"preset","duration" (ticks)}
 *   set_speed {clips, speed (0.1..10), points?, preset?}  points [] = constant speed; later clips move
 *   set_effect {clips, type, params|null}; effect {clip, type} -> {"params"}  (builtin.chroma_key...)
 *   guess_key_color                             -> {"color": "#rrggbb"} from the last preview frame
 *   start {job: "noise"|"stabilize", clip, value (amount|strength)}; job_status -> {"status","progress",
 *     "error"} (0 idle, 1 running, 2 done and applied as one undo step, -1 failed, -2 cancelled); job_cancel
 *   start {job: "follow", clip, at (ticks), size?}  a text/sticker follows the object under it in the video
 *     below, from `at` to the clip's end: x/y (and scale with size) keys, one undo step
 *   stabilize_strength {clip, strength}         re-smooths without measuring again
 * User presets (user_presets.hpp), kept in one file shared by all projects:
 *   presets_init {path}                         the file (until then: in memory only)
 *   user_presets {kind: "style"|"export"} -> {"presets": [{"name","saved","summary","data"}]} newest first
 *   save_style_preset {clip, name} -> {"summary"}  the clip's colour and effects, under that name
 *   apply_style_preset {clips, name}            one undo step; covers, stabilisation and sound stay
 *   save_export_preset {name, data: {base, format, fps?, video_mbps?, audio_kbps?, audio?}}
 *   remove_preset {kind, name}; rename_preset {kind, from, to}
 *   export_preset_info {name | data} -> {"width","height","fps","video_bitrate","audio","estimated_bytes","summary"}
 *   export_user_preset {name, path}             starts the export (vs_export_status as for presets)
 * Picture orientation (orientation.hpp) and layer length:
 *   clip_orientation {clip} -> {"orient": {"rotate","flip_h","flip_v"}}
 *   set_clip_orientation {clips, rotate (0|90|180|270), flip_h, flip_v}  one undo step
 *   set_generated_span {clip, start, end (ticks)}  a text/sticker/overlay over [start, end), one undo step */
VS_API char* vs_tool_command(vs_session* s, const char* command_json);

#ifdef __cplusplus
}
#endif

#endif /* VSTUDIO_C_H */
