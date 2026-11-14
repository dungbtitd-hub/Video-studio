import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../core/project_model.dart';
import 'media_cache.dart';
import 'theme.dart';

/// Touch timeline. Gestures only change what is drawn; the core is edited
/// once when a gesture ends (one command, one undo step), so dragging stays
/// smooth however large the project is.
///
/// Left column: one header per track (lock, hide/mute; long-press for the track
/// menu). Long-press a clip to drag it in time or onto another unlocked track of
/// the same type; it snaps to clip edges and the playhead when [snapMove] is set.
class TimelineView extends StatefulWidget {
  const TimelineView({
    super.key,
    required this.project,
    required this.playhead,
    required this.selectedClip,
    required this.onSeek,
    required this.onSelect,
    required this.onMove,
    this.onTrackFlags,
    this.onTrackMenu,
    this.snapMove,
    this.onTrim,
    this.snapTime,
    this.media,
    this.cuts,
    this.selectedCut,
    this.onCut,
  });

  final ProjectView project;
  final MediaCache? media; // thumbnails / waveforms drawn inside clips when ready
  final double playhead;
  final String? selectedClip;
  final ValueChanged<double> onSeek;
  final ValueChanged<String?> onSelect;
  final void Function(String clipId, String trackId, double start) onMove;
  final void Function(TrackView track, {required bool enabled, required bool locked, required bool muted})?
      onTrackFlags;
  final ValueChanged<TrackView?>? onTrackMenu; // null = no track (add a track)
  /// Snapped clip start for a proposed one, or null if nothing is within [tolerance] seconds.
  final double? Function(String clipId, double proposedStart, double tolerance)? snapMove;

  /// Drag a handle of the selected clip: [start] true = in point. [at] is the new edge time.
  final void Function(String clipId, {required bool start, required double at})? onTrim;

  /// Snapped edge time for trimming, or null.
  final double? Function(double t, double tolerance, String excludeClip)? snapTime;

  /// Places for transitions per video track id (badges under the cuts); tapping one calls [onCut].
  final Map<String, List<CutPoint>>? cuts;
  final CutPoint? selectedCut;
  final ValueChanged<CutPoint>? onCut;

  static const trackH = 56.0;
  static const rulerH = 22.0;
  static const headerW = 76.0;

  @override
  State<TimelineView> createState() => _TimelineViewState();
}

class _TimelineViewState extends State<TimelineView> {
  static const trackH = TimelineView.trackH;
  static const rulerH = TimelineView.rulerH;
  double pxPerSec = 40;
  double scroll = 0; // seconds at the left edge
  double _zoomStart = 40;

  // Clip drag (visual only until release)
  String? _dragClip;
  int _dragRow = -1; // source row
  int _targetRow = -1; // row the clip would land on
  double _dragDelta = 0;
  bool _snapped = false;

  // Trim of the selected clip by its handles (visual only until release)
  String? _trimClip;
  bool _trimStart = true;
  double _trimDelta = 0;
  double _trimOriginX = 0;
  Offset? _downAt; // where the finger landed: gesture callbacks report positions after the touch slop
  static const handleHit = 22.0; // touch slop around a handle, px

  /// The selected clip's handle under [p], if any: (clip, start edge?).
  (ClipView, bool)? _handleAt(Offset p) {
    final sel = widget.selectedClip;
    if (sel == null) return null;
    final row = _rowAt(p.dy);
    if (row < 0 || row >= widget.project.tracks.length) return null;
    final track = widget.project.tracks[row];
    if (track.locked) return null;
    for (final c in track.clips) {
      if (c.id != sel) continue;
      final x0 = (c.start - scroll) * pxPerSec, x1 = (c.end - scroll) * pxPerSec;
      if ((p.dx - x0).abs() <= handleHit) return (c, true);
      if ((p.dx - x1).abs() <= handleHit) return (c, false);
    }
    return null;
  }

  void _trimUpdate(double x) {
    final id = _trimClip;
    if (id == null) return;
    final clip = widget.project.tracks.expand((t) => t.clips).firstWhere((c) => c.id == id);
    final edge = _trimStart ? clip.start : clip.end;
    var t = edge + (x - _trimOriginX) / pxPerSec;
    // Keep at least ~1 frame, never before 0.
    final minLen = 1 / (widget.project.fps > 0 ? widget.project.fps : 30);
    t = _trimStart ? t.clamp(0, clip.end - minLen).toDouble() : (t < clip.start + minLen ? clip.start + minLen : t);
    var snapped = false;
    final s = widget.snapTime?.call(t, 12 / pxPerSec, id);
    if (s != null && (_trimStart ? s < clip.end - minLen : s > clip.start + minLen)) {
      t = s;
      snapped = true;
    }
    if (snapped && !_snapped) HapticFeedback.selectionClick();
    setState(() {
      _trimDelta = t - edge;
      _snapped = snapped;
    });
  }

  int _rowAt(double dy) => ((dy - rulerH) / trackH).floor();

  /// The transition badge under [p]: the cut between two clips, or a free edge.
  CutPoint? _cutAt(Offset p) {
    final cuts = widget.cuts;
    if (cuts == null || widget.onCut == null) return null;
    final row = _rowAt(p.dy);
    if (row < 0 || row >= widget.project.tracks.length) return null;
    final track = widget.project.tracks[row];
    if (track.audio || track.locked) return null;
    CutPoint? best;
    var bestD = double.infinity;
    for (final c in cuts[track.id] ?? const <CutPoint>[]) {
      if (!cutVisible(c, widget.selectedClip)) continue;
      final r = cutBadge(row, c, scroll, pxPerSec).inflate(8);
      if (!r.contains(p)) continue;
      final d = (r.center - p).distance;
      if (d < bestD) {
        bestD = d;
        best = c;
      }
    }
    return best;
  }

  /// Keyframe of [c] under [p] (picture keys on the clip's top edge, audio keys on the bottom).
  double? _keyAt(ClipView c, Offset p) {
    final top = rulerH + _rowAt(p.dy) * trackH + 4;
    final audio = p.dy > top + (trackH - 10) / 2;
    double? best;
    var bestDx = 16.0; // finger-sized
    for (final k in audio ? c.audioKeys : c.pictureKeys) {
      final dx = ((k - scroll) * pxPerSec - p.dx).abs();
      if (dx <= bestDx) {
        best = k;
        bestDx = dx;
      }
    }
    return best;
  }

  (TrackView, ClipView)? _hit(Offset p) {
    final row = _rowAt(p.dy);
    if (row < 0 || row >= widget.project.tracks.length) return null;
    final t = scroll + p.dx / pxPerSec;
    final track = widget.project.tracks[row];
    for (final c in track.clips) {
      if (t >= c.start && t < c.end) return (track, c);
    }
    return null;
  }

  void _dragUpdate(LongPressMoveUpdateDetails d) {
    final id = _dragClip;
    if (id == null) return;
    final tracks = widget.project.tracks;
    final from = tracks[_dragRow];
    final row = _rowAt(d.localPosition.dy);
    var target = _targetRow;
    if (row >= 0 && row < tracks.length && tracks[row].audio == from.audio && !tracks[row].locked) target = row;
    var delta = d.offsetFromOrigin.dx / pxPerSec;
    var snapped = false;
    final clip = from.clips.firstWhere((c) => c.id == id);
    final snap = widget.snapMove?.call(id, (clip.start + delta).clamp(0, double.infinity).toDouble(), 12 / pxPerSec);
    if (snap != null) {
      delta = snap - clip.start;
      snapped = true;
    }
    if (snapped && !_snapped) HapticFeedback.selectionClick();
    setState(() {
      _dragDelta = delta;
      _targetRow = target;
      _snapped = snapped;
    });
  }

  Widget _header(TrackView t) {
    final onFlags = widget.onTrackFlags;
    Widget toggle(IconData icon, bool active, String tip, VoidCallback onTap) => InkResponse(
          onTap: onFlags == null ? null : onTap,
          radius: 18,
          child: Padding(
            padding: const EdgeInsets.all(5),
            child: Icon(icon, size: 17, color: active ? VsColors.accent : VsColors.fg3, semanticLabel: tip),
          ),
        );
    return GestureDetector(
      onLongPress: () => widget.onTrackMenu?.call(t),
      child: Container(
        height: trackH,
        padding: const EdgeInsets.only(left: 6, top: 4),
        decoration: const BoxDecoration(
            color: VsColors.panel, border: Border(bottom: BorderSide(color: VsColors.panel2, width: 2))),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(t.name,
              maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(color: VsColors.fg2, fontSize: 11)),
          Row(children: [
            toggle(t.locked ? Icons.lock : Icons.lock_open, t.locked, t.locked ? 'Mở khóa' : 'Khóa',
                () => onFlags!(t, enabled: t.enabled, locked: !t.locked, muted: t.muted)),
            if (t.audio)
              toggle(t.muted ? Icons.volume_off : Icons.volume_up, t.muted, t.muted ? 'Bật tiếng' : 'Tắt tiếng',
                  () => onFlags!(t, enabled: t.enabled, locked: t.locked, muted: !t.muted))
            else
              toggle(t.enabled ? Icons.visibility : Icons.visibility_off, !t.enabled, t.enabled ? 'Ẩn' : 'Hiện',
                  () => onFlags!(t, enabled: !t.enabled, locked: t.locked, muted: t.muted)),
          ]),
        ]),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final lanes = LayoutBuilder(builder: (context, box) {
      return GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTapUp: (d) {
          final cut = _cutAt(d.localPosition);
          if (cut != null) return widget.onCut!(cut);
          final h = _hit(d.localPosition);
          widget.onSelect(h?.$2.id);
          // A keyframe diamond: jump to it.
          final key = h == null ? null : _keyAt(h.$2, d.localPosition);
          if (key != null) return widget.onSeek(key);
          if (h == null) widget.onSeek(scroll + d.localPosition.dx / pxPerSec);
        },
        onLongPressStart: (d) {
          final h = _hit(d.localPosition);
          if (h == null) return;
          widget.onSelect(h.$2.id);
          if (h.$1.locked) return; // selectable, not editable
          final row = _rowAt(d.localPosition.dy);
          setState(() {
            _dragClip = h.$2.id;
            _dragRow = row;
            _targetRow = row;
            _dragDelta = 0;
            _snapped = false;
          });
        },
        onLongPressMoveUpdate: _dragUpdate,
        onLongPressEnd: (_) {
          final id = _dragClip;
          if (id != null && (_dragDelta.abs() > 0.02 || _targetRow != _dragRow)) {
            final clip = widget.project.tracks[_dragRow].clips.firstWhere((c) => c.id == id);
            widget.onMove(id, widget.project.tracks[_targetRow].id,
                (clip.start + _dragDelta).clamp(0, double.infinity).toDouble());
          }
          setState(() {
            _dragClip = null;
            _snapped = false;
          });
        },
        onScaleStart: (d) {
          _zoomStart = pxPerSec;
          final down = _downAt ?? d.localFocalPoint;
          final h = d.pointerCount == 1 && widget.onTrim != null ? _handleAt(down) : null;
          if (h != null) {
            setState(() {
              _trimClip = h.$1.id;
              _trimStart = h.$2;
              _trimDelta = 0;
              _trimOriginX = down.dx;
              _snapped = false;
            });
          }
        },
        onScaleEnd: (_) {
          final id = _trimClip;
          if (id == null) return;
          if (_trimDelta.abs() > 0.001) {
            final clip = widget.project.tracks.expand((t) => t.clips).firstWhere((c) => c.id == id);
            widget.onTrim!(id, start: _trimStart, at: (_trimStart ? clip.start : clip.end) + _trimDelta);
          }
          setState(() {
            _trimClip = null;
            _snapped = false;
          });
        },
        onScaleUpdate: (d) => _trimClip != null
            ? _trimUpdate(d.localFocalPoint.dx)
            : setState(() {
                if (d.pointerCount >= 2) {
                  pxPerSec = (_zoomStart * d.horizontalScale).clamp(4, 400).toDouble();
                } else {
                  // one finger: scroll the timeline
                  scroll = (scroll - d.focalPointDelta.dx / pxPerSec).clamp(0, widget.project.duration + 5).toDouble();
                }
              }),
        child: CustomPaint(
          size: Size(box.maxWidth, rulerH + widget.project.tracks.length * trackH + 8),
          painter: _TimelinePainter(
            project: widget.project,
            playhead: widget.playhead,
            selected: widget.selectedClip,
            pxPerSec: pxPerSec,
            scroll: scroll,
            dragClip: _dragClip,
            dragDelta: _dragDelta,
            dragRow: _targetRow,
            snapped: _snapped,
            trimClip: _trimClip,
            trimStart: _trimStart,
            trimDelta: _trimDelta,
            media: widget.media,
            cuts: widget.onCut == null ? null : widget.cuts,
            selectedCut: widget.selectedCut,
          ),
        ),
      );
    });
    return Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
      SizedBox(
        width: TimelineView.headerW,
        child: Column(children: [
          SizedBox(
            height: rulerH,
            child: widget.onTrackMenu == null
                ? null
                : InkWell(
                    onTap: () => widget.onTrackMenu!(null),
                    child: const Row(children: [
                      SizedBox(width: 4),
                      Icon(Icons.add, size: 14, color: VsColors.fg2),
                      Flexible(
                          child: Text(' Track',
                              maxLines: 1,
                              overflow: TextOverflow.clip,
                              style: TextStyle(color: VsColors.fg2, fontSize: 11))),
                    ]),
                  ),
          ),
          for (final t in widget.project.tracks) _header(t),
        ]),
      ),
      Expanded(child: Listener(onPointerDown: (e) => _downAt = e.localPosition, child: lanes)),
    ]);
  }
}

class _TimelinePainter extends CustomPainter {
  _TimelinePainter({
    required this.project,
    required this.playhead,
    required this.selected,
    required this.pxPerSec,
    required this.scroll,
    required this.dragClip,
    required this.dragDelta,
    this.dragRow = -1,
    this.snapped = false,
    this.trimClip,
    this.trimStart = true,
    this.trimDelta = 0,
    this.media,
    this.cuts,
    this.selectedCut,
  }) : super(repaint: media);

  final ProjectView project;
  final double playhead;
  final String? selected;
  final double pxPerSec;
  final double scroll;
  final String? dragClip;
  final double dragDelta;
  final int dragRow; // row the dragged clip is drawn on
  final bool snapped;
  final String? trimClip;
  final bool trimStart;
  final double trimDelta;
  final MediaCache? media;
  final Map<String, List<CutPoint>>? cuts;
  final CutPoint? selectedCut;

  @override
  void paint(Canvas canvas, Size size) {
    const trackH = TimelineView.trackH, rulerH = TimelineView.rulerH;
    canvas.drawRect(Offset.zero & size, Paint()..color = VsColors.panel);
    final tick = Paint()..color = VsColors.fg3;
    final step = pxPerSec >= 30 ? 1 : (pxPerSec >= 8 ? 5 : 15);
    for (var s = (scroll ~/ step) * step; (s - scroll) * pxPerSec < size.width; s += step) {
      final x = (s - scroll) * pxPerSec;
      canvas.drawLine(Offset(x, rulerH - (s % (step * 5) == 0 ? 10 : 5)), Offset(x, rulerH), tick);
      if (s % (step * 5) == 0) {
        final tp = TextPainter(
          text: TextSpan(
              text: '${s ~/ 60}:${(s % 60).toString().padLeft(2, '0')}',
              style: const TextStyle(color: VsColors.fg3, fontSize: 10)),
          textDirection: TextDirection.ltr,
        )..layout();
        tp.paint(canvas, Offset(x + 3, 2));
      }
    }
    // Backgrounds first, so a clip dragged onto another row is never covered.
    for (var i = 0; i < project.tracks.length; i++) {
      canvas.drawRect(Rect.fromLTWH(0, rulerH + i * trackH, size.width, trackH - 2),
          Paint()..color = i == dragRow && dragClip != null ? const Color(0xFF2A2F3A) : VsColors.panel2);
    }
    for (var i = 0; i < project.tracks.length; i++) {
      final t = project.tracks[i];
      final trackTop = rulerH + i * trackH;
      final audio = t.audio;
      final dim = !t.enabled || (audio && t.muted);
      if (dim) canvas.saveLayer(Offset.zero & size, Paint()..color = const Color(0x66000000)); // 40% opacity
      for (final c in t.clips) {
        final dragging = c.id == dragClip;
        final top = dragging && dragRow >= 0 ? rulerH + dragRow * trackH : trackTop;
        final trimming = c.id == trimClip;
        final start = c.start + (dragging ? dragDelta : 0) + (trimming && trimStart ? trimDelta : 0);
        final end = c.end + (dragging ? dragDelta : 0) + (trimming && !trimStart ? trimDelta : 0);
        final x = (start - scroll) * pxPerSec;
        final w = (end - start) * pxPerSec;
        if (x > size.width || x + w < 0) continue; // cull off-screen clips
        final r =
            RRect.fromRectAndRadius(Rect.fromLTWH(x, top + 4, w < 3 ? 3 : w, trackH - 10), const Radius.circular(4));
        // Text, stickers and overlays have no media: a colour of their own instead of thumbnails.
        final (fill, edge) = switch (c.kind) {
          'text' => (const Color(0xFF5C46A0), const Color(0xFF8C74D8)),
          'sticker' => (const Color(0xFFA04678), const Color(0xFFD874AC)),
          'overlay' => (const Color(0xFF2E7D8C), const Color(0xFF5FC3D4)),
          _ => audio ? (VsColors.clipAudio2, VsColors.clipAudio) : (VsColors.clipVideo2, VsColors.clipVideo),
        };
        canvas.drawRRect(r, Paint()..color = fill);
        if (media != null && w > 8 && !c.generated) {
          canvas.save();
          canvas.clipRRect(r);
          if (audio) {
            _waveform(canvas, c, r.outerRect, size.width);
          } else {
            _thumbnails(canvas, c, r.outerRect, size.width);
          }
          canvas.restore();
        }
        canvas.drawRRect(
            r,
            Paint()
              ..style = PaintingStyle.stroke
              ..strokeWidth = c.id == selected ? 2 : 1
              ..color = (dragging || trimming) && snapped
                  ? const Color(0xFFFFD65A)
                  : c.id == selected
                      ? VsColors.accent
                      : edge);
        if (w > 30) {
          final tp = TextPainter(
            text: TextSpan(text: c.name, style: const TextStyle(color: VsColors.fg, fontSize: 11)),
            textDirection: TextDirection.ltr,
            maxLines: 1,
            ellipsis: '…',
          )..layout(maxWidth: w - 10);
          tp.paint(canvas, Offset(x + 6, top + 8));
        }
        if (!dragging && !trimming) _keyframes(canvas, c, r.outerRect, c.id == selected);
        // Trim handles on the selected clip of an unlocked track.
        if (c.id == selected && !t.locked && w > 12) {
          final hp = Paint()..color = (trimming && snapped) ? const Color(0xFFFFD65A) : VsColors.accent;
          for (final hx in [x, x + w - 6]) {
            canvas.drawRRect(
                RRect.fromRectAndRadius(Rect.fromLTWH(hx, top + 4, 6, trackH - 10), const Radius.circular(3)), hp);
          }
        }
      }
      if (!audio && dragClip == null && trimClip == null) _cuts(canvas, i, t, size);
      if (dim) canvas.restore();
      if (t.locked) {
        final hatch = Paint()
          ..color = const Color(0x1FFFFFFF)
          ..strokeWidth = 1;
        canvas.save();
        canvas.clipRect(Rect.fromLTWH(0, trackTop, size.width, trackH - 2));
        for (var x = -trackH; x < size.width; x += 10) {
          canvas.drawLine(Offset(x, trackTop + trackH), Offset(x + trackH, trackTop), hatch);
        }
        canvas.restore();
      }
    }
    final px = (playhead - scroll) * pxPerSec;
    canvas.drawLine(
        Offset(px, 0),
        Offset(px, size.height),
        Paint()
          ..color = VsColors.accent
          ..strokeWidth = 2);
  }

  // Transitions: a band where one plays and a badge under each cut ("+" where none is set yet).
  void _cuts(Canvas canvas, int row, TrackView t, Size size) {
    final list = cuts?[t.id];
    if (list == null || t.locked) return;
    final top = TimelineView.rulerH + row * TimelineView.trackH;
    for (final c in list) {
      if (!cutVisible(c, selected)) continue;
      final b = cutBadge(row, c, scroll, pxPerSec);
      if (b.right < -20 || b.left > size.width + 20) continue;
      final isSel = selectedCut != null && selectedCut!.clipId == c.clipId && selectedCut!.edge == c.edge;
      if (c.type != null && c.duration > 0) {
        final (s, e) = c.window;
        canvas.drawRect(Rect.fromLTWH((s - scroll) * pxPerSec, top + TimelineView.trackH - 13, (e - s) * pxPerSec, 5),
            Paint()..color = isSel ? VsColors.accent : const Color(0x99FFFFFF));
      }
      canvas.drawRRect(RRect.fromRectAndRadius(b, const Radius.circular(4)),
          Paint()..color = isSel ? VsColors.accent : const Color(0xE61E1E24));
      canvas.drawRRect(
          RRect.fromRectAndRadius(b, const Radius.circular(4)),
          Paint()
            ..style = PaintingStyle.stroke
            ..color = const Color(0xCCFFFFFF));
      final pen = Paint()
        ..color = VsColors.fg
        ..strokeWidth = 1.4
        ..style = PaintingStyle.stroke;
      final m = b.center;
      if (c.type == null) {
        canvas.drawLine(m - const Offset(4, 0), m + const Offset(4, 0), pen);
        canvas.drawLine(m - const Offset(0, 4), m + const Offset(0, 4), pen);
      } else {
        canvas.drawRect(Rect.fromLTWH(m.dx - 5.5, m.dy - 4.5, 7, 7), pen);
        canvas.drawRect(Rect.fromLTWH(m.dx - 1.5, m.dy - 2.5, 7, 7), pen);
      }
    }
  }

  // Keyframe diamonds: picture keys on the top edge (amber), audio keys on the bottom
  // edge (green), joined by a line where the value is changing; white = at the playhead.
  void _keyframes(Canvas canvas, ClipView c, Rect r, bool selected) {
    if (!c.animated) return;
    const rad = 5.0;
    void row(List<double> keys, Color col, double y) {
      if (keys.isEmpty) return;
      if (keys.length > 1) {
        canvas.drawLine(
            Offset((keys.first - scroll) * pxPerSec, y),
            Offset((keys.last - scroll) * pxPerSec, y),
            Paint()
              ..color = col.withValues(alpha: selected ? 0.6 : 0.3)
              ..strokeWidth = 1.5);
      }
      for (final k in keys) {
        final x = (k - scroll) * pxPerSec;
        final here = (k - playhead).abs() * pxPerSec < 1.5;
        final d = Path()
          ..moveTo(x, y - rad)
          ..lineTo(x + rad, y)
          ..lineTo(x, y + rad)
          ..lineTo(x - rad, y)
          ..close();
        canvas.drawPath(d, Paint()..color = here ? VsColors.fg : (selected ? col : col.withValues(alpha: 0.6)));
        canvas.drawPath(
            d,
            Paint()
              ..style = PaintingStyle.stroke
              ..color = const Color(0xFF0A0C10));
      }
    }

    row(c.pictureKeys, VsColors.accent, r.top + rad + 1);
    row(c.audioKeys, const Color(0xFF78E6BE), r.bottom - rad - 1);
  }

  // Thumbnail tiles across the visible part of a video clip (one image, sub-rects).
  void _thumbnails(Canvas canvas, ClipView c, Rect r, double viewW) {
    final t = media!.thumbs(c.assetId);
    if (t == null || t.count == 0) return;
    final th = r.height - 16, tw = th * t.width / t.height;
    if (tw < 4) return;
    final asset = project.assets.where((a) => a.id == c.assetId).firstOrNull;
    final total = asset?.duration ?? c.duration; // the strip spans the whole asset
    final paint = Paint()..filterQuality = FilterQuality.low;
    final x0 = r.left < 0 ? r.left + ((-r.left) ~/ tw) * tw : r.left;
    for (var x = x0; x < r.right && x < viewW; x += tw) {
      final src = c.sourceIn + (x + tw / 2 - r.left) / pxPerSec * c.speed;
      final i = total > 0 ? (src / total * t.count).floor().clamp(0, t.count - 1) : 0;
      canvas.drawImageRect(t.image, Rect.fromLTWH(0, i * t.height.toDouble(), t.width.toDouble(), t.height.toDouble()),
          Rect.fromLTWH(x, r.top + 15, tw, th), paint);
    }
  }

  // Peak envelope, one vertical line per visible pixel column; quiet media scaled up (max 8x).
  void _waveform(Canvas canvas, ClipView c, Rect r, double viewW) {
    final w = media!.waveform(c.assetId);
    if (w == null || w.buckets == 0) return;
    final mid = r.center.dy + 6;
    final half = (r.height - 16) / 2 / (w.loudest < 16 ? 16 : w.loudest) * 127;
    final paint = Paint()
      ..color = const Color(0xC878DCAA)
      ..strokeWidth = 1;
    final from = r.left < 0 ? 0.0 : r.left, to = r.right < viewW ? r.right : viewW;
    for (var x = from.floorToDouble(); x < to; x += 1) {
      final a = c.sourceIn + (x - r.left) / pxPerSec * c.speed;
      var i0 = (a * w.bucketsPerSecond).floor();
      var i1 = ((a + c.speed / pxPerSec) * w.bucketsPerSecond).floor() + 1;
      if (i0 < 0) i0 = 0;
      if (i0 >= w.buckets) break;
      if (i1 > w.buckets) i1 = w.buckets;
      var lo = 0, hi = 0;
      for (var k = i0; k < i1; k++) {
        final mn = w.peaks[k * 2], mx = w.peaks[k * 2 + 1];
        if (mn < lo) lo = mn;
        if (mx > hi) hi = mx;
      }
      canvas.drawLine(Offset(x + 0.5, mid - hi / 127 * half), Offset(x + 0.5, mid - lo / 127 * half), paint);
    }
  }

  @override
  bool shouldRepaint(_TimelinePainter old) =>
      old.project != project ||
      old.cuts != cuts ||
      old.selectedCut != selectedCut ||
      old.playhead != playhead ||
      old.selected != selected ||
      old.pxPerSec != pxPerSec ||
      old.scroll != scroll ||
      old.dragClip != dragClip ||
      old.dragDelta != dragDelta ||
      old.dragRow != dragRow ||
      old.snapped != snapped ||
      old.trimClip != trimClip ||
      old.trimDelta != trimDelta ||
      old.media != media;
}

/// Cuts between two clips always show a badge; a clip's free start/end only when a
/// transition is set there or the clip is selected (to fade it in or out).
bool cutVisible(CutPoint c, String? selectedClip) => c.joined || c.type != null || c.clipId == selectedClip;

/// Where a cut's badge sits in the lanes: on the cut, or just inside a free edge.
Rect cutBadge(int row, CutPoint c, double scroll, double pxPerSec) {
  const w = 22.0, h = 18.0;
  final x = (c.at - scroll) * pxPerSec + (c.joined ? 0 : (c.edge == 0 ? w / 2 + 3 : -w / 2 - 3));
  final top = TimelineView.rulerH + row * TimelineView.trackH + TimelineView.trackH - 6 - h - 2;
  return Rect.fromLTWH(x - w / 2, top, w, h);
}
