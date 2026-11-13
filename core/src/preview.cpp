#include "vstudio/preview.hpp"

#include <cmath>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace vstudio {

namespace {
using Clock = std::chrono::steady_clock;
}

PreviewEngine::PreviewEngine(std::shared_ptr<IMediaBackend> backend, std::string projectDir, PreviewConfig cfg,
                             ModuleRegistry* modules)
    : cfg_(cfg), cache_(cfg.cacheBytes), backend_(std::move(backend)), modules_(modules),
      projectDir_(std::move(projectDir)) {
    vThread_ = std::thread([this] { videoLoop(); });
    aThread_ = std::thread([this] { audioLoop(); });
}

PreviewEngine::~PreviewEngine() {
    stop_ = true;
    {
        std::lock_guard lk(vMu_);
    }
    vCv_.notify_all();
    {
        std::lock_guard lk(aMu_);
    }
    aCv_.notify_all();
    vThread_.join();
    aThread_.join();
}

void PreviewEngine::setProject(std::shared_ptr<const Project> snap) {
    {
        std::lock_guard lk(snapMu_);
        project_ = std::move(snap);
        ++projectGen_;
    }
    // While paused, show the edit immediately; while playing the loop picks it up.
    if (!playing_) requestFrame(playhead());
    std::lock_guard lk(aMu_);
    if (playing_) {
        // Re-render audio from the current position so edits are heard at once.
        const std::int64_t pos = playStartSample_ + playedSamples_;
        ringRead_ = ringFill_ = 0;
        nextRenderSample_ = pos;
        ++audioGen_;
        aCv_.notify_all();
    }
}

void PreviewEngine::setProjectDir(const std::string& dir) {
    std::lock_guard lk(snapMu_);
    projectDir_ = dir;
    ++projectGen_;
}

void PreviewEngine::setProxy(const Id& assetId, const std::string& path) {
    std::lock_guard lk(snapMu_);
    pendingProxies_.emplace_back(assetId, path);
    ++projectGen_;
}

std::shared_ptr<const Project> PreviewEngine::snapshot() const {
    std::lock_guard lk(snapMu_);
    return project_;
}

void PreviewEngine::setFrameCallback(FrameCallback cb) {
    std::lock_guard lk(vMu_);
    onFrame_ = std::move(cb);
}

void PreviewEngine::setPreviewSize(int w, int h) {
    std::lock_guard lk(vMu_);
    cfg_.width = std::max(16, w);
    cfg_.height = std::max(16, h);
}

void PreviewEngine::requestFrame(Time t) {
    {
        std::lock_guard lk(vMu_);
        requested_ = t;
        pausedAt_ = playing_ ? pausedAt_ : t;
    }
    vCv_.notify_one();
}

void PreviewEngine::play(Time from) {
    auto snap = snapshot();
    const int sr = snap ? snap->sampleRate : 48000;
    {
        std::lock_guard lk(aMu_);
        ringRead_ = ringFill_ = 0;
        playStartSample_ = nextRenderSample_ = from.toSample(sr);
        playedSamples_ = 0;
        ++audioGen_;
    }
    {
        std::lock_guard lk(vMu_);
        pausedAt_ = from;
    }
    wallStart_ = Clock::now().time_since_epoch().count();
    audioDriven_ = false;
    playing_ = true;
    aCv_.notify_all();
    vCv_.notify_all();
}

void PreviewEngine::pause() {
    const Time at = playhead();
    playing_ = false;
    {
        std::lock_guard lk(vMu_);
        pausedAt_ = at;
    }
    requestFrame(at);
}

Time PreviewEngine::playhead() const {
    if (!playing_) {
        std::lock_guard lk(vMu_);
        return pausedAt_;
    }
    auto snap = snapshot();
    const int sr = snap ? snap->sampleRate : 48000;
    std::int64_t start, played;
    {
        std::lock_guard lk(aMu_);
        start = playStartSample_;
        played = playedSamples_;
    }
    if (audioDriven_) return Time::fromSample(start + played, sr);
    // No audio device attached: fall back to the wall clock.
    const auto now = Clock::now().time_since_epoch().count();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::duration(now - wallStart_.load()));
    return Time::fromSample(start, sr) + Time::fromMilliseconds(elapsed.count());
}

void PreviewEngine::pullAudio(float* out, std::int64_t frames) {
    auto snap = snapshot();
    const int ch = snap ? snap->audioChannels : 2;
    const std::size_t want = static_cast<std::size_t>(frames) * ch;
    if (!playing_) {
        std::memset(out, 0, want * sizeof(float));
        return;
    }
    std::size_t got = 0;
    {
        std::lock_guard lk(aMu_);
        audioDriven_ = true;
        const std::size_t cap = ring_.size();
        got = std::min(want, ringFill_);
        for (std::size_t i = 0; i < got; ++i) out[i] = ring_[(ringRead_ + i) % cap];
        if (cap) ringRead_ = (ringRead_ + got) % cap;
        ringFill_ -= got;
        playedSamples_ += frames;  // the device clock advances even on underrun
    }
    if (got < want) {
        std::memset(out + got, 0, (want - got) * sizeof(float));
        ++underruns_;
    }
    aCv_.notify_one();
}

void PreviewEngine::videoLoop() {
    MediaPool pool(backend_, {});
    std::uint64_t seenGen = ~0ull;
    std::int64_t lastFrame = -1, prefetchedTo = -1;
    std::shared_ptr<const Project> snap;
    while (!stop_) {
        std::optional<Time> target;
        int w, h;
        FrameCallback cb;
        {
            std::unique_lock lk(vMu_);
            if (!requested_ && !playing_)
                vCv_.wait(lk, [&] { return stop_ || requested_ || playing_; });
            if (stop_) return;
            target = requested_;
            requested_.reset();
            w = cfg_.width;
            h = cfg_.height;
            cb = onFrame_;
        }
        {
            std::lock_guard lk(snapMu_);
            snap = project_;
            if (seenGen != projectGen_) {
                seenGen = projectGen_;
                pool.setProjectDir(projectDir_);
                for (auto& [id, path] : pendingProxies_) pool.setProxy(id, path);
                lastFrame = prefetchedTo = -1;  // edit: re-composite (sources stay cached)
            }
        }
        if (!snap) continue;
        // Render at the canvas shape inside the requested box: a frame the shape of the view would put
        // text, stickers and subtitles (placed in canvas units) off their spot, and costs more pixels.
        if (snap->canvas.width > 0 && snap->canvas.height > 0) {
            const double cw = snap->canvas.width, ch = snap->canvas.height;
            const double k = std::min(w / cw, h / ch);
            w = std::max(16, static_cast<int>(std::lround(cw * k)) & ~1);
            h = std::max(16, static_cast<int>(std::lround(ch * k)) & ~1);
        }
        const Rational fps = snap->fps;
        if (!target && playing_) {
            const std::int64_t f = playhead().toFrame(fps);
            if (f == lastFrame) {
                // Idle until the next frame is due; prefetch meanwhile.
                if (prefetchedTo < f + cfg_.prefetchFrames) {
                    const std::int64_t next = std::max(prefetchedTo, f) + 1;
                    Renderer r(*snap, pool, &cache_, true, modules_);
                    (void)r.renderVideo(Time::fromFrame(next, fps), w, h, "preview");
                    prefetchedTo = next;
                } else {
                    std::unique_lock lk(vMu_);
                    vCv_.wait_for(lk, std::chrono::milliseconds(2));
                }
                continue;
            }
            target = Time::fromFrame(f, fps);
        }
        if (!target) continue;
        Renderer r(*snap, pool, &cache_, true, modules_);
        auto frame = std::make_shared<VideoFrame>(r.renderVideo(*target, w, h, "preview"));
        lastFrame = target->toFrame(fps);
        prefetchedTo = std::max(prefetchedTo, lastFrame);
        ++framesRendered_;
        if (cb) cb(std::move(frame));
    }
}

void PreviewEngine::audioLoop() {
    MediaPool pool(backend_, {});
    std::uint64_t seenGen = ~0ull;
    constexpr std::int64_t kChunk = 1024;
    while (!stop_) {
        std::uint64_t gen;
        std::int64_t start, n;
        std::shared_ptr<const Project> snap = snapshot();
        if (!snap) {
            std::unique_lock lk(aMu_);
            aCv_.wait_for(lk, std::chrono::milliseconds(20));
            continue;
        }
        {
            std::lock_guard lk(snapMu_);
            if (seenGen != projectGen_) {
                seenGen = projectGen_;
                pool.setProjectDir(projectDir_);
            }
        }
        const int ch = snap->audioChannels;
        const std::size_t cap = static_cast<std::size_t>(cfg_.audioBufferSeconds * snap->sampleRate) * ch;
        {
            std::unique_lock lk(aMu_);
            if (ring_.size() != cap) {
                ring_.assign(cap, 0.0f);
                ringRead_ = ringFill_ = 0;
            }
            aCv_.wait(lk, [&] { return stop_ || (playing_ && ringFill_ + kChunk * ch <= cap); });
            if (stop_) return;
            gen = audioGen_;
            start = nextRenderSample_;
            n = kChunk;
        }
        Renderer r(*snap, pool, nullptr, false);
        AudioBuffer buf = r.renderAudio(start, n, "audio");
        std::lock_guard lk(aMu_);
        if (gen != audioGen_ || ring_.size() != cap) continue;  // seek/edit happened meanwhile
        const std::size_t count = buf.samples.size();
        const std::size_t writeAt = (ringRead_ + ringFill_) % cap;
        for (std::size_t i = 0; i < count; ++i) ring_[(writeAt + i) % cap] = buf.samples[i];
        ringFill_ += count;
        nextRenderSample_ += n;
    }
}

}  // namespace vstudio
