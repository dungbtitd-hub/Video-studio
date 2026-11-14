#include "ClipToolsPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

#include "EditorController.h"
#include "SpeedCurveEditor.h"
#include "vstudio/speed_curve.hpp"
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
    // Speed: a log slider (0.1x .. 10x, 1x in the middle) and a ramp.
    auto* spRow = new QHBoxLayout();
    spRow->addWidget(new QLabel("Tốc độ", speedBox));
    speed_ = new QSlider(Qt::Horizontal, speedBox);
    speed_->setObjectName("clipSpeed");
    speed_->setRange(-100, 100);  // 10^(v/100)
    speed_->setValue(0);
    spRow->addWidget(speed_, 1);
    speedLabel_ = new QLabel("1.00x", speedBox);
    speedLabel_->setMinimumWidth(92);
    spRow->addWidget(speedLabel_);
    sl->addLayout(spRow);
    auto* cvRow = new QHBoxLayout();
    cvRow->addWidget(new QLabel("Đường cong", speedBox));
    curveList_ = new QComboBox(speedBox);
    curveList_->setObjectName("speedCurve");
    curveList_->addItem("Không (tốc độ đều)", QString());
    for (const auto& pr : vstudio::speed::curvePresets())
        curveList_->addItem(QString::fromStdString(pr.name), QString::fromStdString(pr.id));
    curveList_->addItem("Tự vẽ", QString("custom"));
    cvRow->addWidget(curveList_, 1);
    sl->addLayout(cvRow);
    curve_ = new SpeedCurveEditor(speedBox);
    curve_->setVisible(false);
    sl->addWidget(curve_);
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

    // Plugin API (beta): effects from installed plugins; hidden when none is installed.
    pluginBox_ = new QGroupBox("Hiệu ứng plugin (beta)", body_);
    auto* pl = new QVBoxLayout(pluginBox_);
    pluginList_ = new QComboBox(pluginBox_);
    pluginList_->setObjectName("pluginEffects");
    pluginOn_ = new QCheckBox("Áp dụng cho clip", pluginBox_);
    pluginOn_->setObjectName("pluginOn");
    pluginNote_ = new QLabel(pluginBox_);
    pluginNote_->setWordWrap(true);
    pluginNote_->setStyleSheet("color: #a4abb8; font-size: 11px;");
    pl->addWidget(pluginList_);
    pl->addWidget(pluginOn_);
    pl->addWidget(pluginNote_);
    bl->addWidget(pluginBox_);
    l->addWidget(body_);
    l->addStretch(1);

    connect(reverse_, &QCheckBox::toggled, this, [this](bool on) {
        if (!editor_->selectedClip().isEmpty()) editor_->setReversed({editor_->selectedClip()}, on);
    });
    auto speedOf = [this] { return std::pow(10.0, speed_->value() / 100.0); };
    connect(speed_, &QSlider::valueChanged, this, [this, speedOf] { speedLabel_->setText(QString("%1x").arg(speedOf(), 0, 'f', 2)); });
    connect(speed_, &QSlider::sliderReleased, this, [this] {
        commitSpeed(curve_->isVisible() ? curve_->points() : QVector<QPointF>{}, curveList_->currentData().toString());
    });
    connect(curveList_, &QComboBox::activated, this, [this](int) {
        const QString id = curveList_->currentData().toString();
        QVector<QPointF> pts;
        if (id == "custom") {
            pts = curve_->points();
            if (pts.size() < 3) pts = {QPointF(0, 1), QPointF(0.5, 1), QPointF(1, 1)};
        } else if (const auto* pr = vstudio::speed::findCurvePreset(id.toStdString())) {
            for (const auto& q : pr->points) pts.push_back(QPointF(q.u, q.v));
        }
        commitSpeed(pts, id == "custom" ? QString() : id);
    });
    connect(curve_, &SpeedCurveEditor::edited, this, [this](const QVector<QPointF>& pts) { commitSpeed(pts, {}); });
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
    connect(pluginList_, &QComboBox::currentIndexChanged, this, &ClipToolsPanel::refresh);
    connect(pluginOn_, &QCheckBox::toggled, this, [this](bool on) {
        const QString sel = editor_->selectedClip();
        if (!sel.isEmpty() && pluginList_->currentIndex() >= 0)
            editor_->setPluginEffect({sel}, pluginList_->currentData().toString(), on);
    });
    connect(editor_, &EditorController::selectionChanged, this, &ClipToolsPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &ClipToolsPanel::refresh);
    connect(editor_, &EditorController::toolsChanged, this, &ClipToolsPanel::refresh);
    refresh();
}

void ClipToolsPanel::commitSpeed(const QVector<QPointF>& points, const QString& preset) {
    const QString sel = editor_->selectedClip();
    if (sel.isEmpty()) return;
    editor_->setClipSpeed({sel}, std::pow(10.0, speed_->value() / 100.0), points, preset);
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
    const auto& fx = editor_->pluginEffects();
    if (int(fx.size()) != pluginCount_) {
        QSignalBlocker bp(pluginList_);
        pluginList_->clear();
        for (const auto& e : fx)
            pluginList_->addItem(QString::fromStdString(e.name) + (e.audio ? "  (âm thanh)" : ""), QString::fromStdString(e.id));
        pluginCount_ = int(fx.size());
    }
    pluginBox_->setVisible(!fx.empty() || !editor_->pluginErrors().isEmpty());
    pluginList_->setEnabled(!fx.empty());
    pluginNote_->setText(editor_->pluginErrors().isEmpty() ? "Plugin chạy trong ứng dụng: chỉ cài plugin bạn tin cậy."
                                                           : "Không nạp được: " + editor_->pluginErrors().join("; "));
    {
        QSignalBlocker bo(pluginOn_);
        pluginOn_->setEnabled(video && pluginList_->currentIndex() >= 0);
        pluginOn_->setChecked(video && pluginList_->currentIndex() >= 0 &&
                              editor_->hasPluginEffect(sel, pluginList_->currentData().toString()));
    }
    if (!video) return;
    QSignalBlocker b1(reverse_), b2(chroma_), b3(similarity_), b4(smoothness_), b5(spill_), b6(stabStrength_);
    const bool isVideo = a->type == vstudio::MediaType::Video;
    reverse_->setEnabled(isVideo);
    freeze_->setEnabled(isVideo && !vstudio::cliptools::isFreeze(*c));
    reverse_->setChecked(c->reversed);
    const QVariantMap sp = editor_->clipSpeed(sel);
    const bool canSpeed = sp.value("can").toBool();
    for (QWidget* w : std::initializer_list<QWidget*>{speed_, curveList_, curve_}) w->setEnabled(canSpeed);
    {
        QSignalBlocker bs(speed_), bc(curveList_);
        if (!speed_->isSliderDown()) speed_->setValue(int(std::lround(std::log10(sp.value("speed", 1.0).toDouble()) * 100)));
        QVector<QPointF> pts;
        for (const QVariant& v : sp.value("points").toList()) pts.push_back(v.toPointF());
        const QString preset = sp.value("preset").toString();
        const int idx = pts.isEmpty() ? 0 : preset.isEmpty() ? curveList_->count() - 1 : curveList_->findData(preset);
        curveList_->setCurrentIndex(std::max(0, idx));
        curve_->setVisible(!pts.isEmpty());
        if (!pts.isEmpty()) curve_->setPoints(pts);
        speedLabel_->setText(QString("%1x · %2 s").arg(sp.value("speed", 1.0).toDouble(), 0, 'f', 2)
                                 .arg(sp.value("seconds").toDouble(), 0, 'f', 1));
    }
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
