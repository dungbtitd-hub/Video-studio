#include "vstudio/render.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "detail/paths.hpp"

namespace vstudio {

namespace fs = std::filesystem;

std::vector<VideoLayer> evaluateVideo(const Project& p, Time t) {
    std::vector<VideoLayer> out;
    for (std::size_t i = 0; i < p.tracks.size(); ++i) {
        const Track& tr = p.tracks[i];
        if (tr.type != TrackType::Video || !tr.enabled) continue;
        const Clip* c = tr.clipAt(t);
        if (!c) continue;
        const MediaAsset* a = p.findAsset(c->assetId);
        if (!a) continue;
        out.push_back({c, a, c->toSource(t), i});
    }
    return out;
}

Transform Transform::fromJson(const Json& j) {
    Transform t;
    if (!j.is_object()) return t;
    t.scale = j.value("scale", 1.0);
    t.x = j.value("x", 0.0);
    t.y = j.value("y", 0.0);
    t.opacity = std::clamp(j.value("opacity", 1.0), 0.0, 1.0);
    return t;
}

namespace {

// Blends one straight-alpha pixel `c` (alpha already multiplied by opacity: a in 0..255) over `d`.
inline void blendPixel(std::uint8_t* d, const std::uint8_t* c, unsigned a) {
    if (a == 255) {
        d[0] = c[0];
        d[1] = c[1];
        d[2] = c[2];
        d[3] = 255;
        return;
    }
    if (a == 0) return;
    if (d[3] == 255) {  // common case: opaque canvas
        const unsigned ia = 255 - a;
        for (int k = 0; k < 3; ++k) d[k] = static_cast<std::uint8_t>((c[k] * a + d[k] * ia + 127) / 255);
        return;
    }
    const unsigned da = d[3] * (255 - a) / 255;
    const unsigned oa = a + da;
    for (int k = 0; k < 3; ++k) d[k] = static_cast<std::uint8_t>(oa ? (c[k] * a + d[k] * da) / oa : 0);
    d[3] = static_cast<std::uint8_t>(oa);
}

}  // namespace

void compositeOver(VideoFrame& dst, const VideoFrame& src, const Transform& tr) {
    if (src.empty() || dst.empty() || tr.opacity <= 0.0 || tr.scale <= 0.0) return;
    // Fit (contain) then scale, centred, then offset.
    const double fit = std::min(static_cast<double>(dst.width) / src.width, static_cast<double>(dst.height) / src.height);
    const double s = fit * tr.scale;
    const double w = src.width * s, h = src.height * s;
    const double left = (dst.width - w) / 2.0 + tr.x;
    const double top = (dst.height - h) / 2.0 + tr.y;
    const unsigned opacity = static_cast<unsigned>(std::lround(std::clamp(tr.opacity, 0.0, 1.0) * 255.0));
    const int sw = src.width, sh = src.height;
    const std::uint8_t* sp = src.rgba.data();

    // Fast path: the decoder already produced the exact output size (preview
    // decodes to the preview box), so no resampling is needed - copy/blend rows.
    const int ileft = static_cast<int>(std::lround(left)), itop = static_cast<int>(std::lround(top));
    if (std::abs(s - 1.0) < 1e-9 && std::abs(left - ileft) < 1e-6 && std::abs(top - itop) < 1e-6) {
        const int x0 = std::max(0, ileft), x1 = std::min(dst.width, ileft + sw);
        const int y0 = std::max(0, itop), y1 = std::min(dst.height, itop + sh);
        if (x1 <= x0) return;
        for (int y = y0; y < y1; ++y) {
            const std::uint8_t* srow = sp + (static_cast<std::size_t>(y - itop) * sw + (x0 - ileft)) * 4;
            std::uint8_t* drow = dst.rgba.data() + (static_cast<std::size_t>(y) * dst.width + x0) * 4;
            if (opacity == 255) {
                bool opaque = true;
                for (int x = 0; x < x1 - x0 && opaque; ++x) opaque = srow[x * 4 + 3] == 255;
                if (opaque) {
                    std::memcpy(drow, srow, static_cast<std::size_t>(x1 - x0) * 4);
                    continue;
                }
            }
            for (int x = 0; x < x1 - x0; ++x) blendPixel(drow + x * 4, srow + x * 4, srow[x * 4 + 3] * opacity / 255);
        }
        return;
    }

    // General path: bilinear resampling in 8.8 fixed point with per-column tables.
    const int x0 = std::max(0, static_cast<int>(std::floor(left)));
    const int y0 = std::max(0, static_cast<int>(std::floor(top)));
    const int x1 = std::min(dst.width, static_cast<int>(std::ceil(left + w)));
    const int y1 = std::min(dst.height, static_cast<int>(std::ceil(top + h)));
    if (x1 <= x0 || y1 <= y0) return;
    const double inv = 1.0 / s;
    struct Col {
        int i0, i1;
        unsigned f;  // 0..256
        bool inside;
    };
    std::vector<Col> cols(static_cast<std::size_t>(x1 - x0));
    for (int x = x0; x < x1; ++x) {
        const double sx = (x + 0.5 - left) * inv - 0.5;
        Col& c = cols[static_cast<std::size_t>(x - x0)];
        c.inside = sx >= -0.5 && sx <= sw - 0.5;
        const int ix = std::clamp(static_cast<int>(std::floor(sx)), 0, sw - 1);
        c.i0 = ix * 4;
        c.i1 = std::min(ix + 1, sw - 1) * 4;
        c.f = static_cast<unsigned>(std::lround(std::clamp(sx - ix, 0.0, 1.0) * 256));
    }
    for (int y = y0; y < y1; ++y) {
        const double sy = (y + 0.5 - top) * inv - 0.5;
        if (sy < -0.5 || sy > sh - 0.5) continue;
        const int iy = std::clamp(static_cast<int>(std::floor(sy)), 0, sh - 1);
        const int iy1 = std::min(iy + 1, sh - 1);
        const unsigned fy = static_cast<unsigned>(std::lround(std::clamp(sy - iy, 0.0, 1.0) * 256));
        const std::uint8_t* r0 = sp + static_cast<std::size_t>(iy) * sw * 4;
        const std::uint8_t* r1 = sp + static_cast<std::size_t>(iy1) * sw * 4;
        std::uint8_t* drow = dst.rgba.data() + static_cast<std::size_t>(y) * dst.width * 4;
        for (int x = x0; x < x1; ++x) {
            const Col& c = cols[static_cast<std::size_t>(x - x0)];
            if (!c.inside) continue;
            std::uint8_t px[4];
            for (int k = 0; k < 4; ++k) {
                const unsigned t = r0[c.i0 + k] * (256 - c.f) + r0[c.i1 + k] * c.f;
                const unsigned b = r1[c.i0 + k] * (256 - c.f) + r1[c.i1 + k] * c.f;
                px[k] = static_cast<std::uint8_t>((t * (256 - fy) + b * fy + 32768) >> 16);
            }
            blendPixel(drow + static_cast<std::size_t>(x) * 4, px, px[3] * opacity / 255);
        }
    }
}

MediaPool::MediaPool(std::shared_ptr<IMediaBackend> backend, std::string projectDir)
    : backend_(std::move(backend)), projectDir_(std::move(projectDir)) {}

std::string MediaPool::resolve(const MediaAsset& a) const {
    fs::path p = detail::toPath(a.uri);
    if (p.is_relative() && !projectDir_.empty()) p = detail::toPath(projectDir_) / p;
    return detail::fromPath(p.lexically_normal());
}

void MediaPool::setProxy(const Id& assetId, std::string path) {
    proxies_[assetId] = std::move(path);
    // Re-open preview decoders on next use so they pick the proxy up.
    std::erase_if(decoders_, [&](const auto& kv) { return kv.first.rfind(assetId + "#", 0) == 0 && kv.first.find("#proxy") != std::string::npos; });
}

IMediaDecoder* MediaPool::decoder(const MediaAsset& a, const std::string& purpose, std::string* error, int maxW,
                                  int maxH, bool allowProxy) {
    const auto proxy = allowProxy ? proxies_.find(a.id) : proxies_.end();
    const bool useProxy = proxy != proxies_.end();
    const std::string key = a.id + "#" + purpose + "#" + std::to_string(maxW) + "x" + std::to_string(maxH) +
                            (useProxy ? "#proxy" : "");
    if (auto it = decoders_.find(key); it != decoders_.end()) return it->second.get();
    if (!backend_) {
        if (error) *error = "no media backend";
        return nullptr;
    }
    auto d = backend_->createDecoder();
    if (!d) return nullptr;
    d->setMaxOutputSize(maxW, maxH);
    if (!d->open(useProxy ? proxy->second : resolve(a), error)) return nullptr;
    return (decoders_[key] = std::move(d)).get();
}

std::shared_ptr<const VideoFrame> Renderer::sourceFrame(const VideoLayer& layer, int outW, int outH,
                                                        const std::string& purpose) {
    const MediaAsset& a = *layer.asset;
    const bool still = a.type == MediaType::Image;
    const Time src = still ? Time::zero() : layer.sourceTime;
    // Decode size: preview decodes at output size, export at native size.
    const int dw = preview_ ? outW : 0, dh = preview_ ? outH : 0;
    std::string key;
    if (cache_) {
        const Rational fps = a.fps.num > 0 ? a.fps : project_.fps;
        key = FrameCache::key(a.id, src.toFrame(fps), dw, dh);
        if (auto hit = cache_->get(key)) return hit;
    }
    std::string err;
    IMediaDecoder* d = pool_.decoder(a, purpose, &err, dw, dh, preview_);
    if (!d) {
        warn("media offline: " + a.uri + (err.empty() ? "" : " (" + err + ")"));
        return nullptr;
    }
    auto frame = d->videoFrameAt(src);
    if (!frame) return nullptr;
    auto shared = std::make_shared<const VideoFrame>(std::move(*frame));
    if (cache_) cache_->put(key, shared);
    return shared;
}

void Renderer::warn(std::string w) {
    // Deduplicated and bounded: a long preview session must not grow memory.
    if (warnings_.size() >= 64 || std::find(warnings_.begin(), warnings_.end(), w) != warnings_.end()) return;
    warnings_.push_back(std::move(w));
}

namespace {

class CpuCompositor final : public ICompositor {
public:
    std::string name() const override { return "builtin.cpu_compositor"; }
    bool composite(VideoFrame& out, const std::vector<CompositeLayer>& layers, std::string*) override {
        std::fill(out.rgba.begin(), out.rgba.end(), std::uint8_t{0});
        for (std::size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;  // opaque black
        for (const auto& l : layers)
            if (l.frame) compositeOver(out, *l.frame, l.transform);
        return true;
    }
};

}  // namespace

std::shared_ptr<ICompositor> createCpuCompositor() { return std::make_shared<CpuCompositor>(); }

ICompositor& Renderer::compositor() {
    if (!compositor_ && modules_) compositor_ = modules_->best<ICompositor>("composite");
    if (!compositor_) {
        static const std::shared_ptr<ICompositor> cpu = createCpuCompositor();
        compositor_ = cpu;
    }
    return *compositor_;
}

VideoFrame Renderer::renderVideo(Time t, int width, int height, const std::string& purpose) {
    VideoFrame out;
    out.allocate(width > 0 ? width : project_.canvas.width, height > 0 ? height : project_.canvas.height);
    out.pts = t;
    std::vector<CompositeLayer> layers;
    for (const auto& layer : evaluateVideo(project_, t)) {
        auto frame = sourceFrame(layer, out.width, out.height, purpose);
        if (!frame) continue;
        if (modules_ && layer.clip->effects.is_array() && !layer.clip->effects.empty()) {
            // Effects work on a copy: cached source frames stay untouched.
            auto fx = std::make_shared<VideoFrame>(*frame);
            std::vector<std::string> w;
            applyClipEffects(*modules_, *layer.clip, *fx, layer.sourceTime - layer.clip->sourceIn, &w);
            for (auto& m : w) warn(std::move(m));
            frame = std::move(fx);
        }
        Transform tr = Transform::fromJson(layer.clip->transform);
        // Scale offsets from canvas pixels to output pixels (preview may be smaller).
        const double k = static_cast<double>(out.width) / project_.canvas.width;
        tr.x *= k;
        tr.y *= k;
        layers.push_back({std::move(frame), tr});
    }
    std::string err;
    if (!compositor().composite(out, layers, &err)) {
        // GPU lost or failed: this renderer continues on the CPU reference.
        warn("compositor " + compositor_->name() + " failed, using CPU" + (err.empty() ? "" : ": " + err));
        compositor_ = createCpuCompositor();
        compositor_->composite(out, layers, nullptr);
    }
    return out;
}

AudioBuffer Renderer::renderAudio(std::int64_t startSample, std::int64_t frames, const std::string& purpose) {
    const int sr = project_.sampleRate, ch = project_.audioChannels;
    AudioBuffer mix;
    mix.sampleRate = sr;
    mix.channels = ch;
    mix.samples.assign(static_cast<std::size_t>(frames) * ch, 0.0f);
    const std::int64_t endSample = startSample + frames;
    for (const Track& tr : project_.tracks) {
        if (!tr.enabled || tr.muted) continue;
        for (const Clip& c : tr.clips) {
            const MediaAsset* a = project_.findAsset(c.assetId);
            if (!a || !a->audio) continue;
            const std::int64_t cs = c.timelineIn.toSample(sr, Rounding::Up);
            const std::int64_t ce = c.timelineOut().toSample(sr, Rounding::Up);
            const std::int64_t s0 = std::max(cs, startSample), s1 = std::min(ce, endSample);
            if (s1 <= s0) continue;
            std::string err;
            IMediaDecoder* d = pool_.decoder(*a, purpose, &err);
            if (!d) continue;
            const Time src = c.toSource(Time::fromSample(s0, sr));
            const std::int64_t n = s1 - s0;
            const bool unity = c.speed == Rational{1, 1};
            // For speed != 1 read the corresponding source span and resample linearly (no pitch correction in P0).
            const std::int64_t need = unity ? n : std::max<std::int64_t>(1, mulDiv(n, c.speed.num, c.speed.den, Rounding::Up));
            AudioBuffer in = d->readAudio(src, need, sr, ch);
            const float gain = static_cast<float>(c.volume * tr.volume);
            const std::int64_t inFrames = in.frames();
            if (inFrames == 0) continue;
            for (std::int64_t i = 0; i < n; ++i) {
                float* o = &mix.samples[static_cast<std::size_t>(s0 - startSample + i) * ch];
                if (unity) {
                    if (i >= inFrames) break;
                    const float* s = &in.samples[static_cast<std::size_t>(i) * ch];
                    for (int k = 0; k < ch; ++k) o[k] += s[k] * gain;
                } else {
                    const double pos = static_cast<double>(i) * c.speed.num / c.speed.den;
                    const std::int64_t j = std::min<std::int64_t>(static_cast<std::int64_t>(pos), inFrames - 1);
                    const std::int64_t j1 = std::min<std::int64_t>(j + 1, inFrames - 1);
                    const float f = static_cast<float>(pos - j);
                    for (int k = 0; k < ch; ++k) {
                        const float a0 = in.samples[static_cast<std::size_t>(j) * ch + k];
                        const float a1 = in.samples[static_cast<std::size_t>(j1) * ch + k];
                        o[k] += (a0 + (a1 - a0) * f) * gain;
                    }
                }
            }
        }
    }
    for (float& s : mix.samples) s = std::clamp(s, -1.0f, 1.0f);
    return mix;
}

ExportResult exportProject(const Project& p, MediaPool& pool, IMediaEncoder& enc, const std::string& outPath,
                           const ExportSettings& s, const std::function<void(const ExportProgress&)>& onProgress,
                           const std::atomic<bool>* cancel, ModuleRegistry* modules) {
    ExportResult r;
    const auto t0 = std::chrono::steady_clock::now();
    if (p.duration() <= Time::zero()) {
        r.error = "timeline is empty";
        return r;
    }
    // Export always renders at the export rate; the project audio rate must match the encoder input.
    Project tmp = p;
    tmp.sampleRate = s.sampleRate;
    tmp.audioChannels = s.channels;
    Renderer renderer(tmp, pool, nullptr, false, modules);
    if (!enc.open(outPath, s, &r.error)) return r;
    const std::int64_t total = p.duration().toFrame(s.fps, Rounding::Up);
    for (std::int64_t i = 0; i < total; ++i) {
        if (cancel && cancel->load()) {
            r.cancelled = true;
            r.error = "cancelled";
            enc.finish(nullptr);
            return r;
        }
        const Time t = Time::fromFrame(i, s.fps);
        VideoFrame f = renderer.renderVideo(t, s.width, s.height, "export");
        f.pts = t;
        if (!enc.writeVideo(f, &r.error)) return r;
        // Sample range derived from frame boundaries: cumulative, so no drift.
        const std::int64_t a0 = t.toSample(s.sampleRate);
        const std::int64_t a1 = Time::fromFrame(i + 1, s.fps).toSample(s.sampleRate);
        if (a1 > a0 && !enc.writeAudio(renderer.renderAudio(a0, a1 - a0, "export-audio"), &r.error)) return r;
        ++r.framesWritten;
        if (onProgress) onProgress({i + 1, total});
    }
    if (!enc.finish(&r.error)) return r;
    r.ok = true;
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

}  // namespace vstudio
