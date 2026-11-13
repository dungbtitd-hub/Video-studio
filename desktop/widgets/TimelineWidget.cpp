#include "TimelineWidget.h"

#include <QHash>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "EditorController.h"
#include "Theme.h"

TimelineWidget::TimelineWidget(EditorController* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumHeight(kRulerH + kTrackH * 2 + 20);
    hbar_ = new QScrollBar(Qt::Horizontal, this);
    connect(hbar_, &QScrollBar::valueChanged, this, [this](int v) {
        scroll_ = v / 100.0;
        update();
    });
    connect(editor_, &EditorController::projectChanged, this, &TimelineWidget::rebuild);
    connect(editor_, &EditorController::playheadChanged, this, qOverload<>(&QWidget::update));
    connect(editor_, &EditorController::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(editor_, &EditorController::mediaLibraryChanged, this, qOverload<>(&QWidget::update));
    rebuild();
}

void TimelineWidget::rebuild() {
    // Read the immutable core snapshot directly: no QVariant models, and clip
    // names are shared (implicitly shared QString), so this is a flat copy.
    const auto proj = editor_->project();
    tracks_.clear();
    if (!proj) return;
    QHash<QString, QString> names;
    for (const auto& a : proj->assets) {
        const QString uri = QString::fromStdString(a.uri);
        names.insert(QString::fromStdString(a.id), uri.mid(uri.lastIndexOf('/') + 1));
    }
    // Top video track first, audio tracks below (same order as the QML view).
    std::vector<const vstudio::Track*> order;
    for (auto it = proj->tracks.rbegin(); it != proj->tracks.rend(); ++it)
        if (it->type == vstudio::TrackType::Video) order.push_back(&*it);
    for (const auto& t : proj->tracks)
        if (t.type == vstudio::TrackType::Audio) order.push_back(&t);
    tracks_.reserve(order.size());
    int videoIndex = 0, audioIndex = 0;
    for (const auto& tr : proj->tracks) (tr.type == vstudio::TrackType::Video ? videoIndex : audioIndex)++;
    int vSeen = 0, aSeen = 0;
    for (const vstudio::Track* src : order) {
        Track t{QString::fromStdString(src->id), QString::fromStdString(src->name),
                src->type == vstudio::TrackType::Audio, {}};
        t.enabled = src->enabled;
        t.locked = src->locked;
        t.muted = src->muted;
        // Display order is top video first: the index in the project is the reverse for video.
        t.indexInType = t.audio ? aSeen++ : videoIndex - 1 - vSeen++;
        t.clips.reserve(src->clips.size());
        for (const auto& c : src->clips) {
            const double st = c.timelineIn.seconds();
            t.clips.push_back({QString::fromStdString(c.id), st, st + c.duration().seconds(),
                               names.value(QString::fromStdString(c.assetId), QStringLiteral("?")),
                               c.effects.is_array() && !c.effects.empty(), QString::fromStdString(c.assetId),
                               c.sourceIn.seconds(), double(c.speed.num) / c.speed.den});
        }
        tracks_.push_back(std::move(t));
    }
    duration_ = editor_->duration();
    setMinimumHeight(kRulerH + kTrackH * int(std::max<size_t>(2, tracks_.size())) + hbar_->sizeHint().height());
    updateScrollBar();
    update();
}

void TimelineWidget::updateScrollBar() {
    const double visible = std::max(1.0, (width() - kHeaderW) / pxPerSec_);
    hbar_->setRange(0, int(std::max(0.0, duration_ + 30 - visible) * 100));
    hbar_->setPageStep(int(visible * 100));
    hbar_->setGeometry(kHeaderW, height() - hbar_->sizeHint().height(), width() - kHeaderW, hbar_->sizeHint().height());
}

void TimelineWidget::resizeEvent(QResizeEvent*) { updateScrollBar(); }

void TimelineWidget::setScrollSeconds(double s) { hbar_->setValue(int(s * 100)); }

int TimelineWidget::trackAt(int y) const {
    if (y < kRulerH) return -1;
    const int i = (y - kRulerH) / kTrackH;
    return i < int(tracks_.size()) ? i : -1;
}

const TimelineWidget::Clip* TimelineWidget::clipAt(int track, double t) const {
    if (track < 0) return nullptr;
    const auto& cl = tracks_[size_t(track)].clips;
    auto it = std::upper_bound(cl.begin(), cl.end(), t, [](double v, const Clip& c) { return v < c.start; });
    if (it == cl.begin()) return nullptr;
    --it;
    return t < it->end ? &*it : nullptr;
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const int w = width(), h = height();
    p.fillRect(rect(), Theme::panel());
    const double t0 = scroll_, t1 = xToTime(w);

    // Ruler
    p.setPen(Theme::fg3());
    QFont small = font();
    small.setPixelSize(10);
    p.setFont(small);
    const double step = pxPerSec_ >= 30 ? 1 : (pxPerSec_ >= 8 ? 5 : 15);
    for (double s = std::floor(t0 / step) * step; s <= t1; s += step) {
        const int x = int(timeToX(s));
        if (x < kHeaderW) continue;
        const bool major = std::fmod(s, step * 5) < 1e-9;
        p.drawLine(x, kRulerH - (major ? 10 : 5), x, kRulerH);
        if (major) p.drawText(x + 3, 12, QString("%1:%2").arg(int(s) / 60).arg(int(s) % 60, 2, 10, QChar('0')));
    }

    // Tracks: draw only clips overlapping [t0, t1]
    p.setClipRect(kHeaderW, kRulerH, w - kHeaderW, h - kRulerH);
    QFont clipFont = font();
    clipFont.setPixelSize(11);
    p.setFont(clipFont);
    const QString sel = editor_->selectedClip();
    // Backgrounds first, so a clip dragged onto another track is never covered.
    for (size_t i = 0; i < tracks_.size(); ++i) {
        const int top = kRulerH + int(i) * kTrackH;
        p.fillRect(kHeaderW, top, w - kHeaderW, kTrackH - 2, Theme::panel2());
        if (int(i) == dragTargetTrack_ && drag_ == Drag::Move && tracks_[i].id != dragTrack_)
            p.fillRect(kHeaderW, top, w - kHeaderW, kTrackH - 2, QColor(255, 255, 255, 18));
    }
    for (size_t i = 0; i < tracks_.size(); ++i) {
        const Track& t = tracks_[i];
        const int trackTop = kRulerH + int(i) * kTrackH;
        auto first = std::lower_bound(t.clips.begin(), t.clips.end(), t0, [](const Clip& c, double v) { return c.end < v; });
        const bool dim = !t.enabled || (t.audio && t.muted);
        if (dim) p.setOpacity(0.4);
        for (auto it = first; it != t.clips.end() && it->start <= t1 + 1; ++it) {
            const bool dragging = it->id == dragClip_;
            const int top = dragging && drag_ == Drag::Move && dragTargetTrack_ >= 0
                                ? kRulerH + dragTargetTrack_ * kTrackH
                                : trackTop;
            double s = it->start, e = it->end;
            if (dragging && drag_ == Drag::Move) s += dragDelta_, e += dragDelta_;
            if (dragging && drag_ == Drag::TrimStart) s += dragDelta_;
            if (dragging && drag_ == Drag::TrimEnd) e += dragDelta_;
            const QRectF r(timeToX(s), top + 4, std::max(3.0, (e - s) * pxPerSec_), kTrackH - 10);
            const bool selected = it->id == sel;
            p.setPen(QPen(selected ? Theme::accent() : (t.audio ? Theme::clipAudio() : Theme::clipVideo()), selected ? 2 : 1));
            p.setBrush(t.audio ? Theme::clipAudio2() : Theme::clipVideo2());
            p.drawRoundedRect(r, 4, 4);
            if (r.width() > 8) {
                p.save();
                p.setClipRect(r.adjusted(1, 1, -1, -1).intersected(QRectF(kHeaderW, kRulerH, w - kHeaderW, h - kRulerH)));
                if (t.audio) paintWaveform(p, *it, r);
                else paintThumbnails(p, *it, r);
                p.restore();
                p.setPen(QPen(selected ? Theme::accent() : (t.audio ? Theme::clipAudio() : Theme::clipVideo()), selected ? 2 : 1));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r, 4, 4);
            }
            if (r.width() > 30) {
                p.setPen(Theme::fg());
                p.drawText(r.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                           (it->fx ? "fx · " : "") + p.fontMetrics().elidedText(it->name, Qt::ElideRight, int(r.width()) - 12));
            }
        }
        p.setOpacity(1.0);
        if (t.locked) {  // locked: hatched, clips still visible
            p.setPen(Qt::NoPen);
            p.setBrush(QBrush(QColor(255, 255, 255, 22), Qt::BDiagPattern));
            p.drawRect(kHeaderW, trackTop, w - kHeaderW, kTrackH - 2);
            p.setBrush(Qt::NoBrush);
        }
    }
    // Snap indicator
    if (snapAt_ >= 0 && drag_ != Drag::None && drag_ != Drag::Scrub) {
        const double sx = timeToX(snapAt_);
        p.setPen(QPen(QColor(255, 214, 90), 1, Qt::DashLine));
        p.drawLine(QPointF(sx, kRulerH), QPointF(sx, kRulerH + kTrackH * double(tracks_.size())));
    }
    p.setClipping(false);

    // Headers
    p.setBrush(Qt::NoBrush);
    for (size_t i = 0; i < tracks_.size(); ++i) {
        const int top = kRulerH + int(i) * kTrackH;
        p.fillRect(0, top, kHeaderW, kTrackH, Theme::panel());
        p.setPen(Theme::lineSoft());
        p.drawRect(0, top, kHeaderW - 1, kTrackH - 1);
        const Track& t = tracks_[i];
        p.setPen(Theme::fg2());
        p.setFont(clipFont);
        p.drawText(QRect(10, top + 6, kHeaderW - 12, 18), Qt::AlignVCenter,
                   p.fontMetrics().elidedText(t.name, Qt::ElideRight, kHeaderW - 20));
        // Toggles: lock + (hide for video | mute for audio). Active = accent fill.
        p.setFont(small);
        const auto pill = [&](Toggle which, const QString& label, bool active) {
            const QRect r = toggleRect(int(i), which);
            p.setPen(active ? Theme::accent() : Theme::lineSoft());
            p.setBrush(active ? QColor(Theme::accent().red(), Theme::accent().green(), Theme::accent().blue(), 60)
                              : QColor(Qt::transparent));
            p.drawRoundedRect(r, 7, 7);
            p.setPen(active ? Theme::fg() : Theme::fg3());
            p.drawText(r, Qt::AlignCenter, label);
        };
        pill(Toggle::Lock, tr("Khóa"), t.locked);
        if (t.audio) pill(Toggle::Visible, tr("Câm"), t.muted);
        else pill(Toggle::Visible, tr("Ẩn"), !t.enabled);
        p.setBrush(Qt::NoBrush);
    }
    if (!snapping_) {
        p.setPen(Theme::fg3());
        p.setFont(small);
        p.drawText(QRect(4, 0, kHeaderW - 8, kRulerH), Qt::AlignVCenter, tr("Hít: tắt (N)"));
    }

    // Playhead
    const double px = timeToX(editor_->playhead());
    if (px >= kHeaderW) {
        p.setPen(QPen(Theme::accent(), 2));
        p.drawLine(QPointF(px, 0), QPointF(px, h));
        p.setBrush(Theme::accent());
        p.setPen(Qt::NoPen);
        QPainterPath diamond;
        diamond.moveTo(px, 0);
        diamond.lineTo(px + 5, 5);
        diamond.lineTo(px, 10);
        diamond.lineTo(px - 5, 5);
        p.drawPath(diamond);
    }
    p.end();
    emit painted();
}

void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (pos.x() < kHeaderW) {
        const int ti = trackAt(int(pos.y()));
        if (ti < 0 || e->button() != Qt::LeftButton) return;
        const Track& tr = tracks_[size_t(ti)];
        if (toggleRect(ti, Toggle::Lock).contains(pos.toPoint()))
            editor_->setTrackFlags(tr.id, tr.enabled, !tr.locked, tr.muted);
        else if (toggleRect(ti, Toggle::Visible).contains(pos.toPoint()))
            editor_->setTrackFlags(tr.id, tr.audio ? tr.enabled : !tr.enabled, tr.locked, tr.audio ? !tr.muted : tr.muted);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const double t = xToTime(pos.x());
    if (pos.y() < kRulerH) {
        drag_ = Drag::Scrub;
        editor_->seek(t);
        return;
    }
    const int ti = trackAt(int(pos.y()));
    const Clip* c = clipAt(ti, t);
    if (!c) {
        editor_->setSelectedClip({});
        drag_ = Drag::Scrub;
        editor_->seek(t);
        return;
    }
    editor_->setSelectedClip(c->id);
    if (tracks_[size_t(ti)].locked) return;  // selectable, not editable
    dragClip_ = c->id;
    dragTrack_ = tracks_[size_t(ti)].id;
    dragTargetTrack_ = ti;
    dragOrigin_ = t;
    dragDelta_ = 0;
    snapAt_ = -1;
    const double edge = 6 / pxPerSec_;
    drag_ = t - c->start < edge ? Drag::TrimStart : (c->end - t < edge ? Drag::TrimEnd : Drag::Move);
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    const double t = xToTime(pos.x());
    if (drag_ == Drag::Scrub) {
        editor_->seek(std::max(0.0, t));  // latest-wins in the core: never queues
        return;
    }
    if (drag_ != Drag::None) {
        dragDelta_ = t - dragOrigin_;
        snapAt_ = -1;
        const Clip* c = nullptr;
        for (const Track& tr : tracks_)
            for (const Clip& x : tr.clips)
                if (x.id == dragClip_) c = &x;
        const bool snap = snapping_ && !(e->modifiers() & Qt::ShiftModifier) && c;
        const double tol = 8 / pxPerSec_;
        if (drag_ == Drag::Move) {
            // Other tracks of the same type that are not locked accept the clip.
            const int over = trackAt(int(pos.y()));
            const Track& from = *std::find_if(tracks_.begin(), tracks_.end(), [&](const Track& x) { return x.id == dragTrack_; });
            if (over >= 0 && tracks_[size_t(over)].audio == from.audio && !tracks_[size_t(over)].locked)
                dragTargetTrack_ = over;
            if (snap) {
                bool hit = false;
                const double start = std::max(0.0, c->start + dragDelta_);
                double target = 0;
                const double s = editor_->snapClipMove(c->id, start, tol, &hit, &target);
                if (hit) {
                    dragDelta_ = s - c->start;
                    snapAt_ = target;
                }
            }
        } else if (snap) {
            bool hit = false;
            const double edge = (drag_ == Drag::TrimStart ? c->start : c->end) + dragDelta_;
            const double s = editor_->snapTime(edge, tol, c->id, &hit);
            if (hit) {
                dragDelta_ = s - (drag_ == Drag::TrimStart ? c->start : c->end);
                snapAt_ = s;
            }
        }
        update();  // visual only
        return;
    }
    const Clip* c = clipAt(trackAt(int(pos.y())), t);
    const double edge = 6 / pxPerSec_;
    setCursor(c && (t - c->start < edge || c->end - t < edge) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*) {
    const Drag d = drag_;
    drag_ = Drag::None;
    snapAt_ = -1;
    const QString target = dragTargetTrack_ >= 0 ? tracks_[size_t(dragTargetTrack_)].id : dragTrack_;
    dragTargetTrack_ = -1;
    if (d == Drag::None || d == Drag::Scrub || (std::abs(dragDelta_) < 0.01 && target == dragTrack_)) {
        dragClip_.clear();
        update();
        return;
    }
    // Commit once: one command, one undo step. Copy first: the edit rebuilds tracks_.
    std::optional<Clip> found;
    for (const Track& t : tracks_)
        for (const Clip& c : t.clips)
            if (c.id == dragClip_) found = c;
    const QString id = std::exchange(dragClip_, {});
    if (found) {
        if (d == Drag::Move) editor_->moveClip(id, target, std::max(0.0, found->start + dragDelta_));
        else if (d == Drag::TrimStart) editor_->trimClip(id, true, found->start + dragDelta_);
        else editor_->trimClip(id, false, found->end + dragDelta_);
    }
    update();
}

QRect TimelineWidget::toggleRect(int track, Toggle which) const {
    const int top = kRulerH + track * kTrackH;
    return {which == Toggle::Lock ? 10 : 58, top + kTrackH - 24, 44, 16};
}

void TimelineWidget::headerMenu(int track, const QPoint& globalPos) {
    QMenu m(this);
    m.addAction(tr("Thêm track video"), this, [this] { editor_->addTrack(false); });
    m.addAction(tr("Thêm track âm thanh"), this, [this] { editor_->addTrack(true); });
    if (track >= 0) {
        const Track t = tracks_[size_t(track)];  // copy: the menu outlives a rebuild
        int sameType = 0;
        for (const Track& x : tracks_) sameType += x.audio == t.audio;
        m.addSeparator();
        m.addAction(tr("Đổi tên…"), this, [this, t] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Đổi tên track"), tr("Tên"), QLineEdit::Normal, t.name, &ok);
            if (ok && !name.trimmed().isEmpty()) editor_->renameTrack(t.id, name.trimmed());
        });
        // Video is shown top layer first, so "up" means a higher index for video.
        const int up = t.audio ? t.indexInType - 1 : t.indexInType + 1;
        const int down = t.audio ? t.indexInType + 1 : t.indexInType - 1;
        m.addAction(tr("Lên trên"), this, [this, t, up] { editor_->moveTrack(t.id, up); })
            ->setEnabled(up >= 0 && up < sameType);
        m.addAction(tr("Xuống dưới"), this, [this, t, down] { editor_->moveTrack(t.id, down); })
            ->setEnabled(down >= 0 && down < sameType);
        m.addSeparator();
        m.addAction(t.locked ? tr("Mở khóa") : tr("Khóa"), this,
                    [this, t] { editor_->setTrackFlags(t.id, t.enabled, !t.locked, t.muted); });
        m.addAction(tr("Xóa track"), this, [this, t] { editor_->removeTrack(t.id); });
    }
    m.addSeparator();
    QAction* snap = m.addAction(tr("Hít cạnh (N)"), this, [this] { snapping_ = !snapping_; update(); });
    snap->setCheckable(true);
    snap->setChecked(snapping_);
    m.exec(globalPos);
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* e) {
    if (drag_ != Drag::None) return;
    headerMenu(trackAt(e->pos().y()), e->globalPos());
}

void TimelineWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_N && e->modifiers() == Qt::NoModifier) {
        snapping_ = !snapping_;
        update();
        return;
    }
    QWidget::keyPressEvent(e);
}

void TimelineWidget::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        const double anchor = xToTime(e->position().x());
        pxPerSec_ = std::clamp(pxPerSec_ * (e->angleDelta().y() > 0 ? 1.15 : 0.87), 5.0, 400.0);
        updateScrollBar();
        setScrollSeconds(std::max(0.0, anchor - (e->position().x() - kHeaderW) / pxPerSec_));
        update();
    } else {
        hbar_->setValue(hbar_->value() - e->angleDelta().y() * 2);
    }
}

void TimelineWidget::paintThumbnails(QPainter& p, const Clip& c, const QRectF& r) const {
    const auto strip = editor_->thumbnails(c.assetId);
    if (!strip || strip->count() == 0) return;
    const double th = r.height() - 16, tw = th * strip->width / strip->height;  // keep the clip name readable
    if (tw < 4) return;
    const double total = strip->times.size() > 1 ? (strip->times.back() - strip->times.front()).seconds() *
                                                       strip->count() / (strip->count() - 1)
                                                 : 0.0;
    // Tiles only across the visible part of the clip.
    const double x0 = std::max(r.left(), double(kHeaderW)), x1 = std::min(r.right(), double(width()));
    for (double x = r.left() + std::floor((x0 - r.left()) / tw) * tw; x < x1; x += tw) {
        const double src = c.srcIn + (x + tw / 2 - r.left()) / pxPerSec_ * c.speed;
        const int i = total > 0 ? std::clamp(int(src / total * strip->count()), 0, strip->count() - 1) : 0;
        const QImage img(strip->frame(i), strip->width, strip->height, strip->width * 4, QImage::Format_RGBA8888);
        p.drawImage(QRectF(x, r.top() + 15, tw, th), img);
    }
}

void TimelineWidget::paintWaveform(QPainter& p, const Clip& c, const QRectF& r) const {
    const auto wf = editor_->waveform(c.assetId);
    if (!wf || wf->buckets() == 0) return;
    // Quiet media is scaled up (at most 8x) so its shape stays visible.
    const double mid = r.center().y() + 6,
                 half = (r.height() - 16) / 2 / std::max(127.0 / 8, double(std::max(1, wf->loudest))) * 127.0;
    p.setPen(QColor(120, 220, 170, 200));
    const int x0 = int(std::max(r.left(), double(kHeaderW))), x1 = int(std::min(r.right(), double(width())));
    const auto n = static_cast<std::int64_t>(wf->buckets());
    for (int x = x0; x < x1; ++x) {
        // Source range under this pixel column -> max envelope over its buckets.
        const double a = c.srcIn + (x - r.left()) / pxPerSec_ * c.speed, b = a + c.speed / pxPerSec_;
        std::int64_t i0 = std::int64_t(a * wf->bucketsPerSecond), i1 = std::int64_t(b * wf->bucketsPerSecond) + 1;
        i0 = std::clamp<std::int64_t>(i0, 0, n - 1);
        i1 = std::clamp<std::int64_t>(i1, i0 + 1, n);
        int lo = 0, hi = 0;
        for (std::int64_t k = i0; k < i1; ++k) {
            lo = std::min<int>(lo, wf->peaks[std::size_t(k) * 2]);
            hi = std::max<int>(hi, wf->peaks[std::size_t(k) * 2 + 1]);
        }
        p.drawLine(QPointF(x + 0.5, mid - hi / 127.0 * half), QPointF(x + 0.5, mid - lo / 127.0 * half));
    }
}
