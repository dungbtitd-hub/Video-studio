// Video Studio Core - non-destructive timeline operations.
// These functions validate and mutate a Project; they throw EditError and
// leave the project untouched when an edit is not allowed. Higher layers
// wrap them in commands (history.hpp / editor.hpp) for undo/redo.
#pragma once

#include <stdexcept>

#include "vstudio/model.hpp"

namespace vstudio {

class EditError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class TrimEdge { Start, End };

namespace edit {

void addTrack(Project& p, Track track, std::size_t index);
void removeTrack(Project& p, const Id& trackId);
void addAsset(Project& p, MediaAsset asset);

// Inserts a clip; it must not overlap clips already on the track.
void insertClip(Project& p, const Id& trackId, Clip clip);
void removeClip(Project& p, const Id& clipId);
// Moves a clip to another time and/or track; no overlap allowed.
void moveClip(Project& p, const Id& clipId, const Id& toTrackId, Time newTimelineIn);
// Moves one edge of a clip to `timelineTime`, adjusting the source in/out.
void trimClip(Project& p, const Id& clipId, TrimEdge edge, Time timelineTime);
// Splits a clip at `timelineTime` (snapped to the project frame grid).
// The left part keeps clipId, the right part gets newClipId.
void splitClip(Project& p, const Id& clipId, Time timelineTime, const Id& newClipId);
void setClipVolume(Project& p, const Id& clipId, double volume);
void setTrackFlags(Project& p, const Id& trackId, bool enabled, bool locked, bool muted);

// Whether an asset may be placed on a track of this type.
bool compatible(const MediaAsset& a, TrackType t);

}  // namespace edit
}  // namespace vstudio
