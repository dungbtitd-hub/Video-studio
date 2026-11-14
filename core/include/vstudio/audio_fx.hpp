// Video Studio Core - clip sound: fades, equaliser, noise reduction, detached audio,
// and the track mixer settings (pan, solo).
//
// Everything is plain data on the clip/track (clip "audio", track "pan"/"solo" in the
// project JSON), applied while mixing (Renderer::renderAudio); the source files are
// never rewritten. Audio effects are modules of kind "effect.audio" (IAudioEffect):
// the built-in ones are "builtin.eq" and "builtin.denoise"; others can be added
// without touching the core.
//
// Determinism: preview mixes small blocks, export bigger ones. Effects get the block
// plus a margin of source audio on both sides (IAudioEffect::margin), and the block
// start is aligned to kAudioFxAlign source samples, so a block sounds the same
// whichever way the timeline is cut into blocks (no clicks at block edges).
//
// Clip JSON:
//   "audio": {"fade_in": 0.5, "fade_out": 1.0,               seconds
//             "eq": {"preset": "voice", "gains": [g80, g250, g1k, g4k, g10k]},  dB, -12..12
//             "denoise": {"amount": 0.7, "profile": [...]},   0..1; profile = noise floor per FFT bin
//             "detached": true}                               sound moved to an audio track
//   "reverse": true, "freeze": true (clip_tools.hpp)
// Track JSON: "pan": -1..1 (left..right), "solo": true.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/time.hpp"

namespace vstudio {

class ModuleRegistry;

// Audio effect: interleaved float samples in, same out. Stateless across calls (the
// block carries its own margins), so render threads can share one instance.
class IAudioEffect {
public:
    static constexpr const char* kModuleKind = "effect.audio";
    virtual ~IAudioEffect() = default;
    // Source frames of context the effect needs before and after the block.
    [[nodiscard]] virtual std::int64_t margin(const Json& params, int sampleRate) const = 0;
    virtual void apply(AudioBuffer& buf, const Json& params) const = 0;
};

namespace audiofx {

constexpr const char* kEqEffect = "builtin.eq";
constexpr const char* kDenoiseEffect = "builtin.denoise";
constexpr std::int64_t kAudioFxAlign = 256;     // denoise hop
constexpr int kDenoiseFft = 1024;
constexpr int kEqBands = 5;
constexpr std::array<double, kEqBands> kEqFreqs{80, 250, 1000, 4000, 10000};
constexpr double kMaxFadeSeconds = 10.0;

struct EqPreset {
    std::string id;
    std::string name;  // Vietnamese UI name
    std::array<double, kEqBands> gains;
};
const std::vector<EqPreset>& eqPresets();
const EqPreset* findEqPreset(const std::string& id);

// clip.extra["audio"] or an empty object.
const Json& clipAudio(const Clip& c);
[[nodiscard]] bool detached(const Clip& c);
[[nodiscard]] bool hasFades(const Clip& c);
// Fade gain (0..1, smooth curve) of the clip at timeline time t.
[[nodiscard]] double fadeGain(const Clip& c, Time t);
// The clip's audio effects in processing order: denoise, then EQ. {"type","params"} each.
[[nodiscard]] std::vector<std::pair<std::string, Json>> clipEffects(const Clip& c);

[[nodiscard]] double trackPan(const Track& t);
[[nodiscard]] bool trackSolo(const Track& t);
// Whether the track is heard: not muted, enabled, and soloed when any track is.
[[nodiscard]] bool trackAudible(const Project& p, const Track& t);

// Runs the clip's effects on `buf` (which must hold `margin` extra frames on both sides).
// Unknown effect modules are skipped.
void applyClipEffects(ModuleRegistry* modules, const std::vector<std::pair<std::string, Json>>& fx, AudioBuffer& buf);
[[nodiscard]] std::int64_t clipMargin(ModuleRegistry* modules, const std::vector<std::pair<std::string, Json>>& fx,
                                      int sampleRate);

// Noise floor per FFT bin (kDenoiseFft/2+1 values) of a stretch of audio: the quiet
// end (15th percentile) of each bin over time, which is the steady hiss/hum.
[[nodiscard]] std::vector<float> noiseProfile(const AudioBuffer& audio);
// Reads up to `maxSeconds` of the clip's source audio (background job) and returns its profile.
// Empty when the clip has no audio or the read failed (*error set).
[[nodiscard]] std::vector<float> analyzeClipNoise(IMediaDecoder& decoder, const Clip& c, int sampleRate,
                                                  double maxSeconds = 20.0, const std::atomic<bool>* cancel = nullptr,
                                                  std::string* error = nullptr);

// Registers builtin.eq and builtin.denoise.
void registerAudioModules(ModuleRegistry& r);

}  // namespace audiofx
}  // namespace vstudio
