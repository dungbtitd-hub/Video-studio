// Displays the latest preview frame. Painting only scales an existing image;
// frames are produced on the core's render thread.
#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class PreviewItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double aspect READ aspect WRITE setAspect NOTIFY aspectChanged)
public:
    explicit PreviewItem(QQuickItem* parent = nullptr);
    void paint(QPainter* p) override;
    void setFrame(const QImage& img);
    double aspect() const { return aspect_; }
    void setAspect(double a);

signals:
    void sizeHintChanged();
    void aspectChanged();

protected:
    void geometryChange(const QRectF& n, const QRectF& o) override;

private:
    QImage frame_;
    double aspect_ = 16.0 / 9.0;
};
