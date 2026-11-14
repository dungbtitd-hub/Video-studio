// Clip speed on the phone (FR-015): a constant speed 0.1x..10x, or a speed ramp: one of the
// ready-made curves or one drawn by hand (drag the points; tap empty space to add one,
// long-press a point to remove it). Changing the speed changes the clip's length, and the
// clips after it on the track move with it.
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

class SpeedSheet extends StatefulWidget {
  const SpeedSheet({super.key, required this.vs, required this.clipId, required this.onEdit});
  final VStudio vs;
  final String clipId;
  final void Function(void Function(VStudio vs) edit) onEdit;

  @override
  State<SpeedSheet> createState() => _SpeedSheetState();
}

class _SpeedSheetState extends State<SpeedSheet> {
  late final List<SpeedPreset> _presets = widget.vs.speedPresets();
  ClipSpeed? _info;
  double? _drag; // slider position (log10 speed) while held
  List<(double, double)> _points = const [];
  String _mode = 'normal'; // normal | preset id | custom
  String? _note;

  @override
  void initState() {
    super.initState();
    _load();
  }

  void _load() {
    try {
      final i = widget.vs.clipSpeed(widget.clipId);
      _info = i;
      _points = i.points;
      _mode = i.points.isEmpty ? 'normal' : (i.preset.isEmpty ? 'custom' : i.preset);
      _note = i.can ? null : 'Không đổi được tốc độ: ${i.why}';
    } on VStudioException catch (e) {
      _note = e.message;
    }
    if (mounted) setState(() {});
  }

  double get _speed => _info?.speed ?? 1;

  void _apply({double? speed, List<(double, double)>? points, String preset = ''}) {
    try {
      widget.onEdit((vs) => vs.setSpeed([widget.clipId], speed ?? _speed, points: points ?? const [], preset: preset));
    } on VStudioException catch (e) {
      setState(() => _note = e.message);
    }
    _load();
  }

  @override
  Widget build(BuildContext context) {
    final can = _info?.can ?? false;
    final pos = _drag ?? (math.log(_speed) / math.ln10);
    final shown = math.pow(10, pos).toDouble();
    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            const Text('Tốc độ', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
            const Spacer(),
            if (_info != null)
              Text('${shown.toStringAsFixed(2)}x · ${_info!.seconds.toStringAsFixed(1)} s',
                  style: const TextStyle(color: VsColors.fg2)),
          ]),
          if (_note != null) Text(_note!, style: const TextStyle(color: VsColors.fg2, fontSize: 12)),
          Slider(
            key: const Key('speed-slider'),
            min: -1,
            max: 1,
            divisions: 200,
            value: pos.clamp(-1, 1).toDouble(),
            onChanged: can ? (v) => setState(() => _drag = (v.abs() < 0.03) ? 0 : v) : null,
            onChangeEnd: can
                ? (v) {
                    final s = math.pow(10, (v.abs() < 0.03) ? 0 : v).toDouble();
                    setState(() => _drag = null);
                    _apply(speed: s, points: _mode == 'normal' ? const [] : _points, preset: _preset);
                  }
                : null,
          ),
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 16),
            child: Row(mainAxisAlignment: MainAxisAlignment.spaceBetween, children: [
              Text('0.1x', style: TextStyle(fontSize: 11, color: VsColors.fg2)),
              Text('1x', style: TextStyle(fontSize: 11, color: VsColors.fg2)),
              Text('10x', style: TextStyle(fontSize: 11, color: VsColors.fg2)),
            ]),
          ),
          const SizedBox(height: 8),
          const Text('Đường cong tốc độ', style: TextStyle(fontWeight: FontWeight.w600)),
          const SizedBox(height: 4),
          Wrap(spacing: 6, runSpacing: 4, children: [
            ChoiceChip(
              key: const Key('speed-normal'),
              label: const Text('Không'),
              selected: _mode == 'normal',
              onSelected: can ? (_) => _apply(points: const []) : null,
            ),
            for (final p in _presets)
              ChoiceChip(
                key: Key('speed-${p.id}'),
                label: Text(p.name),
                selected: _mode == p.id,
                onSelected: can ? (_) => _apply(points: p.points, preset: p.id) : null,
              ),
            ChoiceChip(
              key: const Key('speed-custom'),
              label: const Text('Tự vẽ'),
              selected: _mode == 'custom',
              onSelected: can
                  ? (_) => _apply(points: _points.length >= 2 ? _points : const [(0.0, 1.0), (0.5, 1.0), (1.0, 1.0)])
                  : null,
            ),
          ]),
          if (_mode != 'normal') ...[
            const SizedBox(height: 8),
            SizedBox(
              height: 150,
              child: _CurveEditor(
                points: _points,
                onChanged: (p) => setState(() => _points = p),
                onDone: (p) => _apply(points: p),
              ),
            ),
            const Text('Kéo điểm để đổi tốc độ · chạm chỗ trống để thêm điểm · giữ lâu một điểm để xóa',
                style: TextStyle(fontSize: 11, color: VsColors.fg2)),
          ],
        ]),
      ),
    );
  }

  String get _preset => _mode == 'normal' || _mode == 'custom' ? '' : _mode;
}

/// Points (u 0..1 along the clip, speed) on a log axis 0.1x..10x.
class _CurveEditor extends StatefulWidget {
  const _CurveEditor({required this.points, required this.onChanged, required this.onDone});
  final List<(double, double)> points;
  final ValueChanged<List<(double, double)>> onChanged, onDone;

  @override
  State<_CurveEditor> createState() => _CurveEditorState();
}

class _CurveEditorState extends State<_CurveEditor> {
  int _drag = -1;
  static const _left = 34.0, _pad = 8.0;

  List<(double, double)> get _pts =>
      widget.points.length >= 2 ? widget.points : const [(0.0, 1.0), (1.0, 1.0)];

  Rect _plot(Size s) => Rect.fromLTRB(_left, _pad, s.width - _pad, s.height - _pad);
  Offset _toPx(Size s, (double, double) p) {
    final r = _plot(s);
    final y = math.log(p.$2.clamp(0.1, 10)) / math.ln10;
    return Offset(r.left + p.$1 * r.width, r.center.dy - y * r.height / 2);
  }

  (double, double) _fromPx(Size s, Offset o) {
    final r = _plot(s);
    final u = ((o.dx - r.left) / r.width).clamp(0.0, 1.0);
    final y = ((r.center.dy - o.dy) / (r.height / 2)).clamp(-1.0, 1.0);
    var v = math.pow(10, y).toDouble();
    if ((v - 1).abs() < 0.06) v = 1;
    return (u, (v * 100).roundToDouble() / 100);
  }

  int _hit(Size s, Offset o) {
    final p = _pts;
    for (var i = 0; i < p.length; i++) {
      if ((_toPx(s, p[i]) - o).distance < 18) return i;
    }
    return -1;
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, box) {
      final size = box.biggest;
      return GestureDetector(
        onPanStart: (d) => _drag = _hit(size, d.localPosition),
        onPanUpdate: (d) {
          if (_drag < 0) return;
          final p = [..._pts];
          var q = _fromPx(size, d.localPosition);
          if (_drag == 0) {
            q = (0, q.$2);
          } else if (_drag == p.length - 1) {
            q = (1, q.$2);
          } else {
            q = (q.$1.clamp(p[_drag - 1].$1 + 0.01, p[_drag + 1].$1 - 0.01).toDouble(), q.$2);
          }
          p[_drag] = q;
          widget.onChanged(p);
        },
        onPanEnd: (_) {
          if (_drag >= 0) widget.onDone(_pts);
          _drag = -1;
        },
        onTapUp: (d) {
          if (_hit(size, d.localPosition) >= 0) return;
          final q = _fromPx(size, d.localPosition);
          final p = [..._pts, q]..sort((a, b) => a.$1.compareTo(b.$1));
          widget.onDone(p);
        },
        onLongPressStart: (d) {
          final i = _hit(size, d.localPosition);
          if (i <= 0 || i >= _pts.length - 1) return;
          widget.onDone([..._pts]..removeAt(i));
        },
        child: CustomPaint(size: size, painter: _CurvePainter(_pts, _toPx)),
      );
    });
  }
}

class _CurvePainter extends CustomPainter {
  _CurvePainter(this.points, this.toPx);
  final List<(double, double)> points;
  final Offset Function(Size, (double, double)) toPx;

  @override
  void paint(Canvas canvas, Size size) {
    final r = Rect.fromLTRB(34, 8, size.width - 8, size.height - 8);
    canvas.drawRect(r, Paint()..color = const Color(0xFF1E2128));
    final grid = Paint()
      ..color = const Color(0xFF464C58)
      ..strokeWidth = 1;
    for (final v in [0.1, 0.5, 1.0, 2.0, 10.0]) {
      final y = r.center.dy - (math.log(v) / math.ln10) * r.height / 2;
      canvas.drawLine(Offset(r.left, y), Offset(r.right, y), grid..strokeWidth = v == 1.0 ? 1.5 : 0.8);
      final tp = TextPainter(
        text: TextSpan(text: '${v}x', style: const TextStyle(fontSize: 9, color: VsColors.fg2)),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(r.left - tp.width - 4, y - tp.height / 2));
    }
    final path = Path();
    for (var i = 0; i < points.length; i++) {
      final o = toPx(size, points[i]);
      i == 0 ? path.moveTo(o.dx, o.dy) : path.lineTo(o.dx, o.dy);
    }
    canvas.drawPath(
        path,
        Paint()
          ..color = VsColors.accent
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2);
    for (final p in points) {
      canvas.drawCircle(toPx(size, p), 6, Paint()..color = VsColors.accent);
    }
  }

  @override
  bool shouldRepaint(_CurvePainter old) => old.points != points;
}
