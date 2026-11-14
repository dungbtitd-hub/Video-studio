// A speed ramp drawn over a clip: points (position 0..1 along the clip, speed) joined by
// straight lines on a log speed axis (0.1x .. 10x, 1x in the middle). Drag a point to move
// it; double-click empty space to add one, double-click a point to remove it. The two end
// points stay at the clip's ends. `edited` fires when a drag or a double-click ends.
#pragma once

#include <QPointF>
#include <QVector>
#include <QWidget>

class SpeedCurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit SpeedCurveEditor(QWidget* parent = nullptr);
    void setPoints(const QVector<QPointF>& points);  // empty = flat 1x
    [[nodiscard]] const QVector<QPointF>& points() const { return pts_; }
    [[nodiscard]] QSize sizeHint() const override { return {260, 120}; }

signals:
    void edited(const QVector<QPointF>& points);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    [[nodiscard]] QRectF plot() const;
    [[nodiscard]] QPointF toWidget(const QPointF& p) const;
    [[nodiscard]] QPointF fromWidget(const QPointF& w) const;
    [[nodiscard]] int hit(const QPointF& w) const;

    QVector<QPointF> pts_;
    int drag_ = -1;
};
