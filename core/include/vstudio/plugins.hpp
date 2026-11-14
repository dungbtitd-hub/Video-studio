// Plugin loader (Plugin API beta, FR-029): third-party effects in shared libraries, through the
// stable C ABI in capi/include/vstudio/vstudio_plugin.h.
//
// loadPlugins() reads only the manifests (<dir>/<plugin>/plugin.json) and registers one module
// per effect. The library itself is opened the first time one of its effects is used, so a
// project that never uses a plugin never loads it. A plugin that is missing, has another ABI or
// fails to load is reported (the module's factory returns an error) and never crashes the app.
// iOS and Android never load plugins.
#pragma once

#include <string>
#include <vector>

#include "vstudio/modules.hpp"

namespace vstudio::plugins {

struct PluginEffect {
    std::string id;
    std::string name;
    bool audio = false;
    Json params;  // default params from the manifest
};

struct PluginInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string folder;
    std::string library;  // full path of the library for this OS
    std::vector<PluginEffect> effects;
};

struct LoadResult {
    std::vector<PluginInfo> plugins;
    std::vector<std::string> errors;  // one line per plugin that was skipped
};

// Whether this platform loads plugins at all (false on iOS and Android).
bool supported();

// Registers the effects of every plugin under `dir`. Effects whose id is already taken
// are skipped (with an error line). Safe to call for a folder that does not exist.
LoadResult loadPlugins(ModuleRegistry& r, const std::string& dir);

}  // namespace vstudio::plugins
