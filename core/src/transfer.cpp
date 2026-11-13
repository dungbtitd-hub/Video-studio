#include "vstudio/transfer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

#include "detail/paths.hpp"
#include "vstudio/project_io.hpp"

namespace fs = std::filesystem;

namespace vstudio {

namespace {

constexpr std::size_t kBlock = 512;
constexpr std::size_t kChunk = 1 << 20;
constexpr const char* kMarker = "vstudiopkg.json";

struct Cancelled {};

void checkCancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) throw Cancelled{};
}

// ---- tar header -------------------------------------------------------------

void putOctal(char* field, std::size_t width, std::uint64_t v) {
    std::snprintf(field, width, "%0*llo", static_cast<int>(width - 1), static_cast<unsigned long long>(v));
}

void putSize(char* field, std::uint64_t size) {
    if (size < (1ull << 33)) return putOctal(field, 12, size);
    // GNU base-256: high bit set, big-endian binary in the remaining bytes.
    std::memset(field, 0, 12);
    field[0] = static_cast<char>(0x80);
    for (int i = 11; i >= 4; --i, size >>= 8) field[i] = static_cast<char>(size & 0xff);
}

std::uint64_t getSize(const char* field) {
    if (static_cast<unsigned char>(field[0]) & 0x80) {
        std::uint64_t v = 0;
        for (int i = 1; i < 12; ++i) v = (v << 8) | static_cast<unsigned char>(field[i]);
        return v;
    }
    std::uint64_t v = 0;
    for (int i = 0; i < 12 && field[i] >= '0' && field[i] <= '7'; ++i) v = v * 8 + (field[i] - '0');
    return v;
}

std::array<char, kBlock> header(const std::string& name, std::uint64_t size) {
    if (name.size() >= 100) throw std::runtime_error("entry name too long: " + name);
    std::array<char, kBlock> h{};
    std::memcpy(h.data(), name.data(), name.size());
    putOctal(h.data() + 100, 8, 0644);
    putOctal(h.data() + 108, 8, 0);
    putOctal(h.data() + 116, 8, 0);
    putSize(h.data() + 124, size);
    putOctal(h.data() + 136, 12, 0);
    h[156] = '0';
    std::memcpy(h.data() + 257, "ustar", 6);
    std::memcpy(h.data() + 263, "00", 2);
    std::memset(h.data() + 148, ' ', 8);
    unsigned sum = 0;
    for (char c : h) sum += static_cast<unsigned char>(c);
    std::snprintf(h.data() + 148, 8, "%06o", sum);
    h[155] = ' ';
    return h;
}

bool validHeader(const std::array<char, kBlock>& h) {
    unsigned sum = 0;
    for (int i = 0; i < static_cast<int>(kBlock); ++i) sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(h[i]);
    unsigned stored = 0;
    for (int i = 148; i < 156 && h[i] >= '0' && h[i] <= '7'; ++i) stored = stored * 8 + (h[i] - '0');
    return sum == stored;
}

void pad(std::ostream& out, std::uint64_t size) {
    static const char zeros[kBlock] = {};
    if (const auto r = size % kBlock) out.write(zeros, static_cast<std::streamsize>(kBlock - r));
}

void writeEntry(std::ostream& out, const std::string& name, const std::string& data) {
    const auto h = header(name, data.size());
    out.write(h.data(), kBlock);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    pad(out, data.size());
}

// Archive name for a media file: plain, unique, under 100 bytes with its extension kept.
std::string archiveName(const fs::path& file, std::set<std::string>& used) {
    std::string stem = detail::fromPath(file.stem()), ext = detail::fromPath(file.extension());
    for (char& c : stem)
        if (c == '/' || c == '\\' || c == ':') c = '_';
    const std::size_t room = 90 - std::min<std::size_t>(ext.size(), 12);
    if (stem.size() > room) stem.resize(room);
    std::string name = "media/" + stem + ext;
    for (int i = 2; used.count(name); ++i) name = "media/" + stem + " (" + std::to_string(i) + ")" + ext;
    used.insert(name);
    return name;
}

// Only "media/<file>" with a plain file name is accepted from a package.
bool safeMediaName(const std::string& n) {
    if (n.rfind("media/", 0) != 0 || n.size() <= 6) return false;
    const std::string file = n.substr(6);
    return file.find('/') == std::string::npos && file.find('\\') == std::string::npos && file != "." &&
           file != ".." && file.find('\0') == std::string::npos;
}

fs::path uniqueDir(const fs::path& base) {
    fs::path p = base;
    for (int i = 2; fs::exists(p); ++i) p = base.parent_path() / (detail::fromPath(base.stem()) + " " + std::to_string(i) + ".vstudio");
    return p;
}

}  // namespace

TransferResult packProject(const Project& project, const std::string& mediaBase, const std::string& outFile,
                           const TransferProgress& progress, const std::atomic<bool>* cancel) {
    TransferResult r;
    const fs::path out = detail::toPath(outFile), part = fs::path(out) += ".part";
    try {
        Project p = project;
        std::set<std::string> used;
        std::vector<std::pair<fs::path, std::string>> files;  // source, archive name
        std::map<std::string, std::string> nameOf;           // source path -> archive name (shared media)
        std::uint64_t total = 0;
        for (auto& a : p.assets) {
            if (a.uri.rfind("synthetic:", 0) == 0) continue;  // test media, no file
            fs::path src = detail::toPath(a.uri);
            if (src.is_relative() && !mediaBase.empty()) src = detail::toPath(mediaBase) / src;
            std::error_code ec;
            if (!fs::is_regular_file(src, ec)) {
                r.missingAssets.push_back(a.id);
                continue;
            }
            const std::string key = detail::fromPath(fs::weakly_canonical(src, ec));
            auto it = nameOf.find(key);
            if (it == nameOf.end()) {
                it = nameOf.emplace(key, archiveName(src, used)).first;
                files.emplace_back(src, it->second);
                total += fs::file_size(src);
            }
            a.uri = it->second;
            if (a.fingerprint.empty()) a.fingerprint = fingerprintFile(detail::fromPath(src));
        }
        std::ofstream f(part, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot write " + detail::fromPath(part));
        writeEntry(f, kMarker, Json{{"format", 1}, {"name", p.name}}.dump());
        writeEntry(f, "project.json", toJson(p).dump(2));
        std::vector<char> buf(kChunk);
        std::uint64_t done = 0;
        for (const auto& [src, name] : files) {
            const std::uint64_t size = fs::file_size(src);
            const auto h = header(name, size);
            f.write(h.data(), kBlock);
            std::ifstream in(src, std::ios::binary);
            if (!in) throw std::runtime_error("cannot read " + detail::fromPath(src));
            for (std::uint64_t left = size; left > 0;) {
                checkCancel(cancel);
                const auto n = static_cast<std::streamsize>(std::min<std::uint64_t>(left, kChunk));
                in.read(buf.data(), n);
                if (in.gcount() != n) throw std::runtime_error("file changed while packing: " + detail::fromPath(src));
                f.write(buf.data(), n);
                left -= static_cast<std::uint64_t>(n);
                done += static_cast<std::uint64_t>(n);
                if (progress && total) progress(static_cast<double>(done) / total);
            }
            pad(f, size);
        }
        static const char end[kBlock * 2] = {};
        f.write(end, sizeof end);
        f.close();
        if (!f) throw std::runtime_error("write failed: " + detail::fromPath(part));
        fs::rename(part, out);
        if (progress) progress(1.0);
        r.ok = true;
    } catch (const Cancelled&) {
        r.cancelled = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    if (!r.ok) {
        std::error_code ec;
        fs::remove(part, ec);
    }
    return r;
}

TransferResult unpackProject(const std::string& packageFile, const std::string& destRoot,
                             const TransferProgress& progress, const std::atomic<bool>* cancel) {
    TransferResult r;
    std::vector<fs::path> written;  // removed again if anything fails
    try {
        const fs::path src = detail::toPath(packageFile), root = detail::toPath(destRoot);
        std::ifstream in(src, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open " + packageFile);
        const std::uint64_t total = fs::file_size(src);
        fs::create_directories(root / "media");
        std::string projectJson;
        bool marker = false;
        std::map<std::string, std::string> renamed;  // archive name -> uri used in the project
        std::map<std::string, std::string> fingerprintOf;  // archive name -> fingerprint (from project.json)
        std::vector<char> buf(kChunk);
        std::array<char, kBlock> h{};
        while (in.read(h.data(), kBlock)) {
            checkCancel(cancel);
            if (std::all_of(h.begin(), h.end(), [](char c) { return c == 0; })) break;  // end of archive
            if (!validHeader(h)) throw std::runtime_error("not a Video Studio package (bad header)");
            const std::string name(h.data(), strnlen(h.data(), 100));
            const std::uint64_t size = getSize(h.data() + 124);
            const std::uint64_t padded = (size + kBlock - 1) / kBlock * kBlock;
            if (!marker) {
                if (name != kMarker) throw std::runtime_error("not a Video Studio package");
                marker = true;
            }
            if (name == kMarker || name == "project.json") {
                if (size > (64u << 20)) throw std::runtime_error("project.json too large");
                std::string data(size, '\0');
                in.read(data.data(), static_cast<std::streamsize>(size));
                in.ignore(static_cast<std::streamsize>(padded - size));
                if (name == "project.json") {
                    projectJson = std::move(data);
                    for (const auto& a : projectFromJson(Json::parse(projectJson)).assets)
                        if (!a.fingerprint.empty()) fingerprintOf[a.uri] = a.fingerprint;
                }
                else if (Json::parse(data).value("format", 0) != 1) throw std::runtime_error("unsupported package version");
                continue;
            }
            if (!safeMediaName(name)) throw std::runtime_error("unsafe entry in package: " + name);
            // Same file already in media/ (also under a name an earlier import gave it): reuse it.
            // Name taken by another file: pick the next free name.
            const fs::path base = root / detail::toPath(name);
            fs::path dest = base;
            bool skip = false;
            for (int i = 2;; ++i) {
                std::error_code ec;
                if (!fs::exists(dest, ec)) break;
                if (fs::is_regular_file(dest, ec) && fs::file_size(dest, ec) == size &&
                    (!fingerprintOf.count(name) || fingerprintFile(detail::fromPath(dest)) == fingerprintOf[name])) {
                    skip = true;
                    break;
                }
                dest = base.parent_path() / (detail::fromPath(base.stem()) + " (" + std::to_string(i) + ")" +
                                             detail::fromPath(base.extension()));
            }
            renamed[name] = "media/" + detail::fromPath(dest.filename());
            if (skip) {
                in.ignore(static_cast<std::streamsize>(padded));
            } else {
                const fs::path part = fs::path(dest) += ".part";
                std::ofstream out(part, std::ios::binary | std::ios::trunc);
                if (!out) throw std::runtime_error("cannot write " + detail::fromPath(part));
                written.push_back(part);
                for (std::uint64_t left = size; left > 0;) {
                    checkCancel(cancel);
                    const auto n = static_cast<std::streamsize>(std::min<std::uint64_t>(left, kChunk));
                    in.read(buf.data(), n);
                    if (in.gcount() != n) throw std::runtime_error("package is truncated");
                    out.write(buf.data(), n);
                    left -= static_cast<std::uint64_t>(n);
                    if (progress && total) progress(std::min(0.99, static_cast<double>(in.tellg()) / total));
                }
                in.ignore(static_cast<std::streamsize>(padded - size));
                out.close();
                if (!out) throw std::runtime_error("write failed: " + detail::fromPath(part));
                fs::rename(part, dest);
                written.back() = dest;
            }
        }
        if (!marker || projectJson.empty()) throw std::runtime_error("package has no project");
        Project p = projectFromJson(Json::parse(projectJson));
        for (auto& a : p.assets)
            if (auto it = renamed.find(a.uri); it != renamed.end()) a.uri = it->second;
        std::string safe = p.name.empty() ? "Project" : p.name;
        for (char& c : safe)
            if (std::strchr("\\/:*?\"<>|", c)) c = '_';
        const fs::path pkg = uniqueDir(root / detail::toPath(safe + ".vstudio"));
        saveProject(p, detail::fromPath(pkg));
        r.packageDir = detail::fromPath(pkg);
        if (progress) progress(1.0);
        r.ok = true;
        return r;
    } catch (const Cancelled&) {
        r.cancelled = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    for (const auto& w : written) {
        std::error_code ec;
        fs::remove(w, ec);
    }
    return r;
}

}  // namespace vstudio
