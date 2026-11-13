#include "GradeWidgets.h"

#include <QConicalGradient>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "Theme.h"
#include "vstudio/grade.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ------------------------------------------------------------------ wheel

class ColorWheel::Disc : public QWidget {
public:
    explicit Disc(ColorWheel* owner) : QWidget(owner), owner_(owner) {
        setMinimumSize(110, 110);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::PointingHandCursor);
    }
    double angle = 0, dist = 0;
    bool dragging = false;

    [[nodiscard]] QSize sizeHint() const override { return {130, 130}; }
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int w) const override { return w; }

    void setFromPos(QPointF p) {
        const QPointF c = rect().center();
        const double r = radius();
        const double dx = (p.x() - c.x()) / r, dy = -(p.y() - c.y()) / r;
        dist = std::min(1.0, std::hypot(dx, dy));
        angle = std::atan2(dy, dx) * 180.0 / kPi;
        if (angle < 0) angle += 360;
        update();
        emit owner_->changed();
    }

protected:
    [[nodiscard]] double radius() const { return std::min(width(), height()) / 2.0 - 6; }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c = rect().center();
        const double r = radius();
        // Hue ring at full chroma fading to grey in the centre (the colour the puck pushes towards).
        QConicalGradient hue(c, 0);
        for (int i = 0; i <= 12; ++i) hue.setColorAt(i / 12.0, QColor::fromHsvF(float(i / 12.0), 0.75f, 0.8f));
        p.setPen(Qt::NoPen);
        p.setBrush(hue);
        p.drawEllipse(c, r, r);
        QRadialGradient fade(c, r);
        fade.setColorAt(0, QColor(40, 44, 52, 255));
        fade.setColorAt(0.85, QColor(40, 44, 52, 90));
        fade.setColorAt(1, QColor(40, 44, 52, 0));
        p.setBrush(fade);
        p.drawEllipse(c, r, r);
        p.setPen(QPen(Theme::line(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r, r);
        p.drawLine(QPointF(c.x() - 6, c.y()), QPointF(c.x() + 6, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 6), QPointF(c.x(), c.y() + 6));
        const double a = angle * kPi / 180.0;
        const QPointF puck(c.x() + std::cos(a) * dist * r, c.y() - std::sin(a) * dist * r);
        if (dist > 0) {
            p.setPen(QPen(QColor(255, 255, 255, 120), 1.2));
            p.drawLine(c, puck);
        }
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(dist > 0 ? QColor::fromHsvF(float(angle / 360.0), float(dist), 1.0f) : Theme::raised());
        p.drawEllipse(puck, 6, 6);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        dragging = true;
        // Fine control: the puck moves relative to the press, at a third of the mouse speed with Shift.
        press_ = e->position();
        startAngle_ = angle;
        startDist_ = dist;
        if (!(e->modifiers() & Qt::ShiftModifier)) setFromPos(e->position());
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging) return;
        if (e->modifiers() & Qt::ShiftModifier) {
            const QPointF c = rect().center();
            const double r = radius(), a = startAngle_ * kPi / 180.0;
            const QPointF start(c.x() + std::cos(a) * startDist_ * r, c.y() - std::sin(a) * startDist_ * r);
            setFromPos(start + (e->position() - press_) / 3.0);
        } else {
            setFromPos(e->position());
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton || !dragging) return;
        dragging = false;
        emit owner_->committed();
    }
    void mouseDoubleClickEvent(QMouseEvent*) override {
        dragging = false;
        angle = dist = 0;
        update();
        emit owner_->changed();
        emit owner_->committed();
    }

private:
    ColorWheel* owner_;
    QPointF press_;
    double startAngle_ = 0, startDist_ = 0;
};

ColorWheel::ColorWheel(const QString& title, QWidget* parent) : QWidget(parent) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(2, 2, 2, 2);
    l->setSpacing(3);
    auto* t = new QLabel(title, this);
    t->setAlignment(Qt::AlignCenter);
    t->setStyleSheet("color: #a4abb8; font-size: 11px;");
    l->addWidget(t);
    disc_ = new Disc(this);
    disc_->setToolTip("Kéo để đẩy màu; Shift để chỉnh tinh; nhấp đúp để đặt lại.");
    l->addWidget(disc_, 1);
    master_ = new QSlider(Qt::Horizontal, this);
    master_->setRange(-100, 100);
    master_->setToolTip("Độ sáng của vùng này; nhấp đúp nhãn để đặt lại.");
    l->addWidget(master_);
    connect(master_, &QSlider::valueChanged, this, [this] {
        emit changed();
        if (!master_->isSliderDown()) emit committed();  // keyboard / wheel steps
    });
    connect(master_, &QSlider::sliderReleased, this, &ColorWheel::committed);
}

void ColorWheel::setValues(const std::array<double, 3>& balance, double master) {
    double a = 0, d = 0;
    vstudio::balanceToWheel(balance, &a, &d);
    disc_->angle = d > 1e-9 ? a : 0;
    disc_->dist = d;
    disc_->update();
    QSignalBlocker b(master_);
    master_->setValue(int(std::lround(master * 100)));
}

std::array<double, 3> ColorWheel::balance() const { return vstudio::wheelToBalance(disc_->angle, disc_->dist); }
double ColorWheel::master() const { return master_->value() / 100.0; }
bool ColorWheel::dragging() const { return disc_->dragging || master_->isSliderDown(); }

void ColorWheel::resetValues() { setValues({0, 0, 0}, 0); }

void ColorWheel::dragPuckTo(double angleDeg, double distance) {
    disc_->angle = angleDeg;
    disc_->dist = distance;
    disc_->update();
    emit changed();
    emit committed();
}

// ------------------------------------------------------------------ curves

CurveEditor::CurveEditor(QWidget* parent) : QWidget(parent) {
    setMinimumSize(180, 180);
    setMouseTracking(true);
    setToolTip("Nhấp để thêm điểm, kéo để chỉnh, nhấp đúp vào điểm để xoá.");
}

QRectF CurveEditor::area() const {
    const double s = std::min(width(), height()) - 12;
    return {(width() - s) / 2.0, (height() - s) / 2.0, s, s};
}
QPointF CurveEditor::toWidget(QPointF p) const {
    const QRectF a = area();
    return {a.left() + p.x() * a.width(), a.bottom() - p.y() * a.height()};
}
QPointF CurveEditor::toCurve(QPointF w) const {
    const QRectF a = area();
    return {std::clamp((w.x() - a.left()) / a.width(), 0.0, 1.0), std::clamp((a.bottom() - w.y()) / a.height(), 0.0, 1.0)};
}
int CurveEditor::hit(QPointF w) const {
    for (int i = 0; i < pts_.size(); ++i)
        if (QLineF(toWidget(pts_[i]), w).length() <= 8) return i;
    return -1;
}

void CurveEditor::setPoints(const QVector<QPointF>& pts) {
    pts_ = pts;
    if (pts_.size() < 2) pts_ = {{0, 0}, {1, 1}};
    std::sort(pts_.begin(), pts_.end(), [](auto& a, auto& b) { return a.x() < b.x(); });
    drag_ = -1;
    update();
}

void CurveEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF a = area();
    p.fillRect(a, Theme::panel2());
    if (!hist_.isEmpty()) {  // luma histogram behind the curve
        const float mx = *std::max_element(hist_.begin(), hist_.end());
        if (mx > 0) {
            QPainterPath h;
            h.moveTo(a.bottomLeft());
            for (int i = 0; i < hist_.size(); ++i)
                h.lineTo(a.left() + a.width() * i / (hist_.size() - 1), a.bottom() - a.height() * 0.9 * hist_[i] / mx);
            h.lineTo(a.bottomRight());
            p.fillPath(h, QColor(255, 255, 255, 22));
        }
    }
    p.setPen(QPen(Theme::lineSoft(), 1));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(a.left() + a.width() * i / 4, a.top()), QPointF(a.left() + a.width() * i / 4, a.bottom()));
        p.drawLine(QPointF(a.left(), a.top() + a.height() * i / 4), QPointF(a.right(), a.top() + a.height() * i / 4));
    }
    p.setPen(QPen(Theme::line(), 1, Qt::DashLine));
    p.drawLine(a.bottomLeft(), a.topRight());
    // The curve exactly as applied (same spline as the effect).
    vstudio::Json pts = vstudio::Json::array();
    for (const auto& q : pts_) pts.push_back({q.x(), q.y()});
    const auto samples = vstudio::sampleCurve(pts, 129);
    QPainterPath path;
    for (int i = 0; i < int(samples.size()); ++i) {
        const QPointF w = toWidget({i / 128.0, samples[i]});
        i == 0 ? path.moveTo(w) : path.lineTo(w);
    }
    p.setPen(QPen(color_, 2));
    p.drawPath(path);
    for (int i = 0; i < pts_.size(); ++i) {
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(i == drag_ ? color_ : Theme::raised());
        p.drawEllipse(toWidget(pts_[i]), 5, 5);
    }
}

void CurveEditor::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    drag_ = hit(e->position());
    if (drag_ < 0 && area().adjusted(-4, -4, 4, 4).contains(e->position())) {
        const QPointF c = toCurve(e->position());
        int i = 0;
        while (i < pts_.size() && pts_[i].x() < c.x()) ++i;
        if (i == 0 || i == pts_.size()) return;  // ends are fixed in x
        pts_.insert(i, c);
        drag_ = i;
        emit changed();
    }
    update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ < 0) {
        setCursor(hit(e->position()) >= 0 ? Qt::SizeAllCursor : Qt::CrossCursor);
        return;
    }
    QPointF c = toCurve(e->position());
    const bool end = drag_ == 0 || drag_ == pts_.size() - 1;
    if (end) {
        c.setX(pts_[drag_].x());  // end points move up/down only
    } else {
        const double lo = pts_[drag_ - 1].x() + 0.01, hi = pts_[drag_ + 1].x() - 0.01;
        c.setX(std::clamp(c.x(), lo, hi));
    }
    pts_[drag_] = c;
    update();
    emit changed();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || drag_ < 0) return;
    // Dragged out of the box sideways: remove the point.
    const QRectF a = area();
    if (drag_ > 0 && drag_ < pts_.size() - 1 && (e->position().x() < a.left() - 20 || e->position().x() > a.right() + 20))
        pts_.remove(drag_);
    drag_ = -1;
    update();
    emit changed();
    emit committed();
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent* e) {
    const int i = hit(e->position());
    if (i > 0 && i < pts_.size() - 1) {
        pts_.remove(i);
        drag_ = -1;
        update();
        emit changed();
        emit committed();
    }
}
