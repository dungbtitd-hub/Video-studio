#include "vstudio/stills.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace vstudio {

const std::vector<std::string>& cameraRawExtensions() {
    static const std::vector<std::string> ext{
        ".dng", ".cr2", ".cr3", ".crw", ".nef", ".nrw", ".arw", ".srf", ".sr2", ".raf", ".orf", ".rw2",
        ".pef", ".srw", ".x3f", ".3fr", ".fff", ".iiq", ".erf", ".mef", ".mos", ".mrw", ".kdc", ".dcr", ".rwl",
    };
    return ext;
}

bool isCameraRawPath(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || path.find_first_of("/\\", dot) != std::string::npos) return false;
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto& all = cameraRawExtensions();
    return std::find(all.begin(), all.end(), e) != all.end();
}

namespace {

// Still modules in priority order, created on first use.
class StillSet {
public:
    explicit StillSet(ModuleRegistry& m) : modules_(m) {}

    // The first module that reads `path`, with its probe.
    std::shared_ptr<IStillDecoder> pick(const std::string& path, ProbeResult* pr, std::string* error) {
        std::string errs;
        for (const auto& info : ordered()) {
            std::string e;
            auto d = modules_.get<IStillDecoder>(info.id, &e);
            if (!d) {
                errs += (errs.empty() ? "" : "; ") + info.id + ": " + e;
                continue;
            }
            if (auto p = d->probe(path, &e)) {
                *pr = *p;
                return d;
            }
            errs += (errs.empty() ? "" : "; ") + d->name() + ": " + e;
        }
        if (error) *error = errs.empty() ? "no camera RAW decoder available" : "cannot read RAW photo (" + errs + ")";
        return nullptr;
    }

private:
    std::vector<ModuleInfo> ordered() const {
        auto v = modules_.list(IStillDecoder::kModuleKind);
        const std::string plat = ModuleRegistry::currentPlatform();
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const ModuleInfo& i) {
                                   return !i.platforms.empty() &&
                                          std::find(i.platforms.begin(), i.platforms.end(), plat) == i.platforms.end();
                               }),
                v.end());
        std::stable_sort(v.begin(), v.end(), [](const ModuleInfo& a, const ModuleInfo& b) { return a.priority > b.priority; });
        return v;
    }

    ModuleRegistry& modules_;
};

// IMediaDecoder over a still module: the picture is developed once (at the asked size and
// depth) and the same frame is returned for every time.
class StillDecoder final : public IMediaDecoder {
public:
    explicit StillDecoder(std::shared_ptr<StillSet> set) : set_(std::move(set)) {}

    bool open(const std::string& path, std::string* error) override {
        path_ = path;
        ProbeResult pr;
        dec_ = set_->pick(path, &pr, error);
        return dec_ != nullptr;
    }
    void setMaxOutputSize(int w, int h) override {
        maxW_ = w;
        maxH_ = h;
        frame_.reset();
    }
    void setDeepOutput(bool on) override {
        deep_ = on;
        frame_.reset();
    }
    std::optional<VideoFrame> videoFrameAt(Time t) override {
        if (!dec_) return std::nullopt;
        if (!frame_) {
            std::string err;
            frame_ = dec_->decode(path_, maxW_, maxH_, deep_, &err);
            if (!frame_) return std::nullopt;
        }
        VideoFrame f = *frame_;
        f.pts = t;
        return f;
    }
    AudioBuffer readAudio(Time, std::int64_t frames, int sampleRate, int channels) override {
        AudioBuffer a;
        a.sampleRate = sampleRate;
        a.channels = channels;
        a.samples.assign(static_cast<std::size_t>(std::max<std::int64_t>(0, frames)) * channels, 0.0f);
        return a;
    }

private:
    std::shared_ptr<StillSet> set_;
    std::shared_ptr<IStillDecoder> dec_;
    std::string path_;
    int maxW_ = 0, maxH_ = 0;
    bool deep_ = false;
    std::optional<VideoFrame> frame_;
};

// Decoder that picks the RAW path or the backend's decoder when the file is opened.
class RoutingDecoder final : public IMediaDecoder {
public:
    RoutingDecoder(std::shared_ptr<IMediaBackend> primary, std::shared_ptr<StillSet> stills)
        : primary_(std::move(primary)), stills_(std::move(stills)) {}

    bool open(const std::string& path, std::string* error) override {
        if (isCameraRawPath(path)) inner_ = std::make_unique<StillDecoder>(stills_);
        else inner_ = primary_->createDecoder();
        if (!inner_) {
            if (error) *error = "no decoder";
            return false;
        }
        inner_->setMaxOutputSize(maxW_, maxH_);
        inner_->setDeepOutput(deep_);
        return inner_->open(path, error);
    }
    void setMaxOutputSize(int w, int h) override {
        maxW_ = w;
        maxH_ = h;
        if (inner_) inner_->setMaxOutputSize(w, h);
    }
    void setDeepOutput(bool on) override {
        deep_ = on;
        if (inner_) inner_->setDeepOutput(on);
    }
    std::optional<VideoFrame> videoFrameAt(Time t) override { return inner_ ? inner_->videoFrameAt(t) : std::nullopt; }
    AudioBuffer readAudio(Time t, std::int64_t frames, int sampleRate, int channels) override {
        if (inner_) return inner_->readAudio(t, frames, sampleRate, channels);
        AudioBuffer a;
        a.sampleRate = sampleRate;
        a.channels = channels;
        a.samples.assign(static_cast<std::size_t>(std::max<std::int64_t>(0, frames)) * channels, 0.0f);
        return a;
    }

private:
    std::shared_ptr<IMediaBackend> primary_;
    std::shared_ptr<StillSet> stills_;
    std::unique_ptr<IMediaDecoder> inner_;
    int maxW_ = 0, maxH_ = 0;
    bool deep_ = false;
};

class RoutingBackend final : public IMediaBackend {
public:
    RoutingBackend(std::shared_ptr<IMediaBackend> primary, ModuleRegistry& m)
        : primary_(std::move(primary)), stills_(std::make_shared<StillSet>(m)) {}

    std::string name() const override { return primary_->name(); }
    std::optional<ProbeResult> probe(const std::string& path, std::string* error) override {
        if (!isCameraRawPath(path)) return primary_->probe(path, error);
        ProbeResult pr;
        if (!stills_->pick(path, &pr, error)) return std::nullopt;
        return pr;
    }
    std::unique_ptr<IMediaDecoder> createDecoder() override {
        return std::make_unique<RoutingDecoder>(primary_, stills_);
    }
    std::unique_ptr<IMediaEncoder> createEncoder() override { return primary_->createEncoder(); }

private:
    std::shared_ptr<IMediaBackend> primary_;
    std::shared_ptr<StillSet> stills_;
};

}  // namespace

std::shared_ptr<IMediaBackend> withStillDecoders(std::shared_ptr<IMediaBackend> primary, ModuleRegistry& modules) {
    if (!primary || modules.list(IStillDecoder::kModuleKind).empty()) return primary;
    return std::make_shared<RoutingBackend>(std::move(primary), modules);
}

}  // namespace vstudio
