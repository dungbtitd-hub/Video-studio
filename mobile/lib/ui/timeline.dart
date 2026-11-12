import 'package:flutter/material.dart';

import '../core/project_model.dart';
import 'theme.dart';

/// Touch timeline. Gestures only change what is drawn; the core is edited
/// once when a gesture ends (one command, one undo step), so dragging stays
/// smooth however large the project is.
class TimelineView extends StatefulWidget {
  const TimelineView({
    super.key,
    required this.project,
    required this.playhead,
    required this.selectedClip,
    required this.onSeek,
    required this.onSelect,
    required this.onMove,
  });

  final ProjectView project;
  final double playhead;
  final String? selectedClip;
  final ValueChanged<double> onSeek;
  final ValueChanged<String?> onSelect;
  final void Function(String clipId, String trackId, double start) onMove;

  @override
  State<TimelineView> createState() => _TimelineViewState();
}

class _TimelineViewState extends State<TimelineView> {
  static const trackH = 56.0;
  static const rulerH = 22.0;
  double pxPerSec = 40;
  double scroll = 0; // seconds at the left edge
  double _zoomStart = 40;

  // Clip drag (visual only until release)
  String? _dragClip;
  String? _dragTrack;
  double _dragDelta = 0;

  (TrackView, ClipView)? _hit(Offset p) {
    final row = ((p.dy - rulerH) / trackH).floor();
    if (row < 0 || row >= widget.project.tracks.length) return null;
    final t = scroll + p.dx / pxPerSec;
    final track = widget.project.tracks[row];
    for (final c in track.clips) {
      if (t >= c.start && t < c.end) return (track, c);
    }
    return null;
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, box) {
      return GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTapUp: (d) {
          final h = _hit(d.localPosition);
          widget.onSelect(h?.$2.id);
          if (h == null) widget.onSeek(scroll + d.localPosition.dx / pxPerSec);
        },
        onLongPressStart: (d) {
          final h = _hit(d.localPosition);
          if (h == null) return;
          setState(() {
            _dragClip = h.$2.id;
            _dragTrack = h.$1.id;
            _dragDelta = 0;
          });
          widget.onSelect(h.$2.id);
        },
        onLongPressMoveUpdate: (d) => setState(() => _dragDelta = d.offsetFromOrigin.dx / pxPerSec),
        onLongPressEnd: (_) {
          final id = _dragClip, track = _dragTrack;
          if (id != null && track != null && _dragDelta.abs() > 0.02) {
            final clip = widget.project.tracks.expand((t) => t.clips).firstWhere((c) => c.id == id);
            widget.onMove(id, track, (clip.start + _dragDelta).clamp(0, double.infinity).toDouble());
          }
          setState(() => _dragClip = null);
        },
        onScaleStart: (_) => _zoomStart = pxPerSec,
        onScaleUpdate: (d) => setState(() {
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
          ),
        ),
      );
    });
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
  });

  final ProjectView project;
  final double playhead;
  final String? selected;
  final double pxPerSec;
  final double scroll;
  final String? dragClip;
  final double dragDelta;

  @override
  void paint(Canvas canvas, Size size) {
    const trackH = _TimelineViewState.trackH, rulerH = _TimelineViewState.rulerH;
    canvas.drawRect(Offset.zero & size, Paint()..color = VsColors.panel);
    final tick = Paint()..color = VsColors.fg3;
    final step = pxPerSec >= 30 ? 1 : (pxPerSec >= 8 ? 5 : 15);
    for (var s = (scroll ~/ step) * step; (s - scroll) * pxPerSec < size.width; s += step) {
      final x = (s - scroll) * pxPerSec;
      canvas.drawLine(Offset(x, rulerH - (s % (step * 5) == 0 ? 10 : 5)), Offset(x, rulerH), tick);
      if (s % (step * 5) == 0) {
        final tp = TextPainter(
          text: TextSpan(text: '${s ~/ 60}:${(s % 60).toString().padLeft(2, '0')}',
              style: const TextStyle(color: VsColors.fg3, fontSize: 10)),
          textDirection: TextDirection.ltr,
        )..layout();
        tp.paint(canvas, Offset(x + 3, 2));
      }
    }
    for (var i = 0; i < project.tracks.length; i++) {
      final t = project.tracks[i];
      final top = rulerH + i * trackH;
      canvas.drawRect(Rect.fromLTWH(0, top, size.width, trackH - 2), Paint()..color = VsColors.panel2);
      final audio = t.type == 'audio';
      for (final c in t.clips) {
        final start = c.start + (c.id == dragClip ? dragDelta : 0);
        final x = (start - scroll) * pxPerSec;
        final w = c.duration * pxPerSec;
        if (x > size.width || x + w < 0) continue; // cull off-screen clips
        final r = RRect.fromRectAndRadius(Rect.fromLTWH(x, top + 4, w < 3 ? 3 : w, trackH - 10), const Radius.circular(4));
        canvas.drawRRect(r, Paint()..color = audio ? VsColors.clipAudio2 : VsColors.clipVideo2);
        canvas.drawRRect(
            r,
            Paint()
              ..style = PaintingStyle.stroke
              ..strokeWidth = c.id == selected ? 2 : 1
              ..color = c.id == selected ? VsColors.accent : (audio ? VsColors.clipAudio : VsColors.clipVideo));
        if (w > 30) {
          final tp = TextPainter(
            text: TextSpan(text: c.name, style: const TextStyle(color: VsColors.fg, fontSize: 11)),
            textDirection: TextDirection.ltr,
            maxLines: 1,
            ellipsis: '…',
          )..layout(maxWidth: w - 10);
          tp.paint(canvas, Offset(x + 6, top + 8));
        }
      }
    }
    final px = (playhead - scroll) * pxPerSec;
    canvas.drawLine(Offset(px, 0), Offset(px, size.height), Paint()
      ..color = VsColors.accent
      ..strokeWidth = 2);
  }

  @override
  bool shouldRepaint(_TimelinePainter old) =>
      old.project != project ||
      old.playhead != playhead ||
      old.selected != selected ||
      old.pxPerSec != pxPerSec ||
      old.scroll != scroll ||
      old.dragClip != dragClip ||
      old.dragDelta != dragDelta;
}
