#include "StickerPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <cstring>

#include "AnimationBox.h"
#include "EditorController.h"
#include "vstudio/stickers.hpp"

namespace {
constexpr int kIcon = 56;
}

QImage StickerPanel::icon(const QString& id, double seconds, int size) {
    vstudio::VideoFrame f;
    f.allocate(size, size);
    vstudio::stickers::drawStickerIcon(id.toStdString(), seconds, f);
    QImage img(size, size, QImage::Format_RGBA8888);
    std::memcpy(img.bits(), f.rgba.data(), f.rgba.size());
    return img;
}

StickerPanel::StickerPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    category_ = new QComboBox(this);
    category_->addItem(tr("Tất cả"), QString());
    for (const auto& c : vstudio::stickers::stickerCategories())
        category_->addItem(QString::fromStdString(c), QString::fromStdString(c));
    l->addWidget(category_);
    list_ = new QListWidget(this);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(kIcon, kIcon));
    list_->setGridSize(QSize(kIcon + 22, kIcon + 34));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);
    l->addWidget(list_, 1);

    editorBox_ = new QWidget(this);
    auto* el = new QVBoxLayout(editorBox_);
    el->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout();
    // Size as seen on the canvas (content size x scale): matches a corner drag or a pinch.
    size_ = new QSlider(Qt::Horizontal, editorBox_);
    size_->setObjectName("stickerSize");
    size_->setRange(20, 2000);
    size_->setSingleStep(10);
    size_->setPageStep(100);
    sizeLabel_ = new QLabel(editorBox_);
    sizeLabel_->setMinimumWidth(56);
    auto* sizeRow = new QWidget(editorBox_);
    auto* sr = new QHBoxLayout(sizeRow);
    sr->setContentsMargins(0, 0, 0, 0);
    sr->addWidget(size_, 1);
    sr->addWidget(sizeLabel_);
    form->addRow(tr("Cỡ"), sizeRow);
    flip_ = new QCheckBox(tr("Lật ngang"), editorBox_);
    alive_ = new QCheckBox(tr("Chuyển động riêng"), editorBox_);
    alive_->setToolTip(tr("Tim đập, hoa đung đưa, mặt cười chớp mắt…"));
    auto* row = new QWidget(editorBox_);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(flip_);
    rl->addWidget(alive_);
    form->addRow(QString(), row);
    el->addLayout(form);
    anim_ = new AnimationBox(editor_, editorBox_);
    el->addWidget(anim_);
    l->addWidget(editorBox_);

    hoverTimer_ = new QTimer(this);
    hoverTimer_->setInterval(50);
    connect(hoverTimer_, &QTimer::timeout, this, [this] {
        if (!hovered_) return;
        hoverT_ += 0.05;
        hovered_->setIcon(QPixmap::fromImage(icon(hovered_->data(Qt::UserRole).toString(), hoverT_, kIcon)));
    });
    connect(category_, &QComboBox::currentIndexChanged, this, &StickerPanel::fill);
    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) { editor_->addSticker(it->data(Qt::UserRole).toString()); });
    // The sticker grows and shrinks while the slider moves (live preview); one undo step on release.
    connect(size_, &QSlider::valueChanged, this, [this](int v) {
        sizeLabel_->setText(QString("%1 px").arg(v));
        editor_->setStickerDisplaySize(v, size_->isSliderDown());
    });
    connect(size_, &QSlider::sliderReleased, this, [this] { editor_->setStickerDisplaySize(size_->value(), false); });
    connect(editor_, &EditorController::playheadChanged, this, &StickerPanel::refresh);  // the scale may be keyed
    connect(flip_, &QCheckBox::toggled, this, [this](bool on) { editor_->updateContent({{"flip", on}}); });
    connect(alive_, &QCheckBox::toggled, this, [this](bool on) { editor_->updateContent({{"alive", on}}); });
    connect(editor_, &EditorController::selectionChanged, this, &StickerPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &StickerPanel::refresh);
    fill();
    refresh();
}

void StickerPanel::fill() {
    hovered_ = nullptr;
    hoverTimer_->stop();
    list_->clear();
    const QString cat = category_->currentData().toString();
    for (const auto& s : vstudio::stickers::stickerCatalog()) {
        if (!cat.isEmpty() && QString::fromStdString(s.category) != cat) continue;
        auto* it = new QListWidgetItem(QString::fromStdString(s.name), list_);
        it->setData(Qt::UserRole, QString::fromStdString(s.id));
        it->setIcon(QPixmap::fromImage(icon(QString::fromStdString(s.id), 0.7, kIcon)));
        it->setToolTip(tr("%1 · bấm để thêm vào đầu phát").arg(QString::fromStdString(s.name)));
    }
}

bool StickerPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == list_->viewport() && (e->type() == QEvent::MouseMove || e->type() == QEvent::Leave)) {
        QListWidgetItem* it =
            e->type() == QEvent::Leave ? nullptr : list_->itemAt(static_cast<QMouseEvent*>(e)->position().toPoint());
        if (it != hovered_) {
            if (hovered_) hovered_->setIcon(QPixmap::fromImage(icon(hovered_->data(Qt::UserRole).toString(), 0.7, kIcon)));
            hovered_ = it;
            hoverT_ = 0.7;
            if (it) hoverTimer_->start();
            else hoverTimer_->stop();
        }
    }
    return QWidget::eventFilter(o, e);
}

void StickerPanel::refresh() {
    const auto c = editor_->selectedContent();
    const bool sticker = c && c->value("kind", std::string{}) == "sticker";
    editorBox_->setVisible(sticker);
    if (!sticker) {
        anim_->setContent(nullptr);
        return;
    }
    if (!size_->isSliderDown()) {
        const int px = int(std::lround(editor_->stickerDisplaySize()));
        QSignalBlocker b(size_);
        size_->setValue(px);
        sizeLabel_->setText(QString("%1 px").arg(px));
    }
    {
        QSignalBlocker b1(flip_), b2(alive_);
        flip_->setChecked(c->value("flip", false));
        alive_->setChecked(c->value("alive", true));
    }
    anim_->setContent(&*c);
}
