#include "MainWindow.h"

#include <QAction>
#include <QComboBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
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
#include <QScrollArea>
#include <QTabBar>
#include <QTimer>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

#include "EditorController.h"
#include "vstudio/stills.hpp"
#include "CoverPanel.h"
#include "TransitionsPanel.h"
#include "TextPanel.h"
#include "StickerPanel.h"
#include "GradePanel.h"
#include "ScopesPanel.h"
#include "OverlayPanel.h"
#include "RecentStrip.h"
#include "HomePage.h"
#include "VideoFxPanel.h"
#include "SubtitlePanel.h"
#include "AudioPanel.h"
#include "ClipToolsPanel.h"
#include "MixerPanel.h"
#include "LooksPanel.h"
#include "PreviewWidget.h"
#include "TimelineWidget.h"

// Bumped when the default dock arrangement changes, so an older saved layout is not restored.
static constexpr int kLayoutVersion = 2;

namespace {
QToolButton* toolButton(const QString& text, QWidget* parent, bool primary = false) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    if (primary) b->setObjectName("primary");
    return b;
}
// "Lưu thành preset của tôi" in the export dialog: a name, the quality and format chosen in the
// dialog, and optional frame rate / bitrates / sound. Returns the saved name ("" = cancelled).
QString askExportPreset(QWidget* parent, EditorController* editor, const QString& base, const QString& format) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Lưu preset xuất");
    auto* f = new QFormLayout(&dlg);
    auto* name = new QLineEdit(&dlg);
    name->setPlaceholderText("vd. Kênh YouTube của tôi");
    f->addRow("Tên:", name);
    auto* fps = new QComboBox(&dlg);
    fps->setObjectName("userExportFps");
    fps->addItem("Theo dự án", 0.0);
    for (double r : {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0}) fps->addItem(QString::number(r, 'g', 5), r);
    f->addRow("Khung/giây:", fps);
    auto* mbps = new QDoubleSpinBox(&dlg);
    mbps->setRange(0, 400);
    mbps->setDecimals(1);
    mbps->setSpecialValueText("Tự động");
    mbps->setSuffix(" Mbit/s");
    f->addRow("Bitrate video:", mbps);
    auto* kbps = new QSpinBox(&dlg);
    kbps->setRange(0, 512);
    kbps->setSingleStep(32);
    kbps->setSpecialValueText("Tự động");
    kbps->setSuffix(" kbit/s");
    f->addRow("Bitrate âm thanh:", kbps);
    auto* audio = new QCheckBox("Có âm thanh", &dlg);
    audio->setChecked(true);
    f->addRow(audio);
    auto* info = new QLabel(&dlg);
    info->setStyleSheet("color: palette(mid);");
    f->addRow(info);
    auto data = [=] {
        return QVariantMap{{"base", base}, {"format", format}, {"fps", fps->currentData().toDouble()},
                           {"video_mbps", mbps->value()}, {"audio_kbps", kbps->value()}, {"audio", audio->isChecked()}};
    };
    auto update = [=] {
        const QVariantMap m = editor->exportPresetInfo(data());
        info->setText(QString("%1\n%2 × %3 · khoảng %4 MB")
                          .arg(m["summary"].toString())
                          .arg(m["width"].toInt())
                          .arg(m["height"].toInt())
                          .arg(qRound(m["estimatedBytes"].toDouble() / 1e6)));
    };
    update();
    QObject::connect(fps, &QComboBox::currentIndexChanged, &dlg, update);
    QObject::connect(mbps, &QDoubleSpinBox::valueChanged, &dlg, update);
    QObject::connect(kbps, &QSpinBox::valueChanged, &dlg, update);
    QObject::connect(audio, &QCheckBox::toggled, &dlg, update);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    f->addRow(bb);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) return {};
    return editor->saveExportPreset(name->text(), data()) ? name->text().trimmed() : QString();
}
}  // namespace

MainWindow::MainWindow(EditorController* editor, QWidget* parent) : QMainWindow(parent), editor_(editor) {
    resize(1440, 900);
    setDockNestingEnabled(true);

    // ---- toolbar (as in the prototype): Nhập, Lưu, hoàn tác, the editing tools, Xuất video ----
    // The tools are added in buildMenus(), from the same actions as the menus and shortcuts.
    mainBar_ = addToolBar("Main");
    mainBar_->setObjectName("mainBar");
    mainBar_->setMovable(false);
    mainBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    undo_ = new QAction("↶ Hoàn tác", this);
    redo_ = new QAction("↷ Làm lại", this);
    undo_->setShortcut(QKeySequence::Undo);
    redo_->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Shift+Z")});
    exportBtn_ = toolButton("Xuất video", mainBar_, true);
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
    kfInfo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // a hint never widens the window
    kfInfo_->setStyleSheet("color: #6e7685; font-size: 11px;");
    keys->addWidget(kfInfo_, 1);
    cl->addLayout(keys);

    // "Đoạn áp dụng": after Điểm đầu (I) and Điểm cuối (O), what to do with that part only.
    // In a scroll area so the row never makes the window wider than the screen.
    auto* rangeScroll = new QScrollArea(centre);
    rangeScroll->setFrameShape(QFrame::NoFrame);
    rangeScroll->setWidgetResizable(true);
    rangeScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rangeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    rangeScroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    rangeBar_ = new QWidget(rangeScroll);
    rangeBar_->setObjectName("rangeBar");
    auto* rl = new QHBoxLayout(rangeBar_);
    rl->setContentsMargins(0, 2, 0, 0);
    rl->setSpacing(6);
    rangeInfo_ = new QLabel(rangeBar_);
    rangeInfo_->setStyleSheet("color: #f2a93b; font-weight: 600;");
    rl->addWidget(rangeInfo_);
    auto rangeBtn = [this, rl](const QString& text, const QString& name, auto&& fn) {
        auto* b = toolButton(text, rangeBar_);
        b->setObjectName(name);
        rl->addWidget(b);
        connect(b, &QToolButton::clicked, this, std::forward<decltype(fn)>(fn));
        return b;
    };
    rangeBtn(tr("Zoom dần 1.3×"), "rangeZoom13", [this] { editor_->applyRange("zoom", 1.3); });
    rangeBtn(tr("1.6×"), "rangeZoom16", [this] { editor_->applyRange("zoom", 1.6); });
    rangeBtn(tr("2×"), "rangeZoom2", [this] { editor_->applyRange("zoom", 2.0); });
    rangeBtn(tr("Thu nhỏ"), "rangeZoomOut", [this] { editor_->applyRange("zoom", 1 / 1.6); });
    rangeBtn(tr("Tiếng nhỏ dần"), "rangeVol0", [this] { editor_->applyRange("volume", 0); });
    rangeBtn(tr("Tiếng to dần"), "rangeVol2", [this] { editor_->applyRange("volume", 2); });
    rangeBtn(tr("Chậm 0.5×"), "rangeSlow", [this] { editor_->applyRange("speed", 0.5); });
    rangeBtn(tr("Nhanh 2×"), "rangeFast", [this] { editor_->applyRange("speed", 2); });
    auto openDock = [this](QDockWidget* d) {
        editor_->applyRange("isolate", 0);
        if (d) {
            d->show();
            d->raise();
        }
    };
    rangeBtn(tr("Hiệu ứng…"), "rangeFx", [this, openDock] { openDock(videoFxDock_); });
    rangeBtn(tr("Bộ lọc…"), "rangeLook", [this, openDock] { openDock(looksDock_); });
    rangeBtn(tr("Màu…"), "rangeGrade", [this, openDock] { openDock(gradeDock_); });
    rl->addStretch();
    rangeBtn("✕", "rangeClear", [this] { editor_->clearRange(); })->setToolTip(tr("Bỏ đoạn"));
    rangeScroll->setWidget(rangeBar_);
    rangeScroll->setFixedHeight(rangeBar_->sizeHint().height() + 14);
    cl->addWidget(rangeScroll);
    connect(editor_, &EditorController::rangeChanged, rangeScroll, [this, rangeScroll] { rangeScroll->setVisible(editor_->hasRange()); });
    rangeScroll->hide();
    connect(editor_, &EditorController::rangeChanged, this, &MainWindow::refreshRange);
    connect(editor_, &EditorController::playheadChanged, this, [this] {
        if (editor_->hasRange() && !editor_->rangeComplete()) refreshRange();
    });
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
    auto* mediaDock = mediaDock_ = new QDockWidget("Media", this);
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

    auto* inspectorDock = inspectorDock_ = new QDockWidget("Cơ bản", this);
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
    // Rotate / flip the clip (selected, or under the playhead): a portrait phone video or a photo
    // taken sideways is set upright here.
    auto* orientTitle = new QLabel("<b>Xoay / lật</b>", ib);
    il->addWidget(orientTitle);
    auto* orow = new QHBoxLayout();
    auto orientBtn = [&](const QString& text, const QString& name, auto&& fn) {
        auto* b = new QPushButton(text, ib);
        b->setObjectName(name);
        orow->addWidget(b);
        connect(b, &QPushButton::clicked, this, std::forward<decltype(fn)>(fn));
    };
    orientBtn("⟲ Trái", "orientLeft", [this] { editor_->rotateClip(270); });
    orientBtn("⟳ Phải", "orientRight", [this] { editor_->rotateClip(90); });
    orientBtn("⇋ Ngang", "orientFlipH", [this] { editor_->flipClip(true); });
    orientBtn("⇵ Dọc", "orientFlipV", [this] { editor_->flipClip(false); });
    il->addLayout(orow);
    auto* orientReset = new QPushButton("Về hướng gốc", ib);
    orientReset->setObjectName("orientReset");
    il->addWidget(orientReset);
    connect(orientReset, &QPushButton::clicked, this, [this] { editor_->setClipOrientation(0, false, false); });
    il->addStretch();
    inspectorDock->setWidget(ib);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    connect(split, &QPushButton::clicked, editor_, &EditorController::splitAtPlayhead);
    connect(del, &QPushButton::clicked, editor_, &EditorController::deleteSelected);

    auto* looksDock = looksDock_ = new QDockWidget("Bộ lọc màu", this);
    looksDock->setObjectName("looks");
    looks_ = new LooksPanel(editor_, looksDock);
    looksDock->setWidget(looks_);
    addDockWidget(Qt::RightDockWidgetArea, looksDock);
    tabifyDockWidget(inspectorDock, looksDock);
    auto* gradeDock = gradeDock_ = new QDockWidget("Màu", this);
    gradeDock->setObjectName("grade");
    grade_ = new GradePanel(editor_, gradeDock);
    gradeDock->setWidget(grade_);
    addDockWidget(Qt::RightDockWidgetArea, gradeDock);
    tabifyDockWidget(looksDock, gradeDock);
    // Scopes sit under the media library so they show beside the grading panel.
    auto* scopesDock = new QDockWidget("Scope", this);
    scopesDock->setObjectName("scopes");
    scopes_ = new ScopesPanel(editor_, scopesDock);
    scopesDock->setWidget(scopes_);
    addDockWidget(Qt::LeftDockWidgetArea, scopesDock);
    // Eyedropper: the qualifier asks the preview for one click.
    connect(grade_, &GradePanel::pickRequested, preview_, &PreviewWidget::setPickMode);
    connect(preview_, &PreviewWidget::colorPicked, grade_, &GradePanel::pickColor);
    // Zoom, rotate and reflection effects for the selected clip.
    auto* videoFxDock = videoFxDock_ = new QDockWidget("Hiệu ứng", this);
    videoFxDock->setObjectName("videoFx");
    videoFx_ = new VideoFxPanel(editor_, videoFxDock);
    videoFxDock->setWidget(videoFx_);
    addDockWidget(Qt::RightDockWidgetArea, videoFxDock);
    tabifyDockWidget(looksDock, videoFxDock);
    auto* coverDock = new QDockWidget("Che mờ & mặt nạ", this);
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
    auto* textDock = textDock_ = new QDockWidget("Chữ", this);
    textDock->setObjectName("text");
    text_ = new TextPanel(editor_, textDock);
    textDock->setWidget(text_);
    addDockWidget(Qt::RightDockWidgetArea, textDock);
    tabifyDockWidget(transitionsDock, textDock);
    auto* stickerDock = new QDockWidget("Sticker", this);
    stickerDock->setObjectName("stickers");
    stickers_ = new StickerPanel(editor_, stickerDock);
    stickerDock->setWidget(stickers_);
    addDockWidget(Qt::RightDockWidgetArea, stickerDock);
    tabifyDockWidget(textDock, stickerDock);
    auto* overlayDock = new QDockWidget("Lớp phủ", this);
    overlayDock->setObjectName("overlays");
    overlays_ = new OverlayPanel(editor_, overlayDock);
    overlayDock->setWidget(overlays_);
    addDockWidget(Qt::RightDockWidgetArea, overlayDock);
    tabifyDockWidget(stickerDock, overlayDock);
    auto* subtitleDock = new QDockWidget("Phụ đề", this);
    subtitleDock->setObjectName("subtitles");
    subtitles_ = new SubtitlePanel(editor_, subtitleDock);
    subtitleDock->setWidget(subtitles_);
    addDockWidget(Qt::RightDockWidgetArea, subtitleDock);
    tabifyDockWidget(overlayDock, subtitleDock);
    auto* audioDock = new QDockWidget("Âm thanh", this);
    audioDock->setObjectName("audio");
    audio_ = new AudioPanel(editor_, audioDock);
    audioDock->setWidget(audio_);
    addDockWidget(Qt::RightDockWidgetArea, audioDock);
    tabifyDockWidget(subtitleDock, audioDock);
    auto* mixerDock = new QDockWidget("Mixer", this);
    mixerDock->setObjectName("mixer");
    mixer_ = new MixerPanel(editor_, mixerDock);
    mixerDock->setWidget(mixer_);
    addDockWidget(Qt::RightDockWidgetArea, mixerDock);
    tabifyDockWidget(audioDock, mixerDock);
    auto* toolsDock = new QDockWidget("Tốc độ", this);
    toolsDock->setObjectName("clipTools");
    clipTools_ = new ClipToolsPanel(editor_, toolsDock);
    toolsDock->setWidget(clipTools_);
    addDockWidget(Qt::RightDockWidgetArea, toolsDock);
    tabifyDockWidget(looksDock, toolsDock);
    // Default arrangement per the approved desktop demo: things to add on the left
    // (Media / Âm thanh / Chữ / Sticker / Lớp phủ / Chuyển cảnh), the selected clip's
    // settings on the right (Cơ bản / Tốc độ / Màu / Bộ lọc / Hiệu ứng / ...).
    {
        QDockWidget* left[] = {audioDock, textDock, stickerDock, overlayDock, transitionsDock};
        QDockWidget* prev = mediaDock;
        for (QDockWidget* d : left) {
            tabifyDockWidget(prev, d);
            prev = d;
        }
        QDockWidget* right[] = {toolsDock, gradeDock, looksDock, videoFxDock, coverDock,
                                subtitleDock, mixerDock, scopesDock};
        prev = inspectorDock;
        for (QDockWidget* d : right) {
            tabifyDockWidget(prev, d);
            prev = d;
        }
    }
    mediaDock->raise();
    inspectorDock->raise();
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);  // tabs on top, as in the demo
    // Side panels scroll instead of forcing the window wider than the screen.
    for (QDockWidget* d : {mediaDock, audioDock, textDock, stickerDock, overlayDock, transitionsDock, inspectorDock,
                           toolsDock, gradeDock, looksDock, videoFxDock, coverDock, subtitleDock, mixerDock,
                           scopesDock}) {
        QWidget* content = d->widget();
        auto* scroll = new QScrollArea(d);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setWidget(content);
        scroll->setMinimumWidth(220);
        d->setWidget(scroll);
    }
    // Selecting a text, sticker, overlay or subtitle line on the timeline brings its panel forward.
    connect(editor_, &EditorController::selectionChanged, this, [this, textDock, stickerDock, overlayDock, subtitleDock] {
        const auto c = editor_->selectedContent();
        if (!c) return;
        const std::string kind = c->value("kind", std::string{});
        QDockWidget* d = kind == "text"      ? textDock
                         : kind == "overlay"  ? overlayDock
                         : kind == "subtitle" ? subtitleDock
                                              : stickerDock;
        d->show();
        d->raise();
    });
    connect(preview_, &PreviewWidget::objectRectChanged, editor_, &EditorController::setObjectCanvasRect);
    connect(preview_, &PreviewWidget::objectRectChanging, editor_, &EditorController::previewObjectCanvasRect);
    connect(preview_, &PreviewWidget::objectRectCancelled, editor_, &EditorController::endLive);
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
    resizeDocks({mediaDock, inspectorDock}, {300, 340}, Qt::Horizontal);

    buildMenus();
    // Tabbed panels: full names with scroll arrows instead of "Th…", "Bộ…".
    QTimer::singleShot(0, this, [this] {
        for (QTabBar* t : findChildren<QTabBar*>()) {
            t->setElideMode(Qt::ElideNone);
            t->setUsesScrollButtons(true);
        }
        // As in the demo, the library opens on Media and the clip settings on Cơ bản
        // (a raise before the window is shown does not stick to the tab bar).
        if (!restoredLayout_) {
            mediaDock_->raise();
            inspectorDock_->raise();
        }
    });
    // Layout as built is the default; then the user's saved layout, if any.
    defaultState_ = saveState(kLayoutVersion);
    QSettings settings;
    settings.beginGroup("workspace");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoredLayout_ = restoreState(settings.value("state").toByteArray(), kLayoutVersion);

    // Unfinished projects slide up at the bottom on start; they slide away once work begins.
    recent_ = new RecentStrip(this);
    connect(recent_, &RecentStrip::openRequested, this, [this](const QString& path) {
        if (!confirmDiscard()) return;
        recent_->slideOut();
        editor_->openProject(QUrl::fromLocalFile(path));
    });
    connect(recent_, &RecentStrip::forgetRequested, this, [this](const QString& path) {
        editor_->forgetRecent(path);
        showRecentProjects();
    });
    connect(recent_, &RecentStrip::browseRequested, this, [this] {
        if (!confirmDiscard()) return;
        const QString d = QFileDialog::getExistingDirectory(this, "Mở project (.vstudio)");
        if (d.isEmpty()) return;
        recent_->slideOut();
        editor_->openProject(QUrl::fromLocalFile(d));
    });
    home_ = new HomePage(this);
    home_->hide();
    connect(home_, &HomePage::newProjectRequested, this, [this](int w, int h) {
        if (!confirmDiscard()) return;
        editor_->newProject("Dự án mới", w, h, 30);
        hideHome();
        importMedia();
    });
    connect(home_, &HomePage::openRequested, this, [this](const QString& path) {
        if (!confirmDiscard()) return;
        hideHome();
        editor_->openProject(QUrl::fromLocalFile(path));
    });
    connect(home_, &HomePage::browseRequested, this, [this] {
        if (!confirmDiscard()) return;
        const QString d = QFileDialog::getExistingDirectory(this, "Mở project (.vstudio)");
        if (d.isEmpty()) return;
        hideHome();
        editor_->openProject(QUrl::fromLocalFile(d));
    });
    connect(home_, &HomePage::forgetRequested, this, [this](const QString& path) {
        editor_->forgetRecent(path);
        showHome();
    });
    connect(home_, &HomePage::deleteRequested, this, [this](const QString& path) {
        editor_->deleteProject(path);
        showHome();
    });
    connect(home_, &HomePage::shortcutsRequested, this, [this] { showShortcuts(); });
    connect(editor_, &EditorController::projectChanged, this, [this] {
        refreshAssets();
        refreshState();
        if (recent_->shown() && editor_->dirty()) recent_->slideOut();  // editing started
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
        const double secs = a["duration"].toDouble();
        // A photo has no length of its own (it gets 5 s on the timeline): label it as a photo.
        const QString len = secs > 0 ? QString("%1 s").arg(secs, 0, 'f', 1) : QString("Ảnh");
        auto* it = new QListWidgetItem(assetIcon(id), QString("%1\n%2").arg(a["name"].toString(), len));
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
    refreshRange();
}

void MainWindow::refreshRange() {
    if (!rangeBar_) return;
    if (auto* sc = qobject_cast<QScrollArea*>(rangeBar_->parentWidget() ? rangeBar_->parentWidget()->parentWidget() : nullptr))
        sc->setVisible(editor_->hasRange());
    if (!editor_->hasRange()) return;
    auto tc = [](double t) {
        const int m = int(t) / 60;
        return QString("%1:%2").arg(m).arg(t - m * 60, 4, 'f', 1, QChar('0'));
    };
    const bool done = editor_->rangeComplete();
    rangeInfo_->setText(done ? QString("Đoạn %1 → %2").arg(tc(editor_->rangeStart()), tc(editor_->rangeEnd()))
                             : QString("Từ %1 · đưa playhead tới chỗ kết thúc, bấm Điểm cuối (O)").arg(tc(editor_->rangeStart())));
    for (QToolButton* b : rangeBar_->findChildren<QToolButton*>())
        if (b->objectName() != "rangeClear") b->setVisible(done);
}

void MainWindow::refreshCoverOverlay() {
    const QRectF r = editor_->coverCanvasRect();
    preview_->setCoverRect(editor_->playing() ? QRectF() : r, editor_->coverState().value("shape").toString());
    // No cover being edited: the selected text/sticker gets move/scale handles.
    if (r.isNull()) {
        const auto c = editor_->selectedContent();
        preview_->setObjectRect(editor_->playing() ? QRectF() : editor_->objectCanvasRect(),
                                c && c->value("kind", std::string{}) == "overlay");
    }
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
    act(file, "Trang bắt đầu", {K("Ctrl+Shift+H")}, [this] {
        if (confirmDiscard()) showHome();
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
    QAction* rangeIn = act(editMenu, "Điểm đầu đoạn (tại playhead)", {K("I")}, [this] { editor_->setRangeStart(); });
    QAction* rangeOut = act(editMenu, "Điểm cuối đoạn (tại playhead)", {K("O")}, [this] { editor_->setRangeEnd(); });
    act(editMenu, "Bỏ đoạn đã chọn", {K("Alt+X")}, [this] { editor_->clearRange(); });
    editMenu->addSeparator();
    QAction* rotR = act(editMenu, "Xoay phải 90°", {K("Ctrl+R")}, [this] { editor_->rotateClip(90); });
    QAction* rotL = act(editMenu, "Xoay trái 90°", {K("Ctrl+Shift+R")}, [this] { editor_->rotateClip(270); });
    QAction* flipH = act(editMenu, "Lật ngang", {}, [this] { editor_->flipClip(true); });
    act(editMenu, "Lật dọc", {}, [this] { editor_->flipClip(false); });
    QAction* freeze = act(editMenu, "Đóng băng khung hình (3 giây)", {}, [this] { editor_->freezeFrame(3.0); });
    QAction* reverse = act(editMenu, "Đảo ngược clip", {}, [this] {
        const QString c = editor_->selectedClip();
        if (c.isEmpty()) return;
        editor_->setReversed({c}, !editor_->clipReversed(c));
    });
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

    // The main tool row of the prototype, from the same actions (labels shortened there).
    {
        QAction* importAct = new QAction("Nhập", this);
        connect(importAct, &QAction::triggered, this, [this] { importMedia(); });
        QAction* saveAct = new QAction("Lưu", this);
        connect(saveAct, &QAction::triggered, this, [this] {
            const QString f = QFileDialog::getSaveFileName(this, "Lưu project", {}, "Video Studio (*.vstudio)");
            if (!f.isEmpty()) editor_->saveProject(QUrl::fromLocalFile(f));
        });
        QAction* splitAct = new QAction("Tách", this);
        splitAct->setToolTip("Tách các clip dưới playhead (S)");
        connect(splitAct, &QAction::triggered, editor_, &EditorController::splitAtPlayhead);
        QAction* textAct = new QAction("Chữ", this);
        connect(textAct, &QAction::triggered, this, [this] {
            textDock_->show();
            textDock_->raise();
        });
        QAction* gradeAct = new QAction("Màu nâng cao", this);
        connect(gradeAct, &QAction::triggered, this, [this] {
            gradeDock_->show();
            gradeDock_->raise();
        });
        rangeIn->setIconText("[ Điểm đầu");
        rangeOut->setIconText("Điểm cuối ]");
        rotR->setIconText("⟳ Xoay");
        rotL->setIconText("⟲");
        flipH->setIconText("⇋ Lật");
        freeze->setIconText("Đóng băng");
        reverse->setIconText("Đảo ngược");
        undo_->setIconText("↶");
        redo_->setIconText("↷");
        for (QAction* a : {importAct, saveAct}) mainBar_->addAction(a);
        mainBar_->addSeparator();
        mainBar_->addAction(undo_);
        mainBar_->addAction(redo_);
        mainBar_->addSeparator();
        for (QAction* a : {splitAct, rangeIn, rangeOut}) mainBar_->addAction(a);
        mainBar_->addSeparator();
        for (QAction* a : {rotL, rotR, flipH, textAct, freeze, reverse, gradeAct}) mainBar_->addAction(a);
        auto* spacer = new QWidget(mainBar_);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        mainBar_->addWidget(spacer);
        mainBar_->addWidget(exportBtn_);
        for (QAction* a : mainBar_->actions()) {
            if (a->isSeparator() || !a->toolTip().isEmpty() && a->toolTip() != a->iconText()) continue;
            const QString keys = a->shortcut().toString(K::NativeText);
            a->setToolTip(keys.isEmpty() ? a->text() : QString("%1 (%2)").arg(a->text(), keys));
        }
    }

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
    // Camera RAW photos (vstudio/stills.hpp) sit next to video, audio and ordinary images.
    QString raw;
    for (const std::string& e : vstudio::cameraRawExtensions()) raw += " *" + QString::fromStdString(e);
    const QList<QUrl> files = QFileDialog::getOpenFileUrls(this, "Nhập media", {},
        "Media (*.mp4 *.mov *.mkv *.webm *.m4v *.avi *.mp3 *.wav *.m4a *.aac *.flac *.jpg *.jpeg *.png *.webp" + raw +
            ");;Ảnh RAW (" + raw.trimmed() + ");;All (*)");
    if (!files.isEmpty()) editor_->importMedia(files);
}

void MainWindow::exportVideo() {
    if (editor_->exporting()) return editor_->cancelExport();
    if (editor_->duration() <= 0) return statusBar()->showMessage("Timeline trống: chưa có gì để xuất");
    QString format;
    const QString preset = choosePreset(&format);
    if (preset.isEmpty()) return;
    const QString f = QFileDialog::getSaveFileName(this, "Xuất MP4", editor_->projectName() + ".mp4", "MP4 (*.mp4)");
    if (f.isEmpty()) return;
    if (preset.startsWith("user:")) editor_->exportWithUserPreset(QUrl::fromLocalFile(f), preset.mid(5));
    else editor_->exportWithPreset(QUrl::fromLocalFile(f), preset, format);
}

QString MainWindow::choosePreset(QString* format) {
    QDialog dlg(this);
    dlg.setWindowTitle("Xuất video");
    dlg.resize(460, 470);
    auto* l = new QVBoxLayout(&dlg);
    QSettings settings;
    // Video format: H.264, H.265, H.265 10-bit (keeps graded 10-bit/Log footage free of banding).
    auto* fmtRow = new QHBoxLayout;
    fmtRow->addWidget(new QLabel("Định dạng:", &dlg));
    auto* fmt = new QComboBox(&dlg);
    fmt->setObjectName("exportFormat");
    const QString lastFmt = settings.value("export/format", "h264").toString();
    for (const QVariant& v : editor_->exportFormats()) {
        const QVariantMap m = v.toMap();
        fmt->addItem(m["name"].toString(), m["id"]);
        fmt->setItemData(fmt->count() - 1, m["description"].toString(), Qt::ToolTipRole);
        if (m["id"].toString() == lastFmt) fmt->setCurrentIndex(fmt->count() - 1);
    }
    fmtRow->addWidget(fmt, 1);
    l->addLayout(fmtRow);
    auto* fmtNote = new QLabel(&dlg);
    fmtNote->setWordWrap(true);
    fmtNote->setStyleSheet("color: palette(mid);");
    l->addWidget(fmtNote);
    l->addWidget(new QLabel("Chọn chất lượng. Kích thước giữ đúng tỉ lệ khung hình của dự án.", &dlg));
    auto* list = new QListWidget(&dlg);
    list->setObjectName("exportPresets");
    const QString last = settings.value("export/preset", "fhd1080").toString();
    auto fill = [this, list, fmt, fmtNote, &last] {
        const QString keep = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : last;
        list->clear();
        fmtNote->setText(fmt->currentData(Qt::ToolTipRole).toString());
        for (const QVariant& v : editor_->exportPresets(fmt->currentData().toString())) {
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
            if (m["id"].toString() == keep) list->setCurrentItem(it);
        }
        // The user's own presets (FR-018) carry their own format.
        for (const QVariant& v : editor_->userPresets("export")) {
            const QVariantMap m = v.toMap();
            const QVariantMap info = editor_->exportPresetInfo(m["data"].toMap());
            auto* it = new QListWidgetItem(QString("★ %1\n%2 × %3 · khoảng %4 MB\n%5")
                                               .arg(m["name"].toString())
                                               .arg(info["width"].toInt())
                                               .arg(info["height"].toInt())
                                               .arg(qRound(info["estimatedBytes"].toDouble() / 1e6))
                                               .arg(m["summary"].toString()),
                                           list);
            const QString id = "user:" + m["name"].toString();
            it->setData(Qt::UserRole, id);
            if (id == keep) list->setCurrentItem(it);
        }
        if (!list->currentItem() && list->count() > 0) list->setCurrentRow(0);
    };
    fill();
    connect(fmt, &QComboBox::currentIndexChanged, &dlg, fill);
    l->addWidget(list, 1);
    auto* mine = new QHBoxLayout;
    auto* saveMine = new QPushButton("Lưu thành preset của tôi…", &dlg);
    saveMine->setObjectName("saveExportPreset");
    saveMine->setToolTip("Lưu chất lượng và định dạng đang chọn, kèm khung/giây và bitrate tuỳ chọn");
    auto* removeMine = new QPushButton("Xoá preset", &dlg);
    mine->addWidget(saveMine);
    mine->addWidget(removeMine);
    mine->addStretch(1);
    l->addLayout(mine);
    auto syncMine = [list, removeMine] {
        removeMine->setEnabled(list->currentItem() && list->currentItem()->data(Qt::UserRole).toString().startsWith("user:"));
    };
    connect(list, &QListWidget::currentItemChanged, &dlg, syncMine);
    syncMine();
    connect(saveMine, &QPushButton::clicked, &dlg, [this, &dlg, list, fmt, fill] {
        QString base = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : "fhd1080";
        QString format = fmt->currentData().toString();
        if (base.startsWith("user:")) {  // start from that preset's own quality and format
            for (const QVariant& v : editor_->userPresets("export"))
                if ("user:" + v.toMap()["name"].toString() == base) {
                    base = v.toMap()["data"].toMap()["base"].toString();
                    format = v.toMap()["data"].toMap()["format"].toString();
                }
        }
        const QString name = askExportPreset(&dlg, editor_, base, format);
        if (name.isEmpty()) return;
        fill();
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toString() == "user:" + name) list->setCurrentRow(i);
    });
    connect(removeMine, &QPushButton::clicked, &dlg, [this, &dlg, list, fill] {
        if (!list->currentItem()) return;
        const QString id = list->currentItem()->data(Qt::UserRole).toString();
        if (!id.startsWith("user:")) return;
        if (QMessageBox::question(&dlg, "Xoá preset", QString("Xoá preset \"%1\"?").arg(id.mid(5))) != QMessageBox::Yes) return;
        editor_->removeUserPreset("export", id.mid(5));
        fill();
    });
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText("Tiếp tục…");
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    l->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted || !list->currentItem()) return {};
    const QString id = list->currentItem()->data(Qt::UserRole).toString();
    settings.setValue("export/preset", id);
    settings.setValue("export/format", fmt->currentData().toString());
    if (format) *format = fmt->currentData().toString();
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
    settings.setValue("state", saveState(kLayoutVersion));
}

void MainWindow::resetLayout() {
    restoreState(defaultState_, kLayoutVersion);
    for (QDockWidget* d : findChildren<QDockWidget*>()) d->show();
}

void MainWindow::showHome() {
    recent_->slideOut();
    home_->setProjects(editor_->recentProjects(false, 40), &EditorController::projectPoster);
    home_->setGeometry(rect());
    home_->show();
    home_->raise();
    statusBar()->hide();
}

void MainWindow::hideHome() {
    home_->hide();
    statusBar()->show();
}

bool MainWindow::homeShown() const { return home_ && home_->isVisible(); }

void MainWindow::showRecentProjects() {
    recent_->setCards(editor_->recentProjects(true, 12), &EditorController::projectPoster);
    if (recent_->cardCount() > 0) recent_->slideIn();
    else recent_->slideOut();
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    if (home_) home_->setGeometry(rect());
    // Once the window is wide enough (and no saved layout set the widths), give the side panels
    // about the widths of the approved demo: library on the left, clip settings on the right.
    if (!sidePanelsFitted_ && width() >= 1200 && mediaDock_ && inspectorDock_) {
        sidePanelsFitted_ = true;
        if (!restoredLayout_)
            resizeDocks({mediaDock_, inspectorDock_}, {width() * 24 / 100, width() * 23 / 100}, Qt::Horizontal);
    }
    if (recent_) recent_->place();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!confirmDiscard()) return e->ignore();
    saveLayout();
    e->accept();
}
