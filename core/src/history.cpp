#include "vstudio/history.hpp"

#include <algorithm>

namespace vstudio {

EditCommand::EditCommand(std::string label, Scope scope, std::vector<Id> trackIds, std::function<void(Project&)> fn)
    : label_(std::move(label)), scope_(scope), trackIds_(std::move(trackIds)), fn_(std::move(fn)) {}

EditCommand::Snapshot EditCommand::take(const Project& p) const {
    Snapshot s;
    if (scope_ == Scope::Tracks) {
        for (const auto& id : trackIds_)
            if (const Track* t = p.findTrack(id)) s.tracks.push_back(*t);
    } else {
        s.tracks = p.tracks;
    }
    if (scope_ == Scope::AllTracksAndAssets) s.assets = p.assets;
    return s;
}

void EditCommand::restore(Project& p, const Snapshot& s) const {
    if (scope_ == Scope::Tracks) {
        for (const auto& saved : s.tracks)
            if (Track* t = p.findTrack(saved.id)) *t = saved;
    } else {
        p.tracks = s.tracks;
    }
    if (s.assets) p.assets = *s.assets;
}

void EditCommand::apply(Project& p) {
    before_ = take(p);
    try {
        fn_(p);
    } catch (...) {
        restore(p, before_);
        throw;
    }
}

void EditCommand::revert(Project& p) { restore(p, before_); }

void CommandHistory::execute(Project& p, std::unique_ptr<Command> cmd) {
    cmd->apply(p);
    undo_.push_back(std::move(cmd));
    if (undo_.size() > limit_) undo_.pop_front();
    redo_.clear();
    ++revision_;
}

bool CommandHistory::undo(Project& p) {
    if (undo_.empty()) return false;
    auto cmd = std::move(undo_.back());
    undo_.pop_back();
    cmd->revert(p);
    redo_.push_back(std::move(cmd));
    ++revision_;
    return true;
}

bool CommandHistory::redo(Project& p) {
    if (redo_.empty()) return false;
    auto cmd = std::move(redo_.back());
    redo_.pop_back();
    cmd->apply(p);
    undo_.push_back(std::move(cmd));
    ++revision_;
    return true;
}

void CommandHistory::clear() {
    undo_.clear();
    redo_.clear();
    ++revision_;
}

}  // namespace vstudio
