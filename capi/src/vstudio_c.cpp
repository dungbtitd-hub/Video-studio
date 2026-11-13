#include "vstudio/vstudio_c.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "vstudio/editor.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/preview.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/render.hpp"
#include "vstudio/scheduler.hpp"

#if defined(VSTUDIO_HAS_FFMPEG)
#include "vstudio/ffmpeg/ffmpeg_module.hpp"
#endif
#if defined(VSTUDIO_HAS_ANDROID_MEDIA)
#include "vstudio/android/mediacodec_module.hpp"
#endif

using namespace vstudio;

struct vs_session {
    ModuleRegistry modules;
    std::shared_ptr<IMediaBackend> backend;
    std::unique_ptr<Editor> editor;
    std::string packageDir;
    std::string error;

    // Created lazily: a session that is only edited never starts threads.
    std::unique_ptr<PreviewEngine> preview;
    std::unique_ptr<JobScheduler> jobs;

    std::mutex frameMu;
    std::shared_ptr<const VideoFrame> latest;
    std::int64_t latestSerial = 0;
    int previewW = 960, previewH = 540;

    std::atomic<int> exportStatus{VS_EXPORT_IDLE};
    std::atomic<double> exportProgress{0.0};
    CancelToken exportCancel;
    std::mutex exportErrMu;
    std::string exportError;

    vs_session() {
        registerBuiltinModules(modules);
#if defined(VSTUDIO_HAS_FFMPEG)
        ffmpeg::registerModule(modules);
#endif
#if defined(VSTUDIO_HAS_ANDROID_MEDIA)
        android::registerModule(modules);  // hardware codecs; preferred over FFmpeg on Android
#endif
        // The Apple module (AVFoundation/VideoToolbox) registers here when it lands.
    }

    IMediaBackend* media() {
        if (!backend) backend = modules.best<IMediaBackend>("decode", &error);
        return backend.get();
    }

    PreviewEngine& previewEngine() {
        if (!preview) {
            media();
            PreviewConfig cfg;
            cfg.width = previewW;
            cfg.height = previewH;
            preview = std::make_unique<PreviewEngine>(backend, mediaBaseDir(packageDir), cfg, &modules);
            preview->setFrameCallback([this](std::shared_ptr<const VideoFrame> f) {
                std::lock_guard lk(frameMu);
                latest = std::move(f);
                ++latestSerial;
            });
            preview->setProject(editor->snapshot());
        }
        return *preview;
    }

    void edited() {
        if (preview) preview->setProject(editor->snapshot());
    }
};

namespace {

thread_local std::string g_error;

char* dup(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

template <class F>
int guard(vs_session* s, F&& f) {
    if (!s) return -1;
    try {
        f();
        s->error.clear();
        return 0;
    } catch (const std::exception& e) {
        s->error = e.what();
        return -1;
    }
}

template <class F>
char* guardStr(vs_session* s, F&& f) {
    if (!s) return nullptr;
    try {
        std::string r = f();
        s->error.clear();
        return dup(r);
    } catch (const std::exception& e) {
        s->error = e.what();
        return nullptr;
    }
}

std::string str(const char* c) { return c ? std::string(c) : std::string(); }

}  // namespace

extern "C" {

const char* vs_version(void) { return "0.1.0-p0"; }
int64_t vs_ticks_per_second(void) { return Time::kTicksPerSecond; }
void vs_free(void* p) { std::free(p); }

char* vs_modules_json(vs_session* s) {
    return guardStr(s, [&] {
        Json arr = Json::array();
        for (const auto& m : s->modules.list())
            arr.push_back({{"id", m.id}, {"kind", m.kind}, {"version", m.version}, {"name", m.displayName},
                           {"capabilities", m.capabilities}, {"loaded", s->modules.loaded(m.id)}});
        return arr.dump();
    });
}

vs_session* vs_session_create(const char* name, int width, int height, int fps_num, int fps_den) {
    try {
        auto s = std::make_unique<vs_session>();
        s->editor = std::make_unique<Editor>(
            Editor::newProject(str(name), width, height, {fps_num, fps_den}, ModuleRegistry::currentPlatform()));
        s->editor->markSaved();
        return s.release();
    } catch (const std::exception& e) {
        g_error = e.what();
        return nullptr;
    }
}

vs_session* vs_session_open(const char* package_dir) {
    try {
        auto s = std::make_unique<vs_session>();
        s->packageDir = str(package_dir);
        s->editor = std::make_unique<Editor>(loadProject(s->packageDir));
        s->editor->markSaved();
        return s.release();
    } catch (const std::exception& e) {
        g_error = e.what();
        return nullptr;
    }
}

void vs_session_destroy(vs_session* s) {
    if (!s) return;
    s->exportCancel.cancel();
    s->jobs.reset();     // joins export job
    s->preview.reset();  // joins preview threads
    delete s;
}

const char* vs_last_error(vs_session* s) { return s ? s->error.c_str() : g_error.c_str(); }

int vs_save(vs_session* s, const char* package_dir) {
    return guard(s, [&] {
        const std::string dir = package_dir && *package_dir ? str(package_dir) : s->packageDir;
        if (dir.empty()) throw std::runtime_error("no package folder given");
        saveProject(s->editor->project(), dir);
        if (dir != s->packageDir && s->preview) s->preview->setProjectDir(mediaBaseDir(dir));
        s->packageDir = dir;
        clearAutosave(dir);
        s->editor->markSaved();
    });
}

int vs_autosave(vs_session* s) {
    return guard(s, [&] {
        if (!s->editor->dirty() || s->packageDir.empty()) return;
        writeAutosave(s->editor->project(), s->packageDir);
    });
}

int vs_is_dirty(vs_session* s) { return s && s->editor->dirty() ? 1 : 0; }

char* vs_project_json(vs_session* s) { return guardStr(s, [&] { return toJson(s->editor->project()).dump(); }); }
int64_t vs_project_revision(vs_session* s) { return s ? static_cast<int64_t>(s->editor->revision()) : 0; }
int64_t vs_duration(vs_session* s) { return s ? s->editor->project().duration().ticks() : 0; }

char* vs_import_media(vs_session* s, const char* path) {
    return guardStr(s, [&] {
        IMediaBackend* b = s->media();
        if (!b) throw std::runtime_error("no media module available: " + s->error);
        std::string err;
        auto pr = b->probe(str(path), &err);
        if (!pr) throw std::runtime_error(err);
        const Id id = s->editor->addAsset(assetFromProbe(newId(), str(path), *pr, s->packageDir));
        s->edited();
        return id;
    });
}

char* vs_add_track(vs_session* s, int type, const char* name) {
    return guardStr(s, [&] {
        const Id id = s->editor->addTrack(type == VS_TRACK_AUDIO ? TrackType::Audio : TrackType::Video, str(name));
        s->edited();
        return id;
    });
}

char* vs_append_clip(vs_session* s, const char* track_id, const char* asset_id) {
    return guardStr(s, [&] {
        const Id id = s->editor->appendClip(str(track_id), str(asset_id));
        s->edited();
        return id;
    });
}

char* vs_insert_clip(vs_session* s, const char* track_id, const char* asset_id, int64_t tin, int64_t sin, int64_t sout) {
    return guardStr(s, [&] {
        const Id id = s->editor->insertClip(str(track_id), str(asset_id), Time::fromTicks(tin), Time::fromTicks(sin),
                                            Time::fromTicks(sout));
        s->edited();
        return id;
    });
}

int vs_move_clip(vs_session* s, const char* clip_id, const char* to_track_id, int64_t tin) {
    return guard(s, [&] {
        s->editor->moveClip(str(clip_id), str(to_track_id), Time::fromTicks(tin));
        s->edited();
    });
}

int vs_trim_clip(vs_session* s, const char* clip_id, int edge, int64_t t) {
    return guard(s, [&] {
        s->editor->trimClip(str(clip_id), edge == VS_TRIM_START ? TrimEdge::Start : TrimEdge::End, Time::fromTicks(t));
        s->edited();
    });
}

char* vs_split_clip(vs_session* s, const char* clip_id, int64_t t) {
    return guardStr(s, [&] {
        const Id id = s->editor->splitClip(str(clip_id), Time::fromTicks(t));
        s->edited();
        return id;
    });
}

int vs_split_at(vs_session* s, int64_t t) {
    int n = -1;
    if (guard(s, [&] {
            n = s->editor->splitAt(Time::fromTicks(t));
            s->edited();
        }) != 0)
        return -1;
    return n;
}

int vs_remove_clip(vs_session* s, const char* clip_id) {
    return guard(s, [&] {
        s->editor->removeClip(str(clip_id));
        s->edited();
    });
}

int vs_set_clip_volume(vs_session* s, const char* clip_id, double v) {
    return guard(s, [&] {
        s->editor->setClipVolume(str(clip_id), v);
        s->edited();
    });
}

int vs_add_clip_effect(vs_session* s, const char* clip_id, const char* module_id, const char* params_json) {
    return guard(s, [&] {
        const Json params = params_json && *params_json ? Json::parse(params_json) : Json::object();
        s->editor->addClipEffect(str(clip_id), str(module_id), params);
        s->edited();
    });
}

int vs_undo(vs_session* s) {
    if (!s || !s->editor->undo()) return 0;
    s->edited();
    return 1;
}

int vs_redo(vs_session* s) {
    if (!s || !s->editor->redo()) return 0;
    s->edited();
    return 1;
}

void vs_preview_set_size(vs_session* s, int w, int h) {
    if (!s) return;
    s->previewW = w;
    s->previewH = h;
    if (s->preview) s->preview->setPreviewSize(w, h);
}

void vs_preview_seek(vs_session* s, int64_t t) {
    if (s) s->previewEngine().requestFrame(Time::fromTicks(t));
}

void vs_play(vs_session* s, int64_t from) {
    if (s) s->previewEngine().play(Time::fromTicks(from));
}

void vs_pause(vs_session* s) {
    if (s && s->preview) s->preview->pause();
}

int vs_is_playing(vs_session* s) { return s && s->preview && s->preview->playing() ? 1 : 0; }

int64_t vs_playhead(vs_session* s) { return s && s->preview ? s->preview->playhead().ticks() : 0; }

int vs_preview_take_frame(vs_session* s, uint8_t* dst, int64_t cap, int64_t* serial, int* w, int* h, int64_t* pts) {
    if (!s) return 0;
    std::shared_ptr<const VideoFrame> f;
    std::int64_t ser;
    {
        std::lock_guard lk(s->frameMu);
        f = s->latest;
        ser = s->latestSerial;
    }
    if (!f || (serial && *serial >= ser)) return 0;
    if (w) *w = f->width;
    if (h) *h = f->height;
    if (static_cast<int64_t>(f->rgba.size()) > cap || !dst) return -1;
    std::memcpy(dst, f->rgba.data(), f->rgba.size());
    if (serial) *serial = ser;
    if (pts) *pts = f->pts.ticks();
    return 1;
}

void vs_pull_audio(vs_session* s, float* out, int64_t frames) {
    if (s && s->preview) s->preview->pullAudio(out, frames);
    else if (out) std::memset(out, 0, static_cast<std::size_t>(frames) * 2 * sizeof(float));
}

int vs_audio_format(vs_session* s, int* sr, int* ch) {
    if (!s) return -1;
    if (sr) *sr = s->editor->project().sampleRate;
    if (ch) *ch = s->editor->project().audioChannels;
    return 0;
}

int vs_export_start(vs_session* s, const char* out_path, int width, int height, int fps_num, int fps_den,
                    int64_t video_bitrate) {
    return guard(s, [&] {
        if (s->exportStatus == VS_EXPORT_RUNNING) throw std::runtime_error("an export is already running");
        if (!s->media()) throw std::runtime_error("no media module available");
        if (!s->jobs) s->jobs = std::make_unique<JobScheduler>(1);
        ExportSettings es;
        es.width = width > 0 ? width : s->editor->project().canvas.width;
        es.height = height > 0 ? height : s->editor->project().canvas.height;
        es.fps = fps_num > 0 && fps_den > 0 ? Rational{fps_num, fps_den} : s->editor->project().fps;
        if (video_bitrate > 0) es.videoBitrate = video_bitrate;
        s->exportCancel = CancelToken{};
        s->exportProgress = 0.0;
        s->exportStatus = VS_EXPORT_RUNNING;
        auto snap = s->editor->snapshot();  // export works on a frozen copy; editing can continue
        const std::string out = str(out_path);
        const std::string base = mediaBaseDir(s->packageDir);
        s->jobs->submit("export", JobPriority::Normal,
                        [s, snap, es, out, base](const CancelToken& tok) {
                            MediaPool pool(s->backend, base);
                            auto enc = s->backend->createEncoder();
                            auto r = exportProject(*snap, pool, *enc, out, es,
                                                   [s](const ExportProgress& p) {
                                                       s->exportProgress = static_cast<double>(p.frame) / p.totalFrames;
                                                   },
                                                   tok.flag(), &s->modules);
                            {
                                std::lock_guard lk(s->exportErrMu);
                                s->exportError = r.error;
                            }
                            s->exportStatus = r.ok ? VS_EXPORT_DONE : (r.cancelled ? VS_EXPORT_CANCELLED : VS_EXPORT_FAILED);
                        },
                        s->exportCancel);
    });
}

int vs_export_status(vs_session* s, double* progress) {
    if (!s) return VS_EXPORT_FAILED;
    if (progress) *progress = s->exportProgress.load();
    const int st = s->exportStatus.load();
    if (st == VS_EXPORT_FAILED) {
        std::lock_guard lk(s->exportErrMu);
        s->error = s->exportError;
    }
    return st;
}

void vs_export_cancel(vs_session* s) {
    if (s) s->exportCancel.cancel();
}

}  // extern "C"
