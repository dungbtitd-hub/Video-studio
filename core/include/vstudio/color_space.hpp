// Log and HDR footage to Rec.709, and 3D LUTs (.cube).
//
// Phones and cameras record "flat" Log or HLG video so grading has room.
// The "builtin.color_space" effect turns it into normal-looking Rec.709:
//   1. decode the camera's Log curve to scene-linear light (per channel),
//   2. convert the camera gamut to Rec.709 primaries (3x3 matrix via XYZ, D65),
//   3. exposure, then a soft highlight shoulder (mid-grey untouched),
//   4. Rec.709 encoding.
// Curves and gamuts follow the manufacturers' published formulas. Formats
// without a published formula (e.g. DJI D-Log M) use the maker's official
// LUT through "builtin.lut" instead.
//
// Both effects run in float on RGBA16F frames (see pixel.hpp) and must be the
// first effect on a clip, so later grading works on Rec.709 pictures.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio {

inline constexpr const char* kColorSpaceEffect = "builtin.color_space";
inline constexpr const char* kLutEffect = "builtin.lut";

struct LogFormat {
    std::string id;      // stored in the project: "slog3", "vlog", "clog3", "nlog", "applelog", "dlog", "hlg"
    std::string name;    // UI name: "Sony S-Log3 / S-Gamut3.Cine"
    std::string vendor;  // "Sony"
};
const std::vector<LogFormat>& logFormats();
const LogFormat* findLogFormat(const std::string& id);

// The input a clip of this asset most likely needs ("" = none / unknown).
// Only what the file states is trusted (HLG / PQ transfer tags); camera Log
// is rarely tagged, so the user picks it.
std::string suggestedLogFormat(const MediaAsset& asset);

// params: { "input": id, "exposure": stops (-4..4), "highlights": true (soft shoulder) }
void applyColorSpace(VideoFrame& frame, const Json& params);

// ---- 3D LUT ----
struct Lut3D {
    int size = 0;                  // N: N*N*N entries, red fastest (as in .cube)
    float domainMin[3] = {0, 0, 0};
    float domainMax[3] = {1, 1, 1};
    std::vector<float> rgb;        // N^3 * 3
    std::string title;
    // Trilinear lookup of an input colour (values outside the domain are clamped).
    void apply(float& r, float& g, float& b) const;
};
// Parses an Adobe/Resolve .cube file (3D; 1D shaper LUTs are refused with a message).
std::shared_ptr<const Lut3D> loadCubeLut(const std::string& path, std::string* error);
std::shared_ptr<const Lut3D> parseCubeLut(const std::string& text, std::string* error);
// Cached by path and modification time: preview reads the file once.
std::shared_ptr<const Lut3D> cachedCubeLut(const std::string& path, std::string* error);

// `path` as is when absolute, else joined to `base` (UTF-8 on every platform).
std::string resolveResourcePath(const std::string& path, const std::string& base);

// params: { "path": "luts/x.cube" (absolute or relative to "base"), "base": dir, "strength": 0..1 }
void applyLut(VideoFrame& frame, const Json& params);

}  // namespace vstudio
