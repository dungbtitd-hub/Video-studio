/* Video Studio Plugin API (beta, FR-029) - stable C ABI for third-party effects.
 *
 * A plugin is a folder with a manifest and a shared library:
 *   my-plugin/
 *     plugin.json      {"abi": 1, "id": "com.example.fx", "name": "...", "version": "1.0.0",
 *                       "library": {"windows": "fx.dll", "macos": "libfx.dylib", "linux": "libfx.so"},
 *                       "video_effects": [{"id": "com.example.fx.invert", "name": "Invert"}],
 *                       "audio_effects": [{"id": "com.example.fx.gain", "name": "Gain"}]}
 *     libfx.so
 *
 * The app reads only the manifests at start-up; the library is loaded the first time one of
 * its effects is used (a project that never uses it never loads it). Effects are applied on
 * render threads: they must be thread-safe and keep no state between calls (everything they
 * need is in params_json). The library exports one function, vs_plugin_entry.
 *
 * Plugins run inside the app: only install plugins you trust. iOS and Android do not load
 * plugins (store rules); there, projects keep plugin effects and show them as unavailable. */
#ifndef VSTUDIO_PLUGIN_H
#define VSTUDIO_PLUGIN_H

#include <stdint.h>

#if defined(_WIN32)
#define VS_PLUGIN_EXPORT __declspec(dllexport)
#else
#define VS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VS_PLUGIN_ABI 1

/* An 8-bit RGBA picture (straight alpha), rows `stride` bytes apart; edited in place. */
typedef struct vs_plugin_frame {
    int32_t width;
    int32_t height;
    int32_t stride;
    uint8_t* rgba;
    double seconds; /* time since the clip's start */
} vs_plugin_frame;

typedef struct vs_plugin_video_effect {
    const char* id; /* must match the manifest */
    /* params_json: the effect's params from the project (UTF-8 JSON object). Return 0 on success. */
    int (*apply)(const char* params_json, vs_plugin_frame* frame);
} vs_plugin_video_effect;

typedef struct vs_plugin_audio_effect {
    const char* id;
    /* Frames of source context needed before/after a block (filters with memory). */
    int64_t (*margin)(const char* params_json, int32_t sample_rate);
    /* Interleaved float samples, edited in place. Return 0 on success. */
    int (*apply)(const char* params_json, float* samples, int64_t frames, int32_t channels, int32_t sample_rate);
} vs_plugin_audio_effect;

typedef struct vs_plugin_info {
    int32_t abi; /* VS_PLUGIN_ABI */
    int32_t video_effect_count;
    const vs_plugin_video_effect* video_effects;
    int32_t audio_effect_count;
    const vs_plugin_audio_effect* audio_effects;
} vs_plugin_info;

/* The one symbol a plugin library exports. */
typedef const vs_plugin_info* (*vs_plugin_entry_fn)(void);
#define VS_PLUGIN_ENTRY_NAME "vs_plugin_entry"

#ifdef __cplusplus
}
#endif

#endif /* VSTUDIO_PLUGIN_H */
