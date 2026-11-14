// "Lớp phủ": animated overlay effects (snow, rain, petals, confetti, fireworks, light leaks,
// old film...) by category. Click one to lay it over the whole frame from the playhead
// (hovering plays it); drag its box on the preview to cover only part of the picture, and its
// clip on the timeline for how long. With an overlay selected: density, speed, particle size,
// strength, soft edge, colour, fade in/out, and "whole frame".
#pragma once

#include <QListWidget>
#include <QWidget>

class EditorController;
class QComboBox;
class QPushButton;
class QSlider;
class QTimer;

class OverlayPanel : public QWidget {
    Q_OBJECT
public:
    explicit OverlayPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QListWidget* list() const { return list_; }  // for tests
    [[nodiscard]] QComboBox* category() const { return category_; }
    [[nodiscard]] QSlider* slider(const QString& key) const;  // density, speed, size, opacity, feather, fade
    [[nodiscard]] QPushButton* fullFrameButton() const { return full_; }
    [[nodiscard]] QWidget* settings() const { return editorBox_; }
    static QImage icon(const QString& id, double seconds, int w, int h);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void fill();
    void refresh();

    EditorController* editor_;
    QComboBox* category_;
    QListWidget* list_;
    QWidget* editorBox_;
    QList<QPair<QString, QSlider*>> sliders_;
    QPushButton* color_;
    QPushButton* colorReset_;
    QPushButton* full_;
    QTimer* hoverTimer_;
    QListWidgetItem* hovered_ = nullptr;
    double hoverT_ = 0;
};
