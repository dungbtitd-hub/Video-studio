#include "MixerPanel.h"

#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include "EditorController.h"
#include "vstudio/audio_fx.hpp"

MixerPanel::MixerPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    box_ = new QWidget(scroll);
    grid_ = new QGridLayout(box_);
    grid_->setColumnStretch(1, 2);
    grid_->setColumnStretch(2, 1);
    scroll->setWidget(box_);
    l->addWidget(scroll);
    connect(editor_, &EditorController::projectChanged, this, &MixerPanel::rebuild);
    rebuild();
}

void MixerPanel::rebuild() {
    const auto p = editor_->project();
    if (!p) return;
    // Rebuild only when something shown changed (not on every clip edit).
    QString sig;
    for (const auto& t : p->tracks)
        sig += QString::fromStdString(t.id + t.name) + QString("|%1|%2|%3|%4|%5;").arg(t.volume).arg(t.muted).arg(
                   vstudio::audiofx::trackPan(t)).arg(vstudio::audiofx::trackSolo(t)).arg(int(t.type));
    if (sig == signature_) return;
    signature_ = sig;
    while (QLayoutItem* it = grid_->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    strips_ = 0;
    int row = 0;
    auto head = [&](const QString& text, int col) {
        auto* lab = new QLabel(text, box_);
        lab->setStyleSheet("color: #a4abb8; font-size: 11px;");
        grid_->addWidget(lab, row, col);
    };
    head("Track", 0), head("Âm lượng", 1), head("Cân bằng L–R", 2);
    ++row;
    for (const auto& t : p->tracks) {
        const bool sound = t.type == vstudio::TrackType::Audio ||
                           std::any_of(t.clips.begin(), t.clips.end(), [&](const vstudio::Clip& c) {
                               const auto* a = p->findAsset(c.assetId);
                               return a && a->audio;
                           });
        if (!sound) continue;
        const QString id = QString::fromStdString(t.id);
        auto* name = new QLabel(QString::fromStdString(t.name), box_);
        grid_->addWidget(name, row, 0);
        auto* vol = new QSlider(Qt::Horizontal, box_);
        vol->setRange(0, 200);
        vol->setValue(int(t.volume * 100 + 0.5));
        vol->setToolTip("Âm lượng track (%)");
        grid_->addWidget(vol, row, 1);
        auto* pan = new QSlider(Qt::Horizontal, box_);
        pan->setRange(-100, 100);
        pan->setValue(int(vstudio::audiofx::trackPan(t) * 100));
        pan->setToolTip("Trái ← → Phải (nhấp đúp để về giữa)");
        grid_->addWidget(pan, row, 2);
        auto* mute = new QToolButton(box_);
        mute->setText("M");
        mute->setCheckable(true);
        mute->setChecked(t.muted);
        mute->setToolTip("Tắt tiếng");
        grid_->addWidget(mute, row, 3);
        auto* solo = new QToolButton(box_);
        solo->setText("S");
        solo->setCheckable(true);
        solo->setChecked(vstudio::audiofx::trackSolo(t));
        solo->setToolTip("Chỉ nghe track này");
        grid_->addWidget(solo, row, 4);
        const bool enabled = t.enabled, locked = t.locked;
        connect(vol, &QSlider::sliderReleased, this, [this, id, vol] { editor_->setTrackVolume(id, vol->value() / 100.0); });
        connect(pan, &QSlider::sliderReleased, this, [this, id, pan, solo] { editor_->setTrackMix(id, pan->value() / 100.0, solo->isChecked()); });
        connect(mute, &QToolButton::toggled, this, [this, id, enabled, locked](bool on) { editor_->setTrackFlags(id, enabled, locked, on); });
        connect(solo, &QToolButton::toggled, this, [this, id, pan](bool on) { editor_->setTrackMix(id, pan->value() / 100.0, on); });
        ++row;
        ++strips_;
    }
    if (strips_ == 0) {
        auto* none = new QLabel("Chưa có track nào có âm thanh.", box_);
        grid_->addWidget(none, row++, 0, 1, 5);
    }
    grid_->setRowStretch(row, 1);
}
