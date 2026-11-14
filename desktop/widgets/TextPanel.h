// "Chữ": add a text at the playhead and edit the selected one: words, font, size, colour,
// alignment, outline/shadow/background, one-tap styles and in/out/loop animations.
// Each change is one undo step; typing is committed after a short pause.
#pragma once

#include <QWidget>

class AnimationBox;
class EditorController;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;

class TextPanel : public QWidget {
    Q_OBJECT
public:
    explicit TextPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QPushButton* addButton() const { return add_; }  // for tests
    [[nodiscard]] QPlainTextEdit* textEdit() const { return text_; }
    [[nodiscard]] QComboBox* fontBox() const { return font_; }
    [[nodiscard]] QListWidget* styles() const { return styles_; }
    [[nodiscard]] AnimationBox* animations() const { return anim_; }
    void commitText();  // pending typing, now

private:
    void refresh();
    void setColor(const char* key);

    EditorController* editor_;
    QPushButton* add_;
    QLabel* hint_;
    QWidget* editorBox_;
    QPlainTextEdit* text_;
    QTimer* typing_;
    QComboBox* font_;
    QSlider* size_;
    QPushButton* color_;
    QToolButton* align_[3];
    QCheckBox* italic_;
    QSlider* outline_;
    QSlider* shadow_;
    QSlider* box_;
    QListWidget* styles_;
    AnimationBox* anim_;
    QString shownClip_;
};
