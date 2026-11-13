#include "vstudio/modules.hpp"

#include "vstudio/compositor.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/pixel.hpp"

#include <algorithm>
#include <cmath>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace vstudio {

const char* ModuleRegistry::currentPlatform() {
#if defined(__ANDROID__)
    return "android";
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
    return "ios";
#else
    return "macos";
#endif
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

bool ModuleRegistry::addErased(ModuleInfo info, std::function<std::shared_ptr<void>()> factory) {
    std::lock_guard lk(mu_);
    for (const auto& e : entries_)
        if (e.info.id == info.id) return false;
    entries_.push_back({std::move(info), std::move(factory), nullptr});
    return true;
}

std::shared_ptr<void> ModuleRegistry::getErased(const std::string& kind, const std::string& id, std::string* error) {
    std::function<std::shared_ptr<void>()> factory;
    {
        std::lock_guard lk(mu_);
        auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.info.id == id; });
        if (it == entries_.end()) {
            if (error) *error = "module not found: " + id;
            return nullptr;
        }
        if (it->info.kind != kind) {
            if (error) *error = "module " + id + " is a " + it->info.kind + ", not " + kind;
            return nullptr;
        }
        if (it->instance) return it->instance;
        factory = it->factory;
    }
    // Create outside the lock: a factory may be slow (load a library, probe hardware).
    std::shared_ptr<void> inst;
    try {
        inst = factory();
    } catch (const std::exception& ex) {
        if (error) *error = "module " + id + " failed to start: " + ex.what();
        return nullptr;
    } catch (...) {
        if (error) *error = "module " + id + " failed to start";
        return nullptr;
    }
    if (!inst) {
        if (error) *error = "module " + id + " is unavailable on this device";
        return nullptr;
    }
    std::lock_guard lk(mu_);
    for (auto& e : entries_) {
        if (e.info.id != id) continue;
        if (!e.instance) e.instance = inst;  // another thread may have won the race
        return e.instance;
    }
    return inst;
}

std::string ModuleRegistry::bestId(const std::string& kind, const std::string& capability) const {
    std::lock_guard lk(mu_);
    const std::string plat = currentPlatform();
    const Entry* best = nullptr;
    for (const auto& e : entries_) {
        if (e.info.kind != kind) continue;
        if (!e.info.platforms.empty() &&
            std::find(e.info.platforms.begin(), e.info.platforms.end(), plat) == e.info.platforms.end())
            continue;
        if (!capability.empty() &&
            std::find(e.info.capabilities.begin(), e.info.capabilities.end(), capability) == e.info.capabilities.end())
            continue;
        if (!best || e.info.priority > best->info.priority) best = &e;
    }
    return best ? best->info.id : std::string{};
}

std::vector<ModuleInfo> ModuleRegistry::list(const std::string& kind) const {
    std::lock_guard lk(mu_);
    std::vector<ModuleInfo> out;
    for (const auto& e : entries_)
        if (kind.empty() || e.info.kind == kind) out.push_back(e.info);
    return out;
}

bool ModuleRegistry::has(const std::string& id) const {
    std::lock_guard lk(mu_);
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.info.id == id; });
}

bool ModuleRegistry::loaded(const std::string& id) const {
    std::lock_guard lk(mu_);
    return std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.info.id == id && e.instance; });
}

void ModuleRegistry::unloadAll() {
    std::lock_guard lk(mu_);
    for (auto& e : entries_) e.instance.reset();
}

// ------------------------------------------------------------------ built-ins

namespace {

std::uint8_t clamp8(double v) { return static_cast<std::uint8_t>(std::clamp(v + 0.5, 0.0, 255.0)); }

// Runs `fn(r, g, b)` (floats, 0..1 for 8-bit input) on every pixel of either format.
template <class Fn>
void forEachRgb(VideoFrame& f, Fn&& fn) {
    if (f.deep()) {
        for (std::size_t i = 0; i + 3 < f.half.size(); i += 4) {
            float c[3] = {halfToFloat(f.half[i]), halfToFloat(f.half[i + 1]), halfToFloat(f.half[i + 2])};
            fn(c[0], c[1], c[2]);
            for (int k = 0; k < 3; ++k) f.half[i + k] = floatToHalf(c[k]);
        }
        return;
    }
    for (std::size_t i = 0; i + 3 < f.rgba.size(); i += 4) {
        float c[3] = {f.rgba[i] / 255.0f, f.rgba[i + 1] / 255.0f, f.rgba[i + 2] / 255.0f};
        fn(c[0], c[1], c[2]);
        for (int k = 0; k < 3; ++k) f.rgba[i + k] = clamp8(c[k] * 255.0);
    }
}

// params: { "brightness": -1..1, "contrast": 0..2, "saturation": 0..2 }
class ColorAdjust final : public IVideoEffect {
public:
    bool supportsDeep() const override { return true; }
    void apply(VideoFrame& f, const Json& p, Time) const override {
        const float b = static_cast<float>(p.value("brightness", 0.0));
        const float c = static_cast<float>(p.value("contrast", 1.0));
        const float s = static_cast<float>(p.value("saturation", 1.0));
        constexpr float mid = 128.0f / 255.0f;
        forEachRgb(f, [&](float& r, float& g, float& bl) {
            const float l = 0.2126f * r + 0.7152f * g + 0.0722f * bl;
            r = (l + (r - l) * s - mid) * c + mid + b;
            g = (l + (g - l) * s - mid) * c + mid + b;
            bl = (l + (bl - l) * s - mid) * c + mid + b;
        });
    }
};

class ColorLookEffect final : public IVideoEffect {
public:
    bool supportsDeep() const override { return true; }
    void apply(VideoFrame& f, const Json& p, Time) const override { applyColorLook(f, p); }
};

class Grayscale final : public IVideoEffect {
public:
    bool supportsDeep() const override { return true; }
    void apply(VideoFrame& f, const Json& p, Time) const override {
        const float amount = static_cast<float>(std::clamp(p.value("amount", 1.0), 0.0, 1.0));
        forEachRgb(f, [&](float& r, float& g, float& b) {
            const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            r += (l - r) * amount;
            g += (l - g) * amount;
            b += (l - b) * amount;
        });
    }
};

}  // namespace

void registerBuiltinModules(ModuleRegistry& r) {
    // CPU reference compositor: always available; GPU compositors register with a higher priority.
    r.add<ICompositor>({.id = "builtin.cpu_compositor", .displayName = "CPU compositor", .capabilities = {"composite"}},
                       [] { return createCpuCompositor(); });
    r.add<IVideoEffect>({.id = "builtin.color_adjust", .displayName = "Color adjust", .capabilities = {"effect.color"}},
                        [] { return std::make_shared<ColorAdjust>(); });
    r.add<IVideoEffect>({.id = kColorLookEffect, .displayName = "Color look", .capabilities = {"effect.color"}},
                        [] { return std::make_shared<ColorLookEffect>(); });
    r.add<IVideoEffect>({.id = "builtin.grayscale", .displayName = "Grayscale", .capabilities = {"effect.color"}},
                        [] { return std::make_shared<Grayscale>(); });
}

void applyClipEffects(ModuleRegistry& r, const Clip& clip, VideoFrame& frame, Time localTime,
                      std::vector<std::string>* warnings) {
    if (!clip.effects.is_array()) return;
    for (const auto& fx : clip.effects) {
        if (!fx.is_object() || !fx.value("enabled", true)) continue;
        const std::string type = fx.value("type", std::string{});
        std::string err;
        auto effect = r.get<IVideoEffect>(type, &err);
        if (!effect) {
            if (warnings) warnings->push_back("effect unavailable (kept in project): " + type);
            continue;
        }
        if (frame.deep() && !effect->supportsDeep()) convertFrame(frame, PixelFormat::RGBA8);
        try {
            effect->apply(frame, fx.value("params", Json::object()), localTime);
        } catch (const std::exception& ex) {
            if (warnings) warnings->push_back("effect " + type + " failed: " + ex.what());
        }
    }
}

}  // namespace vstudio
