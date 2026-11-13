// Qt Widgets workspace (prototype to compare with the QML workspace):
// dockable panels around the preview, toolbar, timeline at the bottom.
#pragma once

#include <QHash>
#include <QIcon>
#include <QMainWindow>

class EditorController;
class PreviewWidget;
class TimelineWidget;
class LooksPanel;
class QListWidget;
class QLabel;
class QToolButton;
class QCloseEvent;
class QToolBar;
class QSlider;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] TimelineWidget* timeline() const { return timeline_; }
    [[nodiscard]] LooksPanel* looks() const { return looks_; }
    // Every shortcut in the menus, as shown in Trợ giúp > Phím tắt (menu, action, keys).
    [[nodiscard]] QList<QStringList> shortcutTable() const;
    // Layout persistence: QSettings group "workspace" (geometry + dock state).
    void saveLayout() const;
    void resetLayout();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    void importMedia();
    void exportVideo();
    QString choosePreset();  // empty = cancelled
    void showShortcuts();
    bool confirmDiscard();  // false = user cancelled
    void refreshAssets();
    void refreshState();
    void refreshKeyframes();  // keyframe bar + region select, from the clip under the playhead
    QIcon assetIcon(const QString& id);  // cached until a new thumbnail strip arrives
    void relinkAsset(const QString& id);

    EditorController* editor_;
    PreviewWidget* preview_;
    TimelineWidget* timeline_;
    LooksPanel* looks_;
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
