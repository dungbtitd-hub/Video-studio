// Qt Widgets workspace (prototype to compare with the QML workspace):
// dockable panels around the preview, toolbar, timeline at the bottom.
#pragma once

#include <QHash>
#include <QIcon>
#include <QMainWindow>

class EditorController;
class PreviewWidget;
class TimelineWidget;
class QListWidget;
class QLabel;
class QToolButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] TimelineWidget* timeline() const { return timeline_; }

private:
    void refreshAssets();
    void refreshState();
    QIcon assetIcon(const QString& id);  // cached until a new thumbnail strip arrives
    void relinkAsset(const QString& id);

    EditorController* editor_;
    PreviewWidget* preview_;
    TimelineWidget* timeline_;
    QListWidget* assets_;
    QLabel* timecode_;
    QLabel* inspector_;
    QToolButton* playBtn_;
    QToolButton* exportBtn_;
    QAction* undo_;
    QAction* redo_;
    QHash<QString, QIcon> icons_;  // key: asset id + missing flag
};
