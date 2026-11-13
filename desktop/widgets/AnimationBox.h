// In / out / loop animations of the selected text or sticker, with a small preview that
// plays the clip's look (in, a moment at rest, out) over and over.
#pragma once

#include <QWidget>

#include "vstudio/model.hpp"

class EditorController;
class QComboBox;
class QLabel;
class QSlider;
class QTimer;

class AnimationBox : public QWidget {
    Q_OBJECT
public:
    explicit AnimationBox(EditorController* editor, QWidget* parent = nullptr);
    // Shows `content` (nullptr: nothing selected).
    void setContent(const vstudio::Json* content);
    [[nodiscard]] QComboBox* inBox() const { return in_; }  // for tests
    [[nodiscard]] QComboBox* outBox() const { return out_; }
    [[nodiscard]] QComboBox* loopBox() const { return loop_; }
    [[nodiscard]] QLabel* preview() const { return preview_; }
    // Draws content at `local` seconds of a clip lasting `length` into a w x h image (transparent).
    static QImage render(const vstudio::Json& content, double local, double length, int w, int h);

private:
    void commit(const char* key, QComboBox* box, QSlider* duration);
    void tick();

    EditorController* editor_;
    QComboBox *in_, *out_, *loop_;
    QSlider *inLen_, *outLen_;
    QLabel* preview_;
    QTimer* timer_;
    vstudio::Json content_;
    double t_ = 0;
};
