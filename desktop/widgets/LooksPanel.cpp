#include "LooksPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "EditorController.h"
#include "Favorites.h"
#include "UserPresetBox.h"
#include "vstudio/color_space.hpp"
#include "vstudio/looks.hpp"

namespace {
constexpr int kIconW = 96, kIconH = 54;

// Neutral sample when no clip is selected: sky, foliage, skin and greys.
QImage samplePicture() {
    QImage img(kIconW, kIconH, QImage::Format_RGBA8888);
    QPainter p(&img);
    QLinearGradient sky(0, 0, 0, kIconH * 0.6);
    sky.setColorAt(0, QColor(70, 130, 200));
    sky.setColorAt(1, QColor(240, 190, 140));
    p.fillRect(img.rect(), sky);
    p.fillRect(QRectF(0, kIconH * 0.6, kIconW, kIconH * 0.4), QColor(60, 120, 60));
    p.setBrush(QColor(224, 172, 140));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(kIconW * 0.68, kIconH * 0.55), 11, 14);
    for (int i = 0; i < 5; ++i) p.fillRect(QRectF(6 + i * 9, kIconH - 12, 9, 8), QColor::fromHsv(0, 0, 40 + i * 50));
    return img;
}
}  // namespace

LooksPanel::LooksPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    // Source conversion first: Log/HDR footage looks flat until it is converted.
    auto* src = new QHBoxLayout();
    src->addWidget(new QLabel("Đầu vào", this));
    input_ = new QComboBox(this);
    input_->addItem("Bình thường (Rec.709)", QString());
    for (const auto& f : vstudio::logFormats())
        input_->addItem(QString::fromStdString(f.name), QString::fromStdString(f.id));
    input_->setToolTip("Video quay Log hoặc HLG trông nhạt màu: chọn đúng định dạng của máy quay để chuyển về Rec.709.");
    src->addWidget(input_, 1);
    l->addLayout(src);
    inputHint_ = new QLabel(this);
    inputHint_->setWordWrap(true);
    inputHint_->setStyleSheet("color: #e0b050;");
    l->addWidget(inputHint_);
    auto* lutRow = new QHBoxLayout();
    lut_ = new QPushButton("Thêm LUT (.cube)…", this);
    lut_->setToolTip("LUT 3D từ hãng máy quay (vd. LUT D-Log M của DJI) hoặc LUT màu điện ảnh.");
    lutClear_ = new QPushButton("Bỏ LUT", this);
    lutRow->addWidget(lut_, 1);
    lutRow->addWidget(lutClear_);
    l->addLayout(lutRow);
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    l->addWidget(hint_);
    list_ = new QListWidget(this);
    favorites::attach(list_, editor_, "look");
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(kIconW, kIconH));
    list_->setGridSize(QSize(kIconW + 14, kIconH + 36));
    list_->setSpacing(2);
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    l->addWidget(list_, 1);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel("Độ mạnh", this));
    strength_ = new QSlider(Qt::Horizontal, this);
    strength_->setRange(0, 100);
    strength_->setValue(100);
    row->addWidget(strength_, 1);
    strengthLabel_ = new QLabel("100%", this);
    strengthLabel_->setMinimumWidth(38);
    row->addWidget(strengthLabel_);
    l->addLayout(row);
    auto* buttons = new QHBoxLayout();
    applyAll_ = new QPushButton("Áp dụng cho tất cả", this);
    clear_ = new QPushButton("Bỏ bộ lọc", this);
    buttons->addWidget(applyAll_);
    buttons->addWidget(clear_);
    l->addLayout(buttons);
    userPresets_ = new UserPresetBox(editor_, this);
    l->addWidget(userPresets_);

    auto* none = new QListWidgetItem("Gốc", list_);
    none->setData(Qt::UserRole, QString());
    for (const auto& look : vstudio::colorLooks()) {
        auto* it = new QListWidgetItem(QString::fromStdString(look.name), list_);
        it->setData(Qt::UserRole, QString::fromStdString(look.id));
        it->setToolTip(QString::fromStdString(look.name + " · " + look.category));
    }

    // One tap applies (one undo step). Re-tapping the current look keeps it.
    connect(list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) { apply(it->data(Qt::UserRole).toString()); });
    connect(strength_, &QSlider::valueChanged, this, [this](int v) { strengthLabel_->setText(QString("%1%").arg(v)); });
    // Strength is committed on release only: dragging the slider never floods the undo stack.
    connect(strength_, &QSlider::sliderReleased, this, [this] {
        const QString sel = editor_->selectedClip();
        if (sel.isEmpty()) return;
        const auto [look, _] = editor_->clipLook(sel);
        if (!look.isEmpty()) editor_->setClipLook({sel}, look, strength_->value() / 100.0);
    });
    connect(applyAll_, &QPushButton::clicked, this, [this] {
        const QString sel = editor_->selectedClip();
        const QString look = sel.isEmpty() ? QString() : editor_->clipLook(sel).first;
        const QStringList all = editor_->lookableClips();
        if (!look.isEmpty() && !all.isEmpty()) editor_->setClipLook(all, look, strength_->value() / 100.0);
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        if (!editor_->selectedClip().isEmpty()) editor_->setClipLook({editor_->selectedClip()}, {}, 1.0);
    });
    connect(input_, &QComboBox::activated, this, [this](int i) {
        const QString sel = editor_->selectedClip();
        if (!sel.isEmpty()) editor_->setClipColorSpace({sel}, input_->itemData(i).toString());
    });
    connect(lut_, &QPushButton::clicked, this, [this] {
        const QString sel = editor_->selectedClip();
        if (sel.isEmpty()) return;
        const QString f = QFileDialog::getOpenFileName(this, "Chọn LUT", {}, "LUT 3D (*.cube)");
        if (!f.isEmpty()) editor_->setClipLut({sel}, f);
    });
    connect(lutClear_, &QPushButton::clicked, this, [this] {
        if (!editor_->selectedClip().isEmpty()) editor_->setClipLut({editor_->selectedClip()}, {});
    });
    connect(editor_, &EditorController::selectionChanged, this, &LooksPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &LooksPanel::refresh);
    connect(editor_, &EditorController::mediaLibraryChanged, this, [this](const QString& assetId) {
        if (assetId == editor_->clipAsset(editor_->selectedClip())) {
            baseKey_ = "?";  // the selected clip's thumbnails just arrived
            refresh();
        }
    });
    baseKey_ = "?";
    refresh();
}

QImage LooksPanel::baseFrame() const {
    const QString asset = editor_->clipAsset(editor_->selectedClip());
    if (auto t = asset.isEmpty() ? nullptr : editor_->thumbnails(asset); t && t->count() > 0) {
        const int i = t->count() / 2;
        const QImage frame(t->frame(i), t->width, t->height, t->width * 4, QImage::Format_RGBA8888);
        return frame.scaled(kIconW, kIconH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
            .copy(0, 0, kIconW, kIconH)
            .convertToFormat(QImage::Format_RGBA8888);
    }
    return samplePicture();
}

void LooksPanel::rebuildIcons() {
    const QImage base = baseFrame();
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* it = list_->item(i);
        QImage img = base.copy();
        const QString id = it->data(Qt::UserRole).toString();
        if (const auto* look = vstudio::findColorLook(id.toStdString())) {
            vstudio::VideoFrame f;
            f.width = img.width();
            f.height = img.height();
            f.rgba.assign(img.constBits(), img.constBits() + img.sizeInBytes());
            vstudio::applyColorLook(f, look->params);
            std::memcpy(img.bits(), f.rgba.data(), f.rgba.size());
        }
        it->setIcon(QPixmap::fromImage(img));
    }
}

void LooksPanel::refresh() {
    const QString sel = editor_->selectedClip();
    const QString asset = editor_->clipAsset(sel);
    if (asset != baseKey_) {
        baseKey_ = asset;
        rebuildIcons();
    }
    const auto [look, strength] = sel.isEmpty() ? QPair<QString, double>{} : editor_->clipLook(sel);
    hint_->setText(sel.isEmpty() ? "Chọn một clip video hoặc ảnh trên Timeline, rồi chạm vào bộ lọc."
                                 : "Chạm vào bộ lọc để áp dụng ngay cho clip đã chọn.");
    list_->setEnabled(!sel.isEmpty());
    input_->setEnabled(!sel.isEmpty());
    lut_->setEnabled(!sel.isEmpty());
    {
        const QString in = sel.isEmpty() ? QString() : editor_->clipColorSpace(sel).first;
        QSignalBlocker b(input_);
        input_->setCurrentIndex(std::max(0, input_->findData(in)));
        const QString suggested = sel.isEmpty() ? QString() : editor_->suggestedColorSpace(sel);
        const auto* f = vstudio::findLogFormat(suggested.toStdString());
        inputHint_->setVisible(in.isEmpty() && f);
        if (f) inputHint_->setText(QString("File này là %1: chọn ở \"Đầu vào\" để hiện màu đúng.").arg(QString::fromStdString(f->name)));
        const QString lut = sel.isEmpty() ? QString() : editor_->clipLut(sel);
        lut_->setText(lut.isEmpty() ? "Thêm LUT (.cube)…" : "LUT: " + QFileInfo(lut).completeBaseName());
        lutClear_->setEnabled(!lut.isEmpty());
    }
    clear_->setEnabled(!look.isEmpty());
    applyAll_->setEnabled(!look.isEmpty());
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toString() == look) {
            QSignalBlocker b(list_);
            list_->setCurrentRow(i);
        }
    if (!look.isEmpty() && !strength_->isSliderDown()) {
        QSignalBlocker b(strength_);
        strength_->setValue(qRound(strength * 100));
        strengthLabel_->setText(QString("%1%").arg(strength_->value()));
    }
}

void LooksPanel::apply(const QString& lookId) {
    const QString sel = editor_->selectedClip();
    if (sel.isEmpty()) return;
    editor_->setClipLook({sel}, lookId, strength_->value() / 100.0);
}
