// Video Studio Core - decoded frame cache (LRU, bounded by bytes).
// Keys are *source* frames (asset + source frame index + size), not timeline
// positions, so cutting, moving or splitting a clip never invalidates what
// has already been decoded: after an edit the preview re-composites from
// cache instead of decoding again.
#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "vstudio/media.hpp"

namespace vstudio {

class FrameCache {
public:
    explicit FrameCache(std::size_t maxBytes = 512ull * 1024 * 1024) : maxBytes_(maxBytes) {}

    static std::string key(const std::string& assetId, std::int64_t sourceFrame, int w, int h);

    std::shared_ptr<const VideoFrame> get(const std::string& key);
    void put(const std::string& key, std::shared_ptr<const VideoFrame> frame);
    void clear();
    void setMaxBytes(std::size_t b);

    [[nodiscard]] std::size_t bytes() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::uint64_t hits() const { return hits_; }
    [[nodiscard]] std::uint64_t misses() const { return misses_; }

private:
    void evictLocked();
    using Entry = std::pair<std::string, std::shared_ptr<const VideoFrame>>;
    mutable std::mutex mu_;
    std::list<Entry> lru_;  // front = most recent
    std::unordered_map<std::string, std::list<Entry>::iterator> map_;
    std::size_t maxBytes_;
    std::size_t bytes_ = 0;
    std::uint64_t hits_ = 0, misses_ = 0;
};

}  // namespace vstudio
