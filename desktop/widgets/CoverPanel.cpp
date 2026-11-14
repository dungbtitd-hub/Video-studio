#include "CoverPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <cstring>

#include "EditorController.h"
#include "vstudio/cover.hpp"

namespace {
constexpr int kIconW = 88, kIconH = 50;

QImage samplePicture() {
    QImage img(kIconW, kIconH, QImage::Format_RGBA8888);
    QPainter p(&img);
    QLinearGradient sky(0, 0, 0, kIconH);
    sky.setColorAt(0, QColor(80, 140, 210));
    sky.setColorAt(1, QColor(230, 200, 160));
    p.fillRect(img.rect(), sky);
    // A "logo" with stripes, so every style shows what it does to detail.
    p.fillRect(QRectF(kIconW * 0.25, kIconH * 0.25, kIconW * 0.5, kIconH * 0.5), QColor(220, 40, 40));
    for (int i = 0; i < 3; ++i) p.fillRect(QRectF(kIconW * 0.3, kIconH * (0.32 + i * 0.13), kIconW * 0.4, 3), Qt::white);
    return img;
}
}  // namespace

CoverPanel::CoverPanel(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);
    hint_ = new QLabel(this);
    hint_->setWordWrap(true);
    l->addWidget(hint_);
    // Covers on this clip.
    covers_ = new QListWidget(this);
    covers_->setMaximumHeight(84);
    l->addWidget(covers_);
    auto* addRow = new QHBoxLayout();
    add_ = new QPushButton("+ Thêm vùng che", this);
    add_->setObjectName("primary");
    remove_ = new QPushButton("Xóa", this);
    addRow->addWidget(add_, 1);
    addRow->addWidget(remove_);
    l->addLayout(addRow);

    styles_ = new QListWidget(this);
    styles_->setViewMode(QListView::IconMode);
    styles_->setIconSize(QSize(kIconW, kIconH));
    styles_->setGridSize(QSize(kIconW + 12, kIconH + 34));
    styles_->setResizeMode(QListView::Adjust);
    styles_->setMovement(QListView::Static);
    styles_->setWordWrap(true);
    for (const auto& st : vstudio::coverStyles()) {
        auto* it = new QListWidgetItem(QString::fromStdString(st.name), styles_);
        it->setData(Qt::UserRole, QString::fromStdString(st.id));
        it->setToolTip(QString::fromStdString(st.category + " · " + st.name) + (st.animated ? " (chuyển động)" : ""));
    }
    l->addWidget(styles_, 1);

    auto* form = new QFormLayout();
    shape_ = new QComboBox(this);
    for (const auto& sh : vstudio::coverShapes()) shape_->addItem(QString::fromStdString(sh.name), QString::fromStdString(sh.id));
    form->addRow("Hình dạng", shape_);
    l->addLayout(form);
    slider(amount_, amountLabel_, "Độ mạnh", 100, "amount", 0.01);
    slider(feather_, featherLabel_, "Viền mềm", 100, "feather", 0.01);
    slider(rotation_, rotationLabel_, "Xoay", 180, "rotation", 1);
    rotation_->setMinimum(-180);
    auto* opts = new QHBoxLayout();
    invert_ = new QCheckBox("Che phần bên ngoài", this);
    color_ = new QPushButton("Màu…", this);
    opts->addWidget(invert_, 1);
    opts->addWidget(color_);
    l->addLayout(opts);

    // Moving subjects.
    auto* motion = new QHBoxLayout();
    follow_ = new QCheckBox("Theo keyframe", this);
    follow_->setToolTip("Bật: mỗi lần kéo vùng che ở một thời điểm khác sẽ đặt keyframe, vùng che trượt mượt giữa các điểm.");
    track_ = new QPushButton("Bám theo đối tượng", this);
    track_->setToolTip("Tự dò đối tượng trong vùng che từ playhead tới cuối clip và tạo keyframe. Chỗ nào lệch thì kéo lại ở đó.");
    motion->addWidget(follow_);
    motion->addWidget(track_, 1);
    l->addLayout(motion);
    trackBar_ = new QProgressBar(this);
    trackBar_->setRange(0, 100);
    trackBar_->setTextVisible(true);
    trackBar_->setFormat("Đang bám theo %p% · bấm nút để dừng");
    trackBar_->hide();
    l->addWidget(trackBar_);

    connect(add_, &QPushButton::clicked, this, [this] {
        const QString style = styles_->currentItem() ? styles_->currentItem()->data(Qt::UserRole).toString() : "blur";
        editor_->addCover(style, shape_->currentData().toString());
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (!editor_->activeCover().isEmpty()) editor_->removeCover(editor_->activeCover());
    });
    connect(covers_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) editor_->setActiveCover(covers_->item(row)->data(Qt::UserRole).toString());
    });
    // One tap on a style: changes the active cover, or adds one when there is none.
    connect(styles_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        const QString style = it->data(Qt::UserRole).toString();
        if (editor_->activeCover().isEmpty()) editor_->addCover(style, shape_->currentData().toString());
        else editor_->updateCover({{"style", style}});
    });
    connect(shape_, &QComboBox::activated, this, [this](int i) {
        if (!editor_->activeCover().isEmpty()) editor_->updateCover({{"shape", shape_->itemData(i)}});
    });
    connect(invert_, &QCheckBox::clicked, this, [this](bool on) { editor_->updateCover({{"invert", on}}); });
    connect(color_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(QColor(editor_->coverState().value("color", "#000000").toString()), this,
                                                "Màu che");
        if (c.isValid()) editor_->updateCover({{"color", c.name()}});
    });
    connect(follow_, &QCheckBox::clicked, this, [this](bool on) { editor_->setCoverFollowing(on); });
    connect(track_, &QPushButton::clicked, this, [this] {
        if (editor_->tracking()) editor_->cancelTrack();
        else editor_->trackCover();
    });
    connect(editor_, &EditorController::selectionChanged, this, &CoverPanel::refresh);
    connect(editor_, &EditorController::projectChanged, this, &CoverPanel::refresh);
    connect(editor_, &EditorController::coversChanged, this, &CoverPanel::refresh);
    connect(editor_, &EditorController::playheadChanged, this, [this] {
        if (!editor_->activeCover().isEmpty() && !editor_->playing()) refresh();  // keyed values change with time
    });
    connect(editor_, &EditorController::mediaLibraryChanged, this, [this](const QString& assetId) {
        if (assetId == editor_->clipAsset(editor_->selectedClip())) {
            baseKey_ = "?";
            refresh();
        }
    });
    refresh();
}

void CoverPanel::slider(QSlider*& s, QLabel*& label, const QString& name, int max, const char* key, double scale) {
    auto* row = new QHBoxLayout();
    auto* n = new QLabel(name, this);
    n->setObjectName(QString("name_") + key);
    n->setMinimumWidth(70);
    row->addWidget(n);
    s = new QSlider(Qt::Horizontal, this);
    s->setRange(0, max);
    row->addWidget(s, 1);
    label = new QLabel(this);
    label->setMinimumWidth(36);
    row->addWidget(label);
    qobject_cast<QVBoxLayout*>(layout())->addLayout(row);
    QSlider* sl = s;
    QLabel* lb = label;
    const QString k = key;
    connect(sl, &QSlider::valueChanged, this, [lb, k](int v) { lb->setText(k == "rotation" ? QString("%1°").arg(v) : QString("%1%").arg(v)); });
    // Committed on release only (one undo step per drag).
    connect(sl, &QSlider::sliderReleased, this, [this, sl, k, scale] { editor_->updateCover({{k, sl->value() * scale}}); });
}

QImage CoverPanel::baseFrame() const {
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

void CoverPanel::rebuildIcons() {
    const QImage base = baseFrame();
    const std::string shape = shape_->currentData().toString().toStdString();
    for (int i = 0; i < styles_->count(); ++i) {
        QImage img = base.copy();
        vstudio::VideoFrame f;
        f.width = img.width();
        f.height = img.height();
        f.rgba.assign(img.constBits(), img.constBits() + img.sizeInBytes());
        vstudio::Json p = vstudio::defaultCover(styles_->item(i)->data(Qt::UserRole).toString().toStdString(), shape);
        p.update(vstudio::Json{{"x", 0.15}, {"y", 0.12}, {"w", 0.7}, {"h", 0.76}, {"feather", 0.08}});
        vstudio::applyCover(f, p, vstudio::Time::fromSeconds(0.5));
        std::memcpy(img.bits(), f.rgba.data(), f.rgba.size());
        if (styles_->item(i)->data(Qt::UserRole).toString().startsWith("mask_")) {
            // Masks make the clip transparent: show what is cut away as a checkerboard.
            QImage shown(img.size(), QImage::Format_RGBA8888);
            QPainter pt(&shown);
            for (int y = 0; y < shown.height(); y += 8)
                for (int x = 0; x < shown.width(); x += 8)
                    pt.fillRect(x, y, 8, 8, ((x + y) / 8) % 2 ? QColor(200, 200, 200) : QColor(245, 245, 245));
            pt.drawImage(0, 0, img);
            pt.end();
            img = shown;
        }
        styles_->item(i)->setIcon(QPixmap::fromImage(img));
    }
}

void CoverPanel::refresh() {
    const QString sel = editor_->selectedClip();
    const QString asset = editor_->clipAsset(sel);
    const QVariantMap st = editor_->coverState();
    const QString key = asset + "|" + st.value("shape", shape_->currentData()).toString();
    if (key != baseKey_) {
        baseKey_ = key;
        if (!st.isEmpty()) {
            QSignalBlocker b(shape_);
            shape_->setCurrentIndex(std::max(0, shape_->findData(st["shape"])));
        }
        rebuildIcons();
    }
    const QVariantList list = sel.isEmpty() ? QVariantList{} : editor_->covers(sel);
    {
        QSignalBlocker b(covers_);
        covers_->clear();
        for (int i = 0; i < list.size(); ++i) {
            const QVariantMap m = list[i].toMap();
            auto* it = new QListWidgetItem(QString("%1. %2%3").arg(i + 1).arg(m["name"].toString(),
                                                                         m["following"].toBool() ? "  ◆ bám theo" : ""),
                                           covers_);
            it->setData(Qt::UserRole, m["id"]);
            if (m["id"].toString() == editor_->activeCover()) covers_->setCurrentItem(it);
        }
    }
    const bool active = !st.isEmpty();
    hint_->setText(sel.isEmpty() ? "Chọn một clip video hoặc ảnh trên Timeline."
                   : list.isEmpty() ? "Chọn kiểu che rồi bấm \"Thêm vùng che\", sau đó kéo vùng che trên khung hình."
                   : active ? "Kéo vùng che trên khung hình. Đối tượng di chuyển? Bấm \"Bám theo đối tượng\"."
                            : "Chọn một vùng che trong danh sách để chỉnh.");
    add_->setEnabled(!sel.isEmpty());
    remove_->setEnabled(active);
    for (QWidget* w : std::initializer_list<QWidget*>{shape_, amount_, feather_, rotation_, invert_, color_, follow_})
        w->setEnabled(active);
    track_->setEnabled(active || editor_->tracking());
    track_->setText(editor_->tracking() ? "Dừng bám theo" : "Bám theo đối tượng");
    trackBar_->setVisible(editor_->tracking());
    trackBar_->setValue(int(editor_->trackProgress() * 100));
    if (active) {
        {
            QSignalBlocker b(styles_);
            for (int i = 0; i < styles_->count(); ++i)
                if (styles_->item(i)->data(Qt::UserRole) == st["style"]) styles_->setCurrentRow(i);
        }
        auto set = [](QSlider* s, QLabel* l, int v, bool deg) {
            if (s->isSliderDown()) return;
            QSignalBlocker b(s);
            s->setValue(v);
            l->setText(deg ? QString("%1°").arg(v) : QString("%1%").arg(v));
        };
        set(amount_, amountLabel_, qRound(st.value("amount", 0.5).toDouble() * 100), false);
        set(feather_, featherLabel_, qRound(st.value("feather", 0.15).toDouble() * 100), false);
        set(rotation_, rotationLabel_, qRound(st.value("rotation", 0.0).toDouble()), true);
        QSignalBlocker b1(invert_), b2(follow_);
        invert_->setChecked(st.value("invert", false).toBool());
        follow_->setChecked(st.value("following", false).toBool());
        const QString style = st["style"].toString();
        color_->setVisible(style == "color" || style == "gradient");
        const bool mask = style.startsWith("mask_");  // masks have no strength
        amount_->setVisible(!mask);
        amountLabel_->setVisible(!mask);
        if (auto* n = findChild<QLabel*>("name_amount")) n->setVisible(!mask);
        invert_->setText(mask ? "Đảo vùng" : "Che phần bên ngoài");
    } else {
        color_->setVisible(false);
    }
}
