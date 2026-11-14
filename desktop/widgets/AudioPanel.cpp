#include "AudioPanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "EditorController.h"
#include "vstudio/audio_fx.hpp"

using vstudio::Json;
namespace afx = vstudio::audiofx;

namespace {
QSlider* slider(int lo, int hi, int value, QWidget* parent, Qt::Orientation o = Qt::Horizontal) {
    auto* s = new QSlider(o, parent);
    s->setRange(lo, hi);
    s->setValue(value);
    return s;
}
const char* kBandNames[5] = {"80", "250", "1k", "4k", "10k"};
}  // namespace

AudioPanel::AudioPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    hint_ = new QLabel("Chọn một clip có âm thanh trên timeline.", this);
    hint_->setWordWrap(true);
    l->addWidget(hint_);
    body_ = new QWidget(this);
    auto* bl = new QVBoxLayout(body_);
    bl->setContentsMargins(0, 0, 0, 0);

    auto* basic = new QFormLayout();
    volume_ = slider(0, 400, 100, body_);
    volume_->setObjectName("audioVolume");
    fadeIn_ = slider(0, 100, 0, body_);  // tenths of a second, up to 10 s
    fadeIn_->setObjectName("audioFadeIn");
    fadeOut_ = slider(0, 100, 0, body_);
    fadeOut_->setObjectName("audioFadeOut");
    basic->addRow("Âm lượng", volume_);
    basic->addRow("Hiện dần", fadeIn_);
    basic->addRow("Tắt dần", fadeOut_);
    bl->addLayout(basic);

    auto* eqBox = new QGroupBox("Bộ cân bằng (EQ)", body_);
    auto* el = new QVBoxLayout(eqBox);
    eqPreset_ = new QComboBox(eqBox);
    eqPreset_->setObjectName("eqPreset");
    for (const auto& p : afx::eqPresets()) eqPreset_->addItem(QString::fromStdString(p.name), QString::fromStdString(p.id));
    eqPreset_->addItem("Tùy chỉnh", QString("custom"));
    el->addWidget(eqPreset_);
    auto* bandRow = new QHBoxLayout();
    for (int i = 0; i < 5; ++i) {
        auto* col = new QVBoxLayout();
        bands_[i] = slider(-12, 12, 0, eqBox, Qt::Vertical);
        bands_[i]->setMinimumHeight(90);
        bands_[i]->setObjectName(QString("eqBand%1").arg(i));
        col->addWidget(bands_[i], 1, Qt::AlignHCenter);
        auto* lab = new QLabel(kBandNames[i], eqBox);
        lab->setAlignment(Qt::AlignCenter);
        lab->setStyleSheet("color: #a4abb8; font-size: 10px;");
        col->addWidget(lab);
        bandRow->addLayout(col);
    }
    el->addLayout(bandRow);
    bl->addWidget(eqBox);

    auto* nrBox = new QGroupBox("Khử tiếng ồn", body_);
    auto* nl = new QHBoxLayout(nrBox);
    denoise_ = slider(0, 100, 60, nrBox);
    denoise_->setObjectName("denoiseAmount");
    denoiseBtn_ = new QPushButton("Khử ồn", nrBox);
    denoiseBtn_->setToolTip("Đo tiếng ồn nền của clip (chạy nền) rồi giảm nó đi");
    nl->addWidget(denoise_, 1);
    nl->addWidget(denoiseBtn_);
    bl->addWidget(nrBox);

    detach_ = new QPushButton("Tách âm thanh ra track riêng", body_);
    detach_->setObjectName("detachAudio");
    bl->addWidget(detach_);
    l->addWidget(body_);

    auto* voBox = new QGroupBox("Thu âm (voice-over)", this);
    auto* vl = new QVBoxLayout(voBox);
    record_ = new QPushButton("● Ghi âm tại playhead", voBox);
    record_->setObjectName("recordVoice");
    recordHint_ = new QLabel("Video phát trong lúc ghi; bản ghi nằm trên track \"Thu âm\".", voBox);
    recordHint_->setWordWrap(true);
    recordHint_->setStyleSheet("color: #a4abb8; font-size: 11px;");
    vl->addWidget(record_);
    vl->addWidget(recordHint_);
    l->addWidget(voBox);
    l->addStretch(1);

    connect(volume_, &QSlider::sliderReleased, this, [this] {
        if (!editor_->selectedClip().isEmpty()) editor_->setClipVolume(editor_->selectedClip(), volume_->value() / 100.0);
    });
    for (QSlider* s : {fadeIn_, fadeOut_}) connect(s, &QSlider::sliderReleased, this, &AudioPanel::commitFades);
    connect(eqPreset_, &QComboBox::activated, this, [this] {
        const QString id = eqPreset_->currentData().toString();
        if (id == "custom" || editor_->selectedClip().isEmpty()) return;
        editor_->setClipAudio({editor_->selectedClip()}, {{"eq", id == "flat" ? Json() : Json{{"preset", id.toStdString()}}}});
    });
    for (QSlider* b : bands_) connect(b, &QSlider::sliderReleased, this, &AudioPanel::commitEq);
    connect(denoiseBtn_, &QPushButton::clicked, this, [this] {
        const Json a = editor_->clipAudio(editor_->selectedClip());
        const bool on = a.contains("denoise");
        if (on) editor_->setClipAudio({editor_->selectedClip()}, {{"denoise", nullptr}});
        else editor_->analyzeNoise(denoise_->value() / 100.0);
    });
    connect(denoise_, &QSlider::sliderReleased, this, [this] {
        const Json a = editor_->clipAudio(editor_->selectedClip());
        if (!a.contains("denoise")) return;
        Json d = a["denoise"];
        d["amount"] = denoise_->value() / 100.0;
        editor_->setClipAudio({editor_->selectedClip()}, {{"denoise", d}});
    });
    connect(detach_, &QPushButton::clicked, editor_, &EditorController::detachAudio);
    connect(record_, &QPushButton::clicked, this, [this] {
        if (editor_->recordingVoice()) editor_->stopVoiceOver();
        else editor_->startVoiceOver();
    });
    connect(editor_, &EditorController::selectionChanged, this, &AudioPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &AudioPanel::refresh);
    connect(editor_, &EditorController::toolsChanged, this, &AudioPanel::refresh);
    refresh();
}

void AudioPanel::commitFades() {
    const QString sel = editor_->selectedClip();
    if (sel.isEmpty()) return;
    editor_->setClipAudio({sel}, {{"fade_in", fadeIn_->value() / 10.0}, {"fade_out", fadeOut_->value() / 10.0}});
}

void AudioPanel::commitEq() {
    const QString sel = editor_->selectedClip();
    if (sel.isEmpty()) return;
    Json gains = Json::array();
    bool flat = true;
    for (QSlider* b : bands_) {
        gains.push_back(b->value());
        flat = flat && b->value() == 0;
    }
    editor_->setClipAudio({sel}, {{"eq", flat ? Json() : Json{{"gains", gains}}}});
}

void AudioPanel::refresh() {
    const QString sel = editor_->selectedClip();
    const auto proj = editor_->project();
    const vstudio::Clip* c = proj ? proj->findClip(sel.toStdString()) : nullptr;
    const vstudio::MediaAsset* a = c ? proj->findAsset(c->assetId) : nullptr;
    const bool has = c && a && a->audio;
    body_->setEnabled(has);
    hint_->setVisible(!has);
    record_->setText(editor_->recordingVoice() ? "■ Dừng ghi âm" : "● Ghi âm tại playhead");
    if (!editor_->voiceOverError().isEmpty()) recordHint_->setText(editor_->voiceOverError());
    if (!has) return;
    const Json au = afx::clipAudio(*c);
    QSignalBlocker b1(volume_), b2(fadeIn_), b3(fadeOut_), b4(eqPreset_), b5(denoise_);
    volume_->setValue(int(c->volume * 100 + 0.5));
    fadeIn_->setValue(int(au.value("fade_in", 0.0) * 10 + 0.5));
    fadeOut_->setValue(int(au.value("fade_out", 0.0) * 10 + 0.5));
    std::array<double, 5> g{};
    QString preset = "flat";
    if (au.contains("eq") && au["eq"].is_object()) {
        const Json& eq = au["eq"];
        if (eq.contains("gains")) {
            preset = "custom";
            for (std::size_t i = 0; i < 5 && i < eq["gains"].size(); ++i) g[i] = eq["gains"][i].get<double>();
        } else if (const auto* p = afx::findEqPreset(eq.value("preset", std::string{}))) {
            preset = QString::fromStdString(p->id);
            g = p->gains;
        }
    }
    eqPreset_->setCurrentIndex(std::max(0, eqPreset_->findData(preset)));
    for (int i = 0; i < 5; ++i) {
        QSignalBlocker bb(bands_[i]);
        bands_[i]->setValue(int(std::lround(g[std::size_t(i)])));
    }
    const bool dn = au.contains("denoise");
    if (dn) denoise_->setValue(int(au["denoise"].value("amount", 0.6) * 100 + 0.5));
    denoiseBtn_->setEnabled(!editor_->analyzingNoise());
    denoiseBtn_->setText(editor_->analyzingNoise() ? "Đang đo…" : dn ? "Bỏ khử ồn" : "Khử ồn");
    detach_->setEnabled(!afx::detached(*c) && proj->trackOfClip(c->id)->type == vstudio::TrackType::Video);
    detach_->setText(afx::detached(*c) ? "Âm thanh đã tách ra track riêng" : "Tách âm thanh ra track riêng");
}
