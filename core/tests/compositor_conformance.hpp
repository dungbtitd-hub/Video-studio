// Conformance tests every ICompositor module must pass (CPU reference, Metal,
// later D3D11/Vulkan/GLES): its output must match the CPU reference
// compositeOver() within rounding. A module test file calls
//   static vtest::CompositorSuite suite("metal", [] { return apple::createMetalCompositor(); });
// A factory that returns nullptr (no GPU on this machine) skips the suite.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "vstudio/compositor.hpp"
#include "vstudio/grade.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/pixel.hpp"
#include "vtest.hpp"

namespace vtest {

using CompositorFactory = std::function<std::shared_ptr<vstudio::ICompositor>()>;

namespace compconf {
using namespace vstudio;

inline std::shared_ptr<const VideoFrame> pattern(int w, int h, std::uint32_t seed, bool alpha) {
    auto f = std::make_shared<VideoFrame>();
    f->allocate(w, h);
    std::mt19937 rng(seed);
    // Smooth gradients plus noise: exercises filtering without being all-edges.
    const int r0 = int(rng() % 256), g0 = int(rng() % 256), b0 = int(rng() % 256);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = f->rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            p[0] = static_cast<std::uint8_t>((r0 + x * 255 / std::max(1, w - 1)) & 255);
            p[1] = static_cast<std::uint8_t>((g0 + y * 255 / std::max(1, h - 1)) & 255);
            p[2] = static_cast<std::uint8_t>((b0 + int(rng() % 32)) & 255);
            p[3] = alpha ? static_cast<std::uint8_t>(128 + (x + y) % 128) : 255;
        }
    return f;
}

inline VideoFrame reference(int w, int h, const std::vector<CompositeLayer>& layers) {
    VideoFrame out;
    out.allocate(w, h);
    createCpuCompositor()->composite(out, layers, nullptr);
    return out;
}

struct Diff {
    int maxDiff = 0;
    double badFraction = 0.0;  // pixels off by more than the tolerance
};

inline Diff compare(const VideoFrame& a, const VideoFrame& b, int tolerance) {
    Diff d;
    if (a.width != b.width || a.height != b.height) {
        d.maxDiff = 255;
        d.badFraction = 1.0;
        return d;
    }
    std::size_t bad = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        int m = 0;
        for (int k = 0; k < 4; ++k) m = std::max(m, std::abs(int(a.rgba[i + k]) - int(b.rgba[i + k])));
        d.maxDiff = std::max(d.maxDiff, m);
        if (m > tolerance) ++bad;
    }
    d.badFraction = double(bad) / double(a.rgba.size() / 4);
    return d;
}

// Interior pixels may differ by float-vs-fixed-point rounding; pixels exactly on
// a layer edge may differ by rasterisation rules. Both stay tiny.
inline void expectClose(const VideoFrame& got, const VideoFrame& ref, const char* what) {
    const Diff d = compare(got, ref, 3);
    if (d.badFraction > 0.003) {
        // Name the worst pixel so a driver-specific difference can be reproduced from the log.
        std::string worst;
        int best = -1;
        for (int y = 0; y < got.height && got.height == ref.height && got.width == ref.width; ++y)
            for (int x = 0; x < got.width; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * got.width + x) * 4;
                int m = 0;
                for (int k = 0; k < 4; ++k) m = std::max(m, std::abs(int(got.rgba[i + k]) - int(ref.rgba[i + k])));
                if (m > best) {
                    best = m;
                    worst = " worst at (" + std::to_string(x) + "," + std::to_string(y) + "): got " +
                            std::to_string(got.rgba[i]) + "," + std::to_string(got.rgba[i + 1]) + "," +
                            std::to_string(got.rgba[i + 2]) + " want " + std::to_string(ref.rgba[i]) + "," +
                            std::to_string(ref.rgba[i + 1]) + "," + std::to_string(ref.rgba[i + 2]);
                }
            }
        throw Failure{std::string(what) + ": " + std::to_string(d.badFraction * 100) + "% pixels differ (max " +
                      std::to_string(d.maxDiff) + ")" + worst};
    }
}

}  // namespace compconf

struct CompositorSuite {
    CompositorSuite(const std::string& name, CompositorFactory make) {
        using namespace compconf;
        struct State {
            std::shared_ptr<ICompositor> c;
            bool tried = false;
        };
        auto st = std::make_shared<State>();
        auto get = [name, make, st]() -> std::shared_ptr<ICompositor> {
            if (!st->tried) {
                st->tried = true;
                st->c = make();
                if (!st->c) std::printf("       (%s: no device on this machine - suite skipped)\n", name.c_str());
                else std::printf("       %s: using %s\n", name.c_str(), st->c->name().c_str());
            }
            return st->c;
        };
        add(name + ": empty timeline is opaque black", [get] {
            auto c = get();
            if (!c) return;
            VideoFrame out;
            out.allocate(64, 36);
            std::string err;
            CHECK(c->composite(out, {}, &err));
            for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
                CHECK_EQ(int(out.rgba[i]), 0);
                CHECK_EQ(int(out.rgba[i + 3]), 255);
            }
        });
        add(name + ": same-size opaque layer is copied exactly", [get] {
            auto c = get();
            if (!c) return;
            auto src = pattern(320, 180, 7, false);
            VideoFrame out;
            out.allocate(320, 180);
            CHECK(c->composite(out, {{src, {}}}, nullptr));
            CHECK(compare(out, *src, 0).maxDiff == 0);
        });
        add(name + ": scaled, offset, translucent layers match the CPU reference", [get] {
            auto c = get();
            if (!c) return;
            std::mt19937 rng(42);
            std::uniform_real_distribution<double> u(0.0, 1.0);
            for (int round = 0; round < 24; ++round) {
                const int ow = 160 + int(rng() % 400), oh = 90 + int(rng() % 300);
                std::vector<CompositeLayer> layers;
                const int n = 1 + int(rng() % 4);
                for (int i = 0; i < n; ++i) {
                    CompositeLayer l;
                    l.frame = pattern(16 + int(rng() % 600), 16 + int(rng() % 400), rng(), u(rng) < 0.5);
                    l.transform.scale = 0.25 + u(rng) * 2.0;
                    l.transform.x = (u(rng) - 0.5) * ow * 0.6;
                    l.transform.y = (u(rng) - 0.5) * oh * 0.6;
                    l.transform.opacity = u(rng) < 0.3 ? 1.0 : 0.2 + u(rng) * 0.8;
                    layers.push_back(l);
                }
                VideoFrame out;
                out.allocate(ow, oh);
                std::string err;
                CHECK(c->composite(out, layers, &err));
                expectClose(out, reference(ow, oh, layers), ("round " + std::to_string(round)).c_str());
            }
        });
        add(name + ": layers fully off-canvas or invisible draw nothing", [get] {
            auto c = get();
            if (!c) return;
            auto src = pattern(100, 100, 3, false);
            CompositeLayer off{src, {}};
            off.transform.x = 10000;
            CompositeLayer hidden{src, {}};
            hidden.transform.opacity = 0.0;
            CompositeLayer zero{src, {}};
            zero.transform.scale = 0.0;
            VideoFrame out;
            out.allocate(64, 64);
            CHECK(c->composite(out, {off, hidden, zero, {nullptr, {}}}, nullptr));
            expectClose(out, reference(64, 64, {}), "off-canvas");
        });
        add(name + ": concurrent callers get correct frames", [get] {
            auto c = get();
            if (!c) return;
            std::vector<std::thread> ts;
            std::vector<std::string> why(4, "did not finish");
            for (int t = 0; t < 4; ++t)
                ts.emplace_back([&, t] {
                    auto src = pattern(200 + t * 10, 120, 100 + t, t % 2 == 0);
                    CompositeLayer l{src, {}};
                    l.transform.scale = 0.7 + 0.1 * t;
                    l.transform.opacity = 0.8;
                    const VideoFrame ref = reference(240, 135, {l});
                    for (int i = 0; i < 20; ++i) {
                        VideoFrame out;
                        out.allocate(240, 135);
                        std::string err;
                        if (!c->composite(out, {l}, &err)) {
                            why[t] = "iteration " + std::to_string(i) + ": composite failed: " + err;
                            return;
                        }
                        const Diff d = compare(out, ref, 3);
                        if (d.badFraction > 0.003) {
                            why[t] = "iteration " + std::to_string(i) + ": " + std::to_string(d.badFraction * 100) +
                                     "% pixels differ (max " + std::to_string(d.maxDiff) + ")";
                            return;
                        }
                    }
                    why[t].clear();
                });
            for (auto& th : ts) th.join();
            for (int t = 0; t < 4; ++t)
                if (!why[t].empty()) throw Failure{"thread " + std::to_string(t) + ": " + why[t]};
        });
        add(name + ": colour grade in the compositor matches the CPU grade (8-bit and 10-bit layers)", [get] {
            auto c = get();
            if (!c || !c->supportsGrade()) return;  // compositors without it get pre-graded layers
            const Json grades[] = {
                {{"lift", {0.05, 0, -0.05, 0.1}}, {"gamma", {0, 0.1, 0, 0.2}}, {"gain", {0, 0, 0.1, -0.1}}, {"contrast", 1.2}},
                {{"saturation", 1.4}, {"curves", {{"master", {{0.25, 0.2}, {0.75, 0.85}}}, {"b", {{0.5, 0.4}}}}}},
                {{"hsl", {{"orange", {0.3, 0.4, 0.2}}, {"blue", {-0.2, -0.6, 0}}, {"green", {0, 0.5, -0.3}}}}},
                {{"qualifier", {{"hue", {200, 80}}, {"sat", {0.2, 1}}, {"softness", 0.2}, {"saturation", -0.7}, {"exposure", 0.4}}}},
                {{"qualifier", {{"hue", {30, 60}}, {"invert", true}, {"show_mask", true}}}},
            };
            for (int deep = 0; deep < 2; ++deep)
                for (const auto& g : grades) {
                    auto src = pattern(320, 180, 11, false);
                    if (deep) src = std::make_shared<const VideoFrame>(convertedFrame(*src, PixelFormat::RGBA16F));
                    CompositeLayer l{src, {}, gpuGrade(g)};
                    l.transform.x = 16;  // unscaled at an integer offset: exact texel path
                    VideoFrame out;
                    out.allocate(320, 180);
                    std::string err;
                    CHECK(c->composite(out, {l}, &err));
                    expectClose(out, reference(320, 180, {l}), (std::string(deep ? "10-bit " : "8-bit ") + g.dump()).c_str());
                }
        });
        add(name + ": one-tap looks in the compositor match the CPU look (alone, after a grade, scaled)", [get] {
            auto c = get();
            if (!c || !c->supportsGrade()) return;
            const char* looks[] = {"teal_orange", "vintage", "vivid", "bleach"};
            const Json grade = {{"saturation", 1.2}, {"contrast", 1.1}};
            for (int deep = 0; deep < 2; ++deep)
                for (const char* id : looks)
                    for (int variant = 0; variant < 3; ++variant) {
                        const ColorLook* lk = findColorLook(id);
                        CHECK(lk != nullptr);
                        Json lp = lk->params;
                        if (variant == 2) lp["strength"] = 0.6;
                        auto src = pattern(320, 180, 13, false);
                        if (deep) src = std::make_shared<const VideoFrame>(convertedFrame(*src, PixelFormat::RGBA16F));
                        CompositeLayer l{src, {}, gpuGrade(variant == 1 ? grade : Json(), lp)};
                        if (variant == 2) {
                            l.transform.scale = 0.75;  // sampled path: the vignette follows the texture
                        } else {
                            l.transform.x = 16;
                        }
                        VideoFrame out;
                        out.allocate(320, 180);
                        std::string err;
                        CHECK(c->composite(out, {l}, &err));
                        expectClose(out, reference(320, 180, {l}),
                                    (std::string(deep ? "10-bit " : "8-bit ") + id + " v" + std::to_string(variant)).c_str());
                    }
        });
        add(name + ": 1080p, 4 layers (timing)", [get, name] {
            auto c = get();
            if (!c) return;
            std::vector<CompositeLayer> layers;
            for (int i = 0; i < 4; ++i) {
                CompositeLayer l{pattern(1920, 1080, 900 + i, i > 0), {}};
                l.transform.scale = 1.0 - 0.2 * i;
                l.transform.opacity = i == 0 ? 1.0 : 0.7;
                layers.push_back(l);
            }
            VideoFrame out;
            out.allocate(1920, 1080);
            CHECK(c->composite(out, layers, nullptr));  // warm-up (pipeline, textures)
            const int iters = 20;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < iters; ++i) CHECK(c->composite(out, layers, nullptr));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / iters;
            std::printf("       %s: 1080p x4 layers %.2f ms/frame\n", name.c_str(), ms);
            expectClose(out, reference(1920, 1080, layers), "1080p");
        });
    }

private:
    static void add(const std::string& n, std::function<void()> f) {
        // Case names must outlive the registry: keep them in a static list.
        static std::vector<std::unique_ptr<std::string>> names;
        names.push_back(std::make_unique<std::string>(n));
        registry().push_back({names.back()->c_str(), std::move(f)});
    }
};

}  // namespace vtest
