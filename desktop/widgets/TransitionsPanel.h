// "Chuyển cảnh": transitions between clips. Drag one onto a cut on the timeline,
// or select a cut (its badge, or a clip: its start) and click one. Hovering a
// thumbnail plays it. Length is set with the slider; "Áp dụng cho tất cả" puts
// the transition on every cut.
#pragma once

#include <QImage>
#include <QListWidget>
#include <QWidget>

class EditorController;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

// Thumbnails that drag out as a transition id (TimelineWidget::kTransitionMime).
class TransitionList : public QListWidget {
    Q_OBJECT
public:
    using QListWidget::QListWidget;

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
};

class TransitionsPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransitionsPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QListWidget* list() const { return list_; }  // for tests
    [[nodiscard]] QSlider* duration() const { return duration_; }
    [[nodiscard]] QPushButton* applyAllButton() const { return applyAll_; }
    [[nodiscard]] QPushButton* removeButton() const { return remove_; }
    [[nodiscard]] QLabel* placeLabel() const { return place_; }
    // Frame of a transition for thumbnails (two sample pictures), progress 0..1.
    static QImage thumbnail(const QString& type, double progress);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void refresh();
    void apply(const QString& type);
    [[nodiscard]] double seconds() const;

    EditorController* editor_;
    QLabel* hint_;
    QLabel* place_;
    TransitionList* list_;
    QSlider* duration_;
    QLabel* durationLabel_;
    QPushButton* applyAll_;
    QPushButton* remove_;
    QTimer* anim_;
    QListWidgetItem* hovered_ = nullptr;
    double phase_ = 0;
};
