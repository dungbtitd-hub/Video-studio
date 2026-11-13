#include "EditorController.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMetaObject>

#include "AudioOutput.h"
#include "vstudio/project_io.hpp"
#include "vstudio/proxy.hpp"
#include "vstudio/render.hpp"

#if defined(VSTUDIO_HAS_FFMPEG)
#include "vstudio/ffmpeg/ffmpeg_module.hpp"
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
#if defined(VSTUDIO_HAS_FFMPEG)
    ffmpeg::registerModule(modules_);
#endif
    std::string err;
    backend_ = modules_.best<IMediaBackend>("decode", &err);
    jobs_ = std::make_unique<JobScheduler>();
    resetSession(Editor::newProject("Untitled", 1920, 1080, {30, 1}, ModuleRegistry::currentPlatform()), {});
    if (!backend_) setStatus(tr("No media module available: %1").arg(q(err)));
    // UI refresh only; never decodes. ~60 Hz while playing.
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &EditorController::tick);
    timer_.start();
}

EditorController::~EditorController() {
    exportCancel_.cancel();
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
        auto* holder = new std::shared_ptr<const VideoFrame>(std::move(f));
        QImage img((*holder)->rgba.data(), (*holder)->width, (*holder)->height, (*holder)->width * 4,
                   QImage::Format_RGBA8888, [](void* h) { delete static_cast<std::shared_ptr<const VideoFrame>*>(h); },
                   holder);
        QMetaObject::invokeMethod(this, [this, img] { emit frameReady(img); }, Qt::QueuedConnection);
    });
    audio_ = std::make_unique<AudioOutput>(preview_.get(), editor_->project().sampleRate, editor_->project().audioChannels);
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
        emit projectChanged();
        setStatus(tr("Saved %1").arg(dir));
    } catch (const std::exception& e) {
        setStatus(tr("Save failed: %1").arg(q(e.what())));
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
    snapshot_ = editor_->snapshot();
    preview_->setProject(snapshot_);
    assetsDirty_ = tracksDirty_ = true;
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

void EditorController::moveClip(const QString& clipId, const QString& trackId, double seconds) {
    edit([&] { editor_->moveClip(s(clipId), s(trackId), fromSecs(std::max(0.0, seconds)).snapToFrame(editor_->project().fps)); });
}

void EditorController::trimClip(const QString& clipId, bool start, double seconds) {
    edit([&] { editor_->trimClip(s(clipId), start ? TrimEdge::Start : TrimEdge::End, fromSecs(seconds)); });
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
    if (exporting_ || !backend_) return;
    ExportSettings es;
    es.width = width > 0 ? width : editor_->project().canvas.width;
    es.height = height > 0 ? height : editor_->project().canvas.height;
    es.fps = editor_->project().fps;
    exportCancel_ = CancelToken{};
    exporting_ = true;
    exportProgress_ = 0;
    emit exportChanged();
    auto snap = editor_->snapshot();  // frozen copy: editing continues during export
    const std::string out = s(file.toLocalFile());
    const std::string base = mediaBaseDir(s(packageDir_));
    jobs_->submit("export", JobPriority::Normal, [this, snap, es, out, base](const CancelToken& tok) {
        MediaPool pool(backend_, base);
        auto enc = backend_->createEncoder();
        auto r = exportProject(*snap, pool, *enc, out, es, [this](const ExportProgress& p) {
            const double v = double(p.frame) / p.totalFrames;
            QMetaObject::invokeMethod(this, [this, v] {
                exportProgress_ = v;
                emit exportChanged();
            }, Qt::QueuedConnection);
        }, tok.flag(), &modules_);
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

void EditorController::setSelectedClip(const QString& id) {
    if (id == selected_) return;
    selected_ = id;
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
            cm["name"] = names.value(q(c.assetId), QStringLiteral("?"));
            cm["hasFx"] = c.effects.is_array() && !c.effects.empty();
            clips.push_back(cm);
        }
        tm["clips"] = clips;
        tracks_.push_back(tm);
    }
}
