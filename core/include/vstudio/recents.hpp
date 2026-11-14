// Recent projects: what the app shows at start so the user can pick up unfinished work in one tap.
//
// Showing the list must be instant, so nothing here opens media or parses a big project:
//   <package>/card.json     written at every save: name, length, clip count, canvas, content hash
//   <package>/poster.rgba   a small picture (<= 256 px) of the preview at save time
//   <package>/exported.json content hash at the last successful export
// A project is "unfinished" when it changed since its last export (or was never exported).
// The recent list itself is a small JSON file of package paths, newest first (the desktop keeps one
// in its app data folder; mobile keeps every project in one folder and scans it).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio::recents {

struct ProjectCard {
    std::string path;  // the .vstudio package folder
    std::string name;
    std::int64_t savedAt = 0;  // unix seconds of the last save
    double duration = 0;       // seconds
    int clips = 0;
    int width = 0, height = 0;
    bool exported = false;  // exported after its last change
    bool hasPoster = false;
};

// Hash of the project's content (ignores save timestamps), so "changed since export" is exact.
std::string contentHash(const Project& p);

// Called after a save: writes card.json, and poster.rgba from `preview` when given (cheap downscale).
void writeCard(const std::string& packageDir, const Project& p, const VideoFrame* preview = nullptr);
// Called after a successful export of `p`.
void markExported(const std::string& packageDir, const Project& p);
// Reads the card (falls back to project.json for packages saved before cards existed).
std::optional<ProjectCard> readCard(const std::string& packageDir);
// The poster as RGBA8, or false when there is none.
bool readPoster(const std::string& packageDir, VideoFrame& out);

// Every package directly inside `dir`, newest first.
std::vector<ProjectCard> scanFolder(const std::string& dir);

// The recent list file: package paths, newest first, at most `kMax`; missing packages drop out.
class RecentList {
public:
    static constexpr std::size_t kMax = 30;
    explicit RecentList(std::string file);
    void touch(const std::string& packageDir);  // moves it to the front
    void remove(const std::string& packageDir);
    [[nodiscard]] std::vector<std::string> paths() const;

private:
    void write(const std::vector<std::string>& paths) const;
    std::string file_;
};

// Cards for the start screen: from `recentFile` and/or every package in `scanDir` (either may be
// empty), de-duplicated, newest first. `unfinishedOnly` keeps projects not exported since their last
// change; `exclude` (the open project) is left out.
std::vector<ProjectCard> startCards(const std::string& recentFile, const std::string& scanDir, bool unfinishedOnly,
                                    std::size_t max, const std::string& exclude = {});

Json toJson(const ProjectCard& c);

}  // namespace vstudio::recents
