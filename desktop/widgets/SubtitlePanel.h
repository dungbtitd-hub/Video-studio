// "Phụ đề": subtitle tracks and their lines.
//   * Add a line at the playhead, type it; import/export .srt .vtt .ass; shift a whole track.
//   * The list shows every line (time in/out, text); edit in place, click to jump there.
//   * One-tap styles, bundled + user fonts ("Thêm phông…"), size, colour, outline, background.
//   * Positions: 7 presets, margin, or drag the line on the preview; "Chỉ dòng này" gives one
//     line its own look/place.
//   * "Dịch bằng AI…": any connector (Claude, OpenAI-compatible, Gemini, a local model, the open
//     protocol or a program on this computer), result as a new track, bilingual or in place.
// Every change is one undo step. The list reads the project snapshot through a model: a track
// of thousands of lines scrolls without building a widget per line.
#pragma once

#include <QDialog>
#include <QWidget>

#include "vstudio/model.hpp"

class EditorController;
class CueModel;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTableView;
class QTimer;
class QToolButton;

class SubtitlePanel : public QWidget {
    Q_OBJECT
public:
    explicit SubtitlePanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QPushButton* addButton() const { return add_; }  // for tests
    [[nodiscard]] QTableView* cueList() const { return list_; }
    [[nodiscard]] QPlainTextEdit* textEdit() const { return text_; }
    [[nodiscard]] QListWidget* styles() const { return styles_; }
    [[nodiscard]] QComboBox* trackBox() const { return track_; }
    [[nodiscard]] QPushButton* positionButton(int i) const { return pos_[i]; }
    void commitText();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void refresh();
    void refreshTracks();
    [[nodiscard]] vstudio::Json effectiveStyle() const;
    [[nodiscard]] bool lineOnly() const;
    void applyStyle(const vstudio::Json& patch);
    void applyPosition(const vstudio::Json& patch);
    void openTranslate();

    EditorController* editor_;
    CueModel* model_;
    QComboBox* track_;
    QToolButton* newTrack_;
    QPushButton* add_;
    QPushButton* import_;
    QPushButton* export_;
    QTableView* list_;
    QLabel* hint_;
    QWidget* lineBox_;
    QPlainTextEdit* text_;
    QTimer* typing_;
    QCheckBox* only_;
    QWidget* styleBox_;
    QListWidget* styles_;
    QComboBox* font_;
    QSlider* size_;
    QPushButton* color_;
    QSlider* outline_;
    QSlider* box_;
    QPushButton* pos_[7];
    QSlider* margin_;
    QDoubleSpinBox* shift_;
    QPushButton* translate_;
    QProgressBar* progress_;
    QPushButton* cancel_;
    QLabel* aiStatus_;
    QString shownClip_;
};

// Connector, languages and mode for an AI translation. Settings (address, model, key) are
// remembered per connector on this computer.
class TranslateDialog : public QDialog {
    Q_OBJECT
public:
    explicit TranslateDialog(QWidget* parent = nullptr);
    [[nodiscard]] vstudio::Json config() const;
    [[nodiscard]] QString from() const;
    [[nodiscard]] QString to() const;
    [[nodiscard]] QString notes() const;
    [[nodiscard]] int mode() const;
    [[nodiscard]] int batchSize() const;
    void save() const;

private:
    void presetChanged();

    QComboBox* preset_;
    QLabel* hint_;
    QLineEdit* url_;
    QLineEdit* model_;
    QLineEdit* key_;
    QLineEdit* command_;
    QWidget* urlRow_;
    QWidget* commandRow_;
    QComboBox* from_;
    QComboBox* to_;
    QPlainTextEdit* notes_;
    QRadioButton* modes_[3];
    QSpinBox* batch_;
    QString shown_;
};
