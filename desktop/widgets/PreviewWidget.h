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
    // Cover editing: the active cover's region (0..1 of the frame) is drawn with handles;
    // drag inside to move, a corner to resize, outside to draw a new region. A null rect
    // turns cover editing off (the zoom box comes back).
    void setCoverRect(const QRectF& normalized, const QString& shape = "rect");
    [[nodiscard]] bool coverEditing() const { return coverOn_; }

signals:
    void previewSizeChanged(int w, int h);  // device pixels
    void regionSelected(const QRectF& normalized);  // 0..1 of the frame, frame aspect
    void coverRectChanged(const QRectF& normalized);  // cover moved/resized/drawn (on release)

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

    enum class CoverDrag { None, Move, Corner, Draw };
    [[nodiscard]] QRectF coverWidgetRect() const;  // the cover in widget coordinates
    [[nodiscard]] QRectF toNormalized(const QRectF& widgetRect) const;

    QImage frame_;
    bool coverOn_ = false;
    QRectF cover_;  // normalized
    QString coverShape_;
    CoverDrag coverDrag_ = CoverDrag::None;
    QRectF coverStart_;  // widget rect when the drag started
    QPointF dragFrom_;
    int corner_ = 0;  // 0 tl, 1 tr, 2 br, 3 bl
    bool regionEnabled_ = false, hover_ = false, dragging_ = false;
    QPointF anchor_;
    QRectF band_;
};
