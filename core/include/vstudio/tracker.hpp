// Following a region through a clip (a logo, a number plate, a screen) so a
// cover can move with it. Classic template tracking, no external library:
// normalised cross-correlation searched coarse-to-fine around the predicted
// position, three scales for zoom, a slowly adapting template. Runs on small
// decoded frames (about 480 px) as a background job; the result becomes
// ordinary keyframes the user can correct by hand.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/time.hpp"

namespace vstudio {

struct TrackBox {
    double x = 0, y = 0, w = 0, h = 0;  // 0..1 of the frame (left, top, width, height)
};

struct TrackSample {
    Time sourceTime;
    TrackBox box;
    double confidence = 1;  // 0..1, normalised correlation of the match
};

class RegionTracker {
public:
    // False when the box is too small or outside the frame.
    bool init(const VideoFrame& first, TrackBox box);
    // Finds the region in the next frame.
    TrackSample update(const VideoFrame& next, Time t);
    [[nodiscard]] bool lost() const { return lowRun_ >= 4; }

private:
    struct Gray {
        int w = 0, h = 0;
        std::vector<float> v;
        [[nodiscard]] float at(int x, int y) const;
        [[nodiscard]] float bilinear(double x, double y) const;
    };
    static Gray gray(const VideoFrame& f, double scale);
    static Gray half(const Gray& g);
    void sample(const Gray& img, double cx, double cy, double w, double h, std::vector<float>& out) const;
    double ncc(const std::vector<float>& a, const std::vector<float>& b) const;

    double scale_ = 1;           // working image = frame * scale_
    int tw_ = 0, th_ = 0;        // template size (working px)
    std::vector<float> tmpl_, first_;
    double cx_ = 0, cy_ = 0, bw_ = 0, bh_ = 0;  // box in working px
    double vx_ = 0, vy_ = 0;
    int fw_ = 0, fh_ = 0;  // frame size
    int lowRun_ = 0;
};

struct TrackResult {
    std::vector<TrackSample> samples;  // one per analysed frame, starting at `from`
    bool lost = false;
    bool cancelled = false;
    std::string error;
};

// Tracks from `from` (where `start` is) to `to`, one sample per frame at `fps`.
// `progress(0..1)` returning false cancels. `decoder` should already be limited
// to a small output size (setMaxOutputSize(480, 480)).
TrackResult trackRegion(IMediaDecoder& decoder, Time from, Time to, Rational fps, TrackBox start,
                        const std::function<bool(double)>& progress = {});

// Fewest samples whose straight-line interpolation stays within `tolerance`
// (fraction of the frame) of every tracked box: dense tracking -> few keys.
std::vector<TrackSample> simplifyTrack(const std::vector<TrackSample>& samples, double tolerance = 0.003);

class MediaPool;

// Tracks a clip's cover from `fromTimeline` towards `toTimeline` (clamped to the clip),
// starting from the cover's region at that moment. On success `keys` holds the simplified
// region keys (source time, {x, y, w, h}) for Editor::setCoverTrack.
struct CoverTrack {
    TrackResult result;
    std::vector<std::pair<Time, std::array<double, 4>>> keys;
};
CoverTrack trackCoverInClip(const Project& project, MediaPool& pool, const Id& clipId, const Id& coverId,
                            Time fromTimeline, Time toTimeline, const std::function<bool(double)>& progress = {});

}  // namespace vstudio
