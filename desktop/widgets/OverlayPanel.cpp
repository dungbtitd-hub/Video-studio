#include "OverlayPanel.h"

#include <QColorDialog>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cstring>

#include "EditorController.h"
#include "vstudio/overlays.hpp"

namespace {
constexpr int kIconW = 96, kIconH = 54;

// Slider value (int) <-> content value.
struct Setting {
    const char* key;
    const char* label;
    int lo, hi;
    double scale;  // content = slider / scale
    double def;
};
const Setting kSettings[] = {
    {"density", "Mật độ", 10, 300, 100, 1.0},  {"speed", "Tốc độ", 0, 300, 100, 1.0},
    {"size", "Cỡ hạt", 30, 300, 100, 1.0},     {"opacity", "Độ đậm", 5, 100, 100, 1.0},
    {"feather", "Viền mờ", 0, 40, 100, 0.12},  {"fade", "Hiện/ẩn dần (giây)", 0, 30, 10, 0.8},
};
}  // namespace

QImage OverlayPanel::icon(const QString& id, double seconds, int w, int h) {
    vstudio::VideoFrame f;
    f.allocate(w, h);
    vstudio::overlays::drawOverlayIcon(id.toStdString(), seconds, f);
    QImage img(w, h, QImage::Format_RGBA8888);
    std::memcpy(img.bits(), f.rgba.data(), f.rgba.size());
    return img;
}

OverlayPanel::OverlayPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    category_ = new QComboBox(this);
    category_->addItem(tr("Tất cả"), QString());
    for (const auto& c : vstudio::overlays::overlayCategories())
        category_->addItem(QString::fromStdString(c), QString::fromStdString(c));
    l->addWidget(category_);
    list_ = new QListWidget(this);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(kIconW, kIconH));
    list_->setGridSize(QSize(kIconW + 14, kIconH + 30));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    l->addWidget(list_, 1);

    editorBox_ = new QWidget(this);
    auto* form = new QFormLayout(editorBox_);
    form->setContentsMargins(0, 0, 0, 0);
    for (const Setting& s : kSettings) {
        auto* sl = new QSlider(Qt::Horizontal, editorBox_);
        sl->setRange(s.lo, s.hi);
        sl->setObjectName(s.key);
        form->addRow(tr(s.label), sl);
        sliders_.push_back({s.key, sl});
        const QString key = s.key;
        const double scale = s.scale;
        connect(sl, &QSlider::valueChanged, this, [this, sl, key, scale] {
            if (sl->isSliderDown()) return;  // dragging: committed on release
            const double v = sl->value() / scale;
            if (key == "fade")
                editor_->updateContent({{"in", {{"type", v > 0 ? "fade" : ""}, {"duration", v}}},
                                        {"out", {{"type", v > 0 ? "fade" : ""}, {"duration", v}}}});
            else editor_->updateContent({{key.toStdString(), v}});
        });
        connect(sl, &QSlider::sliderReleased, sl, [sl] { emit sl->valueChanged(sl->value()); });
    }
    auto* row = new QWidget(editorBox_);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    color_ = new QPushButton(tr("Màu…"), row);
    colorReset_ = new QPushButton(tr("Màu gốc"), row);
    full_ = new QPushButton(tr("Phủ toàn khung"), row);
    full_->setToolTip(tr("Phủ lại cả khung hình (kéo khung trên màn xem để chỉ phủ một phần)"));
    rl->addWidget(color_);
    rl->addWidget(colorReset_);
    rl->addStretch();
    rl->addWidget(full_);
    form->addRow(QString(), row);
    l->addWidget(editorBox_);

    hoverTimer_ = new QTimer(this);
    hoverTimer_->setInterval(60);
    connect(hoverTimer_, &QTimer::timeout, this, [this] {
        if (!hovered_) return;
        hoverT_ += 0.06;
        hovered_->setIcon(QPixmap::fromImage(icon(hovered_->data(Qt::UserRole).toString(), hoverT_, kIconW, kIconH)));
    });
    connect(category_, &QComboBox::currentIndexChanged, this, &OverlayPanel::fill);
    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) { editor_->addOverlay(it->data(Qt::UserRole).toString()); });
    connect(color_, &QPushButton::clicked, this, [this] {
        const auto c = editor_->selectedContent();
        const QColor start(QString::fromStdString(c ? c->value("color", std::string("#ffffff")) : std::string("#ffffff")));
        const QColor picked = QColorDialog::getColor(start, this, tr("Màu lớp phủ"));
        if (picked.isValid()) editor_->updateContent({{"color", picked.name().toStdString()}});
    });
    connect(colorReset_, &QPushButton::clicked, this, [this] { editor_->updateContent({{"color", nullptr}}); });
    connect(full_, &QPushButton::clicked, this, [this] { editor_->setOverlayFullFrame(); });
    connect(editor_, &EditorController::selectionChanged, this, &OverlayPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &OverlayPanel::refresh);
    fill();
    refresh();
}

QSlider* OverlayPanel::slider(const QString& key) const {
    for (const auto& [k, s] : sliders_)
        if (k == key) return s;
    return nullptr;
}

void OverlayPanel::fill() {
    hovered_ = nullptr;
    hoverTimer_->stop();
    list_->clear();
    const QString cat = category_->currentData().toString();
    for (const auto& o : vstudio::overlays::overlayCatalog()) {
        if (!cat.isEmpty() && QString::fromStdString(o.category) != cat) continue;
        auto* it = new QListWidgetItem(QString::fromStdString(o.name), list_);
        it->setData(Qt::UserRole, QString::fromStdString(o.id));
        it->setIcon(QPixmap::fromImage(icon(QString::fromStdString(o.id), 2.0, kIconW, kIconH)));
        it->setToolTip(tr("%1 · bấm để phủ lên video từ đầu phát").arg(QString::fromStdString(o.name)));
    }
}

bool OverlayPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == list_->viewport() && (e->type() == QEvent::MouseMove || e->type() == QEvent::Leave)) {
        QListWidgetItem* it =
            e->type() == QEvent::Leave ? nullptr : list_->itemAt(static_cast<QMouseEvent*>(e)->position().toPoint());
        if (it != hovered_) {
            if (hovered_)
                hovered_->setIcon(QPixmap::fromImage(icon(hovered_->data(Qt::UserRole).toString(), 2.0, kIconW, kIconH)));
            hovered_ = it;
            hoverT_ = 2.0;
            if (it) hoverTimer_->start();
            else hoverTimer_->stop();
        }
    }
    return QWidget::eventFilter(o, e);
}

void OverlayPanel::refresh() {
    const auto c = editor_->selectedContent();
    const bool overlay = c && c->value("kind", std::string{}) == "overlay";
    editorBox_->setVisible(overlay);
    if (!overlay) return;
    for (const Setting& s : kSettings) {
        QSlider* sl = slider(s.key);
        if (sl->isSliderDown()) continue;
        double v = s.def;
        if (std::string(s.key) == "fade") {
            const auto& in = (*c)["in"];
            v = in.is_object() && !in.value("type", std::string{}).empty() ? in.value("duration", s.def) : 0.0;
        } else v = c->value(s.key, s.def);
        QSignalBlocker b(sl);
        sl->setValue(int(std::lround(v * s.scale)));
    }
    const auto* info = vstudio::overlays::findOverlay(c->value("overlay", std::string{}));
    const bool tint = info && info->tintable;
    color_->setVisible(tint);
    colorReset_->setVisible(tint && c->contains("color"));
}
