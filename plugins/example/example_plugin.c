/* Example Video Studio plugin: "Âm bản" (invert colours, with an amount) and a gain.
 * Build: part of the CMake build (target vstudio_example_plugin); see plugins/README.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vstudio/vstudio_plugin.h"

/* Tiny "key": number reader for flat JSON params. */
static double param(const char* json, const char* key, double fallback) {
    char pat[64];
    const char* p;
    if (!json) return fallback;
    snprintf(pat, sizeof pat, "\"%s\":", key);
    p = strstr(json, pat);
    return p ? atof(p + strlen(pat)) : fallback;
}

static int invert(const char* params, vs_plugin_frame* f) {
    const double amount = param(params, "amount", 1.0);
    for (int y = 0; y < f->height; ++y) {
        uint8_t* px = f->rgba + (size_t)y * f->stride;
        for (int x = 0; x < f->width; ++x, px += 4)
            for (int k = 0; k < 3; ++k) px[k] = (uint8_t)(px[k] + (255 - 2 * px[k]) * amount + 0.5);
    }
    return 0;
}

static int64_t no_margin(const char* params, int32_t sr) {
    (void)params;
    (void)sr;
    return 0;
}

static int gain(const char* params, float* s, int64_t frames, int32_t channels, int32_t sr) {
    const float g = (float)param(params, "gain", 0.5);
    (void)sr;
    for (int64_t i = 0; i < frames * channels; ++i) s[i] *= g;
    return 0;
}

static const vs_plugin_video_effect kVideo[] = {{"example.invert", invert}};
static const vs_plugin_audio_effect kAudio[] = {{"example.gain", no_margin, gain}};
static const vs_plugin_info kInfo = {VS_PLUGIN_ABI, 1, kVideo, 1, kAudio};

VS_PLUGIN_EXPORT const vs_plugin_info* vs_plugin_entry(void) { return &kInfo; }
