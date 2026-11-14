// "Mixer": one strip per track that carries sound (audio tracks and video tracks with sound):
// volume, pan, mute (M) and solo (S). Rebuilt only when the track list changes, so dragging a
// fader never rebuilds the panel; each release is one undo step.
#pragma once

#include <QWidget>

class EditorController;
class QGridLayout;

class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] int strips() const { return strips_; }  // for tests

private:
    void rebuild();
    EditorController* editor_;
    QGridLayout* grid_;
    QWidget* box_;
    QString signature_;
    int strips_ = 0;
};
