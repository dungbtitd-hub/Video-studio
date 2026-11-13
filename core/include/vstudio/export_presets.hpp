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

// Rough output size in bytes for `seconds` of video (video + audio bitrate).
std::int64_t estimateExportBytes(const ExportSettings& s, double seconds);

}  // namespace vstudio
