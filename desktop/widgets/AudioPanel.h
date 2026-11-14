// "Âm thanh": sound of the selected clip - volume, fade in/out, equaliser (presets + 5 bands),
// noise reduction (measures the clip's background noise first), "Tách âm thanh" (sound to its
// own audio track) and voice-over recording at the playhead. Sliders commit on release
// (one undo step each).
#pragma once

#include <QWidget>

class EditorController;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

class AudioPanel : public QWidget {
    Q_OBJECT
public:
    explicit AudioPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QSlider* fadeIn() const { return fadeIn_; }  // for tests
    [[nodiscard]] QSlider* fadeOut() const { return fadeOut_; }
    [[nodiscard]] QComboBox* eqPreset() const { return eqPreset_; }
    [[nodiscard]] QSlider* band(int i) const { return bands_[i]; }
    [[nodiscard]] QPushButton* detachButton() const { return detach_; }
    [[nodiscard]] QPushButton* recordButton() const { return record_; }

private:
    void refresh();
    void commitFades();
    void commitEq();

    EditorController* editor_;
    QLabel* hint_;
    QWidget* body_;
    QSlider* volume_;
    QSlider* fadeIn_;
    QSlider* fadeOut_;
    QComboBox* eqPreset_;
    QSlider* bands_[5];
    QSlider* denoise_;
    QPushButton* denoiseBtn_;
    QPushButton* detach_;
    QPushButton* record_;
    QLabel* recordHint_;
};
