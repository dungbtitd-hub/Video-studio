#include "SpeedCurveEditor.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kMin = 0.1, kMax = 10.0;
double toY(double v) { return std::log10(std::clamp(v, kMin, kMax)); }  // -1 .. 1
}  // namespace

SpeedCurveEditor::SpeedCurveEditor(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(110);
    setMouseTracking(true);
    setToolTip("Kéo điểm để đổi tốc độ. Nhấp đúp chỗ trống để thêm điểm, nhấp đúp vào điểm để xóa.");
    setPoints({});
}

void SpeedCurveEditor::setPoints(const QVector<QPointF>& points) {
    if (drag_ >= 0) return;  // never yank a point from under the mouse
    pts_ = points;
    if (pts_.size() < 2) pts_ = {QPointF(0, 1), QPointF(1, 1)};
    std::sort(pts_.begin(), pts_.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    pts_.front().setX(0);
    pts_.back().setX(1);
    update();
}

QRectF SpeedCurveEditor::plot() const { return QRectF(rect()).adjusted(34, 8, -8, -8); }

QPointF SpeedCurveEditor::toWidget(const QPointF& p) const {
    const QRectF r = plot();
    return {r.left() + p.x() * r.width(), r.center().y() - toY(p.y()) * r.height() / 2};
}

QPointF SpeedCurveEditor::fromWidget(const QPointF& w) const {
    const QRectF r = plot();
    const double u = std::clamp((w.x() - r.left()) / r.width(), 0.0, 1.0);
    const double y = std::clamp((r.center().y() - w.y()) / (r.height() / 2), -1.0, 1.0);
    double v = std::pow(10.0, y);
    if (std::abs(v - 1.0) < 0.06) v = 1.0;  // snap to normal speed
    return {u, std::round(v * 100) / 100};
}

int SpeedCurveEditor::hit(const QPointF& w) const {
    for (int i = 0; i < pts_.size(); ++i)
        if (QLineF(toWidget(pts_[i]), w).length() < 8) return i;
    return -1;
}

void SpeedCurveEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plot();
    p.fillRect(r, QColor(30, 33, 40));
    p.setPen(QColor(120, 128, 140));
    p.setFont(QFont(font().family(), 8));
    for (double v : {0.1, 0.5, 1.0, 2.0, 10.0}) {
        const double y = r.center().y() - toY(v) * r.height() / 2;
        p.setPen(QPen(QColor(70, 76, 88), v == 1.0 ? 1.5 : 1, v == 1.0 ? Qt::SolidLine : Qt::DotLine));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(QColor(150, 158, 170));
        p.drawText(QRectF(0, y - 7, r.left() - 4, 14), Qt::AlignRight | Qt::AlignVCenter, QString("%1x").arg(v));
    }
    QPainterPath path(toWidget(pts_.front()));
    for (int i = 1; i < pts_.size(); ++i) path.lineTo(toWidget(pts_[i]));
    p.setPen(QPen(QColor(90, 170, 255), 2));
    p.drawPath(path);
    for (int i = 0; i < pts_.size(); ++i) {
        p.setBrush(i == drag_ ? QColor(255, 255, 255) : QColor(90, 170, 255));
        p.setPen(Qt::NoPen);
        p.drawEllipse(toWidget(pts_[i]), 5, 5);
    }
}

void SpeedCurveEditor::mousePressEvent(QMouseEvent* e) {
    drag_ = hit(e->position());
    update();
}

void SpeedCurveEditor::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ < 0) return;
    QPointF q = fromWidget(e->position());
    if (drag_ == 0) q.setX(0);
    else if (drag_ == pts_.size() - 1) q.setX(1);
    else q.setX(std::clamp(q.x(), pts_[drag_ - 1].x() + 0.01, pts_[drag_ + 1].x() - 0.01));
    pts_[drag_] = q;
    update();
}

void SpeedCurveEditor::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ < 0) return;
    drag_ = -1;
    update();
    emit edited(pts_);
}

void SpeedCurveEditor::mouseDoubleClickEvent(QMouseEvent* e) {
    const int i = hit(e->position());
    if (i > 0 && i < pts_.size() - 1) {
        pts_.remove(i);
    } else if (i < 0) {
        const QPointF q = fromWidget(e->position());
        auto it = std::lower_bound(pts_.begin(), pts_.end(), q, [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
        pts_.insert(it, q);
    } else {
        return;
    }
    update();
    emit edited(pts_);
}
