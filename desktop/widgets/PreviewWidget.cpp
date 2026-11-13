#include "PreviewWidget.h"

#include <QPainter>

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);
}

void PreviewWidget::setFrame(const QImage& img) {
    frame_ = img;
    update();
}

void PreviewWidget::resizeEvent(QResizeEvent*) {
    const qreal dpr = devicePixelRatioF();
    emit previewSizeChanged(int(width() * dpr), int(height() * dpr));
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (frame_.isNull()) return;
    QSizeF s = frame_.size() / devicePixelRatioF();
    s.scale(size(), Qt::KeepAspectRatio);
    const QRectF r((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform, frame_.width() < r.width() * devicePixelRatioF());
    p.drawImage(r, frame_);
}
