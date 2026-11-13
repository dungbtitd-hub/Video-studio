#include <chrono>
#include <atomic>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <thread>

#include "fake_media.hpp"
#include "vstudio/editor.hpp"
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
