#include "VideoFxPanel.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <cstring>
#include <functional>

#include "EditorController.h"
#include "vstudio/video_fx.hpp"

namespace {
constexpr int kIconW = 112, kIconH = 63;

QSlider* slider(int lo, int hi, int value, QWidget* parent) {
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(lo, hi);
    s->setValue(value);
    return s;
}

// Plays the hovered thumbnail: the panel's viewport reports mouse moves to the panel.
class HoverFilter : public QObject {
public:
    HoverFilter(QListWidget* list, std::function<void(QListWidgetItem*)> onHover)
        : QObject(list), list_(list), onHover_(std::move(onHover)) {}
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::MouseMove)
            onHover_(list_->itemAt(static_cast<QMouseEvent*>(e)->position().toPoint()));
        else if (e->type() == QEvent::Leave)
            onHover_(nullptr);
        return false;
    }

private:
    QListWidget* list_;
    std::function<void(QListWidgetItem*)> onHover_;
};
}  // namespace

VideoFxPanel::VideoFxPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    l->addWidget(hint_);
    group_ = new QComboBox(this);
    for (const char* g : {"zoom", "rotate", "reflect"})
        group_->addItem(QString::fromStdString(vstudio::videofx::groupName(g)), QString(g));
    l->addWidget(group_);
    list_ = new QListWidget(this);
    list_->setObjectName("videoFxList");
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(kIconW, kIconH));
    list_->setGridSize(QSize(kIconW + 14, kIconH + 30));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setMouseTracking(true);
    l->addWidget(list_, 1);

    auto* form = new QFormLayout();
    amount_ = slider(0, 200, 100, this);
    amount_->setObjectName("videoFxAmount");
    speed_ = slider(25, 400, 100, this);
    speed_->setObjectName("videoFxSpeed");
    line_ = slider(10, 90, 62, this);
    line_->setObjectName("videoFxLine");
    lineLabel_ = new QLabel("Mặt nước", this);
    form->addRow("Mức độ", amount_);
    form->addRow("Tốc độ", speed_);
    form->addRow(lineLabel_, line_);
    l->addLayout(form);
    active_ = new QLabel(this);
    active_->setWordWrap(true);
    active_->setStyleSheet("color: #a4abb8; font-size: 11px;");
    l->addWidget(active_);
    clear_ = new QPushButton("Bỏ hiệu ứng nhóm này", this);
    l->addWidget(clear_);

    hoverTimer_ = new QTimer(this);
    hoverTimer_->setInterval(50);
    connect(hoverTimer_, &QTimer::timeout, this, [this] {
        if (!hovered_) return;
        hoverT_ += 0.05;
        hovered_->setIcon(QPixmap::fromImage(icon(hovered_->data(Qt::UserRole).toString(), std::fmod(hoverT_, 4.0))));
    });
    list_->viewport()->installEventFilter(new HoverFilter(list_, [this](QListWidgetItem* it) {
        if (it == hovered_) return;
        if (hovered_) hovered_->setIcon(QPixmap::fromImage(icon(hovered_->data(Qt::UserRole).toString(), 1.6)));
        hovered_ = it;
        hoverT_ = 1.6;
        if (it) hoverTimer_->start();
        else hoverTimer_->stop();
    }));
    connect(group_, &QComboBox::currentIndexChanged, this, [this] {
        fill();
        refresh();
    });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { apply(it->data(Qt::UserRole).toString()); });
    for (QSlider* s : {amount_, speed_, line_}) connect(s, &QSlider::sliderReleased, this, &VideoFxPanel::commitSliders);
    connect(clear_, &QPushButton::clicked, this, [this] {
        const QString sel = editor_->selectedClip();
        if (!sel.isEmpty()) editor_->setClipVideoFx({sel}, currentGroup(), {});
    });
    connect(editor_, &EditorController::selectionChanged, this, &VideoFxPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &VideoFxPanel::refresh);
    connect(editor_, &EditorController::mediaLibraryChanged, this, [this](const QString& asset) {
        if (asset == baseKey_) {
            baseKey_.clear();  // a thumbnail strip arrived: redraw on the clip's own frame
            refresh();
        }
    });
    fill();
    refresh();
}

QString VideoFxPanel::currentGroup() const { return group_->currentData().toString(); }

QImage VideoFxPanel::icon(const QString& id, double seconds) const {
    vstudio::VideoFrame f;
    f.allocate(kIconW, kIconH);
    vstudio::VideoFrame pic;
    if (!base_.isNull()) {
        pic.allocate(base_.width(), base_.height());
        std::memcpy(pic.rgba.data(), base_.constBits(), pic.rgba.size());
    }
    vstudio::videofx::drawVideoFxIcon(id.toStdString(), seconds, f, base_.isNull() ? nullptr : &pic);
    QImage img(kIconW, kIconH, QImage::Format_RGBA8888);
    std::memcpy(img.bits(), f.rgba.data(), f.rgba.size());
    return img;
}

void VideoFxPanel::fill() {
    hovered_ = nullptr;
    hoverTimer_->stop();
    QSignalBlocker b(list_);
    list_->clear();
    const std::string g = currentGroup().toStdString();
    for (const auto& fx : vstudio::videofx::videoFxCatalog()) {
        if (fx.group != g) continue;
        auto* it = new QListWidgetItem(QString::fromStdString(fx.name), list_);
        it->setData(Qt::UserRole, QString::fromStdString(fx.id));
        it->setIcon(QPixmap::fromImage(icon(QString::fromStdString(fx.id), 1.6)));
    }
    const bool reflect = g == "reflect";
    line_->setVisible(reflect);
    lineLabel_->setVisible(reflect);
}

void VideoFxPanel::refresh() {
    const QString sel = editor_->selectedClip();
    const QString asset = editor_->clipAsset(sel);
    const bool media = !asset.isEmpty();
    if (asset != baseKey_) {
        baseKey_ = asset;
        base_ = {};
        if (auto t = media ? editor_->thumbnails(asset) : nullptr; t && t->count() > 0) {
            const QImage frame(t->frame(t->count() / 2), t->width, t->height, t->width * 4, QImage::Format_RGBA8888);
            base_ = frame.convertToFormat(QImage::Format_RGBA8888).copy();
        }
        fill();
    }
    hint_->setText(media ? "Bấm vào hiệu ứng để áp dụng cho clip đã chọn. Mỗi clip có thể dùng một hiệu ứng của mỗi nhóm."
                         : "Chọn một clip video hoặc ảnh trên Timeline.");
    list_->setEnabled(media);
    const QVariantMap cur = media ? editor_->clipVideoFx(sel, currentGroup()) : QVariantMap{};
    const QString fx = cur.value("fx").toString();
    for (QSlider* s : {amount_, speed_, line_}) s->setEnabled(!fx.isEmpty());
    clear_->setEnabled(!fx.isEmpty());
    {
        QSignalBlocker b1(list_), b2(amount_), b3(speed_), b4(line_);
        list_->clearSelection();
        for (int i = 0; i < list_->count(); ++i)
            if (list_->item(i)->data(Qt::UserRole).toString() == fx) list_->setCurrentRow(i);
        if (!fx.isEmpty()) {
            amount_->setValue(int(cur.value("amount").toDouble() * 100 + 0.5));
            speed_->setValue(int(cur.value("speed").toDouble() * 100 + 0.5));
            const double ln = cur.value("line").toDouble();
            if (ln >= 0) line_->setValue(int(ln * 100 + 0.5));
            else line_->setValue(fx == "reflect_sky" ? 40 : 62);
        }
    }
    // Every group's effect on the clip, in one line.
    QStringList on;
    if (media)
        for (const char* g : {"zoom", "rotate", "reflect"}) {
            const QString id = editor_->clipVideoFx(sel, g).value("fx").toString();
            if (const auto* info = vstudio::videofx::findVideoFx(id.toStdString())) on << QString::fromStdString(info->name);
        }
    active_->setText(on.isEmpty() ? QString() : "Đang dùng: " + on.join(" · "));
}

void VideoFxPanel::apply(const QString& fxId) {
    const QString sel = editor_->selectedClip();
    if (sel.isEmpty() || editor_->clipAsset(sel).isEmpty()) return;
    // A new effect starts from its defaults; the sliders then tune it.
    const auto* info = vstudio::videofx::findVideoFx(fxId.toStdString());
    editor_->setClipVideoFx({sel}, currentGroup(), fxId, 1.0, 1.0, info && info->usesLine ? (fxId == "reflect_sky" ? 0.4 : 0.62) : -1);
}

void VideoFxPanel::commitSliders() {
    const QString sel = editor_->selectedClip();
    const QString fx = editor_->clipVideoFx(sel, currentGroup()).value("fx").toString();
    if (fx.isEmpty()) return;
    editor_->setClipVideoFx({sel}, currentGroup(), fx, amount_->value() / 100.0, speed_->value() / 100.0,
                            currentGroup() == "reflect" ? line_->value() / 100.0 : -1);
}
