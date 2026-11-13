#include "vstudio/project_io.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "detail/paths.hpp"
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace vstudio {

namespace fs = std::filesystem;

void writeFileAtomic(const std::string& path, const std::string& content) {
    const fs::path target = detail::toPath(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path());
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot write " + tmp.string());
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.flush();
        if (!f) throw std::runtime_error("write failed: " + tmp.string());
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);  // atomic replace on POSIX and on Windows (MoveFileEx REPLACE_EXISTING)
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) throw std::runtime_error("cannot replace " + target.string() + ": " + ec.message());
    }
}

std::string readFile(const std::string& path) {
    std::ifstream f(detail::toPath(path), std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void saveProject(const Project& p, const std::string& packageDir) {
    const fs::path dir = detail::toPath(packageDir);
    fs::create_directories(dir);
    const fs::path manifest = dir / "project.json";
    std::error_code ec;
    if (fs::exists(manifest)) fs::copy_file(manifest, dir / "project.json.bak", fs::copy_options::overwrite_existing, ec);
    Project copy = p;
    copy.modifiedAt = nowIso8601();
    if (copy.createdAt.empty()) copy.createdAt = copy.modifiedAt;
    writeFileAtomic(detail::fromPath(manifest), toJson(copy).dump(2));
}

Project loadProject(const std::string& packageDir) {
    const fs::path manifest = detail::toPath(packageDir) / "project.json";
    try {
        return projectFromJson(Json::parse(readFile(detail::fromPath(manifest))));
    } catch (const Json::exception& ex) {
        throw std::runtime_error(std::string("corrupt project.json: ") + ex.what());
    }
}

void writeAutosave(const Project& p, const std::string& packageDir) {
    writeFileAtomic(detail::fromPath(detail::toPath(packageDir) / "recovery" / "autosave.json"), toJson(p).dump());
}

std::optional<Project> loadAutosave(const std::string& packageDir) {
    const fs::path f = detail::toPath(packageDir) / "recovery" / "autosave.json";
    if (!fs::exists(f)) return std::nullopt;
    try {
        return projectFromJson(Json::parse(readFile(detail::fromPath(f))));
    } catch (...) {
        return std::nullopt;
    }
}

void clearAutosave(const std::string& packageDir) {
    std::error_code ec;
    fs::remove(detail::toPath(packageDir) / "recovery" / "autosave.json", ec);
}

std::string fingerprintFile(const std::string& path) {
    std::ifstream f(detail::toPath(path), std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    const std::uint64_t size = static_cast<std::uint64_t>(f.tellg());
    constexpr std::uint64_t kBlock = 64 * 1024;
    std::uint64_t h = 1469598103934665603ull;
    auto hashRange = [&](std::uint64_t off, std::uint64_t len) {
        std::string buf(static_cast<std::size_t>(len), '\0');
        f.seekg(static_cast<std::streamoff>(off));
        f.read(buf.data(), static_cast<std::streamsize>(len));
        for (char c : buf) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }
    };
    hashRange(0, std::min(size, kBlock));
    if (size > kBlock) hashRange(size - std::min(size - kBlock, kBlock), std::min(size - kBlock, kBlock));
    char out[64];
    std::snprintf(out, sizeof out, "%llu:%016llx", static_cast<unsigned long long>(size), static_cast<unsigned long long>(h));
    return out;
}

std::string mediaBaseDir(const std::string& packageDir) {
    const fs::path p = detail::toPath(packageDir);
    return p.has_parent_path() ? detail::fromPath(p.parent_path()) : std::string{};
}

std::string makeUri(const std::string& mediaPath, const std::string& packageDir) {
    const fs::path media = fs::absolute(detail::toPath(mediaPath)).lexically_normal();
    if (packageDir.empty()) return detail::fromPathGeneric(media);
    const fs::path base = fs::absolute(detail::toPath(mediaBaseDir(packageDir))).lexically_normal();
    const fs::path rel = media.lexically_relative(base);
    // Only keep it relative when the media sits under the base folder.
    if (!rel.empty() && *rel.begin() != "..") return detail::fromPathGeneric(rel);
    return detail::fromPathGeneric(media);
}

MediaAsset assetFromProbe(const Id& id, const std::string& path, const ProbeResult& pr, const std::string& packageDir) {
    MediaAsset a;
    a.id = id;
    a.uri = makeUri(path, packageDir);
    a.type = pr.isStillImage ? MediaType::Image : (pr.hasVideo ? MediaType::Video : MediaType::Audio);
    a.duration = pr.isStillImage ? Time::zero() : pr.duration;
    a.codec = pr.videoCodec;
    a.width = pr.width;
    a.height = pr.height;
    a.fps = pr.fps;
    a.vfr = pr.vfr;
    a.hasVideo = pr.hasVideo;
    if (pr.hasAudio) a.audio = AudioInfo{pr.sampleRate, pr.channels, pr.audioCodec};
    a.fingerprint = fingerprintFile(path);
    if (pr.rotation) a.extra["rotation"] = pr.rotation;
    // Colour description, used to suggest Log/HDR conversion (color_space.hpp).
    if (pr.hasVideo && pr.bitDepth != 8) a.extra["bit_depth"] = pr.bitDepth;
    if (!pr.transfer.empty()) a.extra["transfer"] = pr.transfer;
    if (!pr.primaries.empty()) a.extra["primaries"] = pr.primaries;
    return a;
}

std::string newId() {
    static thread_local std::mt19937_64 rng{std::random_device{}() ^
                                            static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())};
    char buf[33];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(rng()),
                  static_cast<unsigned long long>(rng()));
    return buf;
}

std::string nowIso8601() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace vstudio
