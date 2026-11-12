// Integration tests for the FFmpeg module: encode a synthetic clip, then
// probe / decode / edit / export it through the core.
#include <cmath>
#include <filesystem>

#include "vstudio/editor.hpp"
#include "vstudio/ffmpeg/ffmpeg_module.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/proxy.hpp"
#include "vstudio/render.hpp"
#include "vtest.hpp"

using namespace vstudio;
namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;

fs::path workDir() {
    static const fs::path d = [] {
        auto p = fs::temp_directory_path() / ("vstudio_ff_" + newId());
        fs::create_directories(p);
        return p;
    }();
    return d;
}

// 3 s, 320x180 @ 30 fps; frame i is filled with red = (i * 4) % 256; 440 Hz sine.
std::string makeSource(IMediaBackend& b, const std::string& name = "src.mp4", double seconds = 3.0) {
    const std::string path = (workDir() / name).string();
    if (fs::exists(path)) return path;
    auto enc = b.createEncoder();
    ExportSettings s;
    s.width = 320;
    s.height = 180;
    s.fps = {30, 1};
    s.videoBitrate = 4'000'000;
    s.gopSize = 30;
    std::string err;
    if (!enc->open(path, s, &err)) throw std::runtime_error(err);
    const int frames = static_cast<int>(seconds * 30);
    std::int64_t sample = 0;
    for (int i = 0; i < frames; ++i) {
        VideoFrame f;
        f.allocate(320, 180);
        for (std::size_t k = 0; k < f.rgba.size(); k += 4) {
            f.rgba[k] = static_cast<std::uint8_t>((i * 4) % 256);
            f.rgba[k + 1] = 64;
            f.rgba[k + 2] = 64;
            f.rgba[k + 3] = 255;
        }
        if (!enc->writeVideo(f, &err)) throw std::runtime_error(err);
        AudioBuffer a;
        a.samples.resize(1600 * 2);
        for (int k = 0; k < 1600; ++k, ++sample) {
            const float v = 0.5f * static_cast<float>(std::sin(2 * kPi * 440.0 * sample / 48000.0));
            a.samples[k * 2] = a.samples[k * 2 + 1] = v;
        }
        if (!enc->writeAudio(a, &err)) throw std::runtime_error(err);
    }
    if (!enc->finish(&err)) throw std::runtime_error(err);
    return path;
}

int centreRed(const VideoFrame& f) { return f.rgba[(static_cast<std::size_t>(f.height / 2) * f.width + f.width / 2) * 4]; }

double rms(const AudioBuffer& a) {
    double s = 0;
    for (float v : a.samples) s += static_cast<double>(v) * v;
    return a.samples.empty() ? 0 : std::sqrt(s / a.samples.size());
}

}  // namespace

TEST("ffmpeg: module registers lazily") {
    ModuleRegistry r;
    ffmpeg::registerModule(r);
    CHECK(!r.loaded("ffmpeg"));
    auto b = r.best<IMediaBackend>("decode");
    CHECK(b != nullptr);
    CHECK(r.loaded("ffmpeg"));
    CHECK(b->name().rfind("ffmpeg", 0) == 0);
}

TEST("ffmpeg: probe") {
    auto b = ffmpeg::createBackend();
    const auto path = makeSource(*b);
    std::string err;
    auto p = b->probe(path, &err);
    CHECK(p.has_value());
    CHECK(p->hasVideo && p->hasAudio && !p->isStillImage);
    CHECK_EQ(p->width, 320);
    CHECK_EQ(p->height, 180);
    CHECK(p->fps == Rational(30, 1));
    CHECK_NEAR(p->duration.seconds(), 3.0, 0.1);
    CHECK_EQ(p->sampleRate, 48000);
    CHECK(!b->probe((workDir() / "missing.mp4").string(), &err));
    CHECK(!err.empty());
}

TEST("ffmpeg: frame-accurate decode, sequential and random access") {
    auto b = ffmpeg::createBackend();
    const auto path = makeSource(*b);
    auto d = b->createDecoder();
    std::string err;
    CHECK(d->open(path, &err));
    for (int i : {0, 1, 2, 3, 30, 31, 45}) {  // sequential
        auto f = d->videoFrameAt(Time::fromFrame(i, {30, 1}));
        CHECK(f.has_value());
        CHECK_NEAR(centreRed(*f), (i * 4) % 256, 10);
    }
    for (int i : {80, 10, 60, 5, 89}) {  // random (backward and forward seeks)
        auto f = d->videoFrameAt(Time::fromFrame(i, {30, 1}) + Time::fromMilliseconds(5));
        CHECK(f.has_value());
        CHECK_NEAR(centreRed(*f), (i * 4) % 256, 10);
    }
    // Decoding straight to preview size (no full-size RGBA).
    auto small = b->createDecoder();
    small->setMaxOutputSize(160, 160);
    CHECK(small->open(path, &err));
    auto f = small->videoFrameAt(Time::fromSeconds(1));
    CHECK_EQ(f->width, 160);
    CHECK_EQ(f->height, 90);
}

TEST("ffmpeg: audio reads are continuous and resampled") {
    auto b = ffmpeg::createBackend();
    const auto path = makeSource(*b);
    auto d = b->createDecoder();
    std::string err;
    CHECK(d->open(path, &err));
    double total = 0;
    for (int i = 0; i < 20; ++i) total += rms(d->readAudio(Time::fromSample(i * 1024, 48000) + Time::fromSeconds(0.5), 1024, 48000, 2));
    CHECK_NEAR(total / 20, 0.5 / std::sqrt(2.0), 0.03);
    auto r44 = d->readAudio(Time::fromSeconds(1), 4410, 44100, 1);
    CHECK_EQ(r44.frames(), 4410);
    // Stereo -> mono uses FFmpeg's standard downmix (L+R at -3 dB).
    CHECK(rms(r44) > 0.3 && rms(r44) < 0.52);
    // Past the end: silence, no crash.
    CHECK_NEAR(rms(d->readAudio(Time::fromSeconds(10), 1024, 48000, 2)), 0.0, 1e-6);
}

TEST("ffmpeg: import -> edit -> export MP4 (P0 flow)") {
    auto b = ffmpeg::createBackend();
    const auto src = makeSource(*b);
    const std::string pkg = (workDir() / "Demo.vstudio").string();
    Editor ed{Editor::newProject("Demo", 640, 360, {30, 1}, ModuleRegistry::currentPlatform())};
    std::string err;
    auto probe = b->probe(src, &err);
    const Id asset = ed.addAsset(assetFromProbe(newId(), src, *probe, pkg));
    CHECK_EQ(ed.project().assets[0].uri, std::string("src.mp4"));  // relative to the package folder
    const Id v = ed.addTrack(TrackType::Video, "V1");
    const Id c = ed.appendClip(v, asset);
    const Id right = ed.splitClip(c, Time::fromSeconds(1));
    ed.removeClip(c);                                            // keep 1..3 s
    ed.moveClip(right, v, Time::zero());
    ed.appendClip(v, asset, Time::zero(), Time::fromSeconds(1));  // then 0..1 s
    CHECK_EQ(ed.project().duration(), Time::fromSeconds(3));
    saveProject(ed.project(), pkg);

    const Project loaded = loadProject(pkg);
    MediaPool pool(b, mediaBaseDir(pkg));
    auto enc = b->createEncoder();
    ExportSettings s;
    s.width = 640;
    s.height = 360;
    s.fps = {30, 1};
    const std::string out = (workDir() / "out.mp4").string();
    auto res = exportProject(loaded, pool, *enc, out, s);
    CHECK(res.ok);
    CHECK_EQ(res.framesWritten, 90);
    auto op = b->probe(out, &err);
    CHECK(op.has_value());
    CHECK_EQ(op->width, 640);
    CHECK_NEAR(op->duration.seconds(), 3.0, 0.1);
    // Frame 0 of the export is source frame 30; frame 60 is source frame 0.
    auto d = b->createDecoder();
    CHECK(d->open(out, &err));
    CHECK_NEAR(centreRed(*d->videoFrameAt(Time::zero())), 120, 12);
    CHECK_NEAR(centreRed(*d->videoFrameAt(Time::fromFrame(60, {30, 1}))), 0, 12);
    CHECK_NEAR(rms(d->readAudio(Time::fromSeconds(1.5), 4800, 48000, 2)), 0.5 / std::sqrt(2.0), 0.05);
}

TEST("ffmpeg: proxy generation") {
    auto b = ffmpeg::createBackend();
    const auto src = makeSource(*b);
    std::string err;
    auto probe = b->probe(src, &err);
    const MediaAsset a = assetFromProbe("p", src, *probe, "");
    ProxyPolicy pol;
    pol.maxHeight = 90;
    pol.pixelThreshold = 0;
    CHECK(needsProxy(a, pol));
    const auto res = generateProxy(*b, src, a, (workDir() / "src.proxy.mp4").string(), pol, CancelToken{});
    CHECK(res.ok);
    auto pp = b->probe(res.path, &err);
    CHECK_EQ(pp->height, 90);
    CHECK(!pp->hasAudio);
    CHECK_NEAR(pp->duration.seconds(), 3.0, 0.15);
}
