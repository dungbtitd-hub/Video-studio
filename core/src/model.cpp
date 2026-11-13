#include "vstudio/model.hpp"

#include <algorithm>
#include <stdexcept>

namespace vstudio {

const char* toString(MediaType t) {
    switch (t) {
        case MediaType::Video: return "video";
        case MediaType::Audio: return "audio";
        case MediaType::Image: return "image";
    }
    return "video";
}

const char* toString(TrackType t) { return t == TrackType::Audio ? "audio" : "video"; }

bool parseMediaType(const std::string& s, MediaType& out) {
    if (s == "video") out = MediaType::Video;
    else if (s == "audio") out = MediaType::Audio;
    else if (s == "image") out = MediaType::Image;
    else return false;
    return true;
}

bool parseTrackType(const std::string& s, TrackType& out) {
    if (s == "video") out = TrackType::Video;
    else if (s == "audio") out = TrackType::Audio;
    else return false;
    return true;
}

const Clip* Track::findClip(const Id& cid) const {
    for (const auto& c : clips) if (c.id == cid) return &c;
    return nullptr;
}
Clip* Track::findClip(const Id& cid) {
    for (auto& c : clips) if (c.id == cid) return &c;
    return nullptr;
}
const Clip* Track::clipAt(Time t) const {
    for (const auto& c : clips) if (c.range().contains(t)) return &c;
    return nullptr;
}
Time Track::end() const {
    Time e;
    for (const auto& c : clips) e = std::max(e, c.timelineOut());
    return e;
}
void Track::sortClips() {
    std::stable_sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.timelineIn < b.timelineIn; });
}

const MediaAsset* Project::findAsset(const Id& aid) const {
    for (const auto& a : assets) if (a.id == aid) return &a;
    return nullptr;
}
const Track* Project::findTrack(const Id& tid) const {
    for (const auto& t : tracks) if (t.id == tid) return &t;
    return nullptr;
}
Track* Project::findTrack(const Id& tid) {
    for (auto& t : tracks) if (t.id == tid) return &t;
    return nullptr;
}
Track* Project::trackOfClip(const Id& cid) {
    for (auto& t : tracks) if (t.findClip(cid)) return &t;
    return nullptr;
}
const Track* Project::trackOfClip(const Id& cid) const {
    for (const auto& t : tracks) if (t.findClip(cid)) return &t;
    return nullptr;
}
const Clip* Project::findClip(const Id& cid) const {
    for (const auto& t : tracks) if (auto* c = t.findClip(cid)) return c;
    return nullptr;
}
Time Project::duration() const {
    Time e;
    for (const auto& t : tracks) e = std::max(e, t.end());
    return e;
}

// ---------------------------------------------------------------- JSON

namespace {

// Times are stored as integer ticks plus the tick rate so a reader never has to guess.
Json timeJ(Time t) { return t.ticks(); }
Time timeFrom(const Json& j) { return Time::fromTicks(j.get<std::int64_t>()); }
Json ratJ(Rational r) { return r.toString(); }
Rational ratFrom(const Json& j) {
    Rational r;
    if (!Rational::parse(j.get<std::string>(), r)) throw std::runtime_error("invalid rational: " + j.dump());
    return r;
}

// Copies keys of `j` not in `known` into an object (forward compatibility).
Json collectExtra(const Json& j, std::initializer_list<const char*> known) {
    Json extra = Json::object();
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool k = false;
        for (const char* n : known) if (it.key() == n) { k = true; break; }
        if (!k) extra[it.key()] = it.value();
    }
    return extra;
}

void mergeExtra(Json& j, const Json& extra) {
    for (auto it = extra.begin(); it != extra.end(); ++it)
        if (!j.contains(it.key())) j[it.key()] = it.value();
}

template <class T>
T opt(const Json& j, const char* key, T def) {
    auto it = j.find(key);
    return it == j.end() || it->is_null() ? def : it->get<T>();
}

Json assetJ(const MediaAsset& a) {
    Json j{{"asset_id", a.id}, {"uri", a.uri}, {"media_type", toString(a.type)},
           {"duration", timeJ(a.duration)}, {"codec", a.codec}, {"width", a.width}, {"height", a.height},
           {"fps", ratJ(a.fps)}, {"vfr", a.vfr}, {"has_video", a.hasVideo}, {"fingerprint", a.fingerprint}};
    if (a.audio) j["audio_info"] = {{"sample_rate", a.audio->sampleRate}, {"channels", a.audio->channels}, {"codec", a.audio->codec}};
    else j["audio_info"] = nullptr;
    mergeExtra(j, a.extra);
    return j;
}

MediaAsset assetFrom(const Json& j) {
    MediaAsset a;
    a.id = j.at("asset_id").get<std::string>();
    a.uri = j.at("uri").get<std::string>();
    if (!parseMediaType(j.at("media_type").get<std::string>(), a.type)) throw std::runtime_error("bad media_type");
    a.duration = timeFrom(j.at("duration"));
    a.codec = opt<std::string>(j, "codec", "");
    a.width = opt<int>(j, "width", 0);
    a.height = opt<int>(j, "height", 0);
    a.fps = j.contains("fps") ? ratFrom(j.at("fps")) : Rational{0, 1};
    a.vfr = opt<bool>(j, "vfr", false);
    a.hasVideo = opt<bool>(j, "has_video", a.type != MediaType::Audio);
    a.fingerprint = opt<std::string>(j, "fingerprint", "");
    if (auto it = j.find("audio_info"); it != j.end() && it->is_object()) {
        a.audio = AudioInfo{it->value("sample_rate", 0), it->value("channels", 0), it->value("codec", std::string{})};
    }
    a.extra = collectExtra(j, {"asset_id", "uri", "media_type", "duration", "codec", "width", "height", "fps", "vfr",
                               "has_video", "fingerprint", "audio_info"});
    return a;
}

Json clipJ(const Clip& c, const Id& trackId) {
    Json j{{"clip_id", c.id}, {"asset_id", c.assetId}, {"track_id", trackId}, {"timeline_in", timeJ(c.timelineIn)},
           {"source_in", timeJ(c.sourceIn)}, {"source_out", timeJ(c.sourceOut)}, {"speed", ratJ(c.speed)},
           {"volume", c.volume}, {"transform", c.transform}, {"effects", c.effects}};
    if (c.keyframes.is_object() && !c.keyframes.empty()) j["keyframes"] = c.keyframes;
    if (c.transitionIn.is_object()) j["transition_in"] = c.transitionIn;
    if (c.transitionOut.is_object()) j["transition_out"] = c.transitionOut;
    if (c.content.is_object()) j["content"] = c.content;
    mergeExtra(j, c.extra);
    return j;
}

Clip clipFrom(const Json& j) {
    Clip c;
    c.id = j.at("clip_id").get<std::string>();
    c.assetId = j.at("asset_id").get<std::string>();
    c.timelineIn = timeFrom(j.at("timeline_in"));
    c.sourceIn = timeFrom(j.at("source_in"));
    c.sourceOut = timeFrom(j.at("source_out"));
    c.speed = j.contains("speed") ? ratFrom(j.at("speed")) : Rational{1, 1};
    if (c.speed.num <= 0 || c.speed.den <= 0) throw std::runtime_error("clip speed must be positive");
    c.volume = opt<double>(j, "volume", 1.0);
    c.transform = j.value("transform", Json::object());
    c.effects = j.value("effects", Json::array());
    c.keyframes = j.value("keyframes", Json::object());
    if (auto it = j.find("transition_in"); it != j.end() && it->is_object()) c.transitionIn = *it;
    if (auto it = j.find("transition_out"); it != j.end() && it->is_object()) c.transitionOut = *it;
    if (auto it = j.find("content"); it != j.end() && it->is_object()) c.content = *it;
    c.extra = collectExtra(j, {"clip_id", "asset_id", "track_id", "timeline_in", "source_in", "source_out", "speed",
                               "volume", "transform", "effects", "keyframes", "transition_in", "transition_out",
                               "content"});
    return c;
}

Json trackJ(const Track& t, int order) {
    Json clips = Json::array();
    for (const auto& c : t.clips) clips.push_back(clipJ(c, t.id));
    Json j{{"track_id", t.id}, {"type", toString(t.type)}, {"name", t.name}, {"order", order},
           {"enabled", t.enabled}, {"locked", t.locked}, {"muted", t.muted}, {"volume", t.volume}, {"clips", clips}};
    mergeExtra(j, t.extra);
    return j;
}

Track trackFrom(const Json& j) {
    Track t;
    t.id = j.at("track_id").get<std::string>();
    if (!parseTrackType(j.at("type").get<std::string>(), t.type)) throw std::runtime_error("bad track type");
    t.name = opt<std::string>(j, "name", "");
    t.enabled = opt<bool>(j, "enabled", true);
    t.locked = opt<bool>(j, "locked", false);
    t.muted = opt<bool>(j, "muted", false);
    t.volume = opt<double>(j, "volume", 1.0);
    for (const auto& cj : j.value("clips", Json::array())) t.clips.push_back(clipFrom(cj));
    t.sortClips();
    t.extra = collectExtra(j, {"track_id", "type", "name", "order", "enabled", "locked", "muted", "volume", "clips"});
    return t;
}

}  // namespace

Json toJson(const Project& p) {
    Json assets = Json::array();
    for (const auto& a : p.assets) assets.push_back(assetJ(a));
    Json tracks = Json::array();
    for (std::size_t i = 0; i < p.tracks.size(); ++i) tracks.push_back(trackJ(p.tracks[i], static_cast<int>(i)));
    Json j{{"format", "vstudio"},
           {"schema_version", p.schemaVersion},
           {"project_id", p.id},
           {"name", p.name},
           {"canvas", {{"width", p.canvas.width}, {"height", p.canvas.height}}},
           {"fps", ratJ(p.fps)},
           {"timebase", "1/" + std::to_string(Time::kTicksPerSecond)},
           {"audio", {{"sample_rate", p.sampleRate}, {"channels", p.audioChannels}}},
           {"required_capabilities", p.requiredCapabilities},
           {"created_by_platform", p.createdByPlatform},
           {"created_at", p.createdAt},
           {"modified_at", p.modifiedAt},
           {"assets", assets},
           {"tracks", tracks}};
    mergeExtra(j, p.extra);
    return j;
}

Project projectFromJson(const Json& j) {
    if (j.value("format", std::string{}) != "vstudio") throw std::runtime_error("not a vstudio project");
    Project p;
    p.schemaVersion = j.at("schema_version").get<int>();
    if (p.schemaVersion > kSchemaVersion)
        throw std::runtime_error("project schema " + std::to_string(p.schemaVersion) + " is newer than supported " +
                                 std::to_string(kSchemaVersion));
    if (j.value("timebase", std::string{}) != "1/" + std::to_string(Time::kTicksPerSecond))
        throw std::runtime_error("unsupported timebase");
    p.id = j.at("project_id").get<std::string>();
    p.name = opt<std::string>(j, "name", "");
    if (auto c = j.find("canvas"); c != j.end()) p.canvas = {c->value("width", 1920), c->value("height", 1080)};
    p.fps = ratFrom(j.at("fps"));
    if (auto a = j.find("audio"); a != j.end()) {
        p.sampleRate = a->value("sample_rate", 48000);
        p.audioChannels = a->value("channels", 2);
    }
    p.requiredCapabilities = j.value("required_capabilities", std::vector<std::string>{});
    p.createdByPlatform = opt<std::string>(j, "created_by_platform", "");
    p.createdAt = opt<std::string>(j, "created_at", "");
    p.modifiedAt = opt<std::string>(j, "modified_at", "");
    for (const auto& aj : j.value("assets", Json::array())) p.assets.push_back(assetFrom(aj));
    std::vector<std::pair<int, Track>> ordered;
    for (const auto& tj : j.value("tracks", Json::array()))
        ordered.emplace_back(tj.value("order", static_cast<int>(ordered.size())), trackFrom(tj));
    std::stable_sort(ordered.begin(), ordered.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [o, t] : ordered) p.tracks.push_back(std::move(t));
    p.extra = collectExtra(j, {"format", "schema_version", "project_id", "name", "canvas", "fps", "timebase", "audio",
                               "required_capabilities", "created_by_platform", "created_at", "modified_at", "assets",
                               "tracks"});
    return p;
}

}  // namespace vstudio
