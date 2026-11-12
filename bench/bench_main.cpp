// P0 benchmarks: edit latency on large timelines and preview cost for large media.
// Run: vstudio_bench [--quick]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

#include "vstudio/editor.hpp"
#include "vstudio/frame_cache.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/proxy.hpp"
#include "vstudio/render.hpp"

#if defined(VSTUDIO_HAS_FFMPEG)
#include "vstudio/ffmpeg/ffmpeg_module.hpp"
#endif

using namespace vstudio;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

struct Stats {
    double avg = 0, p95 = 0, max = 0;
};

Stats measure(int n, const std::function<void(int)>& fn) {
    std::vector<double> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) {
        const auto t0 = Clock::now();
        fn(i);
        v.push_back(msSince(t0));
    }
    std::sort(v.begin(), v.end());
    Stats s;
    for (double x : v) s.avg += x;
    s.avg /= n;
    s.p95 = v[static_cast<std::size_t>(n * 0.95) < v.size() ? static_cast<std::size_t>(n * 0.95) : v.size() - 1];
    s.max = v.back();
    return s;
}

void row(const char* what, const Stats& s, const char* unit = "ms") {
    std::printf("| %-58s | %9.3f | %9.3f | %9.3f | %s |\n", what, s.avg, s.p95, s.max, unit);
}

void header(const char* title) {
    std::printf("\n### %s\n\n| %-58s | %9s | %9s | %9s | unit |\n|%s|%s|%s|%s|------|\n", title, "operation", "avg", "p95",
                "max", std::string(60, '-').c_str(), std::string(11, '-').c_str(), std::string(11, '-').c_str(),
                std::string(11, '-').c_str());
}

MediaAsset fakeAsset(const std::string& id, double seconds) {
    MediaAsset a;
    a.id = id;
    a.uri = "bench/" + id + ".mp4";
    a.duration = Time::fromSeconds(seconds);
    a.width = 3840;
    a.height = 2160;
    a.fps = {30, 1};
    a.hasVideo = true;
    a.audio = AudioInfo{48000, 2, "aac"};
    return a;
}

void benchEdits(int clips) {
    Editor ed{Editor::newProject("bench", 1920, 1080, {30, 1}, "bench")};
    const Id v = ed.addTrack(TrackType::Video, "V1");
    const Id a = ed.addTrack(TrackType::Audio, "A1");
    std::vector<Id> assets;
    for (int i = 0; i < 50; ++i) assets.push_back(ed.addAsset(fakeAsset("asset" + std::to_string(i), 600)));
    const auto t0 = Clock::now();
    for (int i = 0; i < clips; ++i) {
        ed.appendClip(v, assets[i % assets.size()], Time::fromSeconds(1), Time::fromSeconds(4));
        if (i % 2 == 0) ed.appendClip(a, assets[i % assets.size()], Time::fromSeconds(1), Time::fromSeconds(7));
    }
    const double build = msSince(t0);
    char title[128];
    std::snprintf(title, sizeof title, "Timeline edits, %d video clips + %d audio clips (%.0f min timeline)", clips,
                  clips / 2, ed.project().duration().seconds() / 60);
    header(title);
    std::printf("| %-58s | %9.3f | %9s | %9s | ms |\n", "build whole timeline (append all clips)", build, "-", "-");
    const Time dur = ed.project().duration();
    row("split at playhead (all tracks)", measure(200, [&](int i) {
            ed.splitAt(Time::fromFrame(37 + i * 211, {30, 1}) .ticks() < dur.ticks() ? Time::fromFrame(37 + i * 211, {30, 1}) : Time::fromSeconds(1.5));
        }));
    const auto& clipsV = ed.project().tracks[0].clips;
    const Id lastClip = clipsV.back().id;
    row("move last clip", measure(200, [&](int i) { ed.moveClip(lastClip, v, dur + Time::fromSeconds(10 + i)); }));
    row("trim end of last clip", measure(200, [&](int i) {
            const Clip* c = ed.project().findClip(lastClip);
            ed.trimClip(lastClip, TrimEdge::End, c->timelineIn + Time::fromFrame(30 + (i % 50), {30, 1}));
        }));
    row("undo", measure(200, [&](int) { ed.undo(); }));
    row("redo", measure(200, [&](int) { ed.redo(); }));
    row("publish snapshot for render threads (after an edit)", measure(50, [&](int i) {
            ed.moveClip(lastClip, v, dur + Time::fromSeconds(500 + i));
            (void)ed.snapshot();
        }));
    row("serialise project to JSON (save)", measure(10, [&](int) { (void)toJson(ed.project()).dump(); }));
}

#if defined(VSTUDIO_HAS_FFMPEG)

std::string makeClip(IMediaBackend& b, const std::filesystem::path& dir, int w, int h, int seconds) {
    const std::string path = (dir / ("bench_" + std::to_string(w) + "x" + std::to_string(h) + ".mp4")).string();
    if (std::filesystem::exists(path)) return path;
    auto enc = b.createEncoder();
    ExportSettings s;
    s.width = w;
    s.height = h;
    s.fps = {30, 1};
    s.videoBitrate = static_cast<std::int64_t>(w) * h * 4;
    s.gopSize = 60;  // phone-like long GOP
    s.fastPreset = true;
    std::string err;
    if (!enc->open(path, s, &err)) throw std::runtime_error(err);
    VideoFrame f;
    f.allocate(w, h);
    AudioBuffer a;
    a.samples.assign(1600 * 2, 0.0f);
    for (int i = 0; i < seconds * 30; ++i) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint8_t* p = &f.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
                p[0] = static_cast<std::uint8_t>(x + i * 3);
                p[1] = static_cast<std::uint8_t>(y + i);
                p[2] = static_cast<std::uint8_t>((x ^ y) + i);
                p[3] = 255;
            }
        enc->writeVideo(f, &err);
        enc->writeAudio(a, &err);
    }
    enc->finish(&err);
    return path;
}

void benchMedia(int w, int h, int seconds) {
    auto backend = ffmpeg::createBackend();
    const auto dir = std::filesystem::temp_directory_path() / "vstudio_bench";
    std::filesystem::create_directories(dir);
    std::fprintf(stderr, "generating %dx%d test clip...\n", w, h);
    const std::string path = makeClip(*backend, dir, w, h, seconds);
    std::string err;
    auto probe = backend->probe(path, &err);

    char title[128];
    std::snprintf(title, sizeof title, "Preview, %dx%d H.264 source, GOP 60 (preview size 960x540)", w, h);
    header(title);
    const Rational fps{30, 1};
    const int frames = seconds * 30;
    {
        auto d = backend->createDecoder();
        d->open(path, &err);
        row("decode next frame at native size (sequential)", measure(frames - 1, [&](int i) { (void)d->videoFrameAt(Time::fromFrame(i, fps)); }));
    }
    {
        auto d = backend->createDecoder();
        d->setMaxOutputSize(960, 540);
        d->open(path, &err);
        row("decode next frame straight to preview size (sequential)", measure(frames - 1, [&](int i) { (void)d->videoFrameAt(Time::fromFrame(i, fps)); }));
        row("random seek (scrub) to preview size", measure(20, [&](int i) {
                (void)d->videoFrameAt(Time::fromFrame((i * 37) % frames, fps));
            }));
    }

    {
        const MediaAsset pa = assetFromProbe("p", path, *probe, "");
        const std::string proxyPath = path + ".proxy.mp4";
        const auto t0 = Clock::now();
        auto pr = generateProxy(*backend, path, pa, proxyPath, ProxyPolicy{}, CancelToken{});
        const double genMs = msSince(t0);
        std::printf("| %-58s | %9.1f | %9s | %9s | ms |\n", "build 540p proxy in background (whole clip, one-off)", genMs, "-", "-");
        if (pr.ok) {
            auto d = backend->createDecoder();
            d->setMaxOutputSize(960, 540);
            d->open(pr.path, &err);
            row("random seek (scrub) on proxy", measure(20, [&](int i) {
                    (void)d->videoFrameAt(Time::fromFrame((i * 37) % frames, fps));
                }));
        }
    }

    Editor ed{Editor::newProject("bench", 1920, 1080, {30, 1}, "bench")};
    const Id v = ed.addTrack(TrackType::Video, "V1");
    const Id asset = ed.addAsset(assetFromProbe("src", path, *probe, ""));
    const Id c = ed.appendClip(v, asset);
    std::shared_ptr<IMediaBackend> shared = backend;
    MediaPool pool(shared, "");
    FrameCache cache(512ull << 20);
    {
        Renderer r(ed.project(), pool, &cache, true);
        row("render preview frame (decode + composite), cold", measure(60, [&](int i) {
                (void)r.renderVideo(Time::fromFrame(i, fps), 960, 540);
            }));
    }
    ed.splitClip(c, Time::fromSeconds(1));
    ed.moveClip(c, v, Time::fromSeconds(seconds + 1));  // reorder the two halves
    {
        Renderer r(ed.project(), pool, &cache, true);
        row("render same frames after split + move (from cache)", measure(30, [&](int i) {
                (void)r.renderVideo(Time::fromSeconds(seconds + 1) + Time::fromFrame(i, fps), 960, 540);
            }));
    }
}
#endif

}  // namespace

int main(int argc, char** argv) {
    const bool quick = argc > 1 && std::strcmp(argv[1], "--quick") == 0;
    std::printf("# Video Studio P0 benchmark\n\nhardware threads: %u\n", std::thread::hardware_concurrency());
    benchEdits(quick ? 500 : 2000);
#if defined(VSTUDIO_HAS_FFMPEG)
    benchMedia(1920, 1080, quick ? 2 : 4);
    benchMedia(3840, 2160, quick ? 2 : 4);
#else
    std::printf("\n(FFmpeg module not built: media benchmarks skipped)\n");
#endif
    return 0;
}
