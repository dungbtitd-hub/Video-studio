#include "GradePanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "EditorController.h"
#include "GradeWidgets.h"
#include "vstudio/grade.hpp"

using vstudio::Json;

namespace {

const char* kWheelKeys[4] = {"lift", "gamma", "gain", "offset"};
const char* kWheelNames[4] = {"Vùng tối (Lift)", "Trung tính (Gamma)", "Vùng sáng (Gain)", "Tổng thể (Offset)"};

struct Channel {
    const char* key;
    const char* name;
    QColor color;
};
const Channel kChannels[4] = {{"master", "Tổng", QColor(230, 230, 230)},
                              {"r", "Đỏ", QColor(235, 90, 80)},
                              {"g", "Lục", QColor(90, 200, 110)},
                              {"b", "Lam", QColor(90, 140, 240)}};

QWidget* scrollable(QWidget* content) {
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(content);
    return sa;
}

}  // namespace

GradePanel::GradePanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    l->addWidget(hint_);
    tabs_ = new QTabWidget(this);
    l->addWidget(tabs_, 1);

    // ---- wheels
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        auto* grid = new QGridLayout;
        for (int i = 0; i < 4; ++i) {
            wheels_[i] = new ColorWheel(kWheelNames[i], page);
            grid->addWidget(wheels_[i], i / 2, i % 2);
            connect(wheels_[i], &ColorWheel::changed, this, &GradePanel::changed);
            connect(wheels_[i], &ColorWheel::committed, this, &GradePanel::commit);
        }
        v->addLayout(grid);
        auto* g = new QGridLayout;
        addSlider(page, g, 0, "Tương phản", "contrast", 0, 200, 100, 1.0);
        addSlider(page, g, 1, "Điểm tựa", "pivot", 5, 95, 100, 0.435);
        addSlider(page, g, 2, "Bão hoà", "saturation", 0, 200, 100, 1.0);
        v->addLayout(g);
        v->addStretch();
        tabs_->addTab(scrollable(page), "Bánh xe màu");
    }
    // ---- curves
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        auto* row = new QHBoxLayout;
        channel_ = new QButtonGroup(this);
        for (int i = 0; i < 4; ++i) {
            auto* b = new QPushButton(kChannels[i].name, page);
            b->setCheckable(true);
            b->setChecked(i == 0);
            b->setStyleSheet(QString("QPushButton:checked { border-color: %1; color: %1; }").arg(kChannels[i].color.name()));
            channel_->addButton(b, i);
            row->addWidget(b);
        }
        v->addLayout(row);
        curve_ = new CurveEditor(page);
        v->addWidget(curve_, 1);
        auto* resetCurve = new QPushButton("Đặt lại đường cong này", page);
        v->addWidget(resetCurve);
        connect(channel_, &QButtonGroup::idClicked, this, [this] { loadCurve(); });
        connect(curve_, &CurveEditor::changed, this, &GradePanel::changed);
        connect(curve_, &CurveEditor::committed, this, &GradePanel::commit);
        connect(resetCurve, &QPushButton::clicked, this, [this] {
            curve_->setPoints({{0, 0}, {1, 1}});
            changed();
            commit();
        });
        tabs_->addTab(page, "Đường cong");
    }
    // ---- HSL mixer
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        auto* bands = new QGridLayout;
        band_ = new QButtonGroup(this);
        const auto& list = vstudio::hslBands();
        for (int i = 0; i < int(list.size()); ++i) {
            auto* b = new QPushButton(QString::fromStdString(list[i].name), page);
            b->setCheckable(true);
            b->setChecked(i == 0);
            const QColor c = QColor::fromHsvF(float(list[i].hue / 360.0), 0.8f, 0.9f);
            b->setStyleSheet(QString("QPushButton { border-left: 6px solid %1; } QPushButton:checked { border-color: %1; }").arg(c.name()));
            band_->addButton(b, i);
            bands->addWidget(b, i / 4, i % 4);
        }
        v->addLayout(bands);
        auto* g = new QGridLayout;
        addSlider(page, g, 0, "Sắc độ", "hsl_hue", -100, 100, 100, 0);
        addSlider(page, g, 1, "Bão hoà", "hsl_sat", -100, 100, 100, 0);
        addSlider(page, g, 2, "Độ sáng", "hsl_lum", -100, 100, 100, 0);
        v->addLayout(g);
        auto* note = new QLabel("Chỉ đổi màu đã chọn, vd. da (Cam), trời (Xanh dương), lá (Xanh lá). Màu xám không bị ảnh hưởng.", page);
        note->setWordWrap(true);
        note->setStyleSheet("color: #6e7685; font-size: 11px;");
        v->addWidget(note);
        v->addStretch();
        connect(band_, &QButtonGroup::idClicked, this, [this] { loadBand(); });
        tabs_->addTab(scrollable(page), "HSL");
    }
    // ---- qualifier
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        auto* top = new QHBoxLayout;
        qualOn_ = new QCheckBox("Bật chọn vùng màu", page);
        pick_ = new QPushButton("Lấy màu…", page);
        pick_->setCheckable(true);
        pick_->setToolTip("Nhấp vào khung hình để chọn màu cần chỉnh (vd. da, bầu trời).");
        top->addWidget(qualOn_, 1);
        top->addWidget(pick_);
        v->addLayout(top);
        auto* g = new QGridLayout;
        int r = 0;
        g->addWidget(new QLabel("<b>Chọn</b>", page), r++, 0, 1, 3);
        addSlider(page, g, r++, "Màu (độ)", "q_hue", 0, 359, 1, 30);
        addSlider(page, g, r++, "Độ rộng màu", "q_width", 5, 360, 1, 40);
        addSlider(page, g, r++, "Bão hoà từ", "q_sat_lo", 0, 100, 100, 0.15);
        addSlider(page, g, r++, "Bão hoà đến", "q_sat_hi", 0, 100, 100, 1.0);
        addSlider(page, g, r++, "Độ sáng từ", "q_lum_lo", 0, 100, 100, 0.0);
        addSlider(page, g, r++, "Độ sáng đến", "q_lum_hi", 0, 100, 100, 1.0);
        addSlider(page, g, r++, "Độ mềm biên", "q_soft", 0, 50, 100, 0.1);
        g->addWidget(new QLabel("<b>Chỉnh vùng đã chọn</b>", page), r++, 0, 1, 3);
        addSlider(page, g, r++, "Sắc độ", "q_hue_shift", -100, 100, 100, 0);
        addSlider(page, g, r++, "Bão hoà", "q_sat", -100, 100, 100, 0);
        addSlider(page, g, r++, "Độ sáng", "q_exp", -100, 100, 100, 0);
        v->addLayout(g);
        invert_ = new QCheckBox("Đảo vùng chọn (chỉnh mọi thứ trừ màu này)", page);
        showMask_ = new QCheckBox("Hiện vùng chọn trên khung hình (trắng = được chọn)", page);
        v->addWidget(invert_);
        v->addWidget(showMask_);
        v->addStretch();
        for (QCheckBox* c : {qualOn_, invert_, showMask_})
            connect(c, &QCheckBox::toggled, this, [this] {
                changed();
                commit();
            });
        connect(pick_, &QPushButton::toggled, this, &GradePanel::pickRequested);
        tabs_->addTab(scrollable(page), "Vùng màu");
    }

    auto* buttons = new QHBoxLayout;
    compare_ = new QPushButton("Xem gốc", this);
    compare_->setToolTip("Giữ để xem clip trước khi chỉnh màu.");
    reset_ = new QPushButton("Đặt lại", this);
    applyAll_ = new QPushButton("Áp dụng cho tất cả", this);
    applyAll_->setToolTip("Chép bản chỉnh màu này sang mọi clip video và ảnh (một bước hoàn tác).");
    buttons->addWidget(compare_);
    buttons->addWidget(reset_);
    buttons->addWidget(applyAll_);
    l->addLayout(buttons);
    connect(compare_, &QPushButton::pressed, this, [this] {
        if (!clip_.isEmpty()) editor_->previewGrade(clip_, Json());
    });
    connect(compare_, &QPushButton::released, this, [this] { editor_->clearGradePreview(); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (!clip_.isEmpty()) editor_->setClipGrade({clip_}, Json());
    });
    connect(applyAll_, &QPushButton::clicked, this, [this] {
        const QStringList all = editor_->lookableClips();
        if (!all.isEmpty()) editor_->setClipGrade(all, params_);
    });

    connect(editor_, &EditorController::selectionChanged, this, &GradePanel::load);
    connect(editor_, &EditorController::projectChanged, this, [this] {
        // An undo or another panel changed the clip: show it, unless the user is mid-drag.
        for (auto* w : wheels_)
            if (w->dragging()) return;
        if (!curve_->dragging()) load();
    });
    load();
}

GradePanel::Slider GradePanel::addSlider(QWidget* parent, QGridLayout* grid, int row, const QString& label,
                                         const QString& key, int min, int max, double scale, double def) {
    Slider s;
    s.scale = scale;
    s.def = def;
    s.s = new QSlider(Qt::Horizontal, parent);
    s.s->setRange(min, max);
    s.s->setValue(int(std::lround(def * scale)));
    s.value = new QLabel(parent);
    s.value->setMinimumWidth(36);
    s.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* name = new QLabel(label, parent);
    grid->addWidget(name, row, 0);
    grid->addWidget(s.s, row, 1);
    grid->addWidget(s.value, row, 2);
    auto show = [v = s.value, sl = s.s] { v->setText(QString::number(sl->value())); };
    show();
    connect(s.s, &QSlider::valueChanged, this, [this, show, sl = s.s] {
        show();
        changed();
        if (!sl->isSliderDown()) commit();  // keyboard / mouse wheel steps
    });
    connect(s.s, &QSlider::sliderReleased, this, &GradePanel::commit);
    sliders_.insert(key, s);
    return s;
}

QSlider* GradePanel::slider(const QString& name) const { return sliders_.value(name).s; }

QString GradePanel::curveKey() const { return kChannels[std::max(0, channel_->checkedId())].key; }

void GradePanel::loadCurve() {
    const int ch = std::max(0, channel_->checkedId());
    curve_->setColor(kChannels[ch].color);
    QVector<QPointF> pts;
    if (auto c = params_.find("curves"); c != params_.end() && c->is_object())
        if (auto k = c->find(curveKey().toStdString()); k != c->end() && k->is_array())
            for (const auto& p : *k)
                if (p.is_array() && p.size() == 2) pts.push_back({p[0].get<double>(), p[1].get<double>()});
    if (pts.isEmpty() || pts.front().x() > 0) pts.prepend({0, 0});
    if (pts.back().x() < 1) pts.push_back({1, 1});
    curve_->setPoints(pts);
}

void GradePanel::loadBand() {
    const std::string band = vstudio::hslBands()[std::max(0, band_->checkedId())].id;
    double v[3] = {0, 0, 0};
    if (auto h = params_.find("hsl"); h != params_.end() && h->is_object())
        if (auto b = h->find(band); b != h->end() && b->is_array())
            for (std::size_t i = 0; i < 3 && i < b->size(); ++i) v[i] = (*b)[i].get<double>();
    const bool was = loading_;
    loading_ = true;
    const char* keys[3] = {"hsl_hue", "hsl_sat", "hsl_lum"};
    for (int i = 0; i < 3; ++i) {
        const Slider& s = sliders_[keys[i]];
        s.s->setValue(int(std::lround(v[i] * s.scale)));
    }
    loading_ = was;
}

void GradePanel::load() {
    clip_ = editor_->selectedClip();
    params_ = clip_.isEmpty() ? Json::object() : editor_->clipGrade(clip_);
    loading_ = true;
    for (int i = 0; i < 4; ++i) {
        std::array<double, 3> bal{0, 0, 0};
        double master = 0;
        if (auto w = params_.find(kWheelKeys[i]); w != params_.end() && w->is_array()) {
            for (std::size_t k = 0; k < 3 && k < w->size(); ++k) bal[k] = (*w)[k].get<double>();
            if (w->size() > 3) master = (*w)[3].get<double>();
        }
        wheels_[i]->setValues(bal, master);
    }
    auto setSlider = [&](const char* key, double v) {
        const Slider& s = sliders_[key];
        s.s->setValue(int(std::lround(v * s.scale)));
    };
    setSlider("contrast", params_.value("contrast", 1.0));
    setSlider("pivot", params_.value("pivot", 0.435));
    setSlider("saturation", params_.value("saturation", 1.0));
    const Json q = params_.value("qualifier", Json::object());
    auto pair = [&](const char* key, double a, double b) {
        if (auto it = q.find(key); it != q.end() && it->is_array() && it->size() == 2)
            return std::pair<double, double>{(*it)[0].get<double>(), (*it)[1].get<double>()};
        return std::pair<double, double>{a, b};
    };
    const auto hue = pair("hue", 30, 40), sat = pair("sat", 0.15, 1), lum = pair("lum", 0, 1);
    setSlider("q_hue", hue.first);
    setSlider("q_width", hue.second);
    setSlider("q_sat_lo", sat.first);
    setSlider("q_sat_hi", sat.second);
    setSlider("q_lum_lo", lum.first);
    setSlider("q_lum_hi", lum.second);
    setSlider("q_soft", q.value("softness", 0.1));
    setSlider("q_hue_shift", q.value("hue_shift", 0.0));
    setSlider("q_sat", q.value("saturation", 0.0));
    setSlider("q_exp", q.value("exposure", 0.0));
    {
        QSignalBlocker b1(qualOn_), b2(invert_);
        qualOn_->setChecked(!q.empty() && q.value("enabled", true));
        invert_->setChecked(q.value("invert", false));
    }
    loadCurve();
    loadBand();
    loading_ = false;
    refreshEnabled();
}

void GradePanel::refreshEnabled() {
    const bool on = !clip_.isEmpty();
    hint_->setText(on ? "Kéo để xem ngay trên khung hình; thả chuột là lưu (Ctrl+Z để hoàn tác)."
                      : "Chọn một clip video hoặc ảnh trên Timeline để chỉnh màu.");
    tabs_->setEnabled(on);
    compare_->setEnabled(on);
    reset_->setEnabled(on && !params_.empty());
    applyAll_->setEnabled(on && !params_.empty());
}

void GradePanel::collect() {
    Json p = Json::object();
    for (int i = 0; i < 4; ++i) {
        const auto b = wheels_[i]->balance();
        const double m = wheels_[i]->master();
        if (std::abs(b[0]) + std::abs(b[1]) + std::abs(b[2]) > 1e-6 || m != 0) p[kWheelKeys[i]] = {b[0], b[1], b[2], m};
    }
    auto val = [&](const char* key) {
        const Slider& s = sliders_[key];
        return s.s->value() / s.scale;
    };
    if (val("contrast") != 1.0) p["contrast"] = val("contrast");
    if (std::abs(val("pivot") - 0.435) > 0.006) p["pivot"] = val("pivot");
    if (val("saturation") != 1.0) p["saturation"] = val("saturation");
    // Curves: keep every channel; the visible one from the editor.
    Json curves = params_.value("curves", Json::object());
    Json pts = Json::array();
    for (const auto& q : curve_->points()) pts.push_back({std::round(q.x() * 1000) / 1000, std::round(q.y() * 1000) / 1000});
    const bool identity = curve_->points().size() == 2 && curve_->points()[0] == QPointF(0, 0) && curve_->points()[1] == QPointF(1, 1);
    if (identity)
        curves.erase(curveKey().toStdString());
    else
        curves[curveKey().toStdString()] = pts;
    if (!curves.empty()) p["curves"] = curves;
    // HSL: every band; the visible one from the sliders.
    Json hsl = params_.value("hsl", Json::object());
    const std::string band = vstudio::hslBands()[std::max(0, band_->checkedId())].id;
    const double hv[3] = {val("hsl_hue"), val("hsl_sat"), val("hsl_lum")};
    if (hv[0] == 0 && hv[1] == 0 && hv[2] == 0)
        hsl.erase(band);
    else
        hsl[band] = {hv[0], hv[1], hv[2]};
    if (!hsl.empty()) p["hsl"] = hsl;
    if (qualOn_->isChecked()) {
        p["qualifier"] = Json{{"enabled", true},
                              {"hue", {val("q_hue"), val("q_width")}},
                              {"sat", {val("q_sat_lo"), val("q_sat_hi")}},
                              {"lum", {val("q_lum_lo"), val("q_lum_hi")}},
                              {"softness", val("q_soft")},
                              {"invert", invert_->isChecked()},
                              {"hue_shift", val("q_hue_shift")},
                              {"saturation", val("q_sat")},
                              {"exposure", val("q_exp")}};
    }
    params_ = p;
}

void GradePanel::changed() {
    if (loading_ || clip_.isEmpty()) return;
    collect();
    Json live = params_;
    if (showMask_->isChecked() && live.contains("qualifier")) live["qualifier"]["show_mask"] = true;
    editor_->previewGrade(clip_, live);
}

void GradePanel::commit() {
    if (loading_ || clip_.isEmpty()) return;
    collect();
    editor_->setClipGrade({clip_}, params_);  // also ends the live preview
    if (showMask_->isChecked() && params_.contains("qualifier")) {
        Json live = params_;
        live["qualifier"]["show_mask"] = true;
        editor_->previewGrade(clip_, live);
    }
    refreshEnabled();
}

void GradePanel::pickColor(const QColor& c) {
    {
        QSignalBlocker b(pick_);
        pick_->setChecked(false);
    }
    if (clip_.isEmpty()) return;
    const double luma = 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
    const double s = c.hsvSaturationF();
    loading_ = true;
    auto set = [&](const char* key, double v) {
        const Slider& sl = sliders_[key];
        sl.s->setValue(int(std::lround(v * sl.scale)));
    };
    set("q_hue", std::max(0.0f, c.hsvHueF()) * 360.0);
    set("q_width", 36);
    set("q_sat_lo", std::max(0.0, s - 0.3));
    set("q_sat_hi", 1.0);
    set("q_lum_lo", std::max(0.0, luma - 0.35));
    set("q_lum_hi", std::min(1.0, luma + 0.35));
    {
        QSignalBlocker b(qualOn_);
        qualOn_->setChecked(true);
    }
    loading_ = false;
    tabs_->setCurrentIndex(3);
    commit();
}
