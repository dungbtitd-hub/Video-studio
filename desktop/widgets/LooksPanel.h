// "Bộ lọc màu": one-tap colour looks for the selected clip (video or photo).
// Thumbnails show each look on the selected clip's own frame (from the media
// library), so the user sees the result before tapping. Above the looks:
// the clip's Log/HDR input (camera Log -> Rec.709) and an optional .cube LUT.
#pragma once

#include <QHash>
#include <QImage>
#include <QWidget>

class EditorController;
class QLabel;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QSlider;
class QPushButton;

class LooksPanel : public QWidget {
    Q_OBJECT
public:
    explicit LooksPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QListWidget* list() const { return list_; }  // for tests
    [[nodiscard]] QSlider* strength() const { return strength_; }
    [[nodiscard]] QComboBox* input() const { return input_; }

private:
    void refresh();                 // selection or project changed
    void rebuildIcons();            // base frame changed
    [[nodiscard]] QImage baseFrame() const;  // selected clip's frame, or a sample picture
    void apply(const QString& lookId);

    EditorController* editor_;
    QLabel* hint_;
    QListWidget* list_;
    QSlider* strength_;
    QLabel* strengthLabel_;
    QPushButton* applyAll_;
    QPushButton* clear_;
    // Source: Log/HDR input conversion and a LUT, applied before the look.
    QComboBox* input_;
    QLabel* inputHint_;
    QPushButton* lut_;
    QPushButton* lutClear_;
    QString baseKey_;  // asset id the icons were rendered from ("" = sample)
};
