#include "MainWindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QHeaderView>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSlider>
#include <QTableWidget>
#include <QFileDialog>
#include <QFileInfo>
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
#include <cmath>

#include "EditorController.h"
#include "CoverPanel.h"
#include "TransitionsPanel.h"
#include "LooksPanel.h"
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
    connect(exportBtn_, &QToolButton::clicked, this, &MainWindow::exportVideo);

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

    // Keyframes right under the picture: ◇ adds/removes a key at the playhead
    // (picture and sound separately), drag on the picture to zoom, slider for volume.
    auto* keys = new QHBoxLayout();
    keys->setSpacing(6);
    auto* kfTitle = new QLabel(tr("Keyframe"), centre);
    kfTitle->setStyleSheet("color: #6e7685; font-size: 11px;");
    keys->addWidget(kfTitle);
    kfPrev_ = toolButton("◀◆", centre);
    kfPicture_ = toolButton(tr("◇ Hình"), centre);
    kfAudio_ = toolButton(tr("◇ Âm thanh"), centre);
    kfNext_ = toolButton("◆▶", centre);
    kfResetZoom_ = toolButton(tr("Bỏ zoom"), centre);
    kfPicture_->setObjectName("kfPicture");
    kfAudio_->setObjectName("kfAudio");
    kfResetZoom_->setObjectName("kfResetZoom");
    for (auto* b : {kfPicture_, kfAudio_}) b->setCheckable(true);
    kfPrev_->setToolTip(tr("Keyframe trước ([)"));
    kfNext_->setToolTip(tr("Keyframe sau (])"));
    kfPicture_->setToolTip(tr("Thêm/bỏ keyframe hình ảnh tại playhead (Ctrl+K).\n"
                              "Đặt một key ở điểm bắt đầu, sang điểm khác rồi kéo chuột trên khung hình để khoanh vùng phóng to: "
                              "hình sẽ zoom mượt giữa hai điểm."));
    kfAudio_->setToolTip(tr("Thêm/bỏ keyframe âm thanh tại playhead (Ctrl+Shift+K), độc lập với hình ảnh.\n"
                            "Đặt key rồi đổi âm lượng ở điểm khác: âm thanh to/nhỏ dần giữa các điểm."));
    kfResetZoom_->setToolTip(tr("Đưa hình về kích thước gốc tại playhead"));
    for (auto* b : {kfPrev_, kfPicture_, kfAudio_, kfNext_, kfResetZoom_}) keys->addWidget(b);
    keys->addSpacing(12);
    auto* volIcon = new QLabel(tr("Âm lượng"), centre);
    volIcon->setStyleSheet("color: #a4abb8;");
    keys->addWidget(volIcon);
    kfVolume_ = new QSlider(Qt::Horizontal, centre);
    kfVolume_->setObjectName("kfVolume");
    kfVolume_->setRange(0, 400);  // percent
    kfVolume_->setSingleStep(5);
    kfVolume_->setPageStep(25);
    kfVolume_->setFixedWidth(160);
    keys->addWidget(kfVolume_);
    kfVolumeLabel_ = new QLabel(centre);
    kfVolumeLabel_->setMinimumWidth(44);
    keys->addWidget(kfVolumeLabel_);
    kfInfo_ = new QLabel(centre);
    kfInfo_->setStyleSheet("color: #6e7685; font-size: 11px;");
    keys->addWidget(kfInfo_, 1);
    cl->addLayout(keys);
    setCentralWidget(centre);
    connect(kfPicture_, &QToolButton::clicked, this, [this] { editor_->toggleKeyframe(false); });
    connect(kfAudio_, &QToolButton::clicked, this, [this] { editor_->toggleKeyframe(true); });
    connect(kfPrev_, &QToolButton::clicked, this, [this] { editor_->jumpKeyframe(false); });
    connect(kfNext_, &QToolButton::clicked, this, [this] { editor_->jumpKeyframe(true); });
    connect(kfResetZoom_, &QToolButton::clicked, editor_, &EditorController::resetZoom);
    connect(preview_, &PreviewWidget::regionSelected, editor_, &EditorController::zoomToRegion);
    // The label follows the drag; the edit (one undo step) happens on release or a key press.
    connect(kfVolume_, &QSlider::valueChanged, this, [this](int v) {
        kfVolumeLabel_->setText(QString("%1%").arg(v));
        if (!kfVolume_->isSliderDown()) editor_->setVolumeAtPlayhead(v / 100.0);
    });
    connect(kfVolume_, &QSlider::sliderReleased, this,
            [this] { editor_->setVolumeAtPlayhead(kfVolume_->value() / 100.0); });
    connect(home, &QToolButton::clicked, this, [this] { editor_->seek(0); });
    connect(back, &QToolButton::clicked, this, [this] { editor_->stepFrames(-1); });
    connect(fwd, &QToolButton::clicked, this, [this] { editor_->stepFrames(1); });
    connect(playBtn_, &QToolButton::clicked, editor_, &EditorController::togglePlay);
    connect(editor_, &EditorController::frameReady, preview_, &PreviewWidget::setFrame);
    connect(preview_, &PreviewWidget::previewSizeChanged, editor_, &EditorController::setPreviewSize);

    // ---- docks ----
    auto* mediaDock = new QDockWidget("Thư viện media", this);
    mediaDock->setObjectName("media");
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
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::importMedia);
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
    inspectorDock->setObjectName("inspector");
    auto* ib = new QWidget(inspectorDock);
    auto* il = new QVBoxLayout(ib);
    inspector_ = new QLabel(ib);
    inspector_->setWordWrap(true);
    il->addWidget(inspector_);
    auto* split = new QPushButton("Tách tại playhead (S)", ib);
    auto* del = new QPushButton("Xóa clip (Del)", ib);
    il->addWidget(split);
    il->addWidget(del);
    il->addStretch();
    inspectorDock->setWidget(ib);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    connect(split, &QPushButton::clicked, editor_, &EditorController::splitAtPlayhead);
    connect(del, &QPushButton::clicked, editor_, &EditorController::deleteSelected);

    auto* looksDock = new QDockWidget("Bộ lọc màu", this);
    looksDock->setObjectName("looks");
    looks_ = new LooksPanel(editor_, looksDock);
    looksDock->setWidget(looks_);
    addDockWidget(Qt::RightDockWidgetArea, looksDock);
    tabifyDockWidget(inspectorDock, looksDock);
    auto* coverDock = new QDockWidget("Che mờ", this);
    coverDock->setObjectName("covers");
    covers_ = new CoverPanel(editor_, coverDock);
    coverDock->setWidget(covers_);
    addDockWidget(Qt::RightDockWidgetArea, coverDock);
    tabifyDockWidget(looksDock, coverDock);
    auto* transitionsDock = new QDockWidget("Chuyển cảnh", this);
    transitionsDock->setObjectName("transitions");
    transitions_ = new TransitionsPanel(editor_, transitionsDock);
    transitionsDock->setWidget(transitions_);
    addDockWidget(Qt::RightDockWidgetArea, transitionsDock);
    tabifyDockWidget(coverDock, transitionsDock);
    looksDock->raise();
    // Region on the preview follows the active cover; dragging it there edits the cover.
    connect(editor_, &EditorController::coversChanged, this, &MainWindow::refreshCoverOverlay);
    connect(preview_, &PreviewWidget::coverRectChanged, editor_, &EditorController::setCoverCanvasRect);

    auto* timelineDock = new QDockWidget("Timeline", this);
    timelineDock->setObjectName("timeline");
    auto* tlBox = new QWidget(timelineDock);
    auto* tll = new QVBoxLayout(tlBox);
    tll->setContentsMargins(0, 0, 0, 0);
    tll->setSpacing(0);
    tlBar_ = new QToolBar(tlBox);
    tlBar_->setIconSize(QSize(14, 14));
    tll->addWidget(tlBar_);
    timeline_ = new TimelineWidget(editor_, tlBox);
    connect(timeline_, &TimelineWidget::transitionsRequested, transitionsDock, [transitionsDock] {
        transitionsDock->show();
        transitionsDock->raise();
    });
    tll->addWidget(timeline_, 1);
    timelineDock->setWidget(tlBox);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock);
    resizeDocks({timelineDock}, {280}, Qt::Vertical);
    resizeDocks({mediaDock, inspectorDock}, {280, 260}, Qt::Horizontal);

    buildMenus();
    // Layout as built is the default; then the user's saved layout, if any.
    defaultState_ = saveState();
    QSettings settings;
    settings.beginGroup("workspace");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("state").toByteArray());

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
    refreshKeyframes();
    refreshCoverOverlay();
}

void MainWindow::refreshCoverOverlay() {
    const QRectF r = editor_->coverCanvasRect();
    preview_->setCoverRect(editor_->playing() ? QRectF() : r, editor_->coverState().value("shape").toString());
}

void MainWindow::refreshKeyframes() {
    const EditorController::KeyState k = editor_->keyState();
    const bool video = k.valid && k.video;
    kfPicture_->setEnabled(video);
    kfAudio_->setEnabled(k.valid);
    kfResetZoom_->setEnabled(video && (std::abs(k.scale - 1) > 1e-3 || k.pictureAnimated));
    kfVolume_->setEnabled(k.valid);
    const bool anyKeys = !editor_->selectedClip().isEmpty() &&
                         (!editor_->keyframeTimes(editor_->selectedClip(), false).isEmpty() ||
                          !editor_->keyframeTimes(editor_->selectedClip(), true).isEmpty());
    kfPrev_->setEnabled(anyKeys);
    kfNext_->setEnabled(anyKeys);
    // Filled diamond = a key sits at the playhead.
    kfPicture_->setChecked(k.pictureHere);
    kfAudio_->setChecked(k.audioHere);
    kfPicture_->setText(k.pictureHere ? tr("◆ Hình") : tr("◇ Hình"));
    kfAudio_->setText(k.audioHere ? tr("◆ Âm thanh") : tr("◇ Âm thanh"));
    if (!kfVolume_->isSliderDown()) {
        const QSignalBlocker block(kfVolume_);
        kfVolume_->setValue(int(std::lround(k.volume * 100)));
        kfVolumeLabel_->setText(QString("%1%").arg(kfVolume_->value()));
    }
    preview_->setRegionSelectEnabled(video && !editor_->playing());
    if (!k.valid) kfInfo_->setText(editor_->selectedClip().isEmpty() ? tr("Chọn một clip để đặt keyframe")
                                                                      : tr("Đưa playhead vào clip đã chọn"));
    else if (video && !k.pictureAnimated) kfInfo_->setText(tr("Kéo chuột trên hình để phóng to"));
    else kfInfo_->setText(video ? tr("Zoom ×%1").arg(k.scale, 0, 'f', 2) : QString());
}

void MainWindow::buildMenus() {
    // One place for every command and its keys; the shortcut sheet is generated from it.
    auto act = [this](QMenu* m, const QString& text, const QList<QKeySequence>& keys, auto&& fn) {
        QAction* a = m->addAction(text);
        a->setShortcuts(keys);
        connect(a, &QAction::triggered, this, std::forward<decltype(fn)>(fn));
        return a;
    };
    using K = QKeySequence;

    QMenu* file = menuBar()->addMenu("&Tệp");
    act(file, "Dự án mới", {K::New}, [this] {
        if (confirmDiscard()) editor_->newProject("Dự án mới", 1920, 1080, 30);
    });
    act(file, "Mở dự án…", {K::Open}, [this] {
        if (!confirmDiscard()) return;
        const QString d = QFileDialog::getExistingDirectory(this, "Mở project (.vstudio)");
        if (!d.isEmpty()) editor_->openProject(QUrl::fromLocalFile(d));
    });
    act(file, "Lưu dự án…", {K::Save}, [this] {
        const QString f = QFileDialog::getSaveFileName(this, "Lưu project", {}, "Video Studio (*.vstudio)");
        if (!f.isEmpty()) editor_->saveProject(QUrl::fromLocalFile(f));
    });
    file->addSeparator();
    act(file, "Nhập media…", {K("Ctrl+I")}, [this] { importMedia(); });
    act(file, "Xuất video…", {K("Ctrl+E")}, [this] { exportVideo(); });
    file->addSeparator();
    // One file with the project and its media, to open on another computer or a phone.
    act(file, "Đóng gói để chuyển sang máy khác…", {}, [this] {
        if (editor_->transferring()) return editor_->cancelTransfer();
        const QString f = QFileDialog::getSaveFileName(this, "Đóng gói dự án", editor_->projectName() + ".vstudiopkg",
                                                       "Gói Video Studio (*.vstudiopkg)");
        if (!f.isEmpty()) editor_->packProject(QUrl::fromLocalFile(f));
    });
    act(file, "Mở gói dự án…", {}, [this] {
        if (editor_->transferring() || !confirmDiscard()) return;
        const QString f = QFileDialog::getOpenFileName(this, "Mở gói dự án", {}, "Gói Video Studio (*.vstudiopkg)");
        if (f.isEmpty()) return;
        // Unpacked into a folder chosen by the user (project + media/ side by side).
        const QString d = QFileDialog::getExistingDirectory(this, "Giải nén vào thư mục", QFileInfo(f).absolutePath());
        if (!d.isEmpty()) editor_->openPackage(QUrl::fromLocalFile(f), QUrl::fromLocalFile(d));
    });
    file->addSeparator();
    act(file, "Thoát", {K::Quit, K("Ctrl+Q")}, [this] { close(); });

    QMenu* editMenu = menuBar()->addMenu("&Sửa");
    editMenu->addAction(undo_);
    editMenu->addAction(redo_);
    editMenu->addSeparator();
    act(editMenu, "Tách tại playhead", {K("S"), K("Ctrl+B")}, [this] { editor_->splitAtPlayhead(); });
    act(editMenu, "Xóa clip", {K(Qt::Key_Delete), K(Qt::Key_Backspace)}, [this] { editor_->deleteSelected(); });
    act(editMenu, "Xóa và dồn khoảng trống", {K("Shift+Delete"), K("Shift+Backspace")},
        [this] { editor_->rippleDeleteSelected(); });
    act(editMenu, "Bỏ chọn", {K(Qt::Key_Escape)}, [this] { editor_->setSelectedClip({}); });
    editMenu->addSeparator();
    act(editMenu, "Keyframe hình ảnh", {K("Ctrl+K")}, [this] { editor_->toggleKeyframe(false); });
    act(editMenu, "Keyframe âm thanh", {K("Ctrl+Shift+K")}, [this] { editor_->toggleKeyframe(true); });
    act(editMenu, "Bỏ zoom tại playhead", {}, [this] { editor_->resetZoom(); });
    editMenu->addSeparator();
    act(editMenu, "Thêm track video", {K("Ctrl+Shift+V")}, [this] { editor_->addTrack(false); });
    act(editMenu, "Thêm track âm thanh", {K("Ctrl+Shift+A")}, [this] { editor_->addTrack(true); });

    QMenu* playMenu = menuBar()->addMenu("&Phát");
    act(playMenu, "Phát / Dừng", {K(Qt::Key_Space)}, [this] { editor_->togglePlay(); });
    act(playMenu, "Phát", {K("L")}, [this] {
        if (!editor_->playing()) editor_->togglePlay();
    });
    act(playMenu, "Dừng", {K("K")}, [this] {
        if (editor_->playing()) editor_->togglePlay();
    });
    playMenu->addSeparator();
    act(playMenu, "Lùi 1 khung hình", {K(Qt::Key_Left)}, [this] { editor_->stepFrames(-1); });
    act(playMenu, "Tới 1 khung hình", {K(Qt::Key_Right)}, [this] { editor_->stepFrames(1); });
    act(playMenu, "Lùi 1 giây", {K("Shift+Left")}, [this] { editor_->seekBy(-1); });
    act(playMenu, "Tới 1 giây", {K("Shift+Right")}, [this] { editor_->seekBy(1); });
    act(playMenu, "Điểm cắt trước", {K(Qt::Key_Up)}, [this] { editor_->jumpEditPoint(false); });
    act(playMenu, "Điểm cắt sau", {K(Qt::Key_Down)}, [this] { editor_->jumpEditPoint(true); });
    act(playMenu, "Keyframe trước", {K("[")}, [this] { editor_->jumpKeyframe(false); });
    act(playMenu, "Keyframe sau", {K("]")}, [this] { editor_->jumpKeyframe(true); });
    act(playMenu, "Về đầu", {K(Qt::Key_Home)}, [this] { editor_->seek(0); });
    act(playMenu, "Về cuối", {K(Qt::Key_End)}, [this] { editor_->seek(editor_->duration()); });

    QMenu* view = menuBar()->addMenu("&Xem");
    QAction* zoomIn = act(view, "Phóng to timeline", {K("="), K(K::ZoomIn)}, [this] { timeline_->zoomBy(1.4); });
    QAction* zoomOut = act(view, "Thu nhỏ timeline", {K("-"), K(K::ZoomOut)}, [this] { timeline_->zoomBy(1 / 1.4); });
    QAction* fit = act(view, "Vừa khung", {K("Shift+Z")}, [this] { timeline_->zoomToFit(); });
    snapAct_ = act(view, "Hít cạnh", {K("N")}, [this](bool on) { timeline_->setSnapping(on); });
    snapAct_->setCheckable(true);
    snapAct_->setChecked(timeline_->snapping());
    connect(timeline_, &TimelineWidget::snappingChanged, snapAct_, &QAction::setChecked);
    view->addSeparator();
    for (QDockWidget* d : findChildren<QDockWidget*>()) view->addAction(d->toggleViewAction());
    act(view, "Khôi phục bố cục mặc định", {}, [this] { resetLayout(); });

    QMenu* help = menuBar()->addMenu("&Trợ giúp");
    act(help, "Phím tắt", {K(Qt::Key_F1), K("Ctrl+/")}, [this] { showShortcuts(); });

    // Timeline tools reuse the same actions, so state and shortcuts stay in one place.
    zoomIn->setIconText("+");  // toolbar label; the menu keeps the full name
    zoomOut->setIconText("−");
    fit->setIconText("Vừa khung");
    snapAct_->setIconText("Hít cạnh");
    tlBar_->addAction(snapAct_);
    tlBar_->addSeparator();
    tlBar_->addAction(zoomOut);
    tlBar_->addAction(zoomIn);
    tlBar_->addAction(fit);
    for (QAction* a : tlBar_->actions())
        if (!a->isSeparator()) a->setToolTip(QString("%1 (%2)").arg(a->text(), a->shortcut().toString(K::NativeText)));
}

QList<QStringList> MainWindow::shortcutTable() const {
    QList<QStringList> rows;
    for (QAction* m : menuBar()->actions()) {
        if (!m->menu()) continue;
        const QString menu = m->text().remove('&');
        for (QAction* a : m->menu()->actions()) {
            if (a->isSeparator() || a->shortcuts().isEmpty()) continue;
            QStringList keys;
            for (const QKeySequence& k : a->shortcuts()) keys << k.toString(QKeySequence::NativeText);
            rows.push_back({menu, a->text(), keys.join("  /  ")});
        }
    }
    return rows;
}

void MainWindow::showShortcuts() {
    QDialog dlg(this);
    dlg.setWindowTitle("Phím tắt");
    dlg.resize(520, 560);
    auto* l = new QVBoxLayout(&dlg);
    const auto rows = shortcutTable();
    auto* table = new QTableWidget(int(rows.size()), 3, &dlg);
    table->setHorizontalHeaderLabels({"Nhóm", "Lệnh", "Phím"});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < 3; ++c) table->setItem(r, c, new QTableWidgetItem(rows[r][c]));
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->resizeColumnToContents(0);
    table->resizeColumnToContents(2);
    l->addWidget(table);
    auto* note = new QLabel("Trên timeline: giữ Shift khi kéo để tắt hít cạnh, Ctrl + cuộn chuột để phóng to/thu nhỏ, "
                            "nhấp chuột phải để mở menu track.",
                            &dlg);
    note->setWordWrap(true);
    l->addWidget(note);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    l->addWidget(bb);
    dlg.exec();
}

void MainWindow::importMedia() {
    const QList<QUrl> files = QFileDialog::getOpenFileUrls(this, "Nhập media", {},
        "Media (*.mp4 *.mov *.mkv *.webm *.m4v *.avi *.mp3 *.wav *.m4a *.aac *.flac *.jpg *.jpeg *.png *.webp);;All (*)");
    if (!files.isEmpty()) editor_->importMedia(files);
}

void MainWindow::exportVideo() {
    if (editor_->exporting()) return editor_->cancelExport();
    if (editor_->duration() <= 0) return statusBar()->showMessage("Timeline trống: chưa có gì để xuất");
    const QString preset = choosePreset();
    if (preset.isEmpty()) return;
    const QString f = QFileDialog::getSaveFileName(this, "Xuất MP4", editor_->projectName() + ".mp4", "MP4 (*.mp4)");
    if (!f.isEmpty()) editor_->exportWithPreset(QUrl::fromLocalFile(f), preset);
}

QString MainWindow::choosePreset() {
    QDialog dlg(this);
    dlg.setWindowTitle("Xuất video");
    dlg.resize(460, 420);
    auto* l = new QVBoxLayout(&dlg);
    l->addWidget(new QLabel("Chọn chất lượng. Kích thước giữ đúng tỉ lệ khung hình của dự án.", &dlg));
    auto* list = new QListWidget(&dlg);
    list->setObjectName("exportPresets");
    QSettings settings;
    const QString last = settings.value("export/preset", "fhd1080").toString();
    for (const QVariant& v : editor_->exportPresets()) {
        const QVariantMap m = v.toMap();
        const double mb = m["estimatedBytes"].toDouble() / 1e6;
        auto* it = new QListWidgetItem(QString("%1\n%2 × %3 · %4 khung/giây · %5 Mbit/s · khoảng %6 MB\n%7")
                                           .arg(m["name"].toString())
                                           .arg(m["width"].toInt())
                                           .arg(m["height"].toInt())
                                           .arg(m["fps"].toDouble(), 0, 'g', 4)
                                           .arg(m["bitrate"].toDouble() / 1e6, 0, 'f', 1)
                                           .arg(mb < 10 ? QString::number(mb, 'f', 1) : QString::number(qRound(mb)))
                                           .arg(m["description"].toString()),
                                       list);
        it->setData(Qt::UserRole, m["id"]);
        if (m["id"].toString() == last) list->setCurrentItem(it);
    }
    if (!list->currentItem() && list->count() > 0) list->setCurrentRow(0);
    l->addWidget(list, 1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText("Tiếp tục…");
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    l->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted || !list->currentItem()) return {};
    const QString id = list->currentItem()->data(Qt::UserRole).toString();
    settings.setValue("export/preset", id);
    return id;
}

bool MainWindow::confirmDiscard() {
    if (!editor_->dirty()) return true;
    const auto r = QMessageBox::question(this, "Video Studio", "Dự án có thay đổi chưa lưu. Bỏ các thay đổi này?",
                                         QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return r == QMessageBox::Discard;
}

void MainWindow::saveLayout() const {
    QSettings settings;
    settings.beginGroup("workspace");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("state", saveState());
}

void MainWindow::resetLayout() {
    restoreState(defaultState_);
    for (QDockWidget* d : findChildren<QDockWidget*>()) d->show();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscard()) return e->ignore();
    saveLayout();
    e->accept();
}
