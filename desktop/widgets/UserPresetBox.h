// "Preset của tôi" (FR-022): save the selected clip's colour and effects under a name, then
// apply them to other clips in one tap (or to every clip), rename or delete them. Presets are
// shared by all projects (EditorController::userPresets, vstudio/user_presets.hpp).
#pragma once

#include <QWidget>

class EditorController;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;

class UserPresetBox : public QWidget {
    Q_OBJECT
public:
    explicit UserPresetBox(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QComboBox* list() const { return list_; }  // for tests
    [[nodiscard]] QPushButton* applyButton() const { return apply_; }

private:
    void reload();
    void refresh();
    void saveCurrent();

    EditorController* editor_;
    QComboBox* list_;
    QPushButton* apply_;
    QPushButton* save_;
    QToolButton* more_;
    QLabel* summary_;
};
