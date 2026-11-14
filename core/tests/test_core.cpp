#include <chrono>
#include <atomic>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <thread>
#include <set>

#include "fake_media.hpp"
#include "vstudio/cover.hpp"
#include "vstudio/editor.hpp"
#include "vstudio/tracker.hpp"
#include "vstudio/export_presets.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/media_library.hpp"
#include "vstudio/frame_cache.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/preview.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/render.hpp"
#include "vstudio/scheduler.hpp"
#include "vstudio/transfer.hpp"
#include "vstudio/transitions.hpp"
#include "vstudio/fonts.hpp"
#include "vstudio/generators.hpp"
#include "vstudio/motion.hpp"
#include "vstudio/overlays.hpp"
#include "vstudio/stickers.hpp"
#include "vstudio/text.hpp"
#include "vstudio/vector.hpp"
#include "vtest.hpp"

using namespace vstudio;
namespace fs = std::filesystem;

namespace {

MediaAsset syntheticAsset(const std::string& id, double seconds) {
    fake::Backend b;
    const std::string uri = "synthetic:" + std::to_string(seconds) + ":64x36";
    auto pr = b.probe(uri, nullptr);
    MediaAsset a;
    a.id = id;
    a.uri = uri;
    a.type = MediaType::Video;
    a.duration = pr->duration;
    a.width = pr->width;
    a.height = pr->height;
    a.fps = pr->fps;
    a.hasVideo = true;
    a.audio = AudioInfo{48000, 2, "pcm"};
    return a;
}

struct Fixture {
    Editor ed{Editor::newProject("Test", 1280, 720, {30, 1}, "test")};
    Id asset, v1, a1;
    Fixture() {
        asset = ed.addAsset(syntheticAsset("A", 10));
        v1 = ed.addTrack(TrackType::Video, "V1");
        a1 = ed.addTrack(TrackType::Audio, "A1");
    }
};

Time sec(double s) { return Time::fromSeconds(s); }

}  // namespace

// ------------------------------------------------------------------ time

TEST("time: frame/sample conversions are exact for common rates") {
    const Rational ntsc{30000, 1001};
    for (std::int64_t f : {0LL, 1LL, 29LL, 30LL, 1798LL, 107892LL}) {
        CHECK_EQ(Time::fromFrame(f, ntsc).toFrame(ntsc), f);
        CHECK_EQ(Time::fromFrame(f, {24000, 1001}).toFrame({24000, 1001}), f);
        CHECK_EQ(Time::fromFrame(f, {25, 1}).toFrame({25, 1}), f);
    }
    // One NTSC frame is an exact number of ticks.
    CHECK_EQ(Time::fromFrame(1, ntsc).ticks() * 30000, Time::kTicksPerSecond * 1001LL);
    CHECK_EQ(Time::fromSample(48000, 48000).ticks(), Time::kTicksPerSecond);
    CHECK_EQ(Time::fromSample(44100, 44100), Time::fromSeconds(1));
    // No drift after an hour of 29.97 frames.
    const std::int64_t frames = 107892;  // ~1h at 29.97
    CHECK_EQ(Time::fromFrame(frames, ntsc).toSample(48000), mulDiv(frames, 1001LL * 48000, 30000, Rounding::Down));
}

TEST("time: timebase conversion and rounding") {
    CHECK_EQ(Time::fromTimebase(90000, {1, 90000}), Time::fromSeconds(1));
    CHECK_EQ(Time::fromSeconds(2).toTimebase({1, 15360}), 30720);
    CHECK_EQ(mulDiv(7, 1, 2, Rounding::Down), 3);
    CHECK_EQ(mulDiv(7, 1, 2, Rounding::Up), 4);
    CHECK_EQ(mulDiv(-7, 1, 2, Rounding::Down), -4);
    CHECK_EQ(mulDiv(-7, 1, 2, Rounding::Nearest), -4);
    CHECK_EQ(Time::fromSeconds(3661.5).timecode({30, 1}), std::string("01:01:01:15"));
}

TEST("rational: compare and parse") {
    CHECK(Rational(1, 2) == Rational(2, 4));
    CHECK(Rational(30000, 1001) < Rational(30, 1));
    Rational r;
    CHECK(Rational::parse("30000/1001", r));
    CHECK_EQ(r.num, 30000);
    CHECK(!Rational::parse("1/0", r));
}

// ------------------------------------------------------------------ timeline

TEST("timeline: append, split, trim, move keep source mapping") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset);
    CHECK_EQ(f.ed.project().duration(), sec(10));
    const Id right = f.ed.splitClip(c, sec(4));
    const Clip* L = f.ed.project().findClip(c);
    const Clip* R = f.ed.project().findClip(right);
    CHECK_EQ(L->sourceOut, sec(4));
    CHECK_EQ(R->sourceIn, sec(4));
    CHECK_EQ(R->timelineIn, sec(4));
    f.ed.trimClip(right, TrimEdge::Start, sec(5));
    R = f.ed.project().findClip(right);
    CHECK_EQ(R->sourceIn, sec(5));
    CHECK_EQ(R->timelineOut(), sec(10));
    f.ed.moveClip(right, f.v1, sec(20));
    CHECK_EQ(f.ed.project().findClip(right)->timelineIn, sec(20));
    CHECK_EQ(f.ed.project().duration(), sec(25));
}

TEST("timeline: overlaps, bounds and track types are rejected") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset);
    CHECK_THROWS(f.ed.insertClip(f.v1, f.asset, sec(5), sec(0), sec(2)));  // overlap
    CHECK_THROWS(f.ed.insertClip(f.v1, f.asset, sec(20), sec(0), sec(11)));  // past media end
    CHECK_THROWS(f.ed.trimClip(c, TrimEdge::End, sec(12)));                   // past media end
    CHECK_THROWS(f.ed.splitClip(c, sec(0)));                                  // at edge
    MediaAsset audioOnly = syntheticAsset("B", 3);
    audioOnly.hasVideo = false;
    audioOnly.type = MediaType::Audio;
    const Id b = f.ed.addAsset(audioOnly);
    CHECK_THROWS(f.ed.appendClip(f.v1, b));  // audio on video track
    f.ed.appendClip(f.a1, b);
    // Failed edits leave no history entry.
    CHECK_EQ(f.ed.undoLabel(), std::string("Insert clip"));
}

TEST("timeline: speed maps timeline to source time") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    Project p = f.ed.project();
    Clip* clip = p.tracks[0].findClip(c);
    clip->speed = {2, 1};
    CHECK_EQ(clip->duration(), sec(2));
    CHECK_EQ(clip->toSource(sec(1)), sec(2));
    CHECK_EQ(clip->toTimeline(sec(3)), sec(1.5));
}

TEST("history: undo/redo restores exact state") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset);
    const Json before = toJson(f.ed.project());
    f.ed.splitAt(sec(3));
    f.ed.splitAt(sec(6));
    CHECK_EQ(f.ed.project().tracks[0].clips.size(), 3u);
    const Json after = toJson(f.ed.project());
    CHECK(f.ed.undo());
    CHECK(f.ed.undo());
    CHECK(toJson(f.ed.project()) == before);
    CHECK(f.ed.redo());
    CHECK(f.ed.redo());
    CHECK(toJson(f.ed.project()) == after);
    // A new edit clears redo.
    f.ed.undo();
    f.ed.removeClip(c);
    CHECK(!f.ed.canRedo());
}

TEST("history: dirty flag and snapshot reuse") {
    Fixture f;
    f.ed.markSaved();
    CHECK(!f.ed.dirty());
    auto s1 = f.ed.snapshot();
    CHECK(s1 == f.ed.snapshot());  // unchanged -> same snapshot, no copy
    f.ed.appendClip(f.v1, f.asset);
    CHECK(f.ed.dirty());
    auto s2 = f.ed.snapshot();
    CHECK(s1 != s2);
    CHECK_EQ(s1->tracks[0].clips.size(), 0u);  // old snapshot is immutable
}

// ------------------------------------------------------------------ schema

TEST("schema: save/load round trip keeps unknown fields") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset);
    f.ed.addClipEffect(c, "desktop.only.glow", {{"radius", 4}});
    Json j = toJson(f.ed.project());
    j["future_field"] = {{"x", 1}};
    j["tracks"][0]["clips"][0]["mobile_hint"] = "keep-me";
    const Project p = projectFromJson(j);
    const Json j2 = toJson(p);
    CHECK(j2["future_field"]["x"] == 1);
    CHECK(j2["tracks"][0]["clips"][0]["mobile_hint"] == "keep-me");
    CHECK(j2["tracks"][0]["clips"][0]["effects"][0]["type"] == "desktop.only.glow");
    CHECK(projectFromJson(j2).findClip(c)->sourceOut == sec(10));

    const fs::path dir = fs::temp_directory_path() / ("vstudio_test_" + newId()) / "Film.vstudio";
    saveProject(p, dir.string());
    saveProject(p, dir.string());
    CHECK(fs::exists(dir / "project.json.bak"));
    const Project loaded = loadProject(dir.string());
    CHECK(toJson(loaded)["tracks"] == j2["tracks"]);
    writeAutosave(p, dir.string());
    CHECK(loadAutosave(dir.string()).has_value());
    clearAutosave(dir.string());
    CHECK(!loadAutosave(dir.string()).has_value());
    fs::remove_all(dir.parent_path());
}

TEST("schema: newer schema version is refused, not misread") {
    Fixture f;
    Json j = toJson(f.ed.project());
    j["schema_version"] = kSchemaVersion + 1;
    CHECK_THROWS(projectFromJson(j));
}

TEST("project io: relative uri under the project parent folder") {
    const fs::path base = fs::temp_directory_path() / "vs_uri";
    CHECK_EQ(makeUri((base / "media" / "a.mp4").string(), (base / "F.vstudio").string()), std::string("media/a.mp4"));
    const std::string outside = makeUri("/elsewhere/b.mp4", (base / "F.vstudio").string());
    CHECK(outside.find("elsewhere/b.mp4") != std::string::npos);
}

// ------------------------------------------------------------------ infra

TEST("scheduler: priorities and cancellation") {
    JobScheduler s(1);
    std::mutex mu;
    std::vector<std::string> order;
    std::atomic<bool> gate{false};
    s.submit("block", JobPriority::Normal, [&](const CancelToken&) { while (!gate) std::this_thread::yield(); });
    s.submit("bg", JobPriority::Background, [&](const CancelToken&) { std::lock_guard l(mu); order.push_back("bg"); });
    s.submit("ui", JobPriority::Interactive, [&](const CancelToken&) { std::lock_guard l(mu); order.push_back("ui"); });
    const JobId dropped = s.submit("x", JobPriority::Normal, [&](const CancelToken&) { std::lock_guard l(mu); order.push_back("x"); });
    s.cancel(dropped);
    gate = true;
    s.waitIdle();
    CHECK_EQ(order.size(), 2u);
    CHECK_EQ(order[0], std::string("ui"));
    CHECK_EQ(order[1], std::string("bg"));
}

TEST("frame cache: LRU eviction by bytes") {
    FrameCache c(3 * 64 * 36 * 4);
    for (int i = 0; i < 5; ++i) {
        auto f = std::make_shared<VideoFrame>();
        f->allocate(64, 36);
        c.put(FrameCache::key("a", i, 0, 0), f);
    }
    CHECK_EQ(c.size(), 3u);
    CHECK(!c.get(FrameCache::key("a", 0, 0, 0)));
    CHECK(c.get(FrameCache::key("a", 4, 0, 0)));
}

TEST("modules: lazy creation, lookup by capability, failure isolation") {
    ModuleRegistry r;
    int created = 0;
    r.add<IMediaBackend>({.id = "fake", .capabilities = {"decode"}, .priority = 1}, [&] {
        ++created;
        return std::make_shared<fake::Backend>();
    });
    r.add<IMediaBackend>({.id = "broken", .capabilities = {"decode"}, .priority = 0},
                         []() -> std::shared_ptr<IMediaBackend> { throw std::runtime_error("no gpu"); });
    CHECK_EQ(created, 0);  // nothing instantiated until used
    CHECK(!r.loaded("fake"));
    auto b = r.best<IMediaBackend>("decode");
    CHECK(b && b->name() == "fake");
    CHECK(r.get<IMediaBackend>("fake") == b);
    CHECK_EQ(created, 1);
    std::string err;
    CHECK(!r.get<IMediaBackend>("broken", &err));
    CHECK(err.find("no gpu") != std::string::npos);
    CHECK(!r.get<IVideoEffect>("fake", &err));  // wrong kind
    CHECK(!r.add<IMediaBackend>({.id = "fake"}, [] { return std::make_shared<fake::Backend>(); }));
}

// ------------------------------------------------------------------ render

TEST("render: picks source frame by timeline position and stacks tracks") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(2), sec(6));  // timeline 0..4 -> source 2..6
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(f.ed.project(), pool);
    VideoFrame fr = r.renderVideo(sec(1), 128, 72);
    // source time 3s -> frame 90 -> red 90 at centre
    const std::size_t centre = (36 * 128 + 64) * 4;
    CHECK_EQ(int(fr.rgba[centre]), 90);
    CHECK_EQ(int(r.renderVideo(sec(5), 128, 72).rgba[centre]), 0);  // gap = black
    // Effects come from modules and do not alter cached source frames.
    ModuleRegistry mods;
    registerBuiltinModules(mods);
    f.ed.addClipEffect(c, "builtin.grayscale", {{"amount", 1.0}});
    FrameCache cache;
    Renderer r2(f.ed.project(), pool, &cache, true, &mods);
    VideoFrame g = r2.renderVideo(sec(1), 128, 72);
    CHECK(g.rgba[centre] == g.rgba[centre + 1]);
    f.ed.addClipEffect(c, "plugin.not.installed", {});
    Renderer r3(f.ed.project(), pool, &cache, true, &mods);
    (void)r3.renderVideo(sec(1), 128, 72);
    CHECK_EQ(r3.warnings().size(), 1u);
}

TEST("render: edits re-use decoded frames from the cache") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset);
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    FrameCache cache;
    {
        Renderer r(f.ed.project(), pool, &cache, true);
        for (int i = 0; i < 30; ++i) (void)r.renderVideo(Time::fromFrame(i, {30, 1}), 64, 36);
    }
    const int decodesBefore = fake::decodes.load();
    f.ed.moveClip(c, f.v1, sec(3));  // shift the clip by 3s
    Renderer r(f.ed.project(), pool, &cache, true);
    for (int i = 0; i < 30; ++i) (void)r.renderVideo(sec(3) + Time::fromFrame(i, {30, 1}), 64, 36);
    CHECK_EQ(fake::decodes.load(), decodesBefore);  // zero new decodes after the edit
}

TEST("export: frame count and audio sample count are exact (no A/V drift)") {
    Editor ed{Editor::newProject("NTSC", 640, 360, {30000, 1001}, "test")};
    const Id a = ed.addAsset(syntheticAsset("A", 10));
    const Id v = ed.addTrack(TrackType::Video, "V1");
    ed.appendClip(v, a, sec(0), sec(7.3));
    ed.appendClip(v, a, sec(1), sec(3.2));
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    fake::Encoder enc;
    ExportSettings s;
    s.width = 320;
    s.height = 180;
    s.fps = {30000, 1001};
    std::int64_t lastProgress = 0;
    auto res = exportProject(ed.project(), pool, enc, "out.mp4", s, [&](const ExportProgress& p) { lastProgress = p.frame; });
    CHECK(res.ok);
    const std::int64_t frames = ed.project().duration().toFrame(s.fps, Rounding::Up);
    CHECK_EQ(enc.videoFrames, frames);
    CHECK_EQ(lastProgress, frames);
    CHECK_EQ(enc.audioFrames, Time::fromFrame(frames, s.fps).toSample(48000));
    CHECK(enc.finished);
    CHECK_EQ(enc.lastWidth, 320);
}

TEST("export: cancel stops quickly") {
    Fixture f;
    f.ed.appendClip(f.v1, f.asset);
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    fake::Encoder enc;
    std::atomic<bool> cancel{false};
    auto res = exportProject(f.ed.project(), pool, enc, "o.mp4", {}, [&](const ExportProgress& p) {
        if (p.frame == 10) cancel = true;
    }, &cancel);
    CHECK(res.cancelled);
    CHECK_EQ(enc.videoFrames, 10);
}

// ------------------------------------------------------------------ preview

TEST("preview: latest request wins and audio ring feeds the device") {
    Fixture f;
    f.ed.appendClip(f.v1, f.asset);
    // Declared before the engine: its threads may still call the frame
    // callback until ~PreviewEngine has joined them, so these must outlive it.
    std::mutex mu;
    std::condition_variable cv;
    std::vector<int> got;
    auto backend = std::make_shared<fake::Backend>();
    PreviewEngine pe(backend, "", PreviewConfig{160, 90, 4, 64ull << 20, 0.25});
    pe.setFrameCallback([&](std::shared_ptr<const VideoFrame> fr) {
        std::lock_guard l(mu);
        got.push_back(fr->rgba[(45 * 160 + 80) * 4]);
        cv.notify_all();
    });
    pe.setProject(f.ed.snapshot());
    for (int i = 0; i < 100; ++i) pe.requestFrame(Time::fromFrame(i, {30, 1}));  // fast scrub
    {
        std::unique_lock l(mu);
        cv.wait_for(l, std::chrono::seconds(5), [&] { return !got.empty() && got.back() == 99; });
        CHECK(!got.empty());
        CHECK_EQ(got.back(), 99);
        CHECK(got.size() < 100);  // stale requests were dropped
    }
    pe.play(sec(1));
    std::vector<float> buf(1024 * 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pe.pullAudio(buf.data(), 1024);
    CHECK(buf[2] != 0.0f || buf[4] != 0.0f);
    CHECK(pe.playhead() > sec(1));
    pe.pause();
}

// ------------------------------------------------------------------ compositor

#include "compositor_conformance.hpp"

static vtest::CompositorSuite cpuCompositorSuite("cpu compositor", [] { return createCpuCompositor(); });

namespace {
// Stands in for a GPU module whose device is lost: always fails.
class LostGpu final : public ICompositor {
public:
    std::string name() const override { return "test.lost_gpu"; }
    bool composite(VideoFrame&, const std::vector<CompositeLayer>&, std::string* e) override {
        ++calls;
        if (e) *e = "device lost";
        return false;
    }
    int calls = 0;
};
}  // namespace

TEST("render: uses the best compositor module and falls back to CPU if it fails") {
    Fixture f;
    f.ed.appendClip(f.v1, f.asset, sec(2), sec(6));
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    ModuleRegistry mods;
    registerBuiltinModules(mods);
    CHECK(mods.has("builtin.cpu_compositor"));
    auto gpu = std::make_shared<LostGpu>();
    mods.add<ICompositor>({.id = "test.lost_gpu", .capabilities = {"composite"}, .priority = 100},
                          [gpu] { return gpu; });
    Renderer r(f.ed.project(), pool, nullptr, false, &mods);
    VideoFrame fr = r.renderVideo(sec(1), 128, 72);
    const std::size_t centre = (36 * 128 + 64) * 4;
    CHECK_EQ(int(fr.rgba[centre]), 90);  // same picture as the CPU path
    CHECK_EQ(int(fr.rgba[3]), 255);
    (void)r.renderVideo(sec(2), 128, 72);
    CHECK_EQ(gpu->calls, 1);  // not retried on every frame by this renderer
    CHECK_EQ(r.warnings().size(), 1u);
}

// ------------------------------------------------------------------ media library

#include <atomic>
#include <fstream>

#include "vstudio/media_library.hpp"

TEST("media library: thumbnails and waveform in the background, cached on disk") {
    auto backend = std::make_shared<fake::Backend>();
    const auto cache = fs::temp_directory_path() / ("vstudio_lib_" + newId());
    MediaAsset a = syntheticAsset("A", 10);  // 64x36, 30 fps, sawtooth audio peaking at 0.25
    a.fingerprint = "123:abc";
    JobScheduler jobs(2);
    std::atomic<int> ready{0};
    {
        MediaLibrary lib(backend, jobs, cache.string());
        lib.setListener([&](const Id& id, MediaLibrary::Kind, bool ok) {
            CHECK_EQ(id, std::string("A"));
            CHECK(ok);
            ++ready;
        });
        lib.requestThumbnails(a, a.uri, 5, 18);
        lib.requestThumbnails(a, a.uri, 5, 18);  // de-duplicated
        lib.requestWaveform(a, a.uri, 50);
        jobs.waitIdle();
        CHECK_EQ(ready.load(), 2);
        auto t = lib.thumbnails("A");
        CHECK(t != nullptr);
        CHECK_EQ(t->count(), 5);
        CHECK_EQ(t->width, 32);  // 16:9 at height 18
        CHECK_EQ(t->height, 18);
        // Slice centres: 1 s, 3 s, ... -> frame 30, 90, ... (red channel = frame index % 256)
        CHECK_EQ(int(t->frame(0)[0]), 30);
        CHECK_EQ(int(t->frame(1)[0]), 90);
        CHECK_EQ(t->times[4], sec(9));
        auto w = lib.waveform("A");
        CHECK(w != nullptr);
        CHECK_EQ(w->buckets(), std::size_t(500));
        CHECK_EQ(int(w->peaks[0]), 0);  // min of the sawtooth
        CHECK(w->peaks[1] >= 30 && w->peaks[1] <= 32);  // max ~0.245 * 127
        CHECK_EQ(w->loudest, int(w->peaks[1]));
    }
    // A new library (next session) reads both from the disk cache without decoding.
    const int decodes = fake::decodes.load();
    MediaLibrary lib2(backend, jobs, cache.string());
    MediaAsset moved = a;
    moved.uri = "synthetic-file-is-gone";  // the cache is keyed by fingerprint, not path
    lib2.requestThumbnails(moved, moved.uri, 5, 18);
    lib2.requestWaveform(moved, moved.uri, 50);
    jobs.waitIdle();
    CHECK_EQ(fake::decodes.load(), decodes);
    CHECK(lib2.thumbnails("A") != nullptr);
    CHECK_EQ(int(lib2.thumbnails("A")->frame(1)[0]), 90);
    CHECK_EQ(lib2.waveform("A")->buckets(), std::size_t(500));
    // Unreadable media reports failure instead of hanging or throwing.
    MediaAsset bad = a;
    bad.id = "B";
    bad.fingerprint.clear();
    std::atomic<int> failed{0};
    lib2.setListener([&](const Id&, MediaLibrary::Kind, bool ok) { failed += ok ? 0 : 1; });
    lib2.requestThumbnails(bad, "not-a-file", 5, 18);
    jobs.waitIdle();
    CHECK_EQ(failed.load(), 1);
    CHECK(lib2.thumbnails("B") == nullptr);
    fs::remove_all(cache);
}

TEST("media library: destroying the library cancels pending work") {
    auto backend = std::make_shared<fake::Backend>();
    JobScheduler jobs(1);
    std::atomic<int> calls{0};
    {
        MediaLibrary lib(backend, jobs);
        lib.setListener([&](const Id&, MediaLibrary::Kind, bool) { ++calls; });
        for (int i = 0; i < 20; ++i) {
            MediaAsset a = syntheticAsset("A" + std::to_string(i), 600);
            lib.requestWaveform(a, a.uri, 100);
        }
    }
    jobs.waitIdle();
    CHECK(calls.load() < 20);  // no callbacks after destruction
}

TEST("relink: missing media found by fingerprint, undoable") {
    const auto dir = fs::temp_directory_path() / ("vstudio_relink_" + newId());
    fs::create_directories(dir / "old");
    fs::create_directories(dir / "new" / "deep");
    auto write = [](const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; };
    write(dir / "old" / "clip.mp4", std::string(100000, 'x') + "tail-1");
    Fixture f;
    MediaAsset a = syntheticAsset("M", 5);
    a.uri = "old/clip.mp4";
    a.fingerprint = fingerprintFile((dir / "old" / "clip.mp4").string());
    f.ed.addAsset(a);
    const std::string base = dir.string();
    CHECK(missingAssets(f.ed.project(), base).empty());
    // The user moves and renames the file; another file keeps the old name.
    fs::rename(dir / "old" / "clip.mp4", dir / "new" / "deep" / "renamed.mp4");
    write(dir / "new" / "clip.mp4", std::string(100000, 'y') + "other!");
    auto missing = missingAssets(f.ed.project(), base);
    CHECK_EQ(missing.size(), 1u);
    CHECK_EQ(missing[0], std::string("M"));
    auto c = findRelinkCandidates(*f.ed.project().findAsset("M"), base);
    CHECK_EQ(c.size(), 2u);
    CHECK(c[0].fingerprintMatch);  // same content, new name: first
    CHECK(c[0].path.find("renamed.mp4") != std::string::npos);
    CHECK(!c[1].fingerprintMatch);  // same name only
    f.ed.relinkAsset("M", makeUri(c[0].path, (dir / "p.vstudio").string()));
    CHECK(missingAssets(f.ed.project(), base).empty());
    CHECK(f.ed.undo());
    CHECK_EQ(missingAssets(f.ed.project(), base).size(), 1u);
    CHECK_THROWS(f.ed.relinkAsset("nope", "x"));
    fs::remove_all(dir);
}

// ------------------------------------------------------------------ multi-track

TEST("tracks: remove, rename, flags, volume, reorder - all undoable") {
    Fixture f;
    const Id v2 = f.ed.addTrack(TrackType::Video, "V2");
    const Id v3 = f.ed.addTrack(TrackType::Video, "V3");
    f.ed.appendClip(f.v1, f.asset);
    auto videoOrder = [&] {
        std::string s;
        for (const auto& t : f.ed.project().tracks)
            if (t.type == TrackType::Video) s += t.name + ",";
        return s;
    };
    CHECK_EQ(videoOrder(), std::string("V1,V2,V3,"));
    f.ed.moveTrack(v3, 0);  // V3 becomes the bottom layer
    CHECK_EQ(videoOrder(), std::string("V3,V1,V2,"));
    CHECK(f.ed.project().tracks.back().type == TrackType::Audio);  // audio tracks untouched
    f.ed.undo();
    CHECK_EQ(videoOrder(), std::string("V1,V2,V3,"));

    f.ed.renameTrack(v2, "Titles");
    CHECK_EQ(f.ed.project().findTrack(v2)->name, std::string("Titles"));
    f.ed.setTrackVolume(f.a1, 0.5);
    CHECK_THROWS(f.ed.setTrackVolume(f.a1, -1));

    // Locked: clip edits and removal refused; unlock is one undo step.
    f.ed.setTrackFlags(f.v1, true, true, false);
    CHECK_EQ(f.ed.undoLabel(), std::string("Lock track"));
    const Id clip = f.ed.project().findTrack(f.v1)->clips[0].id;
    CHECK_THROWS(f.ed.moveClip(clip, f.v1, sec(5)));
    CHECK_THROWS(f.ed.removeTrack(f.v1));
    CHECK_EQ(f.ed.splitAt(sec(1)), 0);
    f.ed.undo();
    f.ed.moveClip(clip, f.v1, sec(5));

    // Hidden video track is not rendered; muted audio track is silent.
    f.ed.setTrackFlags(f.v1, false, false, false);
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(f.ed.project(), pool);
    CHECK_EQ(int(r.renderVideo(sec(6), 64, 36).rgba[(18 * 64 + 32) * 4]), 0);

    f.ed.removeTrack(v2);
    CHECK(f.ed.project().findTrack(v2) == nullptr);
    f.ed.undo();
    CHECK(f.ed.project().findTrack(v2) != nullptr);
}

TEST("snap: clip edges, playhead and zero within tolerance") {
    Fixture f;
    const Id a = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));      // 0..4
    const Id b = f.ed.insertClip(f.v1, f.asset, sec(10), sec(0), sec(2));  // 10..12
    const Id c = f.ed.insertClip(f.a1, f.asset, sec(6), sec(0), sec(3));   // 6..9 on audio
    const Time tol = Time::fromMilliseconds(200);
    const Project& p = f.ed.project();
    auto s = snapTime(p, sec(4.1), tol);
    CHECK(s.snapped);
    CHECK_EQ(s.time, sec(4));
    CHECK(!snapTime(p, sec(5), tol).snapped);
    CHECK_EQ(snapTime(p, sec(7.9), tol, {}, sec(8)).time, sec(8));  // playhead
    CHECK_EQ(snapTime(p, sec(0.1), tol).time, Time::zero());
    // Moving b: its end (len 2) snaps to c's start at 6 -> start 4... and its start snaps to 4.
    auto m = snapClipMove(p, b, sec(4.15), tol);
    CHECK(m.snapped);
    CHECK_EQ(m.time, sec(4));  // start edge onto a's end (closer than end edge 6.15 -> 6)
    m = snapClipMove(p, b, sec(6.95), tol);  // end at 8.95 -> c's end 9
    CHECK_EQ(m.time, sec(7));
    CHECK_EQ(m.target, sec(9));
    // The moving clip never snaps to itself.
    CHECK(!snapTime(p, sec(10.05), tol, b).snapped);
    (void)a;
    (void)c;
}

TEST("ripple delete closes the gap; edit points for navigation") {
    Fixture f;
    const Id a = f.ed.insertClip(f.v1, f.asset, sec(0), sec(0), sec(2));   // 0..2
    const Id b = f.ed.insertClip(f.v1, f.asset, sec(2), sec(0), sec(3));   // 2..5
    const Id c = f.ed.insertClip(f.v1, f.asset, sec(6), sec(0), sec(1));   // 6..7
    const Id d = f.ed.insertClip(f.a1, f.asset, sec(4), sec(0), sec(4));   // 4..8 other track: untouched
    const Project& p = f.ed.project();
    CHECK_EQ(*adjacentEditPoint(p, sec(0), true), sec(2));
    CHECK_EQ(*adjacentEditPoint(p, sec(3), true), sec(4));
    CHECK_EQ(*adjacentEditPoint(p, sec(5.5), false), sec(5));
    CHECK_EQ(*adjacentEditPoint(p, sec(6), false), sec(5));
    CHECK(!adjacentEditPoint(p, sec(8), true));
    CHECK(!adjacentEditPoint(p, sec(0), false));
    f.ed.rippleDelete(b);
    CHECK(!p.findClip(b));
    CHECK_EQ(p.findClip(c)->timelineIn, sec(3));  // 6 - 3
    CHECK_EQ(p.findClip(a)->timelineIn, sec(0));
    CHECK_EQ(p.findClip(d)->timelineIn, sec(4));
    CHECK(f.ed.undo());
    CHECK_EQ(p.findClip(c)->timelineIn, sec(6));
    CHECK(p.findClip(b));
    f.ed.setTrackFlags(f.v1, true, true, false);
    CHECK_THROWS(f.ed.rippleDelete(a));
}

TEST("export presets keep the canvas aspect") {
    Project portrait = Editor::newProject("P", 1080, 1920, {30, 1}, "test");
    Project wide = Editor::newProject("W", 3840, 2160, {60, 1}, "test");
    Project odd = Editor::newProject("O", 1000, 750, {25, 1}, "test");
    auto s = exportSettingsFor(portrait, *findExportPreset("fhd1080"));
    CHECK_EQ(s.width, 1080);
    CHECK_EQ(s.height, 1920);
    s = exportSettingsFor(wide, *findExportPreset("hd720"));
    CHECK_EQ(s.width, 1280);
    CHECK_EQ(s.height, 720);
    CHECK_EQ(s.fps.num, 30);  // 720p preset caps at 30 fps
    s = exportSettingsFor(wide, *findExportPreset("social"));
    CHECK_EQ(s.fps.num, 60);  // up to 60 kept
    s = exportSettingsFor(odd, *findExportPreset("fhd1080"));
    CHECK_EQ(s.height, 1080);
    CHECK_EQ(s.width, 1440);
    CHECK(s.width % 2 == 0 && s.height % 2 == 0);
    s = exportSettingsFor(wide, *findExportPreset("master"));
    CHECK_EQ(s.width, 3840);
    CHECK(s.videoBitrate <= 100'000'000 && s.videoBitrate >= 1'000'000);
    // 1080p30 portrait: about 7.5 Mbit/s -> ~1 MB per second of video.
    s = exportSettingsFor(portrait, *findExportPreset("fhd1080"));
    const auto bytes = estimateExportBytes(s, 10);
    CHECK(bytes > 9'000'000 && bytes < 11'000'000);
    CHECK(findExportPreset("nope") == nullptr);
    for (const auto& pr : exportPresets()) CHECK(!pr.name.empty() && !pr.id.empty());
}

TEST("export formats: H.264, H.265 and H.265 10-bit") {
    Project p = Editor::newProject("P", 1920, 1080, {30, 1}, "test");
    const auto& pr = *findExportPreset("fhd1080");
    const ExportSettings h264 = exportSettingsFor(p, pr, "h264");
    CHECK_EQ(h264.videoCodec, std::string("h264"));
    CHECK_EQ(h264.bitDepth, 8);
    const ExportSettings hevc = exportSettingsFor(p, pr, "hevc");
    CHECK_EQ(hevc.videoCodec, std::string("hevc"));
    CHECK(hevc.videoBitrate < h264.videoBitrate);  // same quality in a smaller file
    const ExportSettings ten = exportSettingsFor(p, pr, "hevc10");
    CHECK_EQ(ten.videoCodec, std::string("hevc"));
    CHECK_EQ(ten.bitDepth, 10);
    CHECK(ten.videoBitrate > hevc.videoBitrate && ten.videoBitrate < h264.videoBitrate);
    ExportSettings u = h264;
    CHECK(!applyExportFormat(u, "nope"));
    CHECK_EQ(u.videoCodec, std::string("h264"));
    CHECK_EQ(exportFormats().size(), 3u);
    CHECK(findExportFormat("hevc10") != nullptr && findExportFormat("x") == nullptr);
}

TEST("colour looks: one tap, replaceable, undoable, render the same everywhere") {
    Fixture f;
    const Id a = f.ed.insertClip(f.v1, f.asset, sec(0), sec(0), sec(2));
    const Id b = f.ed.insertClip(f.v1, f.asset, sec(2), sec(0), sec(2));
    CHECK(colorLooks().size() >= 15);
    f.ed.setClipLook({a, b}, "teal_orange", 0.8);
    auto lookOf = [&](const Id& c) {
        for (const auto& e : f.ed.project().findClip(c)->effects)
            if (e.value("type", std::string{}) == kColorLookEffect) return e["params"].value("look", std::string{});
        return std::string{};
    };
    CHECK_EQ(lookOf(a), std::string("teal_orange"));
    CHECK_EQ(lookOf(b), std::string("teal_orange"));
    f.ed.setClipLook({a}, "bw_contrast");  // replaces, never stacks
    CHECK_EQ(f.ed.project().findClip(a)->effects.size(), std::size_t(1));
    CHECK_EQ(lookOf(a), std::string("bw_contrast"));
    CHECK(f.ed.undo());
    CHECK_EQ(lookOf(a), std::string("teal_orange"));
    f.ed.setClipLook({a}, "");
    CHECK(lookOf(a).empty());
    CHECK_THROWS(f.ed.setClipLook({a}, "nope"));
    f.ed.setTrackFlags(f.v1, true, true, false);
    CHECK_THROWS(f.ed.setClipLook({b}, "vivid"));

    // The effect itself: identity by default, mono gives grey, strength 0 is a no-op.
    VideoFrame fr;
    fr.allocate(64, 36);
    for (std::size_t i = 0; i < fr.rgba.size(); i += 4) {
        fr.rgba[i] = std::uint8_t(i % 251);
        fr.rgba[i + 1] = std::uint8_t((i / 3) % 241);
        fr.rgba[i + 2] = std::uint8_t((i / 7) % 233);
        fr.rgba[i + 3] = 255;
    }
    VideoFrame same = fr;
    applyColorLook(same, Json::object());
    int maxDiff = 0;
    for (std::size_t i = 0; i < fr.rgba.size(); ++i) maxDiff = std::max(maxDiff, std::abs(fr.rgba[i] - same.rgba[i]));
    CHECK(maxDiff <= 1);
    VideoFrame grey = fr;
    applyColorLook(grey, findColorLook("bw_soft")->params);
    for (std::size_t i = 0; i < grey.rgba.size(); i += 4) {
        CHECK(std::abs(grey.rgba[i] - grey.rgba[i + 1]) <= 1);
        CHECK(std::abs(grey.rgba[i + 1] - grey.rgba[i + 2]) <= 1);
        CHECK_EQ(int(grey.rgba[i + 3]), 255);  // alpha untouched
    }
    VideoFrame off = fr;
    Json p = findColorLook("night_neon")->params;
    p["strength"] = 0.0;
    applyColorLook(off, p);
    CHECK(off.rgba == fr.rgba);
    // Speed: a 960x540 preview frame stays well inside a frame budget on CPU.
    VideoFrame big;
    big.allocate(960, 540);
    const auto t0 = std::chrono::steady_clock::now();
    applyColorLook(big, findColorLook("teal_orange")->params);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("       colour look 960x540: %.2f ms\n", ms);
    CHECK(ms < 40);
}

TEST("snap: thousands of clips stay fast") {
    Fixture f;
    for (int i = 0; i < 3000; ++i) f.ed.insertClip(f.v1, f.asset, sec(i * 2.0), sec(0), sec(1));
    const auto t0 = std::chrono::steady_clock::now();
    int snapped = 0;
    for (int i = 0; i < 10000; ++i) snapped += snapTime(f.ed.project(), sec(i * 0.6), Time::fromMilliseconds(50)).snapped;
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 10000;
    std::printf("       snapTime over 3000 clips: %.2f us/call\n", us);
    CHECK(snapped > 0);
    CHECK(us < 50);
}

// ------------------------------------------------------------------ transfer package

namespace {

void writeFile(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// Minimal ustar writer to build hostile packages by hand.
void tarEntry(std::string& out, const std::string& name, const std::string& data) {
    char h[512] = {};
    std::memcpy(h, name.data(), name.size());
    std::snprintf(h + 100, 8, "%07o", 0644);
    std::snprintf(h + 124, 12, "%011llo", static_cast<unsigned long long>(data.size()));
    h[156] = '0';
    std::memcpy(h + 257, "ustar", 6);
    std::memset(h + 148, ' ', 8);
    unsigned sum = 0;
    for (char c : h) sum += static_cast<unsigned char>(c);
    std::snprintf(h + 148, 8, "%06o", sum);
    out.append(h, 512);
    out += data;
    out.append((512 - data.size() % 512) % 512, '\0');
}

bool hasPartFiles(const fs::path& dir) {
    for (const auto& e : fs::recursive_directory_iterator(dir))
        if (e.path().extension() == ".part") return true;
    return false;
}

}  // namespace

TEST("transfer: pack and unpack move a project with its media to another folder") {
    const auto dir = fs::temp_directory_path() / ("vstudio_xfer_" + newId());
    fs::create_directories(dir / "pc" / "media");
    fs::create_directories(dir / "phone");
    const std::string video = std::string(3 << 20, 'v') + "end", music = "music-bytes";
    writeFile(dir / "pc" / "media" / "clip.mp4", video);
    writeFile(dir / "pc" / "song.m4a", music);
    Fixture f;
    MediaAsset a = syntheticAsset("V", 5), b = syntheticAsset("S", 5), gone = syntheticAsset("G", 5), twin = syntheticAsset("T", 5);
    a.uri = "media/clip.mp4";
    b.uri = (dir / "pc" / "song.m4a").string();  // absolute
    gone.uri = "media/missing.mp4";
    twin.uri = "media/clip.mp4";  // same file twice: stored once
    for (const auto& x : {a, b, gone, twin}) f.ed.addAsset(x);
    f.ed.insertClip(f.v1, "V", sec(0), sec(0), sec(2));
    const auto pkgFile = dir / "My.vstudiopkg";
    std::vector<double> prog;
    auto r = packProject(f.ed.project(), (dir / "pc").string(), pkgFile.string(), [&](double p) { prog.push_back(p); });
    CHECK(r.ok);
    CHECK_EQ(r.missingAssets.size(), 1u);
    CHECK_EQ(r.missingAssets[0], std::string("G"));
    CHECK(!prog.empty() && prog.back() == 1.0);
    CHECK(fs::file_size(pkgFile) < video.size() + music.size() + 16 * 1024);  // the clip is stored once
    CHECK(!hasPartFiles(dir));

    // A same-named different file already on the phone gets kept; an identical one is reused.
    fs::create_directories(dir / "phone" / "media");
    writeFile(dir / "phone" / "media" / "clip.mp4", "someone else's clip");
    writeFile(dir / "phone" / "media" / "clip (2).mp4", std::string(3 << 20, 'w') + "end");  // same size, other content
    writeFile(dir / "phone" / "media" / "song.m4a", music);
    auto u = unpackProject(pkgFile.string(), (dir / "phone").string());
    CHECK(u.ok);
    CHECK_EQ(fs::path(u.packageDir).parent_path(), dir / "phone");
    CHECK_EQ(readFile(dir / "phone" / "media" / "clip.mp4"), std::string("someone else's clip"));
    CHECK_EQ(readFile(dir / "phone" / "media" / "clip (3).mp4"), video);
    auto p = loadProject(u.packageDir);
    CHECK_EQ(p.findAsset("V")->uri, std::string("media/clip (3).mp4"));
    CHECK_EQ(p.findAsset("T")->uri, std::string("media/clip (3).mp4"));
    CHECK_EQ(p.findAsset("S")->uri, std::string("media/song.m4a"));
    CHECK_EQ(p.findAsset("G")->uri, std::string("media/missing.mp4"));  // still missing: relink later
    CHECK_EQ(missingAssets(p, (dir / "phone").string()).size(), 1u);
    CHECK_EQ(p.tracks.size(), f.ed.project().tracks.size());

    // Unpacking twice makes a second project, not an overwrite, and reuses the media.
    auto u2 = unpackProject(pkgFile.string(), (dir / "phone").string());
    CHECK(u2.ok);
    CHECK(u2.packageDir != u.packageDir);
    CHECK(!fs::exists(dir / "phone" / "media" / "clip (4).mp4"));
    CHECK(!hasPartFiles(dir));
    fs::remove_all(dir);
}

TEST("transfer: hostile or foreign files are refused and leave nothing behind") {
    const auto dir = fs::temp_directory_path() / ("vstudio_xfer_bad_" + newId());
    fs::create_directories(dir / "out");
    auto tryUnpack = [&](const std::string& bytes) {
        writeFile(dir / "x.vstudiopkg", bytes);
        return unpackProject((dir / "x.vstudiopkg").string(), (dir / "out").string());
    };
    std::string marker;
    tarEntry(marker, "vstudiopkg.json", R"({"format":1,"name":"x"})");
    for (const char* bad : {"../evil.txt", "media/../evil.txt", "/tmp/evil.txt", "media/a/b.mp4", "other/x.mp4"}) {
        std::string t = marker;
        tarEntry(t, bad, "payload");
        t.append(1024, '\0');
        auto r = tryUnpack(t);
        CHECK(!r.ok);
        CHECK(r.error.find("unsafe") != std::string::npos);
    }
    CHECK(!fs::exists(dir / "evil.txt"));
    {
        std::string t;
        tarEntry(t, "project.json", "{}");  // no marker first
        CHECK(!tryUnpack(t).ok);
    }
    {
        std::string t;
        tarEntry(t, "vstudiopkg.json", R"({"format":9})");
        CHECK(tryUnpack(t).error.find("version") != std::string::npos);
    }
    CHECK(!tryUnpack("just a text file, not a tar archive at all").ok);
    {
        std::string t = marker;
        tarEntry(t, "media/half.mp4", std::string(5000, 'h'));
        t.resize(t.size() - 3000);  // truncated download
        auto r = tryUnpack(t);
        CHECK(!r.ok);
        CHECK(!fs::exists(dir / "out" / "media" / "half.mp4"));
    }
    CHECK(!hasPartFiles(dir));
    fs::remove_all(dir);
}

TEST("transfer: cancel stops pack and unpack without partial files") {
    const auto dir = fs::temp_directory_path() / ("vstudio_xfer_cancel_" + newId());
    fs::create_directories(dir / "media");
    writeFile(dir / "media" / "big.mp4", std::string(4 << 20, 'b'));
    Fixture f;
    MediaAsset a = syntheticAsset("B", 5);
    a.uri = "media/big.mp4";
    f.ed.addAsset(a);
    std::atomic<bool> cancel{false};
    auto stopHalfway = [&](double p) {
        if (p > 0.2) cancel = true;
    };
    auto r = packProject(f.ed.project(), dir.string(), (dir / "c.vstudiopkg").string(), stopHalfway, &cancel);
    CHECK(r.cancelled && !r.ok);
    CHECK(!fs::exists(dir / "c.vstudiopkg"));
    cancel = false;
    CHECK(packProject(f.ed.project(), dir.string(), (dir / "c.vstudiopkg").string()).ok);
    fs::create_directories(dir / "dest");
    auto u = unpackProject((dir / "c.vstudiopkg").string(), (dir / "dest").string(), stopHalfway, &cancel);
    CHECK(u.cancelled && !u.ok);
    CHECK(!fs::exists(dir / "dest" / "media" / "big.mp4"));
    CHECK(!hasPartFiles(dir));
    fs::remove_all(dir);
}

// ------------------------------------------------------------------ deep colour (RGBA16F)

#include <set>

#include "vstudio/pixel.hpp"

TEST("pixel: half floats and 8-bit values survive conversions") {
    CHECK_EQ(floatToHalf(1.0f), std::uint16_t{0x3c00});
    CHECK_EQ(floatToHalf(-2.0f), std::uint16_t{0xc000});
    CHECK_EQ(floatToHalf(65504.0f), std::uint16_t{0x7bff});
    CHECK_EQ(floatToHalf(1e6f), std::uint16_t{0x7c00});  // overflow -> inf
    CHECK_EQ(halfToFloat(0x0001), 5.9604645e-8f);        // smallest subnormal
    for (std::uint32_t h = 0; h < 0x7c00; ++h)  // every finite positive half round-trips exactly
        if (floatToHalf(halfToFloat(static_cast<std::uint16_t>(h))) != h) CHECK(false);
    // Every 8-bit value comes back unchanged through RGBA16F, dither included.
    VideoFrame f;
    f.allocate(256, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 256; ++x)
            for (int c = 0; c < 4; ++c) f.rgba[(y * 256 + x) * 4 + c] = static_cast<std::uint8_t>(x);
    const VideoFrame original = f;
    convertFrame(f, PixelFormat::RGBA16F);
    CHECK(f.deep() && f.rgba.empty() && f.half.size() == 256u * 4 * 4);
    CHECK_EQ(f.bytes(), original.bytes() * 2);
    convertFrame(f, PixelFormat::RGBA8);
    CHECK(f.rgba == original.rgba);
}

TEST("pixel: a smooth 10-bit gradient is dithered, not banded, on the way to 8 bits") {
    VideoFrame f;
    f.allocate(64, 16, PixelFormat::RGBA16F);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 64; ++x)
            for (int c = 0; c < 4; ++c)  // 64 steps spanning only 4 8-bit levels
                f.half[(y * 64 + x) * 4 + c] = floatToHalf(c == 3 ? 1.0f : (100.0f + x / 16.0f) / 255.0f);
    convertFrame(f, PixelFormat::RGBA8);
    // Averaged over each 4x4 dither cell, the 8-bit picture follows the fine ramp
    // (error well under one 8-bit step), where plain rounding would show 4 flat bands.
    for (int bx = 0; bx < 64; bx += 4) {
        double sum = 0, want = 0;
        for (int y = 0; y < 4; ++y)
            for (int x = bx; x < bx + 4; ++x) {
                sum += f.rgba[(y * 64 + x) * 4];
                want += 100.0 + x / 16.0;
            }
        CHECK(std::abs(sum - want) / 16 < 0.2);
    }
}

TEST("deep grading keeps far more tonal levels than 8-bit grading") {
    // A dark 10-bit ramp (e.g. underexposed Log footage), pushed hard by a look.
    VideoFrame deep;
    deep.allocate(1024, 1, PixelFormat::RGBA16F);
    for (int x = 0; x < 1024; ++x)
        for (int c = 0; c < 4; ++c) deep.half[x * 4 + c] = floatToHalf(c == 3 ? 1.0f : x / 1023.0f * 0.25f);
    VideoFrame flat = convertedFrame(deep, PixelFormat::RGBA8);
    const Json push = {{"exposure", 2.0}, {"contrast", 1.3}, {"saturation", 1.2}};
    applyColorLook(deep, push);
    applyColorLook(flat, push);
    std::set<std::uint16_t> deepLevels;
    std::set<std::uint8_t> flatLevels;
    for (int x = 0; x < 1024; ++x) {
        deepLevels.insert(deep.half[x * 4]);
        flatLevels.insert(flat.rgba[x * 4]);
    }
    CHECK(flatLevels.size() <= 65u);  // 64 input levels: visible bands after a 4x push
    CHECK(deepLevels.size() > 600u);
    // Same look, same picture: deep and 8-bit agree within dithering/rounding.
    const VideoFrame back = convertedFrame(deep, PixelFormat::RGBA8);
    for (int x = 0; x < 1024; x += 37) CHECK(std::abs(int(back.rgba[x * 4]) - int(flat.rgba[x * 4])) <= 8);
}

namespace {

// An effect from an older module: knows nothing of RGBA16F.
class EightBitOnly final : public IVideoEffect {
public:
    mutable int deepSeen = 0;
    void apply(VideoFrame& f, const Json&, Time) const override {
        if (f.deep()) ++deepSeen;
        for (std::size_t i = 0; i + 3 < f.rgba.size(); i += 4) f.rgba[i] = 255 - f.rgba[i];
    }
};

// A GPU-like compositor that has no deep path: must never be handed half floats.
class ShallowGpu final : public ICompositor {
public:
    int calls = 0;
    std::string name() const override { return "test.shallow"; }
    bool composite(VideoFrame& out, const std::vector<CompositeLayer>& layers, std::string*) override {
        ++calls;
        CHECK(!out.deep());
        for (const auto& l : layers) CHECK(!l.frame->deep());
        return createCpuCompositor()->composite(out, layers, nullptr);
    }
};

}  // namespace

TEST("render: deep output for 10-bit export, 8-bit for preview, old modules still work") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(2), sec(6));
    f.ed.addClipEffect(c, kColorLookEffect, {{"exposure", 0.5}});
    f.ed.addClipEffect(c, "test.eight_bit", {});
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    ModuleRegistry mods;
    registerBuiltinModules(mods);
    auto old = std::make_shared<EightBitOnly>();
    mods.add<IVideoEffect>({.id = "test.eight_bit", .capabilities = {"effect.color"}}, [old] { return old; });
    auto gpu = std::make_shared<ShallowGpu>();
    mods.add<ICompositor>({.id = "test.shallow", .capabilities = {"composite"}, .priority = 100}, [gpu] { return gpu; });

    Renderer preview(f.ed.project(), pool, nullptr, true, &mods);
    const VideoFrame p = preview.renderVideo(sec(1), 128, 72);
    CHECK(!p.deep());
    CHECK_EQ(gpu->calls, 1);

    Renderer exp(f.ed.project(), pool, nullptr, false, &mods);
    exp.setDeepOutput(true);
    const VideoFrame d = exp.renderVideo(sec(1), 128, 72);
    CHECK(d.deep());
    CHECK_EQ(gpu->calls, 1);   // the shallow compositor was skipped for the deep frame
    CHECK_EQ(old->deepSeen, 0);  // and the 8-bit effect only ever saw 8-bit frames
    const VideoFrame d8 = convertedFrame(d, PixelFormat::RGBA8);
    const std::size_t centre = (36 * 128 + 64) * 4;
    for (int k = 0; k < 4; ++k) CHECK(std::abs(int(d8.rgba[centre + k]) - int(p.rgba[centre + k])) <= 2);
    CHECK(exp.warnings().empty());
}

// ------------------------------------------------------------------ Log / HDR -> Rec.709, LUT

#include "vstudio/color_space.hpp"

namespace {

std::array<float, 3> through(const std::string& input, float v, double exposure = 0.0, bool deep = true) {
    VideoFrame f;
    f.allocate(1, 1, deep ? PixelFormat::RGBA16F : PixelFormat::RGBA8);
    for (int c = 0; c < 3; ++c) {
        if (deep) f.half[c] = floatToHalf(v);
        else f.rgba[c] = static_cast<std::uint8_t>(std::lround(v * 255));
    }
    applyColorSpace(f, {{"input", input}, {"exposure", exposure}});
    std::array<float, 3> out{};
    for (int c = 0; c < 3; ++c) out[c] = deep ? halfToFloat(f.half[c]) : f.rgba[c] / 255.0f;
    return out;
}

}  // namespace

TEST("log: each camera's 18% grey becomes Rec.709 mid-grey, neutral stays neutral") {
    // Grey signal (0..1 video level) from each maker's published curve.
    const std::vector<std::pair<std::string, float>> grey = {
        {"slog3", (420.0f - 64) / 876}, {"vlog", (433.0f - 64) / 876}, {"clog3", 0.328f},  {"nlog", 0.3516f},
        {"applelog", 0.48828f},         {"dlog", 0.3926f},             {"hlg", 0.3781f}};
    CHECK_EQ(grey.size(), logFormats().size());
    for (const auto& [id, v] : grey) {
        CHECK(findLogFormat(id) != nullptr);
        const auto o = through(id, v);
        for (int c = 0; c < 3; ++c)
            if (std::abs(o[c] - 0.409f) > 0.012f) {
                std::printf("       %s grey -> %.4f\n", id.c_str(), o[c]);
                CHECK(false);
            }
        // Monotonic, highlights roll off below 1 instead of clipping, exposure brightens.
        const auto dark = through(id, v * 0.5f), bright = through(id, 1.0f), up = through(id, v, 1.0);
        CHECK(dark[1] < o[1] && o[1] < bright[1]);
        CHECK(bright[1] <= 1.0f && bright[1] > 0.85f);
        CHECK(up[1] > o[1] + 0.05f);
        // 8-bit input gives the same answer within 8-bit precision.
        CHECK(std::abs(through(id, v, 0, false)[1] - o[1]) < 0.02f);
    }
    // A saturated camera-gamut colour stays inside Rec.709 (clamped, no NaN).
    VideoFrame f;
    f.allocate(1, 1, PixelFormat::RGBA16F);
    f.half = {floatToHalf(0.7f), floatToHalf(0.2f), floatToHalf(0.1f), floatToHalf(1.0f)};
    applyColorSpace(f, {{"input", "slog3"}});
    for (int c = 0; c < 3; ++c) CHECK(halfToFloat(f.half[c]) >= 0.0f && halfToFloat(f.half[c]) <= 1.0f);
    CHECK(halfToFloat(f.half[0]) > halfToFloat(f.half[2]));
    MediaAsset hlg;
    hlg.extra["transfer"] = "arib-std-b67";
    CHECK_EQ(suggestedLogFormat(hlg), std::string("hlg"));
    CHECK(suggestedLogFormat(MediaAsset{}).empty());
}

TEST("lut: .cube files parse, apply with strength, and bad files are refused") {
    std::string err;
    // 2x2x2 LUT that inverts colours (red fastest).
    std::string cube = "# comment\nTITLE \"Invert\"\nLUT_3D_SIZE 2\n";
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r) cube += std::to_string(1 - r) + " " + std::to_string(1 - g) + " " + std::to_string(1 - b) + "\r\n";
    auto lut = parseCubeLut(cube, &err);
    CHECK(lut != nullptr);
    CHECK_EQ(lut->title, std::string("Invert"));
    float r = 0.2f, g = 0.5f, b = 0.9f;
    lut->apply(r, g, b);
    CHECK(std::abs(r - 0.8f) < 1e-5f && std::abs(g - 0.5f) < 1e-5f && std::abs(b - 0.1f) < 1e-5f);
    CHECK(parseCubeLut("LUT_1D_SIZE 4\n0 0 0\n", &err) == nullptr);
    CHECK(err.find("1D") != std::string::npos);
    CHECK(parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n", &err) == nullptr);  // incomplete
    CHECK(parseCubeLut("hello world", &err) == nullptr);
    CHECK(parseCubeLut("LUT_3D_SIZE 2\n0 0 x\n", &err) == nullptr);

    const auto dir = fs::temp_directory_path() / ("vstudio_lut_" + newId());
    fs::create_directories(dir / "luts");
    std::ofstream(dir / "luts" / "invert.cube") << cube;
    VideoFrame f;
    f.allocate(2, 1, PixelFormat::RGBA16F);
    for (int c = 0; c < 8; ++c) f.half[c] = floatToHalf(c % 4 == 3 ? 1.0f : 0.2f);
    applyLut(f, {{"path", "luts/invert.cube"}, {"base", dir.string()}, {"strength", 0.5}});
    CHECK(std::abs(halfToFloat(f.half[0]) - 0.5f) < 0.002f);  // halfway to 0.8
    CHECK_THROWS(applyLut(f, {{"path", "luts/missing.cube"}, {"base", dir.string()}}));
    fs::remove_all(dir);
}

TEST("editor: Log conversion is always first, LUT and look are replaceable, all undoable") {
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    f.ed.setClipLook({c}, "teal_orange", 1.0);
    f.ed.setClipColorSpace({c}, "slog3", 0.5);
    auto fx = [&] { return f.ed.project().findClip(c)->effects; };
    CHECK_EQ(fx().size(), 2u);
    CHECK_EQ(fx()[0]["type"].get<std::string>(), std::string(kColorSpaceEffect));
    f.ed.setClipColorSpace({c}, "vlog");  // replaced, still first, still one
    CHECK_EQ(fx().size(), 2u);
    CHECK_EQ(fx()[0]["params"]["input"].get<std::string>(), std::string("vlog"));
    f.ed.setClipLut({c}, "luts/film.cube", 0.7);
    CHECK_EQ(fx().size(), 3u);
    CHECK_EQ(fx()[2]["type"].get<std::string>(), std::string(kLutEffect));
    CHECK(f.ed.undo());
    CHECK(f.ed.undo());
    CHECK_EQ(fx()[0]["params"]["input"].get<std::string>(), std::string("slog3"));
    f.ed.setClipColorSpace({c}, "");
    CHECK_EQ(fx().size(), 1u);
    CHECK_THROWS(f.ed.setClipColorSpace({c}, "nope"));
    f.ed.setTrackFlags(f.v1, true, true, false);
    CHECK_THROWS(f.ed.setClipLut({c}, "x.cube"));
}

TEST("transfer: LUT files used by clips travel inside the package") {
    const auto dir = fs::temp_directory_path() / ("vstudio_xfer_lut_" + newId());
    fs::create_directories(dir / "pc" / "luts");
    fs::create_directories(dir / "phone");
    std::string cube = "LUT_3D_SIZE 2\n";
    for (int i = 0; i < 8; ++i) cube += "0.5 0.5 0.5\n";
    std::ofstream(dir / "pc" / "luts" / "grey.cube") << cube;
    Fixture f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(0), sec(2));
    f.ed.setClipLut({c}, "luts/grey.cube");
    CHECK(packProject(f.ed.project(), (dir / "pc").string(), (dir / "p.vstudiopkg").string()).ok);
    auto u = unpackProject((dir / "p.vstudiopkg").string(), (dir / "phone").string());
    CHECK(u.ok);
    const Project p = loadProject(u.packageDir);
    const std::string path = p.findClip(c)->effects[0]["params"]["path"].get<std::string>();
    CHECK_EQ(path, std::string("media/grey.cube"));
    CHECK(fs::exists(dir / "phone" / "media" / "grey.cube"));
    fs::remove_all(dir);
}

// ------------------------------------------------------------------ keyframes

#include "vstudio/keyframes.hpp"

TEST("keyframes: eased curves, hold, values outside the keys") {
    Json keys = Json::array({keyframeJson({sec(1), 0.0, Ease::Smooth}), keyframeJson({sec(3), 10.0, Ease::Linear}),
                             keyframeJson({sec(4), 20.0, Ease::Hold}), keyframeJson({sec(5), 0.0, Ease::Smooth})});
    KeyframeCurve c(keys);
    CHECK_EQ(c.at(sec(0)), 0.0);   // before the first key: its value
    CHECK_EQ(c.at(sec(9)), 0.0);   // after the last key: its value
    CHECK(std::abs(c.at(sec(2)) - 5.0) < 1e-9);    // smooth: symmetric at the middle...
    CHECK(c.at(sec(1.25)) < 10.0 * 0.125);         // ...slow start
    CHECK(c.at(sec(2.75)) > 10.0 * 0.875);         // ...slow finish
    CHECK(std::abs(c.at(sec(3.5)) - 15.0) < 1e-9);  // linear
    CHECK_EQ(c.at(sec(4.9)), 20.0);                 // hold until the next key
}

TEST("keyframes: zoom looks even and the zoom target travels in a straight line") {
    Fixture f;
    const Id clip = f.ed.appendClip(f.v1, f.asset, sec(0), sec(8));
    const double W = 1280, H = 720;
    // Key 1 at 1 s: whole picture. Key 2 at 3 s: the region drawn around a face.
    f.ed.addKeyframe(clip, kf::Group::Picture, sec(1));
    const Transform z = zoomToRegion(Transform{}, W, H, 800, 100, 320, 180);
    CHECK(std::abs(z.scale - 4.0) < 1e-9);
    f.ed.setKeyframes(clip, sec(3), {{kf::kScale, z.scale}, {kf::kX, z.x}, {kf::kY, z.y}});
    const Clip& c = *f.ed.project().findClip(clip);
    CHECK(std::abs(animatedTransform(c, sec(2)).scale - 2.0) < 1e-9);  // geometric middle of 1x..4x
    // The region's centre (960, 190) starts there and ends at the canvas centre, on a straight line.
    const double px = 960 - W / 2, py = 190 - H / 2;  // offset from centre at 1x
    double lastDist = 1e9;
    for (int step = 0; step <= 20; ++step) {
        const Transform tr = animatedTransform(c, sec(1.0 + step * 0.1));
        const double ox = tr.x + px * tr.scale, oy = tr.y + py * tr.scale;  // where it is now, from centre
        CHECK(std::abs(ox * py - oy * px) < 1e-6 * (std::abs(ox) + std::abs(oy) + 1));  // collinear with start
        const double d = std::hypot(ox, oy);
        CHECK(d <= lastDist + 1e-9);  // only ever moves towards the centre
        lastDist = d;
    }
    CHECK(lastDist < 1e-6);
    // Splitting the clip keeps the motion seamless across the cut.
    const Transform before = animatedTransform(c, sec(2));
    const Id right = f.ed.splitClip(clip, sec(2));
    CHECK(std::abs(animatedTransform(*f.ed.project().findClip(right), sec(2)).scale - before.scale) < 1e-9);
}

TEST("keyframes: picture and audio keys are independent, auto-key, undo, limits") {
    Fixture f;
    const Id clip = f.ed.appendClip(f.v1, f.asset, sec(0), sec(8));
    f.ed.addKeyframe(clip, kf::Group::Picture, sec(1));
    f.ed.addKeyframe(clip, kf::Group::Audio, sec(1));
    f.ed.setKeyframes(clip, sec(5), {{kf::kVolume, 0.0}});
    auto clipNow = [&] { return *f.ed.project().findClip(clip); };
    CHECK_EQ(keyframeTimes(clipNow()).size(), 2u);
    // Removing the picture key at 1 s leaves the audio keys alone.
    CHECK(f.ed.removeKeyframe(clip, kf::Group::Picture, sec(1)));
    CHECK(!clipNow().keyframes.contains(kf::kScale));
    CHECK_EQ(clipNow().keyframes[kf::kVolume].size(), 2u);
    CHECK(!f.ed.removeKeyframe(clip, kf::Group::Picture, sec(1)));  // nothing left there
    // Setting a key within half a frame of an existing one replaces it.
    f.ed.setKeyframes(clip, sec(5) + Time::fromMilliseconds(5), {{kf::kVolume, 0.5}});
    CHECK_EQ(clipNow().keyframes[kf::kVolume].size(), 2u);
    CHECK_EQ(clipNow().keyframes[kf::kVolume][1]["v"].get<double>(), 0.5);
    // Auto-key: an animated property is keyed at the playhead; a static one changes the clip.
    f.ed.setPropertyAt(clip, sec(3), {{kf::kVolume, 2.0}, {kf::kScale, 1.5}});
    CHECK_EQ(clipNow().keyframes[kf::kVolume].size(), 3u);
    CHECK_EQ(clipNow().transform["scale"].get<double>(), 1.5);
    CHECK(f.ed.undo());
    CHECK_EQ(clipNow().keyframes[kf::kVolume].size(), 2u);
    // Values are clamped; keys outside the clip, unknown properties and locked tracks are refused.
    f.ed.setKeyframes(clip, sec(2), {{kf::kVolume, 99.0}, {kf::kScale, 0.0}});
    CHECK_EQ(*keyframeValue(clipNow(), kf::kVolume, sec(2)), 4.0);
    CHECK_EQ(*keyframeValue(clipNow(), kf::kScale, sec(2)), 0.05);
    CHECK_THROWS(f.ed.setKeyframes(clip, sec(30), {{kf::kVolume, 1.0}}));
    CHECK_THROWS(f.ed.setKeyframes(clip, sec(2), {{"rotation", 1.0}}));
    f.ed.clearKeyframes(clip, kf::Group::Audio);
    CHECK(!clipNow().keyframes.contains(kf::kVolume));
    CHECK(clipNow().keyframes.contains(kf::kScale));  // picture keys untouched
    f.ed.setTrackFlags(f.v1, true, true, false);
    CHECK_THROWS(f.ed.addKeyframe(clip, kf::Group::Audio, sec(1)));
    // Saved and loaded with the project.
    const Project back = projectFromJson(toJson(f.ed.project()));
    CHECK(back.findClip(clip)->keyframes == clipNow().keyframes);
}

TEST("keyframes: preview/export follow picture keys; volume fades without clicks") {
    Fixture f;
    const Id clip = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    f.ed.setKeyframes(clip, sec(1), {{kf::kOpacity, 0.0}});
    f.ed.setKeyframes(clip, sec(2), {{kf::kOpacity, 1.0}});
    f.ed.setClipVolume(clip, 0.0);  // only the audio track's clip is heard
    const Id a = f.ed.appendClip(f.a1, f.asset, sec(0), sec(4));
    f.ed.setKeyframes(a, sec(1), {{kf::kVolume, 0.0}});
    f.ed.setKeyframes(a, sec(2), {{kf::kVolume, 1.0}});
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(f.ed.project(), pool);
    const std::size_t centre = (36 * 128 + 64) * 4;
    auto lum = [&](double t) { return r.renderVideo(sec(t), 128, 72).rgba[centre + 1]; };
    CHECK_EQ(int(lum(0.5)), 0);  // faded out (black canvas)
    CHECK(lum(1.5) > 0 && lum(1.5) < lum(2.5));
    // Audio: silent before 1 s, full after 2 s, and the gain never jumps between samples.
    const AudioBuffer au = r.renderAudio(0, 48000 * 3);
    double early = 0, late = 0;
    for (int i = 0; i < 48000; ++i) early = std::max(early, double(std::abs(au.samples[i * 2])));
    for (int i = 48000 * 2; i < 48000 * 3; ++i) late = std::max(late, double(std::abs(au.samples[i * 2])));
    CHECK(early < 1e-6 && late > 0.2);
    // The fake source is a 0..0.25 sawtooth with period 48; compare each period's peak to the previous.
    double prevPeak = 0;
    for (int p = 48000 / 48; p < 2 * 48000 / 48; ++p) {
        double peak = 0;
        for (int i = 0; i < 48; ++i) peak = std::max(peak, double(au.samples[(p * 48 + i) * 2]));
        CHECK(peak >= prevPeak - 1e-6);  // rises steadily through the fade
        CHECK(peak - prevPeak < 0.01);   // in small steps
        prevPeak = peak;
    }
}

// ---- covers (blur/mosaic/bars over a region) and tracking ----

namespace {
VideoFrame texturedFrame(int w, int h, int boxX, int boxY, int boxSize) {
    VideoFrame f;
    f.allocate(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &f.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
            // Smooth background; a high-contrast checker "logo" inside the box.
            std::uint8_t v = static_cast<std::uint8_t>(60 + (x * 40) / w + (y * 30) / h);
            if (x >= boxX && x < boxX + boxSize && y >= boxY && y < boxY + boxSize)
                v = (((x - boxX) / 4 + (y - boxY) / 4) % 2) ? 240 : 20;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    return f;
}

double regionContrast(const VideoFrame& f, int x0, int y0, int size) {
    double lo = 255, hi = 0;
    for (int y = y0; y < y0 + size; ++y)
        for (int x = x0; x < x0 + size; ++x) {
            const double v = f.rgba[(static_cast<std::size_t>(y) * f.width + x) * 4];
            lo = std::min(lo, v), hi = std::max(hi, v);
        }
    return hi - lo;
}

// Decoder of a synthetic clip where a checker square moves right and down.
class MovingBoxDecoder : public IMediaDecoder {
public:
    bool open(const std::string&, std::string*) override { return true; }
    std::optional<VideoFrame> videoFrameAt(Time t) override {
        const double s = t.seconds();
        return texturedFrame(320, 180, 40 + static_cast<int>(s * 60), 50 + static_cast<int>(s * 20), 32);
    }
    AudioBuffer readAudio(Time, std::int64_t, int sr, int ch) override { return AudioBuffer{sr, ch, {}}; }
};
}  // namespace

TEST("cover: every style hides the region and leaves the rest untouched") {
    CHECK(coverStyles().size() >= 36);
    CHECK_EQ(coverShapes().size(), 9u);
    const VideoFrame base = texturedFrame(320, 180, 140, 70, 40);
    for (const auto& st : coverStyles()) {
        for (const char* shape : {"rect", "heart"}) {
            VideoFrame f = base;
            Json p = defaultCover(st.id, shape);
            p["x"] = 130.0 / 320;
            p["y"] = 60.0 / 180;
            p["w"] = 60.0 / 320;
            p["h"] = 60.0 / 180;
            p["feather"] = 0.0;
            applyCover(f, p, sec(0.5));
            // Outside the region (plus feather) nothing changes.
            CHECK_EQ(regionContrast(f, 0, 0, 40), regionContrast(base, 0, 0, 40));
            CHECK(f.rgba[(10 * 320 + 10) * 4] == base.rgba[(10 * 320 + 10) * 4]);
            // Inside: the picture changed (the checker is no longer intact).
            CHECK(std::memcmp(f.rgba.data(), base.rgba.data(), f.rgba.size()) != 0);
        }
    }
    // Blur / mosaic / black really hide the checker in the region's core.
    for (const char* id : {"blur_strong", "mosaic_large", "black", "frosted"}) {
        VideoFrame f = base;
        Json p = defaultCover(id, "rect");
        p.update(Json{{"x", 120.0 / 320}, {"y", 50.0 / 180}, {"w", 80.0 / 320}, {"h", 80.0 / 180}, {"feather", 0.0}});
        applyCover(f, p, Time{});
        CHECK(regionContrast(f, 150, 80, 20) < 0.5 * regionContrast(base, 150, 80, 20));
    }
    // Inverted: everything except the region.
    VideoFrame inv = base;
    Json p = defaultCover("black", "rect");
    p.update(Json{{"x", 0.4}, {"y", 0.35}, {"w", 0.2}, {"h", 0.3}, {"feather", 0.0}, {"invert", true}});
    applyCover(inv, p, Time{});
    CHECK(inv.rgba[(5 * 320 + 5) * 4] < 10);
    CHECK_EQ(regionContrast(inv, 145, 75, 20), regionContrast(base, 145, 75, 20));
    // Deep (half float) frames work too.
    VideoFrame deep = convertedFrame(base, PixelFormat::RGBA16F);
    applyCover(deep, defaultCover("mosaic", "ellipse"), Time{});
    CHECK(deep.deep());
}

TEST("cover: keyframed region follows motion; editor add/edit/follow/undo") {
    Fixture f;
    const Id clip = f.ed.appendClip(f.v1, f.asset, sec(0), sec(8));
    const Id cov = f.ed.addCover(clip, Json{{"style", "mosaic"}, {"shape", "ellipse"}});
    CHECK(f.ed.coverParams(clip, cov) != nullptr);
    CHECK_THROWS(f.ed.addCover(clip, Json{{"style", "nope"}}));
    // Static move.
    f.ed.setCoverAt(clip, cov, sec(1), {{"x", 0.1}, {"y", 0.2}});
    CHECK_EQ((*f.ed.coverParams(clip, cov))["x"].get<double>(), 0.1);
    // Follow: key at 1 s, move at 3 s -> keyed; in between it glides.
    f.ed.keyCover(clip, cov, sec(1));
    f.ed.setCoverAt(clip, cov, sec(3), {{"x", 0.5}});
    const Json mid = coverParamsAt(*f.ed.coverParams(clip, cov), sec(2));
    CHECK(mid["x"].get<double>() > 0.1 && mid["x"].get<double>() < 0.5);
    CHECK_EQ(coverParamsAt(*f.ed.coverParams(clip, cov), sec(0))["x"].get<double>(), 0.1);
    // Tracking result replaces keys in its range with linear keys.
    f.ed.setCoverTrack(clip, cov, {{sec(1), {0.1, 0.2, 0.3, 0.3}}, {sec(2), {0.2, 0.25, 0.3, 0.3}}, {sec(4), {0.6, 0.3, 0.3, 0.3}}});
    const Json& anim = (*f.ed.coverParams(clip, cov))["anim"];
    CHECK_EQ(anim["x"].size(), 3u);
    CHECK_EQ(anim["x"][1]["ease"].get<std::string>(), std::string("linear"));
    CHECK_NEAR(coverParamsAt(*f.ed.coverParams(clip, cov), sec(3))["x"].get<double>(), 0.4, 1e-9);
    // Stop following keeps the region shown at that moment.
    f.ed.clearCoverKeys(clip, cov, sec(4));
    CHECK(!f.ed.coverParams(clip, cov)->contains("anim"));
    CHECK_NEAR((*f.ed.coverParams(clip, cov))["x"].get<double>(), 0.6, 1e-9);
    CHECK(f.ed.undo());
    CHECK(f.ed.coverParams(clip, cov)->contains("anim"));
    f.ed.updateCover(clip, cov, Json{{"style", "black"}, {"color", nullptr}});
    CHECK_EQ((*f.ed.coverParams(clip, cov))["style"].get<std::string>(), std::string("black"));
    CHECK(!f.ed.coverParams(clip, cov)->contains("color"));
    // Several covers per clip; removing one keeps the other.
    const Id cov2 = f.ed.addCover(clip, Json{{"style", "blur"}});
    f.ed.removeCover(clip, cov);
    CHECK(f.ed.coverParams(clip, cov) == nullptr);
    CHECK(f.ed.coverParams(clip, cov2) != nullptr);
    // Rendered through the effect module with the clip's source time.
    ModuleRegistry r;
    registerBuiltinModules(r);
    VideoFrame frame = texturedFrame(64, 36, 20, 10, 16);
    const VideoFrame before = frame;
    applyClipEffects(r, *f.ed.project().findClip(clip), frame, sec(1), nullptr);
    CHECK(std::memcmp(frame.rgba.data(), before.rgba.data(), frame.rgba.size()) != 0);
    const Project back = projectFromJson(toJson(f.ed.project()));
    CHECK(back.findClip(clip)->effects == f.ed.project().findClip(clip)->effects);
}

TEST("tracker: follows a moving logo frame by frame, then few keys describe it") {
    MovingBoxDecoder dec;
    const TrackBox start{40.0 / 320, 50.0 / 180, 32.0 / 320, 32.0 / 180};
    int calls = 0;
    const TrackResult r = trackRegion(dec, Time{}, sec(2), {30, 1}, start, [&](double) { return ++calls > 0; });
    CHECK(r.error.empty());
    CHECK(!r.lost);
    CHECK(r.samples.size() >= 60u);
    // At 2 s the box is at (160, 90) px.
    const TrackBox& end = r.samples.back().box;
    CHECK(std::abs(end.x * 320 - 160) < 3);
    CHECK(std::abs(end.y * 180 - 90) < 3);
    CHECK(std::abs(end.w * 320 - 32) < 3);
    for (const auto& s : r.samples) CHECK(s.confidence > 0.6);
    // Straight-line motion collapses to a handful of keys.
    const auto keys = simplifyTrack(r.samples, 0.008);  // ~1.5 px on this tiny frame (the box moves in whole pixels)
    CHECK(keys.size() <= 12u);
    CHECK(keys.front().sourceTime == r.samples.front().sourceTime);
    // Cancel stops at once; a tiny box is refused.
    const TrackResult c = trackRegion(dec, Time{}, sec(2), {30, 1}, start, [](double) { return false; });
    CHECK(c.cancelled);
    CHECK(!trackRegion(dec, Time{}, sec(1), {30, 1}, TrackBox{0.5, 0.5, 0.001, 0.001}).error.empty());
    // A box over a flat area (every candidate matches) neither balloons nor slows down:
    // its size stays within 2x and it never leaves the picture.
    struct FlatDecoder : MovingBoxDecoder {
        std::optional<VideoFrame> videoFrameAt(Time) override {
            VideoFrame f;
            f.width = 320;
            f.height = 180;
            f.rgba.assign(static_cast<std::size_t>(320) * 180 * 4, 128);
            return f;
        }
    } flat;
    const TrackResult fr = trackRegion(flat, Time{}, sec(5), {30, 1}, TrackBox{0.6, 0.6, 0.3, 0.3});
    CHECK(fr.samples.size() >= 140u);
    for (const auto& s : fr.samples) {
        CHECK(s.box.w <= 0.6 + 1e-9 && s.box.w >= 0.15 - 1e-9);
        CHECK(s.box.x + s.box.w / 2 >= 0 && s.box.x + s.box.w / 2 <= 1);
    }
}

// ------------------------------------------------------------------ transitions

namespace {
// Mean absolute difference per channel, 0..255.
double frameDiff(const VideoFrame& a, const VideoFrame& b) {
    double sum = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i) sum += std::abs(int(a.rgba[i]) - int(b.rgba[i]));
    return sum / static_cast<double>(a.rgba.size());
}
}  // namespace

TEST("transitions: every type starts exactly on A, ends exactly on B, and moves without jumps") {
    CHECK(transitionCatalog().size() >= 30u);
    std::set<std::string> ids;
    for (const auto& t : transitionCatalog()) ids.insert(t.id);
    CHECK_EQ(ids.size(), transitionCatalog().size());
    // Two smooth, different pictures (gradients), so a step measures the transition, not texture aliasing.
    auto gradient = [](bool flip) {
        VideoFrame g;
        g.allocate(96, 54);
        for (int y = 0; y < 54; ++y)
            for (int x = 0; x < 96; ++x) {
                std::uint8_t* px = &g.rgba[(static_cast<std::size_t>(y) * 96 + x) * 4];
                px[0] = static_cast<std::uint8_t>(flip ? 230 - x * 2 : 20 + x * 2);
                px[1] = static_cast<std::uint8_t>(flip ? 40 + y * 3 : 200 - y * 3);
                px[2] = static_cast<std::uint8_t>(flip ? 60 : 180);
                px[3] = 255;
            }
        return g;
    };
    const VideoFrame a = gradient(false), bInv = gradient(true);
    for (const auto& info : transitionCatalog()) {
        VideoFrame out, prev;
        out.allocate(96, 54);
        applyTransition(info.id, a, bInv, 0.0, out, sec(1));
        CHECK(frameDiff(out, a) < 0.6);
        applyTransition(info.id, a, bInv, 1.0, out, sec(1));
        CHECK(frameDiff(out, bInv) < 0.6);
        // 50 steps: no single step changes the picture abruptly ("không bục một phát").
        double worst = 0;
        for (int s = 0; s <= 50; ++s) {
            applyTransition(info.id, a, bInv, s / 50.0, out, sec(1));
            if (s > 0) worst = std::max(worst, frameDiff(out, prev));
            prev = out;
        }
        if (std::getenv("VSTUDIO_TRANSITION_STEPS") || worst >= 10) std::printf("  transition %s: largest step %.1f\n", info.id.c_str(), worst);
        CHECK(worst < 10);
    }
    // Deep frames work too.
    VideoFrame da = convertedFrame(a, PixelFormat::RGBA16F), db = convertedFrame(bInv, PixelFormat::RGBA16F), dout;
    dout.allocate(96, 54, PixelFormat::RGBA16F);
    applyTransition("circle_open", da, db, 1.0, dout);
    CHECK(frameDiff(convertedFrame(dout, PixelFormat::RGBA8), bInv) < 1.0);
}

TEST("transitions: centred on the cut, from/to nothing inside the clip, clamped, audio fades") {
    Fixture f;
    const Id c1 = f.ed.appendClip(f.v1, f.asset, sec(2), sec(4));  // timeline 0..2
    const Id c2 = f.ed.appendClip(f.v1, f.asset, sec(6), sec(8));  // timeline 2..4
    f.ed.setTransition(c2, TransitionEdge::In, "fade", sec(1));
    const Track& tr = *f.ed.project().findTrack(f.v1);
    CHECK(!transitionAt(tr, sec(1.4)));
    auto at = transitionAt(tr, sec(2.0));
    CHECK(at && at->type == "fade" && at->from->id == c1 && at->to->id == c2);
    CHECK(std::abs(at->progress - 0.5) < 1e-9);
    CHECK(at->start == sec(1.5) && at->end == sec(2.5));
    CHECK(!transitionAt(tr, sec(2.5)));
    // The end of c1 touching c2 IS that cut: setting it there lands on c2's start.
    f.ed.setTransition(c1, TransitionEdge::Out, "wipe_left", sec(0.8));
    CHECK(f.ed.project().findClip(c2)->transitionIn["type"] == "wipe_left");
    CHECK(f.ed.project().findClip(c1)->transitionOut.is_null());
    f.ed.setTransition(c2, TransitionEdge::In, "fade", sec(1));
    // Free start and end: inside the clip, at most half of it.
    f.ed.setTransition(c1, TransitionEdge::In, "dip_black", sec(0.4));
    f.ed.setTransition(c2, TransitionEdge::Out, "fade", sec(5));
    at = transitionAt(f.ed.project().tracks[0], sec(0.2));
    CHECK(at && !at->from && at->to->id == c1 && std::abs(at->progress - 0.5) < 1e-9);
    at = transitionAt(f.ed.project().tracks[0], sec(3.5));
    CHECK(at && at->from->id == c2 && !at->to && at->start == sec(3.0));  // 5 s clamped to half of 2 s
    const auto cuts = cutPoints(f.ed.project().tracks[0]);
    CHECK_EQ(cuts.size(), 3u);  // start, the cut, end
    CHECK(cuts[1].from && cuts[1].to && cuts[1].duration == sec(1));
    CHECK(cuts[2].duration == sec(1));
    // Audio: c1 falls over the first half of the cut, c2 rises over the second; silent at the cut itself.
    const Track& t2 = f.ed.project().tracks[0];
    const Clip& k1 = t2.clips[0];
    const Clip& k2 = t2.clips[1];
    CHECK(std::abs(transitionGain(t2, k1, sec(1.0)) - 1.0) < 1e-9);
    CHECK(transitionGain(t2, k1, sec(1.75)) > 0.4 && transitionGain(t2, k1, sec(1.75)) < 0.6);
    CHECK(transitionGain(t2, k1, sec(2.0)) < 1e-9);
    CHECK(transitionGain(t2, k2, sec(2.0)) < 1e-9);
    CHECK(std::abs(transitionGain(t2, k2, sec(2.5)) - 1.0) < 1e-9);
    CHECK(transitionGain(t2, k1, sec(0.0)) < 1e-9);  // fades in from silence
    // Split keeps the clip's start on the left half and its end on the right; undo; JSON round trip.
    const Id right = f.ed.splitClip(c2, sec(3));
    CHECK(f.ed.project().findClip(c2)->transitionIn["type"] == "fade");
    CHECK(f.ed.project().findClip(c2)->transitionOut.is_null());
    CHECK(f.ed.project().findClip(right)->transitionIn.is_null());
    CHECK(f.ed.project().findClip(right)->transitionOut["type"] == "fade");
    const Project back = projectFromJson(toJson(f.ed.project()));
    CHECK(back.findClip(right)->transitionOut == f.ed.project().findClip(right)->transitionOut);
    CHECK(back.findClip(c2)->extra.empty() || !back.findClip(c2)->extra.contains("transition_in"));
    CHECK_THROWS(f.ed.setTransition(c1, TransitionEdge::In, "nope"));
    f.ed.setTransition(c2, TransitionEdge::In, "");
    CHECK(f.ed.project().findClip(c2)->transitionIn.is_null());
    CHECK(f.ed.undo());
    CHECK(f.ed.project().findClip(c2)->transitionIn["type"] == "fade");
    // Apply to all: every cut between touching clips.
    CHECK_EQ(f.ed.setTransitionEverywhere("push_left", sec(0.6)), 2);
    CHECK(f.ed.project().findClip(right)->transitionIn["type"] == "push_left");
    CHECK(f.ed.project().findClip(c1)->transitionIn["type"] == "dip_black");  // free starts untouched
}

TEST("render: a transition blends both clips, each reading past its edge, and fades in from black") {
    Fixture f;
    f.ed.appendClip(f.v1, f.asset, sec(2), sec(4));                 // source frames 60..119 on 0..2 s
    const Id c2 = f.ed.appendClip(f.v1, f.asset, sec(6), sec(8));  // source frames 180..239 on 2..4 s
    f.ed.setTransition(c2, TransitionEdge::In, "fade", sec(1));
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(f.ed.project(), pool);
    const std::size_t centre = (36 * 128 + 64) * 4;
    auto red = [&](double s) { return int(r.renderVideo(sec(s), 128, 72).rgba[centre]); };
    CHECK_EQ(red(1.4), 102);                    // before the window: c1 alone
    CHECK(std::abs(red(2.0) - 150) <= 1);       // c1 at source 4.0 (frame 120, past its out) + c2 at 6.0 (180)
    CHECK(std::abs(red(2.25) - 178) <= 1);      // eased: already mostly c2
    CHECK_EQ(red(2.5), 195);                    // after: c2 alone
    f.ed.setTransition(f.ed.project().tracks[0].clips[0].id, TransitionEdge::In, "fade", sec(0.4));
    Renderer r2(f.ed.project(), pool);
    CHECK(std::abs(int(r2.renderVideo(sec(0.2), 128, 72).rgba[centre]) - 33) <= 1);  // half of frame 66 over black
    // Audio: silent exactly at the cut, full a little after.
    const AudioBuffer au = r2.renderAudio(sec(2.0).toSample(48000), 4800);
    CHECK(std::abs(au.samples[0]) < 1e-6);
    const AudioBuffer full = r2.renderAudio(sec(3.0).toSample(48000), 480);
    float peak = 0;
    for (float v : full.samples) peak = std::max(peak, std::abs(v));
    CHECK(peak > 0.2f);
}

// ------------------------------------------------------------------ vector, fonts, text, stickers

TEST("vector: exact area coverage, strokes and blur keep coverage") {
    vec::Path sq;
    sq.rect(2.25, 3.5, 10, 6);
    const vec::Mask m = vec::rasterize(sq.flatten({}), 0, 0, 20, 20);
    double sum = 0;
    for (float v : m.v) sum += v;
    CHECK(std::abs(sum - 60) < 1e-3);
    CHECK(std::abs(m.at(2, 5) - 0.75f) < 1e-4);  // a quarter of the pixel is outside
    vec::Path c;
    c.circle(50, 50, 30);
    const vec::Mask cm = vec::rasterize(c.flatten({}, 0.05), 0, 0, 100, 100);
    sum = 0;
    for (float v : cm.v) sum += v;
    if (std::abs(sum - 3.14159265 * 900) >= 3) std::printf("  circle area %.2f\n", sum);
    CHECK(std::abs(sum - 3.14159265 * 900) < 9);  // flattening within 0.05 px
    // Overlapping shapes union (never more than full coverage).
    vec::Path two;
    two.rect(0, 0, 10, 10).rect(5, 0, 10, 10);
    const vec::Mask tm = vec::rasterize(two.flatten({}), 0, 0, 20, 10);
    sum = 0;
    for (float v : tm.v) sum += v;
    CHECK(std::abs(sum - 150) < 1e-3);
    vec::Mask blurred = m.padded(8);
    blurred.blur(4);
    double bs = 0;
    for (float v : blurred.v) bs += v;
    CHECK(std::abs(bs - 60) < 0.5);
    VideoFrame f;
    f.allocate(40, 40);
    vec::Canvas cv(f);
    vec::Path ln;
    ln.moveTo(5, 20).lineTo(35, 20);
    cv.stroke(ln, 4, vec::Paint::solid(vec::Color::hex("#ff0000")));
    CHECK_EQ(int(f.rgba[(20 * 40 + 20) * 4]), 255);
    CHECK_EQ(int(f.rgba[(20 * 40 + 20) * 4 + 3]), 255);
    CHECK_EQ(int(f.rgba[(10 * 40 + 20) * 4 + 3]), 0);
    // RGBA16F targets too.
    VideoFrame h;
    h.allocate(40, 40, PixelFormat::RGBA16F);
    vec::Canvas hc(h);
    hc.fill(sq, vec::Paint::solid(vec::Color::hex("#ffffff")));
    CHECK(std::abs(halfToFloat(h.half[(5 * 40 + 5) * 4 + 3]) - 1.0f) < 1e-3);
}

TEST("fonts: bundled fonts cover Vietnamese; decomposed input is composed") {
    CHECK(text::fontCatalog().size() >= 7u);
    const std::u32string vi = text::decodeUtf8(
        "ạảấầẩẫậắằẳẵặẹẻẽếềểễệỉịọỏốồổỗộớờởỡợụủứừửữựỳỵỷỹđĐƯƠươẠẢẤẦẨẪẬẮẰẲẴẶẸẺẼẾỀỂỄỆỈỊỌỎỐỒỔỖỘỚỜỞỠỢỤỦỨỪỬỮỰỲỴỶỸ");
    for (const auto& fi : text::fontCatalog()) {
        const auto f = text::Font::get(fi.id);
        CHECK_EQ(f->id(), fi.id);
        int missing = 0;
        for (char32_t ch : vi) missing += f->glyph(ch) == 0;
        CHECK_EQ(missing, 0);
        CHECK(!f->outline(f->glyph(U'Ệ')).empty());
        CHECK(f->ascent() < 0 && f->descent() > 0);
    }
    CHECK(text::decodeUtf8("a\xCC\xA3\xCC\x82") == std::u32string(U"ậ"));  // a + dot below + circumflex
    CHECK(text::decodeUtf8("a\xCC\x82\xCC\xA3") == std::u32string(U"ậ"));  // other order
    CHECK(text::decodeUtf8("o\xCC\x9B\xCC\x80") == std::u32string(U"ờ"));  // o + horn + grave
    CHECK(text::decodeUtf8("Vie\xCC\xA3\xCC\x82t") == std::u32string(U"Việt"));
    CHECK_EQ(text::Font::get("no-such-font")->id(), std::string(text::defaultFontId()));
    CHECK(!text::registerFont("x", "X", {1, 2, 3}));  // not a font
}

TEST("motion: every animation starts hidden, ends at rest and never jumps") {
    CHECK(motion::animations().size() >= 30u);
    CHECK(motion::loops().size() >= 8u);
    auto hidden = [](const motion::Pose& p) {
        return p.opacity < 1e-6 || std::abs(p.sx * p.sy) < 1e-6 || (p.wipe != 0 && p.reveal < 1e-6);
    };
    for (const auto& a : motion::animations()) {
        const int n = a.perLetter ? 8 : 1;
        for (int i = 0; i < n; ++i) {
            CHECK(hidden(motion::inPose(a.id, 0, i, n, 3)));
            const motion::Pose end = motion::inPose(a.id, 1, i, n, 3);
            CHECK(end.opacity == 1 && end.dx == 0 && end.dy == 0 && end.sx == 1 && end.sy == 1 && end.rotation == 0 &&
                  end.blur == 0 && end.reveal == 1);
            motion::Pose prev = motion::inPose(a.id, 0, i, n, 3);
            double worst = 0;
            for (int k = 1; k <= 400; ++k) {
                const motion::Pose p = motion::inPose(a.id, k / 400.0, i, n, 3);
                worst = std::max({worst, std::abs(p.dx - prev.dx), std::abs(p.dy - prev.dy), std::abs(p.sx - prev.sx),
                                  std::abs(p.sy - prev.sy), std::abs(p.rotation - prev.rotation),
                                  std::abs(p.opacity - prev.opacity), std::abs(p.reveal - prev.reveal)});
                prev = p;
            }
            if (worst > 0.12) std::printf("  %s letter %d: step %.3f\n", a.id.c_str(), i, worst);
            CHECK(worst <= 0.12);
        }
    }
    // Loops fade in after the in animation and out before the out animation.
    const Json c{{"in", {{"type", "fade"}, {"duration", 0.5}}}, {"out", {{"type", "fade"}, {"duration", 0.5}}},
                 {"loop", {{"type", "pulse"}, {"speed", 1.0}}}};
    CHECK_EQ(motion::timing(c, 0.5, 4).loopAmount, 0.0);
    CHECK_EQ(motion::timing(c, 2, 4).loopAmount, 1.0);
    CHECK_EQ(motion::timing(c, 3.5, 4).loopAmount, 0.0);
    // Short clips: in and out shrink so they never overlap.
    const auto t = motion::timing(c, 0.3, 0.6);
    CHECK(std::abs(t.inProgress - 1) < 1e-9 && t.outProgress == 0);
}

namespace {
double alphaSum(const VideoFrame& f) {
    double s = 0;
    for (std::size_t i = 3; i < f.rgba.size(); i += 4) s += f.rgba[i];
    return s / 255;
}
}  // namespace

TEST("text: draws, animates in and out smoothly, sizes its box") {
    Json c = text::defaultTextContent("Xin chào Việt Nam");
    c["in"] = {{"type", "pop_letters"}, {"duration", 1.0}};
    c["out"] = {{"type", "blur_in"}, {"duration", 1.0}};
    auto draw = [&](double t) {
        VideoFrame f;
        f.allocate(320, 180);
        text::drawText(c, t, 4, vec::Affine::translate(160, 90) * vec::Affine::scale(0.3), f);
        return f;
    };
    const double rest = alphaSum(draw(2));
    CHECK(rest > 500);
    CHECK(alphaSum(draw(0)) < 1);
    CHECK(alphaSum(draw(4)) < 1);
    double prev = 0, worst = 0;
    for (int k = 0; k <= 100; ++k) {
        const double a = alphaSum(draw(k / 100.0));
        worst = std::max(worst, std::abs(a - prev));
        prev = a;
    }
    CHECK(worst < rest * 0.15);
    CHECK(text::textStill(c, 2, 4));
    CHECK(!text::textStill(c, 0.5, 4));
    const auto b = text::textBounds(c);
    CHECK(b[0] < -300 && b[2] > 300 && b[1] < -40 && b[3] > 40);
    // Every preset and every animation draws something at rest.
    for (const auto& p : text::textPresets()) {
        Json s = text::defaultTextContent("Ổn");
        s.update(p.style);
        VideoFrame f;
        f.allocate(120, 80);
        text::drawText(s, 2, 4, vec::Affine::translate(60, 40) * vec::Affine::scale(0.5), f);
        CHECK(alphaSum(f) > 50);
    }
}

TEST("stickers: a large library, each drawing inside its box at every moment") {
    const auto& cat = stickers::stickerCatalog();
    CHECK(cat.size() >= 120u);
    std::set<std::string> ids;
    for (const auto& s : cat) ids.insert(s.id);
    CHECK_EQ(ids.size(), cat.size());
    CHECK(stickers::stickerCategories().size() >= 9u);
    for (const auto& s : cat) {
        for (double t : {0.0, 0.37, 1.1, 2.3}) {
            VideoFrame f;
            f.allocate(64, 64);
            stickers::drawStickerIcon(s.id, t, f);
            if (alphaSum(f) <= 20) std::printf("  %s at %.2f s draws %.1f\n", s.id.c_str(), t, alphaSum(f));
            CHECK(alphaSum(f) > 20);
            double edge = 0;  // the outer ring of pixels stays (almost) empty
            for (int i = 0; i < 64; ++i)
                edge += f.rgba[(i * 64) * 4 + 3] + f.rgba[(i * 64 + 63) * 4 + 3] + f.rgba[i * 4 + 3] + f.rgba[(63 * 64 + i) * 4 + 3];
            if (edge / 255 > 4) std::printf("  %s at %.2f s touches the edge (%.1f)\n", s.id.c_str(), t, edge / 255);
            CHECK(edge / 255 <= 4);
        }
    }
    // As a clip: pops in from nothing.
    const Json c = stickers::defaultStickerContent("heart_red");
    auto draw = [&](double t) {
        VideoFrame f;
        f.allocate(200, 200);
        stickers::drawSticker(c, t, 3, vec::Affine::translate(100, 100) * vec::Affine::scale(0.5), f);
        return alphaSum(f);
    };
    CHECK(draw(0) < 1);
    CHECK(draw(1.5) > 1000);
    CHECK(draw(3) < 1);
}

TEST("overlays: every effect draws, fades in and out, stays in its region, and repeats exactly") {
    const auto& cat = overlays::overlayCatalog();
    CHECK(cat.size() >= 20u);
    std::set<std::string> ids;
    for (const auto& o : cat) ids.insert(o.id);
    CHECK_EQ(ids.size(), cat.size());
    CHECK(overlays::overlayCategories().size() >= 4u);
    const vec::Affine place = vec::Affine::translate(160, 90) * vec::Affine::scale(320.0 / 1920);
    for (const auto& o : cat) {
        const Json c = overlays::defaultOverlayContent(o.id, 1920, 1080);
        auto draw = [&](const Json& content, double t) {
            VideoFrame f;
            f.allocate(320, 180);
            overlays::drawOverlay(content, t, 6, place, f);
            return f;
        };
        CHECK(alphaSum(draw(c, 0)) < 1);  // fades in from nothing
        CHECK(alphaSum(draw(c, 6)) < 1);  // and out to nothing
        const VideoFrame a = draw(c, 2.5);
        if (alphaSum(a) <= 20) std::printf("  %s draws %.1f\n", o.id.c_str(), alphaSum(a));
        CHECK(alphaSum(a) > 20);
        CHECK(draw(c, 2.5).rgba == a.rgba);  // a pure function of time: seeking shows the same frame
        // A region (the left third, centred vertically): nothing outside it.
        Json part = c;
        part["w"] = 640;
        part["h"] = 540;
        VideoFrame r;
        r.allocate(320, 180);
        overlays::drawOverlay(part, 2.5, 6, vec::Affine::translate(160 - 640.0 / 6, 90) * vec::Affine::scale(320.0 / 1920), r);
        double outside = 0;
        for (int y = 0; y < 180; ++y)
            for (int x = 0; x < 320; ++x)
                if (x >= 108 || y < 44 || y >= 136) outside += r.rgba[(y * 320 + x) * 4 + 3];
        CHECK(outside == 0);
        VideoFrame icon;
        icon.allocate(96, 54);
        overlays::drawOverlayIcon(o.id, 1.5, icon);
        CHECK(icon.rgba[3] == 255);
    }
    // Through the editor: a full-frame overlay on a "Lớp phủ" track, drawn by the renderer.
    Fixture f;
    f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    Json snow = overlays::defaultOverlayContent("snow");
    snow.erase("w");
    snow.erase("h");
    const Id id = f.ed.addGeneratedClip(snow, sec(0), sec(4));
    const Clip* clip = f.ed.project().findClip(id);
    CHECK_EQ(clip->content.value("w", 0), f.ed.project().canvas.width);
    CHECK_EQ(clip->content.value("h", 0), f.ed.project().canvas.height);
    CHECK(f.ed.project().trackOfClip(id)->name == "Lớp phủ");
    const auto rect = generatedRectOnCanvas(f.ed.project(), *clip, sec(1));
    CHECK(std::abs(rect[0]) < 1e-6 && std::abs(rect[2] - f.ed.project().canvas.width) < 1e-6);
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(f.ed.project(), pool);
    Project bare = f.ed.project();
    bare.tracks.erase(std::remove_if(bare.tracks.begin(), bare.tracks.end(), [&](const Track& t) { return t.findClip(id); }), bare.tracks.end());
    Renderer rb(bare, pool);
    const VideoFrame with = r.renderVideo(sec(2), 320, 180), without = rb.renderVideo(sec(2), 320, 180);
    long diff = 0;
    for (std::size_t i = 0; i < with.rgba.size(); ++i) diff += std::abs(int(with.rgba[i]) - int(without.rgba[i]));
    CHECK(diff > 2000);
}

TEST("generated clips: added on an overlay track, rendered, undone, saved") {
    Fixture f;
    f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    Json content = text::defaultTextContent("Chào");
    content["in"] = {{"type", "fade"}, {"duration", 0.5}};
    const Id t1 = f.ed.addGeneratedClip(content, sec(1), sec(2));
    const Id overlay = f.ed.project().trackOfClip(t1)->id;
    CHECK(overlay != f.v1 && f.ed.project().findTrack(overlay)->type == TrackType::Video);
    CHECK(f.ed.project().findTrack(f.v1) < f.ed.project().findTrack(overlay));  // stacked above the main track
    const Id s1 = f.ed.addGeneratedClip(stickers::defaultStickerContent("star_gold"), sec(2), sec(2));
    CHECK(f.ed.project().trackOfClip(s1)->id != overlay);  // overlaps the text: a new track on top
    CHECK(f.ed.project().findTrack(overlay) < f.ed.project().trackOfClip(s1));
    const Id t2 = f.ed.addGeneratedClip(text::defaultTextContent("Sau"), sec(3.5), sec(1));
    CHECK(f.ed.project().trackOfClip(t2)->id == overlay);  // the top track is busy there: the free one below
    CHECK_THROWS(f.ed.addGeneratedClip(content, sec(0), sec(1), f.a1));                 // not on audio
    // Trimmed or extended freely: no media end.
    f.ed.trimClip(s1, TrimEdge::End, sec(9));
    CHECK(f.ed.project().findClip(s1)->timelineOut() == sec(9));
    f.ed.updateClipContent(t1, {{"text", "Xin chào"}, {"stroke", nullptr}});
    CHECK(f.ed.project().findClip(t1)->content["text"] == "Xin chào");
    CHECK(!f.ed.project().findClip(t1)->content.contains("stroke"));
    CHECK_THROWS(f.ed.updateClipContent(t1, {{"kind", nullptr}}));
    // Render: text over the picture, fading in from nothing.
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    FrameCache cache(64 << 20);
    Renderer r(f.ed.project(), pool, &cache);
    Renderer bare(f.ed.project(), pool);
    const VideoFrame atStart = r.renderVideo(sec(1), 320, 180), shown = r.renderVideo(sec(1.8), 320, 180);
    const VideoFrame before = bare.renderVideo(sec(0.9), 320, 180);
    long diffStart = 0, diffShown = 0;
    for (std::size_t i = 0; i < shown.rgba.size(); ++i) diffShown += std::abs(int(shown.rgba[i]) - int(before.rgba[i]));
    (void)atStart;
    CHECK(diffShown > 20000);
    // Same picture through the cache.
    const VideoFrame again = r.renderVideo(sec(1.8), 320, 180);
    CHECK(again.rgba == shown.rgba);
    (void)diffStart;
    // Save and load keep the content.
    const Project back = projectFromJson(toJson(f.ed.project()));
    CHECK(back.findClip(t1)->content == f.ed.project().findClip(t1)->content);
    // Rect for the UI handles follows the transform.
    const auto rect = generatedRectOnCanvas(f.ed.project(), *f.ed.project().findClip(t1), sec(0));
    CHECK(rect[0] < 640 && rect[0] + rect[2] > 640 && rect[2] > 100);
    // Undo removes the clip and the track it created.
    const std::size_t tracks = f.ed.project().tracks.size();
    f.ed.undo();  // content
    f.ed.undo();  // trim
    f.ed.undo();  // t2
    f.ed.undo();  // s1 + its track
    CHECK_EQ(f.ed.project().tracks.size(), tracks - 1);
    CHECK(!f.ed.project().findClip(s1));
}

// ------------------------------------------------------------------ recent projects

#include "vstudio/recents.hpp"

TEST("recents: cards, posters, unfinished until exported, recent list order") {
    const auto root = fs::temp_directory_path() / ("vstudio_recent_" + newId());
    fs::create_directories(root);
    const std::string a = (root / "Phim A.vstudio").string(), b = (root / "Phim B.vstudio").string();
    Fixture f;
    f.ed.insertClip(f.v1, f.asset, sec(0), sec(0), sec(4));
    saveProject(f.ed.project(), a);
    VideoFrame frame;
    frame.allocate(1280, 720);
    for (std::size_t i = 0; i < frame.rgba.size(); i += 4) {
        frame.rgba[i] = 200;
        frame.rgba[i + 3] = 255;
    }
    recents::writeCard(a, f.ed.project(), &frame);
    auto ca = recents::readCard(a);
    CHECK(ca.has_value());
    CHECK_EQ(ca->name, std::string("Test"));
    CHECK_EQ(ca->clips, 1);
    CHECK_NEAR(ca->duration, 4.0, 1e-6);
    CHECK_EQ(ca->width, 1280);
    CHECK(!ca->exported);
    CHECK(ca->hasPoster);
    VideoFrame poster;
    CHECK(recents::readPoster(a, poster));
    CHECK_EQ(poster.width, 256);  // long side 256, aspect kept
    CHECK_EQ(poster.height, 144);
    CHECK_EQ(int(poster.rgba[0]), 200);
    // Exported: no longer unfinished, until the content changes again.
    recents::markExported(a, f.ed.project());
    CHECK(recents::readCard(a)->exported);
    saveProject(f.ed.project(), a);  // saving the same content (new timestamps) keeps it exported
    recents::writeCard(a, f.ed.project());
    CHECK(recents::readCard(a)->exported);
    CHECK(recents::readCard(a)->hasPoster);  // no frame given: the old poster stays
    f.ed.insertClip(f.v1, f.asset, sec(4), sec(0), sec(2));
    saveProject(f.ed.project(), a);
    recents::writeCard(a, f.ed.project());
    CHECK(!recents::readCard(a)->exported);
    CHECK_NEAR(recents::readCard(a)->duration, 6.0, 1e-6);
    // A package saved before cards existed is still listed (read from project.json), without a poster.
    saveProject(Editor::newProject("Cũ", 1080, 1920, {30, 1}, "test"), b);
    auto cb = recents::readCard(b);
    CHECK(cb.has_value());
    CHECK_EQ(cb->name, std::string("Cũ"));
    CHECK(!cb->hasPoster && !cb->exported);
    CHECK(!recents::readPoster(b, poster));
    CHECK(!recents::readCard((root / "nothing.vstudio").string()).has_value());
    // Scanning a folder finds both; the open project and exported ones can be left out.
    CHECK_EQ(recents::scanFolder(root.string()).size(), std::size_t(2));
    CHECK_EQ(recents::startCards("", root.string(), true, 10, b).size(), std::size_t(1));
    recents::markExported(a, f.ed.project());
    CHECK_EQ(recents::startCards("", root.string(), true, 10).size(), std::size_t(1));
    CHECK_EQ(recents::startCards("", root.string(), false, 10).size(), std::size_t(2));
    // The recent list: newest first, no duplicates, missing packages drop out.
    const std::string list = (root / "recent.json").string();
    recents::RecentList rl(list);
    rl.touch(a);
    rl.touch(b);
    rl.touch(a);
    CHECK_EQ(rl.paths().size(), std::size_t(2));
    CHECK_EQ(rl.paths()[0], a);
    CHECK_EQ(recents::startCards(list, root.string(), false, 10).size(), std::size_t(2));  // de-duplicated
    fs::remove_all(b);
    CHECK_EQ(rl.paths().size(), std::size_t(1));
    rl.remove(a);
    CHECK(rl.paths().empty());
    for (int i = 0; i < 40; ++i) rl.touch(a);
    CHECK(rl.paths().size() <= recents::RecentList::kMax);
    fs::remove_all(root);
}

TEST("editor: live preview of a gesture leaves the project and the undo history alone") {
    Fixture f;
    const Id clip = f.ed.addGeneratedClip(stickers::defaultStickerContent("heart_red"), sec(0), sec(3));
    const auto rev = f.ed.revision();
    const bool couldUndo = f.ed.canUndo();
    auto live = f.ed.preview([&](Editor& e) {
        e.updateClipContent(clip, {{"size", 640.0}});
        e.setPropertyAt(clip, sec(0), {{kf::kScale, 2.0}});
    });
    CHECK(live != nullptr);
    CHECK_NEAR(live->findClip(clip)->content.value("size", 0.0), 640.0, 1e-9);
    CHECK_NEAR(animatedTransform(*live->findClip(clip), sec(0)).scale, 2.0, 1e-9);
    // The project, its revision and its history did not move.
    CHECK_NEAR(f.ed.project().findClip(clip)->content.value("size", 0.0), 320.0, 1e-9);
    CHECK_EQ(f.ed.revision(), rev);
    CHECK_EQ(f.ed.canUndo(), couldUndo);
    // A refused edit gives no preview.
    CHECK(f.ed.preview([&](Editor& e) { e.updateClipContent("missing", {{"size", 1.0}}); }) == nullptr);
    // The gesture's end: one undo step.
    f.ed.updateClipContent(clip, {{"size", 640.0}});
    CHECK(f.ed.undo());
    CHECK_NEAR(f.ed.project().findClip(clip)->content.value("size", 0.0), 320.0, 1e-9);
}

// ------------------------------------------------------------------ video effects (zoom, rotate, reflect)

#include "vstudio/video_fx.hpp"

TEST("video effects: every one moves the picture smoothly, repeats exactly, reflections keep the top") {
    using namespace videofx;
    const auto& cat = videoFxCatalog();
    CHECK(cat.size() >= 20);
    std::set<std::string> groups;
    for (const auto& fx : cat) groups.insert(fx.group);
    CHECK_EQ(groups.size(), std::size_t(3));
    VideoFrame base;
    base.allocate(160, 90);
    drawVideoFxIcon("zoom_in", 0, base);  // the landscape, untouched at t = 0
    auto diff = [](const VideoFrame& a, const VideoFrame& b) {
        double d = 0;
        for (std::size_t i = 0; i < a.rgba.size(); ++i) d += std::abs(int(a.rgba[i]) - int(b.rgba[i]));
        return d / a.rgba.size();
    };
    for (const auto& fx : cat) {
        VideoFrame f = base;
        applyVideoFx(f, Json{{"fx", fx.id}}, 2.3, 4.0);
        double moved = 0;  // over a few moments (a beat is brief)
        for (double t : {0.4, 1.3, 2.3}) {
            VideoFrame g = base;
            applyVideoFx(g, Json{{"fx", fx.id}}, t, 4.0);
            moved = std::max(moved, diff(g, base));
        }
        if (moved < 0.5) std::printf("  %s does not change the picture\n", fx.id.c_str());
        CHECK(moved >= 0.5);
        VideoFrame again = base;
        applyVideoFx(again, Json{{"fx", fx.id}}, 2.3, 4.0);
        CHECK(again.rgba == f.rgba);  // a pure function of time
        if (!fx.animated) continue;
        // No jumps: a frame later the picture moved only a little.
        VideoFrame next = base;
        applyVideoFx(next, Json{{"fx", fx.id}}, 2.3 + 1.0 / 30, 4.0);
        if (diff(next, f) > 12) std::printf("  %s jumps (%.1f)\n", fx.id.c_str(), diff(next, f));
        CHECK(diff(next, f) <= 12);
    }
    // Slow zooms start (zoom in) or end (zoom out) on the untouched picture.
    VideoFrame z = base;
    applyVideoFx(z, Json{{"fx", "zoom_in"}}, 0, 4);
    CHECK(diff(z, base) < 0.01);
    z = base;
    applyVideoFx(z, Json{{"fx", "zoom_out"}}, 4, 4);
    CHECK(diff(z, base) < 0.01);
    // Amount 0 leaves it alone.
    z = base;
    applyVideoFx(z, Json{{"fx", "zoom_pulse"}, {"amount", 0}}, 0.4, 4);
    CHECK(diff(z, base) < 0.01);
    // A 90° turn of a wide picture keeps its shape: transparent bars at the sides.
    z = base;
    applyVideoFx(z, Json{{"fx", "rot_90"}}, 0, 4);
    CHECK_EQ(int(z.rgba[(45 * 160 + 2) * 4 + 3]), 0);
    CHECK_EQ(int(z.rgba[(45 * 160 + 80) * 4 + 3]), 255);
    // Water: the picture above the line is untouched, the water below changes over time.
    VideoFrame w1 = base, w2 = base;
    applyVideoFx(w1, Json{{"fx", "reflect_water"}, {"line", 0.6}}, 1.0, 4);
    applyVideoFx(w2, Json{{"fx", "reflect_water"}, {"line", 0.6}}, 1.5, 4);
    CHECK(std::equal(w1.rgba.begin(), w1.rgba.begin() + 50 * 160 * 4, base.rgba.begin()));
    CHECK(!std::equal(w1.rgba.begin() + 60 * 160 * 4, w1.rgba.end(), w2.rgba.begin() + 60 * 160 * 4));
}

TEST("video effects on clips: one per group, after colour, fixed order, undoable, rendered") {
    Fixture f;
    const Id a = f.ed.insertClip(f.v1, f.asset, sec(0), sec(0), sec(4));
    f.ed.setClipVideoFx({a}, "reflect", "reflect_water", 1.0, 1.0, 0.7);
    f.ed.setClipLook({a}, "teal_orange");
    f.ed.setClipVideoFx({a}, "zoom", "zoom_in", 1.5, 2.0);
    auto types = [&] {
        std::vector<std::string> out;
        for (const auto& e : f.ed.project().findClip(a)->effects) out.push_back(e.value("type", std::string{}));
        return out;
    };
    // The look stays first; geometry runs zoom -> reflect whatever the order they were added in.
    CHECK((types() == std::vector<std::string>{kColorLookEffect, videofx::kZoomEffect, videofx::kReflectEffect}));
    f.ed.setClipVideoFx({a}, "zoom", "zoom_pulse");  // replaces within the group
    CHECK_EQ(types().size(), std::size_t(3));
    const Json& zoom = f.ed.project().findClip(a)->effects[1]["params"];
    CHECK_EQ(zoom.value("fx", std::string{}), std::string("zoom_pulse"));
    CHECK_NEAR(f.ed.project().findClip(a)->effects[2]["params"].value("line", 0.0), 0.7, 1e-9);
    bool refused = false;
    try {
        f.ed.setClipVideoFx({a}, "zoom", "reflect_water");  // wrong group
    } catch (const EditError&) {
        refused = true;
    }
    CHECK(refused);
    f.ed.setClipVideoFx({a}, "reflect", "");
    CHECK_EQ(types().size(), std::size_t(2));
    CHECK(f.ed.undo());
    CHECK_EQ(types().size(), std::size_t(3));
    // Rendered through the effect modules, with the clip's length passed in.
    ModuleRegistry mods;
    registerBuiltinModules(mods);
    VideoFrame frame;
    frame.allocate(64, 36);
    videofx::drawVideoFxIcon("zoom_in", 0, frame);
    const VideoFrame before = frame;
    applyClipEffects(mods, *f.ed.project().findClip(a), frame, sec(1), nullptr, {});
    CHECK(frame.rgba != before.rgba);
}
