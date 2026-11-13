// Shows the newest preview frame; painting only scales an existing image.
// With a video clip under the playhead, dragging the mouse over the picture
// draws a frame-shaped box: on release that area is zoomed to fill the frame
// (a keyframe when the clip's picture is animated).
#pragma once

#include <QImage>
#include <QWidget>

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setFrame(const QImage& img);
    void setRegionSelectEnabled(bool on);
    [[nodiscard]] bool regionSelectEnabled() const { return regionEnabled_; }
    // Where the frame is drawn (widget coordinates).
    [[nodiscard]] QRectF frameRect() const;

signals:
    void previewSizeChanged(int w, int h);  // device pixels
    void regionSelected(const QRectF& normalized);  // 0..1 of the frame, frame aspect

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    [[nodiscard]] QRectF bandRect(QPointF to) const;  // frame-shaped box from the press point, inside the frame

    QImage frame_;
    bool regionEnabled_ = false, hover_ = false, dragging_ = false;
    QPointF anchor_;
    QRectF band_;
};
