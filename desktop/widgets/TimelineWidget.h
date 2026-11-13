// Timeline for the Qt Widgets prototype: one custom-painted widget.
// Only clips inside the visible range are drawn (binary search per track),
// so paint cost depends on what is on screen, not on project size.
// Dragging/trimming is drawn locally and committed as one edit on release.
#pragma once

#include <QPainter>
#include <QScrollBar>
#include <QWidget>
#include <vector>

class EditorController;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(EditorController* editor, QWidget* parent = nullptr);

    void setScrollSeconds(double s);
    [[nodiscard]] double scrollSeconds() const { return scroll_; }
    [[nodiscard]] double pxPerSec() const { return pxPerSec_; }

signals:
    void painted();  // for benchmarks

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Clip {
        QString id;
        double start, end;
        QString name;
        bool fx;
        QString assetId;
        double srcIn;  // source seconds at `start`
        double speed;  // source seconds per timeline second
    };
    struct Track {
        QString id, name;
        bool audio;
        std::vector<Clip> clips;  // sorted by start
    };
    enum class Drag { None, Scrub, Move, TrimStart, TrimEnd };

    void rebuild();  // copy the project into flat arrays (only after an edit)
    [[nodiscard]] double xToTime(double x) const { return scroll_ + (x - kHeaderW) / pxPerSec_; }
    [[nodiscard]] double timeToX(double t) const { return kHeaderW + (t - scroll_) * pxPerSec_; }
    [[nodiscard]] int trackAt(int y) const;
    const Clip* clipAt(int track, double t) const;
    // Media drawn inside a clip, only for its visible part.
    void paintThumbnails(QPainter& p, const Clip& c, const QRectF& r) const;
    void paintWaveform(QPainter& p, const Clip& c, const QRectF& r) const;
    void updateScrollBar();

    static constexpr int kHeaderW = 120, kRulerH = 24, kTrackH = 54;
    EditorController* editor_;
    QScrollBar* hbar_;
    std::vector<Track> tracks_;
    double duration_ = 0, scroll_ = 0, pxPerSec_ = 60;
    Drag drag_ = Drag::None;
    QString dragClip_, dragTrack_;
    double dragOrigin_ = 0, dragDelta_ = 0;
};
