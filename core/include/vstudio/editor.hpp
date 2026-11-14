// Video Studio Core - editing session facade used by both UIs
// (Qt calls it directly, Flutter through the C API).
//
// Every edit is a command on the in-memory timeline model: it changes a few
// numbers (in/out points, positions) and never reads or writes media, so it
// completes in microseconds regardless of file size. Media work happens only
// in the preview engine (background threads) and on export.
#pragma once

#include <array>
#include <functional>
#include <map>

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "vstudio/history.hpp"
#include "vstudio/keyframes.hpp"
#include "vstudio/model.hpp"
#include "vstudio/speed_curve.hpp"
#include "vstudio/subtitles.hpp"
#include "vstudio/timeline.hpp"
#include "vstudio/transitions.hpp"

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

    // ---- keyframes (keyframes.hpp); each call is one undo step ----
    // Sets keys at `sourceTime` (a key already within half a frame of it is replaced).
    void setKeyframes(const Id& clipId, Time sourceTime, const std::map<std::string, double>& values,
                      Ease ease = Ease::Smooth);
    // Adds a key for every property of `group` at `sourceTime`, holding the values shown there now.
    void addKeyframe(const Id& clipId, kf::Group group, Time sourceTime);
    // Removes the group's keys at `sourceTime` (half-frame tolerance). Returns false if there were none.
    bool removeKeyframe(const Id& clipId, kf::Group group, Time sourceTime);
    void clearKeyframes(const Id& clipId, kf::Group group);
    // Changes a property at `sourceTime` the way an editor UI does: keyed when the
    // property is animated (auto-key), else the clip's static transform/volume.
    void setPropertyAt(const Id& clipId, Time sourceTime, const std::map<std::string, double>& values);
    void addClipEffect(const Id& clipId, const std::string& type, const Json& params);

    // ---- covers (cover.hpp): one "builtin.cover" effect per covered region ----
    // Adds a cover (params merged over defaultCover) and returns its effect id.
    Id addCover(const Id& clipId, const Json& params);
    // Merges `patch` into the cover's params (a null value removes that key).
    void updateCover(const Id& clipId, const Id& effectId, const Json& patch);
    void removeCover(const Id& clipId, const Id& effectId);
    // Moves/resizes the cover at `sourceTime` the way the UI does: keyed when the cover
    // follows motion (has keys), else its static region.
    void setCoverAt(const Id& clipId, const Id& effectId, Time sourceTime, const std::map<std::string, double>& values);
    // Starts following motion: keys the current region at `sourceTime`.
    void keyCover(const Id& clipId, const Id& effectId, Time sourceTime);
    // Stops following: keeps the region shown at `sourceTime` as the static one.
    void clearCoverKeys(const Id& clipId, const Id& effectId, Time sourceTime);
    // Tracking result: replaces the region keys between the first and last sample
    // with these (linear in between, so the cover moves exactly like the subject).
    void setCoverTrack(const Id& clipId, const Id& effectId, const std::vector<std::pair<Time, std::array<double, 4>>>& boxes);
    // Writes tracked motion (tracker.hpp trackFollow) as picture keys on a clip: keys of the same
    // properties inside the tracked span are replaced, the others kept. One undo step.
    void setFollowTrack(const Id& clipId, const std::vector<std::pair<Time, std::map<std::string, double>>>& keys);
    [[nodiscard]] const Json* coverParams(const Id& clipId, const Id& effectId) const;

    // ---- transitions (transitions.hpp); each call is one undo step ----
    // Sets (`type` non-empty) or removes (empty) the transition at the clip's start or end.
    // The end of a clip that touches the next one is that cut, stored on the next clip.
    void setTransition(const Id& clipId, TransitionEdge edge, const std::string& type,
                       Time duration = defaultTransitionDuration());
    // The same transition on every cut between touching clips of every unlocked video
    // track ("Áp dụng cho tất cả"); empty `type` removes them. Returns the number of cuts.
    int setTransitionEverywhere(const std::string& type, Time duration = defaultTransitionDuration());

    // ---- text and stickers (generators.hpp) ----
    // Adds a generated clip ({"kind": "text" | "sticker", ...}) at `at` for `duration` on
    // `trackId`, or (empty) on the highest video track above the main one that is free
    // there, creating a new top track when none is. One undo step. Returns the clip id.
    Id addGeneratedClip(const Json& content, Time at, Time duration, const Id& trackId = {});
    // Merges `patch` into the clip's content (JSON merge patch: null removes a key). Undoable.
    void updateClipContent(const Id& clipId, const Json& patch);
    // Sets the region an overlay effect covers to the canvas box {x, y, w, h} (canvas px) as seen
    // at `sourceTime`: its "w"/"h" and its position, scale kept (keyed when animated). One undo step.
    void setOverlayRegion(const Id& clipId, Time sourceTime, double x, double y, double w, double h);

    // ---- subtitles (subtitles.hpp); each call is one undo step ----
    // A new subtitle track on top of every video track. `settings` is merged over
    // subtitles::defaultTrackSettings (text size scaled to the canvas).
    Id addSubtitleTrack(const std::string& name = "Phụ đề", const Json& settings = Json::object());
    // Inserts a line at `at` (moved past a line already there) lasting `duration`, cut short
    // so it never covers the next line. Empty `trackId`: the topmost subtitle track, created
    // when there is none (same undo step). Returns the clip id; throws when there is no room.
    Id addSubtitle(const Id& trackId, Time at, const std::string& text, Time duration = Time::fromSeconds(2.0));
    // A new subtitle track holding `cues` (normalised, shifted by `offset`). Returns the track id.
    Id importSubtitles(const std::vector<subtitles::Cue>& cues, const std::string& name, const std::string& lang = {},
                       Time offset = Time::zero());
    // Merges `patch` into the track's subtitle settings (style, position, max_width, lang).
    void setSubtitleSettings(const Id& trackId, const Json& patch);
    // Moves every line of the track by `delta` (subtitles out of sync with the voice).
    void shiftSubtitles(const Id& trackId, Time delta);
    // Puts a translation in place: Replace changes the texts of the track; NewTrack adds a
    // track with the same timing and look and hides the source; Bilingual adds it and keeps
    // both, the translation stacked right above the source lines. Returns the track written.
    enum class TranslationMode { Replace, NewTrack, Bilingual };
    Id applySubtitleTranslation(const Id& sourceTrackId, const std::map<Id, std::string>& texts, const std::string& lang,
                                TranslationMode mode);

    // One-tap colour look (looks.hpp) on one or many clips, as one undo step. Replaces any look
    // already on those clips; an empty lookId removes it. Clips on locked tracks are refused.
    // Adds or replaces the single effect of `type` on each clip, as one undo step; null
    // params removes it. `first` puts it before every other effect (input transforms
    // such as Log -> Rec.709 must run before grading). Refused on locked tracks.
    void setClipEffect(const std::vector<Id>& clipIds, const std::string& type, const Json& params, bool first = false,
                       const std::string& label = "Set effect");
    void setClipLook(const std::vector<Id>& clipIds, const std::string& lookId, double strength = 1.0);
    // A saved style (user_presets.hpp captureStyle) on one or many media clips, as one undo step:
    // replaces their colour space, grade, look, LUT, key and video/plugin effects; covers,
    // stabilisation and sound stay. Text, stickers and clips on locked tracks are refused.
    void applyClipStyle(const std::vector<Id>& clipIds, const Json& style, const std::string& label = "Apply preset");
    // Log/HDR footage -> Rec.709 (color_space.hpp), always the first effect; "" removes.
    void setClipColorSpace(const std::vector<Id>& clipIds, const std::string& input, double exposure = 0.0);
    // 3D LUT (.cube); `path` absolute or relative to the project's media folder; "" removes.
    void setClipLut(const std::vector<Id>& clipIds, const std::string& path, double strength = 1.0);
    // Video effects (video_fx.hpp): zoom, rotate or reflection; one of each group per clip, kept after
    // colour effects and covers in the order zoom -> rotate -> reflect. `fxId` "" removes `group`'s.
    // `line` (reflections) < 0 keeps the default. One undo step.
    void setClipVideoFx(const std::vector<Id>& clipIds, const std::string& group, const std::string& fxId,
                        double amount = 1.0, double speed = 1.0, double line = -1.0);

    // Colour grade (grade.hpp) on one or many clips, one undo step: replaces the clips'
    // grade, keeping its place in the effect chain; null or neutral params remove it.
    void setClipGrade(const std::vector<Id>& clipIds, const Json& params);

    // ---- sound (audio_fx.hpp); each call is one undo step ----
    // Merges `patch` into each clip's "audio" settings (fade_in / fade_out seconds, eq, denoise);
    // a null value removes that key. Fades are capped to half the clip and 10 s.
    void setClipAudio(const std::vector<Id>& clipIds, const Json& patch);
    // "Tách âm thanh": copies the clip's sound to an audio track (a free one, else a new one)
    // with the same timing, and silences the clip (audio.detached). Returns the new audio clip.
    Id detachAudio(const Id& clipId);
    // Puts an audio asset (a recorded voice-over) at `at` on the first audio track named
    // `trackName` that is free there, creating it when none is. Returns the clip id.
    Id placeAudio(const Id& assetId, Time at, const std::string& trackName = "Thu âm");
    // Mixer: pan -1..1 (left..right), solo. Soloed tracks are the only ones heard.
    void setTrackMix(const Id& trackId, double pan, bool solo);

    // ---- clip tools (clip_tools.hpp); each call is one undo step ----
    void setClipReversed(const std::vector<Id>& clipIds, bool reversed);
    // Holds the frame shown at timeline time `t` for `duration`: splits the clip there, inserts
    // the still and pushes the rest of the track right. Returns the freeze clip's id.
    Id freezeFrame(const Id& clipId, Time t, Time duration = Time::fromSeconds(3.0));
    // Speed (speed_curve.hpp): `speed` 0.1..10 x, and a ramp drawn over each clip (empty = none;
    // `preset` names a ready-made one for the UI). The clips after each one on its track move by
    // the change in length, so nothing overlaps and no gap opens.
    void setClipSpeed(const std::vector<Id>& clipIds, double speed, const std::vector<speed::CurvePoint>& curve = {},
                      const std::string& preset = "");

    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const { return history_.canRedo(); }
    [[nodiscard]] std::string undoLabel() const { return history_.undoLabel(); }
    [[nodiscard]] std::string redoLabel() const { return history_.redoLabel(); }

    // Live preview during a gesture (dragging a corner, pinching, moving a slider): runs `edit` on a
    // scratch copy and returns the result for the preview engine. The project and the undo history
    // are untouched; the gesture's end commits with the normal edit (one undo step). Returns nullptr
    // when the edit is refused.
    [[nodiscard]] std::shared_ptr<const Project> preview(const std::function<void(Editor&)>& edit) const;

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
