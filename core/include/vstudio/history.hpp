// Video Studio Core - command history (undo/redo).
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vstudio/model.hpp"

namespace vstudio {

class Command {
public:
    virtual ~Command() = default;
    [[nodiscard]] virtual std::string label() const = 0;
    // Applies the edit. Must leave the project unchanged if it throws.
    virtual void apply(Project& p) = 0;
    virtual void revert(Project& p) = 0;
};

// Generic edit command: runs `fn` and restores a snapshot of the touched
// part of the project on undo. `scope` lists the tracks the edit touches;
// an empty scope snapshots the whole track list (add/remove/reorder track).
// Edits are deterministic (all new ids are generated before the command is
// built), so redo simply runs `fn` again.
class EditCommand final : public Command {
public:
    enum class Scope { Tracks, AllTracks, AllTracksAndAssets };

    EditCommand(std::string label, Scope scope, std::vector<Id> trackIds, std::function<void(Project&)> fn);
    [[nodiscard]] std::string label() const override { return label_; }
    void apply(Project& p) override;
    void revert(Project& p) override;

private:
    struct Snapshot {
        std::vector<Track> tracks;  // either listed tracks or all tracks
        std::optional<std::vector<MediaAsset>> assets;
    };
    Snapshot take(const Project& p) const;
    void restore(Project& p, const Snapshot& s) const;

    std::string label_;
    Scope scope_;
    std::vector<Id> trackIds_;
    std::function<void(Project&)> fn_;
    Snapshot before_;
};

class CommandHistory {
public:
    explicit CommandHistory(std::size_t limit = 200) : limit_(limit) {}

    // Applies and records the command. Exceptions propagate; nothing is recorded then.
    void execute(Project& p, std::unique_ptr<Command> cmd);
    bool undo(Project& p);
    bool redo(Project& p);
    void clear();

    [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
    [[nodiscard]] std::string undoLabel() const { return undo_.empty() ? "" : undo_.back()->label(); }
    [[nodiscard]] std::string redoLabel() const { return redo_.empty() ? "" : redo_.back()->label(); }
    // Monotonic counter, bumped by every execute/undo/redo. Used for dirty/autosave.
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
    std::size_t limit_;
    std::deque<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    std::uint64_t revision_ = 0;
};

}  // namespace vstudio
