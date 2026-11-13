// Video Studio Core - media library services: thumbnail strips and audio
// waveforms generated in the background, cached on disk, and relink helpers
// for media that moved.
//
// Nothing here runs on the caller's thread except cache lookups: requests are
// queued on the JobScheduler at Background priority (so preview and export
// always go first) and the listener is called from a worker thread when a
// result is ready. Results are immutable and shared, so UIs can hold them
// without copying.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/scheduler.hpp"

namespace vstudio {

// `count` frames of `width` x `height` RGBA, evenly spread over the asset.
struct ThumbnailStrip {
    int width = 0;
    int height = 0;
    std::vector<Time> times;
    std::vector<std::uint8_t> rgba;  // count * width * height * 4, frame after frame
    [[nodiscard]] int count() const { return static_cast<int>(times.size()); }
    [[nodiscard]] const std::uint8_t* frame(int i) const {
        return rgba.data() + static_cast<std::size_t>(i) * width * height * 4;
    }
};

// Peak envelope of the mono mix: for each bucket a (min, max) pair in -127..127.
struct Waveform {
    int bucketsPerSecond = 100;
    std::vector<std::int8_t> peaks;  // min0, max0, min1, max1, ...
    int loudest = 0;                 // max |peak|, 0..127: lets a UI scale quiet audio up
    [[nodiscard]] std::size_t buckets() const { return peaks.size() / 2; }
};

class MediaLibrary {
public:
    enum class Kind { Thumbnails, Waveform };
    // Called on a worker thread. ok = false if the media could not be read.
    using Listener = std::function<void(const Id& assetId, Kind kind, bool ok)>;

    // `cacheDir` (optional) keeps results across sessions, keyed by the asset
    // fingerprint so a moved or relinked file is not processed again.
    MediaLibrary(std::shared_ptr<IMediaBackend> backend, JobScheduler& jobs, std::string cacheDir = {});
    ~MediaLibrary();  // cancels pending work; never waits for a whole file
    MediaLibrary(const MediaLibrary&) = delete;
    MediaLibrary& operator=(const MediaLibrary&) = delete;

    void setListener(Listener l);
    // `path` is the resolved file path. Repeated requests are free: an
    // existing or in-flight result for the same asset is reused.
    void requestThumbnails(const MediaAsset& a, const std::string& path, int count = 10, int height = 90);
    void requestWaveform(const MediaAsset& a, const std::string& path, int bucketsPerSecond = 100);

    [[nodiscard]] std::shared_ptr<const ThumbnailStrip> thumbnails(const Id& assetId) const;
    [[nodiscard]] std::shared_ptr<const Waveform> waveform(const Id& assetId) const;
    // Drops results for an asset (e.g. after relink to a different file).
    void forget(const Id& assetId);

private:
    struct State;
    std::shared_ptr<State> st_;
    JobScheduler& jobs_;
};

// ---- relink ----

// Assets whose file does not exist (resolved against `baseDir`).
std::vector<Id> missingAssets(const Project& p, const std::string& baseDir);

struct RelinkCandidate {
    std::string path;
    bool fingerprintMatch = false;  // same size and content hash: certainly the same file
};
// Files under `searchDir` (recursive, bounded) with the asset's file name, or
// with its fingerprint. Exact fingerprint matches come first.
std::vector<RelinkCandidate> findRelinkCandidates(const MediaAsset& a, const std::string& searchDir, int maxDepth = 6,
                                                  std::size_t maxFiles = 50'000);

}  // namespace vstudio
