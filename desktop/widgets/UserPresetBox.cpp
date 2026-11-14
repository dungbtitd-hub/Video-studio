#include "UserPresetBox.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "EditorController.h"

UserPresetBox::UserPresetBox(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 4, 0, 0);
    l->setSpacing(4);
    auto* title = new QLabel(tr("Preset của tôi"), this);
    title->setStyleSheet("font-weight: 600;");
    l->addWidget(title);
    auto* row = new QHBoxLayout();
    list_ = new QComboBox(this);
    list_->setObjectName("userStylePresets");
    list_->setToolTip(tr("Màu và hiệu ứng đã lưu: dùng lại cho clip khác, ở mọi dự án"));
    row->addWidget(list_, 1);
    apply_ = new QPushButton(tr("Áp dụng"), this);
    apply_->setObjectName("applyUserPreset");
    row->addWidget(apply_);
    save_ = new QPushButton(tr("Lưu…"), this);
    save_->setObjectName("saveUserPreset");
    save_->setToolTip(tr("Lưu màu và hiệu ứng của clip đang chọn thành preset"));
    row->addWidget(save_);
    more_ = new QToolButton(this);
    more_->setText("⋯");
    more_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(more_);
    QAction* all = menu->addAction(tr("Áp dụng cho tất cả clip"));
    QAction* rename = menu->addAction(tr("Đổi tên…"));
    QAction* remove = menu->addAction(tr("Xoá preset"));
    more_->setMenu(menu);
    row->addWidget(more_);
    l->addLayout(row);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    summary_->setStyleSheet("color: palette(mid);");
    l->addWidget(summary_);

    connect(apply_, &QPushButton::clicked, this, [this] {
        const QString sel = editor_->selectedClip();
        if (!sel.isEmpty() && list_->currentIndex() >= 0) editor_->applyStylePreset({sel}, list_->currentData().toString());
    });
    connect(save_, &QPushButton::clicked, this, &UserPresetBox::saveCurrent);
    connect(all, &QAction::triggered, this, [this] {
        const QStringList clips = editor_->lookableClips();
        if (!clips.isEmpty() && list_->currentIndex() >= 0) editor_->applyStylePreset(clips, list_->currentData().toString());
    });
    connect(rename, &QAction::triggered, this, [this] {
        const QString from = list_->currentData().toString();
        if (from.isEmpty()) return;
        bool ok = false;
        const QString to = QInputDialog::getText(this, tr("Đổi tên preset"), tr("Tên mới:"), QLineEdit::Normal, from, &ok);
        if (ok && !to.trimmed().isEmpty()) editor_->renameUserPreset("style", from, to);
    });
    connect(remove, &QAction::triggered, this, [this] {
        const QString name = list_->currentData().toString();
        if (name.isEmpty()) return;
        if (QMessageBox::question(this, tr("Xoá preset"), tr("Xoá preset \"%1\"?").arg(name)) == QMessageBox::Yes)
            editor_->removeUserPreset("style", name);
    });
    connect(list_, &QComboBox::currentIndexChanged, this, &UserPresetBox::refresh);
    connect(editor_, &EditorController::userPresetsChanged, this, &UserPresetBox::reload);
    connect(editor_, &EditorController::selectionChanged, this, &UserPresetBox::refresh);
    reload();
}

void UserPresetBox::reload() {
    const QString keep = list_->currentData().toString();
    QSignalBlocker b(list_);
    list_->clear();
    for (const QVariant& v : editor_->userPresets("style")) {
        const QVariantMap m = v.toMap();
        list_->addItem(m["name"].toString(), m["name"]);
        list_->setItemData(list_->count() - 1, m["summary"], Qt::ToolTipRole);
    }
    const int i = list_->findData(keep);
    list_->setCurrentIndex(i >= 0 ? i : (list_->count() > 0 ? 0 : -1));
    refresh();
}

void UserPresetBox::refresh() {
    const bool any = list_->count() > 0;
    const bool clip = !editor_->selectedClip().isEmpty();
    list_->setEnabled(any);
    apply_->setEnabled(any && clip);
    save_->setEnabled(clip);
    more_->setEnabled(any);
    summary_->setText(any ? list_->currentData(Qt::ToolTipRole).toString()
                          : tr("Chỉnh màu, bộ lọc, hiệu ứng cho một clip rồi bấm \"Lưu…\" để dùng lại."));
}

void UserPresetBox::saveCurrent() {
    const QString sel = editor_->selectedClip();
    if (sel.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Lưu preset"), tr("Tên preset:"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    if (list_->findData(name.trimmed()) >= 0 &&
        QMessageBox::question(this, tr("Lưu preset"), tr("Đã có preset \"%1\". Ghi đè?").arg(name.trimmed())) != QMessageBox::Yes)
        return;
    if (editor_->saveStylePreset(sel, name)) list_->setCurrentIndex(list_->findData(name.trimmed()));
}
