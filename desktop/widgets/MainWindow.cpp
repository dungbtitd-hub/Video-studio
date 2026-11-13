#include "MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
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
    ml->addWidget(assets_);
    mediaDock->setWidget(mediaBox);
    addDockWidget(Qt::LeftDockWidgetArea, mediaDock);
    connect(importBtn, &QPushButton::clicked, this, [this] {
        const QList<QUrl> files = QFileDialog::getOpenFileUrls(this, "Nhập media", {},
            "Media (*.mp4 *.mov *.mkv *.webm *.m4v *.avi *.mp3 *.wav *.m4a *.aac *.flac *.jpg *.jpeg *.png *.webp);;All (*)");
        if (!files.isEmpty()) editor_->importMedia(files);
    });
    connect(assets_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) { editor_->addToTimeline(it->data(Qt::UserRole).toString()); });

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

void MainWindow::refreshAssets() {
    assets_->clear();
    for (const QVariant& v : editor_->assets()) {
        const QVariantMap a = v.toMap();
        auto* it = new QListWidgetItem(QString("%1\n%2  %3  %4 s")
                                           .arg(a["name"].toString(), a["type"].toString(), a["resolution"].toString())
                                           .arg(a["duration"].toDouble(), 0, 'f', 1));
        it->setData(Qt::UserRole, a["id"]);
        it->setToolTip("Nhấp đúp để thêm vào Timeline");
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
