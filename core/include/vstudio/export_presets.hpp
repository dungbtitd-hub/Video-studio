// Export presets shared by every front-end (desktop, mobile, CLI).
// A preset names a quality class, not a frame size: the output keeps the
// project's canvas aspect (the renderer fits layers to the output size, so a
// different aspect would re-frame the edit) and its short edge is scaled to
// the preset's (720, 1080, ...). A 9:16 project exported with "1080p" gives
// 1080x1920, a 16:9 one 1920x1080.
#pragma once

#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio {

struct ExportPreset {
    std::string id;           // stable, stored in settings
    std::string name;         // shown to the user (Vietnamese UI)
    std::string description;
    int shortEdge = 0;        // 0 = the canvas size
    int maxFps = 0;           // 0 = project frame rate; else min(project, maxFps)
    double bitsPerPixel = 0.12;  // H.264 bitrate = width * height * fps * bpp
};

const std::vector<ExportPreset>& exportPresets();
const ExportPreset* findExportPreset(const std::string& id);

// Concrete settings for this project: even dimensions in the canvas aspect,
// frame rate and bitrate (clamped to 1..100 Mbit/s).
ExportSettings exportSettingsFor(const Project& p, const ExportPreset& preset);

// Video formats offered next to the presets. "h264" plays everywhere; "hevc" gives about a
// third smaller files; "hevc10" is H.265 Main 10: the render stays in 16-bit float and the
// file keeps 10 bits, so graded Log / 10-bit footage shows no banding. H.265 runs on the GPU's
// media engine where there is one (VideoToolbox, NVENC, Quick Sync, AMF, MediaCodec); a device
// without an encoder for the format fails the export with a clear message.
struct ExportFormat {
    std::string id;
    std::string name;         // Vietnamese UI
    std::string description;
};
const std::vector<ExportFormat>& exportFormats();
const ExportFormat* findExportFormat(const std::string& id);

// Sets codec and bit depth, and scales the H.264 bitrate to the same visual quality
// (H.265 needs about 35% less, 10-bit a little more than 8-bit). Unknown id: unchanged, false.
bool applyExportFormat(ExportSettings& s, const std::string& formatId);
ExportSettings exportSettingsFor(const Project& p, const ExportPreset& preset, const std::string& formatId);

// Rough output size in bytes for `seconds` of video (video + audio bitrate).
std::int64_t estimateExportBytes(const ExportSettings& s, double seconds);

}  // namespace vstudio
