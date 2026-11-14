// Timeline for the Qt Widgets prototype: one custom-painted widget.
// Only clips inside the visible range are drawn (binary search per track),
// so paint cost depends on what is on screen, not on project size.
// Dragging/trimming is drawn locally and committed as one edit on release.
#pragma once

#include <QPainter>
#include <QScrollBar>
#include <QWidget>
#include <optional>
#include <vector>

class EditorController;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(EditorController* editor, QWidget* parent = nullptr);

    void setScrollSeconds(double s);
    [[nodiscard]] double scrollSeconds() const { return scroll_; }
    [[nodiscard]] double pxPerSec() const { return pxPerSec_; }
    // Zoom keeping the time under `anchorX` (widget x) still; -1 = the playhead (or the left edge).
    void zoomBy(double factor, double anchorX = -1);
    void zoomToFit();  // whole project in view
    [[nodiscard]] bool snapping() const { return snapping_; }
    void setSnapping(bool on);
    static constexpr double kMinPxPerSec = 5, kMaxPxPerSec = 400;

signals:
    void painted();  // for benchmarks
    void snappingChanged(bool on);
    void zoomChanged(double pxPerSec);
    void transitionsRequested();  // a transition badge was clicked: show the transitions panel

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void leaveEvent(QEvent*) override;

public:
    // Mime type of a transition dragged from the panel (payload: the transition id, UTF-8).
    static constexpr const char* kTransitionMime = "application/x-vstudio-transition";

private:
    struct Clip {
        QString id;
        double start, end;
        QString name;
        bool fx;
        QString assetId;
        double srcIn;  // source seconds at `start`
        double speed;  // source seconds per timeline second
        std::vector<double> pictureKeys, audioKeys;  // keyframes, timeline seconds (inside the clip)
    };
    // A place a transition can go (video tracks): a cut between touching clips, or a free clip edge.
    struct Cut {
        double at = 0;
        QString clipId;  // the clip storing it
        int edge = 0;    // 0 = start of clipId, 1 = end
        bool joined = false;
        QString type;    // "" = none
        double duration = 0;
    };
    struct Track {
        QString id, name;
        bool audio;
        std::vector<Clip> clips;  // sorted by start
        std::vector<Cut> cuts;    // sorted by time
        bool enabled = true, locked = false, muted = false;
        int indexInType = 0;  // 0 = bottom video layer / first audio track
    };
    // Header toggles, hit-tested by rect.
    enum class Toggle { None, Lock, Visible };
    [[nodiscard]] QRect toggleRect(int track, Toggle which) const;
    void headerMenu(int track, const QPoint& globalPos);
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
    // Keyframe diamond under `pos` on clip `c` (timeline seconds), if any.
    [[nodiscard]] std::optional<double> keyAt(const Clip& c, const QRectF& r, QPointF pos) const;
    void paintKeyframes(QPainter& p, const Clip& c, const QRectF& r, bool selected) const;
    // Transition badge of a cut on track `track` (widget coordinates).
    [[nodiscard]] QRectF cutBadge(int track, const Cut& c) const;
    // The cut whose badge (or, with `near`, whose position within that many px) is under `pos`.
    [[nodiscard]] std::optional<std::pair<int, std::size_t>> cutAt(QPointF pos, double near = 0) const;
    void paintCuts(QPainter& p, int track) const;

    static constexpr int kHeaderW = 120, kRulerH = 24, kTrackH = 54;
    EditorController* editor_;
    QScrollBar* hbar_;
    std::vector<Track> tracks_;
    double duration_ = 0, scroll_ = 0, pxPerSec_ = 60;
    Drag drag_ = Drag::None;
    QString dragClip_, dragTrack_;
    double dragOrigin_ = 0, dragDelta_ = 0;
    int dragTargetTrack_ = -1;      // track under the pointer while moving (same type, unlocked)
    bool snapping_ = true;          // N toggles; hold Shift to drag freely
    double snapAt_ = -1;            // snapped edge to highlight while dragging (seconds), -1 = none
    std::optional<std::pair<int, std::size_t>> hoverCut_, dropCut_;  // (track, cut) under the pointer / a drop
};
