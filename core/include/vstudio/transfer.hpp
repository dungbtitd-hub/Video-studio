// Moving a project between machines (desktop <-> mobile) as one file.
//
// A ".vstudiopkg" is a POSIX tar (ustar; GNU base-256 sizes for files over
// 8 GiB) with, in order:
//   vstudiopkg.json   {"format":1,"name":...}   marks the file as ours
//   project.json      the project, every media URI rewritten to "media/<file>"
//   media/<file>...   the media files, stored as-is (already compressed)
// Tar keeps it streamable: packing never loads a whole video into memory and
// unpacking writes each file through a ".part" name, so an interrupted copy
// never looks complete. Entry names are checked, so a crafted package cannot
// write outside the destination folder.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "vstudio/model.hpp"

namespace vstudio {

struct TransferResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string packageDir;           // unpack: the new project's .vstudio folder
    std::vector<Id> missingAssets;    // pack: media not found, left out (relink later)
};

using TransferProgress = std::function<void(double)>;  // 0..1, by bytes

// `mediaBase` resolves relative media URIs (mediaBaseDir(packageDir)).
TransferResult packProject(const Project& project, const std::string& mediaBase, const std::string& outFile,
                           const TransferProgress& progress = {}, const std::atomic<bool>* cancel = nullptr);

// Extracts into `destRoot`: the project as destRoot/<name>.vstudio (a new name if taken) and its
// media into destRoot/media/ (a file already there with the same name and size is reused; a
// different one gets a new name and the project is updated to match).
TransferResult unpackProject(const std::string& packageFile, const std::string& destRoot,
                             const TransferProgress& progress = {}, const std::atomic<bool>* cancel = nullptr);

}  // namespace vstudio
