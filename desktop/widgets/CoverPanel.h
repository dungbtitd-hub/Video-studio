// "Che mờ": cover parts of the selected clip (a logo, a face, a number plate).
// Pick a style (thumbnails show it on the clip's own frame), a shape, then
// drag the region on the preview. "Bám theo" follows a moving subject.
#pragma once

#include <QImage>
#include <QWidget>

class EditorController;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;

class CoverPanel : public QWidget {
    Q_OBJECT
public:
    explicit CoverPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QListWidget* styles() const { return styles_; }  // for tests
    [[nodiscard]] QListWidget* coverList() const { return covers_; }
    [[nodiscard]] QPushButton* addButton() const { return add_; }
    [[nodiscard]] QPushButton* trackButton() const { return track_; }
    [[nodiscard]] QComboBox* shape() const { return shape_; }

private:
    void refresh();
    void rebuildIcons();
    [[nodiscard]] QImage baseFrame() const;
    void slider(QSlider*& s, QLabel*& label, const QString& name, int max, const char* key, double scale);

    EditorController* editor_;
    QLabel* hint_;
    QListWidget* covers_;
    QPushButton* add_;
    QPushButton* remove_;
    QListWidget* styles_;
    QComboBox* shape_;
    QSlider* amount_ = nullptr;
    QLabel* amountLabel_ = nullptr;
    QSlider* feather_ = nullptr;
    QLabel* featherLabel_ = nullptr;
    QSlider* rotation_ = nullptr;
    QLabel* rotationLabel_ = nullptr;
    QCheckBox* invert_;
    QPushButton* color_;
    QCheckBox* follow_;
    QPushButton* track_;
    QProgressBar* trackBar_;
    QString baseKey_ = "?";
};
