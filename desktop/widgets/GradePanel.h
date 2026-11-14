// "Chỉnh màu": pro grading for the selected clip (video or photo).
// Tabs: colour wheels (+ contrast, pivot, saturation), curves (master / R / G / B),
// HSL mixer (8 colour bands) and a qualifier that picks one colour range
// (eyedropper on the preview) and changes only that.
// Dragging any control shows the result live on the preview without touching
// the project; releasing commits one undo step.
#pragma once

#include <QHash>
#include <QPair>
#include <QWidget>

#include "vstudio/model.hpp"

class EditorController;
class ColorWheel;
class CurveEditor;
class QButtonGroup;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;

class GradePanel : public QWidget {
    Q_OBJECT
public:
    explicit GradePanel(EditorController* editor, QWidget* parent = nullptr);

    // For tests and the eyedropper.
    [[nodiscard]] vstudio::Json params() const { return params_; }
    [[nodiscard]] ColorWheel* wheel(int i) const { return wheels_[i]; }  // 0 lift, 1 gamma, 2 gain, 3 offset
    [[nodiscard]] CurveEditor* curve() const { return curve_; }
    [[nodiscard]] QSlider* slider(const QString& name) const;
    [[nodiscard]] QTabWidget* tabs() const { return tabs_; }
    void pickColor(const QColor& c);  // eyedropper result: select this colour in the qualifier

signals:
    void pickRequested(bool on);  // ask the preview for (or cancel) an eyedropper click

private:
    struct Slider {
        QSlider* s = nullptr;
        QLabel* value = nullptr;
        double scale = 100.0;  // slider units per parameter unit
        double def = 0.0;
    };
    Slider addSlider(QWidget* parent, class QGridLayout* grid, int row, const QString& label, const QString& key,
                     int min, int max, double scale, double def);
    void load();                // selected clip -> controls
    void collect();             // controls -> params_
    void changed();             // a control moved: live preview
    void commit();              // a control was released: one undo step
    [[nodiscard]] QString curveKey() const;
    void loadCurve();
    void loadBand();
    void refreshEnabled();

    EditorController* editor_;
    QString clip_;
    vstudio::Json params_ = vstudio::Json::object();
    bool loading_ = false;

    QLabel* hint_;
    QTabWidget* tabs_;
    ColorWheel* wheels_[4]{};
    QHash<QString, Slider> sliders_;
    CurveEditor* curve_;
    QButtonGroup* channel_;
    QButtonGroup* band_;
    QCheckBox* qualOn_;
    QCheckBox* invert_;
    QCheckBox* showMask_;
    QPushButton* pick_;
    QPushButton* compare_;
    QPushButton* reset_;
    QPushButton* applyAll_;
};
