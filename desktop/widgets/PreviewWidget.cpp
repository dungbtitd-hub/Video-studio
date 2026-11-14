#include "PreviewWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "Theme.h"

namespace {
constexpr double kMinBand = 12;  // px: smaller drags are clicks
}

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);
    setFocusPolicy(Qt::ClickFocus);
}

void PreviewWidget::setFrame(const QImage& img) {
    frame_ = img;
    update();
}

void PreviewWidget::setRegionSelectEnabled(bool on) {
    if (on == regionEnabled_) return;
    regionEnabled_ = on;
    if (!on) dragging_ = false;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

QRectF PreviewWidget::frameRect() const {
    if (frame_.isNull()) return {};
    QSizeF s = frame_.size() / devicePixelRatioF();
    s.scale(size(), Qt::KeepAspectRatio);
    return {(width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height()};
}

void PreviewWidget::resizeEvent(QResizeEvent*) {
    const qreal dpr = devicePixelRatioF();
    emit previewSizeChanged(int(width() * dpr), int(height() * dpr));
}

QRectF PreviewWidget::bandRect(QPointF to) const {
    const QRectF f = frameRect();
    if (f.isEmpty()) return {};
    const double aspect = f.width() / f.height();
    const double dx = to.x() - anchor_.x(), dy = to.y() - anchor_.y();
    // The box keeps the frame's shape, so what is inside is exactly what will fill the frame.
    double w = std::max(std::abs(dx), std::abs(dy) * aspect);
    const double sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    // Room inside the frame in the drag direction.
    const double roomX = sx > 0 ? f.right() - anchor_.x() : anchor_.x() - f.left();
    const double roomY = sy > 0 ? f.bottom() - anchor_.y() : anchor_.y() - f.top();
    w = std::min({w, roomX, roomY * aspect});
    const double h = w / aspect;
    return QRectF(anchor_, QSizeF(sx * w, sy * h)).normalized();
}

void PreviewWidget::mousePressEvent(QMouseEvent* e) {
    if (!regionEnabled_ || e->button() != Qt::LeftButton || !frameRect().contains(e->position())) return;
    dragging_ = true;
    anchor_ = e->position();
    band_ = {};
    setFocus();
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    band_ = bandRect(e->position());
    update();
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_ || e->button() != Qt::LeftButton) return;
    dragging_ = false;
    const QRectF b = bandRect(e->position()), f = frameRect();
    band_ = {};
    update();
    if (b.width() < kMinBand || f.isEmpty()) return;
    emit regionSelected(QRectF((b.x() - f.x()) / f.width(), (b.y() - f.y()) / f.height(), b.width() / f.width(),
                               b.height() / f.height()));
}

void PreviewWidget::keyPressEvent(QKeyEvent* e) {
    if (dragging_ && e->key() == Qt::Key_Escape) {
        dragging_ = false;
        band_ = {};
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void PreviewWidget::enterEvent(QEnterEvent*) {
    hover_ = true;
    update();
}

void PreviewWidget::leaveEvent(QEvent*) {
    hover_ = false;
    update();
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (frame_.isNull()) return;
    const QRectF r = frameRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, frame_.width() < r.width() * devicePixelRatioF());
    p.drawImage(r, frame_);
    if (dragging_ && band_.width() >= 1) {
        // Dim what will be cut away; the box shows what fills the frame.
        QPainterPath outside;
        outside.addRect(r);
        QPainterPath inside;
        inside.addRect(band_);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 140));
        p.setPen(QPen(Theme::accent(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(band_);
        const double k = r.width() / std::max(1.0, band_.width());
        p.setPen(Theme::fg());
        p.drawText(band_.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop,
                   band_.width() < kMinBand ? QString() : QString("×%1").arg(k, 0, 'f', k < 10 ? 1 : 0));
    }
    else if (regionEnabled_ && hover_) {
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        const QString hint = tr("Kéo chuột để khoanh vùng cần phóng to · Esc để hủy");
        const QRectF box(r.left() + 8, r.bottom() - 26, p.fontMetrics().horizontalAdvance(hint) + 16, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(Theme::fg2());
        p.drawText(box, Qt::AlignCenter, hint);
    }
}
