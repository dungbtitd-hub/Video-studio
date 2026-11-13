#include <filesystem>
#include <thread>

#include "fake_media.hpp"
#include "vstudio/editor.hpp"
#include "vstudio/frame_cache.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/preview.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/render.hpp"
#include "vstudio/scheduler.hpp"
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
