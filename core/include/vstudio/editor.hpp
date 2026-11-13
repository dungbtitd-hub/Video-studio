// Video Studio Core - editing session facade used by both UIs
// (Qt calls it directly, Flutter through the C API).
//
// Every edit is a command on the in-memory timeline model: it changes a few
// numbers (in/out points, positions) and never reads or writes media, so it
// completes in microseconds regardless of file size. Media work happens only
// in the preview engine (background threads) and on export.
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "vstudio/history.hpp"
#include "vstudio/model.hpp"
#include "vstudio/timeline.hpp"

namespace vstudio {

class Editor {
public:
    explicit Editor(Project p);
    static Project newProject(const std::string& name, int width, int height, Rational fps, const std::string& platform);

    // --- edits (throw EditError, project unchanged on failure) ---
    Id addAsset(MediaAsset asset);  // asset.id generated if empty
    // Points an asset at another file (moved or renamed media). Undoable; clips are untouched.
    // `fingerprint` is the new file's fingerprintFile() (kept as is when empty).
    void relinkAsset(const Id& assetId, const std::string& uri, const std::string& fingerprint = {});
    Id addTrack(TrackType type, const std::string& name);
    Id insertClip(const Id& trackId, const Id& assetId, Time timelineIn, Time sourceIn, Time sourceOut);
    // Appends the whole asset (or [sourceIn, sourceOut)) at the end of the track.
    Id appendClip(const Id& trackId, const Id& assetId, std::optional<Time> sourceIn = {}, std::optional<Time> sourceOut = {});
    void removeClip(const Id& clipId);
    // Removes the clip and closes the gap on its track (one undo step).
    void rippleDelete(const Id& clipId);
    // ---- tracks ----
    // Removes a track and its clips (refused while locked). Undoable.
    void removeTrack(const Id& trackId);
    void renameTrack(const Id& trackId, const std::string& name);
    // Hidden (video) / silenced (audio) / locked: locked refuses every clip edit on the track.
    void setTrackFlags(const Id& trackId, bool enabled, bool locked, bool muted);
    void setTrackVolume(const Id& trackId, double volume);
    // Moves a track among the tracks of its type: 0 = bottom video layer / first audio track.
    void moveTrack(const Id& trackId, std::size_t indexInType);
    void moveClip(const Id& clipId, const Id& toTrackId, Time timelineIn);
    void trimClip(const Id& clipId, TrimEdge edge, Time timelineTime);
    Id splitClip(const Id& clipId, Time timelineTime);
    // Splits every unlocked clip under the playhead; returns the number split.
    int splitAt(Time timelineTime);
    void setClipVolume(const Id& clipId, double volume);
    void setClipTransform(const Id& clipId, const Json& transform);
    void addClipEffect(const Id& clipId, const std::string& type, const Json& params);
    // One-tap colour look (looks.hpp) on one or many clips, as one undo step. Replaces any look
    // already on those clips; an empty lookId removes it. Clips on locked tracks are refused.
    void setClipLook(const std::vector<Id>& clipIds, const std::string& lookId, double strength = 1.0);

    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const { return history_.canRedo(); }
    [[nodiscard]] std::string undoLabel() const { return history_.undoLabel(); }
    [[nodiscard]] std::string redoLabel() const { return history_.redoLabel(); }

    [[nodiscard]] const Project& project() const { return project_; }
    // Immutable copy for render threads; re-created only when the revision changed.
    std::shared_ptr<const Project> snapshot();
    [[nodiscard]] std::uint64_t revision() const { return history_.revision(); }
    [[nodiscard]] bool dirty() const { return history_.revision() != savedRevision_; }
    void markSaved() { savedRevision_ = history_.revision(); }

private:
    void run(std::string label, EditCommand::Scope scope, std::vector<Id> tracks, std::function<void(Project&)> fn);

    Project project_;
    CommandHistory history_;
    std::uint64_t savedRevision_ = 0;
    std::shared_ptr<const Project> snapshot_;
    std::uint64_t snapshotRevision_ = ~0ull;
};

}  // namespace vstudio
