// "Hiệu ứng video": zoom, rotate and reflection effects for the selected clip (video or photo).
// Three groups (Phóng to, Xoay, Phản chiếu); a clip can have one of each. Thumbnails show the effect
// on the clip's own frame and play while the mouse is over them; a click applies it. Sliders set the
// amount, the speed and, for reflections, where the water line sits (one undo step on release).
#pragma once

#include <QImage>
#include <QWidget>

class EditorController;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QTimer;

class VideoFxPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideoFxPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QComboBox* group() const { return group_; }  // for tests
    [[nodiscard]] QListWidget* list() const { return list_; }
    [[nodiscard]] QSlider* amount() const { return amount_; }
    [[nodiscard]] QSlider* line() const { return line_; }
    [[nodiscard]] QPushButton* clearButton() const { return clear_; }

private:
    void fill();     // the group's effects
    void refresh();  // selection or project changed
    void apply(const QString& fxId);
    void commitSliders();
    [[nodiscard]] QString currentGroup() const;
    [[nodiscard]] QImage icon(const QString& id, double seconds) const;

    EditorController* editor_;
    QLabel* hint_;
    QComboBox* group_;
    QListWidget* list_;
    QSlider* amount_;
    QSlider* speed_;
    QSlider* line_;
    QLabel* lineLabel_;
    QPushButton* clear_;
    QLabel* active_;
    QTimer* hoverTimer_;
    QListWidgetItem* hovered_ = nullptr;
    double hoverT_ = 0;
    QImage base_;  // the selected clip's frame (or empty: the built-in landscape)
    QString baseKey_;
};
