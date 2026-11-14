// "Công cụ clip": reverse, freeze frame, chroma key (key colour picked from the preview,
// similarity / smoothness / spill) and stabilisation (measured in the background, strength
// re-smooths without measuring again) for the selected video clip.
#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>

class EditorController;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class SpeedCurveEditor;

class ClipToolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ClipToolsPanel(EditorController* editor, QWidget* parent = nullptr);
    [[nodiscard]] QCheckBox* reverse() const { return reverse_; }  // for tests
    [[nodiscard]] QSlider* speedSlider() const { return speed_; }
    [[nodiscard]] QComboBox* speedCurve() const { return curveList_; }
    [[nodiscard]] SpeedCurveEditor* curveEditor() const { return curve_; }
    [[nodiscard]] QPushButton* freezeButton() const { return freeze_; }
    [[nodiscard]] QCheckBox* chroma() const { return chroma_; }
    [[nodiscard]] QPushButton* stabilizeButton() const { return stab_; }
    [[nodiscard]] QComboBox* pluginList() const { return pluginList_; }
    [[nodiscard]] QCheckBox* pluginOn() const { return pluginOn_; }

private:
    void refresh();
    void commitChroma();
    void commitSpeed(const QVector<QPointF>& points, const QString& preset);

    EditorController* editor_;
    QLabel* hint_;
    QWidget* body_;
    QCheckBox* reverse_;
    QSlider* speed_;
    QLabel* speedLabel_;
    QComboBox* curveList_;
    SpeedCurveEditor* curve_;
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
    QGroupBox* pluginBox_;
    QComboBox* pluginList_;
    QCheckBox* pluginOn_;
    QLabel* pluginNote_;
    int pluginCount_ = -1;
};
