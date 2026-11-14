// Pro colour grading: wheels, curves, HSL mixer, qualifier, live preview override.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>

#include "fake_media.hpp"
#include "vstudio/color_space.hpp"
#include "vstudio/editor.hpp"
#include "vstudio/grade.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/pixel.hpp"
#include "vstudio/render.hpp"
#include "vtest.hpp"

using namespace vstudio;

namespace {

VideoFrame solid(int r, int g, int b, int w = 8, int h = 8) {
    VideoFrame f;
    f.allocate(w, h);
    for (std::size_t i = 0; i < f.rgba.size(); i += 4) {
        f.rgba[i] = std::uint8_t(r);
        f.rgba[i + 1] = std::uint8_t(g);
        f.rgba[i + 2] = std::uint8_t(b);
        f.rgba[i + 3] = 255;
    }
    return f;
}

std::array<int, 3> graded(int r, int g, int b, const Json& p) {
    VideoFrame f = solid(r, g, b);
    applyColorGrade(f, p);
    return {f.rgba[0], f.rgba[1], f.rgba[2]};
}

double luma(const std::array<int, 3>& c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

VideoFrame testCard(int w, int h) {
    VideoFrame f;
    f.allocate(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* px = f.rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            px[0] = std::uint8_t(x * 255 / std::max(1, w - 1));
            px[1] = std::uint8_t(y * 255 / std::max(1, h - 1));
            px[2] = std::uint8_t((x + y) * 127 / std::max(1, w + h - 2) + 64);
            px[3] = 255;
        }
    return f;
}

MediaAsset asset(const std::string& id) {
    fake::Backend b;
    auto pr = b.probe("synthetic:10:64x36", nullptr);
    MediaAsset a;
    a.id = id;
    a.uri = "synthetic:10:64x36";
    a.type = MediaType::Video;
    a.duration = pr->duration;
    a.width = pr->width;
    a.height = pr->height;
    a.fps = pr->fps;
    a.hasVideo = true;
    return a;
}

}  // namespace

TEST("grade: neutral parameters leave pictures untouched (8-bit and deep)") {
    VideoFrame card = testCard(64, 36);
    VideoFrame a = card;
    applyColorGrade(a, Json::object());
    CHECK(a.rgba == card.rgba);
    const Json neutral{{"lift", {0, 0, 0, 0}}, {"gain", {0, 0, 0, 0}}, {"contrast", 1.0}, {"saturation", 1.0},
                       {"curves", {{"master", {{0, 0}, {1, 1}}}}}, {"hsl", {{"red", {0, 0, 0}}}}};
    applyColorGrade(a, neutral);
    CHECK(a.rgba == card.rgba);
    CHECK(isNeutralGrade(neutral));
    CHECK(!isNeutralGrade({{"saturation", 1.2}}));
    VideoFrame d = convertedFrame(card, PixelFormat::RGBA16F);
    const auto before = d.half;
    applyColorGrade(d, neutral);
    CHECK(d.half == before);
}

TEST("grade: wheels move shadows, midtones and highlights where expected") {
    const Json liftUp{{"lift", {0, 0, 0, 0.4}}};
    CHECK(graded(0, 0, 0, liftUp)[0] > 20);             // blacks lifted
    CHECK(std::abs(graded(255, 255, 255, liftUp)[0] - 255) <= 1);  // white stays
    const Json gainDown{{"gain", {0, 0, 0, -0.5}}};
    CHECK(graded(255, 255, 255, gainDown)[0] < 200);    // highlights down
    CHECK_EQ(graded(0, 0, 0, gainDown)[0], 0);          // black stays
    const Json gammaUp{{"gamma", {0, 0, 0, 0.5}}};
    CHECK(graded(110, 110, 110, gammaUp)[0] > 130);     // midtones up
    CHECK_EQ(graded(0, 0, 0, gammaUp)[0], 0);
    CHECK(std::abs(graded(255, 255, 255, gammaUp)[0] - 255) <= 1);
    CHECK(graded(0, 0, 0, {{"offset", {0, 0, 0, 0.3}}})[0] > 30);  // offset shifts everything

    // Colour balance is luma-neutral: a warm gain push on grey keeps its brightness.
    const auto warm = wheelToBalance(30, 0.6);
    const Json warmGain{{"gain", {warm[0], warm[1], warm[2], 0}}};
    const auto g = graded(128, 128, 128, warmGain);
    CHECK(g[0] > g[2] + 10);  // warmer
    CHECK(std::abs(luma(g) - 128) < 2.5);
    // Wheel <-> balance round trip (what the UIs store and read back).
    for (double ang : {0.0, 45.0, 130.0, 200.0, 315.0})
        for (double dist : {0.2, 0.7, 1.0}) {
            double a = 0, d = 0;
            balanceToWheel(wheelToBalance(ang, dist), &a, &d);
            CHECK_NEAR(d, dist, 1e-6);
            CHECK(std::abs(std::remainder(a - ang, 360.0)) < 1e-6);
        }
}

TEST("grade: contrast pivots on mid-grey; saturation keeps greys") {
    const Json more{{"contrast", 1.5}};
    CHECK(std::abs(graded(111, 111, 111, more)[0] - 111) <= 1);  // 0.435 * 255
    CHECK(graded(40, 40, 40, more)[0] < 30);
    CHECK(graded(200, 200, 200, more)[0] > 230);
    const auto mono = graded(200, 60, 30, {{"saturation", 0.0}});
    CHECK(std::abs(mono[0] - mono[1]) <= 1 && std::abs(mono[1] - mono[2]) <= 1);
    const auto grey = graded(90, 90, 90, {{"saturation", 2.0}});
    CHECK(grey[0] == 90 && grey[1] == 90 && grey[2] == 90);
}

TEST("grade: curves pass through their points without overshoot") {
    const Json s = Json::array({{0, 0}, {0.25, 0.18}, {0.75, 0.84}, {1, 1}});
    const auto c = sampleCurve(s, 1025);
    CHECK_NEAR(c[256], 0.18, 1e-3);
    CHECK_NEAR(c[768], 0.84, 1e-3);
    for (std::size_t i = 1; i < c.size(); ++i) CHECK(c[i] >= c[i - 1] - 1e-6f);  // monotone data -> monotone curve
    // Steep step between two points: no ringing past the points (Fritsch-Carlson).
    const auto step = sampleCurve(Json::array({{0, 0}, {0.45, 0.05}, {0.55, 0.95}, {1, 1}}), 1001);
    for (float v : step) CHECK(v >= 0.0f && v <= 1.0f);
    for (int i = 0; i <= 450; ++i) CHECK(step[i] <= 0.0501f);
    // Missing ends default to the diagonal ends; a single point lifts the midtones.
    const auto mid = sampleCurve(Json::array({{0.5, 0.7}}), 101);
    CHECK_NEAR(mid[0], 0.0, 1e-6);
    CHECK_NEAR(mid[100], 1.0, 1e-6);
    CHECK_NEAR(mid[50], 0.7, 1e-3);
    // Per-channel curves: lowering blue's curve cools nothing but blue.
    const auto out = graded(128, 128, 128, {{"curves", {{"b", {{0.5, 0.35}}}}}});
    CHECK(std::abs(out[0] - 128) <= 1 && std::abs(out[1] - 128) <= 1 && out[2] < 100);
    // Master curve applies to every channel.
    const auto bright = graded(128, 128, 128, {{"curves", {{"master", {{0.5, 0.7}}}}}});
    CHECK(bright[0] > 170 && std::abs(bright[0] - bright[2]) <= 1);
}

TEST("grade: HSL mixer changes one colour band and leaves greys alone") {
    CHECK_EQ(hslBands().size(), std::size_t(8));
    const Json noBlue{{"hsl", {{"blue", {0, -1, 0}}}}};
    const auto blue = graded(40, 80, 220, noBlue);
    CHECK(std::abs(blue[0] - blue[2]) < 25);  // blue nearly grey now
    const auto red = graded(220, 40, 40, noBlue);
    CHECK(red[0] == 220 && red[1] == 40 && red[2] == 40);  // red untouched
    const auto grey = graded(128, 128, 128, noBlue);
    CHECK(grey[0] == 128 && grey[1] == 128 && grey[2] == 128);
    // Hue shift: oranges towards yellow (skin warmth) moves the hue up.
    const auto o = graded(230, 120, 30, {{"hsl", {{"orange", {1, 0, 0}}}}});
    CHECK(o[1] > 150);
    // Luminance: darker greens, other colours kept.
    const auto green = graded(40, 200, 40, {{"hsl", {{"green", {0, 0, -1}}}}});
    CHECK(green[1] < 150);
    // Bands blend: a hue half-way between two bands gets part of each.
    const auto between = graded(230, 172, 30, {{"hsl", {{"orange", {0, -1, 0}}}}});  // ~45 deg
    CHECK(between[0] - between[2] < 200 && between[0] - between[2] > 20);
}

TEST("grade: qualifier selects a colour range, softly, and can be inverted or shown") {
    // Select blue sky (hue ~220) and desaturate it; the red subject is untouched.
    Json q{{"enabled", true}, {"hue", {220, 50}}, {"sat", {0.2, 1}}, {"lum", {0, 1}}, {"softness", 0.1}, {"saturation", -1}};
    const auto sky = graded(60, 120, 230, {{"qualifier", q}});
    CHECK(std::abs(sky[0] - sky[2]) < 10);
    const auto subject = graded(220, 50, 40, {{"qualifier", q}});
    CHECK(subject[0] == 220 && subject[1] == 50 && subject[2] == 40);
    const auto grey = graded(128, 128, 128, {{"qualifier", q}});  // below the saturation range
    CHECK(grey[0] == 128);
    // Mask view: white where selected, black elsewhere.
    q["show_mask"] = true;
    CHECK(graded(60, 120, 230, {{"qualifier", q}})[0] > 250);
    CHECK(graded(220, 50, 40, {{"qualifier", q}})[0] < 5);
    q["invert"] = true;
    CHECK(graded(60, 120, 230, {{"qualifier", q}})[0] < 5);
    CHECK(graded(220, 50, 40, {{"qualifier", q}})[0] > 250);
    // Soft edge: a hue just outside the range is partly selected.
    q["invert"] = false;
    q["softness"] = 0.4;
    const int edge = graded(60, 200, 230, {{"qualifier", q}})[0];  // hue ~190, 5 deg outside
    CHECK(edge > 20 && edge < 250);
    // Exposure inside the selection only.
    Json qe{{"hue", {0, 40}}, {"sat", {0.3, 1}}, {"exposure", 0.8}};
    CHECK(graded(180, 40, 40, {{"qualifier", qe}})[0] > 220);
    CHECK_EQ(graded(40, 40, 180, {{"qualifier", qe}})[2], 180);
}

TEST("grade: 8-bit and deep frames agree; deep keeps fine gradients") {
    const Json p{{"lift", {0.05, 0, -0.05, 0.1}}, {"gamma", {0, 0.1, 0, 0.2}}, {"gain", {0, 0, 0.1, -0.1}},
                 {"contrast", 1.2}, {"saturation", 1.3}, {"curves", {{"r", {{0.5, 0.55}}}}},
                 {"hsl", {{"orange", {0.2, 0.3, 0.1}}, {"blue", {0, -0.4, 0}}}},
                 {"qualifier", {{"hue", {120, 60}}, {"saturation", 0.5}}}};
    VideoFrame card = testCard(96, 54);
    VideoFrame a = card, d = convertedFrame(card, PixelFormat::RGBA16F);
    applyColorGrade(a, p);
    applyColorGrade(d, p);
    const VideoFrame d8 = convertedFrame(d, PixelFormat::RGBA8);
    int maxDiff = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i) maxDiff = std::max(maxDiff, std::abs(a.rgba[i] - d8.rgba[i]));
    CHECK(maxDiff <= 3);
    // A dark 10-bit ramp pushed hard keeps hundreds of distinct levels on the deep path.
    VideoFrame ramp;
    ramp.allocate(1024, 1, PixelFormat::RGBA16F);
    for (int x = 0; x < 1024; ++x)
        for (int c = 0; c < 3; ++c) ramp.half[x * 4 + c] = floatToHalf(x / 1023.0f * 0.25f);
    applyColorGrade(ramp, {{"gain", {0, 0, 0, 1}}, {"gamma", {0, 0, 0, 0.6}}});
    std::set<std::uint16_t> levels;
    for (int x = 0; x < 1024; ++x) levels.insert(ramp.half[x * 4]);
    CHECK(levels.size() > 600);
}

TEST("grade: editor keeps one grade per clip, in the right place, undoable") {
    Editor ed{Editor::newProject("G", 1280, 720, {30, 1}, "test")};
    const Id a = ed.addAsset(asset("A"));
    const Id v = ed.addTrack(TrackType::Video, "V1");
    const Id c = ed.insertClip(v, a, Time::fromSeconds(0), Time::fromSeconds(0), Time::fromSeconds(2));
    auto types = [&] {
        std::vector<std::string> t;
        for (const auto& e : ed.project().findClip(c)->effects) t.push_back(e.value("type", std::string{}));
        return t;
    };
    ed.setClipLook({c}, "vivid");
    ed.setClipColorSpace({c}, "slog3");
    ed.setClipGrade({c}, {{"saturation", 1.2}});
    // Log conversion, then the grade, then the look.
    CHECK((types() == std::vector<std::string>{kColorSpaceEffect, kColorGradeEffect, kColorLookEffect}));
    const std::string id = ed.project().findClip(c)->effects[1].value("effect_id", std::string{});
    ed.setClipGrade({c}, {{"saturation", 0.8}, {"qualifier", {{"hue", {20, 30}}, {"show_mask", true}}}});
    CHECK((types() == std::vector<std::string>{kColorSpaceEffect, kColorGradeEffect, kColorLookEffect}));
    const Json& fx = ed.project().findClip(c)->effects[1];
    CHECK_EQ(fx.value("effect_id", std::string{}), id);  // same effect, same place
    CHECK_NEAR(fx["params"].value("saturation", 0.0), 0.8, 1e-9);
    CHECK(!fx["params"]["qualifier"].contains("show_mask"));  // preview helper never saved
    CHECK(ed.undo());
    CHECK_NEAR(ed.project().findClip(c)->effects[1]["params"].value("saturation", 0.0), 1.2, 1e-9);
    ed.setClipGrade({c}, {{"saturation", 1.0}});  // neutral = reset
    CHECK((types() == std::vector<std::string>{kColorSpaceEffect, kColorLookEffect}));
    CHECK(ed.undo());
    CHECK_EQ(types().size(), std::size_t(3));
    CHECK_THROWS(ed.setClipGrade({"nope"}, {{"saturation", 1.2}}));
    CHECK_THROWS(ed.setClipGrade({c}, Json::array()));
}

TEST("grade: live preview override shows a change without touching the project") {
    Editor ed{Editor::newProject("G", 128, 72, {30, 1}, "test")};
    const Id a = ed.addAsset(asset("A"));
    const Id v = ed.addTrack(TrackType::Video, "V1");
    const Id c = ed.insertClip(v, a, Time::fromSeconds(0), Time::fromSeconds(0), Time::fromSeconds(2));
    ModuleRegistry mods;
    registerBuiltinModules(mods);
    MediaPool pool(std::make_shared<fake::Backend>(), "");
    const std::size_t centre = (36 * 128 + 64) * 4;
    const auto base = Renderer(ed.project(), pool, nullptr, true, &mods).renderVideo(Time::fromSeconds(1), 128, 72);
    Renderer live(ed.project(), pool, nullptr, true, &mods);
    live.setEffectOverride(std::make_shared<EffectOverride>(EffectOverride{c, kColorGradeEffect, {{"offset", {0, 0, 0, 0.5}}}}));
    const auto lifted = live.renderVideo(Time::fromSeconds(1), 128, 72);
    CHECK(lifted.rgba[centre + 1] > base.rgba[centre + 1] + 40);
    CHECK(ed.project().findClip(c)->effects.empty());
    // Another clip's override leaves this clip alone.
    Renderer other(ed.project(), pool, nullptr, true, &mods);
    other.setEffectOverride(std::make_shared<EffectOverride>(EffectOverride{"x", kColorGradeEffect, {{"saturation", 0}}}));
    CHECK(other.renderVideo(Time::fromSeconds(1), 128, 72).rgba == base.rgba);
}

TEST("grade: full grade is fast enough for preview and 4K export") {
    const Json p{{"lift", {0.05, 0, -0.05, 0.1}}, {"gamma", {0, 0.1, 0, 0.2}}, {"contrast", 1.2}, {"saturation", 1.3},
                 {"curves", {{"master", {{0.25, 0.2}, {0.75, 0.8}}}}}, {"hsl", {{"orange", {0.2, 0.3, 0.1}}}},
                 {"qualifier", {{"hue", {120, 60}}, {"saturation", 0.5}}}};
    // Steady-state cost (what playback sees): best of a few frames, so the first call's
    // cold caches and thread start-up on slow emulators do not count.
    const VideoFrame card = testCard(960, 540);
    double msPrev = 1e9;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 4; ++i) {
        VideoFrame prev = card;
        t0 = std::chrono::steady_clock::now();
        applyColorGrade(prev, p);
        msPrev = std::min(msPrev, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    VideoFrame uhd = convertedFrame(testCard(3840, 2160), PixelFormat::RGBA16F);
    t0 = std::chrono::steady_clock::now();
    applyColorGrade(uhd, p);
    const double msUhd = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("       full grade 960x540 8-bit: %.2f ms, 3840x2160 deep: %.1f ms\n", msPrev, msUhd);
    CHECK(msPrev < 40);
}

// ------------------------------------------------------------------ scopes

#include "vstudio/scopes.hpp"

namespace {
int brightness(const ScopeImage& s, int x, int y) {
    const std::uint8_t* p = s.rgba.data() + (static_cast<std::size_t>(y) * s.width + x) * 4;
    return p[0] + p[1] + p[2];
}
int brightestRow(const ScopeImage& s, int x0, int x1) {
    int best = -1, row = -1;
    for (int y = 0; y < s.height; ++y) {
        int sum = 0;
        for (int x = x0; x < x1; ++x) sum += brightness(s, x, y);
        if (sum > best) best = sum, row = y;
    }
    return row;
}
}  // namespace

TEST("scopes: waveform and parade put levels where they belong") {
    VideoFrame f;
    f.allocate(200, 100);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x) {
            std::uint8_t* p = f.rgba.data() + (static_cast<std::size_t>(y) * 200 + x) * 4;
            const std::uint8_t v = x < 100 ? 0 : 255;  // left black, right white
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    const auto wf = renderScope(f, ScopeKind::Waveform, 256, 128);
    CHECK_EQ(wf.width, 256);
    CHECK(brightestRow(wf, 10, 118) >= 125);   // black half at the bottom
    CHECK(brightestRow(wf, 138, 246) <= 2);    // white half at the top
    CHECK(brightness(wf, 64, 64) < 100);       // nothing in the middle (only the grid line)
    // Parade of pure red: red lane at the top, green and blue lanes at the bottom.
    VideoFrame red = solid(255, 0, 0, 64, 36);
    const auto pa = renderScope(red, ScopeKind::Parade, 300, 100);
    CHECK(brightestRow(pa, 5, 95) <= 2);
    CHECK(brightestRow(pa, 105, 195) >= 97);
    CHECK(brightestRow(pa, 205, 295) >= 97);
}

TEST("scopes: vectorscope centres greys and places colours by hue") {
    const auto grey = renderScope(solid(128, 128, 128, 64, 36), ScopeKind::Vectorscope, 200, 200);
    CHECK(brightness(grey, 100, 100) > 300);  // a dot in the centre
    auto spot = [](const ScopeImage& s) {      // brightest pixel
        int bx = 0, by = 0, best = -1;
        for (int y = 0; y < s.height; ++y)
            for (int x = 0; x < s.width; ++x)
                if (brightness(s, x, y) > best) best = brightness(s, x, y), bx = x, by = y;
        return std::pair<int, int>{bx, by};
    };
    const auto red = spot(renderScope(solid(200, 30, 30, 64, 36), ScopeKind::Vectorscope, 200, 200));
    CHECK(red.second < 80);   // red is up (Cr > 0) ...
    CHECK(red.first < 100);   // ... and slightly left (Cb < 0)
    const auto blue = spot(renderScope(solid(30, 30, 200, 64, 36), ScopeKind::Vectorscope, 200, 200));
    CHECK(blue.first > 140);  // blue is right (Cb > 0)
}

TEST("scopes: histogram counts every sample; scopes stay cheap") {
    const auto h = computeHistogram(solid(128, 64, 200, 100, 50));
    CHECK_EQ(h.r[128], std::uint32_t(5000));
    CHECK_EQ(h.g[64], std::uint32_t(5000));
    CHECK_EQ(h.b[200], std::uint32_t(5000));
    const auto img = renderScope(solid(128, 64, 200, 100, 50), ScopeKind::Histogram, 256, 100);
    CHECK(brightness(img, 128, 98) > brightness(img, 10, 98));
    // A 4K deep frame samples ~150k pixels: a few milliseconds per scope.
    VideoFrame uhd = convertedFrame(testCard(3840, 2160), PixelFormat::RGBA16F);
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 4; ++k) (void)renderScope(uhd, static_cast<ScopeKind>(k), 320, 180);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 4;
    std::printf("       scope on a 4K deep frame: %.2f ms\n", ms);
    CHECK(ms < 30);
}
