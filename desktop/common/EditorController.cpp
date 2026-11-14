#include "EditorController.h"

#include "vstudio/fonts.hpp"
#include "vstudio/generators.hpp"
#include "vstudio/overlays.hpp"
#include "vstudio/stickers.hpp"
#include "vstudio/text.hpp"

#include "vstudio/color_space.hpp"
#include "vstudio/cover.hpp"
#include "vstudio/tracker.hpp"
#include "vstudio/export_presets.hpp"
#include "vstudio/keyframes.hpp"
#include "vstudio/grade.hpp"
#include "vstudio/looks.hpp"
#include "vstudio/video_fx.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMetaObject>
#include <QCoreApplication>
#include <QStandardPaths>

#include "AudioOutput.h"
#include "TranslationClient.h"
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include "vstudio/project_io.hpp"
#include "vstudio/recents.hpp"
#include "vstudio/proxy.hpp"
#include "vstudio/render.hpp"

#include "vstudio/stills.hpp"
#if defined(VSTUDIO_HAS_LIBRAW)
#include "vstudio/libraw/libraw_module.hpp"
#endif
#if defined(VSTUDIO_HAS_APPLE_MEDIA)
#include "vstudio/apple/apple_raw.hpp"
#endif
#if defined(VSTUDIO_HAS_FFMPEG)
#include "vstudio/ffmpeg/ffmpeg_module.hpp"
#endif
#if defined(VSTUDIO_HAS_METAL)
#include "vstudio/apple/metal_compositor.hpp"
#endif
#if defined(VSTUDIO_HAS_D3D11)
#include "vstudio/windows/d3d11_compositor.hpp"
#endif

using namespace vstudio;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& q) { return q.toStdString(); }
double secs(Time t) { return t.seconds(); }
Time fromSecs(double v) { return Time::fromSeconds(v); }
}  // namespace

EditorController::EditorController(QObject* parent) : QObject(parent) {
    registerBuiltinModules(modules_);
    // Fonts the user added earlier (Phụ đề / Chữ panels: "Thêm phông…").
    for (QDirIterator it(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/fonts",
                         {"*.ttf", "*.otf", "*.TTF", "*.OTF"}, QDir::Files);
         it.hasNext();) {
        const QFileInfo info(it.next());
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray b = f.readAll();
        QString name = info.completeBaseName();
        if (name.startsWith("user-")) name = name.mid(5);
        vstudio::text::registerFont(info.completeBaseName().toStdString(), name.toStdString(),
                                    std::vector<std::uint8_t>(b.begin(), b.end()));
    }
#if defined(VSTUDIO_HAS_FFMPEG)
    ffmpeg::registerModule(modules_);
#endif
#if defined(VSTUDIO_HAS_METAL)
    apple::registerMetalCompositor(modules_);  // GPU compositing; CPU stays the fallback
#endif
#if defined(VSTUDIO_HAS_D3D11)
    windows::registerD3D11Compositor(modules_);
#endif
#if defined(VSTUDIO_HAS_APPLE_MEDIA)
    apple::registerRawModule(modules_);  // camera RAW with Apple's engine; LibRaw reads the rest
#endif
#if defined(VSTUDIO_HAS_LIBRAW)
    libraw::registerModule(modules_);  // camera RAW photos, loaded on the first RAW file
#endif
    // Plugin effects (Plugin API beta): only manifests are read here, libraries open on first use.
    {
        const QString app = QCoreApplication::applicationDirPath();
        QStringList dirs{app + "/plugins", app + "/../PlugIns",
                         QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins"};
        for (const QString& d : qEnvironmentVariable("VSTUDIO_PLUGIN_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts))
            dirs << d;
        dirs.removeDuplicates();
        for (const QString& d : dirs) loadPlugins(QDir::cleanPath(d));
    }
    std::string err;
    backend_ = withStillDecoders(modules_.best<IMediaBackend>("decode", &err), modules_);
    jobs_ = std::make_unique<JobScheduler>();
    libJobs_ = std::make_unique<JobScheduler>(1);
    resetSession(Editor::newProject("Untitled", 1920, 1080, {30, 1}, ModuleRegistry::currentPlatform()), {});
    if (!backend_) setStatus(tr("No media module available: %1").arg(q(err)));
    // UI refresh only; never decodes. ~60 Hz while playing.
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &EditorController::tick);
    timer_.start();
}

EditorController::~EditorController() {
    stabCancel_.cancel();
    toolJobs_.reset();
    exportCancel_.cancel();
    trackCancel_.cancel();
    trackJobs_.reset();
    library_.reset();
    libJobs_.reset();
    audio_.reset();
    preview_.reset();
    jobs_.reset();
}

void EditorController::resetSession(Project p, const QString& packageDir) {
    audio_.reset();
    preview_.reset();
    editor_ = std::make_unique<Editor>(std::move(p));
    editor_->markSaved();
    packageDir_ = packageDir;
    PreviewConfig cfg;
    cfg.width = previewW_;
    cfg.height = previewH_;
    preview_ = std::make_unique<PreviewEngine>(backend_, mediaBaseDir(s(packageDir)), cfg, &modules_);
    preview_->setFrameCallback([this](std::shared_ptr<const VideoFrame> f) {
        // Render thread -> UI thread. Wrap without copying; QImage keeps the frame alive.
        auto* holder = new std::shared_ptr<const VideoFrame>(f);
        QImage img((*holder)->rgba.data(), (*holder)->width, (*holder)->height, (*holder)->width * 4,
                   QImage::Format_RGBA8888, [](void* h) { delete static_cast<std::shared_ptr<const VideoFrame>*>(h); },
                   holder);
        QMetaObject::invokeMethod(this, [this, img, f = std::move(f)] {
            lastFrame_ = f;
            emit frameReady(img);
        }, Qt::QueuedConnection);
    });
    audio_ = std::make_unique<AudioOutput>(preview_.get(), editor_->project().sampleRate, editor_->project().audioChannels);
    // Disk cache next to the project (or in temp until it is saved), keyed by file fingerprint.
    library_.reset();
    const QString cache = packageDir.isEmpty() ? QDir::tempPath() + "/vstudio-media-cache" : packageDir + "/cache/media";
    library_ = std::make_unique<MediaLibrary>(backend_, *libJobs_, s(cache));
    library_->setListener([this](const Id& id, MediaLibrary::Kind, bool) {
        QMetaObject::invokeMethod(this, [this, id = q(id)] { emit mediaLibraryChanged(id); }, Qt::QueuedConnection);
    });
    assetsSignature_ = 0;
    selected_.clear();
    playhead_ = 0;
    published();
    emit selectionChanged();
    emit playheadChanged();
}

void EditorController::setPreviewSize(int w, int h) {
    previewW_ = std::max(16, w);
    previewH_ = std::max(16, h);
    preview_->setPreviewSize(previewW_, previewH_);
    preview_->requestFrame(fromSecs(playhead_));
}

void EditorController::refreshPreview() { preview_->requestFrame(fromSecs(playhead_)); }

void EditorController::loadSyntheticProject(int clips) {
    // Timeline-only stress project: assets point at files that do not exist,
    // which is fine because edits and timeline drawing never touch media.
    Project p = Editor::newProject("Stress " + std::to_string(clips), 1920, 1080, {30, 1}, ModuleRegistry::currentPlatform());
    resetSession(std::move(p), {});
    std::vector<Id> assets;
    for (int i = 0; i < 40; ++i) {
        MediaAsset a;
        a.uri = "synthetic/clip" + std::to_string(i) + ".mp4";
        a.duration = Time::fromSeconds(600);
        a.width = 1920;
        a.height = 1080;
        a.fps = {30, 1};
        a.hasVideo = true;
        a.audio = AudioInfo{48000, 2, "aac"};
        assets.push_back(editor_->addAsset(a));
    }
    const Id v2 = editor_->addTrack(TrackType::Video, "Video 2");
    const Id v1 = editor_->addTrack(TrackType::Video, "Video 1");
    const Id a1 = editor_->addTrack(TrackType::Audio, "Audio 1");
    for (int i = 0; i < clips; ++i) {
        const Id& asset = assets[i % assets.size()];
        const Id track = i % 3 == 0 ? v2 : (i % 3 == 1 ? v1 : a1);
        editor_->appendClip(track, asset, Time::fromSeconds(1), Time::fromSeconds(1 + 2 + (i % 5)));
    }
    editor_->markSaved();
    published();
}

void EditorController::newProject(const QString& name, int w, int h, int fps) {
    try {
        resetSession(Editor::newProject(s(name), w, h, {fps, 1}, ModuleRegistry::currentPlatform()), {});
        setStatus(tr("New project"));
    } catch (const std::exception& e) {
        setStatus(q(e.what()));
    }
}

void EditorController::openProject(const QUrl& folder) {
    const QString dir = folder.toLocalFile();
    try {
        Project p = loadProject(s(dir));
        if (auto rec = loadAutosave(s(dir))) {
            p = std::move(*rec);  // crash recovery: newer unsaved state
            setStatus(tr("Recovered unsaved changes"));
        }
        resetSession(std::move(p), dir);
        try {
            recents::RecentList(s(recentFile())).touch(s(dir));
        } catch (...) {
        }
        // Assets that already have a proxy from a previous session.
        for (const auto& a : editor_->project().assets) {
            const QString proxy = dir + "/cache/proxy/" + q(a.id) + ".mp4";
            if (QFileInfo::exists(proxy)) preview_->setProxy(a.id, s(proxy));
        }
    } catch (const std::exception& e) {
        setStatus(tr("Cannot open project: %1").arg(q(e.what())));
    }
}

void EditorController::saveProject(const QUrl& folder) {
    QString dir = folder.isEmpty() ? packageDir_ : folder.toLocalFile();
    if (dir.isEmpty()) return;
    if (!dir.endsWith(".vstudio")) dir += ".vstudio";
    try {
        vstudio::saveProject(editor_->project(), s(dir));
        clearAutosave(s(dir));
        if (dir != packageDir_) preview_->setProjectDir(mediaBaseDir(s(dir)));
        packageDir_ = dir;
        editor_->markSaved();
        try {  // the start strip's card: name, length, a small poster of the preview
            recents::writeCard(s(dir), editor_->project(), lastFrame_.get());
            recents::RecentList(s(recentFile())).touch(s(dir));
        } catch (...) {
        }
        emit projectChanged();
        setStatus(tr("Saved %1").arg(dir));
    } catch (const std::exception& e) {
        setStatus(tr("Save failed: %1").arg(q(e.what())));
    }
}

QString EditorController::recentFile() const {
    if (!recentFile_.isEmpty()) return recentFile_;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recent.json";
}

QVariantList EditorController::recentProjects(bool unfinishedOnly, int max) const {
    QVariantList out;
    for (const auto& c : recents::startCards(s(recentFile()), {}, unfinishedOnly, static_cast<std::size_t>(std::max(0, max)),
                                             s(packageDir_))) {
        out.push_back(QVariantMap{{"path", q(c.path)},
                                  {"name", q(c.name)},
                                  {"savedAt", QVariant::fromValue<qint64>(c.savedAt)},
                                  {"duration", c.duration},
                                  {"clips", c.clips},
                                  {"width", c.width},
                                  {"height", c.height},
                                  {"exported", c.exported},
                                  {"hasPoster", c.hasPoster}});
    }
    return out;
}

QImage EditorController::projectPoster(const QString& packageDir) {
    VideoFrame f;
    if (!recents::readPoster(s(packageDir), f)) return {};
    return QImage(f.rgba.data(), f.width, f.height, f.width * 4, QImage::Format_RGBA8888).copy();
}

void EditorController::forgetRecent(const QString& packageDir) {
    try {
        recents::RecentList(s(recentFile())).remove(s(packageDir));
    } catch (...) {
    }
}

void EditorController::importMedia(const QList<QUrl>& files, bool appendToTimeline) {
    if (!backend_ || files.isEmpty()) return;
    std::vector<std::string> paths;
    for (const QUrl& url : files) paths.push_back(s(url.toLocalFile()));
    const std::string pkg = s(packageDir_);
    pendingJobs_ += static_cast<int>(paths.size());
    emit jobsChanged();
    // Probe off the UI thread (slow disks, network shares, huge files). One job
    // per batch so results arrive - and get appended - in the order chosen.
    jobs_->submit("probe", JobPriority::Interactive, [this, paths, pkg, appendToTimeline](const CancelToken& tok) {
        for (const auto& path : paths) {
            if (tok.cancelled()) return;
            std::string err;
            auto pr = backend_->probe(path, &err);
            std::optional<MediaAsset> asset;
            if (pr) asset = assetFromProbe(newId(), path, *pr, pkg);
            QMetaObject::invokeMethod(this, [this, asset, err, path, appendToTimeline] { onProbed(asset, err, path, appendToTimeline); },
                                      Qt::QueuedConnection);
        }
    });
}

void EditorController::onProbed(const std::optional<MediaAsset>& asset, const std::string& err, const std::string& path,
                                bool appendToTimeline) {
    --pendingJobs_;
    emit jobsChanged();
    if (!asset) {
        setStatus(tr("Cannot import %1: %2").arg(q(path), q(err)));
        return;
    }
    if (edit([&] { editor_->addAsset(*asset); })) {
        setStatus(tr("Imported %1").arg(QFileInfo(q(path)).fileName()));
        if (appendToTimeline) addToTimeline(q(asset->id));
    }
    // Large media (4K, VFR phone video): build a preview proxy in the background.
    if (needsProxy(*asset) && !packageDir_.isEmpty()) {
        const std::string out = s(packageDir_) + "/cache/proxy/" + asset->id + ".mp4";
        QDir().mkpath(packageDir_ + "/cache/proxy");
        ++pendingJobs_;
        emit jobsChanged();
        jobs_->submit("proxy", JobPriority::Background, [this, a = *asset, path, out](const CancelToken& tok) {
            auto r = generateProxy(*backend_, path, a, out, ProxyPolicy{}, tok);
            QMetaObject::invokeMethod(this, [this, r, id = a.id] {
                --pendingJobs_;
                emit jobsChanged();
                if (r.ok) preview_->setProxy(id, r.path);
            }, Qt::QueuedConnection);
        });
    }
}

template <class F>
bool EditorController::edit(F&& f) {
    try {
        f();
        published();
        return true;
    } catch (const std::exception& e) {
        setStatus(q(e.what()));
        return false;
    }
}

void EditorController::published() {
    live_ = false;  // a real edit replaces any live preview
    snapshot_ = editor_->snapshot();
    if (gradePreviewOn_) {  // an edit (or undo) always shows the committed project
        gradePreviewOn_ = false;
        preview_->setEffectOverride(nullptr);
    }
    preview_->setProject(snapshot_);
    assetsDirty_ = tracksDirty_ = true;
    refreshLibrary();
    emit projectChanged();
}

void EditorController::addToTimeline(const QString& assetId) {
    const MediaAsset* a = editor_->project().findAsset(s(assetId));
    if (!a) return;
    const TrackType want = a->hasVideo || a->type == MediaType::Image ? TrackType::Video : TrackType::Audio;
    edit([&] {
        Id track;
        for (const auto& t : editor_->project().tracks)
            if (t.type == want && !t.locked) track = t.id;
        if (track.empty()) track = editor_->addTrack(want, want == TrackType::Video ? "Video 1" : "Audio 1");
        setSelectedClip(q(editor_->appendClip(track, a->id)));
    });
}

void EditorController::splitAtPlayhead() {
    edit([&] {
        if (editor_->splitAt(fromSecs(playhead_)) == 0) setStatus(tr("Nothing to split at the playhead"));
    });
}

void EditorController::deleteSelected() {
    if (selected_.isEmpty()) return;
    if (edit([&] { editor_->removeClip(s(selected_)); })) setSelectedClip({});
}

void EditorController::rippleDeleteSelected() {
    if (selected_.isEmpty()) return;
    if (edit([&] { editor_->rippleDelete(s(selected_)); })) setSelectedClip({});
}

void EditorController::setClipLook(const QStringList& clipIds, const QString& lookId, double strength) {
    std::vector<Id> ids;
    for (const QString& c : clipIds) ids.push_back(s(c));
    edit([&] { editor_->setClipLook(ids, s(lookId), strength); });
}

void EditorController::setClipVideoFx(const QStringList& clipIds, const QString& group, const QString& fxId, double amount,
                                      double speed, double line) {
    std::vector<Id> ids;
    for (const QString& c : clipIds) ids.push_back(s(c));
    edit([&] { editor_->setClipVideoFx(ids, s(group), s(fxId), amount, speed, line); });
}

QVariantMap EditorController::clipVideoFx(const QString& clipId, const QString& group) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    const std::string type = videofx::effectTypeFor(s(group));
    if (c && c->effects.is_array())
        for (const auto& e : c->effects)
            if (e.is_object() && e.value("type", std::string{}) == type && e.value("enabled", true) && e.contains("params")) {
                const Json& p = e["params"];
                return {{"fx", q(p.value("fx", std::string{}))},
                        {"amount", p.value("amount", 1.0)},
                        {"speed", p.value("speed", 1.0)},
                        {"line", p.value("line", -1.0)}};
            }
    return {};
}

QPair<QString, double> EditorController::clipLook(const QString& clipId) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    if (c && c->effects.is_array())
        for (const auto& e : c->effects)
            if (e.is_object() && e.value("type", std::string{}) == kColorLookEffect && e.value("enabled", true))
                return {q(e["params"].value("look", std::string{})), e["params"].value("strength", 1.0)};
    return {{}, 1.0};
}

namespace {
const Json* clipEffect(const Project* p, const QString& clipId, const char* type) {
    const Clip* c = p ? p->findClip(clipId.toStdString()) : nullptr;
    if (c && c->effects.is_array())
        for (const auto& e : c->effects)
            if (e.is_object() && e.value("type", std::string{}) == type && e.value("enabled", true) && e.contains("params"))
                return &e["params"];
    return nullptr;
}
}  // namespace

void EditorController::setClipGrade(const QStringList& clipIds, const Json& params) {
    std::vector<Id> ids;
    for (const QString& c : clipIds) ids.push_back(s(c));
    gradePreviewOn_ = false;
    preview_->setEffectOverride(nullptr);
    edit([&] { editor_->setClipGrade(ids, params); });
}

Json EditorController::clipGrade(const QString& clipId) const {
    if (const Json* p = clipEffect(snapshot_.get(), clipId, kColorGradeEffect)) return *p;
    return Json::object();
}

void EditorController::previewGrade(const QString& clipId, const Json& params) {
    gradePreviewOn_ = true;
    preview_->setEffectOverride(std::make_shared<EffectOverride>(EffectOverride{s(clipId), kColorGradeEffect, params}));
}

void EditorController::clearGradePreview() {
    if (!gradePreviewOn_) return;
    gradePreviewOn_ = false;
    preview_->setEffectOverride(nullptr);
}

void EditorController::setClipColorSpace(const QStringList& clipIds, const QString& input, double exposure) {
    std::vector<Id> ids;
    for (const QString& c : clipIds) ids.push_back(s(c));
    edit([&] { editor_->setClipColorSpace(ids, s(input), exposure); });
}

QPair<QString, double> EditorController::clipColorSpace(const QString& clipId) const {
    if (const Json* p = clipEffect(snapshot_.get(), clipId, kColorSpaceEffect))
        return {q(p->value("input", std::string{})), p->value("exposure", 0.0)};
    return {{}, 0.0};
}

QString EditorController::suggestedColorSpace(const QString& clipId) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    const MediaAsset* a = c ? snapshot_->findAsset(c->assetId) : nullptr;
    return a ? q(suggestedLogFormat(*a)) : QString{};
}

void EditorController::setClipLut(const QStringList& clipIds, const QString& file, double strength) {
    std::vector<Id> ids;
    for (const QString& c : clipIds) ids.push_back(s(c));
    std::string path;
    if (!file.isEmpty()) {
        std::string err;
        if (!loadCubeLut(s(file), &err)) return setStatus(tr("Cannot use LUT: %1").arg(q(err)));
        path = packageDir_.isEmpty() ? s(file) : makeUri(s(file), s(packageDir_));
    }
    edit([&] { editor_->setClipLut(ids, path, strength); });
}

QString EditorController::clipLut(const QString& clipId) const {
    const Json* p = clipEffect(snapshot_.get(), clipId, kLutEffect);
    return p ? q(p->value("path", std::string{})) : QString{};
}

// ---- keyframes ----

EditorController::KeyState EditorController::keyState() const {
    KeyState k;
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    const Track* tr = c ? snapshot_->trackOfClip(c->id) : nullptr;
    const Time t = fromSecs(playhead_);
    if (!c || t < c->timelineIn || t > c->timelineOut()) return k;
    k.valid = true;
    k.video = tr->type == TrackType::Video;
    const Time src = c->toSource(t), tol = Time::fromTicks(Time::fromFrame(1, snapshot_->fps).ticks() / 2);
    auto here = [&](kf::Group g, bool& animated) {
        bool found = false;
        for (const auto& prop : kf::properties(g)) {
            if (!c->keyframes.is_object() || !c->keyframes.contains(prop)) continue;
            animated = true;
            const KeyframeCurve curve(c->keyframes[prop]);
            for (const auto& key : curve.keys()) found |= std::llabs((key.t - src).ticks()) <= tol.ticks();
        }
        return found;
    };
    k.pictureHere = here(kf::Group::Picture, k.pictureAnimated);
    k.audioHere = here(kf::Group::Audio, k.audioAnimated);
    k.scale = animatedTransform(*c, src).scale;
    k.volume = animatedVolume(*c, src);
    return k;
}

void EditorController::toggleKeyframe(bool audio) {
    const KeyState k = keyState();
    if (!k.valid) return setStatus(tr("Select a clip and put the playhead inside it"));
    const Clip* c = snapshot_->findClip(s(selected_));
    const Time src = c->toSource(fromSecs(playhead_));
    const auto g = audio ? kf::Group::Audio : kf::Group::Picture;
    edit([&] {
        if (!editor_->removeKeyframe(c->id, g, src)) editor_->addKeyframe(c->id, g, src);
    });
}

void EditorController::zoomToRegion(const QRectF& r) {
    const KeyState k = keyState();
    if (!k.valid || !k.video) return setStatus(tr("Select a video or photo clip under the playhead"));
    const Clip* c = snapshot_->findClip(s(selected_));
    const Time src = c->toSource(fromSecs(playhead_));
    const double W = snapshot_->canvas.width, H = snapshot_->canvas.height;
    const Transform z = vstudio::zoomToRegion(animatedTransform(*c, src), W, H, r.x() * W, r.y() * H, r.width() * W,
                                              r.height() * H);
    edit([&] { editor_->setPropertyAt(c->id, src, {{kf::kScale, z.scale}, {kf::kX, z.x}, {kf::kY, z.y}}); });
    if (!k.pictureAnimated)
        setStatus(tr("Zoomed the whole clip. For a gradual zoom, add a picture keyframe (◇) where it should start."));
}

void EditorController::resetZoom() {
    const KeyState k = keyState();
    if (!k.valid || !k.video) return;
    const Clip* c = snapshot_->findClip(s(selected_));
    edit([&] {
        editor_->setPropertyAt(c->id, c->toSource(fromSecs(playhead_)), {{kf::kScale, 1.0}, {kf::kX, 0.0}, {kf::kY, 0.0}});
    });
}

void EditorController::setVolumeAtPlayhead(double v) {
    const KeyState k = keyState();
    if (!k.valid) return;
    const Clip* c = snapshot_->findClip(s(selected_));
    edit([&] { editor_->setPropertyAt(c->id, c->toSource(fromSecs(playhead_)), {{kf::kVolume, v}}); });
}

void EditorController::jumpKeyframe(bool forward) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c) return;
    const Time now = fromSecs(playhead_), half = Time::fromTicks(Time::fromFrame(1, snapshot_->fps).ticks() / 2);
    std::optional<Time> best;
    for (const Time src : vstudio::keyframeTimes(*c)) {
        const Time t = c->toTimeline(src);
        if (t < c->timelineIn || t > c->timelineOut()) continue;
        if (forward ? t > now + half && (!best || t < *best) : t < now - half && (!best || t > *best)) best = t;
    }
    if (best) seek(secs(*best));
}

QList<double> EditorController::keyframeTimes(const QString& clipId, bool audio) const {
    QList<double> out;
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    if (!c || !c->keyframes.is_object()) return out;
    for (const auto& prop : kf::properties(audio ? kf::Group::Audio : kf::Group::Picture)) {
        if (!c->keyframes.contains(prop)) continue;
        const KeyframeCurve curve(c->keyframes[prop]);
        for (const auto& k : curve.keys()) {
            const double t = secs(c->toTimeline(k.t));
            if (!out.contains(t)) out << t;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---- covers ----

void EditorController::setActiveCover(const QString& id) {
    if (id == activeCover_) return;
    activeCover_ = id;
    emit coversChanged();
}

QVariantList EditorController::covers(const QString& clipId) const {
    QVariantList out;
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    if (!c || !c->effects.is_array()) return out;
    for (const auto& e : c->effects) {
        if (!e.is_object() || e.value("type", std::string{}) != kCoverEffect) continue;
        const Json p = e.value("params", Json::object());
        const auto* st = findCoverStyle(p.value("style", std::string{}));
        QString shapeName;
        for (const auto& sh : coverShapes())
            if (sh.id == p.value("shape", std::string{})) shapeName = q(sh.name);
        out.push_back(QVariantMap{{"id", q(e.value("effect_id", std::string{}))},
                                  {"style", q(p.value("style", std::string{}))},
                                  {"shape", q(p.value("shape", std::string{}))},
                                  {"name", (st ? q(st->name) : QString("?")) + " · " + shapeName},
                                  {"following", p.contains("anim") && !p["anim"].empty()}});
    }
    return out;
}

QString EditorController::addCover(const QString& style, const QString& shape) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    const Track* track = c ? snapshot_->trackOfClip(c->id) : nullptr;
    if (!c || track->type != TrackType::Video) {
        setStatus(track ? tr("Covers need a video or photo clip") : tr("Select a clip first"));
        return {};
    }
    Id id;
    if (!edit([&] { id = editor_->addCover(c->id, Json{{"style", s(style)}, {"shape", s(shape)}}); })) return {};
    activeCover_ = q(id);
    emit coversChanged();
    return activeCover_;
}

void EditorController::removeCover(const QString& id) {
    if (selected_.isEmpty()) return;
    edit([&] { editor_->removeCover(s(selected_), s(id)); });
    if (activeCover_ == id) activeCover_.clear();
    emit coversChanged();
}

QVariantMap EditorController::coverState() const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    const Json* p = c && !activeCover_.isEmpty() ? editor_->coverParams(c->id, s(activeCover_)) : nullptr;
    if (!p) return {};
    const Time t = std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut());
    Json now = coverParamsAt(*p, c->toSource(t));
    now["following"] = p->contains("anim") && !(*p)["anim"].empty();
    return QJsonDocument::fromJson(QByteArray::fromStdString(now.dump())).object().toVariantMap();
}

void EditorController::updateCover(const QVariantMap& patch) {
    if (selected_.isEmpty() || activeCover_.isEmpty()) return;
    const Json j = Json::parse(QJsonDocument(QJsonObject::fromVariantMap(patch)).toJson(QJsonDocument::Compact).toStdString());
    // Region-like values go through setCoverAt so they key when following.
    std::map<std::string, double> keyed;
    Json rest = Json::object();
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key() == "amount" || it.key() == "rotation") keyed[it.key()] = it.value().get<double>();
        else rest[it.key()] = it.value();
    }
    const Clip* c = snapshot_->findClip(s(selected_));
    if (!c) return;
    const Time src = c->toSource(std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut()));
    edit([&] {
        if (!rest.empty()) editor_->updateCover(c->id, s(activeCover_), rest);
        if (!keyed.empty()) editor_->setCoverAt(c->id, s(activeCover_), src, keyed);
    });
}

std::optional<QRectF> EditorController::sourceRectOnCanvas(const Clip& c) const {
    const MediaAsset* a = snapshot_->findAsset(c.assetId);
    if (!a || a->width <= 0 || a->height <= 0) return std::nullopt;
    const double W = snapshot_->canvas.width, H = snapshot_->canvas.height;
    const Time t = std::clamp(fromSecs(playhead_), c.timelineIn, c.timelineOut());
    const Transform tf = animatedTransform(c, c.toSource(t));
    const double fit = std::min(W / a->width, H / a->height) * tf.scale;
    const double w = a->width * fit, h = a->height * fit;
    return QRectF(W / 2 + tf.x - w / 2, H / 2 + tf.y - h / 2, w, h);
}

QRectF EditorController::coverCanvasRect() const {
    const QVariantMap st = coverState();
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (st.isEmpty() || !c) return {};
    const auto src = sourceRectOnCanvas(*c);
    if (!src) return {};
    const double W = snapshot_->canvas.width, H = snapshot_->canvas.height;
    return QRectF((src->x() + st["x"].toDouble() * src->width()) / W, (src->y() + st["y"].toDouble() * src->height()) / H,
                  st["w"].toDouble() * src->width() / W, st["h"].toDouble() * src->height() / H);
}

void EditorController::setCoverCanvasRect(const QRectF& r) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || activeCover_.isEmpty()) return;
    const auto src = sourceRectOnCanvas(*c);
    if (!src || src->width() <= 0 || src->height() <= 0) return;
    const double W = snapshot_->canvas.width, H = snapshot_->canvas.height;
    const double x = (r.x() * W - src->x()) / src->width(), y = (r.y() * H - src->y()) / src->height();
    const double w = r.width() * W / src->width(), h = r.height() * H / src->height();
    const Time t = std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut());
    edit([&] { editor_->setCoverAt(c->id, s(activeCover_), c->toSource(t), {{"x", x}, {"y", y}, {"w", w}, {"h", h}}); });
}

void EditorController::setCoverFollowing(bool on) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || activeCover_.isEmpty()) return;
    const Time src = c->toSource(std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut()));
    edit([&] {
        if (on) editor_->keyCover(c->id, s(activeCover_), src);
        else editor_->clearCoverKeys(c->id, s(activeCover_), src);
    });
    emit coversChanged();
}

void EditorController::trackCover() {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (tracking_ || !c || activeCover_.isEmpty() || !backend_) return;
    if (!trackJobs_) trackJobs_ = std::make_unique<JobScheduler>(1);
    trackCancel_ = CancelToken{};
    tracking_ = true;
    trackProgress_ = 0;
    emit coversChanged();
    auto snap = snapshot_;
    const Id clip = c->id, cover = s(activeCover_);
    const Time from = std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut());
    const std::string base = mediaBaseDir(s(packageDir_));
    trackJobs_->submit("track", JobPriority::Normal, [this, snap, clip, cover, from, base](const CancelToken& tok) {
        MediaPool pool(backend_, base);
        CoverTrack r = trackCoverInClip(*snap, pool, clip, cover, from, snap->findClip(clip)->timelineOut(),
                                        [this, &tok](double p) {
                                            QMetaObject::invokeMethod(this, [this, p] {
                                                trackProgress_ = p;
                                                emit coversChanged();
                                            }, Qt::QueuedConnection);
                                            return !tok.cancelled();
                                        });
        QMetaObject::invokeMethod(this, [this, r = std::move(r), clip, cover] {
            tracking_ = false;
            bool ok = false;
            QString msg;
            if (r.result.cancelled) msg = tr("Tracking cancelled");
            else if (!r.result.error.empty()) msg = q(r.result.error);
            else if (r.keys.size() >= 2 && edit([&] { editor_->setCoverTrack(clip, cover, r.keys); })) {
                ok = true;
                msg = r.result.lost ? tr("Lost the subject after %1 s: the cover follows it up to there")
                                          .arg(secs(r.result.samples.back().sourceTime - r.result.samples.front().sourceTime), 0, 'f', 1)
                                    : tr("The cover now follows the subject (%1 keys)").arg(r.keys.size());
            } else msg = tr("Could not follow the subject");
            setStatus(msg);
            emit coversChanged();
            emit trackFinished(ok, msg);
        }, Qt::QueuedConnection);
    }, trackCancel_);
}

bool EditorController::canFollowMotion() const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    return c && !followTargetAt(*snapshot_, c->id, std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut())).empty();
}

void EditorController::followMotion(bool withSize) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (tracking_ || !c || !backend_) return;
    const Time from = std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut());
    const Id target = followTargetAt(*snapshot_, c->id, from);
    const auto box = target.empty() ? std::nullopt : followStartBox(*snapshot_, c->id, target, from);
    if (!box) {
        setStatus(tr("Không có video nào bên dưới clip này ở playhead để bám theo"));
        return;
    }
    if (!trackJobs_) trackJobs_ = std::make_unique<JobScheduler>(1);
    trackCancel_ = CancelToken{};
    tracking_ = true;
    trackProgress_ = 0;
    emit coversChanged();
    auto snap = snapshot_;
    const Id clip = c->id;
    const TrackBox start = *box;
    const std::string base = mediaBaseDir(s(packageDir_));
    trackJobs_->submit("track", JobPriority::Normal, [this, snap, clip, target, start, from, withSize, base](const CancelToken& tok) {
        MediaPool pool(backend_, base);
        FollowTrack r = trackFollow(*snap, pool, clip, target, start, from, snap->findClip(clip)->timelineOut(), withSize,
                                    [this, &tok](double p) {
                                        QMetaObject::invokeMethod(this, [this, p] {
                                            trackProgress_ = p;
                                            emit coversChanged();
                                        }, Qt::QueuedConnection);
                                        return !tok.cancelled();
                                    });
        QMetaObject::invokeMethod(this, [this, r = std::move(r), clip] {
            tracking_ = false;
            bool ok = false;
            QString msg;
            if (r.result.cancelled) msg = tr("Đã dừng bám theo");
            else if (!r.result.error.empty()) msg = q(r.result.error);
            else if (r.keys.size() >= 2 && edit([&] { editor_->setFollowTrack(clip, r.keys); })) {
                ok = true;
                msg = r.result.lost ? tr("Mất dấu đối tượng sau %1 s: clip bám theo tới đó")
                                          .arg(secs(r.result.samples.back().sourceTime - r.result.samples.front().sourceTime), 0, 'f', 1)
                                    : tr("Clip đang bám theo đối tượng (%1 keyframe)").arg(r.keys.size());
            } else msg = tr("Không bám theo được đối tượng");
            setStatus(msg);
            emit coversChanged();
            emit trackFinished(ok, msg);
        }, Qt::QueuedConnection);
    }, trackCancel_);
}

void EditorController::cancelTrack() { trackCancel_.cancel(); }

QString EditorController::clipAsset(const QString& clipId) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    return c ? q(c->assetId) : QString{};
}

QStringList EditorController::lookableClips() const {
    QStringList out;
    if (!snapshot_) return out;
    for (const auto& t : snapshot_->tracks)
        if (t.type == TrackType::Video && !t.locked)
            for (const auto& c : t.clips) out << q(c.id);
    return out;
}

void EditorController::seekBy(double seconds) { seek(std::max(0.0, playhead_ + seconds)); }

void EditorController::jumpEditPoint(bool forward) {
    // Half a frame of slack so a playhead parked on an edge moves on to the next one.
    const Time half = Time::fromTicks(Time::fromFrame(1, editor_->project().fps).ticks() / 2);
    const Time from = fromSecs(playhead_) + (forward ? half : -half);
    if (const auto t = adjacentEditPoint(editor_->project(), from, forward)) seek(secs(*t));
    else seek(forward ? duration() : 0.0);
}

void EditorController::moveClip(const QString& clipId, const QString& trackId, double seconds) {
    edit([&] { editor_->moveClip(s(clipId), s(trackId), fromSecs(std::max(0.0, seconds)).snapToFrame(editor_->project().fps)); });
}

void EditorController::trimClip(const QString& clipId, bool start, double seconds) {
    edit([&] { editor_->trimClip(s(clipId), start ? TrimEdge::Start : TrimEdge::End, fromSecs(seconds)); });
}

void EditorController::addTrack(bool audio) {
    int n = 1;
    for (const auto& t : editor_->project().tracks) n += (t.type == TrackType::Audio) == audio;
    const QString name = QString(audio ? "Audio %1" : "Video %1").arg(n);
    edit([&] { editor_->addTrack(audio ? TrackType::Audio : TrackType::Video, s(name)); });
}

void EditorController::removeTrack(const QString& trackId) { edit([&] { editor_->removeTrack(s(trackId)); }); }

void EditorController::renameTrack(const QString& trackId, const QString& name) {
    edit([&] { editor_->renameTrack(s(trackId), s(name)); });
}

void EditorController::setTrackFlags(const QString& trackId, bool enabled, bool locked, bool muted) {
    edit([&] { editor_->setTrackFlags(s(trackId), enabled, locked, muted); });
}

void EditorController::moveTrack(const QString& trackId, int indexInType) {
    if (indexInType < 0) return;
    edit([&] { editor_->moveTrack(s(trackId), std::size_t(indexInType)); });
}

double EditorController::snapClipMove(const QString& clipId, double proposedStart, double tolerance, bool* snapped,
                                      double* target) const {
    const auto r = vstudio::snapClipMove(*snapshot_, s(clipId), fromSecs(proposedStart), fromSecs(tolerance), fromSecs(playhead_));
    if (snapped) *snapped = r.snapped;
    if (target) *target = secs(r.target);
    return secs(r.time);
}

double EditorController::snapTime(double t, double tolerance, const QString& excludeClip, bool* snapped,
                                  double* target) const {
    const auto r = vstudio::snapTime(*snapshot_, fromSecs(t), fromSecs(tolerance), s(excludeClip), fromSecs(playhead_));
    if (snapped) *snapped = r.snapped;
    if (target) *target = secs(r.target);
    return secs(r.time);
}

void EditorController::undo() {
    if (editor_->undo()) published();
}

void EditorController::redo() {
    if (editor_->redo()) published();
}

void EditorController::togglePlay() {
    if (preview_->playing()) {
        preview_->pause();
        audio_->stop();
    } else {
        if (playhead_ >= duration()) playhead_ = 0;
        preview_->play(fromSecs(playhead_));
        audio_->start();
    }
    emit playingChanged();
}

void EditorController::seek(double seconds) {
    playhead_ = std::clamp(seconds, 0.0, std::max(0.0, duration()));
    if (preview_->playing()) {
        preview_->play(fromSecs(playhead_));
    } else {
        preview_->requestFrame(fromSecs(playhead_));  // latest wins: scrubbing never queues
    }
    emit playheadChanged();
}

void EditorController::stepFrames(int frames) {
    const Rational fps = editor_->project().fps;
    const std::int64_t f = fromSecs(playhead_).toFrame(fps, Rounding::Nearest) + frames;
    seek(Time::fromFrame(std::max<std::int64_t>(0, f), fps).seconds());
}

void EditorController::exportTo(const QUrl& file, int width, int height) {
    ExportSettings es;
    es.width = width > 0 ? width : editor_->project().canvas.width;
    es.height = height > 0 ? height : editor_->project().canvas.height;
    es.fps = editor_->project().fps;
    startExport(file, es);
}

void EditorController::exportWithPreset(const QUrl& file, const QString& presetId, const QString& formatId) {
    const ExportPreset* pr = findExportPreset(s(presetId));
    if (!pr) return setStatus(tr("Unknown export preset"));
    if (!findExportFormat(s(formatId))) return setStatus(tr("Unknown export format"));
    startExport(file, exportSettingsFor(editor_->project(), *pr, s(formatId)));
}

QVariantList EditorController::exportFormats() const {
    QVariantList out;
    for (const auto& f : vstudio::exportFormats())
        out.push_back(QVariantMap{{"id", q(f.id)}, {"name", q(f.name)}, {"description", q(f.description)}});
    return out;
}

QVariantList EditorController::exportPresets(const QString& formatId) const {
    QVariantList out;
    const Project& p = editor_->project();
    for (const auto& pr : vstudio::exportPresets()) {
        const ExportSettings es = exportSettingsFor(p, pr, s(formatId));
        out.push_back(QVariantMap{{"id", q(pr.id)},
                                  {"name", q(pr.name)},
                                  {"description", q(pr.description)},
                                  {"width", es.width},
                                  {"height", es.height},
                                  {"fps", double(es.fps.num) / es.fps.den},
                                  {"bitrate", qint64(es.videoBitrate)},
                                  {"estimatedBytes", qint64(estimateExportBytes(es, p.duration().seconds()))}});
    }
    return out;
}

void EditorController::startExport(const QUrl& file, const ExportSettings& es) {
    if (exporting_ || !backend_) return;
    exportCancel_ = CancelToken{};
    exporting_ = true;
    exportProgress_ = 0;
    emit exportChanged();
    auto snap = editor_->snapshot();  // frozen copy: editing continues during export
    const std::string out = s(file.toLocalFile());
    const std::string base = mediaBaseDir(s(packageDir_)), package = s(packageDir_);
    jobs_->submit("export", JobPriority::Normal, [this, snap, es, out, base, package](const CancelToken& tok) {
        MediaPool pool(backend_, base);
        auto enc = backend_->createEncoder();
        auto r = exportProject(*snap, pool, *enc, out, es, [this](const ExportProgress& p) {
            const double v = double(p.frame) / p.totalFrames;
            QMetaObject::invokeMethod(this, [this, v] {
                exportProgress_ = v;
                emit exportChanged();
            }, Qt::QueuedConnection);
        }, tok.flag(), &modules_);
        if (r.ok && !package.empty()) {
            try {
                recents::markExported(package, *snap);  // no longer "unfinished" on the start strip
            } catch (...) {
            }
        }
        QMetaObject::invokeMethod(this, [this, r, out] {
            exporting_ = false;
            emit exportChanged();
            if (r.ok) setStatus(tr("Exported %1 in %2 s").arg(q(out)).arg(r.seconds, 0, 'f', 1));
            else setStatus(r.cancelled ? tr("Export cancelled") : tr("Export failed: %1").arg(q(r.error)));
        }, Qt::QueuedConnection);
    }, exportCancel_);
}

void EditorController::cancelExport() { exportCancel_.cancel(); }

QString EditorController::projectName() const { return q(editor_->project().name); }
double EditorController::duration() const { return secs(editor_->project().duration()); }
double EditorController::fps() const { return editor_->project().fps.toDouble(); }
bool EditorController::playing() const { return preview_ && preview_->playing(); }
QString EditorController::timecode() const { return q(fromSecs(playhead_).timecode(editor_->project().fps)); }

QString generatedLabel(const vstudio::Clip& c) {
    if (!c.generated()) return {};
    const std::string kind = c.content.value("kind", std::string{});
    if (kind == "text") {
        QString t = QString::fromStdString(c.content.value("text", std::string{})).simplified();
        return QStringLiteral("T  ") + (t.isEmpty() ? QObject::tr("(chữ trống)") : t);
    }
    if (kind == "subtitle") {
        QString t = QString::fromStdString(c.content.value("text", std::string{})).simplified();
        return QStringLiteral("CC  ") + (t.isEmpty() ? QObject::tr("(dòng trống)") : t);
    }
    if (kind == "sticker") {
        const auto* info = vstudio::stickers::findSticker(c.content.value("sticker", std::string{}));
        return QStringLiteral("★  ") + (info ? QString::fromStdString(info->name) : QObject::tr("Sticker"));
    }
    if (kind == "overlay") {
        const auto* info = vstudio::overlays::findOverlay(c.content.value("overlay", std::string{}));
        return QStringLiteral("✦  ") + (info ? QString::fromStdString(info->name) : QObject::tr("Lớp phủ"));
    }
    return QString::fromStdString(kind);
}

QString EditorController::addText(const QString& text) {
    vstudio::Json c = vstudio::text::defaultTextContent(text.isEmpty() ? std::string("Nhập chữ") : s(text));
    Id id;
    if (!edit([&] { id = editor_->addGeneratedClip(c, fromSecs(playhead_), Time::fromSeconds(3)); })) return {};
    setSelectedClip(q(id));
    return q(id);
}

QString EditorController::addSticker(const QString& stickerId) {
    if (!vstudio::stickers::findSticker(s(stickerId))) return {};
    Id id;
    if (!edit([&] {
            id = editor_->addGeneratedClip(vstudio::stickers::defaultStickerContent(s(stickerId)), fromSecs(playhead_),
                                           Time::fromSeconds(3));
        }))
        return {};
    setSelectedClip(q(id));
    return q(id);
}

QString EditorController::addOverlay(const QString& overlayId) {
    if (!vstudio::overlays::findOverlay(s(overlayId))) return {};
    vstudio::Json c = vstudio::overlays::defaultOverlayContent(s(overlayId));
    c.erase("w");  // sized to the canvas by the editor
    c.erase("h");
    c.erase("ref");
    Id id;
    if (!edit([&] { id = editor_->addGeneratedClip(c, fromSecs(playhead_), Time::fromSeconds(5)); })) return {};
    setSelectedClip(q(id));
    return q(id);
}

void EditorController::setOverlayFullFrame() {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || c->content.value("kind", std::string{}) != "overlay") return;
    const Time src = c->toSource(std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut()));
    edit([&] { editor_->setOverlayRegion(c->id, src, 0, 0, snapshot_->canvas.width, snapshot_->canvas.height); });
}

std::optional<vstudio::Json> EditorController::selectedContent() const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || !c->generated()) return std::nullopt;
    return std::optional<vstudio::Json>(std::in_place, c->content);  // MSVC: Json converts to too many types
}

void EditorController::updateContent(const vstudio::Json& patch) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || !c->generated()) return;
    if (!edit([&] { editor_->updateClipContent(c->id, patch); })) endLive();
}

// ---- subtitles ----

QString EditorController::currentSubtitleTrack() const {
    if (!snapshot_) return {};
    if (const auto* t = snapshot_->trackOfClip(s(selected_)); t && vstudio::subtitles::isSubtitleTrack(*t)) return q(t->id);
    if (const auto* t = snapshot_->findTrack(s(subtitleTrack_)); t && vstudio::subtitles::isSubtitleTrack(*t))
        return subtitleTrack_;
    for (auto it = snapshot_->tracks.rbegin(); it != snapshot_->tracks.rend(); ++it)
        if (vstudio::subtitles::isSubtitleTrack(*it)) return q(it->id);
    return {};
}

void EditorController::setCurrentSubtitleTrack(const QString& trackId) {
    if (subtitleTrack_ == trackId) return;
    subtitleTrack_ = trackId;
    // A line of another track stays selected only if it belongs to this one.
    if (const auto* t = snapshot_ ? snapshot_->trackOfClip(s(selected_)) : nullptr; t && q(t->id) != trackId &&
                                                                                     vstudio::subtitles::isSubtitleTrack(*t))
        setSelectedClip({});
    emit subtitleTrackChanged();
}

QString EditorController::addSubtitleTrack() {
    Id id;
    if (!edit([&] { id = editor_->addSubtitleTrack(); })) return {};
    setCurrentSubtitleTrack(q(id));
    return q(id);
}

QString EditorController::addSubtitle(const QString& text) {
    Id id;
    if (!edit([&] { id = editor_->addSubtitle(s(currentSubtitleTrack()), fromSecs(playhead_), s(text)); })) return {};
    setSelectedClip(q(id));
    return q(id);
}

bool EditorController::importSubtitles(const QUrl& file, double offsetSecs) {
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(tr("Không mở được tệp phụ đề"));
        return false;
    }
    if (f.size() > 50 * 1024 * 1024) {
        setStatus(tr("Tệp phụ đề quá lớn"));
        return false;
    }
    const std::string text = f.readAll().toStdString();
    const auto cues = vstudio::subtitles::parseAny(text);
    Id id;
    const QString name = QFileInfo(file.toLocalFile()).completeBaseName();
    if (!edit([&] { id = editor_->importSubtitles(cues, s(name), {}, fromSecs(offsetSecs)); })) return false;
    setCurrentSubtitleTrack(q(id));
    setStatus(tr("Đã nhập %1 dòng phụ đề").arg(cues.size()));
    return true;
}

bool EditorController::exportSubtitles(const QString& trackId, const QUrl& file) {
    const auto* t = snapshot_ ? snapshot_->findTrack(s(trackId)) : nullptr;
    if (!t || !vstudio::subtitles::isSubtitleTrack(*t)) return false;
    const QString path = file.toLocalFile();
    const auto cues = vstudio::subtitles::cuesOf(*t);
    const std::string out =
        path.endsWith(".vtt", Qt::CaseInsensitive) ? vstudio::subtitles::toVtt(cues) : vstudio::subtitles::toSrt(cues);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(out.data(), qint64(out.size())) != qint64(out.size())) {
        setStatus(tr("Không ghi được tệp phụ đề"));
        return false;
    }
    setStatus(tr("Đã xuất %1 dòng phụ đề").arg(cues.size()));
    return true;
}

void EditorController::setSubtitleText(const QString& clipId, const QString& text) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    if (!c || !c->generated() || c->content.value("text", std::string{}) == s(text)) return;
    edit([&] { editor_->updateClipContent(c->id, {{"text", s(text)}}); });
}

void EditorController::setSubtitleSettings(const QString& trackId, const vstudio::Json& patch) {
    edit([&] { editor_->setSubtitleSettings(s(trackId), patch); });
}

void EditorController::shiftSubtitles(const QString& trackId, double seconds) {
    edit([&] { editor_->shiftSubtitles(s(trackId), fromSecs(seconds)); });
}

bool EditorController::translating() const { return translator_ && translator_->running(); }

void EditorController::cancelTranslation() {
    if (translator_) translator_->cancel();
}

void EditorController::translateSubtitles(const QString& trackId, const vstudio::Json& config, const QString& from,
                                          const QString& to, const QString& notes, int mode, int batchSize) {
    const auto* t = snapshot_ ? snapshot_->findTrack(s(trackId)) : nullptr;
    if (!t || !vstudio::subtitles::isSubtitleTrack(*t) || t->clips.empty()) {
        emit translationFinished(false, tr("Track này chưa có dòng phụ đề nào"));
        return;
    }
    if (!translator_) {
        translator_ = new TranslationClient(this);
        connect(translator_, &TranslationClient::progress, this, &EditorController::translationProgress);
        connect(translator_, &TranslationClient::finished, this, [this](int failed, const QString& err, bool cancelled) {
            auto job = translator_->take();
            if (cancelled || !job) {
                emit translationFinished(false, tr("Đã dừng dịch"));
                return;
            }
            if (job->results().empty()) {
                emit translationFinished(false, tr("Không dịch được: %1").arg(err));
                return;
            }
            const auto mode = translateMode_ == 0   ? vstudio::Editor::TranslationMode::Replace
                              : translateMode_ == 2 ? vstudio::Editor::TranslationMode::Bilingual
                                                    : vstudio::Editor::TranslationMode::NewTrack;
            Id id;
            if (!edit([&] { id = editor_->applySubtitleTranslation(job->sourceTrack(), job->results(), job->targetLanguage(), mode); })) {
                emit translationFinished(false, status_);
                return;
            }
            setCurrentSubtitleTrack(q(id));
            const QString msg = failed ? tr("Đã dịch %1 dòng; %2 nhóm dòng chưa dịch được (giữ nguyên bản gốc): %3")
                                             .arg(job->results().size())
                                             .arg(failed)
                                             .arg(err)
                                       : tr("Đã dịch %1 dòng").arg(job->results().size());
            setStatus(msg);
            emit translationFinished(failed == 0, msg);
        });
    }
    try {
        const vstudio::Json jobCfg = TranslationClient::jobConfig(config);
        auto job = std::make_unique<vstudio::subtitles::TranslationJob>(*t, jobCfg, s(from), s(to), s(notes), batchSize);
        if (config.value("provider", std::string{}) != "command") (void)job->request(0);  // config problems show now
        translateTrack_ = trackId;
        translateMode_ = mode;
        translator_->start(std::move(job), config);
    } catch (const std::exception& e) {
        emit translationFinished(false, q(e.what()));
    }
}

bool EditorController::addFontFile(const QUrl& file, QString* fontId) {
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = f.readAll();
    const QFileInfo info(file.toLocalFile());
    const QString id = "user-" + info.completeBaseName().toLower().replace(QRegularExpression("[^a-z0-9]+"), "-");
    std::vector<std::uint8_t> data(bytes.begin(), bytes.end());
    if (!vstudio::text::registerFont(s(id), s(info.completeBaseName()), std::move(data))) {
        setStatus(tr("Tệp này không phải phông TrueType/OpenType dùng được"));
        return false;
    }
    // Kept with the app so the font is there next time.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/fonts";
    QDir().mkpath(dir);
    const QString dest = dir + "/" + id + "." + info.suffix().toLower();
    if (QFileInfo(dest) != info) {
        QFile::remove(dest);
        QFile::copy(info.absoluteFilePath(), dest);
    }
    if (fontId) *fontId = id;
    setStatus(tr("Đã thêm phông %1").arg(info.completeBaseName()));
    return true;
}

QRectF EditorController::objectCanvasRect() const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || !c->generated()) return {};
    const Time t = fromSecs(playhead_);
    if (t < c->timelineIn || t >= c->timelineOut()) return {};
    const auto r = vstudio::generatedRectOnCanvas(*snapshot_, *c, c->toSource(t));
    const double W = snapshot_->canvas.width, H = snapshot_->canvas.height;
    return {r[0] / W, r[1] / H, r[2] / W, r[3] / H};
}

std::function<void(Editor&)> EditorController::objectRectEdit(const QRectF& from, const QRectF& to) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || !c->generated() || from.width() <= 0) return {};
    const Id id = c->id;
    if (c->content.value("kind", std::string{}) == "subtitle") {
        // Subtitles move as a block: dragging places the track's lines (or this line, when it has its
        // own position) freely; pulling a corner changes the text size.
        const vstudio::Track* tr = snapshot_->trackOfClip(id);
        if (!tr) return {};
        const Id trackId = tr->id;
        const double k = to.width() / from.width();
        if (std::abs(k - 1) > 0.01) {
            const vstudio::Json st = tr->extra.value(vstudio::subtitles::kExtraKey, vstudio::Json::object())
                                         .value("style", vstudio::Json::object());
            const double size = std::round(std::clamp(st.value("size", 56.0) * k, 12.0, 400.0));
            return [=](Editor& e) { e.setSubtitleSettings(trackId, {{"style", {{"size", size}}}}); };
        }
        const bool own = c->content.contains("position") && c->content["position"].is_object();
        const vstudio::Json pos{{"anchor", "custom"}, {"h", "center"}, {"x", std::clamp(to.center().x(), 0.0, 1.0)},
                                {"y", std::clamp(to.center().y(), 0.0, 1.0)}};
        return [=](Editor& e) {
            if (own) e.updateClipContent(id, {{"position", pos}});
            else e.setSubtitleSettings(trackId, {{"position", pos}});
        };
    }
    const Time src = c->toSource(std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut()));
    const Transform tf = animatedTransform(*c, src);
    const double W = snapshot_->canvas.width, H = snapshot_->canvas.height;
    if (c->content.value("kind", std::string{}) == "overlay") {
        if (to.width() <= 0 || to.height() <= 0) return {};
        return [=](Editor& e) { e.setOverlayRegion(id, src, to.x() * W, to.y() * H, to.width() * W, to.height() * H); };
    }
    const double k = to.width() / from.width();
    // The box centre sits at canvas centre + offset + (box centre in content units) * scale.
    const double x = to.center().x() * W - W / 2 - (from.center().x() * W - W / 2 - tf.x) * k;
    const double y = to.center().y() * H - H / 2 - (from.center().y() * H - H / 2 - tf.y) * k;
    const double scale = std::clamp(tf.scale * k, 0.05, 20.0);
    return [=](Editor& e) { e.setPropertyAt(id, src, {{kf::kScale, scale}, {kf::kX, x}, {kf::kY, y}}); };
}

void EditorController::setObjectCanvasRect(const QRectF& from, const QRectF& to) {
    auto fn = objectRectEdit(from, to);
    if (!fn || !edit([&] { fn(*editor_); })) endLive();
}

void EditorController::previewObjectCanvasRect(const QRectF& from, const QRectF& to) {
    if (auto fn = objectRectEdit(from, to)) showLive(fn);
}

void EditorController::previewContent(const vstudio::Json& patch) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || !c->generated()) return;
    const Id id = c->id;
    showLive([&](Editor& e) { e.updateClipContent(id, patch); });
}

double EditorController::stickerDisplaySize() const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || !c->generated()) return 0;
    const Time src = c->toSource(std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut()));
    return c->content.value("size", 320.0) * animatedTransform(*c, src).scale;
}

void EditorController::setStickerDisplaySize(double px, bool live) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || !c->generated() || px <= 0) return;
    const Time src = c->toSource(std::clamp(fromSecs(playhead_), c->timelineIn, c->timelineOut()));
    const double scale = std::max(0.05, animatedTransform(*c, src).scale);
    const vstudio::Json patch = {{"size", std::max(1.0, std::round(px / scale))}};
    if (live) previewContent(patch);
    else updateContent(patch);
}

void EditorController::showLive(const std::function<void(Editor&)>& fn) {
    if (!preview_) return;
    if (auto p = editor_->preview(fn)) {
        live_ = true;
        preview_->setProject(std::move(p));
    }
}

void EditorController::endLive() {
    if (!live_ || !preview_) return;
    live_ = false;
    preview_->setProject(snapshot_);
}

void EditorController::setSelectedClip(const QString& id) {
    if (id == selected_) return;
    selected_ = id;
    if (cut_.valid() && cut_.clipId != id) {  // a cut belongs to the clip that stores it
        cut_ = {};
        emit cutChanged();
    }
    if (!activeCover_.isEmpty()) {  // the active cover belongs to the previous clip
        activeCover_.clear();
        emit coversChanged();
    }
    emit selectionChanged();
}

void EditorController::setStatus(const QString& st) {
    status_ = st;
    emit statusChanged();
}

void EditorController::tick() {
    const bool playing = preview_->playing();
    if (playing) {
        playhead_ = secs(preview_->playhead());
        if (playhead_ >= duration()) {
            preview_->pause();
            audio_->stop();
            playhead_ = duration();
        }
        emit playheadChanged();
    }
    if (playing != wasPlaying_) {
        wasPlaying_ = playing;
        emit playingChanged();
    }
    // Autosave every ~10 s of idle editing (atomic write, cheap).
    static int counter = 0;
    if (++counter % 600 == 0 && editor_->dirty() && !packageDir_.isEmpty()) {
        try {
            writeAutosave(editor_->project(), s(packageDir_));
        } catch (...) {
        }
    }
}

QVariantList EditorController::assets() const {
    if (assetsDirty_) buildAssetsModel();
    return assets_;
}

QVariantList EditorController::tracks() const {
    if (tracksDirty_) buildTracksModel();
    return tracks_;
}

void EditorController::buildAssetsModel() const {
    assetsDirty_ = false;
    const Project& p = *snapshot_;
    assets_.clear();
    for (const auto& a : p.assets) {
        QVariantMap m;
        m["id"] = q(a.id);
        m["name"] = QFileInfo(q(a.uri)).fileName();
        m["type"] = q(toString(a.type));
        m["duration"] = secs(a.duration);
        m["resolution"] = a.width ? QString("%1×%2").arg(a.width).arg(a.height) : QString();
        assets_.push_back(m);
    }
}

void EditorController::buildTracksModel() const {
    tracksDirty_ = false;
    const Project& p = *snapshot_;
    tracks_.clear();
    QHash<QString, QString> names;  // asset id -> file name, once per asset
    for (const auto& a : p.assets) names.insert(q(a.id), QFileInfo(q(a.uri)).fileName());
    // Top video track first, like the prototype; audio tracks below.
    std::vector<const Track*> order;
    for (auto it = p.tracks.rbegin(); it != p.tracks.rend(); ++it)
        if (it->type == TrackType::Video) order.push_back(&*it);
    for (const auto& t : p.tracks)
        if (t.type == TrackType::Audio) order.push_back(&t);
    for (const Track* t : order) {
        QVariantMap tm;
        tm["id"] = q(t->id);
        tm["name"] = q(t->name);
        tm["type"] = q(toString(t->type));
        tm["muted"] = t->muted;
        tm["locked"] = t->locked;
        QVariantList clips;
        for (const auto& c : t->clips) {
            QVariantMap cm;
            cm["id"] = q(c.id);
            cm["start"] = secs(c.timelineIn);
            cm["duration"] = secs(c.duration());
            const QString label = generatedLabel(c);
            cm["name"] = label.isEmpty() ? names.value(q(c.assetId), QStringLiteral("?")) : label;
            cm["hasFx"] = c.effects.is_array() && !c.effects.empty();
            clips.push_back(cm);
        }
        tm["clips"] = clips;
        tracks_.push_back(tm);
    }
}

// ---- media library ----

std::string EditorController::resolve(const MediaAsset& a) const {
    QString p = q(a.uri);
    if (QFileInfo(p).isRelative() && !packageDir_.isEmpty()) p = q(mediaBaseDir(s(packageDir_))) + "/" + p;
    return s(p);
}

void EditorController::refreshLibrary() {
    if (!library_ || !snapshot_) return;
    // Clip edits do not touch assets: skip the file checks unless ids or paths changed.
    std::size_t sig = snapshot_->assets.size();
    for (const auto& a : snapshot_->assets) sig = sig * 1000003u ^ std::hash<std::string>{}(a.id + "|" + a.uri);
    if (sig == assetsSignature_) return;
    assetsSignature_ = sig;
    missing_.clear();
    for (const auto& id : missingAssets(*snapshot_, mediaBaseDir(s(packageDir_)))) missing_.insert(q(id));
    for (const auto& a : snapshot_->assets) {
        if (missing_.contains(q(a.id))) continue;
        const std::string path = resolve(a);
        library_->requestThumbnails(a, path, 12, 54);
        library_->requestWaveform(a, path, 100);
    }
}

std::shared_ptr<const ThumbnailStrip> EditorController::thumbnails(const QString& assetId) const {
    return library_ ? library_->thumbnails(s(assetId)) : nullptr;
}

std::shared_ptr<const Waveform> EditorController::waveform(const QString& assetId) const {
    return library_ ? library_->waveform(s(assetId)) : nullptr;
}

void EditorController::relinkToFile(const QString& assetId, const QUrl& file) {
    const std::string path = s(file.toLocalFile());
    edit([&] {
        editor_->relinkAsset(s(assetId), makeUri(path, s(packageDir_)), fingerprintFile(path));
        library_->forget(s(assetId));
    });
    setStatus(missing_.contains(assetId) ? tr("File still not found") : tr("Media relinked"));
}

void EditorController::relinkFromFolder(const QString& assetId, const QUrl& folder) {
    const MediaAsset* a = editor_->project().findAsset(s(assetId));
    if (!a) return;
    ++pendingJobs_;
    emit jobsChanged();
    setStatus(tr("Searching for %1…").arg(QFileInfo(q(a->uri)).fileName()));
    // The search reads directories and hashes same-size files: never on the UI thread.
    libJobs_->submit("relink search", JobPriority::Normal, [this, asset = *a, dir = s(folder.toLocalFile()), assetId](const CancelToken&) {
        const auto found = findRelinkCandidates(asset, dir);
        QMetaObject::invokeMethod(this, [this, found, assetId] {
            --pendingJobs_;
            emit jobsChanged();
            if (found.empty()) return setStatus(tr("No matching file in that folder"));
            relinkToFile(assetId, QUrl::fromLocalFile(q(found.front().path)));
            if (!found.front().fingerprintMatch)
                setStatus(tr("Relinked by file name (content differs from the original): %1").arg(q(found.front().path)));
        }, Qt::QueuedConnection);
    });
}

// ---- transfer package ----

void EditorController::runTransfer(
    const QString& what,
    std::function<TransferResult(const TransferProgress&, const std::atomic<bool>*)> work,
    std::function<void(const TransferResult&)> done) {
    transferring_ = true;
    transferCancel_ = CancelToken{};
    ++pendingJobs_;
    emit jobsChanged();
    setStatus(what);
    const CancelToken token = transferCancel_;
    jobs_->submit("transfer", JobPriority::Normal, [this, what, work = std::move(work), done = std::move(done), token](const CancelToken&) {
        auto lastPercent = std::make_shared<int>(-1);
        const TransferProgress progress = [this, what, lastPercent](double p) {
            const int pct = static_cast<int>(p * 100);
            if (pct == *lastPercent) return;  // at most 100 UI updates
            *lastPercent = pct;
            QMetaObject::invokeMethod(this, [this, what, pct] { setStatus(QStringLiteral("%1 %2%").arg(what).arg(pct)); },
                                      Qt::QueuedConnection);
        };
        const TransferResult r = work(progress, token.flag());
        QMetaObject::invokeMethod(this, [this, r, done] {
            transferring_ = false;
            --pendingJobs_;
            emit jobsChanged();
            done(r);
        }, Qt::QueuedConnection);
    });
}

void EditorController::packProject(const QUrl& file) {
    if (transferring_) return;
    QString out = file.toLocalFile();
    if (!out.endsWith(".vstudiopkg")) out += ".vstudiopkg";
    const Project project = editor_->project();  // a copy: editing goes on while packing
    const std::string base = mediaBaseDir(s(packageDir_));
    runTransfer(tr("Packing project…"), [project, base, out](const TransferProgress& p, const std::atomic<bool>* c) {
        return vstudio::packProject(project, base, s(out), p, c);
    }, [this, out](const TransferResult& r) {
        QString msg;
        if (r.cancelled) msg = tr("Packing cancelled");
        else if (!r.ok) msg = tr("Packing failed: %1").arg(q(r.error));
        else if (!r.missingAssets.empty())
            msg = tr("Packed %1 (%2 missing media left out)").arg(QFileInfo(out).fileName()).arg(r.missingAssets.size());
        else msg = tr("Packed %1").arg(QFileInfo(out).fileName());
        setStatus(msg);
        emit transferFinished(r.ok, out, msg);
    });
}

void EditorController::openPackage(const QUrl& file, const QUrl& destRoot) {
    if (transferring_) return;
    const QString in = file.toLocalFile();
    const QString root = destRoot.isEmpty() ? QFileInfo(in).absolutePath() : destRoot.toLocalFile();
    runTransfer(tr("Opening package…"), [in, root](const TransferProgress& p, const std::atomic<bool>* c) {
        return unpackProject(s(in), s(root), p, c);
    }, [this](const TransferResult& r) {
        if (!r.ok) {
            const QString msg = r.cancelled ? tr("Opening cancelled") : tr("Cannot open package: %1").arg(q(r.error));
            setStatus(msg);
            emit transferFinished(false, {}, msg);
            return;
        }
        openProject(QUrl::fromLocalFile(q(r.packageDir)));
        emit transferFinished(true, q(r.packageDir), status_);
    });
}

// ---------------------------------------------------------------- transitions

namespace {
const vstudio::CutPoint* findCut(const vstudio::Project& p, const std::string& clipId, int edge,
                                 std::vector<vstudio::CutPoint>& storage) {
    const vstudio::Track* tr = p.trackOfClip(clipId);
    if (!tr) return nullptr;
    storage = vstudio::cutPoints(*tr);
    for (const auto& cp : storage) {
        if (edge == 0 && cp.to && cp.to->id == clipId) return &cp;
        if (edge == 1 && cp.from && cp.from->id == clipId) return &cp;
    }
    return nullptr;
}
}  // namespace

EditorController::Cut EditorController::normalizedCut(const QString& clipId, int edge) const {
    std::vector<vstudio::CutPoint> cuts;
    if (!snapshot_ || clipId.isEmpty()) return {};
    const auto* cp = findCut(*snapshot_, s(clipId), edge, cuts);
    if (!cp) return {};
    return {q(cp->owner()->id), cp->edge() == vstudio::TransitionEdge::In ? 0 : 1};
}

void EditorController::setSelectedCut(const QString& clipId, int edge) {
    const Cut c = normalizedCut(clipId, edge);
    const bool same = c.clipId == cut_.clipId && c.edge == cut_.edge;
    cut_ = c;
    if (c.valid() && c.clipId != selected_) {  // the clip that stores it becomes the selected clip
        selected_ = c.clipId;
        if (!activeCover_.isEmpty()) {
            activeCover_.clear();
            emit coversChanged();
        }
        emit selectionChanged();
    } else if (same) {
        return;
    }
    emit cutChanged();
}

QPair<QString, double> EditorController::transitionAt(const Cut& cut) const {
    std::vector<vstudio::CutPoint> cuts;
    if (!snapshot_ || !cut.valid()) return {{}, 0.0};
    const auto* cp = findCut(*snapshot_, s(cut.clipId), cut.edge, cuts);
    if (!cp || !cp->transition.is_object()) return {{}, 0.0};
    return {q(cp->transition.value("type", std::string{})), cp->duration.seconds()};
}

QString EditorController::cutLabel(const Cut& cut) const {
    std::vector<vstudio::CutPoint> cuts;
    if (!snapshot_ || !cut.valid()) return {};
    const auto* cp = findCut(*snapshot_, s(cut.clipId), cut.edge, cuts);
    if (!cp) return {};
    auto name = [&](const vstudio::Clip* c) {
        if (c->generated()) return generatedLabel(*c);
        const auto* a = snapshot_->findAsset(c->assetId);
        const QString uri = a ? q(a->uri) : QString("?");
        return uri.mid(uri.lastIndexOf('/') + 1);
    };
    if (cp->from && cp->to) return tr("Chỗ nối: %1 → %2").arg(name(cp->from), name(cp->to));
    if (cp->to) return tr("Đầu clip %1: hiện dần lên").arg(name(cp->to));
    return tr("Cuối clip %1: mờ dần đi").arg(name(cp->from));
}

void EditorController::setTransition(const Cut& cut, const QString& type, double seconds) {
    if (!cut.valid()) return;
    edit([&] {
        editor_->setTransition(s(cut.clipId), cut.edge == 1 ? vstudio::TransitionEdge::Out : vstudio::TransitionEdge::In,
                               s(type), vstudio::Time::fromSeconds(seconds));
    });
    emit cutChanged();  // what plays at the selected cut may have changed
}

int EditorController::setTransitionEverywhere(const QString& type, double seconds) {
    int n = 0;
    edit([&] { n = editor_->setTransitionEverywhere(s(type), vstudio::Time::fromSeconds(seconds)); });
    emit cutChanged();
    return n;
}
