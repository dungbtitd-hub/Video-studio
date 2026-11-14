// Tests every camera RAW module ("media.still": LibRaw, Apple's RAW engine) must pass.
// A module test file calls
//   static vtest::StillSuite suite("libraw", [] { return libraw::createDecoder(); });
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>

#include "raw_sample.hpp"
#include "vstudio/pixel.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/stills.hpp"
#include "vtest.hpp"

namespace vtest {

using StillFactory = std::function<std::shared_ptr<vstudio::IStillDecoder>()>;

namespace stills {
using namespace vstudio;
namespace fs = std::filesystem;

inline std::string sample(const std::string& name, int w, int h, int orientation = 1) {
    static const fs::path dir = [] {
        auto p = fs::temp_directory_path() / ("vstudio_raw_" + newId());
        fs::create_directories(p);
        return p;
    }();
    const std::string path = (dir / name).string();
    if (!fs::exists(path)) writeTestDng(path, w, h, orientation);
    return path;
}

inline float channel(const VideoFrame& f, int x, int y, int c) {
    const std::size_t i = (static_cast<std::size_t>(y) * f.width + x) * 4 + c;
    return f.deep() ? halfToFloat(f.half[i]) : f.rgba[i] / 255.0f;
}

// A media backend that knows nothing about RAW (stands in for FFmpeg / AVFoundation / MediaCodec).
class NoRawBackend final : public IMediaBackend {
public:
    std::string name() const override { return "video-only"; }
    std::optional<ProbeResult> probe(const std::string&, std::string* error) override {
        ++probes;
        if (error) *error = "not a video";
        return std::nullopt;
    }
    std::unique_ptr<IMediaDecoder> createDecoder() override { return nullptr; }
    std::unique_ptr<IMediaEncoder> createEncoder() override { return nullptr; }
    int probes = 0;
};
}  // namespace stills

class StillSuite {
public:
    StillSuite(const std::string& prefix, StillFactory make) {
        auto add = [&](const char* what, void (*fn)(const StillFactory&)) {
            names().push_back(prefix + ": " + what);
            registry().push_back({names().back().c_str(), [make, fn] { fn(make); }});
        };
        add("RAW probe: size, still, more than 8 bits", probe);
        add("RAW develops to 16-bit: a grey ramp keeps far more than 256 levels", deep);
        add("RAW preview size: fits the box, developed fast", preview);
        add("RAW orientation: portrait shots come out upright", orientation);
        add("RAW through the media backend (routing, MediaPool, import)", routed);
    }

private:
    static std::vector<std::string>& names() {
        static std::vector<std::string> n;
        return n;
    }

    static void probe(const StillFactory& make) {
        using namespace stills;
        auto d = make();
        std::string err;
        auto pr = d->probe(sample("ramp.dng", 1024, 64), &err);
        if (!pr) std::printf("       %s\n", err.c_str());
        CHECK(pr.has_value());
        CHECK(pr->isStillImage && pr->hasVideo && !pr->hasAudio);
        CHECK_EQ(pr->width, 1024);
        CHECK_EQ(pr->height, 64);
        CHECK(pr->bitDepth > 8);
        CHECK(!d->probe(sample("ramp.dng", 1024, 64) + ".missing", &err).has_value());
        CHECK(!err.empty());
    }

    static void deep(const StillFactory& make) {
        using namespace stills;
        auto d = make();
        std::string err;
        const std::string path = sample("ramp.dng", 1024, 64);
        auto f = d->decode(path, 0, 0, true, &err);
        if (!f) std::printf("       %s\n", err.c_str());
        CHECK(f.has_value() && f->deep());
        CHECK_EQ(f->width, 1024);
        CHECK_EQ(f->height, 64);
        std::set<int> levels;
        const int y = f->height / 2;
        for (int x = 0; x < f->width; ++x) levels.insert(static_cast<int>(std::lround(channel(*f, x, y, 1) * 4095)));
        std::printf("       %zu levels (12-bit scale) in the developed ramp\n", levels.size());
        CHECK(levels.size() > 400u);
        // Brighter to the right, neutral grey (camera matrix and white balance applied).
        CHECK(channel(*f, f->width - 8, y, 1) > channel(*f, 8, y, 1) + 0.3f);
        for (int x : {128, 512, 900})
            for (int c : {0, 2}) CHECK_NEAR(channel(*f, x, y, c), channel(*f, x, y, 1), 0.06);
        auto f8 = d->decode(path, 0, 0, false, &err);
        CHECK(f8.has_value() && !f8->deep());
        std::set<int> l8;
        for (int x = 0; x < f8->width; ++x) l8.insert(f8->rgba[(static_cast<std::size_t>(y) * f8->width + x) * 4 + 1]);
        CHECK(l8.size() <= 256u);
        CHECK_NEAR(channel(*f8, 512, y, 1), channel(*f, 512, y, 1), 0.02);
    }

    static void preview(const StillFactory& make) {
        using namespace stills;
        auto d = make();
        std::string err;
        auto f = d->decode(sample("ramp.dng", 1024, 64), 256, 256, true, &err);
        CHECK(f.has_value());
        CHECK_EQ(f->width, 256);
        CHECK_EQ(f->height, 16);
        CHECK(channel(*f, 250, 8, 1) > channel(*f, 5, 8, 1));
    }

    static void orientation(const StillFactory& make) {
        using namespace stills;
        auto d = make();
        std::string err;
        const std::string path = sample("portrait.dng", 512, 128, 6);  // camera held upright: 90 degrees CW
        auto pr = d->probe(path, &err);
        CHECK(pr.has_value());
        CHECK_EQ(pr->width, 128);
        CHECK_EQ(pr->height, 512);
        auto f = d->decode(path, 0, 0, true, &err);
        CHECK(f.has_value());
        CHECK_EQ(f->width, 128);
        CHECK_EQ(f->height, 512);
        // The ramp ran left -> right on the sensor; turned 90 degrees CW it runs top -> bottom.
        CHECK(channel(*f, 64, 500, 1) > channel(*f, 64, 10, 1) + 0.3f);
    }

    static void routed(const StillFactory& make) {
        using namespace stills;
        ModuleRegistry mods;
        auto still = make();
        mods.add<IStillDecoder>({.id = "test.raw", .capabilities = {"decode.raw"}}, [still] { return still; });
        auto video = std::make_shared<NoRawBackend>();
        auto b = withStillDecoders(video, mods);
        CHECK(b != video);
        CHECK(isCameraRawPath("/a/b/IMG_0001.CR3") && isCameraRawPath("x.dng") && !isCameraRawPath("clip.mp4"));
        CHECK(!isCameraRawPath("photos.dng/clip.mp4"));
        std::string err;
        const std::string path = sample("ramp.dng", 1024, 64);
        auto pr = b->probe(path, &err);
        CHECK(pr.has_value() && pr->isStillImage);
        CHECK_EQ(video->probes, 0);  // RAW never reaches the video backend
        CHECK(!b->probe("clip.mp4", &err).has_value());
        CHECK_EQ(video->probes, 1);  // everything else does
        const MediaAsset a = assetFromProbe("raw", path, *pr, "");
        CHECK(a.type == MediaType::Image);
        auto dec = b->createDecoder();
        dec->setMaxOutputSize(320, 180);
        dec->setDeepOutput(true);
        CHECK(dec->open(path, &err));
        auto f1 = dec->videoFrameAt(Time::zero());
        auto f2 = dec->videoFrameAt(Time::fromSeconds(3));
        CHECK(f1.has_value() && f2.has_value() && f1->deep());
        CHECK_EQ(f1->width, 320);
        CHECK(f1->half == f2->half);  // developed once, same picture at every time
        CHECK_EQ(dec->readAudio(Time::zero(), 480, 48000, 2).samples.size(), 960u);
        // No still module registered: the backend is returned unchanged.
        ModuleRegistry none;
        CHECK(withStillDecoders(video, none) == video);
    }
};

}  // namespace vtest
