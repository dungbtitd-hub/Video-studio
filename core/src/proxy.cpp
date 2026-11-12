#include "vstudio/proxy.hpp"

#include "vstudio/render.hpp"

#include <algorithm>
#include <cstdio>

namespace vstudio {

bool needsProxy(const MediaAsset& a, const ProxyPolicy& policy) {
    if (!a.hasVideo || a.type == MediaType::Image) return false;
    return static_cast<std::int64_t>(a.width) * a.height > policy.pixelThreshold || a.vfr;
}

ProxyResult generateProxy(IMediaBackend& backend, const std::string& src, const MediaAsset& a, const std::string& dst,
                          const ProxyPolicy& policy, const CancelToken& cancel, const std::function<void(double)>& progress) {
    ProxyResult r;
    auto dec = backend.createDecoder();
    auto enc = backend.createEncoder();
    if (!dec || !enc) {
        r.error = "backend cannot transcode";
        return r;
    }
    const int h = std::min(policy.maxHeight, a.height) & ~1;
    const int w = std::max(2, static_cast<int>(static_cast<std::int64_t>(a.width) * h / std::max(1, a.height)) & ~1);
    dec->setMaxOutputSize(w, h);
    if (!dec->open(src, &r.error)) return r;
    ExportSettings s;
    s.width = w;
    s.height = h;
    s.fps = a.fps.num > 0 ? a.fps : Rational{30, 1};  // VFR sources are conformed to their average rate
    s.videoBitrate = 2'000'000;
    s.includeAudio = false;
    s.gopSize = 10;  // short GOP: any seek decodes at most 10 frames
    s.fastPreset = true;
    const std::string tmp = dst + ".part.mp4";
    if (!enc->open(tmp, s, &r.error)) return r;
    const std::int64_t total = std::max<std::int64_t>(1, a.duration.toFrame(s.fps, Rounding::Up));
    for (std::int64_t i = 0; i < total; ++i) {
        if (cancel.cancelled()) {
            enc->finish(nullptr);
            std::remove(tmp.c_str());
            r.error = "cancelled";
            return r;
        }
        auto f = dec->videoFrameAt(Time::fromFrame(i, s.fps));
        if (!f) break;
        if (f->width != w || f->height != h) {
            VideoFrame canvas;
            canvas.allocate(w, h);
            for (std::size_t i = 3; i < canvas.rgba.size(); i += 4) canvas.rgba[i] = 255;
            compositeOver(canvas, *f, Transform{});
            f = std::move(canvas);
        }
        if (!enc->writeVideo(*f, &r.error)) return r;
        if (progress && i % 30 == 0) progress(static_cast<double>(i) / total);
    }
    if (!enc->finish(&r.error)) return r;
    std::remove(dst.c_str());
    if (std::rename(tmp.c_str(), dst.c_str()) != 0) {
        r.error = "cannot move proxy into place";
        return r;
    }
    if (progress) progress(1.0);
    r.ok = true;
    r.path = dst;
    return r;
}

}  // namespace vstudio
