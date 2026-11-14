#include "ClipToolsPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "EditorController.h"
#include "vstudio/clip_tools.hpp"

using vstudio::Json;

namespace {
QSlider* slider(int value, QWidget* parent) {
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(0, 100);
    s->setValue(value);
    return s;
}
}  // namespace

ClipToolsPanel::ClipToolsPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    hint_ = new QLabel("Chọn một clip video trên timeline.", this);
    hint_->setWordWrap(true);
    l->addWidget(hint_);
    body_ = new QWidget(this);
    auto* bl = new QVBoxLayout(body_);
    bl->setContentsMargins(0, 0, 0, 0);

    auto* speedBox = new QGroupBox("Thời gian", body_);
    auto* sl = new QVBoxLayout(speedBox);
    reverse_ = new QCheckBox("Phát ngược (reverse)", speedBox);
    reverse_->setObjectName("reverseClip");
    sl->addWidget(reverse_);
    auto* fr = new QHBoxLayout();
    freeze_ = new QPushButton("Đóng băng khung tại playhead", speedBox);
    freeze_->setObjectName("freezeFrame");
    freezeSecs_ = new QDoubleSpinBox(speedBox);
    freezeSecs_->setRange(0.2, 30);
    freezeSecs_->setValue(3);
    freezeSecs_->setSuffix(" s");
    fr->addWidget(freeze_, 1);
    fr->addWidget(freezeSecs_);
    sl->addLayout(fr);
    bl->addWidget(speedBox);

    auto* keyBox = new QGroupBox("Tách nền màu (chroma key)", body_);
    auto* kl = new QFormLayout(keyBox);
    chroma_ = new QCheckBox("Bật", keyBox);
    chroma_->setObjectName("chromaKey");
    keyColor_ = new QPushButton(keyBox);
    keyColor_->setToolTip("Màu nền cần xóa (nhấp để chọn; nút Tự nhận lấy màu ở viền khung hình)");
    auto* autoKey = new QPushButton("Tự nhận", keyBox);
    auto* colorRow = new QHBoxLayout();
    colorRow->addWidget(keyColor_, 1);
    colorRow->addWidget(autoKey);
    similarity_ = slider(40, keyBox);
    smoothness_ = slider(10, keyBox);
    spill_ = slider(50, keyBox);
    kl->addRow(chroma_);
    kl->addRow("Màu nền", colorRow);
    kl->addRow("Độ tương đồng", similarity_);
    kl->addRow("Làm mềm viền", smoothness_);
    kl->addRow("Khử ám màu", spill_);
    bl->addWidget(keyBox);

    auto* stabBox = new QGroupBox("Chống rung", body_);
    auto* stl = new QFormLayout(stabBox);
    stabStrength_ = slider(70, stabBox);
    stab_ = new QPushButton("Chống rung", stabBox);
    stab_->setObjectName("stabilize");
    stabRemove_ = new QPushButton("Bỏ", stabBox);
    stabProgress_ = new QProgressBar(stabBox);
    stabProgress_->setRange(0, 100);
    stabProgress_->setVisible(false);
    auto* stRow = new QHBoxLayout();
    stRow->addWidget(stab_, 1);
    stRow->addWidget(stabRemove_);
    stl->addRow("Mức độ", stabStrength_);
    stl->addRow(stRow);
    stl->addRow(stabProgress_);
    bl->addWidget(stabBox);
    l->addWidget(body_);
    l->addStretch(1);

    connect(reverse_, &QCheckBox::toggled, this, [this](bool on) {
        if (!editor_->selectedClip().isEmpty()) editor_->setReversed({editor_->selectedClip()}, on);
    });
    connect(freeze_, &QPushButton::clicked, this, [this] { editor_->freezeFrame(freezeSecs_->value()); });
    connect(chroma_, &QCheckBox::toggled, this, &ClipToolsPanel::commitChroma);
    for (QSlider* s : {similarity_, smoothness_, spill_}) connect(s, &QSlider::sliderReleased, this, &ClipToolsPanel::commitChroma);
    connect(keyColor_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor(color_), this, "Màu nền");
        if (!c.isValid()) return;
        color_ = c.name();
        chroma_->setChecked(true);
        commitChroma();
    });
    connect(autoKey, &QPushButton::clicked, this, [this] {
        color_ = editor_->guessKeyColor();
        if (chroma_->isChecked()) commitChroma();
        else chroma_->setChecked(true);
    });
    connect(stab_, &QPushButton::clicked, this, [this] {
        if (editor_->stabilizing()) editor_->cancelStabilize();
        else editor_->stabilize(stabStrength_->value() / 100.0);
    });
    connect(stabRemove_, &QPushButton::clicked, editor_, &EditorController::removeStabilize);
    connect(stabStrength_, &QSlider::sliderReleased, this, [this] { editor_->setStabilizeStrength(stabStrength_->value() / 100.0); });
    connect(editor_, &EditorController::selectionChanged, this, &ClipToolsPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &ClipToolsPanel::refresh);
    connect(editor_, &EditorController::toolsChanged, this, &ClipToolsPanel::refresh);
    refresh();
}

void ClipToolsPanel::commitChroma() {
    const QString sel = editor_->selectedClip();
    if (sel.isEmpty()) return;
    if (!chroma_->isChecked()) {
        if (!editor_->chromaKey(sel).is_null()) editor_->setChromaKey({sel}, Json());
        return;
    }
    editor_->setChromaKey({sel}, Json{{"color", color_.toStdString()},
                                      {"similarity", similarity_->value() / 100.0},
                                      {"smoothness", smoothness_->value() / 100.0},
                                      {"spill", spill_->value() / 100.0}});
}

void ClipToolsPanel::refresh() {
    const QString sel = editor_->selectedClip();
    const auto proj = editor_->project();
    const vstudio::Clip* c = proj ? proj->findClip(sel.toStdString()) : nullptr;
    const vstudio::MediaAsset* a = c ? proj->findAsset(c->assetId) : nullptr;
    const bool video = c && a && !c->generated();
    body_->setEnabled(video);
    hint_->setVisible(!video);
    const bool busy = editor_->stabilizing();
    stab_->setText(busy ? "Hủy" : "Chống rung");
    stabProgress_->setVisible(busy);
    stabProgress_->setValue(int(editor_->stabilizeProgress() * 100));
    if (!video) return;
    QSignalBlocker b1(reverse_), b2(chroma_), b3(similarity_), b4(smoothness_), b5(spill_), b6(stabStrength_);
    const bool isVideo = a->type == vstudio::MediaType::Video;
    reverse_->setEnabled(isVideo);
    freeze_->setEnabled(isVideo && !vstudio::cliptools::isFreeze(*c));
    reverse_->setChecked(c->reversed);
    const Json key = editor_->chromaKey(sel);
    chroma_->setChecked(!key.is_null());
    if (!key.is_null()) {
        color_ = QString::fromStdString(key.value("color", std::string("#00ff00")));
        similarity_->setValue(int(key.value("similarity", 0.4) * 100 + 0.5));
        smoothness_->setValue(int(key.value("smoothness", 0.1) * 100 + 0.5));
        spill_->setValue(int(key.value("spill", 0.5) * 100 + 0.5));
    }
    keyColor_->setText(color_);
    keyColor_->setStyleSheet(QString("background: %1; color: %2;").arg(color_, QColor(color_).lightness() > 128 ? "#000" : "#fff"));
    const Json st = editor_->stabilizeParams(sel);
    stabRemove_->setEnabled(!st.is_null());
    stab_->setEnabled(isVideo || busy);
    if (!st.is_null()) stabStrength_->setValue(int(st.value("strength", 0.7) * 100 + 0.5));
}
