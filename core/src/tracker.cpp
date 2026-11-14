#include "vstudio/tracker.hpp"

#include <algorithm>
#include <cmath>

#include "vstudio/cover.hpp"
#include "vstudio/pixel.hpp"
#include "vstudio/render.hpp"

namespace vstudio {

namespace {
constexpr int kTemplateSide = 40;     // working template size (largest side), px
constexpr double kMinBoxPx = 6;       // smaller boxes cannot be tracked
constexpr double kLowConfidence = 0.45;
constexpr int kMaxCoarseRadius = 24;  // half-res px: bounds the cost of one frame whatever happens
constexpr double kMaxSizeDrift = 2.0;  // the box stays within 1/2x..2x of its starting size
}  // namespace

float RegionTracker::Gray::at(int x, int y) const {
    x = std::clamp(x, 0, w - 1);
    y = std::clamp(y, 0, h - 1);
    return v[static_cast<std::size_t>(y) * w + x];
}

float RegionTracker::Gray::bilinear(double x, double y) const {
    x -= 0.5;
    y -= 0.5;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = static_cast<float>(x - x0), fy = static_cast<float>(y - y0);
    const float a = at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx;
    const float b = at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx;
    return a * (1 - fy) + b * fy;
}

RegionTracker::Gray RegionTracker::gray(const VideoFrame& f, double scale) {
    Gray g;
    g.w = std::max(1, static_cast<int>(std::lround(f.width * scale)));
    g.h = std::max(1, static_cast<int>(std::lround(f.height * scale)));
    g.v.resize(static_cast<std::size_t>(g.w) * g.h);
    const double step = 1.0 / scale;
    const int n = std::max(1, static_cast<int>(std::floor(step)));  // box filter when shrinking
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            float sum = 0;
            int cnt = 0;
            const int sx0 = static_cast<int>(x * step), sy0 = static_cast<int>(y * step);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) {
                    const int sx = std::min(f.width - 1, sx0 + i), sy = std::min(f.height - 1, sy0 + j);
                    const std::size_t k = (static_cast<std::size_t>(sy) * f.width + sx) * 4;
                    float r, gg, b;
                    if (f.deep()) {
                        r = halfToFloat(f.half[k]);
                        gg = halfToFloat(f.half[k + 1]);
                        b = halfToFloat(f.half[k + 2]);
                    } else {
                        r = f.rgba[k] / 255.f;
                        gg = f.rgba[k + 1] / 255.f;
                        b = f.rgba[k + 2] / 255.f;
                    }
                    sum += 0.299f * r + 0.587f * gg + 0.114f * b;
                    ++cnt;
                }
            g.v[static_cast<std::size_t>(y) * g.w + x] = sum / static_cast<float>(cnt);
        }
    return g;
}

RegionTracker::Gray RegionTracker::half(const Gray& g) {
    Gray h;
    h.w = std::max(1, g.w / 2);
    h.h = std::max(1, g.h / 2);
    h.v.resize(static_cast<std::size_t>(h.w) * h.h);
    for (int y = 0; y < h.h; ++y)
        for (int x = 0; x < h.w; ++x)
            h.v[static_cast<std::size_t>(y) * h.w + x] =
                0.25f * (g.at(2 * x, 2 * y) + g.at(2 * x + 1, 2 * y) + g.at(2 * x, 2 * y + 1) + g.at(2 * x + 1, 2 * y + 1));
    return h;
}

// Samples the box (centre cx, cy, size w x h) onto the template grid.
void RegionTracker::sample(const Gray& img, double cx, double cy, double w, double h, std::vector<float>& out) const {
    out.resize(static_cast<std::size_t>(tw_) * th_);
    for (int y = 0; y < th_; ++y)
        for (int x = 0; x < tw_; ++x)
            out[static_cast<std::size_t>(y) * tw_ + x] =
                img.bilinear(cx + ((x + 0.5) / tw_ - 0.5) * w, cy + ((y + 0.5) / th_ - 0.5) * h);
}

double RegionTracker::ncc(const std::vector<float>& a, const std::vector<float>& b) const {
    const std::size_t n = std::min(a.size(), b.size());
    double ma = 0, mb = 0;
    for (std::size_t i = 0; i < n; ++i) ma += a[i], mb += b[i];
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double ab = 0, aa = 0, bb = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        ab += da * db, aa += da * da, bb += db * db;
    }
    if (aa < 1e-9 || bb < 1e-9) return (aa < 1e-9 && bb < 1e-9) ? 1 : 0;  // flat patches
    return ab / std::sqrt(aa * bb);
}

bool RegionTracker::init(const VideoFrame& first, TrackBox box) {
    if (first.empty()) return false;
    fw_ = first.width;
    fh_ = first.height;
    const double bw = box.w * fw_, bh = box.h * fh_;
    if (bw < kMinBoxPx || bh < kMinBoxPx) return false;
    // Work so that the box is about kTemplateSide px: cheap and scale independent.
    scale_ = std::min(1.0, kTemplateSide / std::max(bw, bh));
    bw_ = bw * scale_;
    bh_ = bh * scale_;
    bw0_ = bw_;
    cx_ = (box.x + box.w / 2) * fw_ * scale_;
    cy_ = (box.y + box.h / 2) * fh_ * scale_;
    tw_ = std::max(4, static_cast<int>(std::lround(bw_)));
    th_ = std::max(4, static_cast<int>(std::lround(bh_)));
    sample(gray(first, scale_), cx_, cy_, bw_, bh_, tmpl_);
    first_ = tmpl_;
    vx_ = vy_ = 0;
    lowRun_ = 0;
    return true;
}

TrackSample RegionTracker::update(const VideoFrame& next, Time t) {
    const Gray g = gray(next, scale_);
    const Gray g2 = half(g);
    std::vector<float> patch;
    // 1) Coarse: half resolution, whole window around the predicted centre.
    const double px = cx_ + vx_, py = cy_ + vy_;
    const int radius = std::min(
        kMaxCoarseRadius,
        static_cast<int>(std::ceil(std::max(6.0, 0.75 * std::max(bw_, bh_) + std::hypot(vx_, vy_)) / 2)));
    double best = -2, bx = px, by = py;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            const double cx = px / 2 + dx, cy = py / 2 + dy;
            sample(g2, cx, cy, bw_ / 2, bh_ / 2, patch);
            const double c = ncc(tmpl_, patch);
            if (c > best) best = c, bx = cx * 2, by = cy * 2;
        }
    // 2) Fine: full resolution, +-2 px, three scales.
    double bestScale = 1;
    best = -2;
    double fx = bx, fy = by;
    for (const double s : {0.96, 1.0, 1.04})
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                sample(g, bx + dx, by + dy, bw_ * s, bh_ * s, patch);
                const double c = ncc(tmpl_, patch) - (s != 1.0 ? 0.004 : 0.0);  // prefer no scale change
                if (c > best) best = c, fx = bx + dx, fy = by + dy, bestScale = s;
            }
    const double conf = std::clamp(best, 0.0, 1.0);
    if (conf >= kLowConfidence) {
        vx_ = 0.6 * vx_ + 0.4 * (fx - cx_);
        vy_ = 0.6 * vy_ + 0.4 * (fy - cy_);
        cx_ = fx;
        cy_ = fy;
        // Flat or repeating content can make "a bit bigger" win every frame: bound the drift.
        const double grown = bw_ * bestScale;
        if (grown >= bw0_ / kMaxSizeDrift && grown <= bw0_ * kMaxSizeDrift) {
            bw_ = grown;
            bh_ *= bestScale;
        }
        lowRun_ = 0;
        // Adapt slowly to lighting / perspective, anchored to the first look to limit drift.
        if (conf > 0.8) {
            sample(g, cx_, cy_, bw_, bh_, patch);
            for (std::size_t i = 0; i < tmpl_.size(); ++i) tmpl_[i] = 0.8f * tmpl_[i] + 0.12f * patch[i] + 0.08f * first_[i];
        }
    } else {
        ++lowRun_;  // keep the prediction; give up after a few frames
        cx_ += vx_;
        cy_ += vy_;
    }
    // The centre never leaves the picture.
    cx_ = std::clamp(cx_, 0.0, fw_ * scale_);
    cy_ = std::clamp(cy_, 0.0, fh_ * scale_);
    TrackSample s;
    s.sourceTime = t;
    s.confidence = conf;
    const double W = fw_ * scale_, H = fh_ * scale_;
    s.box = {(cx_ - bw_ / 2) / W, (cy_ - bh_ / 2) / H, bw_ / W, bh_ / H};
    return s;
}

TrackResult trackRegion(IMediaDecoder& decoder, Time from, Time to, Rational fps, TrackBox start,
                        const std::function<bool(double)>& progress) {
    TrackResult r;
    if (fps.num <= 0 || fps.den <= 0) fps = {30, 1};
    auto first = decoder.videoFrameAt(from);
    RegionTracker tracker;
    if (!first || !tracker.init(*first, start)) {
        r.error = first ? "vùng quá nhỏ để bám theo" : "không đọc được khung hình";
        return r;
    }
    r.samples.push_back({from, start, 1});
    const std::int64_t total = std::max<std::int64_t>(1, (to - from).ticks() * fps.num / (Time::kTicksPerSecond * fps.den));
    for (std::int64_t i = 1; i <= total; ++i) {
        const Time t = from + Time::fromFrame(i, fps);
        if (t > to) break;
        if (progress && !progress(static_cast<double>(i) / static_cast<double>(total))) {
            r.cancelled = true;
            break;
        }
        auto f = decoder.videoFrameAt(t);
        if (!f) break;
        TrackSample s = tracker.update(*f, t);
        if (tracker.lost()) {
            r.lost = true;
            // Drop the predicted (unconfirmed) tail.
            while (r.samples.size() > 1 && r.samples.back().confidence < kLowConfidence) r.samples.pop_back();
            break;
        }
        r.samples.push_back(s);
    }
    while (r.samples.size() > 1 && r.samples.back().confidence < kLowConfidence) r.samples.pop_back();
    return r;
}

namespace {
double boxError(const TrackSample& a, const TrackSample& b, const TrackSample& m) {
    const double span = static_cast<double>((b.sourceTime - a.sourceTime).ticks());
    const double u = span > 0 ? static_cast<double>((m.sourceTime - a.sourceTime).ticks()) / span : 0;
    auto lerp = [&](double p, double q) { return p + (q - p) * u; };
    return std::max({std::abs(lerp(a.box.x, b.box.x) - m.box.x), std::abs(lerp(a.box.y, b.box.y) - m.box.y),
                     std::abs(lerp(a.box.w, b.box.w) - m.box.w), std::abs(lerp(a.box.h, b.box.h) - m.box.h)});
}

void simplify(const std::vector<TrackSample>& s, std::size_t a, std::size_t b, double tol, std::vector<bool>& keep) {
    if (b <= a + 1) return;
    double worst = -1;
    std::size_t at = a;
    for (std::size_t i = a + 1; i < b; ++i)
        if (const double e = boxError(s[a], s[b], s[i]); e > worst) worst = e, at = i;
    if (worst <= tol) return;
    keep[at] = true;
    simplify(s, a, at, tol, keep);
    simplify(s, at, b, tol, keep);
}
}  // namespace

std::vector<TrackSample> simplifyTrack(const std::vector<TrackSample>& samples, double tolerance) {
    if (samples.size() <= 2) return samples;
    std::vector<bool> keep(samples.size(), false);
    keep.front() = keep.back() = true;
    simplify(samples, 0, samples.size() - 1, tolerance, keep);
    std::vector<TrackSample> out;
    for (std::size_t i = 0; i < samples.size(); ++i)
        if (keep[i]) out.push_back(samples[i]);
    return out;
}

CoverTrack trackCoverInClip(const Project& project, MediaPool& pool, const Id& clipId, const Id& coverId,
                            Time fromTimeline, Time toTimeline, const std::function<bool(double)>& progress) {
    CoverTrack out;
    const Clip* clip = project.findClip(clipId);
    const MediaAsset* asset = clip ? project.findAsset(clip->assetId) : nullptr;
    const Json* params = nullptr;
    if (clip && clip->effects.is_array())
        for (const auto& e : clip->effects)
            if (e.is_object() && e.value("effect_id", std::string{}) == coverId) params = &e["params"];
    if (!clip || !asset || !params) {
        out.result.error = "không tìm thấy vùng che";
        return out;
    }
    const Time a = std::clamp(fromTimeline, clip->timelineIn, clip->timelineOut());
    const Time b = std::clamp(toTimeline, clip->timelineIn, clip->timelineOut());
    if (b <= a) {
        out.result.error = "không còn đoạn nào phía sau để bám theo";
        return out;
    }
    std::string err;
    IMediaDecoder* dec = pool.decoder(*asset, "track", &err, 480, 480);
    if (!dec) {
        out.result.error = err.empty() ? "không mở được media" : err;
        return out;
    }
    const Time srcA = clip->toSource(a), srcB = clip->toSource(b);
    const Json now = coverParamsAt(*params, srcA);
    const TrackBox start{now.value("x", 0.0), now.value("y", 0.0), now.value("w", 0.1), now.value("h", 0.1)};
    const Rational fps = asset->fps.num > 0 ? asset->fps : project.fps;
    out.result = trackRegion(*dec, srcA, srcB, fps, start, progress);
    if (!out.result.error.empty() || out.result.samples.size() < 2) return out;
    for (const auto& s : simplifyTrack(out.result.samples))
        out.keys.push_back({s.sourceTime, {s.box.x, s.box.y, s.box.w, s.box.h}});
    return out;
}

}  // namespace vstudio
