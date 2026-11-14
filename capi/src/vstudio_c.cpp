#include "vstudio/vstudio_c.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "vstudio/color_space.hpp"
#include "vstudio/cover.hpp"
#include "vstudio/tracker.hpp"
#include "vstudio/editor.hpp"
#include "vstudio/export_presets.hpp"
#include "vstudio/keyframes.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/transfer.hpp"
#include "vstudio/transitions.hpp"
#include "vstudio/media_library.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/preview.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/render.hpp"
#include "vstudio/scheduler.hpp"

#if defined(VSTUDIO_HAS_FFMPEG)
#include "vstudio/ffmpeg/ffmpeg_module.hpp"
#endif
#if defined(VSTUDIO_HAS_ANDROID_MEDIA)
#include "vstudio/android/gles_compositor.hpp"
#include "vstudio/android/mediacodec_module.hpp"
#endif
#if defined(VSTUDIO_HAS_APPLE_MEDIA)
#include "vstudio/apple/avfoundation_module.hpp"
#include "vstudio/apple/metal_compositor.hpp"
#endif
#if defined(VSTUDIO_HAS_D3D11)
#include "vstudio/windows/d3d11_compositor.hpp"
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
    // Thumbnails / waveforms: their own background worker, so they never wait behind an export.
    std::unique_ptr<JobScheduler> libJobs;
    std::unique_ptr<MediaLibrary> library;
    std::atomic<std::int64_t> librarySerial{0};

    std::mutex frameMu;
    std::shared_ptr<const VideoFrame> latest;
    std::int64_t latestSerial = 0;
    // Native preview sink; sinkMu is held while it runs so removing it is a hard barrier.
    std::mutex sinkMu;
    vs_frame_sink sink = nullptr;
    void* sinkUser = nullptr;
    int previewW = 960, previewH = 540;

    // Cover tracking: result handed back to the editing thread by vs_track_status.
    std::atomic<int> trackStatus{0};
    std::atomic<double> trackProgress{0.0};
    CancelToken trackCancel;
    std::mutex trackMu;
    std::string trackClip, trackCover, trackError;
    std::vector<std::pair<Time, std::array<double, 4>>> trackKeys;
    bool trackLost = false;
    std::unique_ptr<JobScheduler> trackJobs;

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
        android::registerGlesCompositor(modules);  // GPU compositing; CPU stays the fallback
#endif
#if defined(VSTUDIO_HAS_APPLE_MEDIA)
        apple::registerModule(modules);  // VideoToolbox hardware codecs; preferred over FFmpeg on macOS/iOS
        apple::registerMetalCompositor(modules);  // GPU compositing; CPU stays the fallback
#endif
#if defined(VSTUDIO_HAS_D3D11)
        windows::registerD3D11Compositor(modules);  // GPU compositing; CPU stays the fallback
#endif
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
                {
                    std::lock_guard lk(sinkMu);
                    if (sink && f) sink(sinkUser, f->rgba.data(), f->width, f->height, f->width * 4, f->pts.ticks());
                }
                std::lock_guard lk(frameMu);
                latest = std::move(f);
                ++latestSerial;
            });
            preview->setProject(editor->snapshot());
        }
        return *preview;
    }

    MediaLibrary& mediaLibrary() {
        if (!library) {
            media();
            libJobs = std::make_unique<JobScheduler>(1);
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path cache = packageDir.empty() ? fs::temp_directory_path(ec) / "vstudio-media-cache"
                                                      : fs::path(packageDir) / "cache" / "media";
            library = std::make_unique<MediaLibrary>(backend, *libJobs, cache.string());
            library->setListener([this](const Id&, MediaLibrary::Kind, bool) { ++librarySerial; });
        }
        return *library;
    }

    const MediaAsset& asset(const char* id) const {
        const MediaAsset* a = editor->project().findAsset(id ? id : "");
        if (!a) throw std::runtime_error("asset not found");
        return *a;
    }

    std::string resolve(const MediaAsset& a) const {
        namespace fs = std::filesystem;
        fs::path p(a.uri);
        if (p.is_relative() && !packageDir.empty()) p = fs::path(mediaBaseDir(packageDir)) / p;
        return p.string();
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

// Kept out of the extern "C" block: MSVC (/EHsc) assumes C-linkage functions never throw.
const Clip& clipOrThrow(vs_session* s, const char* clip_id) {
    const Clip* c = s->editor->project().findClip(str(clip_id));
    if (!c) throw std::runtime_error("unknown clip");
    return *c;
}
std::map<std::string, double> numberMap(const std::string& json) {
    const Json j = Json::parse(json);
    if (!j.is_object()) throw std::runtime_error("values must be a JSON object");
    std::map<std::string, double> values;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_number()) throw std::runtime_error("not a number: " + it.key());
        values[it.key()] = it.value().get<double>();
    }
    return values;
}

kf::Group groupFrom(int g) {
    if (g != 0 && g != 1) throw std::runtime_error("group must be 0 (picture) or 1 (audio)");
    return g == 1 ? kf::Group::Audio : kf::Group::Picture;
}

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
    s->trackCancel.cancel();
    s->trackJobs.reset();  // joins a running tracking job
    s->library.reset();  // cancels thumbnail/waveform work
    s->libJobs.reset();
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

char* vs_looks_json(void) {
    try {
        Json out = Json::array();
        for (const auto& l : colorLooks()) out.push_back({{"id", l.id}, {"name", l.name}, {"category", l.category}});
        return dup(out.dump());
    } catch (...) {
        return nullptr;
    }
}

int vs_set_clip_look(vs_session* s, const char* clip_ids_json, const char* look_id, double strength) {
    return guard(s, [&] {
        const Json ids = Json::parse(str(clip_ids_json));
        if (!ids.is_array()) throw std::runtime_error("clip_ids_json must be a JSON array");
        std::vector<Id> clips;
        for (const auto& id : ids) clips.push_back(id.get<std::string>());
        s->editor->setClipLook(clips, str(look_id), strength);
        s->edited();
    });
}

namespace {
std::vector<Id> clipIdsFrom(const char* json) {
    const Json ids = Json::parse(str(json));
    if (!ids.is_array()) throw std::runtime_error("clip_ids_json must be a JSON array");
    std::vector<Id> clips;
    for (const auto& id : ids) clips.push_back(id.get<std::string>());
    return clips;
}
}  // namespace

char* vs_log_formats_json(void) {
    try {
        Json out = Json::array();
        for (const auto& f : logFormats()) out.push_back({{"id", f.id}, {"name", f.name}, {"vendor", f.vendor}});
        return dup(out.dump());
    } catch (...) {
        return nullptr;
    }
}

int vs_set_clip_color_space(vs_session* s, const char* clip_ids_json, const char* input, double exposure) {
    return guard(s, [&] {
        s->editor->setClipColorSpace(clipIdsFrom(clip_ids_json), str(input), exposure);
        s->edited();
    });
}

int vs_set_clip_lut(vs_session* s, const char* clip_ids_json, const char* path, double strength) {
    return guard(s, [&] {
        std::string p = str(path);
        if (!p.empty()) {
            // Validate now (a bad file fails here, not silently at render time).
            const std::string full =
                resolveResourcePath(p, s->packageDir.empty() ? std::string{} : mediaBaseDir(s->packageDir));
            std::string err;
            if (!loadCubeLut(full, &err)) throw std::runtime_error(err);
            if (!s->packageDir.empty()) p = makeUri(full, s->packageDir);  // relative when inside the project
        }
        s->editor->setClipLut(clipIdsFrom(clip_ids_json), p, strength);
        s->edited();
    });
}

int vs_add_keyframe(vs_session* s, const char* clip_id, int group, int64_t t) {
    return guard(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        s->editor->addKeyframe(c.id, groupFrom(group), c.toSource(Time::fromTicks(t)));
        s->edited();
    });
}

int vs_remove_keyframe(vs_session* s, const char* clip_id, int group, int64_t t) {
    bool removed = false;
    if (guard(s, [&] {
            const Clip& c = clipOrThrow(s, clip_id);
            removed = s->editor->removeKeyframe(c.id, groupFrom(group), c.toSource(Time::fromTicks(t)));
            if (removed) s->edited();
        }) != 0)
        return -1;
    return removed ? 1 : 0;
}

int vs_clear_keyframes(vs_session* s, const char* clip_id, int group) {
    return guard(s, [&] {
        s->editor->clearKeyframes(clipOrThrow(s, clip_id).id, groupFrom(group));
        s->edited();
    });
}

int vs_set_property_at(vs_session* s, const char* clip_id, int64_t t, const char* values_json) {
    return guard(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        const Json j = Json::parse(str(values_json));
        if (!j.is_object()) throw std::runtime_error("values must be a JSON object");
        std::map<std::string, double> values;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!kf::isProperty(it.key()) || !it.value().is_number())
                throw std::runtime_error("not an animatable number: " + it.key());
            values[it.key()] = it.value().get<double>();
        }
        s->editor->setPropertyAt(c.id, c.toSource(Time::fromTicks(t)), values);
        s->edited();
    });
}

int vs_zoom_to_region(vs_session* s, const char* clip_id, int64_t t, double rx, double ry, double rw, double rh) {
    return guard(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        if (!(rw > 0 && rh > 0)) throw std::runtime_error("empty region");
        const Time src = c.toSource(Time::fromTicks(t));
        const double W = s->editor->project().canvas.width, H = s->editor->project().canvas.height;
        const Transform z = zoomToRegion(animatedTransform(c, src), W, H, rx * W, ry * H, rw * W, rh * H);
        s->editor->setPropertyAt(c.id, src, {{kf::kScale, z.scale}, {kf::kX, z.x}, {kf::kY, z.y}});
        s->edited();
    });
}

char* vs_clip_state_at(vs_session* s, const char* clip_id, int64_t ticks) {
    return guardStr(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        const Time t = Time::fromTicks(ticks), src = c.toSource(t);
        const Time tol = Time::fromTicks(Time::fromFrame(1, s->editor->project().fps).ticks() / 2);
        Json out;
        out["inside"] = t >= c.timelineIn && t <= c.timelineOut();
        const Transform tr = animatedTransform(c, src);
        out["scale"] = tr.scale;
        out["x"] = tr.x;
        out["y"] = tr.y;
        out["opacity"] = tr.opacity;
        out["volume"] = animatedVolume(c, src);
        for (const auto g : {kf::Group::Picture, kf::Group::Audio}) {
            bool animated = false, here = false;
            for (const auto& prop : kf::properties(g)) {
                if (!c.keyframes.is_object() || !c.keyframes.contains(prop)) continue;
                animated = true;
                const KeyframeCurve curve(c.keyframes[prop]);
                for (const auto& k : curve.keys()) here |= std::llabs((k.t - src).ticks()) <= tol.ticks();
            }
            const std::string name = g == kf::Group::Audio ? "audio" : "picture";
            out[name + "_here"] = here;
            out[name + "_animated"] = animated;
        }
        return out.dump();
    });
}

char* vs_cover_styles_json(void) {
    Json out = Json::array();
    for (const auto& st : coverStyles())
        out.push_back({{"id", st.id}, {"name", st.name}, {"category", st.category}, {"animated", st.animated}});
    return dup(out.dump());
}

char* vs_cover_shapes_json(void) {
    Json out = Json::array();
    for (const auto& sh : coverShapes()) out.push_back({{"id", sh.id}, {"name", sh.name}});
    return dup(out.dump());
}

char* vs_add_cover(vs_session* s, const char* clip_id, const char* params_json) {
    return guardStr(s, [&] {
        const std::string j = str(params_json);
        const Json p = j.empty() ? Json::object() : Json::parse(j);
        const Id id = s->editor->addCover(clipOrThrow(s, clip_id).id, p);
        s->edited();
        return id;
    });
}

int vs_update_cover(vs_session* s, const char* clip_id, const char* cover_id, const char* patch_json) {
    return guard(s, [&] {
        const Json p = Json::parse(str(patch_json));
        if (!p.is_object()) throw std::runtime_error("patch must be a JSON object");
        s->editor->updateCover(clipOrThrow(s, clip_id).id, str(cover_id), p);
        s->edited();
    });
}

int vs_remove_cover(vs_session* s, const char* clip_id, const char* cover_id) {
    return guard(s, [&] {
        s->editor->removeCover(clipOrThrow(s, clip_id).id, str(cover_id));
        s->edited();
    });
}

int vs_set_cover_at(vs_session* s, const char* clip_id, const char* cover_id, int64_t t, const char* values_json) {
    return guard(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        s->editor->setCoverAt(c.id, str(cover_id), c.toSource(Time::fromTicks(t)), numberMap(str(values_json)));
        s->edited();
    });
}

int vs_key_cover(vs_session* s, const char* clip_id, const char* cover_id, int64_t t) {
    return guard(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        s->editor->keyCover(c.id, str(cover_id), c.toSource(Time::fromTicks(t)));
        s->edited();
    });
}

int vs_clear_cover_keys(vs_session* s, const char* clip_id, const char* cover_id, int64_t t) {
    return guard(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        s->editor->clearCoverKeys(c.id, str(cover_id), c.toSource(Time::fromTicks(t)));
        s->edited();
    });
}

char* vs_cover_state_at(vs_session* s, const char* clip_id, const char* cover_id, int64_t t) {
    return guardStr(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        const Json* p = s->editor->coverParams(c.id, str(cover_id));
        if (!p) throw std::runtime_error("cover not found");
        Json out = coverParamsAt(*p, c.toSource(Time::fromTicks(t)));
        out["following"] = p->contains("anim") && (*p)["anim"].is_object() && !(*p)["anim"].empty();
        return out.dump();
    });
}

char* vs_covers_json(vs_session* s, const char* clip_id) {
    return guardStr(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        Json out = Json::array();
        if (c.effects.is_array())
            for (const auto& e : c.effects)
                if (e.is_object() && e.value("type", std::string{}) == kCoverEffect)
                    out.push_back({{"id", e.value("effect_id", std::string{})}, {"params", e.value("params", Json::object())}});
        return out.dump();
    });
}

int vs_cover_canvas_rect(vs_session* s, const char* clip_id, const char* cover_id, int64_t t, double* xywh) {
    return guard(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        const Json* p = s->editor->coverParams(c.id, str(cover_id));
        if (!p) throw std::runtime_error("cover not found");
        const Time src = c.toSource(Time::fromTicks(t));
        const auto r = sourceRectOnCanvas(s->editor->project(), c, src);
        if (!r) throw std::runtime_error("clip has no picture size");
        const Json now = coverParamsAt(*p, src);
        const double W = s->editor->project().canvas.width, H = s->editor->project().canvas.height;
        if (xywh) {
            xywh[0] = ((*r)[0] + now.value("x", 0.0) * (*r)[2]) / W;
            xywh[1] = ((*r)[1] + now.value("y", 0.0) * (*r)[3]) / H;
            xywh[2] = now.value("w", 0.0) * (*r)[2] / W;
            xywh[3] = now.value("h", 0.0) * (*r)[3] / H;
        }
    });
}

int vs_set_cover_canvas_rect(vs_session* s, const char* clip_id, const char* cover_id, int64_t t, double x, double y,
                             double w, double h) {
    return guard(s, [&] {
        const Clip& c = clipOrThrow(s, clip_id);
        const Time src = c.toSource(Time::fromTicks(t));
        const auto r = sourceRectOnCanvas(s->editor->project(), c, src);
        if (!r || (*r)[2] <= 0 || (*r)[3] <= 0) throw std::runtime_error("clip has no picture size");
        const double W = s->editor->project().canvas.width, H = s->editor->project().canvas.height;
        s->editor->setCoverAt(c.id, str(cover_id), src,
                              {{"x", (x * W - (*r)[0]) / (*r)[2]}, {"y", (y * H - (*r)[1]) / (*r)[3]},
                               {"w", w * W / (*r)[2]}, {"h", h * H / (*r)[3]}});
        s->edited();
    });
}

int vs_cover_preview(const char* style, const char* shape, uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0 || !findCoverStyle(str(style))) return -1;
    try {
        VideoFrame f;
        f.width = width;
        f.height = height;
        f.rgba.assign(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
        Json p = defaultCover(str(style), isCoverShape(str(shape)) ? str(shape) : "rect");
        p.update(Json{{"x", 0.15}, {"y", 0.15}, {"w", 0.7}, {"h", 0.7}, {"feather", 0.08}});
        applyCover(f, p, Time::fromSeconds(0.5));
        std::memcpy(rgba, f.rgba.data(), f.rgba.size());
        return 0;
    } catch (...) {
        return -1;
    }
}

int vs_track_cover_start(vs_session* s, const char* clip_id, const char* cover_id, int64_t from, int64_t to) {
    return guard(s, [&] {
        if (s->trackStatus == 1) throw std::runtime_error("tracking is already running");
        if (!s->media()) throw std::runtime_error("no media module available");
        const Clip& c = clipOrThrow(s, clip_id);
        if (!s->editor->coverParams(c.id, str(cover_id))) throw std::runtime_error("cover not found");
        if (!s->trackJobs) s->trackJobs = std::make_unique<JobScheduler>(1);
        auto snap = s->editor->snapshot();
        const Id clip = c.id, cover = str(cover_id);
        const Time a = Time::fromTicks(from), b = to > 0 ? Time::fromTicks(to) : c.timelineOut();
        {
            std::lock_guard lk(s->trackMu);
            s->trackClip = clip;
            s->trackCover = cover;
            s->trackKeys.clear();
            s->trackError.clear();
            s->trackLost = false;
        }
        s->trackCancel = CancelToken{};
        s->trackProgress = 0.0;
        s->trackStatus = 1;
        const std::string base = mediaBaseDir(s->packageDir);
        s->trackJobs->submit("track", JobPriority::Normal,
                             [s, snap, clip, cover, a, b, base](const CancelToken& tok) {
                                 MediaPool pool(s->backend, base);
                                 const CoverTrack r = trackCoverInClip(*snap, pool, clip, cover, a, b, [&](double p) {
                                     s->trackProgress = p;
                                     return !tok.cancelled();
                                 });
                                 std::lock_guard lk(s->trackMu);
                                 s->trackKeys = r.keys;
                                 s->trackError = r.result.error;
                                 s->trackLost = r.result.lost;
                                 s->trackStatus = r.result.cancelled ? -2 : (!r.result.error.empty() ? -1 : 3);  // 3: ready
                             },
                             s->trackCancel);
    });
}

int vs_track_status(vs_session* s, double* progress) {
    if (!s) return -1;
    if (progress) *progress = s->trackProgress.load();
    int st = s->trackStatus.load();
    if (st == 3) {  // finished: apply on this (editing) thread, once
        std::lock_guard lk(s->trackMu);
        try {
            if (!s->trackKeys.empty()) {
                s->editor->setCoverTrack(s->trackClip, s->trackCover, s->trackKeys);
                s->edited();
            }
            st = s->trackLost ? -3 : 2;
        } catch (const std::exception& e) {
            s->trackError = e.what();
            st = -1;
        }
        s->trackStatus = st;
    }
    if (st == -1) {
        std::lock_guard lk(s->trackMu);
        s->error = s->trackError;
    }
    return st;
}

void vs_track_cancel(vs_session* s) {
    if (s) s->trackCancel.cancel();
}

char* vs_transitions_json(void) {
    Json out = Json::array();
    for (const auto& t : transitionCatalog()) out.push_back({{"id", t.id}, {"name", t.name}, {"category", t.category}});
    return dup(out.dump());
}

int vs_set_transition(vs_session* s, const char* clip_id, int edge, const char* type, int64_t duration) {
    return guard(s, [&] {
        const Time d = duration > 0 ? Time::fromTicks(duration) : defaultTransitionDuration();
        s->editor->setTransition(clipOrThrow(s, clip_id).id, edge == 1 ? TransitionEdge::Out : TransitionEdge::In,
                                 str(type), d);
        s->edited();
    });
}

int vs_set_transition_everywhere(vs_session* s, const char* type, int64_t duration) {
    int n = 0;
    const int rc = guard(s, [&] {
        n = s->editor->setTransitionEverywhere(str(type),
                                               duration > 0 ? Time::fromTicks(duration) : defaultTransitionDuration());
        if (n > 0) s->edited();
    });
    return rc == 0 ? n : -1;
}

char* vs_cut_points_json(vs_session* s) {
    return guardStr(s, [&] {
        Json out = Json::object();
        for (const Track& tr : s->editor->project().tracks) {
            if (tr.type != TrackType::Video) continue;
            Json cuts = Json::array();
            for (const auto& cp : cutPoints(tr))
                cuts.push_back({{"at", cp.at.ticks()},
                                {"from", cp.from ? Json(cp.from->id) : Json()},
                                {"to", cp.to ? Json(cp.to->id) : Json()},
                                {"type", cp.transition.is_object() ? cp.transition["type"] : Json()},
                                {"duration", cp.duration.ticks()}});
            out[tr.id] = std::move(cuts);
        }
        return out.dump();
    });
}

int vs_transition_preview(const char* type, const uint8_t* a, const uint8_t* b, uint8_t* out, int width, int height,
                          double progress) {
    if (!a || !b || !out || width <= 0 || height <= 0 || !findTransition(str(type))) return -1;
    try {
        const std::size_t n = static_cast<std::size_t>(width) * height * 4;
        VideoFrame fa, fb, fo;
        fa.width = fb.width = width;
        fa.height = fb.height = height;
        fa.rgba.assign(a, a + n);
        fb.rgba.assign(b, b + n);
        fo.allocate(width, height);
        applyTransition(str(type), fa, fb, progress, fo, Time::fromSeconds(progress));
        std::memcpy(out, fo.rgba.data(), n);
        return 0;
    } catch (...) {
        return -1;
    }
}

char* vs_suggested_log_format(vs_session* s, const char* asset_id) {
    if (!s || !s->editor) return nullptr;
    const MediaAsset* a = s->editor->project().findAsset(str(asset_id));
    return dup(a ? suggestedLogFormat(*a) : std::string{});
}

int vs_look_preview(const char* look_id, double strength, uint8_t* rgba, int width, int height) {
    const ColorLook* look = findColorLook(str(look_id));
    if (!look || !rgba || width <= 0 || height <= 0) return -1;
    VideoFrame f;
    f.width = width;
    f.height = height;
    f.rgba.assign(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
    Json p = look->params;
    p["strength"] = strength;
    applyColorLook(f, p);
    std::memcpy(rgba, f.rgba.data(), f.rgba.size());
    return 0;
}

int vs_ripple_delete(vs_session* s, const char* clip_id) {
    return guard(s, [&] {
        s->editor->rippleDelete(str(clip_id));
        s->edited();
    });
}

/* ---- tracks ---- */

int vs_remove_track(vs_session* s, const char* track_id) {
    return guard(s, [&] {
        s->editor->removeTrack(str(track_id));
        s->edited();
    });
}

int vs_rename_track(vs_session* s, const char* track_id, const char* name) {
    return guard(s, [&] {
        s->editor->renameTrack(str(track_id), str(name));
        s->edited();
    });
}

int vs_set_track_flags(vs_session* s, const char* track_id, int enabled, int locked, int muted) {
    return guard(s, [&] {
        s->editor->setTrackFlags(str(track_id), enabled != 0, locked != 0, muted != 0);
        s->edited();
    });
}

int vs_set_track_volume(vs_session* s, const char* track_id, double volume) {
    return guard(s, [&] {
        s->editor->setTrackVolume(str(track_id), volume);
        s->edited();
    });
}

int vs_move_track(vs_session* s, const char* track_id, int index_in_type) {
    return guard(s, [&] {
        if (index_in_type < 0) throw std::runtime_error("negative index");
        s->editor->moveTrack(str(track_id), static_cast<std::size_t>(index_in_type));
        s->edited();
    });
}

int64_t vs_snap_clip_move(vs_session* s, const char* clip_id, int64_t proposed_start, int64_t tolerance, int64_t playhead,
                          int* snapped) {
    if (!s) return proposed_start;
    const auto r = snapClipMove(s->editor->project(), str(clip_id), Time::fromTicks(proposed_start),
                                Time::fromTicks(tolerance),
                                playhead >= 0 ? std::optional<Time>(Time::fromTicks(playhead)) : std::nullopt);
    if (snapped) *snapped = r.snapped ? 1 : 0;
    return r.time.ticks();
}

int64_t vs_snap_time(vs_session* s, int64_t t, int64_t tolerance, const char* exclude_clip_id, int64_t playhead,
                     int* snapped) {
    if (!s) return t;
    const auto r = snapTime(s->editor->project(), Time::fromTicks(t), Time::fromTicks(tolerance), str(exclude_clip_id),
                            playhead >= 0 ? std::optional<Time>(Time::fromTicks(playhead)) : std::nullopt);
    if (snapped) *snapped = r.snapped ? 1 : 0;
    return r.time.ticks();
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

void vs_preview_set_sink(vs_session* s, vs_frame_sink sink, void* user) {
    if (!s) return;
    std::lock_guard lk(s->sinkMu);
    s->sink = sink;
    s->sinkUser = user;
    if (!sink) return;
    std::shared_ptr<const VideoFrame> f;
    {
        std::lock_guard fl(s->frameMu);
        f = s->latest;
    }
    if (f) sink(user, f->rgba.data(), f->width, f->height, f->width * 4, f->pts.ticks());
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

namespace {
void startExport(vs_session* s, const char* out_path, const ExportSettings& es);
}

int vs_export_start(vs_session* s, const char* out_path, int width, int height, int fps_num, int fps_den,
                    int64_t video_bitrate) {
    return guard(s, [&] {
        ExportSettings es;
        es.width = width > 0 ? width : s->editor->project().canvas.width;
        es.height = height > 0 ? height : s->editor->project().canvas.height;
        es.fps = fps_num > 0 && fps_den > 0 ? Rational{fps_num, fps_den} : s->editor->project().fps;
        if (video_bitrate > 0) es.videoBitrate = video_bitrate;
        startExport(s, out_path, es);
    });
}

char* vs_export_presets_json(vs_session* s) {
    return guardStr(s, [&] {
        const Project& p = s->editor->project();
        const double seconds = p.duration().seconds();
        Json out = Json::array();
        for (const auto& pr : exportPresets()) {
            const ExportSettings es = exportSettingsFor(p, pr);
            out.push_back({{"id", pr.id},
                           {"name", pr.name},
                           {"description", pr.description},
                           {"width", es.width},
                           {"height", es.height},
                           {"fps", static_cast<double>(es.fps.num) / es.fps.den},
                           {"video_bitrate", es.videoBitrate},
                           {"estimated_bytes", estimateExportBytes(es, seconds)}});
        }
        return out.dump();
    });
}

int vs_export_start_preset(vs_session* s, const char* out_path, const char* preset_id) {
    return guard(s, [&] {
        const ExportPreset* pr = findExportPreset(str(preset_id));
        if (!pr) throw std::runtime_error("unknown export preset: " + str(preset_id));
        startExport(s, out_path, exportSettingsFor(s->editor->project(), *pr));
    });
}

namespace {
void startExport(vs_session* s, const char* out_path, const ExportSettings& es) {
        if (s->exportStatus == VS_EXPORT_RUNNING) throw std::runtime_error("an export is already running");
        if (!s->media()) throw std::runtime_error("no media module available");
        if (!s->jobs) s->jobs = std::make_unique<JobScheduler>(1);
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
}
}  // namespace

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

/* ---- media library ---- */

int vs_request_thumbnails(vs_session* s, const char* asset_id, int count, int height) {
    return guard(s, [&] {
        const MediaAsset& a = s->asset(asset_id);
        s->mediaLibrary().requestThumbnails(a, s->resolve(a), count, height);
    });
}

int vs_request_waveform(vs_session* s, const char* asset_id, int buckets_per_second) {
    return guard(s, [&] {
        const MediaAsset& a = s->asset(asset_id);
        s->mediaLibrary().requestWaveform(a, s->resolve(a), buckets_per_second);
    });
}

int64_t vs_library_serial(vs_session* s) { return s ? s->librarySerial.load() : 0; }

int vs_thumbnails(vs_session* s, const char* asset_id, uint8_t* dst, int64_t dst_capacity, int* count, int* width,
                  int* height) {
    if (!s || !s->library) return 0;
    auto t = s->library->thumbnails(asset_id ? asset_id : "");
    if (!t) return 0;
    if (count) *count = t->count();
    if (width) *width = t->width;
    if (height) *height = t->height;
    if (!dst) return 1;  /* size query */
    if (dst_capacity < static_cast<int64_t>(t->rgba.size())) return -1;
    std::memcpy(dst, t->rgba.data(), t->rgba.size());
    return 1;
}

int64_t vs_waveform(vs_session* s, const char* asset_id, int8_t* dst, int64_t dst_capacity, int* buckets_per_second) {
    if (!s || !s->library) return 0;
    auto w = s->library->waveform(asset_id ? asset_id : "");
    if (!w) return 0;
    if (buckets_per_second) *buckets_per_second = w->bucketsPerSecond;
    const auto n = static_cast<int64_t>(w->buckets());
    if (dst) std::memcpy(dst, w->peaks.data(), static_cast<std::size_t>(std::min<int64_t>(dst_capacity, n * 2)));
    return n;
}

char* vs_missing_assets_json(vs_session* s) {
    return guardStr(s, [&] {
        Json out = Json::array();
        for (const auto& id : missingAssets(s->editor->project(), mediaBaseDir(s->packageDir))) out.push_back(id);
        return out.dump();
    });
}

char* vs_relink_candidates_json(vs_session* s, const char* asset_id, const char* search_dir) {
    return guardStr(s, [&] {
        Json out = Json::array();
        for (const auto& c : findRelinkCandidates(s->asset(asset_id), str(search_dir)))
            out.push_back({{"path", c.path}, {"fingerprint_match", c.fingerprintMatch}});
        return out.dump();
    });
}

int vs_relink_asset(vs_session* s, const char* asset_id, const char* path) {
    return guard(s, [&] {
        const std::string p = str(path);
        s->asset(asset_id);
        s->editor->relinkAsset(asset_id, makeUri(p, s->packageDir), fingerprintFile(p));
        if (s->library) s->library->forget(asset_id);
        // Decoders opened on the old file are dropped with the preview engine's pool.
        s->preview.reset();
        s->edited();
    });
}

namespace {

char* transferJson(const TransferResult& r) {
    return dup(Json{{"ok", r.ok},
                    {"cancelled", r.cancelled},
                    {"error", r.error},
                    {"missing", r.missingAssets},
                    {"package_dir", r.packageDir}}
                   .dump());
}

struct ProgressBridge {
    vs_transfer_progress fn;
    void* user;
    std::atomic<bool> cancel{false};
    TransferProgress callback() {
        if (!fn) return {};
        return [this](double p) {
            if (fn(p, user)) cancel = true;
        };
    }
};

}  // namespace

char* vs_transfer_pack(vs_session* s, const char* out_file, vs_transfer_progress progress, void* user) {
    if (!s || !s->editor) return nullptr;
    try {
        ProgressBridge b{progress, user};
        return transferJson(
            packProject(s->editor->project(), mediaBaseDir(s->packageDir), str(out_file), b.callback(), &b.cancel));
    } catch (const std::exception& e) {
        TransferResult r;
        r.error = e.what();
        return transferJson(r);
    }
}

char* vs_transfer_unpack(const char* package_file, const char* dest_root, vs_transfer_progress progress, void* user) {
    try {
        ProgressBridge b{progress, user};
        return transferJson(unpackProject(str(package_file), str(dest_root), b.callback(), &b.cancel));
    } catch (const std::exception& e) {
        TransferResult r;
        r.error = e.what();
        return transferJson(r);
    }
}
