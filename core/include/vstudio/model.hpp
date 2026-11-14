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

// A point of a speed curve: at SOURCE time `t` the clip plays `v` times its base speed.
// Between points the speed changes linearly with source time; before the first and after
// the last it holds. Source times keep the curve on the same pictures when the clip is
// trimmed or split (like keyframes).
inline constexpr double kMinCurveSpeed = 0.05, kMaxCurveSpeed = 20.0;
struct SpeedKey {
    Time t;
    double v = 1.0;  // 0.05..20
    bool operator==(const SpeedKey&) const = default;
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
    // Plays the source range backwards (clip_tools.hpp). Source times then run from
    // sourceOut down to sourceIn; keyframes stay keyed in source time.
    bool reversed = false;
    // Speed ramp (speed_curve.hpp): empty = the constant `speed`. Otherwise the speed at a
    // source time is `speed` x the curve's value there.
    std::vector<SpeedKey> speedCurve;
    Json extra = Json::object();

    [[nodiscard]] bool generated() const { return content.is_object() && content.contains("kind"); }
    [[nodiscard]] bool curved() const { return !speedCurve.empty(); }
    [[nodiscard]] Time sourceDuration() const { return sourceOut - sourceIn; }
    [[nodiscard]] Time duration() const {
        return curved() ? curveElapsed(sourceIn, sourceOut) : sourceDuration().scaled(speed.inverse());
    }
    [[nodiscard]] Time timelineOut() const { return timelineIn + duration(); }
    [[nodiscard]] TimeRange range() const { return {timelineIn, timelineOut()}; }
    // Maps a timeline time inside this clip to source time.
    [[nodiscard]] Time toSource(Time timeline) const {
        if (curved()) return curveToSource(timeline - timelineIn);
        const Time d = (timeline - timelineIn).scaled(speed);
        return reversed ? sourceOut - Time::fromTicks(1) - d : sourceIn + d;
    }
    // Maps a source time to timeline time.
    [[nodiscard]] Time toTimeline(Time source) const {
        if (curved())
            return timelineIn + (reversed ? curveElapsed(source + Time::fromTicks(1), sourceOut) : curveElapsed(sourceIn, source));
        return timelineIn + (reversed ? sourceOut - Time::fromTicks(1) - source : source - sourceIn).scaled(speed.inverse());
    }
    // Speed (x real time) at a source time, curve included.
    [[nodiscard]] double speedAt(Time source) const;
    // Timeline time taken to play the source span [a, b] (a <= b) with the curve.
    [[nodiscard]] Time curveElapsed(Time a, Time b) const;
    // Source time reached `sinceStart` into the clip with the curve (toSource for curved clips).
    [[nodiscard]] Time curveToSource(Time sinceStart) const;
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
