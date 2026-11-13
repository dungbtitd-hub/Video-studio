// Deep colour: frames can be RGBA8 (display, 8-bit sources) or RGBA16F
// (half float: 10/12-bit and Log sources, grading, 10-bit export). Half float
// is the native "deep" texture format on Metal, D3D11 and GLES 3, so the CPU
// reference and the GPU modules can share one layout. Values are straight
// alpha and nominally 0..1, but may go outside it between grading steps.
#pragma once

#include <cstdint>
#include <cstring>

#include "vstudio/media.hpp"

namespace vstudio {

// IEEE 754 binary16 <-> float. Decoding is a table lookup; encoding rounds to nearest even.
float halfToFloat(std::uint16_t h);
std::uint16_t floatToHalf(float f);

// Converts in place. To RGBA8 rounds with a 4x4 ordered dither, so smooth 10-bit
// gradients do not turn into visible bands on an 8-bit display or encode.
void convertFrame(VideoFrame& f, PixelFormat to);
// A converted copy (or the same data when the format already matches).
VideoFrame convertedFrame(const VideoFrame& f, PixelFormat to);

}  // namespace vstudio
