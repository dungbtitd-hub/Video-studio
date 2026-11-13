// Video Studio Core - minimal render graph (P0): timeline evaluation,
// layer compositing (through an ICompositor module, see compositor.hpp),
// audio mixer and the export pipeline.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vstudio/compositor.hpp"
#include "vstudio/frame_cache.hpp"
#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/modules.hpp"

namespace vstudio {

struct VideoLayer {
    const Clip* clip = nullptr;
    const MediaAsset* asset = nullptr;
    Time sourceTime;
    std::size_t trackIndex = 0;
};

// Video layers visible at `t`, bottom to top (enabled video tracks only).
std::vector<VideoLayer> evaluateVideo(const Project& p, Time t);

// Owns one decoder per (asset, purpose) so preview, audio and export read sequentially.
class MediaPool {
public:
    MediaPool(std::shared_ptr<IMediaBackend> backend, std::string projectDir);
    // maxW/maxH: decode straight to this box (0 = native). Preview passes the
    // preview size so a 4K source is never expanded to full-size RGBA.
    IMediaDecoder* decoder(const MediaAsset& a, const std::string& purpose, std::string* error, int maxW = 0,
                           int maxH = 0, bool allowProxy = false);
    [[nodiscard]] std::string resolve(const MediaAsset& a) const;
    // Registers a low-resolution proxy file used for preview (never for export).
    void setProxy(const Id& assetId, std::string path);
    [[nodiscard]] bool hasProxy(const Id& assetId) const { return proxies_.count(assetId) != 0; }
    void setProjectDir(std::string dir) { projectDir_ = std::move(dir); }
    void clear() { decoders_.clear(); }
    [[nodiscard]] std::shared_ptr<IMediaBackend> backend() const { return backend_; }

private:
    std::shared_ptr<IMediaBackend> backend_;
    std::string projectDir_;
    std::map<std::string, std::unique_ptr<IMediaDecoder>> decoders_;
    std::map<Id, std::string> proxies_;
};

class Renderer {
public:
    // `cache` (optional) holds decoded source frames shared across renders.
    // `preview` = true decodes at output size and may use proxies.
    // `modules` (optional) provides effect modules for clip.effects.
    Renderer(const Project& p, MediaPool& pool, FrameCache* cache = nullptr, bool preview = false,
             ModuleRegistry* modules = nullptr)
        : project_(p), pool_(pool), cache_(cache), preview_(preview), modules_(modules) {}
    // Composites the frame at timeline time `t`, at `width` x `height`
    // (defaults to the project canvas). Background is opaque black. Uses the
    // best "compositor" module in `modules` (GPU when available), else the CPU one.
    VideoFrame renderVideo(Time t, int width = 0, int height = 0, const std::string& purpose = "preview");
    // Mixes `frames` audio frames starting at timeline sample `startSample`
    // at the project sample rate/channels.
    AudioBuffer renderAudio(std::int64_t startSample, std::int64_t frames, const std::string& purpose = "audio");
    [[nodiscard]] const std::vector<std::string>& warnings() const { return warnings_; }

private:
    std::shared_ptr<const VideoFrame> sourceFrame(const VideoLayer& layer, int outW, int outH, const std::string& purpose);
    ICompositor& compositor();

    const Project& project_;
    MediaPool& pool_;
    FrameCache* cache_;
    bool preview_;
    ModuleRegistry* modules_;
    std::shared_ptr<ICompositor> compositor_;
    std::vector<std::string> warnings_;
    void warn(std::string w);
};

struct ExportProgress {
    std::int64_t frame = 0;
    std::int64_t totalFrames = 0;
};

struct ExportResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::int64_t framesWritten = 0;
    double seconds = 0.0;  // wall-clock time
};

// Renders the whole timeline and encodes it. `cancel` may be flipped from another thread.
ExportResult exportProject(const Project& p, MediaPool& pool, IMediaEncoder& encoder, const std::string& outPath,
                           const ExportSettings& s, const std::function<void(const ExportProgress&)>& onProgress = {},
                           const std::atomic<bool>* cancel = nullptr, ModuleRegistry* modules = nullptr);

}  // namespace vstudio
