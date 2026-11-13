#include "vstudio/timeline.hpp"

#include <algorithm>

namespace vstudio::edit {

namespace {

Track& requireTrack(Project& p, const Id& id) {
    Track* t = p.findTrack(id);
    if (!t) throw EditError("track not found: " + id);
    return *t;
}

Track& requireUnlocked(Project& p, const Id& id) {
    Track& t = requireTrack(p, id);
    if (t.locked) throw EditError("track is locked: " + t.name);
    return t;
}

Clip& requireClip(Project& p, const Id& clipId, Track** owner = nullptr) {
    Track* t = p.trackOfClip(clipId);
    if (!t) throw EditError("clip not found: " + clipId);
    if (t->locked) throw EditError("track is locked: " + t->name);
    if (owner) *owner = t;
    return *t->findClip(clipId);
}

void checkNoOverlap(const Track& t, const TimeRange& r, const Id& ignoreClip) {
    for (const auto& c : t.clips) {
        if (c.id == ignoreClip) continue;
        if (c.range().overlaps(r)) throw EditError("clip would overlap " + c.id + " on track " + t.name);
    }
}

Time minDuration(const Project& p) { return Time::fromFrame(1, p.fps); }

// Source media bounds. Still images and generated clips (text, stickers) have no upper bound.
void checkSourceRange(const Project& p, const Clip& c) {
    if (c.generated()) {
        if (c.sourceIn < Time::zero()) throw EditError("source in before start");
        if (c.sourceOut <= c.sourceIn) throw EditError("empty clip");
        return;
    }
    const MediaAsset* a = p.findAsset(c.assetId);
    if (!a) throw EditError("asset not found: " + c.assetId);
    if (c.sourceIn < Time::zero()) throw EditError("source in before media start");
    if (c.sourceOut <= c.sourceIn) throw EditError("empty clip");
    if (a->type != MediaType::Image && c.sourceOut > a->duration) throw EditError("source out beyond media end");
}

// Generated clips draw pictures: video tracks only.
bool fits(const Project& p, const Clip& c, TrackType t) {
    if (c.generated()) return t == TrackType::Video;
    const MediaAsset* a = p.findAsset(c.assetId);
    return a && compatible(*a, t);
}

}  // namespace

bool compatible(const MediaAsset& a, TrackType t) {
    if (t == TrackType::Video) return a.hasVideo || a.type == MediaType::Image;
    return a.audio.has_value();
}

void addTrack(Project& p, Track track, std::size_t index) {
    if (track.id.empty() || p.findTrack(track.id)) throw EditError("duplicate or empty track id");
    index = std::min(index, p.tracks.size());
    p.tracks.insert(p.tracks.begin() + static_cast<std::ptrdiff_t>(index), std::move(track));
}

void removeTrack(Project& p, const Id& trackId) {
    auto it = std::find_if(p.tracks.begin(), p.tracks.end(), [&](const Track& t) { return t.id == trackId; });
    if (it == p.tracks.end()) throw EditError("track not found: " + trackId);
    if (it->locked) throw EditError("track is locked");
    p.tracks.erase(it);
}

void addAsset(Project& p, MediaAsset asset) {
    if (asset.id.empty() || p.findAsset(asset.id)) throw EditError("duplicate or empty asset id");
    p.assets.push_back(std::move(asset));
}

void insertClip(Project& p, const Id& trackId, Clip clip) {
    Track& t = requireUnlocked(p, trackId);
    if (clip.id.empty() || p.findClip(clip.id)) throw EditError("duplicate or empty clip id");
    if (clip.speed.num <= 0 || clip.speed.den <= 0) throw EditError("speed must be positive");
    if (clip.timelineIn < Time::zero()) throw EditError("clip starts before 0");
    checkSourceRange(p, clip);
    if (!fits(p, clip, t.type)) throw EditError("asset does not fit this track type");
    if (clip.duration() < minDuration(p)) throw EditError("clip shorter than one frame");
    checkNoOverlap(t, clip.range(), {});
    t.clips.push_back(std::move(clip));
    t.sortClips();
}

void rippleDelete(Project& p, const Id& clipId) {
    Track* t = nullptr;
    const Clip c = requireClip(p, clipId, &t);
    const Time end = c.timelineOut(), len = c.duration();
    std::erase_if(t->clips, [&](const Clip& x) { return x.id == clipId; });
    // Every later clip moves by the same amount into the gap: order kept, no overlap possible.
    for (Clip& x : t->clips)
        if (x.timelineIn >= end) x.timelineIn = x.timelineIn - len;
}

void removeClip(Project& p, const Id& clipId) {
    Track* t = nullptr;
    requireClip(p, clipId, &t);
    std::erase_if(t->clips, [&](const Clip& c) { return c.id == clipId; });
}

void moveClip(Project& p, const Id& clipId, const Id& toTrackId, Time newTimelineIn) {
    Track* from = nullptr;
    Clip c = requireClip(p, clipId, &from);
    Track& to = requireUnlocked(p, toTrackId);
    if (newTimelineIn < Time::zero()) throw EditError("clip starts before 0");
    if (!fits(p, c, to.type)) throw EditError("asset does not fit this track type");
    c.timelineIn = newTimelineIn;
    checkNoOverlap(to, c.range(), clipId);
    std::erase_if(from->clips, [&](const Clip& x) { return x.id == clipId; });
    to.clips.push_back(std::move(c));
    to.sortClips();
}

void trimClip(Project& p, const Id& clipId, TrimEdge edge, Time t) {
    Track* owner = nullptr;
    Clip& orig = requireClip(p, clipId, &owner);
    Clip c = orig;
    t = t.snapToFrame(p.fps);
    if (edge == TrimEdge::Start) {
        if (t >= c.timelineOut()) throw EditError("trim start past clip end");
        const Time out = c.timelineOut();
        c.sourceIn = c.toSource(t);
        c.timelineIn = t;
        // keep the end edge fixed on the timeline
        c.sourceOut = c.toSource(out);
    } else {
        if (t <= c.timelineIn) throw EditError("trim end before clip start");
        c.sourceOut = c.toSource(t);
    }
    if (c.timelineIn < Time::zero()) throw EditError("clip starts before 0");
    checkSourceRange(p, c);
    if (c.duration() < minDuration(p)) throw EditError("clip shorter than one frame");
    checkNoOverlap(*owner, c.range(), clipId);
    orig = std::move(c);
    owner->sortClips();
}

void splitClip(Project& p, const Id& clipId, Time t, const Id& newClipId) {
    Track* owner = nullptr;
    Clip& left = requireClip(p, clipId, &owner);
    if (newClipId.empty() || p.findClip(newClipId)) throw EditError("duplicate or empty clip id");
    t = t.snapToFrame(p.fps);
    const Time one = minDuration(p);
    if (t < left.timelineIn + one || t > left.timelineOut() - one)
        throw EditError("split point must be inside the clip");
    Clip right = left;
    right.id = newClipId;
    const Time cutSource = left.toSource(t);
    right.timelineIn = t;
    right.sourceIn = cutSource;
    left.sourceOut = cutSource;
    // The left half keeps how the clip starts, the right half how it ends; the new cut has none.
    right.transitionIn = nullptr;
    left.transitionOut = nullptr;
    owner->clips.push_back(std::move(right));
    owner->sortClips();
}

void setClipVolume(Project& p, const Id& clipId, double volume) {
    if (volume < 0.0 || volume > 4.0) throw EditError("volume out of range");
    requireClip(p, clipId).volume = volume;
}

void setTrackFlags(Project& p, const Id& trackId, bool enabled, bool locked, bool muted) {
    Track& t = requireTrack(p, trackId);
    t.enabled = enabled;
    t.locked = locked;
    t.muted = muted;
}

}  // namespace vstudio::edit

namespace vstudio {

SnapResult snapTime(const Project& p, Time t, Time tolerance, const Id& excludeClip, std::optional<Time> playhead) {
    SnapResult r{t, false, t};
    Time best = tolerance + Time::fromTicks(1);
    auto consider = [&](Time target) {
        const Time d = target > t ? target - t : t - target;
        if (d < best) {
            best = d;
            r = {target, true, target};
        }
    };
    consider(Time::zero());
    if (playhead) consider(*playhead);
    for (const Track& tr : p.tracks) {
        const auto& cs = tr.clips;  // sorted, non-overlapping: starts and ends both ascend
        auto it = std::lower_bound(cs.begin(), cs.end(), t, [](const Clip& c, Time v) { return c.timelineIn < v; });
        // The nearest edges belong to the clips around the insertion point.
        const std::ptrdiff_t at = it - cs.begin(), n = static_cast<std::ptrdiff_t>(cs.size());
        for (std::ptrdiff_t k = std::max<std::ptrdiff_t>(0, at - 2); k < std::min(n, at + 2); ++k) {
            const Clip& c = cs[static_cast<std::size_t>(k)];
            if (c.id == excludeClip) continue;
            consider(c.timelineIn);
            consider(c.timelineOut());
        }
    }
    return r;
}

std::optional<Time> adjacentEditPoint(const Project& p, Time t, bool forward) {
    std::optional<Time> best;
    auto consider = [&](Time e) {
        if (forward ? e <= t : e >= t) return;
        if (!best || (forward ? e < *best : e > *best)) best = e;
    };
    for (const Track& tr : p.tracks) {
        const auto& cl = tr.clips;  // sorted by start, non-overlapping: ends are sorted too
        if (cl.empty()) continue;
        // First clip whose end is after t: its start or end is the next edge; the one before
        // it holds the previous edge.
        auto it = std::upper_bound(cl.begin(), cl.end(), t, [](Time v, const Clip& c) { return v < c.timelineOut(); });
        if (it != cl.end()) {
            consider(it->timelineIn);
            consider(it->timelineOut());
        }
        if (it != cl.begin()) {
            const Clip& prev = *std::prev(it);
            consider(prev.timelineIn);
            consider(prev.timelineOut());
        }
    }
    return best;
}

SnapResult snapClipMove(const Project& p, const Id& clipId, Time proposedStart, Time tolerance,
                        std::optional<Time> playhead) {
    const Track* tr = p.trackOfClip(clipId);
    const Clip* c = tr ? tr->findClip(clipId) : nullptr;
    if (!c) return {proposedStart, false, proposedStart};
    const Time len = c->duration();
    const SnapResult a = snapTime(p, proposedStart, tolerance, clipId, playhead);
    const SnapResult b = snapTime(p, proposedStart + len, tolerance, clipId, playhead);
    auto dist = [](Time x, Time y) { return x > y ? x - y : y - x; };
    const bool useEnd = b.snapped && (!a.snapped || dist(b.target, proposedStart + len) < dist(a.target, proposedStart));
    if (useEnd) return {b.target - len, true, b.target};
    if (a.snapped) return {std::max(Time::zero(), a.target), true, a.target};
    return {proposedStart, false, proposedStart};
}

}  // namespace vstudio
