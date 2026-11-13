#include "vstudio/frame_cache.hpp"

namespace vstudio {

std::string FrameCache::key(const std::string& assetId, std::int64_t sourceFrame, int w, int h) {
    return assetId + '|' + std::to_string(sourceFrame) + '|' + std::to_string(w) + 'x' + std::to_string(h);
}

std::shared_ptr<const VideoFrame> FrameCache::get(const std::string& k) {
    std::lock_guard lk(mu_);
    auto it = map_.find(k);
    if (it == map_.end()) {
        ++misses_;
        return nullptr;
    }
    ++hits_;
    lru_.splice(lru_.begin(), lru_, it->second);
    return it->second->second;
}

void FrameCache::put(const std::string& k, std::shared_ptr<const VideoFrame> f) {
    if (!f) return;
    std::lock_guard lk(mu_);
    if (auto it = map_.find(k); it != map_.end()) {
        bytes_ -= it->second->second->bytes();
        lru_.erase(it->second);
        map_.erase(it);
    }
    bytes_ += f->bytes();
    lru_.emplace_front(k, std::move(f));
    map_[k] = lru_.begin();
    evictLocked();
}

void FrameCache::evictLocked() {
    while (bytes_ > maxBytes_ && lru_.size() > 1) {
        auto& back = lru_.back();
        bytes_ -= back.second->bytes();
        map_.erase(back.first);
        lru_.pop_back();
    }
}

void FrameCache::clear() {
    std::lock_guard lk(mu_);
    lru_.clear();
    map_.clear();
    bytes_ = 0;
}

void FrameCache::setMaxBytes(std::size_t b) {
    std::lock_guard lk(mu_);
    maxBytes_ = b;
    evictLocked();
}

std::size_t FrameCache::bytes() const {
    std::lock_guard lk(mu_);
    return bytes_;
}

std::size_t FrameCache::size() const {
    std::lock_guard lk(mu_);
    return lru_.size();
}

}  // namespace vstudio
