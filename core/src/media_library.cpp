#include "vstudio/media_library.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "detail/paths.hpp"
#include "vstudio/project_io.hpp"

namespace vstudio {

namespace fs = std::filesystem;

struct MediaLibrary::State {
    std::shared_ptr<IMediaBackend> backend;
    std::string cacheDir;
    mutable std::mutex mu;
    Listener listener;
    std::map<Id, std::shared_ptr<const ThumbnailStrip>> thumbs;
    std::map<Id, std::shared_ptr<const Waveform>> waves;
    std::set<std::string> inflight;  // "<kind>:<assetId>"
    std::vector<CancelToken> tokens;
    std::atomic<bool> closed{false};

    void notify(const Id& id, Kind k, bool ok) {
        Listener l;
        {
            std::lock_guard lk(mu);
            l = listener;
        }
        if (l && !closed) l(id, k, ok);
    }
};

namespace {

// Cache file name from the fingerprint ("size:hash"), or nothing without one.
std::string cacheFile(const std::string& dir, const MediaAsset& a, const std::string& suffix) {
    if (dir.empty() || a.fingerprint.empty()) return {};
    std::string key = a.fingerprint;
    std::replace(key.begin(), key.end(), ':', '_');
    return detail::fromPath(detail::toPath(dir) / (key + suffix));
}

template <class T>
void put(std::ofstream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof v);
}
template <class T>
bool get(std::ifstream& f, T& v) {
    return static_cast<bool>(f.read(reinterpret_cast<char*>(&v), sizeof v));
}

constexpr std::uint32_t kThumbMagic = 0x31485456;  // "VTH1"
constexpr std::uint32_t kWaveMagic = 0x31575756;   // "VWW1"

void writeThumbs(const std::string& path, const ThumbnailStrip& s) {
    if (path.empty()) return;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(detail::toPath(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return;
        put(f, kThumbMagic);
        put(f, std::int32_t(s.width));
        put(f, std::int32_t(s.height));
        put(f, std::int32_t(s.count()));
        for (Time t : s.times) put(f, t.ticks());
        f.write(reinterpret_cast<const char*>(s.rgba.data()), static_cast<std::streamsize>(s.rgba.size()));
        if (!f) return;
    }
    std::error_code ec;
    fs::rename(detail::toPath(tmp), detail::toPath(path), ec);
}

std::shared_ptr<ThumbnailStrip> readThumbs(const std::string& path, int count, int height) {
    if (path.empty()) return nullptr;
    std::ifstream f(detail::toPath(path), std::ios::binary);
    std::uint32_t magic = 0;
    std::int32_t w = 0, h = 0, n = 0;
    if (!f || !get(f, magic) || magic != kThumbMagic || !get(f, w) || !get(f, h) || !get(f, n)) return nullptr;
    if (n != count || h != height || w <= 0 || w > 4096) return nullptr;
    auto s = std::make_shared<ThumbnailStrip>();
    s->width = w;
    s->height = h;
    for (int i = 0; i < n; ++i) {
        std::int64_t t = 0;
        if (!get(f, t)) return nullptr;
        s->times.push_back(Time::fromTicks(t));
    }
    s->rgba.resize(static_cast<std::size_t>(n) * w * h * 4);
    if (!f.read(reinterpret_cast<char*>(s->rgba.data()), static_cast<std::streamsize>(s->rgba.size()))) return nullptr;
    return s;
}

void writeWave(const std::string& path, const Waveform& w) {
    if (path.empty()) return;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(detail::toPath(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return;
        put(f, kWaveMagic);
        put(f, std::int32_t(w.bucketsPerSecond));
        put(f, std::uint64_t(w.peaks.size()));
        f.write(reinterpret_cast<const char*>(w.peaks.data()), static_cast<std::streamsize>(w.peaks.size()));
        if (!f) return;
    }
    std::error_code ec;
    fs::rename(detail::toPath(tmp), detail::toPath(path), ec);
}

std::shared_ptr<Waveform> readWave(const std::string& path, int bps) {
    if (path.empty()) return nullptr;
    std::ifstream f(detail::toPath(path), std::ios::binary);
    std::uint32_t magic = 0;
    std::int32_t b = 0;
    std::uint64_t n = 0;
    if (!f || !get(f, magic) || magic != kWaveMagic || !get(f, b) || !get(f, n) || b != bps || n > (1ull << 32))
        return nullptr;
    auto w = std::make_shared<Waveform>();
    w->bucketsPerSecond = b;
    w->peaks.resize(static_cast<std::size_t>(n));
    if (!f.read(reinterpret_cast<char*>(w->peaks.data()), static_cast<std::streamsize>(n))) return nullptr;
    return w;
}

std::int8_t toPeak(float v) { return static_cast<std::int8_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f)); }

}  // namespace

MediaLibrary::MediaLibrary(std::shared_ptr<IMediaBackend> backend, JobScheduler& jobs, std::string cacheDir)
    : st_(std::make_shared<State>()), jobs_(jobs) {
    st_->backend = std::move(backend);
    st_->cacheDir = std::move(cacheDir);
    if (!st_->cacheDir.empty()) {
        std::error_code ec;
        fs::create_directories(detail::toPath(st_->cacheDir), ec);
    }
}

MediaLibrary::~MediaLibrary() {
    // Jobs keep the state alive through their shared_ptr; they see the cancel
    // flag at their next step and the listener is never called again.
    st_->closed = true;
    std::lock_guard lk(st_->mu);
    for (auto& t : st_->tokens) t.cancel();
}

void MediaLibrary::setListener(Listener l) {
    std::lock_guard lk(st_->mu);
    st_->listener = std::move(l);
}

std::shared_ptr<const ThumbnailStrip> MediaLibrary::thumbnails(const Id& id) const {
    std::lock_guard lk(st_->mu);
    auto it = st_->thumbs.find(id);
    return it == st_->thumbs.end() ? nullptr : it->second;
}

std::shared_ptr<const Waveform> MediaLibrary::waveform(const Id& id) const {
    std::lock_guard lk(st_->mu);
    auto it = st_->waves.find(id);
    return it == st_->waves.end() ? nullptr : it->second;
}

void MediaLibrary::forget(const Id& id) {
    std::lock_guard lk(st_->mu);
    st_->thumbs.erase(id);
    st_->waves.erase(id);
}

void MediaLibrary::requestThumbnails(const MediaAsset& a, const std::string& path, int count, int height) {
    if (!a.hasVideo && a.type != MediaType::Image) return;
    count = std::clamp(count, 1, 200);
    height = std::clamp(height, 8, 1080);
    const std::string key = "t:" + a.id;
    CancelToken token;
    {
        std::lock_guard lk(st_->mu);
        if (st_->thumbs.count(a.id) || !st_->inflight.insert(key).second) return;
        st_->tokens.push_back(token);
    }
    jobs_.submit("thumbnails " + a.id, JobPriority::Background,
                 [st = st_, a, path, count, height, key](const CancelToken& tok) {
                     const std::string file = cacheFile(st->cacheDir, a, "_" + std::to_string(count) + "x" +
                                                                             std::to_string(height) + ".thumbs");
                     std::shared_ptr<ThumbnailStrip> s = readThumbs(file, count, height);
                     if (!s && !tok.cancelled()) {
                         auto dec = st->backend ? st->backend->createDecoder() : nullptr;
                         const int srcW = a.width > 0 ? a.width : 16, srcH = a.height > 0 ? a.height : 9;
                         const int w = std::max(2, static_cast<int>(std::lround(double(height) * srcW / srcH)) & ~1);
                         if (dec) dec->setMaxOutputSize(w, height);
                         std::string err;
                         if (dec && dec->open(path, &err)) {
                             s = std::make_shared<ThumbnailStrip>();
                             s->width = w;
                             s->height = height;
                             const bool still = a.type == MediaType::Image || a.duration <= Time::zero();
                             for (int i = 0; i < count && !tok.cancelled(); ++i) {
                                 // Centre of each of `count` equal slices.
                                 const Time t = still ? Time::zero()
                                                      : Time::fromTicks(a.duration.ticks() / count * i +
                                                                        a.duration.ticks() / count / 2);
                                 auto f = dec->videoFrameAt(t);
                                 std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * height * 4, 0);
                                 if (f && !f->empty()) {
                                     // Contain the decoded frame in w x h (decoder already scaled it down).
                                     const int ox = (w - std::min(w, f->width)) / 2, oy = (height - std::min(height, f->height)) / 2;
                                     for (int y = 0; y < std::min(height, f->height); ++y)
                                         std::memcpy(px.data() + (static_cast<std::size_t>(y + oy) * w + ox) * 4,
                                                     f->rgba.data() + static_cast<std::size_t>(y) * f->width * 4,
                                                     static_cast<std::size_t>(std::min(w, f->width)) * 4);
                                 }
                                 s->times.push_back(t);
                                 s->rgba.insert(s->rgba.end(), px.begin(), px.end());
                             }
                             if (tok.cancelled()) s.reset();
                             else writeThumbs(file, *s);
                         }
                     }
                     {
                         std::lock_guard lk(st->mu);
                         st->inflight.erase(key);
                         if (s) st->thumbs[a.id] = s;
                     }
                     if (!tok.cancelled()) st->notify(a.id, Kind::Thumbnails, s != nullptr);
                 },
                 token);
}

void MediaLibrary::requestWaveform(const MediaAsset& a, const std::string& path, int bucketsPerSecond) {
    if (!a.audio || a.duration <= Time::zero()) return;
    bucketsPerSecond = std::clamp(bucketsPerSecond, 1, 1000);
    const std::string key = "w:" + a.id;
    CancelToken token;
    {
        std::lock_guard lk(st_->mu);
        if (st_->waves.count(a.id) || !st_->inflight.insert(key).second) return;
        st_->tokens.push_back(token);
    }
    jobs_.submit("waveform " + a.id, JobPriority::Background,
                 [st = st_, a, path, bucketsPerSecond, key](const CancelToken& tok) {
                     const std::string file = cacheFile(st->cacheDir, a, "_" + std::to_string(bucketsPerSecond) + ".wave");
                     std::shared_ptr<Waveform> w = readWave(file, bucketsPerSecond);
                     if (!w && !tok.cancelled()) {
                         auto dec = st->backend ? st->backend->createDecoder() : nullptr;
                         std::string err;
                         if (dec && dec->open(path, &err)) {
                             // Mono at a low rate is plenty for an envelope and keeps decoding cheap.
                             constexpr int kRate = 8000;
                             const int per = std::max(1, kRate / bucketsPerSecond);
                             const std::int64_t total = a.duration.toSample(kRate);
                             const std::int64_t chunk = static_cast<std::int64_t>(per) * bucketsPerSecond;  // ~1 s
                             w = std::make_shared<Waveform>();
                             w->bucketsPerSecond = bucketsPerSecond;
                             w->peaks.reserve(static_cast<std::size_t>(total / per + 1) * 2);
                             for (std::int64_t s0 = 0; s0 < total && !tok.cancelled(); s0 += chunk) {
                                 const std::int64_t n = std::min(chunk, total - s0);
                                 AudioBuffer b = dec->readAudio(Time::fromSample(s0, kRate), n, kRate, 1);
                                 for (std::int64_t i = 0; i < n; i += per) {
                                     float lo = 0, hi = 0;
                                     const std::int64_t end = std::min(n, i + per);
                                     for (std::int64_t k = i; k < end && k < b.frames(); ++k) {
                                         lo = std::min(lo, b.samples[static_cast<std::size_t>(k)]);
                                         hi = std::max(hi, b.samples[static_cast<std::size_t>(k)]);
                                     }
                                     w->peaks.push_back(toPeak(lo));
                                     w->peaks.push_back(toPeak(hi));
                                 }
                             }
                             if (tok.cancelled()) w.reset();
                             else writeWave(file, *w);
                         }
                     }
                     {
                         std::lock_guard lk(st->mu);
                         st->inflight.erase(key);
                         if (w) st->waves[a.id] = w;
                     }
                     if (!tok.cancelled()) st->notify(a.id, Kind::Waveform, w != nullptr);
                 },
                 token);
}

// ---- relink ----

std::vector<Id> missingAssets(const Project& p, const std::string& baseDir) {
    std::vector<Id> out;
    for (const auto& a : p.assets) {
        if (a.uri.rfind("synthetic:", 0) == 0) continue;  // test media
        fs::path path = detail::toPath(a.uri);
        if (path.is_relative() && !baseDir.empty()) path = detail::toPath(baseDir) / path;
        std::error_code ec;
        if (!fs::exists(path, ec)) out.push_back(a.id);
    }
    return out;
}

std::vector<RelinkCandidate> findRelinkCandidates(const MediaAsset& a, const std::string& searchDir, int maxDepth,
                                                  std::size_t maxFiles) {
    std::vector<RelinkCandidate> out;
    const std::string name = detail::toPath(a.uri).filename().string();
    std::uint64_t wantSize = 0;
    if (auto colon = a.fingerprint.find(':'); colon != std::string::npos)
        wantSize = std::strtoull(a.fingerprint.substr(0, colon).c_str(), nullptr, 10);
    std::error_code ec;
    fs::recursive_directory_iterator it(detail::toPath(searchDir), fs::directory_options::skip_permission_denied, ec);
    std::size_t seen = 0;
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (++seen > maxFiles) break;
        if (it.depth() > maxDepth) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const bool sameName = it->path().filename().string() == name;
        // Only hash files of the right size: the fingerprint starts with the size.
        const bool sameSize = wantSize != 0 && it->file_size(fec) == wantSize;
        if (!sameName && !sameSize) continue;
        const std::string path = detail::fromPath(it->path());
        const bool fp = sameSize && !a.fingerprint.empty() && fingerprintFile(path) == a.fingerprint;
        if (sameName || fp) out.push_back({path, fp});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const RelinkCandidate& x, const RelinkCandidate& y) { return x.fingerprintMatch > y.fingerprintMatch; });
    return out;
}

}  // namespace vstudio
