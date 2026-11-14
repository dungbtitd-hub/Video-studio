// "Sticker": the in-house animated sticker library by category. Click one to add it at the
// playhead (hovering plays it); with a sticker selected, set its size, mirror it, turn its own
// motion on or off and pick in/out/loop animations.
#pragma once

#include <QListWidget>
#include <QWidget>

class AnimationBox;
class EditorController;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QTimer;

class StickerPanel : public QWidget {
    Q_OBJECT
public:
    explicit StickerPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QListWidget* list() const { return list_; }  // for tests
    [[nodiscard]] QComboBox* category() const { return category_; }
    [[nodiscard]] AnimationBox* animations() const { return anim_; }
    static QImage icon(const QString& id, double seconds, int size);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void fill();
    void refresh();

    EditorController* editor_;
    QComboBox* category_;
    QListWidget* list_;
    QWidget* editorBox_;
    QSlider* size_;
    QCheckBox* flip_;
    QCheckBox* alive_;
    AnimationBox* anim_;
    QTimer* hoverTimer_;
    QListWidgetItem* hovered_ = nullptr;
    double hoverT_ = 0;
};
