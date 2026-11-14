// EditorController: rotate / flip, overlay length and "Đoạn áp dụng" (a part of a clip chosen
// with the playhead: Điểm đầu, Điểm cuối, then zoom / sound / speed / effects on that part).
#include <algorithm>
#include <cmath>

#include "EditorController.h"
#include "vstudio/keyframes.hpp"
#include "vstudio/orientation.hpp"

using namespace vstudio;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& q) { return q.toStdString(); }
QString tc(double t) {
    const int m = int(t) / 60;
    return QString("%1:%2").arg(m).arg(t - m * 60, 4, 'f', 1, QChar('0'));
}
}  // namespace

#define VS_EDIT(body)                       \
    [&]() -> bool {                         \
        try {                               \
            body;                           \
            published();                    \
            return true;                    \
        } catch (const std::exception& e) { \
            setStatus(q(e.what()));         \
            return false;                   \
        }                                   \
    }()

// ---------------------------------------------------------------- rotate / flip

QString EditorController::mediaClipAtPlayhead() const {
    if (!snapshot_) return {};
    const Time t = Time::fromSeconds(playhead_);
    // Top of the stack first (the last video track is drawn on top).
    for (auto it = snapshot_->tracks.rbegin(); it != snapshot_->tracks.rend(); ++it) {
        if (it->type != TrackType::Video || !it->enabled) continue;
        for (const Clip& c : it->clips)
            if (!c.generated() && t >= c.timelineIn && t < c.timelineOut()) return q(c.id);
    }
    return {};
}

namespace {
const Clip* mediaClip(const Project& p, const QString& id) {
    const Clip* c = p.findClip(s(id));
    if (!c || c->generated()) return nullptr;
    const Track* t = p.trackOfClip(c->id);
    return t && t->type == TrackType::Video ? c : nullptr;
}
}  // namespace

QVariantMap EditorController::clipOrientation() const {
    QVariantMap m{{"valid", false}, {"rotate", 0}, {"flipH", false}, {"flipV", false}};
    if (!snapshot_) return m;
    const Clip* c = mediaClip(*snapshot_, selected_.isEmpty() ? mediaClipAtPlayhead() : selected_);
    if (!c) return m;
    const Orientation o = vstudio::clipOrientation(*c);
    return {{"valid", true}, {"rotate", o.rotate}, {"flipH", o.flipH}, {"flipV", o.flipV}, {"clip", q(c->id)}};
}

void EditorController::setClipOrientation(int rotate, bool flipH, bool flipV) {
    const QVariantMap now = clipOrientation();
    if (!now["valid"].toBool()) return setStatus(tr("Chọn một clip video hoặc ảnh, hoặc đưa playhead vào nó"));
    const QString id = now["clip"].toString();
    const Orientation o{normalizeQuarterTurn(rotate), flipH, flipV};
    if (VS_EDIT(editor_->setClipOrientation({s(id)}, o))) {
        if (selected_ != id) setSelectedClip(id);
        setStatus(o.identity() ? tr("Clip về hướng gốc") : tr("Đã xoay %1° %2").arg(o.rotate).arg(
                                                                o.flipH ? tr("· lật ngang") : o.flipV ? tr("· lật dọc") : ""));
    }
}

void EditorController::rotateClip(int deg) {
    const QVariantMap o = clipOrientation();
    setClipOrientation(o["rotate"].toInt() + deg, o["flipH"].toBool(), o["flipV"].toBool());
}

void EditorController::flipClip(bool horizontal) {
    const QVariantMap o = clipOrientation();
    setClipOrientation(o["rotate"].toInt(), o["flipH"].toBool() != horizontal, o["flipV"].toBool() != !horizontal);
}

// ---------------------------------------------------------------- overlay length

void EditorController::setGeneratedSpan(bool whole) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(selected_)) : nullptr;
    if (!c || !c->generated()) return setStatus(tr("Chọn một lớp phủ, chữ hoặc sticker trên Timeline"));
    Time end = Time::zero();
    for (const Track& t : snapshot_->tracks)
        for (const Clip& x : t.clips)
            if (x.id != c->id) end = std::max(end, x.timelineOut());
    const Time start = whole ? Time::zero() : std::min(Time::fromSeconds(playhead_), end);
    if ((end - start).seconds() < 0.05) return setStatus(tr("Chưa có video để phủ theo: nhập video trước"));
    const std::string kind = c->content.value("kind", std::string{});
    if (VS_EDIT(editor_->setGeneratedClipSpan(c->id, start, end)))
        setStatus(whole ? tr("%1 chạy suốt video").arg(kind == "overlay" ? tr("Lớp phủ") : tr("Đối tượng"))
                        : tr("%1 chạy từ %2 tới hết video").arg(kind == "overlay" ? tr("Lớp phủ") : tr("Đối tượng"), tc(start.seconds())));
}

// ---------------------------------------------------------------- Đoạn áp dụng

void EditorController::setRangeStart() {
    // The part is chosen in the media clip under the playhead (or the selected one when inside it).
    QString id = selected_;
    const Clip* c = snapshot_ ? mediaClip(*snapshot_, id) : nullptr;
    const Time t = Time::fromSeconds(playhead_);
    if (!c || t < c->timelineIn || t >= c->timelineOut()) {
        id = mediaClipAtPlayhead();
        if (id.isEmpty() && snapshot_) {  // a sound clip under the playhead
            for (const Track& trk : snapshot_->tracks)
                for (const Clip& x : trk.clips)
                    if (!x.generated() && t >= x.timelineIn && t < x.timelineOut()) id = q(x.id);
        }
        c = snapshot_ && !id.isEmpty() ? snapshot_->findClip(s(id)) : nullptr;
    }
    if (!c) return setStatus(tr("Đưa playhead vào một clip rồi bấm Điểm đầu"));
    if (selected_ != id) setSelectedClip(id);
    rangeClip_ = id;
    rangeStart_ = std::clamp(playhead_, c->timelineIn.seconds(), c->timelineOut().seconds());
    rangeEnd_ = -1;
    setStatus(tr("Điểm đầu %1. Đưa playhead tới chỗ kết thúc rồi bấm Điểm cuối (O)").arg(tc(rangeStart_)));
    emit rangeChanged();
}

void EditorController::setRangeEnd() {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(rangeClip_)) : nullptr;
    if (!c || rangeStart_ < 0) return setStatus(tr("Bấm Điểm đầu trước (I)"));
    double a = rangeStart_, b = std::clamp(playhead_, c->timelineIn.seconds(), c->timelineOut().seconds());
    if (b < a) std::swap(a, b);
    if (b - a < 0.1) return setStatus(tr("Đoạn quá ngắn: đưa playhead xa hơn rồi bấm Điểm cuối"));
    rangeStart_ = a;
    rangeEnd_ = b;
    const bool clamped = playhead_ > c->timelineOut().seconds() + 1e-6 || playhead_ < c->timelineIn.seconds() - 1e-6;
    setStatus(clamped ? tr("Đoạn %1 → %2 (dừng ở mép clip: mỗi đoạn nằm trong một clip). Chọn việc cần làm cho đoạn này.")
                            .arg(tc(a), tc(b))
                      : tr("Đoạn %1 → %2. Chọn việc cần làm cho đoạn này.").arg(tc(a), tc(b)));
    emit rangeChanged();
}

void EditorController::clearRange() {
    if (rangeStart_ < 0) return;
    rangeStart_ = rangeEnd_ = -1;
    rangeClip_.clear();
    emit rangeChanged();
}

QString EditorController::isolateRange() {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(rangeClip_)) : nullptr;
    if (!c || !rangeComplete()) return {};
    const double eps = 1.0 / std::max(1.0, snapshot_->fps.toDouble());
    const double a = rangeStart_, b = rangeEnd_, c0 = c->timelineIn.seconds(), c1 = c->timelineOut().seconds();
    Id mid = c->id;
    const bool ok = VS_EDIT({
        if (a > c0 + eps) mid = editor_->splitClip(mid, Time::fromSeconds(a));
        if (b < c1 - eps) editor_->splitClip(mid, Time::fromSeconds(b));
    });
    if (!ok) return {};
    setSelectedClip(q(mid));
    return q(mid);
}

void EditorController::applyRange(const QString& action, double value) {
    const Clip* c = snapshot_ ? snapshot_->findClip(s(rangeClip_)) : nullptr;
    if (!c || !rangeComplete()) return setStatus(tr("Chọn đoạn trước: Điểm đầu (I), rồi Điểm cuối (O)"));
    const double a = rangeStart_, b = rangeEnd_;
    if (action == "zoom" || action == "volume") {
        const bool picture = action == "zoom";
        const Track* trk = snapshot_->trackOfClip(c->id);
        if (picture && (!trk || trk->type != TrackType::Video)) return setStatus(tr("Zoom cần clip video hoặc ảnh"));
        const auto g = picture ? kf::Group::Picture : kf::Group::Audio;
        const Time sa = c->toSource(Time::fromSeconds(a)), sb = c->toSource(Time::fromSeconds(b));
        const Transform st = animatedTransform(*c, sa);
        const Id id = c->id;
        // A key at each end: the start keeps today's look, the end gets the new one. One step each.
        const bool ok = VS_EDIT({
            editor_->addKeyframe(id, g, sa);
            editor_->addKeyframe(id, g, sb);
            if (picture)
                editor_->setPropertyAt(id, sb, {{kf::kScale, std::clamp(st.scale * value, 0.05, 20.0)},
                                                {kf::kX, st.x * value}, {kf::kY, st.y * value}});
            else
                editor_->setPropertyAt(id, sb, {{kf::kVolume, value}});
        });
        if (!ok) return;
        seek(a);
        setStatus(picture ? (value >= 1 ? tr("Đã đặt zoom dần %1× từ %2 tới %3. Bấm ▶ để xem.").arg(value, 0, 'f', 1).arg(tc(a), tc(b))
                                        : tr("Đã đặt thu nhỏ dần từ %1 tới %2. Bấm ▶ để xem.").arg(tc(a), tc(b)))
                          : (value == 0 ? tr("Âm thanh nhỏ dần tới tắt từ %1 tới %2").arg(tc(a), tc(b))
                                        : tr("Âm thanh to dần ×%1 từ %2 tới %3").arg(value, 0, 'f', 0).arg(tc(a), tc(b))));
        return;
    }
    const QString mid = isolateRange();
    if (mid.isEmpty()) return;
    clearRange();
    seek(a);
    if (action == "speed") {
        setClipSpeed({mid}, value);
        setStatus(value < 1 ? tr("Đoạn đã chọn chạy chậm %1×").arg(value) : tr("Đoạn đã chọn chạy nhanh %1×").arg(value));
    } else {
        setStatus(tr("Đoạn đã chọn được tách riêng: hiệu ứng, bộ lọc, màu chỉ áp vào đoạn này"));
    }
}
