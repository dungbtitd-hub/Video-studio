// User presets (FR-018, FR-022): looks the user builds once and reuses, by name.
//
//   "style"  a clip's picture style: colour space (Log input), grade, look, LUT, chroma key,
//            video effects and plugin effects, with their settings. Not what belongs to one
//            clip only: covers and masks (a region of that picture), stabilisation (its
//            measured shake), sound (denoise profile, EQ), text and stickers.
//   "export" export settings: a quality class (export_presets.hpp), a video format, and
//            optional overrides for frame rate, video bitrate, audio bitrate and sound on/off.
//
// Presets live in one small JSON file per user, next to the favourites, shared by every project:
//   {"version": 1, "style": [{"name", "saved", "data"}...], "export": [...]}
// The store writes atomically (temp file, then rename), so a crash never leaves a broken file;
// an unreadable file loads as empty and is rewritten on the next change.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio::presets {

inline constexpr const char* kStyle = "style";
inline constexpr const char* kExport = "export";
inline constexpr std::size_t kMaxNameLength = 60;

struct UserPreset {
    std::string name;
    std::int64_t saved = 0;  // unix seconds
    Json data;
};

class UserPresetStore {
public:
    explicit UserPresetStore(std::string path);  // loads `path` (missing or broken: empty)

    [[nodiscard]] const std::vector<UserPreset>& list(const std::string& kind) const;  // newest first
    [[nodiscard]] const UserPreset* find(const std::string& kind, const std::string& name) const;
    // Adds, or replaces the preset with the same name (moved to the front), then saves.
    // Names are trimmed; empty or unknown kinds are refused with a message.
    bool put(const std::string& kind, const std::string& name, const Json& data, std::string* error = nullptr);
    bool remove(const std::string& kind, const std::string& name, std::string* error = nullptr);
    bool rename(const std::string& kind, const std::string& from, const std::string& to, std::string* error = nullptr);
    [[nodiscard]] const std::string& path() const { return path_; }
    [[nodiscard]] Json toJson() const;

private:
    bool save(std::string* error) const;
    std::string path_;
    std::map<std::string, std::vector<UserPreset>> items_;
};

// ---- style presets ----
// True for effect types that are part of a reusable style (see above).
bool isStyleEffect(const std::string& type);
// {"effects": [{"type", "enabled", "params"}...]} of the clip's style effects, in order.
// Empty effects when the clip has none.
Json captureStyle(const Clip& clip);
// The clip's effects with its style replaced by `style` (fresh effect ids from `newId`):
// non-style effects keep their order, the colour space goes first, the rest of the style
// takes the place of the clip's old style (or goes last).
Json applyStyle(const Json& clipEffects, const Json& style, const std::function<std::string()>& newId);
// "Màu: chỉnh màu, look Điện ảnh · LUT · Phông xanh" style summary for lists.
std::string describeStyle(const Json& style);

// ---- export presets ----
struct UserExport {
    std::string base = "fhd1080";  // export_presets.hpp id
    std::string format = "h264";   // exportFormats() id
    double fps = 0;                // 0 = the base preset's rule
    double videoMbps = 0;          // 0 = from the base preset
    int audioKbps = 0;             // 0 = default (192)
    bool audio = true;
};
Json toJson(const UserExport& e);
// Unknown base or format ids are refused with a message.
std::optional<UserExport> userExportFromJson(const Json& j, std::string* error = nullptr);
ExportSettings exportSettingsFor(const Project& p, const UserExport& e);
// "Full HD 1080p · H.265 · 25 khung/giây · 12 Mbit/s · không tiếng"
std::string describeExport(const UserExport& e);

}  // namespace vstudio::presets
