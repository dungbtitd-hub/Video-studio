// Video Studio Core - project data model (schema v0).
// Mirrors section 4.2 of the plan: Project, MediaAsset, Track, Clip.
// Unknown JSON fields are kept in `extra` so that a platform that does not
// understand a property (e.g. a desktop-only effect opened on mobile) never
// silently drops it (plan 4.3, NFR-07).
#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "vstudio/time.hpp"

namespace vstudio {

using Id = std::string;
using Json = nlohmann::json;

inline constexpr int kSchemaVersion = 0;

enum class MediaType { Video, Audio, Image };
enum class TrackType { Video, Audio };

const char* toString(MediaType t);
const char* toString(TrackType t);
bool parseMediaType(const std::string& s, MediaType& out);
bool parseTrackType(const std::string& s, TrackType& out);

struct AudioInfo {
    int sampleRate = 0;
    int channels = 0;
    std::string codec;
};

struct MediaAsset {
    Id id;
    std::string uri;         // relative to the project folder when possible
    MediaType type = MediaType::Video;
    Time duration;           // Time::zero() for still images (any length allowed)
    std::string codec;
    int width = 0;
    int height = 0;
    Rational fps{0, 1};
    bool vfr = false;
    bool hasVideo = false;
    std::optional<AudioInfo> audio;
    std::string fingerprint; // size + head/tail hash, used for relink
    Json extra = Json::object();
};

struct Clip {
    Id id;
    Id assetId;
    Time timelineIn;         // where the clip starts on the timeline
    Time sourceIn;           // first source time used (inclusive)
    Time sourceOut;          // end source time (exclusive)
    Rational speed{1, 1};    // > 0; 2/1 = twice as fast
    double volume = 1.0;     // linear gain for audio
    Json transform = Json::object();
    Json effects = Json::array();
    Json keyframes = Json::object();  // animated properties, see keyframes.hpp
    // Transitions (transitions.hpp): {"type": "fade", "duration": <ticks>} or null.
    // `transitionIn` plays at the clip's start: the cut from the clip just before it on
    // the track, or a fade in from nothing. `transitionOut` plays at its end only when
    // no clip follows right after it (a fade out to nothing).
    Json transitionIn;
    Json transitionOut;
    // Generated clips (text, stickers: generators.hpp) have no asset (assetId is empty) and
    // describe what they draw here: {"kind": "text", ...}. Null for media clips.
    Json content;
    Json extra = Json::object();

    [[nodiscard]] bool generated() const { return content.is_object() && content.contains("kind"); }
    [[nodiscard]] Time sourceDuration() const { return sourceOut - sourceIn; }
    [[nodiscard]] Time duration() const { return sourceDuration().scaled(speed.inverse()); }
    [[nodiscard]] Time timelineOut() const { return timelineIn + duration(); }
    [[nodiscard]] TimeRange range() const { return {timelineIn, timelineOut()}; }
    // Maps a timeline time inside this clip to source time.
    [[nodiscard]] Time toSource(Time timeline) const { return sourceIn + (timeline - timelineIn).scaled(speed); }
    // Maps a source time to timeline time.
    [[nodiscard]] Time toTimeline(Time source) const { return timelineIn + (source - sourceIn).scaled(speed.inverse()); }
};

struct Track {
    Id id;
    TrackType type = TrackType::Video;
    std::string name;
    bool enabled = true;
    bool locked = false;
    bool muted = false;
    double volume = 1.0;
    std::vector<Clip> clips;  // kept sorted by timelineIn, never overlapping
    Json extra = Json::object();

    [[nodiscard]] const Clip* findClip(const Id& id) const;
    [[nodiscard]] Clip* findClip(const Id& id);
    [[nodiscard]] const Clip* clipAt(Time t) const;
    [[nodiscard]] Time end() const;
    void sortClips();
};

struct Canvas {
    int width = 1920;
    int height = 1080;
};

struct Project {
    Id id;
    int schemaVersion = kSchemaVersion;
    std::string name;
    Canvas canvas;
    Rational fps{30, 1};
    int sampleRate = 48000;
    int audioChannels = 2;
    std::vector<std::string> requiredCapabilities;
    std::string createdByPlatform;
    std::string createdAt;   // ISO-8601 UTC
    std::string modifiedAt;
    std::vector<MediaAsset> assets;
    // Order = stacking order: tracks[0] is the bottom video layer.
    std::vector<Track> tracks;
    Json extra = Json::object();

    [[nodiscard]] const MediaAsset* findAsset(const Id& id) const;
    [[nodiscard]] const Track* findTrack(const Id& id) const;
    [[nodiscard]] Track* findTrack(const Id& id);
    // Track that owns the clip, or nullptr.
    [[nodiscard]] Track* trackOfClip(const Id& clipId);
    [[nodiscard]] const Track* trackOfClip(const Id& clipId) const;
    [[nodiscard]] const Clip* findClip(const Id& clipId) const;
    [[nodiscard]] Time duration() const;
};

// Serialisation (schema v0). Throws std::runtime_error on invalid input.
Json toJson(const Project& p);
Project projectFromJson(const Json& j);

}  // namespace vstudio
