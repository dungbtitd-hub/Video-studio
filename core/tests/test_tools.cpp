// Sound (fades, EQ, noise reduction, detach, mixer, voice-over) and clip tools
// (reverse, freeze frame, chroma key, stabilisation, favourites).
#include <cmath>
#include <filesystem>
#include <numbers>

#include "fake_media.hpp"
#include "vstudio/audio_fx.hpp"
#include "vstudio/clip_tools.hpp"
#include "vstudio/color_space.hpp"
#include "vstudio/editor.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/pixel.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/render.hpp"
#include "vstudio/speed_curve.hpp"
#include "vstudio/export_presets.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/user_presets.hpp"
#include "vtest.hpp"

using namespace vstudio;

namespace {

MediaAsset synth(const std::string& id, double seconds) {
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

struct Fx {
    Editor ed{Editor::newProject("T", 1280, 720, {30, 1}, "test")};
    Id asset, v1, a1;
    Fx() {
        asset = ed.addAsset(synth("A", 10));
        v1 = ed.addTrack(TrackType::Video, "V1");
        a1 = ed.addTrack(TrackType::Audio, "A1");
    }
};

Time sec(double s) { return Time::fromSeconds(s); }

double peak(const AudioBuffer& b, std::int64_t from, std::int64_t to) {
    double m = 0;
    for (std::int64_t i = from; i < to; ++i) m = std::max(m, double(std::abs(b.samples[static_cast<std::size_t>(i * b.channels)])));
    return m;
}

AudioBuffer sine(double hz, double amp, int frames, int sr = 48000) {
    AudioBuffer b;
    b.sampleRate = sr;
    b.channels = 1;
    b.samples.resize(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) b.samples[static_cast<std::size_t>(i)] = float(amp * std::sin(2 * std::numbers::pi * hz * i / sr));
    return b;
}

double rms(const AudioBuffer& b, std::size_t from = 0) {
    double s = 0;
    for (std::size_t i = from; i < b.samples.size(); ++i) s += b.samples[i] * b.samples[i];
    return std::sqrt(s / double(b.samples.size() - from));
}

}  // namespace

TEST("sound: fades in and out smoothly, saved with the project") {
    Fx f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    f.ed.setClipAudio({c}, {{"fade_in", 1.0}, {"fade_out", 30.0}});  // capped to half the clip
    CHECK_NEAR(audiofx::clipAudio(*f.ed.project().findClip(c))["fade_out"].get<double>(), 2.0, 1e-9);
    f.ed.setClipAudio({c}, {{"fade_out", 0.5}});
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(f.ed.project(), pool);
    const AudioBuffer au = r.renderAudio(0, 48000 * 4);
    CHECK(peak(au, 0, 480) < 0.01);                  // starts silent
    CHECK(peak(au, 48000 * 2, 48000 * 3) > 0.2);     // full in the middle
    CHECK(peak(au, 48000 * 4 - 480, 48000 * 4) < 0.02);  // ends silent
    const Project back = projectFromJson(toJson(f.ed.project()));
    CHECK_NEAR(audiofx::clipAudio(*back.findClip(c))["fade_in"].get<double>(), 1.0, 1e-9);
    CHECK(f.ed.undo());
    CHECK_NEAR(audiofx::clipAudio(*f.ed.project().findClip(c))["fade_out"].get<double>(), 2.0, 1e-9);
}

TEST("sound: EQ presets shape the spectrum") {
    ModuleRegistry reg;
    registerBuiltinModules(reg);
    auto eq = reg.get<IAudioEffect>(audiofx::kEqEffect);
    CHECK(eq != nullptr);
    AudioBuffer low = sine(40, 0.2, 48000), high = sine(8000, 0.2, 48000);
    const double l0 = rms(low, 24000), h0 = rms(high, 24000);
    eq->apply(low, {{"preset", "bass"}});
    eq->apply(high, {{"preset", "bass"}});
    CHECK(rms(low, 24000) > l0 * 1.6);         // ~ +6 dB at 80 Hz
    CHECK(std::abs(rms(high, 24000) / h0 - 1) < 0.1);  // treble untouched
    AudioBuffer phone = sine(40, 0.2, 48000);
    eq->apply(phone, {{"preset", "phone"}});
    CHECK(rms(phone, 24000) < l0 * 0.4);
    CHECK(audiofx::findEqPreset("voice") != nullptr);
}

TEST("sound: noise reduction removes hiss and keeps the tone, identically in any block size") {
    ModuleRegistry reg;
    registerBuiltinModules(reg);
    auto dn = reg.get<IAudioEffect>(audiofx::kDenoiseEffect);
    // Tone + white noise; profile from noise alone.
    std::uint32_t seed = 1;
    auto noise = [&] {
        seed = seed * 1664525u + 1013904223u;
        return ((seed >> 8) / double(1 << 24) - 0.5) * 0.05;
    };
    AudioBuffer n = sine(440, 0.0, 48000);
    for (auto& s : n.samples) s = float(noise());
    const auto profile = audiofx::noiseProfile(n);
    CHECK_EQ(profile.size(), std::size_t(audiofx::kDenoiseFft / 2 + 1));
    AudioBuffer onlyNoise = sine(440, 0.0, 48000);
    for (auto& s : onlyNoise.samples) s = float(noise());
    const double before = rms(onlyNoise, 2048);
    dn->apply(onlyNoise, {{"amount", 1.0}, {"profile", profile}});
    CHECK(rms(onlyNoise, 2048) < before * 0.25);  // at least -12 dB of hiss
    AudioBuffer tone = sine(440, 0.3, 48000);
    const double t0 = rms(tone, 2048);
    dn->apply(tone, {{"amount", 1.0}, {"profile", profile}});
    CHECK(std::abs(rms(tone, 2048) / t0 - 1) < 0.1);  // the voice stays

    // Through the renderer: one big block == many small blocks.
    Fx f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(0), sec(3));
    f.ed.setClipAudio({c}, {{"denoise", {{"amount", 0.8}}}, {"eq", {{"preset", "voice"}}}});
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(f.ed.project(), pool, nullptr, false, &reg);
    const AudioBuffer whole = r.renderAudio(24000, 9600);
    std::vector<float> parts;
    for (int i = 0; i < 8; ++i) {
        const AudioBuffer b = r.renderAudio(24000 + i * 1200, 1200);
        parts.insert(parts.end(), b.samples.begin(), b.samples.end());
    }
    double diff = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) diff = std::max(diff, double(std::abs(parts[i] - whole.samples[i])));
    CHECK(diff < 1e-4);
}

TEST("sound: detach audio, mixer pan/solo, voice-over placement") {
    Fx f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    const Id a = f.ed.detachAudio(c);
    const Project& p = f.ed.project();
    CHECK(audiofx::detached(*p.findClip(c)));
    CHECK(p.trackOfClip(a)->type == TrackType::Audio);
    CHECK(p.findClip(a)->range().start == p.findClip(c)->range().start);
    CHECK_THROWS(f.ed.detachAudio(c));
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    {
        Renderer r(f.ed.project(), pool);
        CHECK(peak(r.renderAudio(48000, 4800), 0, 4800) > 0.2);  // heard once, from the audio track
    }
    // Pan hard left: right channel silent.
    f.ed.setTrackMix(f.ed.project().trackOfClip(a)->id, -1.0, false);
    {
        Renderer r(f.ed.project(), pool);
        const AudioBuffer au = r.renderAudio(48000, 4800);
        double right = 0;
        for (int i = 0; i < 4800; ++i) right = std::max(right, double(std::abs(au.samples[static_cast<std::size_t>(i * 2 + 1)])));
        CHECK(right < 1e-6 && peak(au, 0, 4800) > 0.2);
    }
    // Solo another (empty) track: everything else is silent.
    f.ed.setTrackMix(f.v1, 0.0, true);  // the video track, whose sound was detached
    {
        Renderer r(f.ed.project(), pool);
        CHECK(peak(r.renderAudio(48000, 4800), 0, 4800) < 1e-6);
    }
    // Voice-over: lands on a "Thu âm" track, a second take overlapping goes to another one.
    const Id rec = f.ed.addAsset(synth("R", 2));
    const Id v1 = f.ed.placeAudio(rec, sec(1));
    const Id v2 = f.ed.placeAudio(rec, sec(2));
    CHECK(f.ed.project().trackOfClip(v1)->name == "Thu âm");
    CHECK(f.ed.project().trackOfClip(v1)->id != f.ed.project().trackOfClip(v2)->id);
    const Id v3 = f.ed.placeAudio(rec, sec(5));
    CHECK(f.ed.project().trackOfClip(v3)->id == f.ed.project().trackOfClip(v1)->id);
}

TEST("clip tools: reverse plays the source backwards, trims and splits stay consistent") {
    Fx f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(1), sec(3));  // source 1..3 s
    f.ed.setClipReversed({c}, true);
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    FrameCache cache;
    Renderer r(f.ed.project(), pool, &cache, true);
    const std::size_t centre = (18 * 64 + 32) * 4;
    auto frameIdx = [&](double t) { return int(r.renderVideo(sec(t), 64, 36).rgba[centre]); };
    CHECK_EQ(frameIdx(0.0), 89);  // last source frame (3 s = frame 90, exclusive)
    CHECK_EQ(frameIdx(1.0), 59);
    CHECK_EQ(frameIdx(1.95), 31);
    // Played backwards, earlier frames come from the cache: few seeks.
    fake::seeks = 0;
    for (int i = 0; i < 60; ++i) (void)frameIdx(i / 30.0);
    CHECK(fake::seeks.load() <= 4);
    // Split in the middle: left half shows source 2..3 s backwards, right half 1..2 s.
    const Id right = f.ed.splitClip(c, sec(1));
    const Clip& L = *f.ed.project().findClip(c);
    const Clip& R = *f.ed.project().findClip(right);
    CHECK(L.sourceIn == sec(2) && L.sourceOut == sec(3));
    CHECK(R.sourceIn == sec(1) && R.sourceOut == sec(2) && R.reversed);
    // Trim the start of the reversed right half by 0.5 s: it now starts from source 1.5 s.
    f.ed.trimClip(right, TrimEdge::Start, sec(1.5));
    CHECK(f.ed.project().findClip(right)->sourceOut == sec(1.5));
    CHECK(f.ed.project().findClip(right)->timelineOut() == sec(2));
    // Sound is reversed too: the fake sawtooth falls instead of rising.
    Renderer ra(f.ed.project(), pool);
    const AudioBuffer au = ra.renderAudio(4800, 48);
    int falling = 0;
    for (int i = 1; i < 48; ++i) falling += au.samples[static_cast<std::size_t>(i * 2)] < au.samples[static_cast<std::size_t>((i - 1) * 2)];
    CHECK(falling > 40);
    CHECK(projectFromJson(toJson(f.ed.project())).findClip(c)->reversed);
}

TEST("clip tools: freeze frame holds one frame and pushes the rest") {
    Fx f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    const Id fr = f.ed.freezeFrame(c, sec(1), sec(2));
    const Project& p = f.ed.project();
    const Clip& still = *p.findClip(fr);
    CHECK(cliptools::isFreeze(still));
    CHECK(still.timelineIn == sec(1) && still.duration() == sec(2));
    CHECK_NEAR(p.duration().seconds(), 6.0, 1e-9);
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(p, pool);
    const std::size_t centre = (18 * 64 + 32) * 4;
    for (double t : {1.0, 1.7, 2.96}) CHECK_EQ(int(r.renderVideo(sec(t), 64, 36).rgba[centre]), 30);
    CHECK_EQ(int(r.renderVideo(sec(3.0), 64, 36).rgba[centre]), 30);  // the rest resumes at the same frame
    CHECK(peak(r.renderAudio(sec(1.5).toSample(48000), 4800), 0, 4800) < 1e-6);  // silent
    CHECK(f.ed.undo());
    CHECK_NEAR(f.ed.project().duration().seconds(), 4.0, 1e-9);
}

namespace {
// Playback speed of the fake sawtooth (period 48 samples, rising 0.25 per period) around `at`.
double soundSpeed(Renderer& r, double at) {
    const AudioBuffer b = r.renderAudio(sec(at).toSample(48000), 480);
    double rise = 0;
    int n = 0;
    for (int i = 1; i < 480; ++i) {
        const double d = b.samples[static_cast<std::size_t>(i * 2)] - b.samples[static_cast<std::size_t>((i - 1) * 2)];
        if (d > 0) {
            rise += d;
            ++n;
        }
    }
    return n ? rise / n / (0.25 / 48) : 0;
}
}  // namespace

TEST("speed: constant speed changes the length and moves the clips after it") {
    Fx f;
    const Id a = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    const Id b = f.ed.appendClip(f.v1, f.asset, sec(4), sec(6));  // starts at 4 s
    f.ed.setClipSpeed({a}, 2.0);
    const Project& p = f.ed.project();
    CHECK_NEAR(p.findClip(a)->duration().seconds(), 2.0, 1e-9);
    CHECK_NEAR(p.findClip(b)->timelineIn.seconds(), 2.0, 1e-9);  // follows the new end
    f.ed.setClipSpeed({a}, 0.5);
    CHECK_NEAR(p.findClip(a)->duration().seconds(), 8.0, 1e-9);
    CHECK_NEAR(p.findClip(b)->timelineIn.seconds(), 8.0, 1e-9);
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    Renderer r(p, pool);
    CHECK_NEAR(soundSpeed(r, 1.0), 0.5, 0.05);
    const std::size_t centre = (18 * 64 + 32) * 4;
    CHECK_EQ(int(r.renderVideo(sec(4.0), 64, 36).rgba[centre]), 60);  // 4 s at 0.5x = source 2 s
    CHECK_THROWS(f.ed.setClipSpeed({f.ed.addGeneratedClip(Json{{"kind", "text"}, {"text", "Hi"}}, sec(20), sec(2))}, 2.0));
    CHECK(f.ed.undo());  // the text
    CHECK(f.ed.undo() && f.ed.undo());
    CHECK_NEAR(p.findClip(a)->duration().seconds(), 4.0, 1e-9);
    CHECK_NEAR(p.findClip(b)->timelineIn.seconds(), 4.0, 1e-9);
}

TEST("speed: a curve ramps picture and sound; trims, splits, reverse and save keep it") {
    Fx f;
    const Id c = f.ed.appendClip(f.v1, f.asset, sec(0), sec(4));
    const Id after = f.ed.appendClip(f.v1, f.asset, sec(4), sec(5));
    // Slow (0.5x) for the first half of the source, ramping to 2x at the end.
    f.ed.setClipSpeed({c}, 1.0, {{0, 0.5}, {0.5, 0.5}, {1, 2}}, "");
    const Project& p = f.ed.project();
    const Clip& k = *p.findClip(c);
    CHECK(k.curved());
    // 2 s of source at 0.5x = 4 s; then 2 s ramping 0.5 -> 2: ln(4) / 0.75 = 1.848 s.
    const double expect = 4.0 + std::log(4.0) / 0.75;
    CHECK_NEAR(k.duration().seconds(), expect, 1e-6);
    CHECK_NEAR(p.findClip(after)->timelineIn.seconds(), expect, 1e-6);
    // toSource / toTimeline are inverse.
    for (double t : {0.0, 1.0, 3.9, 4.5, 5.5})
        CHECK_NEAR(k.toTimeline(k.toSource(sec(t))).seconds(), t, 1e-6);
    CHECK_NEAR(k.toSource(sec(2)).seconds(), 1.0, 1e-6);
    auto backend = std::make_shared<fake::Backend>();
    MediaPool pool(backend, "");
    {
        Renderer r(p, pool);
        CHECK_NEAR(soundSpeed(r, 1.0), 0.5, 0.06);
        CHECK(soundSpeed(r, expect - 0.05) > 1.7);  // near 2x at the end
        const std::size_t centre = (18 * 64 + 32) * 4;
        CHECK_EQ(int(r.renderVideo(sec(2.0), 64, 36).rgba[centre]), 30);  // source 1 s
    }
    // Saved and loaded.
    const Project back = projectFromJson(toJson(p));
    CHECK(back.findClip(c)->speedCurve == k.speedCurve);
    CHECK_NEAR(back.findClip(c)->duration().seconds(), expect, 1e-6);
    // Split: both halves continue the same curve, total length unchanged.
    const Id right = f.ed.splitClip(c, sec(4));
    const Clip& L = *p.findClip(c);
    const Clip& R = *p.findClip(right);
    CHECK_NEAR(L.duration().seconds() + R.duration().seconds(), expect, 2e-3);
    CHECK_NEAR(R.toSource(R.timelineIn).seconds(), 2.0, 2e-3);
    // Reversed: plays the source backwards with the same curve on the same pictures.
    f.ed.setClipReversed({right}, true);
    const Clip& RR = *p.findClip(right);
    CHECK_NEAR(RR.duration().seconds(), R.duration().seconds(), 1e-6);
    CHECK(RR.toSource(RR.timelineIn) > RR.toSource(RR.timelineIn + sec(0.5)));
    // Presets fit any clip.
    CHECK(speed::curvePresets().size() >= 8);
    const Id d = f.ed.appendClip(f.v1, f.asset, sec(0), sec(3));
    f.ed.setClipSpeed({d}, 1.0, speed::findCurvePreset("bullet")->points, "bullet");
    CHECK(p.findClip(d)->extra.value("speed_preset", std::string()) == "bullet");
    CHECK(p.findClip(d)->duration() > sec(1));
    // Back to normal speed.
    f.ed.setClipSpeed({d}, 1.0);
    CHECK(!p.findClip(d)->curved() && !p.findClip(d)->extra.contains("speed_preset"));
    CHECK_NEAR(p.findClip(d)->duration().seconds(), 3.0, 1e-9);
}

TEST("clip tools: chroma key makes green transparent and keeps the subject") {
    VideoFrame fr;
    fr.allocate(4, 1);
    const std::uint8_t px[4][3] = {{0, 255, 0}, {30, 200, 40}, {200, 120, 90}, {250, 250, 250}};
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) fr.rgba[static_cast<std::size_t>(i * 4 + k)] = px[i][k];
        fr.rgba[static_cast<std::size_t>(i * 4 + 3)] = 255;
    }
    cliptools::applyChromaKey(fr, cliptools::defaultChromaKey());
    CHECK_EQ(int(fr.rgba[3]), 0);
    CHECK(fr.rgba[7] < 40);
    CHECK_EQ(int(fr.rgba[11]), 255);  // skin
    CHECK_EQ(int(fr.rgba[15]), 255);  // white
    // 10-bit / Log sources stay deep through the effects: the key works there too.
    VideoFrame deep;
    deep.allocate(4, 1);
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) deep.rgba[static_cast<std::size_t>(i * 4 + k)] = px[i][k];
        deep.rgba[static_cast<std::size_t>(i * 4 + 3)] = 255;
    }
    convertFrame(deep, PixelFormat::RGBA16F);
    cliptools::applyChromaKey(deep, cliptools::defaultChromaKey());
    CHECK(halfToFloat(deep.half[3]) < 0.01f);
    CHECK(halfToFloat(deep.half[11]) > 0.99f);
    VideoFrame screen;
    screen.allocate(32, 32);
    for (std::size_t i = 0; i < screen.rgba.size(); i += 4) screen.rgba[i + 1] = 230, screen.rgba[i + 3] = 255;
    CHECK(cliptools::guessKeyColor(screen).substr(0, 3) == "#00");
}

TEST("clip tools: stabilisation measures shake and cancels it") {
    // A textured scene shaken by a known path.
    auto scene = [](double ox, double oy, int idx) {
        VideoFrame f;
        f.allocate(160, 90);
        f.pts = Time::fromFrame(idx, {30, 1});
        for (int y = 0; y < 90; ++y)
            for (int x = 0; x < 160; ++x) {
                const double u = x + ox, v = y + oy;
                const auto val = std::uint8_t(128 + 60 * std::sin(u * 0.21) * std::cos(v * 0.17) + 40 * std::sin((u + v) * 0.05));
                std::uint8_t* p = &f.rgba[(std::size_t(y) * 160 + x) * 4];
                p[0] = p[1] = p[2] = val;
                p[3] = 255;
            }
        return f;
    };
    std::vector<double> shakeX;
    for (int i = 0; i < 60; ++i) shakeX.push_back(4.0 * std::sin(i * 1.3));
    int i = 0;
    const auto path = cliptools::measureShake([&]() -> std::optional<VideoFrame> {
        if (i >= 60) return std::nullopt;
        const int k = i++;
        return scene(shakeX[std::size_t(k)], 0, k);
    });
    CHECK_EQ(path.size(), std::size_t(60));
    // Measured path follows the true shake (the picture moves left when the offset grows).
    double err = 0;
    for (std::size_t k = 1; k < path.size(); ++k)
        err = std::max(err, std::abs((path[k].x - path[0].x) * 160 + (shakeX[k] - shakeX[0])));
    CHECK(err < 1.0);
    const Json p = cliptools::stabilizeParams(path, 0.8);
    CHECK(p["zoom"].get<double>() > 1.0 && p["zoom"].get<double>() <= 1.25);
    const auto c = cliptools::stabilizeAt(p, path[5].t);
    CHECK(std::abs(c.dx) > 0.005);  // a real correction at a shaken frame
}

TEST("clip tools: favourites toggle and persist") {
    const auto dir = std::filesystem::temp_directory_path() / "vs_fav_test";
    std::filesystem::remove_all(dir);
    const std::string file = (dir / "favorites.json").string();
    {
        cliptools::Favorites fav(file);
        CHECK(fav.toggle("look", "film"));
        CHECK(fav.toggle("look", "teal"));
        CHECK(fav.toggle("transition", "fade"));
        CHECK(!fav.toggle("look", "film"));
    }
    cliptools::Favorites back(file);
    CHECK(back.contains("look", "teal") && !back.contains("look", "film"));
    CHECK(back.list("transition") == std::vector<std::string>{"fade"});
    std::filesystem::remove_all(dir);
}

TEST("presets: a saved style moves colour and effects to other clips, keeps covers; one undo step") {
    Fx f;
    const Id a = f.ed.appendClip(f.v1, f.asset, sec(0), sec(2));
    const Id b = f.ed.appendClip(f.v1, f.asset, sec(2), sec(4));
    f.ed.setClipColorSpace({a}, "slog3", 0.5);
    f.ed.setClipLook({a}, colorLooks().front().id, 0.8);
    f.ed.setClipEffect({a}, "builtin.color_grade", {{"exposure", 0.3}});
    f.ed.addCover(a, {{"style", "blur"}});
    f.ed.addCover(b, {{"style", "black"}});
    f.ed.setClipEffect({b}, kColorLookEffect, {{"look", "x"}, {"strength", 1.0}});
    const Json style = presets::captureStyle(*f.ed.project().findClip(a));
    CHECK_EQ(style["effects"].size(), std::size_t{3});  // the cover is not part of a style
    CHECK(presets::describeStyle(style).find("Log") != std::string::npos);

    const auto before = f.ed.project().findClip(b)->effects;
    f.ed.applyClipStyle({b}, style);
    const Json& fx = f.ed.project().findClip(b)->effects;
    CHECK_EQ(fx.size(), std::size_t{4});
    CHECK_EQ(fx[0]["type"].get<std::string>(), std::string(kColorSpaceEffect));  // input transform first
    int covers = 0, looks = 0;
    for (const auto& e : fx) {
        covers += e["type"] == "builtin.cover";
        looks += e["type"] == kColorLookEffect;
        CHECK(!e["effect_id"].get<std::string>().empty());
    }
    CHECK_EQ(covers, 1);
    CHECK_EQ(looks, 1);
    CHECK(f.ed.undo());
    CHECK(f.ed.project().findClip(b)->effects == before);
    CHECK(f.ed.redo());
    CHECK(f.ed.project().findClip(b)->effects == fx);
    // An empty style clears the style, not the cover.
    f.ed.applyClipStyle({b}, {{"effects", Json::array()}});
    CHECK_EQ(f.ed.project().findClip(b)->effects.size(), std::size_t{1});
    CHECK_THROWS(f.ed.applyClipStyle({b}, Json::object()));
    CHECK_THROWS(f.ed.applyClipStyle({"nope"}, style));
}

TEST("presets: the store keeps named presets per kind, newest first, across reloads") {
    const auto dir = std::filesystem::temp_directory_path() / "vs_presets_test";
    std::filesystem::remove_all(dir);
    const std::string path = (dir / "presets.json").string();
    {
        presets::UserPresetStore s(path);
        CHECK(s.list(presets::kStyle).empty());
        CHECK(s.put(presets::kStyle, "  Ấm áp  ", {{"effects", Json::array()}}));
        CHECK(s.put(presets::kStyle, "Lạnh", {{"effects", Json::array()}}));
        CHECK(s.put(presets::kExport, "Kênh của tôi", presets::toJson(presets::UserExport{"hd720", "hevc", 25, 4, 128, true})));
        std::string err;
        CHECK(!s.put("nope", "x", {}, &err));
        CHECK(!s.put(presets::kStyle, "   ", {}, &err));
        CHECK(s.put(presets::kStyle, "Ấm áp", {{"effects", Json::array()}, {"v", 2}}));  // replaces, moves to front
        CHECK(!s.rename(presets::kStyle, "Lạnh", "Ấm áp", &err));
        CHECK(s.rename(presets::kStyle, "Lạnh", "Lạnh xanh"));
    }
    presets::UserPresetStore s(path);
    const auto& styles = s.list(presets::kStyle);
    CHECK_EQ(styles.size(), std::size_t{2});
    CHECK_EQ(styles[0].name, std::string("Ấm áp"));
    CHECK_EQ(styles[0].data.value("v", 0), 2);
    CHECK_EQ(styles[1].name, std::string("Lạnh xanh"));
    CHECK(s.find(presets::kExport, "Kênh của tôi"));
    CHECK(s.remove(presets::kStyle, "Ấm áp"));
    CHECK(!s.remove(presets::kStyle, "Ấm áp"));
    CHECK_EQ(presets::UserPresetStore(path).list(presets::kStyle).size(), std::size_t{1});
    // A broken file loads empty and is replaced on the next change.
    vstudio::writeFileAtomic(path, "{not json");
    presets::UserPresetStore broken(path);
    CHECK(broken.list(presets::kExport).empty());
    CHECK(broken.put(presets::kExport, "A", presets::toJson({})));
    CHECK_EQ(presets::UserPresetStore(path).list(presets::kExport).size(), std::size_t{1});
    std::filesystem::remove_all(dir);
}

TEST("presets: user export settings start from a quality class and override what was set") {
    Editor ed{Editor::newProject("T", 1920, 1080, {30, 1}, "test")};
    const auto e = presets::userExportFromJson({{"base", "hd720"}, {"format", "hevc"}, {"fps", 29.97}, {"audio_kbps", 128}});
    CHECK(e.has_value());
    const ExportSettings s = presets::exportSettingsFor(ed.project(), *e);
    CHECK_EQ(s.width, 1280);
    CHECK_EQ(s.height, 720);
    CHECK_EQ(s.fps.num, std::int64_t{30000});
    CHECK_EQ(s.fps.den, std::int64_t{1001});
    CHECK_EQ(s.videoCodec, std::string("hevc"));
    CHECK_EQ(s.audioBitrate, std::int64_t{128000});
    CHECK(s.includeAudio);
    const ExportSettings base = exportSettingsFor(ed.project(), *findExportPreset("hd720"), "hevc");
    CHECK(std::abs(double(s.videoBitrate) - double(base.videoBitrate)) < base.videoBitrate * 0.01);  // same quality per frame
    auto fixed = *e;
    fixed.videoMbps = 12;
    fixed.audio = false;
    const ExportSettings m = presets::exportSettingsFor(ed.project(), fixed);
    CHECK_EQ(m.videoBitrate, std::int64_t{12'000'000});
    CHECK(!m.includeAudio);
    CHECK(presets::describeExport(fixed).find("12 Mbit/s") != std::string::npos);
    CHECK(presets::describeExport(fixed).find("không tiếng") != std::string::npos);
    CHECK(!presets::userExportFromJson({{"base", "nope"}}).has_value());
    CHECK(!presets::userExportFromJson({{"format", "vp9"}}).has_value());
    // Round trip.
    const auto back = presets::userExportFromJson(presets::toJson(fixed));
    CHECK(back && back->videoMbps == 12 && !back->audio && back->format == "hevc");
}
