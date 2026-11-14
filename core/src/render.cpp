#include "vstudio/render.hpp"
#include "vstudio/audio_fx.hpp"

#include "vstudio/grade.hpp"
#include "vstudio/looks.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "detail/paths.hpp"
#include "vstudio/generators.hpp"
#include "vstudio/keyframes.hpp"
#include "vstudio/pixel.hpp"
#include "vstudio/transitions.hpp"

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

LayerRect layerRect(int srcW, int srcH, int dstW, int dstH, const Transform& tr) {
    // Fit (contain) then scale, centred, then offset.
    const double fit = std::min(static_cast<double>(dstW) / srcW, static_cast<double>(dstH) / srcH);
    const double s = fit * tr.scale;
    auto snap = [](double v) { return std::round(v * 256.0) / 256.0; };
    LayerRect r;
    r.width = snap(srcW * s);
    r.height = snap(srcH * s);
    r.left = snap((dstW - r.width) / 2.0 + tr.x);
    r.top = snap((dstH - r.height) / 2.0 + tr.y);
    r.unscaled = r.width == srcW && r.height == srcH && r.left == std::floor(r.left) && r.top == std::floor(r.top);
    return r;
}

void compositeOver(VideoFrame& dst, const VideoFrame& src, const Transform& tr) {
    if (dst.deep()) return compositeOverDeep(dst, src, tr);
    if (src.deep()) return compositeOver(dst, convertedFrame(src, PixelFormat::RGBA8), tr);
    if (src.empty() || dst.empty() || tr.opacity <= 0.0 || tr.scale <= 0.0) return;
    // Fit (contain) then scale, centred, then offset.
    const LayerRect r = layerRect(src.width, src.height, dst.width, dst.height, tr);
    const double w = r.width, h = r.height, left = r.left, top = r.top;
    const unsigned opacity = static_cast<unsigned>(std::lround(std::clamp(tr.opacity, 0.0, 1.0) * 255.0));
    const int sw = src.width, sh = src.height;
    const std::uint8_t* sp = src.rgba.data();

    // Fast path: the decoder already produced the exact output size (preview
    // decodes to the preview box), so no resampling is needed - copy/blend rows.
    const int ileft = static_cast<int>(std::lround(left)), itop = static_cast<int>(std::lround(top));
    if (r.unscaled) {
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
    const double invX = src.width / w, invY = src.height / h;
    struct Col {
        int i0, i1;
        unsigned f;  // 0..256
        bool inside;
    };
    std::vector<Col> cols(static_cast<std::size_t>(x1 - x0));
    for (int x = x0; x < x1; ++x) {
        const double sx = (x + 0.5 - left) * invX - 0.5;
        Col& c = cols[static_cast<std::size_t>(x - x0)];
        // Half-open coverage by pixel centre: [left, left + w). Shared with the GPU compositors.
        c.inside = x + 0.5 >= left && x + 0.5 < left + w;
        const int ix = std::clamp(static_cast<int>(std::floor(sx)), 0, sw - 1);
        c.i0 = ix * 4;
        c.i1 = std::min(ix + 1, sw - 1) * 4;
        c.f = static_cast<unsigned>(std::lround(std::clamp(sx - ix, 0.0, 1.0) * 256));
    }
    for (int y = y0; y < y1; ++y) {
        if (y + 0.5 < top || y + 0.5 >= top + h) continue;
        const double sy = (y + 0.5 - top) * invY - 0.5;
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

namespace {

// Straight-alpha RGBA as floats, from either pixel format.
struct FloatReader {
    const VideoFrame& f;
    void at(int x, int y, float out[4]) const {
        const std::size_t i = (static_cast<std::size_t>(y) * f.width + x) * 4;
        if (f.deep()) {
            for (int k = 0; k < 4; ++k) out[k] = halfToFloat(f.half[i + k]);
        } else {
            for (int k = 0; k < 4; ++k) out[k] = f.rgba[i + k] * (1.0f / 255.0f);
        }
    }
};

}  // namespace

void compositeOverDeep(VideoFrame& dst, const VideoFrame& src, const Transform& tr) {
    if (src.empty() || dst.empty() || !dst.deep() || tr.opacity <= 0.0 || tr.scale <= 0.0) return;
    const LayerRect r = layerRect(src.width, src.height, dst.width, dst.height, tr);
    const float opacity = static_cast<float>(std::clamp(tr.opacity, 0.0, 1.0));
    const FloatReader rd{src};
    const int x0 = std::max(0, static_cast<int>(std::floor(r.left)));
    const int y0 = std::max(0, static_cast<int>(std::floor(r.top)));
    const int x1 = std::min(dst.width, static_cast<int>(std::ceil(r.left + r.width)));
    const int y1 = std::min(dst.height, static_cast<int>(std::ceil(r.top + r.height)));
    const double invX = src.width / r.width, invY = src.height / r.height;
    for (int y = y0; y < y1; ++y) {
        if (y + 0.5 < r.top || y + 0.5 >= r.top + r.height) continue;
        const double sy = (y + 0.5 - r.top) * invY - 0.5;
        const int iy = std::clamp(static_cast<int>(std::floor(sy)), 0, src.height - 1);
        const int iy1 = std::min(iy + 1, src.height - 1);
        const float fy = r.unscaled ? 0.0f : static_cast<float>(std::clamp(sy - iy, 0.0, 1.0));
        for (int x = x0; x < x1; ++x) {
            if (x + 0.5 < r.left || x + 0.5 >= r.left + r.width) continue;
            const double sx = (x + 0.5 - r.left) * invX - 0.5;
            const int ix = std::clamp(static_cast<int>(std::floor(sx)), 0, src.width - 1);
            const int ix1 = std::min(ix + 1, src.width - 1);
            const float fx = r.unscaled ? 0.0f : static_cast<float>(std::clamp(sx - ix, 0.0, 1.0));
            float a[4], b[4], c[4], d[4], px[4];
            rd.at(ix, iy, a);
            rd.at(ix1, iy, b);
            rd.at(ix, iy1, c);
            rd.at(ix1, iy1, d);
            for (int k = 0; k < 4; ++k) {
                const float top = a[k] + (b[k] - a[k]) * fx, bot = c[k] + (d[k] - c[k]) * fx;
                px[k] = top + (bot - top) * fy;
            }
            const float sa = std::clamp(px[3], 0.0f, 1.0f) * opacity;
            if (sa <= 0.0f) continue;
            std::uint16_t* o = dst.half.data() + (static_cast<std::size_t>(y) * dst.width + x) * 4;
            const float da = halfToFloat(o[3]) * (1.0f - sa), oa = sa + da;
            for (int k = 0; k < 3; ++k)
                o[k] = floatToHalf(oa > 0 ? (px[k] * sa + halfToFloat(o[k]) * da) / oa : 0.0f);
            o[3] = floatToHalf(oa);
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
                                  int maxH, bool allowProxy, bool deep) {
    const auto proxy = allowProxy ? proxies_.find(a.id) : proxies_.end();
    const bool useProxy = proxy != proxies_.end();
    const std::string key = a.id + "#" + purpose + "#" + std::to_string(maxW) + "x" + std::to_string(maxH) +
                            (useProxy ? "#proxy" : "") + (deep ? "#deep" : "");
    if (auto it = decoders_.find(key); it != decoders_.end()) return it->second.get();
    if (!backend_) {
        if (error) *error = "no media backend";
        return nullptr;
    }
    auto d = backend_->createDecoder();
    if (!d) return nullptr;
    d->setMaxOutputSize(maxW, maxH);
    d->setDeepOutput(deep);
    if (!d->open(useProxy ? proxy->second : resolve(a), error)) return nullptr;
    return (decoders_[key] = std::move(d)).get();
}

std::shared_ptr<const VideoFrame> Renderer::sourceFrame(const VideoLayer& layer, int outW, int outH,
                                                        const std::string& purpose, bool deep) {
    const MediaAsset& a = *layer.asset;
    const bool still = a.type == MediaType::Image;
    const Time src = still ? Time::zero() : layer.sourceTime;
    // Decode size: preview decodes at output size, export at native size.
    const int dw = preview_ ? outW : 0, dh = preview_ ? outH : 0;
    std::string key;
    const Rational fps = a.fps.num > 0 ? a.fps : project_.fps;
    const std::string cacheId = deep ? a.id + "#deep" : a.id;
    const bool reversed = layer.clip->reversed && !still;
    FrameCache* cache = cache_;
    if (reversed && !cache) {
        if (!reverseCache_) reverseCache_ = std::make_unique<FrameCache>(384ull * 1024 * 1024);
        cache = reverseCache_.get();
    }
    if (cache) {
        key = FrameCache::key(cacheId, src.toFrame(fps), dw, dh);
        if (auto hit = cache->get(key)) return hit;
    }
    std::string err;
    IMediaDecoder* d = pool_.decoder(a, purpose, &err, dw, dh, preview_, deep);
    if (!d) {
        warn("media offline: " + a.uri + (err.empty() ? "" : " (" + err + ")"));
        return nullptr;
    }
    if (reversed) {
        // Playing backwards: decode the block of frames that ends here forwards (one seek,
        // then sequential reads) and keep them all; the next frames shown are the earlier
        // ones of the block, which are then already in the cache.
        const std::int64_t target = src.toFrame(fps);
        const std::int64_t firstFrame = layer.clip->sourceIn.toFrame(fps);
        const int w = dw > 0 ? dw : (a.width > 0 ? a.width : 1920), h = dh > 0 ? dh : (a.height > 0 ? a.height : 1080);
        const std::int64_t frameBytes = static_cast<std::int64_t>(w) * h * (deep ? 8 : 4);
        const std::int64_t maxFrames = std::clamp<std::int64_t>((128ll * 1024 * 1024) / std::max<std::int64_t>(1, frameBytes), 4,
                                                                std::max<std::int64_t>(4, fps.num / std::max<std::int64_t>(1, fps.den)));
        const std::int64_t from = std::max(firstFrame, target - maxFrames + 1);
        std::shared_ptr<const VideoFrame> wanted;
        for (std::int64_t i = from; i <= target; ++i) {
            auto f = d->videoFrameAt(Time::fromFrame(i, fps));
            if (!f) continue;
            auto shared = std::make_shared<const VideoFrame>(std::move(*f));
            cache->put(FrameCache::key(cacheId, i, dw, dh), shared);
            if (i == target) wanted = shared;
        }
        return wanted;
    }
    auto frame = d->videoFrameAt(src);
    if (!frame) return nullptr;
    auto shared = std::make_shared<const VideoFrame>(std::move(*frame));
    if (cache) cache->put(key, shared);
    return shared;
}

void Renderer::warn(std::string w) {
    // Deduplicated and bounded: a long preview session must not grow memory.
    if (warnings_.size() >= 64 || std::find(warnings_.begin(), warnings_.end(), w) != warnings_.end()) return;
    warnings_.push_back(std::move(w));
}

namespace {

// Runs a layer's grade on the CPU (reference compositor, transitions, fallbacks) and
// brings the frame to the output's format.
CompositeLayer baked(const CompositeLayer& l, bool deepOut) {
    if (!l.frame || (!l.grade && l.frame->deep() == deepOut)) return l;
    auto f = std::make_shared<VideoFrame>(*l.frame);
    if (l.grade && !l.grade->params.is_null()) applyColorGrade(*f, l.grade->params);
    if (l.grade && !l.grade->lookParams.is_null()) applyColorLook(*f, l.grade->lookParams);
    if (f->deep() && !deepOut) convertFrame(*f, PixelFormat::RGBA8);  // dither down after grading
    return CompositeLayer{std::move(f), l.transform, nullptr};
}

class CpuCompositor final : public ICompositor {
public:
    std::string name() const override { return "builtin.cpu_compositor"; }
    bool supportsDeep() const override { return true; }
    bool supportsGrade() const override { return true; }
    bool composite(VideoFrame& out, const std::vector<CompositeLayer>& in, std::string*) override {
        std::vector<CompositeLayer> layers;
        layers.reserve(in.size());
        for (const auto& l : in) layers.push_back(l.grade || (l.frame && l.frame->deep() && !out.deep()) ? baked(l, out.deep()) : l);
        if (out.deep()) {
            const std::uint16_t one = floatToHalf(1.0f);
            std::fill(out.half.begin(), out.half.end(), std::uint16_t{0});
            for (std::size_t i = 3; i < out.half.size(); i += 4) out.half[i] = one;  // opaque black
            for (const auto& l : layers)
                if (l.frame) compositeOverDeep(out, *l.frame, l.transform);
            return true;
        }
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

std::optional<CompositeLayer> Renderer::clipLayer(const VideoLayer& layer, const VideoFrame& out,
                                                  const std::string& purpose) {
    // Live preview of an uncommitted effect change: swap only this clip's effects.
    std::optional<Clip> live;
    if (override_ && override_->clipId == layer.clip->id) {
        live = *layer.clip;
        live->effects = override_->type == kColorGradeEffect
                            ? withGradeEffect(live->effects, override_->params, "live")
                            : Json::array();
        if (override_->type != kColorGradeEffect) {
            bool placed = false;
            for (const auto& e : layer.clip->effects) {
                if (e.is_object() && e.value("type", std::string{}) == override_->type) {
                    if (!override_->params.is_null()) {
                        Json c = e;
                        c["params"] = override_->params;
                        live->effects.push_back(std::move(c));
                    }
                    placed = true;
                    continue;
                }
                live->effects.push_back(e);
            }
            if (!placed && !override_->params.is_null())
                live->effects.push_back({{"type", override_->type}, {"enabled", true}, {"params", override_->params}});
        }
    }
    const Clip& full = live ? *live : *layer.clip;
    // A grade and/or a one-tap look at the end of the chain run in the compositor (GPU
    // shader) when it can: only the effects before them run here, and the frame stays deep.
    std::shared_ptr<const GpuGrade> gpu;
    std::optional<Clip> rest;
    if (!deep_ && full.effects.is_array() && !full.effects.empty() && compositor().supportsGrade()) {
        std::vector<int> on;  // enabled effects, in order
        for (int i = 0; i < static_cast<int>(full.effects.size()); ++i)
            if (full.effects[i].is_object() && full.effects[i].value("enabled", true)) on.push_back(i);
        auto typeAt = [&](std::size_t k) { return full.effects[on[k]].value("type", std::string{}); };
        auto paramsAt = [&](std::size_t k) { return full.effects[on[k]].value("params", Json::object()); };
        std::vector<int> moved;
        Json grade, look;
        if (!on.empty() && typeAt(on.size() - 1) == kColorLookEffect) {
            look = paramsAt(on.size() - 1);
            moved.push_back(on.back());
            if (on.size() >= 2 && typeAt(on.size() - 2) == kColorGradeEffect) {
                grade = paramsAt(on.size() - 2);
                moved.push_back(on[on.size() - 2]);
            }
        } else if (!on.empty() && typeAt(on.size() - 1) == kColorGradeEffect) {
            grade = paramsAt(on.size() - 1);
            moved.push_back(on.back());
        }
        if (!moved.empty()) {
            gpu = gpuGrade(grade, look);
            rest = full;
            for (int i : moved) rest->effects.erase(rest->effects.begin() + i);  // highest index first
        }
    }
    const Clip& clip = rest ? *rest : full;
    // Deep sources (10-bit, Log) stay deep through grading even in an 8-bit preview,
    // so strong corrections do not band; plain clips skip the cost.
    const bool graded = full.effects.is_array() && !full.effects.empty();
    auto frame = sourceFrame(layer, out.width, out.height, purpose, deep_ || graded);
    if (!frame) return std::nullopt;
    if (modules_ && clip.effects.is_array() && !clip.effects.empty()) {
        // Effects work on a copy: cached source frames stay untouched.
        auto fx = std::make_shared<VideoFrame>(*frame);
        std::vector<std::string> w;
        applyClipEffects(*modules_, clip, *fx, layer.sourceTime - layer.clip->sourceIn, &w, pool_.projectDir());
        for (auto& m : w) warn(std::move(m));
        frame = std::move(fx);
    }
    if (frame->deep() && !out.deep() && !gpu)  // 8-bit output: dither down once, after grading
        frame = std::make_shared<const VideoFrame>(convertedFrame(*frame, PixelFormat::RGBA8));
    Transform tr = animatedTransform(*layer.clip, layer.sourceTime);  // keyframes (zoom, pan, opacity)
    // Scale offsets from canvas pixels to output pixels (preview may be smaller).
    const double k = static_cast<double>(out.width) / project_.canvas.width;
    tr.x *= k;
    tr.y *= k;
    return CompositeLayer{std::move(frame), tr, std::move(gpu)};
}

std::optional<CompositeLayer> Renderer::generatedLayer(const Clip& c, Time src, const VideoFrame& out) {
    const std::string kind = c.content.value("kind", std::string{});
    // Subtitle lines take their look and place from the track.
    const Json resolved = kind == "subtitle" ? drawnContent(project_, c) : Json();
    const Json& content = kind == "subtitle" ? resolved : c.content;
    const auto gen = generatorFor(modules_, kind);
    if (!gen) {
        warn("no generator for clip kind: " + kind);
        return std::nullopt;
    }
    const double local = (src - c.sourceIn).seconds(), length = c.sourceDuration().seconds();
    const Transform tr = animatedTransform(c, src);
    // Drawn straight at output size with the clip's position and scale: sharp at any zoom.
    const double k = static_cast<double>(out.width) / project_.canvas.width;
    const vec::Affine place = vec::Affine::translate(out.width / 2.0 + tr.x * k, out.height / 2.0 + tr.y * k) *
                              vec::Affine::scale(tr.scale * k);
    std::string key;
    if (cache_ && gen->still(content, local, length)) {
        key = "gen|" + c.id + "|" + std::to_string(std::hash<std::string>{}(content.dump())) + "|" +
              std::to_string(tr.x) + "," + std::to_string(tr.y) + "," + std::to_string(tr.scale) + "|" +
              std::to_string(out.width) + "x" + std::to_string(out.height) + (out.deep() ? "d" : "");
        if (auto hit = cache_->get(key)) {
            Transform t;
            t.opacity = tr.opacity;
            return CompositeLayer{std::move(hit), t};
        }
    }
    auto frame = std::make_shared<VideoFrame>();
    frame->allocate(out.width, out.height, out.format);  // transparent
    frame->pts = out.pts;
    gen->draw(content, local, length, place, *frame);
    std::shared_ptr<const VideoFrame> shared = std::move(frame);
    if (!key.empty()) cache_->put(key, shared);
    Transform t;
    t.opacity = tr.opacity;
    return CompositeLayer{std::move(shared), t};
}

std::optional<CompositeLayer> Renderer::transitionLayer(const ActiveTransition& tr, std::size_t trackIndex, Time t,
                                                        const VideoFrame& out, const std::string& purpose) {
    // Each side is drawn on its own transparent canvas, then the two are blended.
    auto side = [&](const Clip* c, const std::string& why) {
        VideoFrame f;
        f.allocate(out.width, out.height, out.format);
        if (out.deep()) std::fill(f.half.begin(), f.half.end(), std::uint16_t{0});
        else std::fill(f.rgba.begin(), f.rgba.end(), std::uint8_t{0});
        if (c && c->generated()) {  // text, sticker: held at its first/last frame outside the clip
            const Time src = std::clamp(c->toSource(t), c->sourceIn, std::max(c->sourceIn, c->sourceOut - Time::fromFrame(1, project_.fps)));
            if (auto l = generatedLayer(*c, src, f); l && l->frame) {
                if (out.deep()) compositeOverDeep(f, *l->frame, l->transform);
                else compositeOver(f, *l->frame, l->transform);
            }
            return f;
        }
        const MediaAsset* a = c ? project_.findAsset(c->assetId) : nullptr;
        if (!a) return f;
        // Past its out point the outgoing clip plays on into its source (its last frame
        // held when there is none); the incoming one starts early the same way.
        Time src = c->toSource(t);
        const Rational fps = a->fps.num > 0 ? a->fps : project_.fps;
        const Time last = a->duration > Time::zero() ? a->duration - Time::fromFrame(1, fps) : c->sourceOut - Time::fromFrame(1, fps);
        src = std::clamp(src, Time::zero(), std::max(Time::zero(), last));
        if (auto l = clipLayer({c, a, src, trackIndex}, out, why); l && l->frame) {
            *l = baked(*l, out.deep());  // each side is drawn here, not by the compositor
            if (out.deep()) compositeOverDeep(f, *l->frame, l->transform);
            else compositeOver(f, *l->frame, l->transform);
        }
        return f;
    };
    // The incoming clip reads through its own decoder: both sides decode at once without seeking back and forth.
    const VideoFrame a = side(tr.from, purpose), b = side(tr.to, purpose + "#in");
    auto blended = std::make_shared<VideoFrame>();
    blended->allocate(out.width, out.height, out.format);
    blended->pts = t;
    applyTransition(tr.type, a, b, tr.progress, *blended, t);
    return CompositeLayer{std::move(blended), Transform{}};
}

VideoFrame Renderer::renderVideo(Time t, int width, int height, const std::string& purpose) {
    VideoFrame out;
    out.allocate(width > 0 ? width : project_.canvas.width, height > 0 ? height : project_.canvas.height,
                 deep_ ? PixelFormat::RGBA16F : PixelFormat::RGBA8);
    out.pts = t;
    std::vector<CompositeLayer> layers;
    for (std::size_t i = 0; i < project_.tracks.size(); ++i) {
        const Track& track = project_.tracks[i];
        if (track.type != TrackType::Video || !track.enabled) continue;
        if (auto tr = transitionAt(track, t)) {
            if (auto l = transitionLayer(*tr, i, t, out, purpose)) layers.push_back(std::move(*l));
            continue;
        }
        const Clip* c = track.clipAt(t);
        if (c && c->generated()) {
            if (auto l = generatedLayer(*c, c->toSource(t), out)) layers.push_back(std::move(*l));
            continue;
        }
        const MediaAsset* a = c ? project_.findAsset(c->assetId) : nullptr;
        if (!a) continue;
        if (auto l = clipLayer({c, a, c->toSource(t), i}, out, purpose)) layers.push_back(std::move(*l));
    }
    std::string err;
    if (out.deep() && !compositor().supportsDeep()) {
        static const std::shared_ptr<ICompositor> cpu = createCpuCompositor();
        cpu->composite(out, layers, nullptr);
        return out;
    }
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
        if (!audiofx::trackAudible(project_, tr)) continue;
        // Track pan (balance): the far side is turned down, the near side kept.
        const double pan = ch == 2 ? audiofx::trackPan(tr) : 0.0;
        const float panL = static_cast<float>(std::min(1.0, 1.0 - pan)), panR = static_cast<float>(std::min(1.0, 1.0 + pan));
        for (const Clip& c : tr.clips) {
            const MediaAsset* a = project_.findAsset(c.assetId);
            if (!a || !a->audio || c.volume <= 0.0) continue;
            if (audiofx::detached(c) || c.extra.value("freeze", false)) continue;  // sound elsewhere / still frame
            const std::int64_t cs = c.timelineIn.toSample(sr, Rounding::Up);
            const std::int64_t ce = c.timelineOut().toSample(sr, Rounding::Up);
            const std::int64_t s0 = std::max(cs, startSample), s1 = std::min(ce, endSample);
            if (s1 <= s0) continue;
            std::string err;
            IMediaDecoder* d = pool_.decoder(*a, purpose, &err);
            if (!d) continue;
            const std::int64_t n = s1 - s0;
            const bool unity = c.speed == Rational{1, 1};
            // For speed != 1 read the corresponding source span and resample linearly (no pitch correction in P0).
            const std::int64_t need = unity ? n : std::max<std::int64_t>(1, mulDiv(n, c.speed.num, c.speed.den, Rounding::Up));
            // First source sample of the span read forwards (a reversed clip reads the span
            // that ends where the block starts, then plays it backwards).
            Time src = c.toSource(Time::fromSample(s0, sr));
            if (c.reversed) src = src + Time::fromTicks(1) - Time::fromSample(need, sr);
            AudioBuffer in;
            const auto fx = audiofx::clipEffects(c);
            if (fx.empty()) {
                in = d->readAudio(src, need, sr, ch);
            } else {
                // Effects see the block with source context on both sides, starting on a fixed
                // grid, so the result does not depend on how the timeline is cut into blocks.
                const std::int64_t margin = audiofx::clipMargin(modules_, fx, sr);
                const std::int64_t first = src.toSample(sr, Rounding::Nearest);
                std::int64_t from = first - margin;
                from = from <= 0 ? 0 : from / audiofx::kAudioFxAlign * audiofx::kAudioFxAlign;
                const std::int64_t pre = first - from;
                in = d->readAudio(Time::fromSample(from, sr), pre + need + margin, sr, ch);
                audiofx::applyClipEffects(modules_, fx, in);
                const std::int64_t keep = std::max<std::int64_t>(0, std::min(need, in.frames() - pre));
                std::vector<float> body(in.samples.begin() + std::min<std::int64_t>(pre, in.frames()) * ch,
                                        in.samples.begin() + (std::min<std::int64_t>(pre, in.frames()) + keep) * ch);
                in.samples.swap(body);
            }
            if (c.reversed) {
                const std::int64_t m = in.frames();
                for (std::int64_t i = 0; i < m / 2; ++i)
                    for (int k = 0; k < ch; ++k)
                        std::swap(in.samples[static_cast<std::size_t>(i * ch + k)],
                                  in.samples[static_cast<std::size_t>((m - 1 - i) * ch + k)]);
            }
            const std::int64_t inFrames = in.frames();
            if (inFrames == 0) continue;
            // Volume keyframes: gain evaluated every 32 samples and ramped in between,
            // so fades are smooth (no zipper noise) and cheap.
            const bool keyed = c.keyframes.is_object() && c.keyframes.contains(kf::kVolume);
            const KeyframeCurve volumeCurve = keyed ? KeyframeCurve(c.keyframes[kf::kVolume]) : KeyframeCurve();
            // Transitions fade the sound with the picture (out before a cut, in after it).
            const std::size_t ci = static_cast<std::size_t>(&c - tr.clips.data());
            const bool fading = c.transitionIn.is_object() || c.transitionOut.is_object() ||
                                (ci + 1 < tr.clips.size() && tr.clips[ci + 1].transitionIn.is_object());
            const bool clipFades = audiofx::hasFades(c);
            const bool animated = keyed || fading || clipFades;
            auto gainAt = [&](std::int64_t i) {
                const Time at = Time::fromSample(s0 + i, sr);
                const double v = keyed ? volumeCurve.at(c.toSource(at)) : 1.0;
                const double f = fading ? transitionGain(tr, c, at) : 1.0;
                const double cf = clipFades ? audiofx::fadeGain(c, at) : 1.0;
                return static_cast<float>(c.volume * tr.volume * v * f * cf);
            };
            constexpr std::int64_t kStep = 32;
            float g0 = gainAt(0), g1 = animated ? gainAt(std::min(kStep, n)) : g0;
            for (std::int64_t i = 0; i < n; ++i) {
                if (animated && i % kStep == 0 && i > 0) {
                    g0 = g1;
                    g1 = gainAt(std::min(i + kStep, n));
                }
                const float gain = animated ? g0 + (g1 - g0) * static_cast<float>(i % kStep) / kStep : g0;
                float* o = &mix.samples[static_cast<std::size_t>(s0 - startSample + i) * ch];
                float v[8];
                const int nc = std::min(ch, 8);
                if (unity) {
                    if (i >= inFrames) break;
                    const float* s = &in.samples[static_cast<std::size_t>(i) * ch];
                    for (int k = 0; k < nc; ++k) v[k] = s[k];
                } else {
                    const double pos = static_cast<double>(i) * c.speed.num / c.speed.den;
                    const std::int64_t j = std::min<std::int64_t>(static_cast<std::int64_t>(pos), inFrames - 1);
                    const std::int64_t j1 = std::min<std::int64_t>(j + 1, inFrames - 1);
                    const float f = static_cast<float>(pos - j);
                    for (int k = 0; k < nc; ++k) {
                        const float a0 = in.samples[static_cast<std::size_t>(j) * ch + k];
                        const float a1 = in.samples[static_cast<std::size_t>(j1) * ch + k];
                        v[k] = a0 + (a1 - a0) * f;
                    }
                }
                if (ch == 2) {
                    v[0] *= panL;
                    v[1] *= panR;
                }
                for (int k = 0; k < nc; ++k) o[k] += v[k] * gain;
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
    renderer.setDeepOutput(s.bitDepth > 8 && enc.supportsDeep());
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
