// Video Studio Core - module registry.
//
// The core only defines contracts (interfaces + plain data). Every concrete
// capability - media backends (FFmpeg, Apple, MediaCodec), effects, and later
// exporters, AI tools, plugins - is a module that registers a factory here.
// A module is instantiated lazily on first use, receives data and returns a
// result; it shares no hidden state with the core. Adding a module never
// requires changing the core.
//
// A module interface declares its kind with `static constexpr const char* kModuleKind`.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio {

struct ModuleInfo {
    std::string id;                         // unique, e.g. "ffmpeg", "builtin.brightness"
    std::string kind;                       // filled from the interface's kModuleKind
    std::string version = "0.1.0";
    std::string displayName;
    std::vector<std::string> capabilities;  // e.g. "decode", "encode.h264", "effect.color"
    std::vector<std::string> platforms;     // empty = all
    int priority = 0;                       // higher wins when several modules fit
};

// ---- Contracts for module kinds defined by the core ----

// Video effect: frame in -> frame out. Must be stateless across calls
// (any per-clip state lives in `params`), so the render threads can share it.
class IVideoEffect {
public:
    static constexpr const char* kModuleKind = "effect.video";
    virtual ~IVideoEffect() = default;
    // `localTime` is the time since the clip start, for animated effects.
    virtual void apply(VideoFrame& frame, const Json& params, Time localTime) const = 0;
    // Handles RGBA16F frames. Effects that do not are given an 8-bit copy
    // (applyClipEffects converts once), so older modules keep working.
    [[nodiscard]] virtual bool supportsDeep() const { return false; }
};

class ModuleRegistry {
public:
    template <class Interface>
    using Factory = std::function<std::shared_ptr<Interface>()>;

    // Registers a module. Returns false if the id is already taken.
    template <class Interface>
    bool add(ModuleInfo info, Factory<Interface> factory) {
        info.kind = kindOf<Interface>();
        return addErased(std::move(info), [f = std::move(factory)]() -> std::shared_ptr<void> { return f(); });
    }

    // Instance of module `id` (created on first call, then shared). nullptr if
    // missing, of another kind, or its factory failed (error in *error).
    template <class Interface>
    std::shared_ptr<Interface> get(const std::string& id, std::string* error = nullptr) {
        return std::static_pointer_cast<Interface>(getErased(kindOf<Interface>(), id, error));
    }

    // Highest-priority module of this kind offering `capability` ("" = any) on this platform.
    template <class Interface>
    std::shared_ptr<Interface> best(const std::string& capability = {}, std::string* error = nullptr) {
        const auto id = bestId(kindOf<Interface>(), capability);
        if (id.empty()) {
            if (error) *error = std::string("no module of kind ") + kindOf<Interface>();
            return nullptr;
        }
        return get<Interface>(id, error);
    }

    [[nodiscard]] std::vector<ModuleInfo> list(const std::string& kind = {}) const;
    [[nodiscard]] bool has(const std::string& id) const;
    [[nodiscard]] bool loaded(const std::string& id) const;
    // Drops cached instances (e.g. to free memory); they are re-created on demand.
    void unloadAll();

    static const char* currentPlatform();  // "windows", "macos", "linux", "android", "ios"

private:
    template <class Interface>
    static const char* kindOf() { return Interface::kModuleKind; }
    bool addErased(ModuleInfo info, std::function<std::shared_ptr<void>()> factory);
    std::shared_ptr<void> getErased(const std::string& kind, const std::string& id, std::string* error);
    std::string bestId(const std::string& kind, const std::string& capability) const;

    struct Entry {
        ModuleInfo info;
        std::function<std::shared_ptr<void>()> factory;
        std::shared_ptr<void> instance;
    };
    mutable std::mutex mu_;
    std::vector<Entry> entries_;
};

// Built-in modules that need no external library (CPU effects).
void registerBuiltinModules(ModuleRegistry& r);

// Applies the enabled effects stored in clip.effects ({"type", "enabled", "params"}).
// Unknown effect types are skipped (and reported) but never removed from the project.
void applyClipEffects(ModuleRegistry& r, const Clip& clip, VideoFrame& frame, Time localTime,
                      std::vector<std::string>* warnings);

}  // namespace vstudio
