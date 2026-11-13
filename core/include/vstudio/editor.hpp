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
    void moveClip(const Id& clipId, const Id& toTrackId, Time timelineIn);
    void trimClip(const Id& clipId, TrimEdge edge, Time timelineTime);
    Id splitClip(const Id& clipId, Time timelineTime);
    // Splits every unlocked clip under the playhead; returns the number split.
    int splitAt(Time timelineTime);
    void setClipVolume(const Id& clipId, double volume);
    void setClipTransform(const Id& clipId, const Json& transform);
    void addClipEffect(const Id& clipId, const std::string& type, const Json& params);

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
