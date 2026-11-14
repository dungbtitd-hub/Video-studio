#include "vstudio/recents.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

#include "detail/paths.hpp"
#include "vstudio/pixel.hpp"
#include "vstudio/project_io.hpp"

namespace vstudio::recents {

namespace fs = std::filesystem;

namespace {

constexpr int kPosterMax = 256;
constexpr char kPosterMagic[4] = {'V', 'S', 'P', '1'};

fs::path pkg(const std::string& dir) { return detail::toPath(dir); }

std::string key(const std::string& dir) {
    return detail::fromPathGeneric(pkg(dir).lexically_normal());
}

std::optional<Json> readJson(const fs::path& f) {
    std::error_code ec;
    if (!fs::is_regular_file(f, ec)) return std::nullopt;
    try {
        return Json::parse(readFile(detail::fromPath(f)));
    } catch (...) {
        return std::nullopt;
    }
}

std::int64_t mtimeSeconds(const fs::path& f) {
    std::error_code ec;
    const auto ft = fs::last_write_time(f, ec);
    if (ec) return 0;
    using namespace std::chrono;
    const auto sys = time_point_cast<system_clock::duration>(ft - fs::file_time_type::clock::now() + system_clock::now());
    return duration_cast<seconds>(sys.time_since_epoch()).count();
}

int clipCount(const Project& p) {
    int n = 0;
    for (const auto& t : p.tracks) n += static_cast<int>(t.clips.size());
    return n;
}

// Box-filtered downscale to at most kPosterMax on the long side.
VideoFrame shrink(const VideoFrame& in) {
    const VideoFrame* src = &in;
    VideoFrame tmp;
    if (in.deep()) {
        tmp = convertedFrame(in, PixelFormat::RGBA8);
        src = &tmp;
    }
    const int w = src->width, h = src->height;
    const double k = std::min(1.0, static_cast<double>(kPosterMax) / std::max(w, h));
    const int tw = std::max(1, static_cast<int>(w * k + 0.5)), th = std::max(1, static_cast<int>(h * k + 0.5));
    VideoFrame out;
    out.allocate(tw, th);
    for (int y = 0; y < th; ++y) {
        const int y0 = y * h / th, y1 = std::max(y0 + 1, (y + 1) * h / th);
        for (int x = 0; x < tw; ++x) {
            const int x0 = x * w / tw, x1 = std::max(x0 + 1, (x + 1) * w / tw);
            std::uint32_t acc[4] = {0, 0, 0, 0};
            for (int yy = y0; yy < y1; ++yy) {
                const std::uint8_t* p = src->rgba.data() + (static_cast<std::size_t>(yy) * w + x0) * 4;
                for (int xx = x0; xx < x1; ++xx, p += 4)
                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
            }
            const std::uint32_t n = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
            std::uint8_t* o = out.rgba.data() + (static_cast<std::size_t>(y) * tw + x) * 4;
            for (int c = 0; c < 4; ++c) o[c] = static_cast<std::uint8_t>((acc[c] + n / 2) / n);
        }
    }
    return out;
}

void put32(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

std::uint32_t get32(const std::string& s, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + i])) << (8 * i);
    return v;
}

}  // namespace

std::string contentHash(const Project& p) {
    Project copy = p;
    copy.createdAt.clear();
    copy.modifiedAt.clear();
    const std::string s = toJson(copy).dump();
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

void writeCard(const std::string& packageDir, const Project& p, const VideoFrame* preview) {
    Json j = {{"name", p.name},
              {"savedAt", static_cast<std::int64_t>(std::time(nullptr))},
              {"duration", p.duration().seconds()},
              {"clips", clipCount(p)},
              {"width", p.canvas.width},
              {"height", p.canvas.height},
              {"hash", contentHash(p)}};
    writeFileAtomic(detail::fromPath(pkg(packageDir) / "card.json"), j.dump());
    if (preview && !preview->empty()) {
        const VideoFrame small = shrink(*preview);
        std::string s(kPosterMagic, 4);
        put32(s, static_cast<std::uint32_t>(small.width));
        put32(s, static_cast<std::uint32_t>(small.height));
        s.append(reinterpret_cast<const char*>(small.rgba.data()), small.rgba.size());
        writeFileAtomic(detail::fromPath(pkg(packageDir) / "poster.rgba"), s);
    }
}

void markExported(const std::string& packageDir, const Project& p) {
    writeFileAtomic(detail::fromPath(pkg(packageDir) / "exported.json"), Json{{"hash", contentHash(p)}}.dump());
}

std::optional<ProjectCard> readCard(const std::string& packageDir) {
    const fs::path dir = pkg(packageDir);
    std::error_code ec;
    if (!fs::is_regular_file(dir / "project.json", ec)) return std::nullopt;
    ProjectCard c;
    c.path = packageDir;
    std::string hash;
    const auto card = readJson(dir / "card.json");
    if (card && card->is_object()) {
        c.name = card->value("name", std::string());
        c.savedAt = card->value("savedAt", std::int64_t{0});
        c.duration = card->value("duration", 0.0);
        c.clips = card->value("clips", 0);
        c.width = card->value("width", 0);
        c.height = card->value("height", 0);
        hash = card->value("hash", std::string());
    } else {
        // Saved before cards existed: read the project once (metadata only, no media).
        try {
            const Project p = loadProject(packageDir);
            c.name = p.name;
            c.savedAt = mtimeSeconds(dir / "project.json");
            c.duration = p.duration().seconds();
            c.clips = clipCount(p);
            c.width = p.canvas.width;
            c.height = p.canvas.height;
            hash = contentHash(p);
        } catch (...) {
            return std::nullopt;
        }
    }
    if (c.name.empty()) c.name = detail::fromPath(dir.lexically_normal().filename().stem());
    const auto exp = readJson(dir / "exported.json");
    c.exported = exp && exp->is_object() && !hash.empty() && exp->value("hash", std::string()) == hash;
    c.hasPoster = fs::is_regular_file(dir / "poster.rgba", ec);
    return c;
}

bool readPoster(const std::string& packageDir, VideoFrame& out) {
    std::string s;
    try {
        s = readFile(detail::fromPath(pkg(packageDir) / "poster.rgba"));
    } catch (...) {
        return false;
    }
    if (s.size() < 12 || s.compare(0, 4, std::string(kPosterMagic, 4)) != 0) return false;
    const std::uint32_t w = get32(s, 4), h = get32(s, 8);
    if (w == 0 || h == 0 || w > 4096 || h > 4096 || s.size() != 12 + static_cast<std::size_t>(w) * h * 4) return false;
    out.allocate(static_cast<int>(w), static_cast<int>(h));
    std::copy(s.begin() + 12, s.end(), reinterpret_cast<char*>(out.rgba.data()));
    return true;
}

std::vector<ProjectCard> scanFolder(const std::string& dirUtf8) {
    std::vector<ProjectCard> out;
    std::error_code ec;
    const fs::path dir = pkg(dirUtf8);
    if (dirUtf8.empty() || !fs::is_directory(dir, ec)) return out;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec) || it->path().extension() != ".vstudio") continue;
        if (auto c = readCard(detail::fromPath(it->path()))) out.push_back(std::move(*c));
    }
    std::sort(out.begin(), out.end(), [](const ProjectCard& a, const ProjectCard& b) { return a.savedAt > b.savedAt; });
    return out;
}

RecentList::RecentList(std::string file) : file_(std::move(file)) {}

std::vector<std::string> RecentList::paths() const {
    std::vector<std::string> out;
    const auto j = readJson(pkg(file_));
    if (!j || !j->is_array()) return out;
    std::error_code ec;
    for (const auto& e : *j)
        if (e.is_string() && fs::is_regular_file(pkg(e.get<std::string>()) / "project.json", ec)) out.push_back(e.get<std::string>());
    return out;
}

void RecentList::write(const std::vector<std::string>& paths) const {
    writeFileAtomic(file_, Json(paths).dump());
}

void RecentList::touch(const std::string& packageDir) {
    auto p = paths();
    const std::string k = key(packageDir);
    p.erase(std::remove_if(p.begin(), p.end(), [&](const std::string& s) { return key(s) == k; }), p.end());
    p.insert(p.begin(), packageDir);
    if (p.size() > kMax) p.resize(kMax);
    write(p);
}

void RecentList::remove(const std::string& packageDir) {
    auto p = paths();
    const std::string k = key(packageDir);
    p.erase(std::remove_if(p.begin(), p.end(), [&](const std::string& s) { return key(s) == k; }), p.end());
    write(p);
}

std::vector<ProjectCard> startCards(const std::string& recentFile, const std::string& scanDir, bool unfinishedOnly,
                                    std::size_t max, const std::string& exclude) {
    std::vector<ProjectCard> all;
    std::set<std::string> seen;
    if (!exclude.empty()) seen.insert(key(exclude));
    auto take = [&](ProjectCard c) {
        if (!seen.insert(key(c.path)).second) return;
        if (unfinishedOnly && c.exported) return;
        all.push_back(std::move(c));
    };
    if (!recentFile.empty())
        for (const auto& p : RecentList(recentFile).paths())
            if (auto c = readCard(p)) take(std::move(*c));
    for (auto& c : scanFolder(scanDir)) take(std::move(c));
    std::stable_sort(all.begin(), all.end(), [](const ProjectCard& a, const ProjectCard& b) { return a.savedAt > b.savedAt; });
    if (all.size() > max) all.resize(max);
    return all;
}

Json toJson(const ProjectCard& c) {
    return {{"path", c.path},       {"name", c.name},     {"savedAt", c.savedAt},   {"duration", c.duration},
            {"clips", c.clips},     {"width", c.width},   {"height", c.height},     {"exported", c.exported},
            {"hasPoster", c.hasPoster}};
}

}  // namespace vstudio::recents
