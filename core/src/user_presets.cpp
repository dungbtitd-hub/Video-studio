#include "vstudio/user_presets.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

#include "vstudio/clip_tools.hpp"
#include "vstudio/color_space.hpp"
#include "vstudio/export_presets.hpp"
#include "vstudio/grade.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/project_io.hpp"
#include "detail/paths.hpp"

namespace vstudio::presets {

namespace {

bool knownKind(const std::string& kind) { return kind == kStyle || kind == kExport; }

std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    std::string out = s.substr(a, b - a + 1);
    if (out.size() > kMaxNameLength) {
        out.resize(kMaxNameLength);
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();  // UTF-8 tail
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80)) out.pop_back();  // its lead byte
    }
    return out;
}

bool fail(std::string* error, const std::string& msg) {
    if (error) *error = msg;
    return false;
}

std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

UserPresetStore::UserPresetStore(std::string path) : path_(std::move(path)) {
    try {
        std::error_code ec;
        if (path_.empty() || !std::filesystem::exists(detail::toPath(path_), ec)) return;
        const Json j = Json::parse(readFile(path_));
        for (const char* kind : {kStyle, kExport}) {
            if (!j.contains(kind) || !j[kind].is_array()) continue;
            auto& list = items_[kind];
            std::set<std::string> seen;
            for (const auto& e : j[kind]) {
                if (!e.is_object() || !e.contains("name") || !e["name"].is_string()) continue;
                const std::string name = trimmed(e["name"].get<std::string>());
                if (name.empty() || !seen.insert(name).second) continue;
                list.push_back({name, e.value("saved", std::int64_t{0}), e.value("data", Json::object())});
            }
        }
    } catch (const std::exception&) {
        items_.clear();  // broken file: start empty, rewritten on the next change
    }
}

const std::vector<UserPreset>& UserPresetStore::list(const std::string& kind) const {
    static const std::vector<UserPreset> none;
    const auto it = items_.find(kind);
    return it == items_.end() ? none : it->second;
}

const UserPreset* UserPresetStore::find(const std::string& kind, const std::string& name) const {
    const std::string n = trimmed(name);
    for (const auto& p : list(kind))
        if (p.name == n) return &p;
    return nullptr;
}

bool UserPresetStore::put(const std::string& kind, const std::string& name, const Json& data, std::string* error) {
    if (!knownKind(kind)) return fail(error, "unknown preset kind: " + kind);
    const std::string n = trimmed(name);
    if (n.empty()) return fail(error, "preset needs a name");
    auto& v = items_[kind];
    const auto before = v;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const UserPreset& p) { return p.name == n; }), v.end());
    v.insert(v.begin(), UserPreset{n, now(), data});
    if (!save(error)) {
        v = before;
        return false;
    }
    return true;
}

bool UserPresetStore::remove(const std::string& kind, const std::string& name, std::string* error) {
    const std::string n = trimmed(name);
    auto it = items_.find(kind);
    if (it == items_.end()) return fail(error, "preset not found: " + n);
    auto& v = it->second;
    const auto before = v;
    const auto e = std::remove_if(v.begin(), v.end(), [&](const UserPreset& p) { return p.name == n; });
    if (e == v.end()) return fail(error, "preset not found: " + n);
    v.erase(e, v.end());
    if (!save(error)) {
        v = before;
        return false;
    }
    return true;
}

bool UserPresetStore::rename(const std::string& kind, const std::string& from, const std::string& to, std::string* error) {
    const std::string a = trimmed(from), b = trimmed(to);
    if (b.empty()) return fail(error, "preset needs a name");
    auto it = items_.find(kind);
    if (it == items_.end()) return fail(error, "preset not found: " + a);
    auto& v = it->second;
    if (a == b) return find(kind, a) ? true : fail(error, "preset not found: " + a);
    if (find(kind, b)) return fail(error, "a preset with that name already exists: " + b);
    const auto before = v;
    bool found = false;
    for (auto& p : v)
        if (p.name == a) {
            p.name = b;
            found = true;
        }
    if (!found) return fail(error, "preset not found: " + a);
    if (!save(error)) {
        v = before;
        return false;
    }
    return true;
}

Json UserPresetStore::toJson() const {
    Json j{{"version", 1}};
    for (const char* kind : {kStyle, kExport}) {
        Json arr = Json::array();
        for (const auto& p : list(kind)) arr.push_back({{"name", p.name}, {"saved", p.saved}, {"data", p.data}});
        j[kind] = std::move(arr);
    }
    return j;
}

bool UserPresetStore::save(std::string* error) const {
    if (path_.empty()) return true;  // in-memory store (tests, no app folder yet)
    try {
        writeFileAtomic(path_, toJson().dump(1));
        return true;
    } catch (const std::exception& e) {
        return fail(error, e.what());
    }
}

// ---------------------------------------------------------------- style

bool isStyleEffect(const std::string& type) {
    static const std::set<std::string> perClip{
        "builtin.cover",   "builtin.stabilize", "builtin.denoise", "builtin.eq",
        "builtin.text",    "builtin.sticker",   "builtin.overlay", "builtin.cpu_compositor",
    };
    return !type.empty() && !perClip.count(type);
}

Json captureStyle(const Clip& clip) {
    Json fx = Json::array();
    if (clip.effects.is_array())
        for (const auto& e : clip.effects) {
            if (!e.is_object() || !isStyleEffect(e.value("type", std::string{}))) continue;
            fx.push_back({{"type", e["type"]}, {"enabled", e.value("enabled", true)}, {"params", e.value("params", Json::object())}});
        }
    return {{"effects", std::move(fx)}};
}

Json applyStyle(const Json& clipEffects, const Json& style, const std::function<std::string()>& newId) {
    Json front = Json::array(), rest = Json::array();
    if (style.is_object() && style.contains("effects") && style["effects"].is_array())
        for (const auto& e : style["effects"]) {
            if (!e.is_object()) continue;
            const std::string type = e.value("type", std::string{});
            if (!isStyleEffect(type)) continue;
            Json fx{{"effect_id", newId()}, {"type", type}, {"enabled", e.value("enabled", true)},
                    {"params", e.value("params", Json::object())}};
            (type == kColorSpaceEffect ? front : rest).push_back(std::move(fx));
        }
    Json out = front;
    bool placed = false;
    if (clipEffects.is_array())
        for (const auto& e : clipEffects) {
            if (e.is_object() && isStyleEffect(e.value("type", std::string{}))) {
                if (!placed)
                    for (const auto& r : rest) out.push_back(r);
                placed = true;
                continue;
            }
            out.push_back(e);
        }
    if (!placed)
        for (const auto& r : rest) out.push_back(r);
    return out;
}

std::string describeStyle(const Json& style) {
    std::vector<std::string> parts;
    if (style.is_object() && style.contains("effects") && style["effects"].is_array())
        for (const auto& e : style["effects"]) {
            const std::string type = e.value("type", std::string{});
            const Json params = e.value("params", Json::object());
            std::string part;
            if (type == kColorSpaceEffect) part = "Log";
            else if (type == kColorGradeEffect) part = "chỉnh màu";
            else if (type == kColorLookEffect) {
                const ColorLook* l = findColorLook(params.value("look", std::string{}));
                part = l ? "look " + l->name : "look";
            } else if (type == kLutEffect) part = "LUT";
            else if (type == cliptools::kChromaKeyEffect) part = "phông xanh";
            else if (type.rfind("builtin.fx_", 0) == 0) part = "hiệu ứng";
            else part = "plugin";
            if (std::find(parts.begin(), parts.end(), part) == parts.end()) parts.push_back(part);
        }
    if (parts.empty()) return "Không có hiệu ứng";
    std::string s;
    for (const auto& p : parts) s += (s.empty() ? "" : " · ") + p;
    return s;
}

// ---------------------------------------------------------------- export

Json toJson(const UserExport& e) {
    Json j{{"base", e.base}, {"format", e.format}, {"audio", e.audio}};
    if (e.fps > 0) j["fps"] = e.fps;
    if (e.videoMbps > 0) j["video_mbps"] = e.videoMbps;
    if (e.audioKbps > 0) j["audio_kbps"] = e.audioKbps;
    return j;
}

std::optional<UserExport> userExportFromJson(const Json& j, std::string* error) {
    if (!j.is_object()) {
        fail(error, "export preset must be an object");
        return std::nullopt;
    }
    UserExport e;
    try {
        e.base = j.value("base", e.base);
        e.format = j.value("format", e.format);
        e.fps = std::clamp(j.value("fps", 0.0), 0.0, 240.0);
        e.videoMbps = std::clamp(j.value("video_mbps", 0.0), 0.0, 400.0);
        e.audioKbps = std::clamp(j.value("audio_kbps", 0), 0, 512);
        e.audio = j.value("audio", true);
    } catch (const std::exception& ex) {
        fail(error, std::string("bad export preset: ") + ex.what());
        return std::nullopt;
    }
    if (!findExportPreset(e.base)) {
        fail(error, "unknown export quality: " + e.base);
        return std::nullopt;
    }
    if (!findExportFormat(e.format)) {
        fail(error, "unknown export format: " + e.format);
        return std::nullopt;
    }
    return e;
}

ExportSettings exportSettingsFor(const Project& p, const UserExport& e) {
    const ExportPreset* base = findExportPreset(e.base);
    if (!base) base = findExportPreset("fhd1080");
    ExportSettings s = vstudio::exportSettingsFor(p, *base);
    if (e.fps > 0) {
        // Common rates as exact rationals (29.97 = 30000/1001), others rounded to 1/1000.
        const double f = e.fps;
        if (std::abs(f - 23.976) < 0.01) s.fps = {24000, 1001};
        else if (std::abs(f - 29.97) < 0.01) s.fps = {30000, 1001};
        else if (std::abs(f - 59.94) < 0.01) s.fps = {60000, 1001};
        else if (std::abs(f - std::round(f)) < 1e-6) s.fps = {static_cast<std::int64_t>(std::lround(f)), 1};
        else s.fps = {static_cast<std::int64_t>(std::lround(f * 1000)), 1000};
        // Keep the preset's quality per frame when only the rate changes.
        const double fps = static_cast<double>(s.fps.num) / s.fps.den;
        s.videoBitrate = static_cast<std::int64_t>(
            std::clamp(static_cast<double>(s.width) * s.height * fps * base->bitsPerPixel, 1e6, 100e6));
    }
    applyExportFormat(s, e.format);
    if (e.videoMbps > 0) s.videoBitrate = static_cast<std::int64_t>(std::clamp(e.videoMbps, 0.5, 400.0) * 1e6);
    if (e.audioKbps > 0) s.audioBitrate = std::int64_t{std::clamp(e.audioKbps, 32, 512)} * 1000;
    s.includeAudio = e.audio;
    return s;
}

std::string describeExport(const UserExport& e) {
    const ExportPreset* base = findExportPreset(e.base);
    const ExportFormat* fmt = findExportFormat(e.format);
    std::string s = base ? base->name : e.base;
    s += " · " + (fmt ? fmt->name : e.format);
    auto num = [](double v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, std::abs(v - std::round(v)) < 1e-6 ? "%.0f" : "%.2f", v);
        return std::string(buf);
    };
    if (e.fps > 0) s += " · " + num(e.fps) + " khung/giây";
    if (e.videoMbps > 0) s += " · " + num(e.videoMbps) + " Mbit/s";
    if (e.audioKbps > 0 && e.audio) s += " · âm thanh " + std::to_string(e.audioKbps) + " kbit/s";
    if (!e.audio) s += " · không tiếng";
    return s;
}

}  // namespace vstudio::presets
