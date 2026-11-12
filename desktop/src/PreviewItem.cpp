#include "PreviewItem.h"

#include <QPainter>

PreviewItem::PreviewItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(true);
    setFillColor(Qt::black);
}

void PreviewItem::setFrame(const QImage& img) {
    frame_ = img;
    update();
}

void PreviewItem::setAspect(double a) {
    if (qFuzzyCompare(a, aspect_)) return;
    aspect_ = a;
    emit aspectChanged();
}

void PreviewItem::geometryChange(const QRectF& n, const QRectF& o) {
    QQuickPaintedItem::geometryChange(n, o);
    if (n.size() != o.size()) emit sizeHintChanged();
}

void PreviewItem::paint(QPainter* p) {
    if (frame_.isNull()) return;
    const QSizeF box = size();
    QSizeF s = frame_.size();
    s.scale(box, Qt::KeepAspectRatio);
    const QRectF target((box.width() - s.width()) / 2, (box.height() - s.height()) / 2, s.width(), s.height());
    p->setRenderHint(QPainter::SmoothPixmapTransform, frame_.width() < target.width());
    p->drawImage(target, frame_);
}
