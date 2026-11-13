#include "MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "EditorController.h"
#include "PreviewWidget.h"
#include "TimelineWidget.h"

namespace {
QToolButton* toolButton(const QString& text, QWidget* parent, bool primary = false) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    if (primary) b->setObjectName("primary");
    return b;
}
}  // namespace

MainWindow::MainWindow(EditorController* editor, QWidget* parent) : QMainWindow(parent), editor_(editor) {
    resize(1440, 900);
    setDockNestingEnabled(true);

    // ---- toolbar ----
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    auto* brand = new QLabel("<b>Video Studio</b>  <span style='color:#a4abb8'>· Widgets</span>", tb);
    tb->addWidget(brand);
    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    auto* open = tb->addAction("Mở");
    auto* save = tb->addAction("Lưu");
    undo_ = tb->addAction("↶");
    redo_ = tb->addAction("↷");
    undo_->setShortcut(QKeySequence::Undo);
    redo_->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Shift+Z")});
    exportBtn_ = toolButton("Xuất video", tb, true);
    tb->addWidget(exportBtn_);
    connect(open, &QAction::triggered, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, "Mở project (.vstudio)");
        if (!d.isEmpty()) editor_->openProject(QUrl::fromLocalFile(d));
    });
    connect(save, &QAction::triggered, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, "Lưu project", {}, "Video Studio (*.vstudio)");
        if (!f.isEmpty()) editor_->saveProject(QUrl::fromLocalFile(f));
    });
    connect(undo_, &QAction::triggered, editor_, &EditorController::undo);
    connect(redo_, &QAction::triggered, editor_, &EditorController::redo);
    connect(exportBtn_, &QToolButton::clicked, this, [this] {
        if (editor_->exporting()) return editor_->cancelExport();
        const QString f = QFileDialog::getSaveFileName(this, "Xuất MP4", {}, "MP4 (*.mp4)");
        if (!f.isEmpty()) editor_->exportTo(QUrl::fromLocalFile(f), 0, 0);
    });

    // ---- centre: preview + transport ----
    auto* centre = new QWidget(this);
    auto* cl = new QVBoxLayout(centre);
    cl->setContentsMargins(10, 10, 10, 6);
    preview_ = new PreviewWidget(centre);
    cl->addWidget(preview_, 1);
    auto* transport = new QHBoxLayout();
    timecode_ = new QLabel(centre);
    timecode_->setStyleSheet("font-family: 'JetBrains Mono', Consolas, monospace;");
    transport->addWidget(timecode_);
    transport->addStretch();
    auto* home = toolButton("⏮", centre);
    auto* back = toolButton("◀", centre);
    playBtn_ = toolButton("▶", centre, true);
    auto* fwd = toolButton("▶|", centre);
    for (auto* b : {home, back, playBtn_, fwd}) transport->addWidget(b);
    transport->addStretch();
    cl->addLayout(transport);
    setCentralWidget(centre);
    connect(home, &QToolButton::clicked, this, [this] { editor_->seek(0); });
    connect(back, &QToolButton::clicked, this, [this] { editor_->stepFrames(-1); });
    connect(fwd, &QToolButton::clicked, this, [this] { editor_->stepFrames(1); });
    connect(playBtn_, &QToolButton::clicked, editor_, &EditorController::togglePlay);
    connect(editor_, &EditorController::frameReady, preview_, &PreviewWidget::setFrame);
    connect(preview_, &PreviewWidget::previewSizeChanged, editor_, &EditorController::setPreviewSize);

    // ---- docks ----
    auto* mediaDock = new QDockWidget("Thư viện media", this);
    auto* mediaBox = new QWidget(mediaDock);
    auto* ml = new QVBoxLayout(mediaBox);
    auto* importBtn = new QPushButton("+ Nhập", mediaBox);
    ml->addWidget(importBtn);
    assets_ = new QListWidget(mediaBox);
    assets_->setViewMode(QListView::IconMode);
    assets_->setIconSize(QSize(128, 72));
    assets_->setGridSize(QSize(140, 120));
    assets_->setResizeMode(QListView::Adjust);
    assets_->setMovement(QListView::Static);
    assets_->setWordWrap(true);
    assets_->setContextMenuPolicy(Qt::CustomContextMenu);
    ml->addWidget(assets_);
    mediaDock->setWidget(mediaBox);
    addDockWidget(Qt::LeftDockWidgetArea, mediaDock);
    connect(importBtn, &QPushButton::clicked, this, [this] {
        const QList<QUrl> files = QFileDialog::getOpenFileUrls(this, "Nhập media", {},
            "Media (*.mp4 *.mov *.mkv *.webm *.m4v *.avi *.mp3 *.wav *.m4a *.aac *.flac *.jpg *.jpeg *.png *.webp);;All (*)");
        if (!files.isEmpty()) editor_->importMedia(files);
    });
    connect(assets_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const QString id = it->data(Qt::UserRole).toString();
        if (editor_->assetMissing(id)) return relinkAsset(id);
        editor_->addToTimeline(id);
    });
    connect(assets_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* it = assets_->itemAt(pos);
        if (!it) return;
        const QString id = it->data(Qt::UserRole).toString();
        QMenu menu(this);
        menu.addAction("Thêm vào Timeline", this, [this, id] { editor_->addToTimeline(id); })
            ->setEnabled(!editor_->assetMissing(id));
        menu.addAction("Tìm file trong thư mục…", this, [this, id] { relinkAsset(id); });
        menu.addAction("Chọn file thay thế…", this, [this, id] {
            const QUrl f = QFileDialog::getOpenFileUrl(this, "Chọn file thay thế");
            if (!f.isEmpty()) editor_->relinkToFile(id, f);
        });
        menu.exec(assets_->viewport()->mapToGlobal(pos));
    });
    // A thumbnail strip arrived: update only that item's icon.
    connect(editor_, &EditorController::mediaLibraryChanged, this, [this](const QString& id) {
        icons_.remove(id);
        for (int i = 0; i < assets_->count(); ++i)
            if (assets_->item(i)->data(Qt::UserRole).toString() == id) assets_->item(i)->setIcon(assetIcon(id));
    });

    auto* inspectorDock = new QDockWidget("Thuộc tính", this);
    auto* ib = new QWidget(inspectorDock);
    auto* il = new QVBoxLayout(ib);
    inspector_ = new QLabel(ib);
    inspector_->setWordWrap(true);
    il->addWidget(inspector_);
    auto* split = new QPushButton("Tách tại playhead (S)", ib);
    auto* del = new QPushButton("Xóa clip (Del)", ib);
    split->setShortcut(QKeySequence("S"));
    del->setShortcut(QKeySequence::Delete);
    il->addWidget(split);
    il->addWidget(del);
    il->addStretch();
    inspectorDock->setWidget(ib);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    connect(split, &QPushButton::clicked, editor_, &EditorController::splitAtPlayhead);
    connect(del, &QPushButton::clicked, editor_, &EditorController::deleteSelected);

    auto* timelineDock = new QDockWidget("Timeline", this);
    timeline_ = new TimelineWidget(editor_, timelineDock);
    timelineDock->setWidget(timeline_);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock);
    resizeDocks({timelineDock}, {280}, Qt::Vertical);
    resizeDocks({mediaDock, inspectorDock}, {280, 260}, Qt::Horizontal);

    auto* play = new QAction(this);
    play->setShortcut(Qt::Key_Space);
    connect(play, &QAction::triggered, editor_, &EditorController::togglePlay);
    addAction(play);
    auto* left = new QAction(this);
    left->setShortcut(Qt::Key_Left);
    connect(left, &QAction::triggered, this, [this] { editor_->stepFrames(-1); });
    addAction(left);
    auto* right = new QAction(this);
    right->setShortcut(Qt::Key_Right);
    connect(right, &QAction::triggered, this, [this] { editor_->stepFrames(1); });
    addAction(right);

    connect(editor_, &EditorController::projectChanged, this, [this] {
        refreshAssets();
        refreshState();
    });
    connect(editor_, &EditorController::playheadChanged, this, &MainWindow::refreshState);
    connect(editor_, &EditorController::playingChanged, this, &MainWindow::refreshState);
    connect(editor_, &EditorController::selectionChanged, this, &MainWindow::refreshState);
    connect(editor_, &EditorController::exportChanged, this, &MainWindow::refreshState);
    connect(editor_, &EditorController::statusChanged, this, [this] { statusBar()->showMessage(editor_->status()); });
    refreshAssets();
    refreshState();
}

QIcon MainWindow::assetIcon(const QString& id) {
    const QString key = id + (editor_->assetMissing(id) ? "!" : "");
    if (auto it = icons_.constFind(key); it != icons_.constEnd()) return *it;
    QPixmap pm(128, 72);
    pm.fill(QColor("#1b1f27"));
    if (auto t = editor_->thumbnails(id); t && t->count() > 0) {
        // Middle frame of the strip, contained in the icon.
        const int i = t->count() / 2;
        const QImage img(t->frame(i), t->width, t->height, t->width * 4, QImage::Format_RGBA8888);
        QPainter p(&pm);
        const QImage scaled = img.scaled(pm.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((pm.width() - scaled.width()) / 2, (pm.height() - scaled.height()) / 2, scaled);
    }
    else if (auto w = editor_->waveform(id); w && w->buckets() > 0) {
        // Audio-only media: the envelope across the icon.
        QPainter p(&pm);
        p.setPen(QColor(120, 220, 170));
        const double k = 30.0 / std::max(16, w->loudest);
        for (int x = 0; x < pm.width(); ++x) {
            const std::size_t i = std::size_t(x) * w->buckets() / pm.width();
            p.drawLine(x, int(36 - w->peaks[i * 2 + 1] * k), x, int(36 - w->peaks[i * 2] * k));
        }
    }
    if (editor_->assetMissing(id)) {
        QPainter p(&pm);
        p.fillRect(pm.rect(), QColor(120, 20, 20, 170));
        p.setPen(Qt::white);
        p.drawText(pm.rect(), Qt::AlignCenter, "⚠ Mất file");
    }
    const QIcon icon(pm);
    if (editor_->thumbnails(id) || editor_->waveform(id) || editor_->assetMissing(id)) icons_.insert(key, icon);  // placeholder is not cached
    return icon;
}

void MainWindow::relinkAsset(const QString& id) {
    const QUrl dir = QFileDialog::getExistingDirectoryUrl(this, "Tìm file media trong thư mục");
    if (!dir.isEmpty()) editor_->relinkFromFolder(id, dir);
}

void MainWindow::refreshAssets() {
    assets_->clear();
    for (const QVariant& v : editor_->assets()) {
        const QVariantMap a = v.toMap();
        const QString id = a["id"].toString();
        const bool missing = editor_->assetMissing(id);
        auto* it = new QListWidgetItem(assetIcon(id), QString("%1\n%2 s").arg(a["name"].toString())
                                                          .arg(a["duration"].toDouble(), 0, 'f', 1));
        it->setData(Qt::UserRole, id);
        it->setToolTip(missing ? "Không tìm thấy file. Nhấp đúp để tìm trong một thư mục."
                               : QString("%1  %2\nNhấp đúp để thêm vào Timeline").arg(a["type"].toString(), a["resolution"].toString()));
        if (missing) it->setForeground(QColor("#ff8a80"));
        assets_->addItem(it);
    }
}

void MainWindow::refreshState() {
    setWindowTitle("Video Studio — " + editor_->projectName() + (editor_->dirty() ? " •" : ""));
    timecode_->setText(editor_->timecode() + QString("   /  %1 s").arg(editor_->duration(), 0, 'f', 2));
    playBtn_->setText(editor_->playing() ? "❚❚" : "▶");
    undo_->setEnabled(editor_->canUndo());
    redo_->setEnabled(editor_->canRedo());
    exportBtn_->setText(editor_->exporting() ? QString("Đang xuất %1%  ✕").arg(int(editor_->exportProgress() * 100))
                                             : "Xuất video");
    inspector_->setText(editor_->selectedClip().isEmpty() ? "Chọn một clip trên Timeline" : "Clip đã chọn");
}
