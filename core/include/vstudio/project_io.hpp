// Video Studio Core - .vstudio package I/O, autosave and asset helpers.
// Package layout (P0, "Project Linked" mode):
//   MyFilm.vstudio/
//     project.json        manifest + timeline (schema v0)
//     project.json.bak    previous save
//     recovery/autosave.json
// Media is referenced by relative path when it lives under the package's
// parent folder; asset_id + fingerprint are used for relink.
#pragma once

#include <optional>
#include <string>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio {

// Writes `content` to `path` via a temp file + rename, so a crash never leaves a half-written file.
void writeFileAtomic(const std::string& path, const std::string& content);
std::string readFile(const std::string& path);

void saveProject(const Project& p, const std::string& packageDir);
Project loadProject(const std::string& packageDir);

void writeAutosave(const Project& p, const std::string& packageDir);
std::optional<Project> loadAutosave(const std::string& packageDir);
void clearAutosave(const std::string& packageDir);

// size + FNV-1a over the first and last 64 KiB: cheap even for multi-GB files.
std::string fingerprintFile(const std::string& path);
// Path stored in the project: relative to the package's parent folder when possible.
std::string makeUri(const std::string& mediaPath, const std::string& packageDir);
// Folder that relative URIs resolve against.
std::string mediaBaseDir(const std::string& packageDir);

MediaAsset assetFromProbe(const Id& id, const std::string& path, const ProbeResult& probe, const std::string& packageDir);

std::string newId();
std::string nowIso8601();

}  // namespace vstudio
