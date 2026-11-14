#include "vstudio/editor.hpp"
#include "vstudio/audio_fx.hpp"
#include "vstudio/video_fx.hpp"

#include <cmath>
#include <limits>

#include "vstudio/color_space.hpp"
#include "vstudio/cover.hpp"
#include "vstudio/grade.hpp"
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

namespace {

Time absTime(Time t) { return Time::fromTicks(t.ticks() < 0 ? -t.ticks() : t.ticks()); }

// Half a frame at the project rate: keys closer than this to `t` are "at" t.
Time keyTolerance(const Project& p) { return Time::fromTicks(Time::fromFrame(1, p.fps).ticks() / 2); }

Clip& editableClip(Project& p, const Id& clipId) {
    Track* t = p.trackOfClip(clipId);
    if (!t) throw EditError("clip not found");
    if (t->locked) throw EditError("track is locked: " + t->name);
    return *t->findClip(clipId);
}

void putKeyIn(Json& keys, const std::string& prop, Keyframe k, Time tol) {
    if (!keys.is_object()) keys = Json::object();
    Json& arr = keys[prop];
    if (!arr.is_array()) arr = Json::array();
    Json out = Json::array();
    bool placed = false;
    for (const auto& j : arr) {
        const Time t = Time::fromTicks(j.value("t", std::int64_t{0}));
        if (absTime(t - k.t) <= tol) continue;  // replaced
        if (!placed && k.t < t) {
            out.push_back(keyframeJson(k));
            placed = true;
        }
        out.push_back(j);
    }
    if (!placed) out.push_back(keyframeJson(k));
    arr = std::move(out);
}

void putKey(Clip& c, const std::string& prop, Keyframe k, Time tol) { putKeyIn(c.keyframes, prop, k, tol); }

Json& coverOf(Clip& c, const Id& effectId) {
    if (c.effects.is_array())
        for (auto& e : c.effects)
            if (e.is_object() && e.value("effect_id", std::string{}) == effectId && e.value("type", std::string{}) == kCoverEffect)
                return e;
    throw EditError("cover not found");
}

double clampCover(const std::string& prop, double v) {
    if (prop == "w" || prop == "h") return std::clamp(v, 0.005, 3.0);
    if (prop == "x" || prop == "y") return std::clamp(v, -1.0, 2.0);
    if (prop == "amount") return std::clamp(v, 0.0, 1.0);
    return v;  // rotation
}

}  // namespace

void Editor::setKeyframes(const Id& clipId, Time sourceTime, const std::map<std::string, double>& values, Ease ease) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    const Clip* c = tr->findClip(clipId);
    if (sourceTime < c->sourceIn || sourceTime > c->sourceOut) throw EditError("keyframe outside the clip");
    for (const auto& [prop, v] : values)
        if (!kf::isProperty(prop)) throw EditError("not an animatable property: " + prop);
    if (values.empty()) return;
    const Time tol = keyTolerance(project_);
    run("Keyframe", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        Clip& clip = editableClip(p, clipId);
        for (const auto& [prop, v] : values) putKey(clip, prop, Keyframe{sourceTime, kf::clampValue(prop, v), ease}, tol);
    });
}

void Editor::addKeyframe(const Id& clipId, kf::Group group, Time sourceTime) {
    const Clip* c = project_.findClip(clipId);
    if (!c) throw EditError("clip not found");
    std::map<std::string, double> values;
    const Transform t = animatedTransform(*c, sourceTime);
    for (const auto& prop : kf::properties(group)) {
        if (prop == kf::kScale) values[prop] = t.scale;
        else if (prop == kf::kX) values[prop] = t.x;
        else if (prop == kf::kY) values[prop] = t.y;
        else if (prop == kf::kOpacity) values[prop] = t.opacity;
        else if (prop == kf::kVolume) values[prop] = keyframeValue(*c, prop, sourceTime).value_or(1.0);
    }
    setKeyframes(clipId, sourceTime, values);
}

bool Editor::removeKeyframe(const Id& clipId, kf::Group group, Time sourceTime) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    const Clip* c = tr->findClip(clipId);
    const Time tol = keyTolerance(project_);
    bool found = false;
    for (const auto& prop : kf::properties(group))
        if (c->keyframes.is_object() && c->keyframes.contains(prop)) {
            const KeyframeCurve curve(c->keyframes[prop]);
            for (const auto& k : curve.keys()) found |= absTime(k.t - sourceTime) <= tol;
        }
    if (!found) return false;
    run("Remove keyframe", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        Clip& clip = editableClip(p, clipId);
        for (const auto& prop : kf::properties(group)) {
            if (!clip.keyframes.contains(prop)) continue;
            Json kept = Json::array();
            for (const auto& j : clip.keyframes[prop])
                if (absTime(Time::fromTicks(j.value("t", std::int64_t{0})) - sourceTime) > tol) kept.push_back(j);
            if (kept.empty()) clip.keyframes.erase(prop);
            else clip.keyframes[prop] = std::move(kept);
        }
    });
    return true;
}

void Editor::clearKeyframes(const Id& clipId, kf::Group group) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    run("Clear keyframes", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        Clip& clip = editableClip(p, clipId);
        for (const auto& prop : kf::properties(group)) clip.keyframes.erase(prop);
    });
}

namespace {

// What setPropertyAt changes: the static transform/volume, or keys where the property is animated.
struct PropertyChange {
    Json transform;
    double volume = 1;
    bool staticChange = false;
    std::map<std::string, double> keyed;
};

PropertyChange propertyChange(const Clip& c, Time sourceTime, const std::map<std::string, double>& values) {
    PropertyChange ch;
    ch.transform = c.transform.is_object() ? c.transform : Json::object();
    ch.volume = c.volume;
    for (const auto& [prop, v] : values) {
        if (!kf::isProperty(prop)) throw EditError("not an animatable property: " + prop);
        if (keyframeValue(c, prop, sourceTime)) {
            // Keyed volume multiplies the clip volume: store the factor that gives `v`.
            ch.keyed[prop] = prop == kf::kVolume ? v / std::max(c.volume, 1e-6) : v;
        } else if (prop == kf::kVolume) {
            ch.volume = kf::clampValue(prop, v);
            ch.staticChange = true;
        } else {
            ch.transform[prop] = kf::clampValue(prop, v);
            ch.staticChange = true;
        }
    }
    return ch;
}

void applyPropertyChange(Clip& clip, const PropertyChange& ch, Time sourceTime, Time tol) {
    if (ch.staticChange) {
        clip.transform = ch.transform;
        clip.volume = ch.volume;
    }
    for (const auto& [prop, v] : ch.keyed) putKey(clip, prop, Keyframe{sourceTime, kf::clampValue(prop, v), Ease::Smooth}, tol);
}

}  // namespace

void Editor::setPropertyAt(const Id& clipId, Time sourceTime, const std::map<std::string, double>& values) {
    const Clip* c = project_.findClip(clipId);
    if (!c) throw EditError("clip not found");
    const PropertyChange ch = propertyChange(*c, sourceTime, values);
    const Track* tr = project_.trackOfClip(clipId);
    run("Adjust", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        applyPropertyChange(editableClip(p, clipId), ch, sourceTime, keyTolerance(p));
    });
}

void Editor::setOverlayRegion(const Id& clipId, Time sourceTime, double x, double y, double w, double h) {
    const Clip* c = project_.findClip(clipId);
    if (!c) throw EditError("clip not found");
    if (!c->generated() || c->content.value("kind", std::string{}) != "overlay") throw EditError("not an overlay");
    if (w < 1 || h < 1) throw EditError("empty region");
    const Transform tf = animatedTransform(*c, sourceTime);
    const double s = std::max(tf.scale, 1e-6);
    const PropertyChange ch = propertyChange(*c, sourceTime,
                                             {{kf::kX, x + w / 2 - project_.canvas.width / 2.0},
                                              {kf::kY, y + h / 2 - project_.canvas.height / 2.0}});
    const Json patch{{"w", w / s}, {"h", h / s}};
    const Track* tr = project_.trackOfClip(clipId);
    run("Overlay region", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        Clip& clip = editableClip(p, clipId);
        clip.content.merge_patch(patch);
        applyPropertyChange(clip, ch, sourceTime, keyTolerance(p));
    });
}

Id Editor::addCover(const Id& clipId, const Json& params) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    if (tr->type != TrackType::Video) throw EditError("covers need a picture");
    Json p = defaultCover(params.value("style", std::string("blur")), params.value("shape", std::string("rect")));
    if (params.is_object())
        for (auto it = params.begin(); it != params.end(); ++it) p[it.key()] = it.value();
    if (!findCoverStyle(p.value("style", std::string{}))) throw EditError("unknown cover style");
    if (!isCoverShape(p.value("shape", std::string{}))) throw EditError("unknown cover shape");
    const Id id = newId();
    run("Add cover", EditCommand::Scope::Tracks, {tr->id}, [=](Project& pr) {
        Clip& clip = editableClip(pr, clipId);
        if (!clip.effects.is_array()) clip.effects = Json::array();
        clip.effects.push_back(Json{{"effect_id", id}, {"type", kCoverEffect}, {"enabled", true}, {"params", p}});
    });
    return id;
}

const Json* Editor::coverParams(const Id& clipId, const Id& effectId) const {
    const Clip* c = project_.findClip(clipId);
    if (!c || !c->effects.is_array()) return nullptr;
    for (const auto& e : c->effects)
        if (e.is_object() && e.value("effect_id", std::string{}) == effectId && e.value("type", std::string{}) == kCoverEffect)
            return &e["params"];
    return nullptr;
}

void Editor::updateCover(const Id& clipId, const Id& effectId, const Json& patch) {
    if (!coverParams(clipId, effectId)) throw EditError("cover not found");
    if (patch.contains("style") && !findCoverStyle(patch.value("style", std::string{}))) throw EditError("unknown cover style");
    if (patch.contains("shape") && !isCoverShape(patch.value("shape", std::string{}))) throw EditError("unknown cover shape");
    const Track* tr = project_.trackOfClip(clipId);
    run("Edit cover", EditCommand::Scope::Tracks, {tr->id}, [=](Project& pr) {
        Json& params = coverOf(editableClip(pr, clipId), effectId)["params"];
        for (auto it = patch.begin(); it != patch.end(); ++it) {
            if (it.value().is_null()) params.erase(it.key());
            else params[it.key()] = it.value();
        }
    });
}

void Editor::removeCover(const Id& clipId, const Id& effectId) {
    if (!coverParams(clipId, effectId)) throw EditError("cover not found");
    const Track* tr = project_.trackOfClip(clipId);
    run("Remove cover", EditCommand::Scope::Tracks, {tr->id}, [=](Project& pr) {
        Clip& clip = editableClip(pr, clipId);
        Json kept = Json::array();
        for (const auto& e : clip.effects)
            if (!(e.is_object() && e.value("effect_id", std::string{}) == effectId)) kept.push_back(e);
        clip.effects = std::move(kept);
    });
}

void Editor::setCoverAt(const Id& clipId, const Id& effectId, Time sourceTime, const std::map<std::string, double>& values) {
    const Json* params = coverParams(clipId, effectId);
    if (!params) throw EditError("cover not found");
    for (const auto& [prop, v] : values) {
        (void)v;
        if (std::find(coverAnimatable().begin(), coverAnimatable().end(), prop) == coverAnimatable().end())
            throw EditError("not a cover property: " + prop);
    }
    const bool follows = params->contains("anim") && (*params)["anim"].is_object() && !(*params)["anim"].empty();
    const Track* tr = project_.trackOfClip(clipId);
    run(follows ? "Move cover (key)" : "Move cover", EditCommand::Scope::Tracks, {tr->id}, [=](Project& pr) {
        Json& p = coverOf(editableClip(pr, clipId), effectId)["params"];
        const Time tol = keyTolerance(pr);
        for (const auto& [prop, v] : values) {
            if (follows && p["anim"].contains(prop)) putKeyIn(p["anim"], prop, Keyframe{sourceTime, clampCover(prop, v), Ease::Smooth}, tol);
            else p[prop] = clampCover(prop, v);
        }
    });
}

void Editor::keyCover(const Id& clipId, const Id& effectId, Time sourceTime) {
    const Json* params = coverParams(clipId, effectId);
    if (!params) throw EditError("cover not found");
    const Json now = coverParamsAt(*params, sourceTime);
    const Track* tr = project_.trackOfClip(clipId);
    run("Follow motion", EditCommand::Scope::Tracks, {tr->id}, [=](Project& pr) {
        Json& p = coverOf(editableClip(pr, clipId), effectId)["params"];
        const Time tol = keyTolerance(pr);
        for (const char* prop : {"x", "y", "w", "h"})
            putKeyIn(p["anim"], prop, Keyframe{sourceTime, now.value(prop, 0.0), Ease::Smooth}, tol);
    });
}

void Editor::clearCoverKeys(const Id& clipId, const Id& effectId, Time sourceTime) {
    const Json* params = coverParams(clipId, effectId);
    if (!params) throw EditError("cover not found");
    const Json now = coverParamsAt(*params, sourceTime);
    const Track* tr = project_.trackOfClip(clipId);
    run("Stop following", EditCommand::Scope::Tracks, {tr->id}, [=](Project& pr) {
        Json& p = coverOf(editableClip(pr, clipId), effectId)["params"];
        for (const auto& prop : coverAnimatable())
            if (now.contains(prop)) p[prop] = now[prop];
        p.erase("anim");
    });
}

void Editor::setCoverTrack(const Id& clipId, const Id& effectId,
                           const std::vector<std::pair<Time, std::array<double, 4>>>& boxes) {
    if (!coverParams(clipId, effectId)) throw EditError("cover not found");
    if (boxes.empty()) return;
    const Track* tr = project_.trackOfClip(clipId);
    run("Track cover", EditCommand::Scope::Tracks, {tr->id}, [=](Project& pr) {
        Json& p = coverOf(editableClip(pr, clipId), effectId)["params"];
        if (!p["anim"].is_object()) p["anim"] = Json::object();
        const Time a = boxes.front().first, b = boxes.back().first, tol = keyTolerance(pr);
        const char* props[] = {"x", "y", "w", "h"};
        for (int k = 0; k < 4; ++k) {
            Json kept = Json::array();
            if (p["anim"][props[k]].is_array())
                for (const auto& j : p["anim"][props[k]]) {
                    const Time t = Time::fromTicks(j.value("t", std::int64_t{0}));
                    if (t < a - tol || t > b + tol) kept.push_back(j);
                }
            p["anim"][props[k]] = kept;
            for (const auto& [t, box] : boxes)
                putKeyIn(p["anim"], props[k], Keyframe{t, clampCover(props[k], box[static_cast<std::size_t>(k)]), Ease::Linear}, tol);
        }
    });
}

namespace {
Json transitionJson(const std::string& type, Time duration) {
    if (type.empty()) return nullptr;
    if (!findTransition(type)) throw EditError("unknown transition: " + type);
    const Time d = std::clamp(duration, minTransitionDuration(), maxTransitionDuration());
    return Json{{"type", type}, {"duration", d.ticks()}};
}
}  // namespace

void Editor::setTransition(const Id& clipId, TransitionEdge edge, const std::string& type, Time duration) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    const Json value = transitionJson(type, duration);
    Id owner = clipId;
    if (edge == TransitionEdge::Out) {
        // A clip's end that touches the next clip is the cut: it belongs to the next clip's start.
        for (const auto& cp : cutPoints(*tr))
            if (cp.from && cp.from->id == clipId && cp.to) {
                owner = cp.to->id;
                edge = TransitionEdge::In;
            }
    }
    run(type.empty() ? "Remove transition" : "Transition", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        Clip& c = editableClip(p, owner);
        (edge == TransitionEdge::In ? c.transitionIn : c.transitionOut) = value;
    });
}

int Editor::setTransitionEverywhere(const std::string& type, Time duration) {
    const Json value = transitionJson(type, duration);
    std::vector<Id> tracks, clips;
    for (const Track& tr : project_.tracks) {
        if (tr.type != TrackType::Video || tr.locked) continue;
        bool any = false;
        for (const auto& cp : cutPoints(tr))
            if (cp.from && cp.to) {
                clips.push_back(cp.to->id);
                any = true;
            }
        if (any) tracks.push_back(tr.id);
    }
    if (clips.empty()) return 0;
    run(type.empty() ? "Remove transitions" : "Transitions everywhere", EditCommand::Scope::Tracks, tracks,
        [=](Project& p) {
            for (const Id& id : clips) editableClip(p, id).transitionIn = value;
        });
    return static_cast<int>(clips.size());
}

Id Editor::addGeneratedClip(const Json& content, Time at, Time duration, const Id& trackId) {
    if (!content.is_object() || !content.contains("kind")) throw EditError("content needs a kind");
    if (duration <= Time::zero()) throw EditError("empty clip");
    Clip c;
    c.id = newId();
    c.timelineIn = std::max(Time::zero(), at).snapToFrame(project_.fps);
    c.sourceIn = Time::zero();
    c.sourceOut = duration.snapToFrame(project_.fps);
    c.content = content;
    const std::string kind = content.value("kind", std::string{});
    if (kind == "overlay") {  // covers the whole frame unless a region is given
        if (!c.content.contains("w")) c.content["w"] = project_.canvas.width;
        if (!c.content.contains("h")) c.content["h"] = project_.canvas.height;
        if (!c.content.contains("ref")) c.content["ref"] = project_.canvas.height;
    }
    const Id id = c.id;
    Id target = trackId;
    std::size_t newIndex = 0;
    std::string newName;
    if (target.empty()) {
        // Topmost overlay track (not the bottom video track) with room here.
        std::size_t firstVideo = project_.tracks.size(), lastVideo = 0;
        for (std::size_t i = 0; i < project_.tracks.size(); ++i)
            if (project_.tracks[i].type == TrackType::Video) {
                firstVideo = std::min(firstVideo, i);
                lastVideo = i;
            }
        for (std::size_t i = project_.tracks.size(); i-- > 0;) {
            const Track& t = project_.tracks[i];
            if (t.type != TrackType::Video || t.locked || i == firstVideo) continue;
            const bool free = std::none_of(t.clips.begin(), t.clips.end(), [&](const Clip& x) { return x.range().overlaps(c.range()); });
            if (free) {
                target = t.id;
                break;
            }
        }
        if (target.empty()) {
            target = newId();
            newIndex = firstVideo < project_.tracks.size() ? lastVideo + 1 : 0;
            newName = kind == "text" ? "Chữ" : kind == "overlay" ? "Lớp phủ" : "Sticker";
        }
    }
    run(kind == "text" ? "Add text" : kind == "overlay" ? "Add overlay" : "Add sticker", EditCommand::Scope::AllTracks, {},
        [=](Project& p) {
            if (!newName.empty()) {
                Track t;
                t.id = target;
                t.type = TrackType::Video;
                t.name = newName;
                edit::addTrack(p, t, newIndex);
            }
            edit::insertClip(p, target, c);
        });
    return id;
}

void Editor::updateClipContent(const Id& clipId, const Json& patch) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    if (!tr->findClip(clipId)->generated()) throw EditError("not a generated clip");
    run("Edit content", EditCommand::Scope::Tracks, {tr->id}, [=](Project& p) {
        Clip& c = editableClip(p, clipId);
        c.content.merge_patch(patch);
        if (!c.generated()) throw EditError("content needs a kind");
    });
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

void Editor::setClipEffect(const std::vector<Id>& clipIds, const std::string& type, const Json& params, bool first,
                           const std::string& label) {
    if (clipIds.empty()) throw EditError("no clip selected");
    std::vector<Id> tracks;
    for (const Id& c : clipIds) {
        const Track* t = project_.trackOfClip(c);
        if (!t) throw EditError("clip not found");
        if (t->locked) throw EditError("track is locked: " + t->name);
        if (std::find(tracks.begin(), tracks.end(), t->id) == tracks.end()) tracks.push_back(t->id);
    }
    Json fx;
    if (!params.is_null()) fx = Json{{"effect_id", newId()}, {"type", type}, {"enabled", true}, {"params", params}};
    run(label, EditCommand::Scope::Tracks, tracks, [=](Project& p) {
        for (const Id& c : clipIds) {
            Clip* clip = p.trackOfClip(c)->findClip(c);
            if (!clip->effects.is_array()) clip->effects = Json::array();
            Json kept = Json::array();
            if (first && !fx.is_null()) kept.push_back(fx);
            for (const auto& e : clip->effects)
                if (!(e.is_object() && e.value("type", std::string{}) == type)) kept.push_back(e);
            if (!first && !fx.is_null()) kept.push_back(fx);
            clip->effects = std::move(kept);
        }
    });
}

void Editor::setClipVideoFx(const std::vector<Id>& clipIds, const std::string& group, const std::string& fxId,
                            double amount, double speed, double line) {
    const std::string type = videofx::effectTypeFor(group);
    if (type.empty()) throw EditError("unknown effect group: " + group);
    Json params;
    if (!fxId.empty()) {
        const videofx::VideoFxInfo* fx = videofx::findVideoFx(fxId);
        if (!fx || fx->group != group) throw EditError("unknown " + group + " effect: " + fxId);
        params = {{"fx", fxId}, {"amount", std::clamp(amount, 0.0, 2.0)}, {"speed", std::clamp(speed, 0.25, 4.0)}};
        if (fx->usesLine && line >= 0) params["line"] = std::clamp(line, 0.1, 0.9);
    }
    if (clipIds.empty()) throw EditError("no clip selected");
    std::vector<Id> tracks;
    for (const Id& c : clipIds) {
        const Track* t = project_.trackOfClip(c);
        if (!t) throw EditError("clip not found");
        if (t->locked) throw EditError("track is locked: " + t->name);
        if (std::find(tracks.begin(), tracks.end(), t->id) == tracks.end()) tracks.push_back(t->id);
    }
    Json fx;
    if (!params.is_null()) fx = Json{{"effect_id", newId()}, {"type", type}, {"enabled", true}, {"params", params}};
    run(fx.is_null() ? "Remove video effect" : "Video effect", EditCommand::Scope::Tracks, tracks, [=](Project& p) {
        // Geometry last, in a fixed order, so colour and covers stay on their subject.
        static const std::vector<std::string> order = {videofx::kZoomEffect, videofx::kRotateEffect,
                                                       videofx::kReflectEffect};
        auto rank = [&](const Json& e) {
            return std::find(order.begin(), order.end(), e.value("type", std::string{})) - order.begin();
        };
        for (const Id& c : clipIds) {
            Clip* clip = p.trackOfClip(c)->findClip(c);
            Json others = Json::array(), geo = Json::array();
            if (clip->effects.is_array())
                for (const auto& e : clip->effects) {
                    if (!e.is_object()) continue;
                    const std::string t = e.value("type", std::string{});
                    if (t == type) continue;  // replaced (or removed)
                    (rank(e) < static_cast<std::ptrdiff_t>(order.size()) ? geo : others).push_back(e);
                }
            if (!fx.is_null()) geo.push_back(fx);
            std::stable_sort(geo.begin(), geo.end(), [&](const Json& x, const Json& y) { return rank(x) < rank(y); });
            for (auto& e : geo) others.push_back(e);
            clip->effects = std::move(others);
        }
    });
}

void Editor::setClipLook(const std::vector<Id>& clipIds, const std::string& lookId, double strength) {
    const ColorLook* look = lookId.empty() ? nullptr : findColorLook(lookId);
    if (!lookId.empty() && !look) throw EditError("unknown look: " + lookId);
    Json params;
    if (look) {
        params = look->params;
        params["look"] = look->id;
        params["strength"] = std::clamp(strength, 0.0, 1.0);
    }
    setClipEffect(clipIds, kColorLookEffect, params, false, look ? "Apply look" : "Remove look");
}

void Editor::setClipColorSpace(const std::vector<Id>& clipIds, const std::string& input, double exposure) {
    if (!input.empty() && !findLogFormat(input)) throw EditError("unknown Log/HDR format: " + input);
    Json params;
    if (!input.empty()) params = Json{{"input", input}, {"exposure", std::clamp(exposure, -4.0, 4.0)}, {"highlights", true}};
    setClipEffect(clipIds, kColorSpaceEffect, params, true, input.empty() ? "Remove Log conversion" : "Convert Log to Rec.709");
}

void Editor::setClipLut(const std::vector<Id>& clipIds, const std::string& path, double strength) {
    Json params;
    if (!path.empty()) params = Json{{"path", path}, {"strength", std::clamp(strength, 0.0, 1.0)}};
    setClipEffect(clipIds, kLutEffect, params, false, path.empty() ? "Remove LUT" : "Apply LUT");
}

void Editor::setClipGrade(const std::vector<Id>& clipIds, const Json& params) {
    if (clipIds.empty()) throw EditError("no clip selected");
    if (!params.is_null() && !params.is_object()) throw EditError("grade parameters must be an object");
    std::vector<Id> tracks;
    for (const Id& c : clipIds) {
        const Track* t = project_.trackOfClip(c);
        if (!t) throw EditError("clip not found");
        if (t->locked) throw EditError("track is locked: " + t->name);
        if (std::find(tracks.begin(), tracks.end(), t->id) == tracks.end()) tracks.push_back(t->id);
    }
    Json p = params;
    if (p.is_object()) p.erase("show_mask");
    if (p.is_object())
        if (auto q = p.find("qualifier"); q != p.end() && q->is_object()) q->erase("show_mask");  // preview helper only
    const bool remove = p.is_null() || isNeutralGrade(p);
    const Id effectId = newId();
    run(remove ? "Reset grade" : "Grade colour", EditCommand::Scope::Tracks, tracks, [=](Project& pr) {
        for (const Id& c : clipIds) {
            Clip* clip = pr.trackOfClip(c)->findClip(c);
            clip->effects = withGradeEffect(clip->effects, remove ? Json() : p, effectId);
        }
    });
}

bool Editor::undo() { return history_.undo(project_); }
bool Editor::redo() { return history_.redo(project_); }

std::shared_ptr<const Project> Editor::preview(const std::function<void(Editor&)>& edit) const {
    Editor scratch(project_);
    try {
        edit(scratch);
    } catch (const std::exception&) {
        return nullptr;
    }
    return std::make_shared<const Project>(std::move(scratch.project_));
}

std::shared_ptr<const Project> Editor::snapshot() {
    if (!snapshot_ || snapshotRevision_ != history_.revision()) {
        snapshot_ = std::make_shared<const Project>(project_);
        snapshotRevision_ = history_.revision();
    }
    return snapshot_;
}


// ---------------------------------------------------------------- subtitles

namespace {

std::size_t topVideoIndex(const Project& p) {
    std::size_t idx = 0;
    for (std::size_t i = 0; i < p.tracks.size(); ++i)
        if (p.tracks[i].type == TrackType::Video) idx = i + 1;
    return idx;
}

Json subtitleSettingsFor(const Project& p, const Json& patch) {
    Json set = subtitles::defaultTrackSettings();
    // 5.2 % of the short side: ~56 px on 1080p, as large on a vertical 1080x1920 video.
    set["style"]["size"] = std::round(std::min(p.canvas.width, p.canvas.height) * 0.052);
    if (patch.is_object()) set.merge_patch(patch);
    return set;
}

Track& requireSubtitleTrack(Project& p, const Id& trackId) {
    Track* t = p.findTrack(trackId);
    if (!t || !subtitles::isSubtitleTrack(*t)) throw EditError("not a subtitle track");
    if (t->locked) throw EditError("track is locked: " + t->name);
    return *t;
}

Clip cueClip(const Project& p, Time start, Time end, const std::string& text) {
    Clip c;
    c.id = newId();
    c.timelineIn = start;
    c.sourceIn = Time::zero();
    c.sourceOut = std::max(Time::fromFrame(1, p.fps), end - start);
    c.content = subtitles::cueContent(text);
    return c;
}

}  // namespace

Id Editor::addSubtitleTrack(const std::string& name, const Json& settings) {
    Track t;
    t.id = newId();
    t.type = TrackType::Video;
    t.name = name.empty() ? "Phụ đề" : name;
    t.extra[subtitles::kExtraKey] = subtitleSettingsFor(project_, settings);
    const Id id = t.id;
    const std::size_t index = topVideoIndex(project_);
    run("Add subtitle track", EditCommand::Scope::AllTracks, {}, [t, index](Project& p) { edit::addTrack(p, t, index); });
    return id;
}

Id Editor::addSubtitle(const Id& trackId, Time at, const std::string& text, Time duration) {
    const Time frame = Time::fromFrame(1, project_.fps);
    Id target = trackId;
    if (target.empty()) {
        for (std::size_t i = project_.tracks.size(); i-- > 0;)
            if (subtitles::isSubtitleTrack(project_.tracks[i]) && !project_.tracks[i].locked) {
                target = project_.tracks[i].id;
                break;
            }
    }
    const Track* existing = target.empty() ? nullptr : project_.findTrack(target);
    if (!target.empty() && (!existing || !subtitles::isSubtitleTrack(*existing))) throw EditError("not a subtitle track");
    if (existing && existing->locked) throw EditError("track is locked: " + existing->name);
    Time start = std::max(Time::zero(), at).snapToFrame(project_.fps);
    Time limit = Time::fromTicks(std::numeric_limits<std::int64_t>::max() / 4);
    if (existing) {
        if (const Clip* under = existing->clipAt(start)) start = under->timelineOut();
        for (const auto& c : existing->clips)
            if (c.timelineIn >= start) limit = std::min(limit, c.timelineIn);
    }
    const Time end = std::min(limit, (start + std::max(duration, frame)).snapToFrame(project_.fps));
    if (end - start < frame) throw EditError("Không còn chỗ cho dòng phụ đề ở đây");
    const Clip c = cueClip(project_, start, end, text);
    const Id id = c.id;
    if (existing) {
        run("Add subtitle", EditCommand::Scope::Tracks, {target}, [=](Project& p) { edit::insertClip(p, target, c); });
        return id;
    }
    Track t;
    t.id = newId();
    t.type = TrackType::Video;
    t.name = "Phụ đề";
    t.extra[subtitles::kExtraKey] = subtitleSettingsFor(project_, Json::object());
    const std::size_t index = topVideoIndex(project_);
    run("Add subtitle", EditCommand::Scope::AllTracks, {}, [=](Project& p) {
        edit::addTrack(p, t, index);
        edit::insertClip(p, t.id, c);
    });
    return id;
}

Id Editor::importSubtitles(const std::vector<subtitles::Cue>& cues, const std::string& name, const std::string& lang,
                           Time offset) {
    std::vector<subtitles::Cue> shifted = cues;
    for (auto& c : shifted) {
        c.start += offset;
        c.end += offset;
    }
    shifted = subtitles::normalise(std::move(shifted), project_.fps);
    if (shifted.empty()) throw EditError("Tệp phụ đề không có dòng nào");
    Track t;
    t.id = newId();
    t.type = TrackType::Video;
    t.name = name.empty() ? "Phụ đề" : name;
    Json patch = Json::object();
    if (!lang.empty()) patch["lang"] = lang;
    t.extra[subtitles::kExtraKey] = subtitleSettingsFor(project_, patch);
    // Built in one go (not clip by clip): thousands of lines import in milliseconds.
    t.clips.reserve(shifted.size());
    for (const auto& c : shifted) t.clips.push_back(cueClip(project_, c.start, c.end, c.text));
    const Id id = t.id;
    const std::size_t index = topVideoIndex(project_);
    run("Import subtitles", EditCommand::Scope::AllTracks, {}, [t = std::move(t), index](Project& p) { edit::addTrack(p, t, index); });
    return id;
}

void Editor::setSubtitleSettings(const Id& trackId, const Json& patch) {
    if (!patch.is_object()) throw EditError("settings patch must be an object");
    const Track* t = project_.findTrack(trackId);
    if (!t || !subtitles::isSubtitleTrack(*t)) throw EditError("not a subtitle track");
    run("Subtitle style", EditCommand::Scope::Tracks, {trackId}, [=](Project& p) {
        Track& tr = requireSubtitleTrack(p, trackId);
        tr.extra[subtitles::kExtraKey].merge_patch(patch);
    });
}

void Editor::shiftSubtitles(const Id& trackId, Time delta) {
    const Track* t = project_.findTrack(trackId);
    if (!t || !subtitles::isSubtitleTrack(*t)) throw EditError("not a subtitle track");
    if (!t->clips.empty() && t->clips.front().timelineIn + delta < Time::zero())
        throw EditError("Dòng đầu tiên sẽ bắt đầu trước 0 giây");
    const Time d = delta.snapToFrame(project_.fps);
    run("Shift subtitles", EditCommand::Scope::Tracks, {trackId}, [=](Project& p) {
        Track& tr = requireSubtitleTrack(p, trackId);
        for (auto& c : tr.clips) c.timelineIn += d;
    });
}

Id Editor::applySubtitleTranslation(const Id& sourceTrackId, const std::map<Id, std::string>& texts,
                                    const std::string& lang, TranslationMode mode) {
    const Track* src = project_.findTrack(sourceTrackId);
    if (!src || !subtitles::isSubtitleTrack(*src)) throw EditError("not a subtitle track");
    if (texts.empty()) throw EditError("Chưa có bản dịch");
    if (mode == TranslationMode::Replace) {
        run("Translate subtitles", EditCommand::Scope::Tracks, {sourceTrackId}, [=](Project& p) {
            Track& tr = requireSubtitleTrack(p, sourceTrackId);
            for (auto& c : tr.clips)
                if (auto it = texts.find(c.id); it != texts.end()) c.content["text"] = it->second;
            tr.extra[subtitles::kExtraKey]["lang"] = lang;
        });
        return sourceTrackId;
    }
    Track t;
    t.id = newId();
    t.type = TrackType::Video;
    std::string langName = lang;
    for (const auto& l : subtitles::languages())
        if (l.value("code", std::string{}) == lang) langName = l.value("name", lang);
    t.name = "Phụ đề " + langName;
    Json set = src->extra[subtitles::kExtraKey];
    set["lang"] = lang;
    if (mode == TranslationMode::Bilingual) {
        // The translation sits right above the source line, a little smaller.
        set["position"]["anchor"] = "above";
        set["position"]["track"] = sourceTrackId;
        if (set["style"].is_object()) set["style"]["size"] = std::round(set["style"].value("size", 56.0) * 0.9);
    }
    t.extra[subtitles::kExtraKey] = set;
    for (const auto& c : src->clips) {
        if (!c.generated() || c.content.value("kind", std::string{}) != "subtitle") continue;
        Clip n = c;
        n.id = newId();
        if (auto it = texts.find(c.id); it != texts.end()) n.content["text"] = it->second;
        t.clips.push_back(std::move(n));
    }
    const Id id = t.id;
    std::size_t index = 0;
    for (std::size_t i = 0; i < project_.tracks.size(); ++i)
        if (project_.tracks[i].id == sourceTrackId) index = i + 1;
    const bool hideSource = mode == TranslationMode::NewTrack;
    run("Translate subtitles", EditCommand::Scope::AllTracks, {}, [t = std::move(t), index, hideSource, sourceTrackId](Project& p) {
        edit::addTrack(p, t, index);
        if (hideSource)
            if (Track* s = p.findTrack(sourceTrackId)) s->enabled = false;
    });
    return id;
}

// ---------------------------------------------------------------- sound

namespace {
void mergePatch(Json& target, const Json& patch) {
    if (!target.is_object()) target = Json::object();
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it->is_null()) target.erase(it.key());
        else target[it.key()] = *it;
    }
}

std::vector<Id> tracksOf(const Project& p, const std::vector<Id>& clipIds) {
    if (clipIds.empty()) throw EditError("no clip selected");
    std::vector<Id> tracks;
    for (const Id& c : clipIds) {
        const Track* t = p.trackOfClip(c);
        if (!t) throw EditError("clip not found");
        if (t->locked) throw EditError("track is locked: " + t->name);
        if (std::find(tracks.begin(), tracks.end(), t->id) == tracks.end()) tracks.push_back(t->id);
    }
    return tracks;
}

// First audio track (preferring `name`) with nothing in [range); nullptr when none.
const Track* freeAudioTrack(const Project& p, TimeRange range, const std::string& name) {
    const Track* any = nullptr;
    for (const Track& t : p.tracks) {
        if (t.type != TrackType::Audio || t.locked) continue;
        const bool busy = std::any_of(t.clips.begin(), t.clips.end(), [&](const Clip& c) {
            return c.timelineIn < range.end && range.start < c.timelineOut();
        });
        if (busy) continue;
        if (name.empty() || t.name == name) return &t;
        if (!any) any = &t;
    }
    return name.empty() ? any : nullptr;
}
}  // namespace

void Editor::setClipAudio(const std::vector<Id>& clipIds, const Json& patch) {
    if (!patch.is_object()) throw EditError("audio settings must be an object");
    const auto tracks = tracksOf(project_, clipIds);
    run("Sound", EditCommand::Scope::Tracks, tracks, [=](Project& p) {
        for (const Id& id : clipIds) {
            Clip* c = p.trackOfClip(id)->findClip(id);
            Json a = c->extra.contains("audio") ? c->extra["audio"] : Json::object();
            mergePatch(a, patch);
            const double cap = std::min(audiofx::kMaxFadeSeconds, c->duration().seconds() / 2);
            for (const char* k : {"fade_in", "fade_out"})
                if (a.contains(k)) {
                    if (!a[k].is_number()) throw EditError(std::string(k) + " must be a number");
                    const double v = std::clamp(a[k].get<double>(), 0.0, cap);
                    if (v <= 0) a.erase(k);
                    else a[k] = v;
                }
            if (a.contains("eq") && a["eq"].is_object() && a["eq"].contains("gains")) {
                const Json& g = a["eq"]["gains"];
                if (!g.is_array() || g.size() != audiofx::kEqBands) throw EditError("eq needs 5 band gains");
            }
            if (a.contains("denoise") && a["denoise"].is_object())
                a["denoise"]["amount"] = std::clamp(a["denoise"].value("amount", 0.6), 0.0, 1.0);
            if (a.empty()) c->extra.erase("audio");
            else c->extra["audio"] = a;
        }
    });
}

Id Editor::detachAudio(const Id& clipId) {
    const Track* src = project_.trackOfClip(clipId);
    if (!src) throw EditError("clip not found");
    if (src->locked) throw EditError("track is locked: " + src->name);
    const Clip& c = *src->findClip(clipId);
    const MediaAsset* a = project_.findAsset(c.assetId);
    if (!a || !a->audio) throw EditError("this clip has no sound");
    if (src->type == TrackType::Audio) throw EditError("already an audio clip");
    if (audiofx::detached(c)) throw EditError("sound already detached");
    Clip copy = c;
    copy.id = newId();
    copy.effects = Json::array();
    copy.transform = Json::object();
    copy.transitionIn = copy.transitionOut = nullptr;
    copy.extra.erase("audio");
    if (c.extra.contains("audio")) {
        Json au = c.extra["audio"];
        au.erase("detached");
        if (!au.empty()) copy.extra["audio"] = au;
    }
    // Only volume keyframes travel with the sound.
    Json keys = Json::object();
    if (c.keyframes.is_object() && c.keyframes.contains(kf::kVolume)) keys[kf::kVolume] = c.keyframes[kf::kVolume];
    copy.keyframes = keys;
    const Track* target = freeAudioTrack(project_, c.range(), {});
    Id targetId = target ? target->id : Id{};
    const Id newTrack = target ? Id{} : newId();
    const Id videoTrack = src->id;
    run("Detach audio", EditCommand::Scope::AllTracks, {}, [=](Project& p) mutable {
        if (targetId.empty()) {
            Track t;
            t.id = newTrack;
            t.type = TrackType::Audio;
            t.name = "Âm thanh tách";
            edit::addTrack(p, t, p.tracks.size());
            targetId = newTrack;
        }
        edit::insertClip(p, targetId, copy);
        Clip* orig = p.findTrack(videoTrack)->findClip(clipId);
        orig->extra["audio"]["detached"] = true;
    });
    return copy.id;
}

Id Editor::placeAudio(const Id& assetId, Time at, const std::string& trackName) {
    const MediaAsset* a = project_.findAsset(assetId);
    if (!a || !a->audio) throw EditError("not an audio asset");
    if (at < Time::zero()) throw EditError("negative time");
    Clip c;
    c.id = newId();
    c.assetId = assetId;
    c.timelineIn = at.snapToFrame(project_.fps);
    c.sourceIn = Time::zero();
    c.sourceOut = a->duration;
    const Track* target = freeAudioTrack(project_, c.range(), trackName);
    Id targetId = target ? target->id : Id{};
    const Id newTrack = newId();
    run("Voice-over", EditCommand::Scope::AllTracks, {}, [=](Project& p) mutable {
        if (targetId.empty()) {
            Track t;
            t.id = newTrack;
            t.type = TrackType::Audio;
            t.name = trackName;
            edit::addTrack(p, t, p.tracks.size());
            targetId = newTrack;
        }
        edit::insertClip(p, targetId, c);
    });
    return c.id;
}

void Editor::setTrackMix(const Id& trackId, double pan, bool solo) {
    if (!project_.findTrack(trackId)) throw EditError("track not found");
    if (!(pan >= -1.0 && pan <= 1.0)) throw EditError("pan must be within -1..1");
    run("Mixer", EditCommand::Scope::Tracks, {trackId}, [=](Project& p) {
        Track* t = p.findTrack(trackId);
        if (std::abs(pan) < 1e-6) t->extra.erase("pan");
        else t->extra["pan"] = pan;
        if (solo) t->extra["solo"] = true;
        else t->extra.erase("solo");
    });
}

// ---------------------------------------------------------------- clip tools

void Editor::setClipReversed(const std::vector<Id>& clipIds, bool reversed) {
    const auto tracks = tracksOf(project_, clipIds);
    for (const Id& id : clipIds) {
        const Clip* c = project_.findClip(id);
        if (c->generated()) throw EditError("text and stickers cannot be reversed");
        const MediaAsset* a = project_.findAsset(c->assetId);
        if (a && a->type == MediaType::Image) throw EditError("photos cannot be reversed");
    }
    run(reversed ? "Reverse" : "Play forwards", EditCommand::Scope::Tracks, tracks, [=](Project& p) {
        for (const Id& id : clipIds) p.trackOfClip(id)->findClip(id)->reversed = reversed;
    });
}

Id Editor::freezeFrame(const Id& clipId, Time t, Time duration) {
    const Track* tr = project_.trackOfClip(clipId);
    if (!tr) throw EditError("clip not found");
    if (tr->locked) throw EditError("track is locked: " + tr->name);
    const Clip& c = *tr->findClip(clipId);
    const MediaAsset* a = project_.findAsset(c.assetId);
    if (c.generated() || !a || a->type != MediaType::Video) throw EditError("freeze frame needs a video clip");
    t = t.snapToFrame(project_.fps);
    if (t < c.timelineIn || t >= c.timelineOut()) throw EditError("playhead is not on the clip");
    duration = std::max(duration.snapToFrame(project_.fps), Time::fromFrame(1, project_.fps));
    const Rational fps = a->fps.num > 0 ? a->fps : project_.fps;
    Clip still = c;
    still.id = newId();
    still.reversed = false;
    still.sourceIn = c.toSource(t).snapToFrame(fps);
    if (still.sourceIn >= a->duration) still.sourceIn = Time::fromFrame(std::max<std::int64_t>(0, a->duration.toFrame(fps) - 1), fps);
    const Time frame = Time::fromFrame(1, fps);
    still.sourceOut = std::min(still.sourceIn + frame, a->duration);
    // One source frame spread over `duration`: speed = frame / duration.
    still.speed = Rational{(still.sourceOut - still.sourceIn).ticks(), duration.ticks()}.reduced();
    still.timelineIn = t;
    still.transitionIn = still.transitionOut = nullptr;
    still.keyframes = Json::object();
    still.extra["freeze"] = true;
    const Id trackId = tr->id, rightId = newId();
    const bool split = t > c.timelineIn;
    run("Freeze frame", EditCommand::Scope::Tracks, {trackId}, [=](Project& p) {
        if (split) edit::splitClip(p, clipId, t, rightId);
        Track* track = p.findTrack(trackId);
        for (Clip& x : track->clips)
            if (x.timelineIn >= t) x.timelineIn = x.timelineIn + duration;
        Clip s = still;
        // The still is exactly `duration` long whatever the rounding of the speed.
        edit::insertClip(p, trackId, s);
    });
    return still.id;
}

}  // namespace vstudio
