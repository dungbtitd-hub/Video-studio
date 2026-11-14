#include "vstudio/plugins.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "vstudio/audio_fx.hpp"
#include "vstudio/vstudio_plugin.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__ANDROID__)
#include <dlfcn.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace fs = std::filesystem;

namespace vstudio::plugins {

namespace {

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr bool kSupported = false;
#else
constexpr bool kSupported = true;
#endif

// One opened library, shared by all effects of the plugin; closed only at exit
// (effect instances may still be in use by render threads until then).
class Library {
public:
    explicit Library(std::string path) : path_(std::move(path)) {}

    // The plugin's info, opening the library on first call. Throws on failure.
    const vs_plugin_info& info() {
        std::lock_guard lock(mu_);
        if (info_) return *info_;
        if (!error_.empty()) throw std::runtime_error(error_);
        try {
            open();
        } catch (const std::exception& e) {
            error_ = e.what();
            throw;
        }
        return *info_;
    }

private:
    void open() {
        if constexpr (!kSupported) throw std::runtime_error("plugins are not supported on this platform");
        void* sym = nullptr;
#if defined(_WIN32)
        HMODULE h = LoadLibraryW(fs::path(path_).wstring().c_str());
        if (!h) throw std::runtime_error("cannot load " + path_);
        sym = reinterpret_cast<void*>(GetProcAddress(h, VS_PLUGIN_ENTRY_NAME));
#elif !defined(__ANDROID__)
        void* h = dlopen(path_.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            const char* e = dlerror();
            throw std::runtime_error("cannot load " + path_ + (e ? std::string(": ") + e : std::string()));
        }
        sym = dlsym(h, VS_PLUGIN_ENTRY_NAME);
#endif
        if (!sym) throw std::runtime_error(path_ + " has no " VS_PLUGIN_ENTRY_NAME);
        const auto entry = reinterpret_cast<vs_plugin_entry_fn>(sym);
        const vs_plugin_info* info = entry();
        if (!info) throw std::runtime_error(path_ + ": " VS_PLUGIN_ENTRY_NAME " returned null");
        if (info->abi != VS_PLUGIN_ABI)
            throw std::runtime_error(path_ + ": plugin ABI " + std::to_string(info->abi) + ", app expects " +
                                     std::to_string(VS_PLUGIN_ABI));
        info_ = info;
    }

    std::string path_;
    std::mutex mu_;
    const vs_plugin_info* info_ = nullptr;
    std::string error_;
};

class PluginVideoEffect final : public IVideoEffect {
public:
    explicit PluginVideoEffect(const vs_plugin_video_effect& fx) : fx_(fx) {}
    void apply(VideoFrame& frame, const Json& params, Time localTime) const override {
        if (frame.empty() || frame.deep() || !fx_.apply) return;
        const std::string p = params.is_null() ? "{}" : params.dump();
        vs_plugin_frame f{frame.width, frame.height, frame.width * 4, frame.rgba.data(), localTime.seconds()};
        fx_.apply(p.c_str(), &f);
    }

private:
    vs_plugin_video_effect fx_;
};

class PluginAudioEffect final : public IAudioEffect {
public:
    explicit PluginAudioEffect(const vs_plugin_audio_effect& fx) : fx_(fx) {}
    [[nodiscard]] std::int64_t margin(const Json& params, int sampleRate) const override {
        if (!fx_.margin) return 0;
        const std::string p = params.is_null() ? "{}" : params.dump();
        return std::max<std::int64_t>(0, fx_.margin(p.c_str(), sampleRate));
    }
    void apply(AudioBuffer& buf, const Json& params) const override {
        if (!fx_.apply || buf.samples.empty()) return;
        const std::string p = params.is_null() ? "{}" : params.dump();
        fx_.apply(p.c_str(), buf.samples.data(), buf.frames(), buf.channels, buf.sampleRate);
    }

private:
    vs_plugin_audio_effect fx_;
};

const char* libraryKey() {
    const std::string p = ModuleRegistry::currentPlatform();
    if (p == "windows") return "windows";
    if (p == "macos") return "macos";
    return "linux";
}

}  // namespace

bool supported() { return kSupported; }

LoadResult loadPlugins(ModuleRegistry& r, const std::string& dir) {
    LoadResult out;
    std::error_code ec;
    if (!kSupported || dir.empty() || !fs::is_directory(dir, ec)) return out;
    std::vector<fs::path> folders;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory(ec) && fs::exists(e.path() / "plugin.json", ec)) folders.push_back(e.path());
    std::sort(folders.begin(), folders.end());
    for (const auto& folder : folders) {
        const auto manifest = folder / "plugin.json";
        Json m;
        try {
            std::ifstream in(manifest);
            m = Json::parse(in);
        } catch (const std::exception& e) {
            out.errors.push_back(manifest.string() + ": " + e.what());
            continue;
        }
        if (!m.is_object() || m.value("abi", 0) != VS_PLUGIN_ABI) {
            out.errors.push_back(manifest.string() + ": unsupported plugin ABI");
            continue;
        }
        PluginInfo info;
        info.id = m.value("id", folder.filename().string());
        info.name = m.value("name", info.id);
        info.version = m.value("version", std::string("0.0.0"));
        info.folder = folder.string();
        const Json lib = m.value("library", Json::object());
        const std::string file = lib.is_object() ? lib.value(libraryKey(), std::string{}) : std::string{};
        if (file.empty() || file.find("..") != std::string::npos) {
            out.errors.push_back(info.id + ": no library for this platform");
            continue;
        }
        info.library = (folder / fs::path(std::u8string(file.begin(), file.end()))).string();
        auto library = std::make_shared<Library>(info.library);
        auto addEffects = [&](const char* key, bool audio) {
            for (const auto& fx : m.value(key, Json::array())) {
                if (!fx.is_object() || !fx.contains("id") || !fx["id"].is_string()) continue;
                PluginEffect pe{fx["id"].get<std::string>(), fx.value("name", fx["id"].get<std::string>()), audio,
                                fx.value("params", Json::object())};
                if (pe.id.rfind("builtin.", 0) == 0) {
                    out.errors.push_back(info.id + ": effect id " + pe.id + " is reserved");
                    continue;
                }
                ModuleInfo mi{.id = pe.id, .version = info.version, .displayName = pe.name,
                              .capabilities = {"plugin", audio ? "effect.audio.plugin" : "effect.plugin"}};
                const std::string id = pe.id;
                bool ok;
                if (audio) {
                    ok = r.add<IAudioEffect>(mi, [library, id]() -> std::shared_ptr<IAudioEffect> {
                        const auto& in = library->info();
                        for (int i = 0; i < in.audio_effect_count; ++i)
                            if (in.audio_effects[i].id && id == in.audio_effects[i].id)
                                return std::make_shared<PluginAudioEffect>(in.audio_effects[i]);
                        throw std::runtime_error("plugin library has no audio effect " + id);
                    });
                } else {
                    ok = r.add<IVideoEffect>(mi, [library, id]() -> std::shared_ptr<IVideoEffect> {
                        const auto& in = library->info();
                        for (int i = 0; i < in.video_effect_count; ++i)
                            if (in.video_effects[i].id && id == in.video_effects[i].id)
                                return std::make_shared<PluginVideoEffect>(in.video_effects[i]);
                        throw std::runtime_error("plugin library has no video effect " + id);
                    });
                }
                if (ok) info.effects.push_back(std::move(pe));
                else out.errors.push_back(info.id + ": effect id " + id + " already exists");
            }
        };
        addEffects("video_effects", false);
        addEffects("audio_effects", true);
        out.plugins.push_back(std::move(info));
    }
    return out;
}

}  // namespace vstudio::plugins
