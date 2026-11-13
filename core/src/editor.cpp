#include "vstudio/editor.hpp"

#include "vstudio/looks.hpp"
#include "vstudio/project_io.hpp"

namespace vstudio {

Editor::Editor(Project p) : project_(std::move(p)) {}

Project Editor::newProject(const std::string& name, int width, int height, Rational fps, const std::string& platform) {
    if (width <= 0 || height <= 0 || width % 2 || height % 2) throw EditError("canvas size must be positive and even");
    if (fps.num <= 0 || fps.den <= 0) throw EditError("invalid frame rate");
    Project p;
    p.id = newId();
    p.name = name;
    p.canvas = {width, height};
    p.fps = fps.reduced();
    p.createdByPlatform = platform;
    p.createdAt = p.modifiedAt = nowIso8601();
    return p;
}

void Editor::run(std::string label, EditCommand::Scope scope, std::vector<Id> tracks, std::function<void(Project&)> fn) {
    history_.execute(project_, std::make_unique<EditCommand>(std::move(label), scope, std::move(tracks), std::move(fn)));
}

Id Editor::addAsset(MediaAsset asset) {
    if (asset.id.empty()) asset.id = newId();
    const Id id = asset.id;
    run("Import media", EditCommand::Scope::AllTracksAndAssets, {},
        [a = std::move(asset)](Project& p) { edit::addAsset(p, a); });
    return id;
}

void Editor::relinkAsset(const Id& assetId, const std::string& uri, const std::string& fingerprint) {
    if (!project_.findAsset(assetId)) throw EditError("asset not found");
    if (uri.empty()) throw EditError("empty uri");
    run("Relink media", EditCommand::Scope::AllTracksAndAssets, {}, [=](Project& p) {
        for (auto& a : p.assets) {
            if (a.id != assetId) continue;
            a.uri = uri;
            if (!fingerprint.empty()) a.fingerprint = fingerprint;
        }
    });
}

Id Editor::addTrack(TrackType type, const std::string& name) {
    Track t;
    t.id = newId();
    t.type = type;
    t.name = name;
    const Id id = t.id;
    // Video tracks stack on top of other video tracks; audio tracks go last.
    std::size_t index = project_.tracks.size();
    if (type == TrackType::Video) {
        index = 0;
        for (std::size_t i = 0; i < project_.tracks.size(); ++i)
            if (project_.tracks[i].type == TrackType::Video) index = i + 1;
    }
    run("Add track", EditCommand::Scope::AllTracks, {}, [t, index](Project& p) { edit::addTrack(p, t, index); });
    return id;
}

void Editor::removeTrack(const Id& trackId) {
    run("Remove track", EditCommand::Scope::AllTracks, {}, [=](Project& p) { edit::removeTrack(p, trackId); });
}

void Editor::renameTrack(const Id& trackId, const std::string& name) {
    if (!project_.findTrack(trackId)) throw EditError("track not found");
    run("Rename track", EditCommand::Scope::Tracks, {trackId}, [=](Project& p) { p.findTrack(trackId)->name = name; });
}

void Editor::setTrackFlags(const Id& trackId, bool enabled, bool locked, bool muted) {
    const Track* t = project_.findTrack(trackId);
    if (!t) throw EditError("track not found");
    const char* label = t->locked != locked ? (locked ? "Lock track" : "Unlock track")
                        : t->muted != muted ? (muted ? "Mute track" : "Unmute track")
                                            : (enabled ? "Show track" : "Hide track");
    run(label, EditCommand::Scope::Tracks, {trackId},
        [=](Project& p) { edit::setTrackFlags(p, trackId, enabled, locked, muted); });
}

void Editor::setTrackVolume(const Id& trackId, double volume) {
    if (!project_.findTrack(trackId)) throw EditError("track not found");
    if (!(volume >= 0.0 && volume <= 4.0)) throw EditError("volume must be within 0..4");
    run("Track volume", EditCommand::Scope::Tracks, {trackId}, [=](Project& p) { p.findTrack(trackId)->volume = volume; });
}

void Editor::moveTrack(const Id& trackId, std::size_t indexInType) {
    const Track* t = project_.findTrack(trackId);
    if (!t) throw EditError("track not found");
    const TrackType type = t->type;
    run("Move track", EditCommand::Scope::AllTracks, {}, [=](Project& p) {
        // Positions (in p.tracks) of this type's tracks, in order; reorder within them only.
        std::vector<std::size_t> slots;
        std::vector<Track> same;
        for (std::size_t i = 0; i < p.tracks.size(); ++i)
            if (p.tracks[i].type == type) {
                slots.push_back(i);
                same.push_back(std::move(p.tracks[i]));
            }
        auto it = std::find_if(same.begin(), same.end(), [&](const Track& x) { return x.id == trackId; });
        Track moving = std::move(*it);
        same.erase(it);
        same.insert(same.begin() + static_cast<std::ptrdiff_t>(std::min(indexInType, same.size())), std::move(moving));
        for (std::size_t k = 0; k < slots.size(); ++k) p.tracks[slots[k]] = std::move(same[k]);
    });
}

Id Editor::insertClip(const Id& trackId, const Id& assetId, Time timelineIn, Time sourceIn, Time sourceOut) {
    Clip c;
    c.id = newId();
    c.assetId = assetId;
    c.timelineIn = timelineIn;
    c.sourceIn = sourceIn;
    c.sourceOut = sourceOut;
    const Id id = c.id;
    run("Insert clip", EditCommand::Scope::Tracks, {trackId}, [c, trackId](Project& p) { edit::insertClip(p, trackId, c); });
    return id;
}

Id Editor::appendClip(const Id& trackId, const Id& assetId, std::optional<Time> sourceIn, std::optional<Time> sourceOut) {
    const Track* t = project_.findTrack(trackId);
    const MediaAsset* a = project_.findAsset(assetId);
    if (!t) throw EditError("track not found");
    if (!a) throw EditError("asset not found");
    const Time in = sourceIn.value_or(Time::zero());
    // Still images default to 5 seconds.
    const Time out = sourceOut.value_or(a->type == MediaType::Image ? Time::fromSeconds(5) : a->duration);
    return insertClip(trackId, assetId, t->end(), in, out);
}

void Editor::removeClip(const Id& clipId) {
    const Track* t = project_.trackOfClip(clipId);
    if (!t) throw EditError("clip not found");
    run("Delete clip", EditCommand::Scope::Tracks, {t->id}, [clipId](Project& p) { edit::removeClip(p, clipId); });
}

void Editor::rippleDelete(const Id& clipId) {
    const Track* t = project_.trackOfClip(clipId);
    if (!t) throw EditError("clip not found");
    run("Ripple delete", EditCommand::Scope::Tracks, {t->id}, [clipId](Project& p) { edit::rippleDelete(p, clipId); });
}

void Editor::moveClip(const Id& clipId, const Id& toTrackId, Time timelineIn) {
    const Track* t = project_.trackOfClip(clipId);
    if (!t) throw EditError("clip not found");
    std::vector<Id> scope{t->id};
    if (toTrackId != t->id) scope.push_back(toTrackId);
    run("Move clip", EditCommand::Scope::Tracks, scope,
        [=](Project& p) { edit::moveClip(p, clipId, toTrackId, timelineIn); });
}

void Editor::trimClip(const Id& clipId, TrimEdge edge, Time t) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    run("Trim clip", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) { edit::trimClip(p, clipId, edge, t); });
}

Id Editor::splitClip(const Id& clipId, Time t) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    const Id right = newId();
    run("Split clip", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) { edit::splitClip(p, clipId, t, right); });
    return right;
}

int Editor::splitAt(Time t) {
    std::vector<std::pair<Id, Id>> cuts;  // clip -> new id
    std::vector<Id> tracks;
    const Time snapped = t.snapToFrame(project_.fps);
    const Time one = Time::fromFrame(1, project_.fps);
    for (const auto& tr : project_.tracks) {
        if (tr.locked) continue;
        const Clip* c = tr.clipAt(snapped);
        if (!c || snapped < c->timelineIn + one || snapped > c->timelineOut() - one) continue;
        cuts.emplace_back(c->id, newId());
        tracks.push_back(tr.id);
    }
    if (cuts.empty()) return 0;
    run("Split", EditCommand::Scope::Tracks, tracks, [cuts, snapped](Project& p) {
        for (const auto& [cid, nid] : cuts) edit::splitClip(p, cid, snapped, nid);
    });
    return static_cast<int>(cuts.size());
}

void Editor::setClipVolume(const Id& clipId, double v) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    run("Volume", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) { edit::setClipVolume(p, clipId, v); });
}

void Editor::setClipTransform(const Id& clipId, const Json& tf) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    if (!tf.is_object()) throw EditError("transform must be an object");
    run("Transform", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        Track* t = p.trackOfClip(clipId);
        if (t->locked) throw EditError("track is locked");
        t->findClip(clipId)->transform = tf;
    });
}

void Editor::addClipEffect(const Id& clipId, const std::string& type, const Json& params) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    const Json fx{{"effect_id", newId()}, {"type", type}, {"enabled", true}, {"params", params}};
    run("Add effect", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        Track* t = p.trackOfClip(clipId);
        if (t->locked) throw EditError("track is locked");
        t->findClip(clipId)->effects.push_back(fx);
    });
}

void Editor::setClipLook(const std::vector<Id>& clipIds, const std::string& lookId, double strength) {
    if (clipIds.empty()) throw EditError("no clip selected");
    const ColorLook* look = lookId.empty() ? nullptr : findColorLook(lookId);
    if (!lookId.empty() && !look) throw EditError("unknown look: " + lookId);
    std::vector<Id> tracks;
    for (const Id& c : clipIds) {
        const Track* t = project_.trackOfClip(c);
        if (!t) throw EditError("clip not found");
        if (t->locked) throw EditError("track is locked: " + t->name);
        if (std::find(tracks.begin(), tracks.end(), t->id) == tracks.end()) tracks.push_back(t->id);
    }
    Json fx;
    if (look) {
        Json params = look->params;
        params["look"] = look->id;
        params["strength"] = std::clamp(strength, 0.0, 1.0);
        fx = Json{{"effect_id", newId()}, {"type", kColorLookEffect}, {"enabled", true}, {"params", params}};
    }
    run(look ? "Apply look" : "Remove look", EditCommand::Scope::Tracks, tracks, [=](Project& p) {
        for (const Id& c : clipIds) {
            Clip* clip = p.trackOfClip(c)->findClip(c);
            if (!clip->effects.is_array()) clip->effects = Json::array();
            Json kept = Json::array();
            for (const auto& e : clip->effects)
                if (!(e.is_object() && e.value("type", std::string{}) == kColorLookEffect)) kept.push_back(e);
            if (!fx.is_null()) kept.push_back(fx);
            clip->effects = std::move(kept);
        }
    });
}

bool Editor::undo() { return history_.undo(project_); }
bool Editor::redo() { return history_.redo(project_); }

std::shared_ptr<const Project> Editor::snapshot() {
    if (!snapshot_ || snapshotRevision_ != history_.revision()) {
        snapshot_ = std::make_shared<const Project>(project_);
        snapshotRevision_ = history_.revision();
    }
    return snapshot_;
}

}  // namespace vstudio
