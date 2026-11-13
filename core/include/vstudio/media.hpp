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
// RGBA8: 8 bits per channel in `rgba`. RGBA16F: half floats in `half` (see pixel.hpp),
// used for 10-bit and Log sources and for grading so gradients do not band.
enum class PixelFormat : std::uint8_t { RGBA8, RGBA16F };

struct VideoFrame {
    int width = 0;
    int height = 0;
    Time pts;
    PixelFormat format = PixelFormat::RGBA8;
    std::vector<std::uint8_t> rgba;   // RGBA8
    std::vector<std::uint16_t> half;  // RGBA16F

    [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
    [[nodiscard]] bool deep() const { return format == PixelFormat::RGBA16F; }
    void allocate(int w, int h, PixelFormat fmt = PixelFormat::RGBA8) {
        width = w;
        height = h;
        format = fmt;
        const std::size_t n = static_cast<std::size_t>(w) * h * 4;
        if (fmt == PixelFormat::RGBA8) {
            rgba.assign(n, 0);
            half.clear();
        } else {
            half.assign(n, 0);
            rgba.clear();
        }
    }
    [[nodiscard]] std::size_t bytes() const { return rgba.size() + half.size() * 2; }
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
    // Colour description of the video stream ("" = unknown / Rec.709 8-bit).
    int bitDepth = 8;
    std::string transfer;   // e.g. "bt709", "arib-std-b67" (HLG), "smpte2084" (PQ)
    std::string primaries;  // e.g. "bt709", "bt2020"
};

class IMediaDecoder {
public:
    virtual ~IMediaDecoder() = default;
    virtual bool open(const std::string& path, std::string* error) = 0;
    // Output frames are scaled (keeping aspect) to fit inside this box.
    // Preview asks for display size instead of decoding 4K RGBA frames:
    // this is the single biggest saving for large media. 0 = native size.
    virtual void setMaxOutputSize(int maxWidth, int maxHeight) { (void)maxWidth; (void)maxHeight; }
    // Deep output: sources with more than 8 bits per channel come out as RGBA16F
    // instead of being rounded to 8 bits. Decoders that cannot do it ignore this.
    virtual void setDeepOutput(bool on) { (void)on; }
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
    int bitDepth = 8;        // 10: deep render + Main10 encode (videoCodec "hevc")
};

class IMediaEncoder {
public:
    virtual ~IMediaEncoder() = default;
    virtual bool open(const std::string& path, const ExportSettings& s, std::string* error) = 0;
    // Takes RGBA16F frames (10-bit encode). Encoders without it get 8-bit frames.
    [[nodiscard]] virtual bool supportsDeep() const { return false; }
    // Frames arrive in order; frame i has pts = i / fps.
    virtual bool writeVideo(const VideoFrame& frame, std::string* error) = 0;
    // Audio arrives contiguous from t = 0.
    virtual bool writeAudio(const AudioBuffer& audio, std::string* error) = 0;
    // Flushes and finalizes the file; when it returns the file is complete and closed.
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
