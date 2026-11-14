#include "TransitionsPanel.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "EditorController.h"
#include "Favorites.h"
#include "TimelineWidget.h"
#include "vstudio/transitions.hpp"

namespace {
constexpr int kIconW = 96, kIconH = 54;

// Two clearly different pictures: a cool landscape and a warm sunset.
QImage samplePicture(bool warm) {
    QImage img(kIconW, kIconH, QImage::Format_RGBA8888);
    QPainter p(&img);
    QLinearGradient sky(0, 0, 0, kIconH);
    sky.setColorAt(0, warm ? QColor(250, 160, 60) : QColor(60, 120, 220));
    sky.setColorAt(1, warm ? QColor(190, 80, 100) : QColor(100, 180, 160));
    p.fillRect(img.rect(), sky);
    p.setPen(Qt::NoPen);
    if (warm) {
        p.setBrush(QColor(255, 240, 120));
        p.drawEllipse(QPointF(kIconW * 0.68, kIconH * 0.4), 11, 11);
        p.fillRect(QRectF(0, kIconH * 0.75, kIconW, kIconH * 0.25), QColor(90, 50, 30));
    } else {
        p.setBrush(QColor(40, 90, 50));
        p.drawPolygon(QPolygonF({QPointF(0, kIconH), QPointF(kIconW * 0.32, kIconH * 0.35), QPointF(kIconW * 0.64, kIconH)}));
        p.fillRect(QRectF(kIconW * 0.74, kIconH * 0.25, 12, 26), QColor(235, 235, 235));
    }
    return img;
}

vstudio::VideoFrame toFrame(const QImage& img) {
    vstudio::VideoFrame f;
    f.width = img.width();
    f.height = img.height();
    f.rgba.assign(img.constBits(), img.constBits() + img.sizeInBytes());
    return f;
}
}  // namespace

QStringList TransitionList::mimeTypes() const { return {TimelineWidget::kTransitionMime}; }

QMimeData* TransitionList::mimeData(const QList<QListWidgetItem*>& items) const {
    if (items.isEmpty()) return nullptr;
    auto* m = new QMimeData();
    m->setData(TimelineWidget::kTransitionMime, items.first()->data(Qt::UserRole).toString().toUtf8());
    return m;
}

QImage TransitionsPanel::thumbnail(const QString& type, double progress) {
    static const vstudio::VideoFrame a = toFrame(samplePicture(false)), b = toFrame(samplePicture(true));
    vstudio::VideoFrame out;
    out.allocate(kIconW, kIconH);
    vstudio::applyTransition(type.toStdString(), a, b, progress, out, vstudio::Time::fromSeconds(progress));
    QImage img(kIconW, kIconH, QImage::Format_RGBA8888);
    std::memcpy(img.bits(), out.rgba.data(), out.rgba.size());
    return img;
}

TransitionsPanel::TransitionsPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    hint_ = new QLabel(tr("Kéo hiệu ứng thả vào chỗ nối giữa hai clip trên Timeline. Hoặc chọn chỗ nối (ô ⊞ dưới chỗ nối, "
                          "hoặc một clip: đầu clip) rồi bấm vào hiệu ứng. Rê chuột lên hình để xem chạy thử."),
                       this);
    hint_->setWordWrap(true);
    l->addWidget(hint_);
    place_ = new QLabel(this);
    place_->setWordWrap(true);
    place_->setStyleSheet("color: #e0b050;");
    l->addWidget(place_);

    list_ = new TransitionList(this);
    favorites::attach(list_, editor_, "transition");
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(kIconW, kIconH));
    list_->setGridSize(QSize(kIconW + 14, kIconH + 36));
    list_->setSpacing(2);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    for (const auto& t : vstudio::transitionCatalog()) {
        auto* it = new QListWidgetItem(QString::fromStdString(t.name), list_);
        it->setData(Qt::UserRole, QString::fromStdString(t.id));
        it->setToolTip(QString::fromStdString(t.name + " · " + t.category));
        it->setIcon(QPixmap::fromImage(thumbnail(QString::fromStdString(t.id), 0.45)));
    }
    l->addWidget(list_, 1);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Độ dài"), this));
    duration_ = new QSlider(Qt::Horizontal, this);
    duration_->setRange(1, 30);  // tenths of a second: 0.1 .. 3 s
    duration_->setValue(5);
    row->addWidget(duration_, 1);
    durationLabel_ = new QLabel(this);
    durationLabel_->setMinimumWidth(52);
    row->addWidget(durationLabel_);
    l->addLayout(row);
    auto* buttons = new QHBoxLayout();
    applyAll_ = new QPushButton(tr("Áp dụng cho tất cả"), this);
    applyAll_->setToolTip(tr("Đặt hiệu ứng này vào mọi chỗ nối giữa các clip."));
    remove_ = new QPushButton(tr("Bỏ chuyển cảnh"), this);
    buttons->addWidget(applyAll_);
    buttons->addWidget(remove_);
    l->addLayout(buttons);

    // Hovered thumbnail plays: forward, a short hold on each picture, repeat.
    anim_ = new QTimer(this);
    anim_->setInterval(40);
    connect(anim_, &QTimer::timeout, this, [this] {
        if (!hovered_) return;
        phase_ = std::fmod(phase_ + 0.04 / 1.6, 1.0);
        const double p = std::clamp((phase_ - 0.15) / 0.7, 0.0, 1.0);
        hovered_->setIcon(QPixmap::fromImage(thumbnail(hovered_->data(Qt::UserRole).toString(), p)));
    });

    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { apply(it->data(Qt::UserRole).toString()); });
    connect(duration_, &QSlider::valueChanged, this,
            [this] { durationLabel_->setText(tr("%1 giây").arg(QLocale().toString(seconds(), 'f', 1))); });
    // Length is committed on release only: one undo step.
    connect(duration_, &QSlider::sliderReleased, this, [this] {
        const auto cut = editor_->selectedCut();
        const QString type = editor_->transitionAt(cut).first;
        if (!type.isEmpty()) editor_->setTransition(cut, type, seconds());
    });
    connect(applyAll_, &QPushButton::clicked, this, [this] {
        const QString type = editor_->transitionAt(editor_->selectedCut()).first;
        const QString use = type.isEmpty() && list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : type;
        if (!use.isEmpty()) editor_->setTransitionEverywhere(use, seconds());
    });
    connect(remove_, &QPushButton::clicked, this, [this] { editor_->setTransition(editor_->selectedCut(), {}, seconds()); });
    connect(editor_, &EditorController::cutChanged, this, &TransitionsPanel::refresh);
    connect(editor_, &EditorController::selectionChanged, this, &TransitionsPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &TransitionsPanel::refresh);
    durationLabel_->setText(tr("%1 giây").arg(QLocale().toString(seconds(), 'f', 1)));
    refresh();
}

double TransitionsPanel::seconds() const { return duration_->value() / 10.0; }

bool TransitionsPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == list_->viewport()) {
        if (e->type() == QEvent::MouseMove || e->type() == QEvent::Leave) {
            QListWidgetItem* it = e->type() == QEvent::Leave
                                      ? nullptr
                                      : list_->itemAt(static_cast<QMouseEvent*>(e)->position().toPoint());
            if (it != hovered_) {
                if (hovered_)  // back to its still
                    hovered_->setIcon(QPixmap::fromImage(thumbnail(hovered_->data(Qt::UserRole).toString(), 0.45)));
                hovered_ = it;
                phase_ = 0;
                if (it) anim_->start();
                else anim_->stop();
            }
        }
    }
    return QWidget::eventFilter(o, e);
}

void TransitionsPanel::refresh() {
    auto cut = editor_->selectedCut();
    if (!cut.valid() && !editor_->selectedClip().isEmpty()) cut = editor_->normalizedCut(editor_->selectedClip(), 0);
    const auto [type, secs] = editor_->transitionAt(cut);
    place_->setText(cut.valid() ? editor_->cutLabel(cut) : tr("Chưa chọn chỗ nối nào."));
    remove_->setEnabled(!type.isEmpty());
    applyAll_->setEnabled(!type.isEmpty() || list_->currentItem());
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toString() == type) {
            QSignalBlocker b(list_);
            list_->setCurrentRow(i);
        }
    if (!type.isEmpty() && !duration_->isSliderDown()) {
        QSignalBlocker b(duration_);
        duration_->setValue(std::clamp(int(std::lround(secs * 10)), duration_->minimum(), duration_->maximum()));
        durationLabel_->setText(tr("%1 giây").arg(QLocale().toString(seconds(), 'f', 1)));
    }
}

void TransitionsPanel::apply(const QString& type) {
    auto cut = editor_->selectedCut();
    if (!cut.valid() && !editor_->selectedClip().isEmpty()) cut = editor_->normalizedCut(editor_->selectedClip(), 0);
    if (!cut.valid()) {
        place_->setText(tr("Chọn một chỗ nối hoặc một clip trước, hoặc kéo hiệu ứng thả vào Timeline."));
        applyAll_->setEnabled(true);
        return;
    }
    editor_->setTransition(cut, type, seconds());
    editor_->setSelectedCut(cut.clipId, cut.edge);
}
