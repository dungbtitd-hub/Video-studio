#include "PreviewWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QLineF>
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

void PreviewWidget::setCoverRect(const QRectF& normalized, const QString& shape) {
    const bool on = !normalized.isNull();
    if (coverDrag_ != CoverDrag::None && on) return;  // the user is dragging: keep the local state
    coverOn_ = on;
    cover_ = normalized;
    coverShape_ = shape;
    if (on) setCursor(Qt::CrossCursor);
    else setCursor(regionEnabled_ ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

QRectF PreviewWidget::coverWidgetRect() const {
    const QRectF f = frameRect();
    return {f.x() + cover_.x() * f.width(), f.y() + cover_.y() * f.height(), cover_.width() * f.width(),
            cover_.height() * f.height()};
}

QRectF PreviewWidget::toNormalized(const QRectF& r) const {
    const QRectF f = frameRect();
    if (f.isEmpty()) return {};
    return {(r.x() - f.x()) / f.width(), (r.y() - f.y()) / f.height(), r.width() / f.width(), r.height() / f.height()};
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
    if (coverOn_ && e->button() == Qt::LeftButton && !frameRect().isEmpty()) {
        const QRectF r = coverWidgetRect();
        const QPointF p = e->position();
        const QPointF corners[4] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
        coverDrag_ = CoverDrag::Draw;
        for (int i = 0; i < 4; ++i)
            if (QLineF(p, corners[i]).length() <= 9) {
                coverDrag_ = CoverDrag::Corner;
                corner_ = i;
            }
        if (coverDrag_ == CoverDrag::Draw && r.contains(p)) coverDrag_ = CoverDrag::Move;
        coverStart_ = r;
        dragFrom_ = p;
        setFocus();
        return;
    }
    if (!regionEnabled_ || e->button() != Qt::LeftButton || !frameRect().contains(e->position())) return;
    dragging_ = true;
    anchor_ = e->position();
    band_ = {};
    setFocus();
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* e) {
    if (coverDrag_ != CoverDrag::None) {
        const QPointF p = e->position(), d = p - dragFrom_;
        QRectF r = coverStart_;
        if (coverDrag_ == CoverDrag::Move) r.translate(d);
        else if (coverDrag_ == CoverDrag::Draw) r = QRectF(dragFrom_, p).normalized();
        else {
            QPointF a = (corner_ == 0 || corner_ == 3) ? r.topRight() : r.topLeft();  // opposite x edge
            a.setY((corner_ == 0 || corner_ == 1) ? r.bottom() : r.top());           // opposite y edge
            r = QRectF(a, p).normalized();
        }
        if (r.width() >= 4 && r.height() >= 4) cover_ = toNormalized(r);
        update();
        return;
    }
    if (!dragging_) return;
    band_ = bandRect(e->position());
    update();
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (coverDrag_ != CoverDrag::None && e->button() == Qt::LeftButton) {
        const bool moved = QLineF(e->position(), dragFrom_).length() > 2;
        coverDrag_ = CoverDrag::None;
        if (moved) emit coverRectChanged(cover_);
        update();
        return;
    }
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
    if (coverDrag_ != CoverDrag::None && e->key() == Qt::Key_Escape) {
        coverDrag_ = CoverDrag::None;
        cover_ = toNormalized(coverStart_);
        update();
        return;
    }
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
    if (coverOn_) {
        const QRectF c = coverWidgetRect();
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(242, 169, 59, 40));
        p.setPen(QPen(Theme::accent(), 1.5, Qt::DashLine));
        if (coverShape_ == "ellipse") p.drawEllipse(c);
        else if (coverShape_ == "circle") {
            const double d = std::min(c.width(), c.height());
            p.drawEllipse(QRectF(c.center().x() - d / 2, c.center().y() - d / 2, d, d));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(242, 169, 59, 120), 1, Qt::DotLine));
            p.drawRect(c);
        } else p.drawRect(c);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::accent());
        for (const QPointF& k : {c.topLeft(), c.topRight(), c.bottomRight(), c.bottomLeft()}) p.drawRect(QRectF(k - QPointF(4, 4), QSizeF(8, 8)));
        if (hover_ && coverDrag_ == CoverDrag::None) {
            QFont fnt = font();
            fnt.setPixelSize(11);
            p.setFont(fnt);
            const QString hint = tr("Kéo vùng che để di chuyển, kéo góc để đổi cỡ, kéo ra ngoài để vẽ vùng mới");
            const QRectF box(r.left() + 8, r.bottom() - 26, p.fontMetrics().horizontalAdvance(hint) + 16, 20);
            p.setBrush(QColor(0, 0, 0, 150));
            p.drawRoundedRect(box, 4, 4);
            p.setPen(Theme::fg2());
            p.drawText(box, Qt::AlignCenter, hint);
        }
        return;
    }
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
