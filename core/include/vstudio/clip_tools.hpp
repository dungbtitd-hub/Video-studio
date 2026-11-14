// Video Studio Core - clip tools: reverse, freeze frame, chroma key, stabilisation, favourites.
//
// All of them are data on the clip; media is never rewritten:
//   - Reverse: Clip::reversed. The renderer decodes short blocks forwards and shows them
//     backwards (frames shared through the frame cache), the sound is read and reversed.
//   - Freeze frame: a clip of one source frame stretched over its length with a very slow
//     speed (so every timeline time maps into that frame), marked "freeze": true (silent).
//   - Chroma key: effect "builtin.chroma_key" {"color": "#00ff00", "similarity", "smoothness",
//     "spill"} that makes the key colour transparent so the layers below show through.
//   - Stabilisation: a background job measures the shake (global motion between frames on
//     small greyscale frames); effect "builtin.stabilize" {"strength", "path": [[t, x, y]...]}
//     moves each frame against the shake and zooms in just enough to hide the edges.
//   - Favourites: a small JSON file per user (looks, transitions, effects, stickers...).
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/time.hpp"

namespace vstudio {

class ModuleRegistry;

namespace cliptools {

constexpr const char* kChromaKeyEffect = "builtin.chroma_key";
constexpr const char* kStabilizeEffect = "builtin.stabilize";

[[nodiscard]] bool isFreeze(const Clip& c);

// ---- chroma key ----
Json defaultChromaKey();  // green, similarity 0.4, smoothness 0.1, spill 0.5
// Keys `frame` (RGBA8) in place. Exposed for tests and thumbnails.
void applyChromaKey(VideoFrame& frame, const Json& params);
// The dominant saturated colour of a frame's border (the likely screen colour), "#rrggbb".
std::string guessKeyColor(const VideoFrame& frame);

// ---- stabilisation ----
struct ShakeSample {
    double t = 0;       // source seconds
    double x = 0, y = 0;  // camera path, fraction of the frame width / height
};
// Global translation between consecutive frames, accumulated into a path. `next` returns
// frames in source order (small, e.g. 320 px wide) or nullopt at the end.
std::vector<ShakeSample> measureShake(const std::function<std::optional<VideoFrame>()>& next,
                                      const std::atomic<bool>* cancel = nullptr,
                                      const std::function<void(double)>& progress = {}, double expectedFrames = 0);
// Reads the clip's source range from `decoder` and measures it.
std::vector<ShakeSample> analyzeClipShake(IMediaDecoder& decoder, const Clip& c, Rational fps,
                                          const std::atomic<bool>* cancel = nullptr,
                                          const std::function<void(double)>& progress = {});
Json stabilizeParams(const std::vector<ShakeSample>& path, double strength = 0.7);
// Shift (fraction of frame) and zoom applied at source time `t`.
struct StabilizeCorrection {
    double dx = 0, dy = 0, zoom = 1;
};
StabilizeCorrection stabilizeAt(const Json& params, double t);

// ---- favourites ----
class Favorites {
public:
    // Loads `path` if it exists (a missing or broken file starts empty).
    explicit Favorites(std::string path);
    [[nodiscard]] bool contains(const std::string& category, const std::string& id) const;
    // Adds or removes; returns the new state. Saves at once (atomic write).
    bool toggle(const std::string& category, const std::string& id);
    [[nodiscard]] std::vector<std::string> list(const std::string& category) const;  // most recent first
    [[nodiscard]] Json toJson() const;

private:
    void save() const;
    std::string path_;
    std::map<std::string, std::vector<std::string>> items_;
};

void registerClipToolModules(ModuleRegistry& r);

}  // namespace cliptools
}  // namespace vstudio
