// "Công cụ clip": reverse, freeze frame, chroma key (key colour picked from the preview,
// similarity / smoothness / spill) and stabilisation (measured in the background, strength
// re-smooths without measuring again) for the selected video clip.
#pragma once

#include <QWidget>

class EditorController;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;

class ClipToolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ClipToolsPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QCheckBox* reverse() const { return reverse_; }  // for tests
    [[nodiscard]] QPushButton* freezeButton() const { return freeze_; }
    [[nodiscard]] QCheckBox* chroma() const { return chroma_; }
    [[nodiscard]] QPushButton* stabilizeButton() const { return stab_; }

private:
    void refresh();
    void commitChroma();

    EditorController* editor_;
    QLabel* hint_;
    QWidget* body_;
    QCheckBox* reverse_;
    QDoubleSpinBox* freezeSecs_;
    QPushButton* freeze_;
    QCheckBox* chroma_;
    QPushButton* keyColor_;
    QString color_ = "#00ff00";
    QSlider* similarity_;
    QSlider* smoothness_;
    QSlider* spill_;
    QSlider* stabStrength_;
    QPushButton* stab_;
    QPushButton* stabRemove_;
    QProgressBar* stabProgress_;
};
