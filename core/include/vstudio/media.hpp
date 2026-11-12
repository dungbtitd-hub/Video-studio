// Video Studio Core - platform media interfaces (plan 3.4 / 3.5).
// The core never calls FFmpeg, AVFoundation or MediaCodec directly: each
// platform supplies an IMediaBackend (FFmpeg on Windows/Linux, Apple adapter
// on macOS/iOS, MediaCodec on Android).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vstudio/time.hpp"

namespace vstudio {

// Tightly-packed 8-bit RGBA, straight alpha. Stride = width * 4.
struct VideoFrame {
    int width = 0;
    int height = 0;
    Time pts;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
    void allocate(int w, int h) {
        width = w;
        height = h;
        rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    }
};

// Interleaved float samples in [-1, 1].
struct AudioBuffer {
    int sampleRate = 48000;
    int channels = 2;
    std::vector<float> samples;  // frames * channels
    [[nodiscard]] std::int64_t frames() const { return channels ? static_cast<std::int64_t>(samples.size()) / channels : 0; }
};

struct ProbeResult {
    bool hasVideo = false;
    bool hasAudio = false;
    bool isStillImage = false;
    Time duration;
    std::string videoCodec;
    int width = 0;
    int height = 0;
    Rational fps{0, 1};
    bool vfr = false;
    int rotation = 0;  // degrees, from display matrix
    std::string audioCodec;
    int sampleRate = 0;
    int channels = 0;
};

class IMediaDecoder {
public:
    virtual ~IMediaDecoder() = default;
    virtual bool open(const std::string& path, std::string* error) = 0;
    // Output frames are scaled (keeping aspect) to fit inside this box.
    // Preview asks for display size instead of decoding 4K RGBA frames:
    // this is the single biggest saving for large media. 0 = native size.
    virtual void setMaxOutputSize(int maxWidth, int maxHeight) { (void)maxWidth; (void)maxHeight; }
    // Frame showing at `sourceTime` (the latest frame with pts <= sourceTime),
    // converted to RGBA. Sequential forward reads must not re-seek.
    virtual std::optional<VideoFrame> videoFrameAt(Time sourceTime) = 0;
    // Reads `frameCount` audio frames starting at `sourceTime`, resampled to
    // the requested rate / channel count. Missing audio is returned as silence.
    virtual AudioBuffer readAudio(Time sourceTime, std::int64_t frameCount, int sampleRate, int channels) = 0;
};

struct ExportSettings {
    std::string container = "mp4";
    std::string videoCodec = "h264";
    int width = 1920;
    int height = 1080;
    Rational fps{30, 1};
    std::int64_t videoBitrate = 8'000'000;
    std::string audioCodec = "aac";
    int sampleRate = 48000;
    int channels = 2;
    std::int64_t audioBitrate = 192'000;
    bool includeAudio = true;  // proxies are video-only
    int gopSize = 0;        // 0 = encoder default; proxies use a short GOP for fast seeking
    bool fastPreset = false; // proxies/preview renders favour speed over size
};

class IMediaEncoder {
public:
    virtual ~IMediaEncoder() = default;
    virtual bool open(const std::string& path, const ExportSettings& s, std::string* error) = 0;
    // Frames arrive in order; frame i has pts = i / fps.
    virtual bool writeVideo(const VideoFrame& frame, std::string* error) = 0;
    // Audio arrives contiguous from t = 0.
    virtual bool writeAudio(const AudioBuffer& audio, std::string* error) = 0;
    virtual bool finish(std::string* error) = 0;
};

// Module kind "media.backend" (see modules.hpp).
class IMediaBackend {
public:
    static constexpr const char* kModuleKind = "media.backend";
    virtual ~IMediaBackend() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual std::optional<ProbeResult> probe(const std::string& path, std::string* error) = 0;
    virtual std::unique_ptr<IMediaDecoder> createDecoder() = 0;
    virtual std::unique_ptr<IMediaEncoder> createEncoder() = 0;
};

// Further platform interfaces named in the plan. P0 only needs the
// declarations; implementations live in each platform shell.
class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    // Pull model: the device asks for `frames` frames; the callback fills them.
    using Pull = std::function<void(float* interleaved, std::int64_t frames)>;
    virtual bool start(int sampleRate, int channels, Pull pull) = 0;
    virtual void stop() = 0;
    // Device clock in samples played, used as the master clock for A/V sync.
    [[nodiscard]] virtual std::int64_t playedFrames() const = 0;
};

class IFileAccess {
public:
    virtual ~IFileAccess() = default;
    // Resolve a platform URI (content://, file://, PHAsset id, ...) to a readable path.
    virtual std::optional<std::string> resolve(const std::string& uri) = 0;
};

class IGpuContext {
public:
    virtual ~IGpuContext() = default;
    [[nodiscard]] virtual std::string api() const = 0;  // "metal", "d3d11", "vulkan", "gles", "cpu"
};

class IThermalMonitor {
public:
    virtual ~IThermalMonitor() = default;
    // 0 = nominal ... 3 = critical. Mobile lowers preview quality as it rises.
    [[nodiscard]] virtual int level() const = 0;
};

}  // namespace vstudio
