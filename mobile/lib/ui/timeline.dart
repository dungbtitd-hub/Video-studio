import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../core/project_model.dart';
import 'media_cache.dart';
import 'theme.dart';

/// Touch timeline, as in the approved prototype (MOB-04):
///
///  - The playhead is a fixed line in the middle; swiping the timeline slides the clips under it
///    and seeks continuously, so the preview follows the finger. While playing, the clips scroll
///    by under the line. Every tool acts at that line (split, keyframes, the clip under it).
///  - One lane per kind of layer, top to bottom: texts, stickers, overlay effects, other video,
///    the main video (taller, with thumbnails), then sound. No header column: long-press an
///    empty part of a lane for its menu (lock, hide, mute, rename...).
///  - Tap a clip to select it; long-press a clip to drag it in time or to another lane of the
///    same kind; drag the handles of the selected clip to trim. Two fingers zoom.
///
/// Gestures only change what is drawn; the core is edited once when a gesture ends (one
/// command, one undo step), so dragging stays smooth however large the project is.
class TimelineView extends StatefulWidget {
  const TimelineView({
    super.key,
    required this.project,
    required this.playhead,
    required this.selectedClip,
    required this.onSeek,
    required this.onSelect,
    required this.onMove,
    this.onScrub,
    this.onTrackFlags,
    this.onTrackMenu,
    this.snapMove,
    this.onTrim,
    this.snapTime,
    this.media,
    this.cuts,
    this.selectedCut,
    this.onCut,
    this.range,
  });

  final ProjectView project;
  /// "Đoạn áp dụng" being chosen: lit on the timeline (end null while only the start is set).
  final (double, double?)? range;
  final MediaCache? media; // thumbnails / waveforms drawn inside clips when ready
  final double playhead;
  final String? selectedClip;
  final ValueChanged<double> onSeek;

  /// true when a swipe starts (pause playback), false when it ends.
  final ValueChanged<bool>? onScrub;
  final ValueChanged<String?> onSelect;
  final void Function(String clipId, String trackId, double start) onMove;
  final void Function(TrackView track, {required bool enabled, required bool locked, required bool muted})?
      onTrackFlags;
  final ValueChanged<TrackView?>? onTrackMenu;

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

  static const rulerH = 22.0;
  static const gap = 4.0;
  static const startPxPerSec = 40.0;

  /// Height of a lane: the main media lane shows thumbnails, the others are slim.
  static double laneHeight(TrackView t) => switch (t.laneKind) {
        'media' || '' || 'mixed' => 52.0,
        'audio' => 34.0,
        _ => 28.0,
      };

  /// Top of each lane, then the total height, for [project.tracks] in display order.
  static (List<double>, double) laneTops(ProjectView project) {
    final tops = <double>[];
    var y = rulerH + gap;
    for (final t in project.tracks) {
      tops.add(y);
      y += laneHeight(t) + gap;
    }
    return (tops, y + 4);
  }

  /// Height the timeline needs for [project].
  static double heightFor(ProjectView project) => project.tracks.isEmpty ? rulerH + 60 : laneTops(project).$2;

  /// Seconds at the left edge of lanes [width] wide, with the playhead in the middle.
  static double scrollFor(double playhead, double width, double pxPerSec) => playhead - width / 2 / pxPerSec;

  @override
  State<TimelineView> createState() => _TimelineViewState();
}

class _TimelineViewState extends State<TimelineView> {
  double pxPerSec = TimelineView.startPxPerSec;
  double _zoomStart = TimelineView.startPxPerSec;
  double _width = 0;

  // Swipe: the playhead follows the finger (seconds), until the parent's playhead catches up.
  double? _scrubHead;
  bool _zooming = false;

  double get _head => _scrubHead ?? widget.playhead;
  double get scroll => TimelineView.scrollFor(_head, _width, pxPerSec);

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

  late List<double> _tops;

  @override
  void didUpdateWidget(TimelineView old) {
    super.didUpdateWidget(old);
    // The parent caught up with the swipe: draw from its playhead again.
    if (_scrubHead != null && !_scrubbing && (widget.playhead - _scrubHead!).abs() < 1e-6) _scrubHead = null;
  }

  bool _scrubbing = false;

  int _rowAt(double dy) {
    final tracks = widget.project.tracks;
    for (var i = 0; i < tracks.length; i++) {
      if (dy >= _tops[i] - TimelineView.gap / 2 && dy < _tops[i] + TimelineView.laneHeight(tracks[i]) + TimelineView.gap / 2) {
        return i;
      }
    }
    return -1;
  }

  /// The selected clip's handle under [p], if any: (clip, start edge?).
  (ClipView, bool)? _handleAt(Offset p) {
    final sel = widget.selectedClip;
    if (sel == null) return null;
    final row = _rowAt(p.dy);
    if (row < 0) return null;
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

  /// The transition badge under [p]: the cut between two clips, or a free edge.
  CutPoint? _cutAt(Offset p) {
    final cuts = widget.cuts;
    if (cuts == null || widget.onCut == null) return null;
    final row = _rowAt(p.dy);
    if (row < 0) return null;
    final track = widget.project.tracks[row];
    if (track.audio || track.locked) return null;
    CutPoint? best;
    var bestD = double.infinity;
    for (final c in cuts[track.id] ?? const <CutPoint>[]) {
      if (!cutVisible(c, widget.selectedClip)) continue;
      final r = cutBadge(_laneRect(row), c, scroll, pxPerSec).inflate(8);
      if (!r.contains(p)) continue;
      final d = (r.center - p).distance;
      if (d < bestD) {
        bestD = d;
        best = c;
      }
    }
    return best;
  }

  Rect _laneRect(int row) =>
      Rect.fromLTWH(0, _tops[row], _width, TimelineView.laneHeight(widget.project.tracks[row]));

  /// Keyframe of [c] under [p] (picture keys on the clip's top edge, audio keys on the bottom).
  double? _keyAt(int row, ClipView c, Offset p) {
    final lane = _laneRect(row);
    final audio = p.dy > lane.center.dy;
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
    if (row < 0) return null;
    final t = scroll + p.dx / pxPerSec;
    final track = widget.project.tracks[row];
    for (final c in track.clips) {
      if (t >= c.start && t < c.end) return (track, c);
    }
    return null;
  }

  /// Lanes a clip from [from] may be dropped on: same kind, unlocked.
  bool _accepts(TrackView from, TrackView to) {
    if (to.locked || to.audio != from.audio) return false;
    if (from.audio) return true;
    final a = from.laneKind, b = to.laneKind;
    if (a == 'media' || a == '' || a == 'mixed') return to.takesMedia;
    return a == b || b == '';
  }

  void _dragUpdate(LongPressMoveUpdateDetails d) {
    final id = _dragClip;
    if (id == null) return;
    final tracks = widget.project.tracks;
    final from = tracks[_dragRow];
    final row = _rowAt(d.localPosition.dy);
    var target = _targetRow;
    if (row >= 0 && _accepts(from, tracks[row])) target = row;
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

  void _scrubTo(double dx) {
    final end = widget.project.duration;
    final t = (_head - dx / pxPerSec).clamp(0.0, end > 0 ? end : 0.0).toDouble();
    if (t == _scrubHead) return;
    setState(() => _scrubHead = t);
    widget.onSeek(t);
  }

  @override
  Widget build(BuildContext context) {
    _tops = TimelineView.laneTops(widget.project).$1;
    return LayoutBuilder(builder: (context, box) {
      _width = box.maxWidth;
      return Listener(
        onPointerDown: (e) => _downAt = e.localPosition,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTapUp: (d) {
            final cut = _cutAt(d.localPosition);
            if (cut != null) return widget.onCut!(cut);
            final h = _hit(d.localPosition);
            // A keyframe diamond: jump to it (it moves under the playhead).
            final key = h == null ? null : _keyAt(_rowAt(d.localPosition.dy), h.$2, d.localPosition);
            widget.onSelect(h?.$2.id);
            if (key != null) widget.onSeek(key);
          },
          onLongPressStart: (d) {
            final h = _hit(d.localPosition);
            if (h == null) {
              // An empty part of a lane: that lane's menu.
              final row = _rowAt(d.localPosition.dy);
              if (row >= 0) widget.onTrackMenu?.call(widget.project.tracks[row]);
              return;
            }
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
            _zooming = d.pointerCount >= 2;
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
            } else if (!_zooming) {
              _scrubbing = true;
              _scrubHead = widget.playhead;
              widget.onScrub?.call(true);
            }
          },
          onScaleUpdate: (d) {
            if (_trimClip != null) return _trimUpdate(d.localFocalPoint.dx);
            if (d.pointerCount >= 2 || _zooming) {
              _zooming = true;
              setState(() => pxPerSec = (_zoomStart * d.horizontalScale).clamp(4, 400).toDouble());
            } else if (_scrubbing) {
              _scrubTo(d.focalPointDelta.dx);
            }
          },
          onScaleEnd: (_) {
            final id = _trimClip;
            if (id != null) {
              if (_trimDelta.abs() > 0.001) {
                final clip = widget.project.tracks.expand((t) => t.clips).firstWhere((c) => c.id == id);
                widget.onTrim!(id, start: _trimStart, at: (_trimStart ? clip.start : clip.end) + _trimDelta);
              }
              setState(() {
                _trimClip = null;
                _snapped = false;
              });
            }
            if (_scrubbing) {
              _scrubbing = false;
              widget.onScrub?.call(false);
              if (_scrubHead != null && (widget.playhead - _scrubHead!).abs() < 1e-6) setState(() => _scrubHead = null);
            }
            _zooming = false;
          },
          child: CustomPaint(
            size: Size(box.maxWidth, TimelineView.heightFor(widget.project)),
            painter: _TimelinePainter(
              project: widget.project,
              tops: _tops,
              playhead: _head,
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
              range: widget.range,
            ),
          ),
        ),
      );
    });
  }
}

class _TimelinePainter extends CustomPainter {
  _TimelinePainter({
    required this.project,
    required this.tops,
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
    this.range,
  }) : super(repaint: media);

  final ProjectView project;
  final List<double> tops;
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
  final (double, double?)? range;

  static const _textFill = Color(0xFF7B5BC4), _stickerFill = Color(0xFF9C4E7A), _overlayFill = Color(0xFF2E7D8C);

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = VsColors.bg);
    final x0 = (0 - scroll) * pxPerSec; // where the timeline starts
    final dur = project.duration;
    // Ruler: labels every 1/2/5/10/30 s, whatever fits.
    final tick = Paint()..color = VsColors.fg3;
    final step = [1, 2, 5, 10, 30, 60].firstWhere((s) => s * pxPerSec >= 44, orElse: () => 60);
    final first = scroll <= 0 ? 0 : (scroll / step).floor() * step;
    for (var s = first; (s - scroll) * pxPerSec < size.width; s += step) {
      final x = (s - scroll) * pxPerSec;
      canvas.drawLine(Offset(x, TimelineView.rulerH - 5), Offset(x, TimelineView.rulerH), tick);
      final tp = TextPainter(
        text: TextSpan(
            text: '${s ~/ 60}:${(s % 60).toString().padLeft(2, '0')}',
            style: const TextStyle(color: VsColors.fg3, fontSize: 10, fontFeatures: [ui.FontFeature.tabularFigures()])),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(x - tp.width / 2, 3));
    }
    // A faint band where the timeline has content.
    if (dur > 0) {
      canvas.drawRect(Rect.fromLTWH(x0, TimelineView.rulerH, dur * pxPerSec, size.height - TimelineView.rulerH), Paint()..color = const Color(0x08FFFFFF));
    }
    if (dragClip != null && dragRow >= 0) {
      final lane = Rect.fromLTWH(0, tops[dragRow], size.width, TimelineView.laneHeight(project.tracks[dragRow]));
      canvas.drawRect(lane, Paint()..color = const Color(0x22FFFFFF));
    }
    // Under the playhead with nothing selected: the clip the tools will act on.
    String? underHead;
    if (selected == null) {
      for (final t in project.tracks.reversed) {
        if (!t.takesMedia) continue;
        for (final c in t.clips) {
          if (playhead >= c.start && playhead < c.end) underHead = c.id;
        }
      }
    }
    for (var i = 0; i < project.tracks.length; i++) {
      final t = project.tracks[i];
      final laneH = TimelineView.laneHeight(t);
      final audio = t.audio;
      final dim = !t.enabled || (audio && t.muted);
      if (dim) canvas.saveLayer(Offset.zero & size, Paint()..color = const Color(0x66000000)); // 40% opacity
      for (final c in t.clips) {
        final dragging = c.id == dragClip;
        final top = dragging && dragRow >= 0 ? tops[dragRow] : tops[i];
        final h = dragging && dragRow >= 0 ? TimelineView.laneHeight(project.tracks[dragRow]) : laneH;
        final trimming = c.id == trimClip;
        final start = c.start + (dragging ? dragDelta : 0) + (trimming && trimStart ? trimDelta : 0);
        final end = c.end + (dragging ? dragDelta : 0) + (trimming && !trimStart ? trimDelta : 0);
        final x = (start - scroll) * pxPerSec + 1;
        final w = (end - start) * pxPerSec - 2;
        if (x > size.width || x + w < 0) continue; // cull off-screen clips
        final r = RRect.fromRectAndRadius(Rect.fromLTWH(x, top, w < 3 ? 3 : w, h), const Radius.circular(6));
        final fill = switch (c.kind) {
          'text' || 'subtitle' => _textFill,
          'sticker' => _stickerFill,
          'overlay' => _overlayFill,
          _ => audio ? VsColors.clipAudio2 : VsColors.clipVideo2,
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
        final isSel = c.id == selected;
        if (isSel || c.id == underHead || ((dragging || trimming) && snapped)) {
          canvas.drawRRect(
              r,
              Paint()
                ..style = PaintingStyle.stroke
                ..strokeWidth = isSel ? 2.5 : 1.5
                ..color = (dragging || trimming) && snapped
                    ? const Color(0xFFFFD65A)
                    : isSel
                        ? VsColors.fg
                        : const Color(0x99FFFFFF));
        }
        // Labels on slim lanes and sound; video lanes show their pictures.
        if (w > 30 && (c.generated || audio)) {
          final tp = TextPainter(
            text: TextSpan(
                text: c.name,
                style: const TextStyle(
                    color: VsColors.fg, fontSize: 11, shadows: [Shadow(color: Colors.black, blurRadius: 3)])),
            textDirection: TextDirection.ltr,
            maxLines: 1,
            ellipsis: '…',
          )..layout(maxWidth: w - 10);
          tp.paint(canvas, Offset(x + 6, top + (audio ? 2 : (h - tp.height) / 2)));
        }
        if (!dragging && !trimming) _keyframes(canvas, c, r.outerRect, isSel);
        // Trim handles on the selected clip of an unlocked track.
        if (isSel && !t.locked && w > 12) {
          final hp = Paint()..color = (trimming && snapped) ? const Color(0xFFFFD65A) : VsColors.fg;
          for (final hx in [x, x + w - 8]) {
            canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromLTWH(hx, top, 8, h), const Radius.circular(3)), hp);
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
        canvas.clipRect(Rect.fromLTWH(0, tops[i], size.width, laneH));
        for (var x = -laneH; x < size.width; x += 10) {
          canvas.drawLine(Offset(x, tops[i] + laneH), Offset(x + laneH, tops[i]), hatch);
        }
        canvas.restore();
      }
    }
    // "Đoạn áp dụng": a lit band between the two points (to the playhead while choosing the end).
    if (range != null) {
      final a = range!.$1, b = range!.$2 ?? playhead;
      final l = (a < b ? a : b), r = (a < b ? b : a);
      final rect = Rect.fromLTRB((l - scroll) * pxPerSec, TimelineView.rulerH, (r - scroll) * pxPerSec, size.height);
      canvas.drawRect(rect, Paint()..color = VsColors.accent.withValues(alpha: range!.$2 == null ? 0.12 : 0.22));
      final edge = Paint()
        ..color = VsColors.accent
        ..strokeWidth = 3;
      for (final (x, open) in [(rect.left, true), if (range!.$2 != null) (rect.right, false)]) {
        canvas.drawLine(Offset(x, TimelineView.rulerH), Offset(x, size.height), edge);
        final dx = open ? 6.0 : -6.0;
        canvas.drawLine(Offset(x, TimelineView.rulerH), Offset(x + dx, TimelineView.rulerH), edge);
        canvas.drawLine(Offset(x, size.height - 1), Offset(x + dx, size.height - 1), edge);
      }
    }
    // The fixed playhead, in the middle.
    final px = (playhead - scroll) * pxPerSec;
    canvas.drawLine(
        Offset(px, 0),
        Offset(px, size.height),
        Paint()
          ..color = VsColors.fg
          ..strokeWidth = 2);
  }

  // Transitions: a band where one plays and a badge under each cut ("+" where none is set yet).
  void _cuts(Canvas canvas, int row, TrackView t, Size size) {
    final list = cuts?[t.id];
    if (list == null || t.locked) return;
    final lane = Rect.fromLTWH(0, tops[row], size.width, TimelineView.laneHeight(t));
    for (final c in list) {
      if (!cutVisible(c, selected)) continue;
      final b = cutBadge(lane, c, scroll, pxPerSec);
      if (b.right < -20 || b.left > size.width + 20) continue;
      final isSel = selectedCut != null && selectedCut!.clipId == c.clipId && selectedCut!.edge == c.edge;
      if (c.type != null && c.duration > 0) {
        final (s, e) = c.window;
        canvas.drawRect(Rect.fromLTWH((s - scroll) * pxPerSec, lane.bottom - 5, (e - s) * pxPerSec, 5),
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

  // Thumbnail tiles across the visible part of a video clip, filling its whole height.
  void _thumbnails(Canvas canvas, ClipView c, Rect r, double viewW) {
    final t = media!.thumbs(c.assetId);
    if (t == null || t.count == 0) return;
    final th = r.height, tw = th * t.width / t.height;
    if (tw < 4) return;
    final asset = project.assets.where((a) => a.id == c.assetId).firstOrNull;
    final total = asset?.duration ?? c.duration; // the strip spans the whole asset
    final paint = Paint()..filterQuality = FilterQuality.medium;
    final x0 = r.left < 0 ? r.left + ((-r.left) ~/ tw) * tw : r.left;
    for (var x = x0; x < r.right && x < viewW; x += tw) {
      final src = c.sourceIn + (x + tw / 2 - r.left) / pxPerSec * c.speed;
      final i = total > 0 ? (src / total * t.count).floor().clamp(0, t.count - 1) : 0;
      canvas.drawImageRect(t.image, Rect.fromLTWH(0, i * t.height.toDouble(), t.width.toDouble(), t.height.toDouble()),
          Rect.fromLTWH(x, r.top, tw, th), paint);
      // A thin line between tiles, as on a film strip.
      canvas.drawLine(Offset(x, r.top), Offset(x, r.bottom), Paint()..color = const Color(0x55000000));
    }
  }

  // Peak envelope, one vertical line per visible pixel column; quiet media scaled up (max 8x).
  void _waveform(Canvas canvas, ClipView c, Rect r, double viewW) {
    final w = media!.waveform(c.assetId);
    if (w == null || w.buckets == 0) return;
    final mid = r.center.dy + 4;
    final half = (r.height - 12) / 2 / (w.loudest < 16 ? 16 : w.loudest) * 127;
    final paint = Paint()
      ..color = const Color(0xC8D2FFF8)
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
      old.range != range ||
      old.media != media;
}

/// Cuts between two clips always show a badge; a clip's free start/end only when a
/// transition is set there or the clip is selected (to fade it in or out).
bool cutVisible(CutPoint c, String? selectedClip) => c.joined || c.type != null || c.clipId == selectedClip;

/// Where a cut's badge sits on its [lane]: on the cut, or just inside a free edge.
Rect cutBadge(Rect lane, CutPoint c, double scroll, double pxPerSec) {
  const w = 22.0, h = 18.0;
  final x = (c.at - scroll) * pxPerSec + (c.joined ? 0 : (c.edge == 0 ? w / 2 + 3 : -w / 2 - 3));
  return Rect.fromLTWH(x - w / 2, lane.bottom - h - 4, w, h);
}
