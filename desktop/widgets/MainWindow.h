// Qt Widgets workspace (prototype to compare with the QML workspace):
// dockable panels around the preview, toolbar, timeline at the bottom.
#pragma once

#include <QHash>
#include <QIcon>
#include <QMainWindow>

class EditorController;
class PreviewWidget;
class TimelineWidget;
class GradePanel;
class ScopesPanel;
class LooksPanel;
class CoverPanel;
class TransitionsPanel;
class TextPanel;
class SubtitlePanel;
class AudioPanel;
class MixerPanel;
class ClipToolsPanel;
class StickerPanel;
class OverlayPanel;
class RecentStrip;
class VideoFxPanel;
class QListWidget;
class QLabel;
class QToolButton;
class QCloseEvent;
class QResizeEvent;
class QToolBar;
class QSlider;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] TimelineWidget* timeline() const { return timeline_; }
    [[nodiscard]] LooksPanel* looks() const { return looks_; }
    [[nodiscard]] GradePanel* grade() const { return grade_; }
    [[nodiscard]] ScopesPanel* scopes() const { return scopes_; }
    [[nodiscard]] CoverPanel* coverPanel() const { return covers_; }
    [[nodiscard]] TransitionsPanel* transitionsPanel() const { return transitions_; }
    [[nodiscard]] TextPanel* textPanel() const { return text_; }
    [[nodiscard]] SubtitlePanel* subtitlePanel() const { return subtitles_; }
    [[nodiscard]] AudioPanel* audioPanel() const { return audio_; }
    [[nodiscard]] MixerPanel* mixerPanel() const { return mixer_; }
    [[nodiscard]] ClipToolsPanel* clipToolsPanel() const { return clipTools_; }
    [[nodiscard]] StickerPanel* stickerPanel() const { return stickers_; }
    [[nodiscard]] OverlayPanel* overlayPanel() const { return overlays_; }
    [[nodiscard]] VideoFxPanel* videoFxPanel() const { return videoFx_; }
    [[nodiscard]] PreviewWidget* preview() const { return preview_; }
    [[nodiscard]] RecentStrip* recentStrip() const { return recent_; }
    // Fills the "continue" strip with unfinished projects and slides it in (at start).
    void showRecentProjects();
    // Every shortcut in the menus, as shown in Trợ giúp > Phím tắt (menu, action, keys).
    [[nodiscard]] QList<QStringList> shortcutTable() const;
    // Layout persistence: QSettings group "workspace" (geometry + dock state).
    void saveLayout() const;
    void resetLayout();

protected:
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void buildMenus();
    void importMedia();
    void exportVideo();
    QString choosePreset(QString* format = nullptr);  // empty = cancelled; *format = "h264" | "hevc" | "hevc10"
    void showShortcuts();
    bool confirmDiscard();  // false = user cancelled
    void refreshAssets();
    void refreshState();
    void refreshKeyframes();
    void refreshCoverOverlay();  // active cover's region on the preview  // keyframe bar + region select, from the clip under the playhead
    QIcon assetIcon(const QString& id);  // cached until a new thumbnail strip arrives
    void relinkAsset(const QString& id);

    EditorController* editor_;
    PreviewWidget* preview_;
    TimelineWidget* timeline_;
    LooksPanel* looks_;
    GradePanel* grade_;
    ScopesPanel* scopes_;
    CoverPanel* covers_;
    TransitionsPanel* transitions_;
    TextPanel* text_;
    SubtitlePanel* subtitles_;
    AudioPanel* audio_ = nullptr;
    MixerPanel* mixer_ = nullptr;
    ClipToolsPanel* clipTools_ = nullptr;
    StickerPanel* stickers_;
    OverlayPanel* overlays_;
    VideoFxPanel* videoFx_;
    RecentStrip* recent_;
    QListWidget* assets_;
    QLabel* timecode_;
    QLabel* inspector_;
    QToolButton* playBtn_;
    QToolButton* exportBtn_;
    QAction* undo_;
    QAction* redo_;
    QAction* snapAct_ = nullptr;
    // Keyframe bar under the transport: picture and audio keys are separate.
    QToolButton* kfPicture_ = nullptr;
    QToolButton* kfAudio_ = nullptr;
    QToolButton* kfPrev_ = nullptr;
    QToolButton* kfNext_ = nullptr;
    QToolButton* kfResetZoom_ = nullptr;
    QSlider* kfVolume_ = nullptr;
    QLabel* kfVolumeLabel_ = nullptr;
    QLabel* kfInfo_ = nullptr;
    QToolBar* tlBar_ = nullptr;  // timeline tools: snapping, zoom
    QByteArray defaultState_;  // dock layout as built, for "reset layout"
    QHash<QString, QIcon> icons_;  // key: asset id + missing flag
};
