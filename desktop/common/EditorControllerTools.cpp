// EditorController: sound (fades, EQ, noise reduction, detach, mixer, voice-over),
// clip tools (reverse, freeze frame, chroma key, stabilisation) and favourites.
// Heavy work (noise analysis, shake measurement, probing a recording) runs on toolJobs_;
// results come back to the UI thread and become one undo step.
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QStandardPaths>

#include "EditorController.h"
#include "vstudio/speed_curve.hpp"
#include "vstudio/user_presets.hpp"
#include "vstudio/export_presets.hpp"
#include "VoiceRecorder.h"
#include "vstudio/audio_fx.hpp"
#include "vstudio/plugins.hpp"
#include "vstudio/project_io.hpp"
#include "vstudio/render.hpp"

using namespace vstudio;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& q) { return q.toStdString(); }
std::vector<Id> ids(const QStringList& l) {
    std::vector<Id> v;
    for (const QString& x : l) v.push_back(s(x));
    return v;
}
const Json* effectParams(const Clip* c, const char* type) {
    if (!c || !c->effects.is_array()) return nullptr;
    for (const auto& e : c->effects)
        if (e.is_object() && e.value("type", std::string{}) == type && e.contains("params")) return &e["params"];
    return nullptr;
}
}  // namespace

#define VS_EDIT(body)                 \
    [&]() -> bool {                   \
        try {                         \
            body;                     \
            published();              \
            return true;              \
        } catch (const std::exception& e) { \
            setStatus(q(e.what()));   \
            return false;             \
        }                             \
    }()

// ---------------------------------------------------------------- sound

Json EditorController::clipAudio(const QString& clipId) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    return c ? audiofx::clipAudio(*c) : Json::object();
}

void EditorController::setClipAudio(const QStringList& clipIds, const Json& patch) {
    VS_EDIT(editor_->setClipAudio(ids(clipIds), patch));
}

void EditorController::setClipVolume(const QString& clipId, double volume) { VS_EDIT(editor_->setClipVolume(s(clipId), volume)); }

double EditorController::clipVolume(const QString& clipId) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    return c ? c->volume : 1.0;
}

void EditorController::detachAudio() {
    if (selected_.isEmpty()) return;
    if (VS_EDIT(editor_->detachAudio(s(selected_)))) setStatus(tr("Đã tách âm thanh ra track riêng"));
}

void EditorController::setTrackVolume(const QString& trackId, double volume) {
    VS_EDIT(editor_->setTrackVolume(s(trackId), volume));
}

void EditorController::setTrackMix(const QString& trackId, double pan, bool solo) {
    VS_EDIT(editor_->setTrackMix(s(trackId), pan, solo));
}

void EditorController::analyzeNoise(double amount) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    const MediaAsset* a = c ? snapshot_->findAsset(c->assetId) : nullptr;
    if (noiseBusy_ || !c || !a || !a->audio || !backend_) {
        if (c && (!a || !a->audio)) setStatus(tr("Clip này không có âm thanh"));
        return;
    }
    if (!toolJobs_) toolJobs_ = std::make_unique<JobScheduler>(1);
    noiseBusy_ = true;
    emit toolsChanged();
    const Clip clip = *c;
    const MediaAsset asset = *a;
    const std::string base = mediaBaseDir(s(packageDir_));
    const int sr = snapshot_->sampleRate;
    toolJobs_->submit("noise", JobPriority::Normal, [this, clip, asset, base, sr, amount](const CancelToken&) {
        MediaPool pool(backend_, base);
        std::string err;
        std::vector<float> profile;
        if (IMediaDecoder* d = pool.decoder(asset, "analyze", &err)) profile = audiofx::analyzeClipNoise(*d, clip, sr, 20.0, nullptr, &err);
        QMetaObject::invokeMethod(this, [this, profile, err, id = clip.id, amount] {
            noiseBusy_ = false;
            if (profile.empty()) setStatus(tr("Không đo được tiếng ồn: %1").arg(q(err)));
            else if (VS_EDIT(editor_->setClipAudio({id}, {{"denoise", {{"amount", amount}, {"profile", profile}}}})))
                setStatus(tr("Đã khử tiếng ồn nền"));
            emit toolsChanged();
        }, Qt::QueuedConnection);
    });
}

bool EditorController::recordingVoice() const { return recorder_ && recorder_->recording(); }

void EditorController::startVoiceOver() {
    if (recordingVoice()) return;
    if (!recorder_) {
        recorder_ = new VoiceRecorder(this);
        connect(recorder_, &VoiceRecorder::finished, this, [this](const QString& file) {
            if (file.isEmpty()) {
                voiceError_ = recorder_->error();
                setStatus(tr("Ghi âm lỗi: %1").arg(voiceError_));
                emit toolsChanged();
                return;
            }
            onVoiceRecorded(file, voiceStart_);
        });
    }
    const QString dir = packageDir_.isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recordings"
                            : packageDir_ + "/recordings";
    voiceStart_ = playhead_;
    voiceError_.clear();
    if (!recorder_->start(dir)) {
        voiceError_ = recorder_->error();
        setStatus(voiceError_);
        emit toolsChanged();
        return;
    }
    if (!playing()) togglePlay();  // the picture plays while the voice is recorded
    setStatus(tr("Đang ghi âm… bấm Dừng để kết thúc"));
    emit toolsChanged();
}

void EditorController::stopVoiceOver() {
    if (!recordingVoice()) return;
    recorder_->stop();
    if (playing()) togglePlay();
    emit toolsChanged();
}

void EditorController::onVoiceRecorded(const QString& file, double at) {
    if (!toolJobs_) toolJobs_ = std::make_unique<JobScheduler>(1);
    const std::string path = s(file), pkg = s(packageDir_);
    toolJobs_->submit("probe", JobPriority::Interactive, [this, path, pkg, at](const CancelToken&) {
        std::string err;
        auto pr = backend_->probe(path, &err);
        std::optional<MediaAsset> asset;
        if (pr) asset = assetFromProbe(newId(), path, *pr, pkg);
        QMetaObject::invokeMethod(this, [this, asset, err, at] {
            if (!asset) {
                setStatus(tr("Không đọc được bản ghi âm: %1").arg(q(err)));
            } else if (VS_EDIT({
                           editor_->addAsset(*asset);
                           editor_->placeAudio(asset->id, Time::fromSeconds(at));
                       })) {
                setStatus(tr("Đã thêm bản ghi âm vào timeline"));
            }
            emit toolsChanged();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------- clip tools

void EditorController::setReversed(const QStringList& clipIds, bool on) {
    if (VS_EDIT(editor_->setClipReversed(ids(clipIds), on))) setStatus(on ? tr("Clip phát ngược") : tr("Clip phát xuôi"));
}

bool EditorController::clipReversed(const QString& clipId) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    return c && c->reversed;
}

void EditorController::freezeFrame(double seconds) {
    if (selected_.isEmpty()) return;
    Id still;
    if (VS_EDIT(still = editor_->freezeFrame(s(selected_), Time::fromSeconds(playhead_), Time::fromSeconds(seconds)))) {
        setSelectedClip(q(still));
        setStatus(tr("Đã đóng băng khung hình %1 giây").arg(seconds, 0, 'f', 1));
    }
}

void EditorController::setClipSpeed(const QStringList& clipIds, double speedValue, const QVector<QPointF>& points,
                                    const QString& preset) {
    std::vector<vstudio::speed::CurvePoint> pts;
    for (const QPointF& p : points) pts.push_back({p.x(), p.y()});
    if (VS_EDIT(editor_->setClipSpeed(ids(clipIds), speedValue, pts, preset.toStdString())))
        setStatus(pts.empty() ? tr("Tốc độ %1x").arg(speedValue, 0, 'f', 2) : tr("Đã đặt đường cong tốc độ"));
}

QVariantMap EditorController::clipSpeed(const QString& clipId) const {
    QVariantMap m;
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    if (!c) return m;
    std::string why;
    m["can"] = vstudio::speed::canChangeSpeed(*snapshot_, *c, &why);
    m["why"] = QString::fromStdString(why);
    m["speed"] = vstudio::speed::baseSpeed(*c);
    QVariantList pts;
    for (const auto& p : vstudio::speed::pointsOf(*c)) pts.push_back(QPointF(p.u, p.v));
    m["points"] = pts;
    m["preset"] = QString::fromStdString(c->extra.value("speed_preset", std::string()));
    m["seconds"] = c->duration().seconds();
    return m;
}

void EditorController::setChromaKey(const QStringList& clipIds, const Json& params) {
    VS_EDIT(editor_->setClipEffect(ids(clipIds), cliptools::kChromaKeyEffect, params, false,
                                   params.is_null() ? "Remove chroma key" : "Chroma key"));
}

Json EditorController::chromaKey(const QString& clipId) const {
    const Json* p = effectParams(snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr, cliptools::kChromaKeyEffect);
    return p ? *p : Json();
}

QString EditorController::guessKeyColor() const {
    return lastFrame_ ? q(cliptools::guessKeyColor(*lastFrame_)) : QStringLiteral("#00ff00");
}

void EditorController::stabilize(double strength) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    const MediaAsset* a = c ? snapshot_->findAsset(c->assetId) : nullptr;
    if (stabBusy_ || !c || !a || a->type != MediaType::Video || !backend_) {
        if (c && (!a || a->type != MediaType::Video)) setStatus(tr("Chống rung cần clip video"));
        return;
    }
    if (!toolJobs_) toolJobs_ = std::make_unique<JobScheduler>(1);
    stabCancel_ = CancelToken{};
    stabBusy_ = true;
    stabProgress_ = 0;
    emit toolsChanged();
    const Clip clip = *c;
    const MediaAsset asset = *a;
    const std::string base = mediaBaseDir(s(packageDir_));
    const Rational fps = a->fps.num > 0 ? a->fps : snapshot_->fps;
    toolJobs_->submit("stabilize", JobPriority::Normal, [this, clip, asset, base, fps, strength](const CancelToken& tok) {
        MediaPool pool(backend_, base);
        std::string err;
        std::vector<cliptools::ShakeSample> path;
        std::atomic<bool> cancel{false};
        if (IMediaDecoder* d = pool.decoder(asset, "stabilize", &err, 320, 320)) {
            path = cliptools::analyzeClipShake(*d, clip, fps, &cancel, [this, &tok, &cancel](double p) {
                if (tok.cancelled()) cancel = true;
                QMetaObject::invokeMethod(this, [this, p] {
                    stabProgress_ = p;
                    emit toolsChanged();
                }, Qt::QueuedConnection);
            });
        }
        const bool cancelled = tok.cancelled();
        QMetaObject::invokeMethod(this, [this, path, err, cancelled, id = clip.id, strength] {
            stabBusy_ = false;
            if (cancelled) setStatus(tr("Đã hủy chống rung"));
            else if (path.size() < 3) setStatus(tr("Không đo được độ rung: %1").arg(q(err)));
            else if (VS_EDIT(editor_->setClipEffect({id}, cliptools::kStabilizeEffect, cliptools::stabilizeParams(path, strength),
                                                    false, "Stabilise")))
                setStatus(tr("Đã chống rung (%1 khung hình)").arg(path.size()));
            emit toolsChanged();
        }, Qt::QueuedConnection);
    }, stabCancel_);
}

void EditorController::setStabilizeStrength(double strength) {
    const Json p = stabilizeParams(selected_);
    if (p.is_null() || !p.contains("path")) return;
    std::vector<cliptools::ShakeSample> path;
    for (const auto& x : p["path"]) path.push_back({x[0].get<double>(), x[1].get<double>(), x[2].get<double>()});
    VS_EDIT(editor_->setClipEffect({s(selected_)}, cliptools::kStabilizeEffect, cliptools::stabilizeParams(path, strength),
                                   false, "Stabilise"));
}

void EditorController::removeStabilize() {
    if (!selected_.isEmpty())
        VS_EDIT(editor_->setClipEffect({s(selected_)}, cliptools::kStabilizeEffect, Json(), false, "Remove stabilisation"));
}

Json EditorController::stabilizeParams(const QString& clipId) const {
    const Json* p = effectParams(snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr, cliptools::kStabilizeEffect);
    return p ? *p : Json();
}

// ---------------------------------------------------------------- favourites

cliptools::Favorites& EditorController::favoriteStore() const {
    if (!favorites_) {
        const QString file = favoritesFile_.isEmpty()
                                 ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/favorites.json"
                                 : favoritesFile_;
        favorites_ = std::make_unique<cliptools::Favorites>(s(file));
    }
    return *favorites_;
}

void EditorController::setFavoritesFile(const QString& file) {
    favoritesFile_ = file;
    favorites_.reset();
}

bool EditorController::isFavorite(const QString& category, const QString& id) const {
    return favoriteStore().contains(s(category), s(id));
}

void EditorController::toggleFavorite(const QString& category, const QString& id) {
    const bool on = favoriteStore().toggle(s(category), s(id));
    setStatus(on ? tr("Đã thêm vào Yêu thích") : tr("Đã bỏ khỏi Yêu thích"));
    emit favoritesChanged();
}

QStringList EditorController::favorites(const QString& category) const {
    QStringList out;
    for (const auto& id : favoriteStore().list(s(category))) out << q(id);
    return out;
}

// ---------------------------------------------------------------- user presets

namespace {
QVariantMap toVariant(const Json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object().toVariantMap();
}
Json fromVariant(const QVariantMap& m) {
    return Json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString());
}
}  // namespace

presets::UserPresetStore& EditorController::presetStore() const {
    if (!userPresets_) {
        const QString file = userPresetsFile_.isEmpty()
                                 ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/presets.json"
                                 : userPresetsFile_;
        userPresets_ = std::make_unique<presets::UserPresetStore>(s(file));
    }
    return *userPresets_;
}

void EditorController::setUserPresetsFile(const QString& file) {
    userPresetsFile_ = file;
    userPresets_.reset();
}

QVariantList EditorController::userPresets(const QString& kind) const {
    QVariantList out;
    for (const auto& p : presetStore().list(s(kind))) {
        QString summary;
        if (kind == presets::kStyle) summary = q(presets::describeStyle(p.data));
        else if (auto e = presets::userExportFromJson(p.data)) summary = q(presets::describeExport(*e));
        out.push_back(QVariantMap{{"name", q(p.name)}, {"summary", summary}, {"saved", qint64(p.saved)}, {"data", toVariant(p.data)}});
    }
    return out;
}

bool EditorController::saveStylePreset(const QString& clipId, const QString& name) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    if (!c || c->generated()) {
        setStatus(tr("Chọn một clip video hoặc ảnh để lưu preset"));
        return false;
    }
    std::string err;
    if (!presetStore().put(presets::kStyle, s(name), presets::captureStyle(*c), &err)) {
        setStatus(tr("Không lưu được preset: %1").arg(q(err)));
        return false;
    }
    setStatus(tr("Đã lưu preset \"%1\"").arg(name.trimmed()));
    emit userPresetsChanged();
    return true;
}

void EditorController::applyStylePreset(const QStringList& clipIds, const QString& name) {
    const presets::UserPreset* p = presetStore().find(presets::kStyle, s(name));
    if (!p) return setStatus(tr("Không tìm thấy preset"));
    const Json data = p->data;
    const std::string label = "Preset: " + p->name;
    if (VS_EDIT(editor_->applyClipStyle(ids(clipIds), data, label)))
        setStatus(tr("Đã áp dụng preset \"%1\"").arg(name));
}

bool EditorController::saveExportPreset(const QString& name, const QVariantMap& data) {
    std::string err;
    Json j;
    try {
        j = fromVariant(data);
    } catch (const std::exception& e) {
        err = e.what();
    }
    const auto e = err.empty() ? presets::userExportFromJson(j, &err) : std::nullopt;
    if (!e || !presetStore().put(presets::kExport, s(name), presets::toJson(*e), &err)) {
        setStatus(tr("Không lưu được preset: %1").arg(q(err)));
        return false;
    }
    setStatus(tr("Đã lưu preset xuất \"%1\"").arg(name.trimmed()));
    emit userPresetsChanged();
    return true;
}

bool EditorController::removeUserPreset(const QString& kind, const QString& name) {
    std::string err;
    if (!presetStore().remove(s(kind), s(name), &err)) {
        setStatus(q(err));
        return false;
    }
    emit userPresetsChanged();
    return true;
}

bool EditorController::renameUserPreset(const QString& kind, const QString& from, const QString& to) {
    std::string err;
    if (!presetStore().rename(s(kind), s(from), s(to), &err)) {
        setStatus(err.find("already exists") != std::string::npos ? tr("Đã có preset tên này") : q(err));
        return false;
    }
    emit userPresetsChanged();
    return true;
}

QVariantMap EditorController::exportPresetInfo(const QVariantMap& data) const {
    try {
        const auto e = presets::userExportFromJson(fromVariant(data));
        if (!e) return {};
        const ExportSettings es = presets::exportSettingsFor(editor_->project(), *e);
        return {{"width", es.width},
                {"height", es.height},
                {"fps", double(es.fps.num) / es.fps.den},
                {"bitrate", double(es.videoBitrate)},
                {"audio", es.includeAudio},
                {"estimatedBytes", double(estimateExportBytes(es, editor_->project().duration().seconds()))},
                {"summary", q(presets::describeExport(*e))}};
    } catch (const std::exception&) {
        return {};
    }
}

void EditorController::exportWithUserPreset(const QUrl& file, const QString& name) {
    const presets::UserPreset* p = presetStore().find(presets::kExport, s(name));
    const auto e = p ? presets::userExportFromJson(p->data) : std::nullopt;
    if (!e) return setStatus(tr("Không tìm thấy preset xuất"));
    startExport(file, presets::exportSettingsFor(editor_->project(), *e));
}

// ---- plugins (Plugin API beta) ----

void EditorController::loadPlugins(const QString& dir) {
    const auto res = plugins::loadPlugins(modules_, s(dir));
    for (const auto& p : res.plugins)
        for (const auto& fx : p.effects) pluginEffects_.push_back(fx);
    for (const auto& e : res.errors) pluginErrors_ << q(e);
    if (!res.plugins.empty() || !res.errors.empty()) emit toolsChanged();
}

bool EditorController::hasPluginEffect(const QString& clipId, const QString& id) const {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(clipId)) : nullptr;
    if (!c) return false;
    if (effectParams(c, s(id).c_str())) return true;
    const Json a = audiofx::clipAudio(*c);
    if (a.contains("effects") && a["effects"].is_array())
        for (const auto& e : a["effects"])
            if (e.is_object() && e.value("type", std::string{}) == s(id)) return true;
    return false;
}

void EditorController::setPluginEffect(const QStringList& clipIds, const QString& id, bool on) {
    const auto it = std::find_if(pluginEffects_.begin(), pluginEffects_.end(), [&](const auto& fx) { return fx.id == s(id); });
    if (it == pluginEffects_.end() || clipIds.isEmpty()) return;
    if (!it->audio) {
        VS_EDIT(editor_->setClipEffect(ids(clipIds), it->id, on ? it->params : Json(), false,
                                       (on ? "Add " : "Remove ") + it->name));
        return;
    }
    // Audio plugin effects live in the clip's "audio.effects" list (audio_fx.hpp).
    for (const QString& cid : clipIds) {
        const Clip* c = snapshot_ ? snapshot_->findClip(s(cid)) : nullptr;
        if (!c) continue;
        const Json a = audiofx::clipAudio(*c);
        Json list = Json::array();
        if (a.contains("effects") && a["effects"].is_array())
            for (const auto& e : a["effects"])
                if (!(e.is_object() && e.value("type", std::string{}) == it->id)) list.push_back(e);
        if (on) list.push_back(Json{{"type", it->id}, {"params", it->params}});
        VS_EDIT(editor_->setClipAudio({s(cid)}, {{"effects", list.empty() ? Json() : list}}));
    }
}
