// vstudio-cli: headless driver for the P0 flow (import -> edit -> export) and
// a quick way to check media files on every desktop OS in CI.
//
//   vstudio-cli probe <file>...
//   vstudio-cli demo <out.mp4> <media>...      join media, cut each to its middle 80 %, export
//   vstudio-cli export <package.vstudio> <out.mp4> [width height]
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "vstudio/editor.hpp"
#include "vstudio/ffmpeg/ffmpeg_module.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/render.hpp"
#if defined(VSTUDIO_HAS_METAL)
#include "vstudio/apple/metal_compositor.hpp"
#endif

using namespace vstudio;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n  vstudio-cli probe <file>...\n  vstudio-cli demo <out.mp4> <media>...\n"
                 "  vstudio-cli export <package.vstudio> <out.mp4> [width height]\n");
    return 2;
}

void printProgress(const ExportProgress& p) {
    if (p.frame % 30 == 0 || p.frame == p.totalFrames)
        std::fprintf(stderr, "\r  rendering %lld / %lld", static_cast<long long>(p.frame), static_cast<long long>(p.totalFrames));
    if (p.frame == p.totalFrames) std::fprintf(stderr, "\n");
}

int doExport(const Project& p, const std::string& baseDir, const std::string& out, IMediaBackend& b, int w, int h,
             ModuleRegistry& mods) {
    std::shared_ptr<IMediaBackend> shared(&b, [](IMediaBackend*) {});
    MediaPool pool(shared, baseDir);
    auto enc = b.createEncoder();
    ExportSettings s;
    s.width = w > 0 ? w : p.canvas.width;
    s.height = h > 0 ? h : p.canvas.height;
    s.fps = p.fps;
    auto r = exportProject(p, pool, *enc, out, s, printProgress, nullptr, &mods);
    if (!r.ok) {
        std::fprintf(stderr, "export failed: %s\n", r.error.c_str());
        return 1;
    }
    const double dur = p.duration().seconds();
    std::printf("exported %s: %lld frames, %.2f s of video in %.2f s (%.1fx realtime)\n", out.c_str(),
                static_cast<long long>(r.framesWritten), dur, r.seconds, dur / std::max(1e-9, r.seconds));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    ModuleRegistry mods;
    registerBuiltinModules(mods);
    ffmpeg::registerModule(mods);
#if defined(VSTUDIO_HAS_METAL)
    apple::registerMetalCompositor(mods);
#endif
    std::string err;
    auto backend = mods.best<IMediaBackend>("decode", &err);
    if (!backend) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const std::string cmd = argv[1];

    if (cmd == "probe") {
        for (int i = 2; i < argc; ++i) {
            auto p = backend->probe(argv[i], &err);
            if (!p) {
                std::printf("%s: ERROR %s\n", argv[i], err.c_str());
                continue;
            }
            std::printf("%s: %.3f s video=%s %dx%d @ %s%s audio=%s %d Hz %d ch%s\n", argv[i], p->duration.seconds(),
                        p->hasVideo ? p->videoCodec.c_str() : "-", p->width, p->height, p->fps.toString().c_str(),
                        p->vfr ? " (VFR)" : "", p->hasAudio ? p->audioCodec.c_str() : "-", p->sampleRate, p->channels,
                        p->isStillImage ? " [image]" : "");
        }
        return 0;
    }

    if (cmd == "demo" && argc >= 4) {
        const std::string out = argv[2];
        const auto t0 = std::chrono::steady_clock::now();
        Editor ed{Editor::newProject("demo", 1280, 720, {30, 1}, ModuleRegistry::currentPlatform())};
        const Id v = ed.addTrack(TrackType::Video, "V1");
        const Id a = ed.addTrack(TrackType::Audio, "A1");
        for (int i = 3; i < argc; ++i) {
            auto p = backend->probe(argv[i], &err);
            if (!p) {
                std::fprintf(stderr, "%s: %s\n", argv[i], err.c_str());
                return 1;
            }
            const Id asset = ed.addAsset(assetFromProbe(newId(), argv[i], *p, ""));
            const Id track = p->hasVideo ? v : a;
            const Id c = ed.appendClip(track, asset);
            const Time d = ed.project().findClip(c)->duration();
            if (d > Time::fromSeconds(1)) {
                // Cut 10 % off each end: split twice and drop the outer pieces.
                const Time start = ed.project().findClip(c)->timelineIn;
                const Id mid = ed.splitClip(c, start + d.scaled({1, 10}));
                const Id tail = ed.splitClip(mid, start + d.scaled({9, 10}));
                ed.removeClip(c);
                ed.removeClip(tail);
                const Time prevEnd = [&] {
                    Time e;
                    for (const auto& cl : ed.project().findTrack(track)->clips)
                        if (cl.id != mid) e = std::max(e, cl.timelineOut());
                    return e;
                }();
                ed.moveClip(mid, track, prevEnd);
            }
        }
        const double editMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("timeline: %.2f s, %zu clips, edits+probe took %.1f ms\n", ed.project().duration().seconds(),
                    ed.project().tracks[0].clips.size() + ed.project().tracks[1].clips.size(), editMs);
        return doExport(ed.project(), "", out, *backend, 0, 0, mods);
    }

    if (cmd == "export" && argc >= 4) {
        try {
            const Project p = loadProject(argv[2]);
            const int w = argc >= 6 ? std::atoi(argv[4]) : 0;
            const int h = argc >= 6 ? std::atoi(argv[5]) : 0;
            return doExport(p, mediaBaseDir(argv[2]), argv[3], *backend, w, h, mods);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", e.what());
            return 1;
        }
    }
    return usage();
}
