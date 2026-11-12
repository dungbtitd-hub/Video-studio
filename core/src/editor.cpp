#include "vstudio/editor.hpp"

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
