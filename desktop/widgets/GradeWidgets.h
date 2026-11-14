// Controls for the "Chỉnh màu" panel: a colour wheel (balance puck + master
// ring value) and a curve editor. Both emit `changed` continuously while
// dragged (live preview) and `committed` once on release (one undo step).
#pragma once

#include <array>

#include <QColor>
#include <QPointF>
#include <QVector>
#include <QWidget>

class QSlider;

// A colour wheel: the puck sets the colour balance (angle = hue pushed towards,
// distance = amount); the slider under it is the master (brightness) value.
// Double-click the wheel to centre the puck.
class ColorWheel : public QWidget {
    Q_OBJECT
public:
    ColorWheel(const QString& title, QWidget* parent = nullptr);
    // balance r,g,b (luma-neutral, as stored) and master -1..1
    void setValues(const std::array<double, 3>& balance, double master);
    [[nodiscard]] std::array<double, 3> balance() const;
    [[nodiscard]] double master() const;
    [[nodiscard]] bool dragging() const;
    void resetValues();
    // For tests: move the puck as if dragged to wheel coordinates (angle deg, distance 0..1).
    void dragPuckTo(double angleDeg, double distance);

signals:
    void changed();
    void committed();

private:
    class Disc;
    Disc* disc_;
    QSlider* master_;
};

// Curve editor for one channel at a time: click to add a point, drag to move,
// double-click (or drag off the side) to remove one. The two end points stay.
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget* parent = nullptr);
    void setPoints(const QVector<QPointF>& pts);  // 0..1, sorted by x
    [[nodiscard]] QVector<QPointF> points() const { return pts_; }
    void setColor(const QColor& c) { color_ = c; update(); }
    void setHistogram(const QVector<float>& h) { hist_ = h; update(); }  // optional backdrop
    [[nodiscard]] bool dragging() const { return drag_ >= 0; }
    [[nodiscard]] QSize sizeHint() const override { return {260, 260}; }

signals:
    void changed();
    void committed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    [[nodiscard]] QRectF area() const;
    [[nodiscard]] QPointF toWidget(QPointF p) const;
    [[nodiscard]] QPointF toCurve(QPointF w) const;
    [[nodiscard]] int hit(QPointF w) const;
    QVector<QPointF> pts_{{0, 0}, {1, 1}};
    QVector<float> hist_;
    QColor color_{230, 230, 230};
    int drag_ = -1;
};
