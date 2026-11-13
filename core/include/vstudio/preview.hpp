// Video Studio Core - asynchronous preview engine.
//
// Threading model (why the UI never stutters):
//   * UI thread: edits the timeline (cheap, metadata only), publishes an
//     immutable Project snapshot, calls requestFrame()/play(). Never decodes.
//   * Video thread: renders only the *latest* requested frame (older
//     requests are dropped, so scrubbing a long timeline never queues up
//     work), then prefetches the next frames while playing.
//   * Audio thread: keeps a ~250 ms ring buffer filled; the audio device
//     pulls from it. The audio sample clock is the master clock; video
//     follows it (A/V sync).
// Decoded source frames live in a byte-bounded FrameCache keyed by source
// frame, so an edit re-composites from cache instead of decoding again.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "vstudio/frame_cache.hpp"
#include "vstudio/render.hpp"

namespace vstudio {

struct PreviewConfig {
    int width = 960;    // preview render size (fit inside)
    int height = 540;
    int prefetchFrames = 8;
    std::size_t cacheBytes = 384ull * 1024 * 1024;
    double audioBufferSeconds = 0.25;
};

class PreviewEngine {
public:
    using FrameCallback = std::function<void(std::shared_ptr<const VideoFrame>)>;

    PreviewEngine(std::shared_ptr<IMediaBackend> backend, std::string projectDir, PreviewConfig cfg = {},
                  ModuleRegistry* modules = nullptr);
    ~PreviewEngine();
    PreviewEngine(const PreviewEngine&) = delete;
    PreviewEngine& operator=(const PreviewEngine&) = delete;

    // Publish a new immutable snapshot (call after every edit). O(1) for the caller.
    void setProject(std::shared_ptr<const Project> snapshot);
    void setProjectDir(const std::string& dir);
    void setProxy(const Id& assetId, const std::string& path);
    // Called on the video thread with each finished frame. Marshal to the UI thread yourself.
    // It may run until the destructor has joined the threads, so whatever it captures must
    // outlive this engine.
    void setFrameCallback(FrameCallback cb);
    void setPreviewSize(int width, int height);

    // Live preview of an uncommitted effect change on one clip (see EffectOverride);
    // nullptr clears it. Latest wins; re-renders the current frame when paused.
    void setEffectOverride(std::shared_ptr<const EffectOverride> o);

    // Latest-wins frame request (scrubbing / paused seek).
    void requestFrame(Time t);

    void play(Time from);
    void pause();
    [[nodiscard]] bool playing() const { return playing_.load(); }
    // Current playhead: while playing it is derived from the audio sample clock.
    [[nodiscard]] Time playhead() const;

    // Audio device pull (real-time safe: copies from the ring buffer, never decodes).
    // Fills silence on underrun.
    void pullAudio(float* interleaved, std::int64_t frames);

    [[nodiscard]] const FrameCache& cache() const { return cache_; }
    [[nodiscard]] std::uint64_t framesRendered() const { return framesRendered_.load(); }
    [[nodiscard]] std::uint64_t audioUnderruns() const { return underruns_.load(); }

private:
    void videoLoop();
    void audioLoop();
    std::shared_ptr<const Project> snapshot() const;

    PreviewConfig cfg_;
    FrameCache cache_;
    std::shared_ptr<IMediaBackend> backend_;
    ModuleRegistry* modules_;

    mutable std::mutex snapMu_;
    std::shared_ptr<const Project> project_;
    std::shared_ptr<const EffectOverride> override_;
    std::string projectDir_;
    std::vector<std::pair<Id, std::string>> pendingProxies_;
    std::uint64_t projectGen_ = 0;

    // video
    mutable std::mutex vMu_;
    std::condition_variable vCv_;
    std::optional<Time> requested_;
    FrameCallback onFrame_;
    std::thread vThread_;

    // audio ring (interleaved, guarded by aMu_)
    mutable std::mutex aMu_;
    std::condition_variable aCv_;
    std::vector<float> ring_;
    std::size_t ringRead_ = 0, ringFill_ = 0;
    std::int64_t nextRenderSample_ = 0;   // timeline sample to render next
    std::int64_t playedSamples_ = 0;      // samples handed to the device since play()
    std::int64_t playStartSample_ = 0;
    std::uint64_t audioGen_ = 0;          // bumps on play()/seek to flush the ring
    std::thread aThread_;

    std::atomic<bool> playing_{false};
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> framesRendered_{0};
    std::atomic<std::uint64_t> underruns_{0};
    Time pausedAt_;
    std::atomic<std::int64_t> wallStart_{0};
    std::atomic<bool> audioDriven_{false};
};

}  // namespace vstudio
