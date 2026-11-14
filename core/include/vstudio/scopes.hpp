// Video scopes for grading: waveform, RGB parade, vectorscope, histogram.
//
// Scopes read a finished preview frame (what the viewer sees) and draw a small
// RGBA picture of it. They sample at most ~150k pixels, so one scope costs a
// millisecond or two and can run beside playback on a background thread; the
// UI never computes them itself. Pure functions: no state, no allocation
// beyond the returned image.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vstudio/media.hpp"

namespace vstudio {

enum class ScopeKind {
    Waveform = 0,     // luma (Rec.709) per column, 0..100 IRE bottom to top
    Parade = 1,       // R, G, B waveforms side by side
    Vectorscope = 2,  // Cb/Cr plot with 75% colour targets and the skin-tone line
    Histogram = 3,    // R, G, B and luma distributions
};

struct ScopeImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;  // straight alpha; background is opaque near-black
};

// Draws `kind` for `frame` (RGBA8 or RGBA16F) at width x height (clamped to 32..2048).
ScopeImage renderScope(const VideoFrame& frame, ScopeKind kind, int width, int height);

// Histogram counts (256 bins each) for UIs that draw their own (curve editor backdrop).
struct Histogram {
    std::vector<std::uint32_t> r, g, b, luma;
};
Histogram computeHistogram(const VideoFrame& frame);

const char* scopeName(ScopeKind k);  // Vietnamese UI name

}  // namespace vstudio
