// "Chỉnh màu" on the phone: pro grading for the selected clip, in a panel under
// the preview so the picture stays visible. Four pages: colour wheels (one at a
// time, with its brightness), curves, HSL mixer and a colour-range qualifier
// (tap "Lấy màu" then the picture). While a finger moves, the change is shown
// live on the preview ([GradePanel.onLive], no undo step); lifting the finger
// commits one undo step ([GradePanel.onCommit]). Same effect and maths as desktop.
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

typedef GradeParams = Map<String, dynamic>;

/// Deep copy through JSON-like maps and lists (params are plain JSON).
dynamic _copy(dynamic v) {
  if (v is Map) return <String, dynamic>{for (final e in v.entries) e.key as String: _copy(e.value)};
  if (v is List) return [for (final e in v) _copy(e)];
  return v;
}

double _num(dynamic v, double def) => v is num ? v.toDouble() : def;

class GradePanel extends StatefulWidget {
  const GradePanel({
    super.key,
    required this.vs,
    required this.clipId,
    required this.params,
    required this.onLive,
    required this.onCommit,
    required this.onApplyAll,
    required this.onDone,
    this.onPickMode,
  });

  final VStudio vs;
  final String clipId;
  final GradeParams params; // the clip's committed grade ({} = none)
  final ValueChanged<GradeParams?> onLive; // null = show the clip without its grade (compare)
  final ValueChanged<GradeParams> onCommit; // {} removes the grade
  final ValueChanged<GradeParams> onApplyAll;
  final VoidCallback onDone;
  final ValueChanged<bool>? onPickMode; // eyedropper: the next tap on the preview picks a colour

  @override
  State<GradePanel> createState() => GradePanelState();
}

class GradePanelState extends State<GradePanel> {
  static const wheelKeys = ['lift', 'gamma', 'gain', 'offset'];
  static const wheelNames = ['Vùng tối', 'Trung tính', 'Vùng sáng', 'Tổng thể'];
  static const curveKeys = ['master', 'r', 'g', 'b'];
  static const curveNames = ['Tổng', 'Đỏ', 'Lục', 'Lam'];
  static const curveColors = [Color(0xFFE6E6E6), Color(0xFFEB5A50), Color(0xFF5AC86E), Color(0xFF5A8CF0)];

  late GradeParams _p = _copy(widget.params) as GradeParams;
  int _page = 0, _wheel = 0, _curve = 0, _band = 1; // orange (skin) first
  bool _showMask = false, _picking = false;
  late final List<HslBandInfo> _bands = widget.vs.hslBands();

  @override
  void didUpdateWidget(GradePanel oldWidget) {
    super.didUpdateWidget(oldWidget);
    // Undo, another clip, or the commit coming back: follow the project.
    if (oldWidget.clipId != widget.clipId || !_sameJson(oldWidget.params, widget.params)) {
      _p = _copy(widget.params) as GradeParams;
    }
  }

  static bool _sameJson(dynamic a, dynamic b) => a.toString() == b.toString();

  GradeParams get _liveParams {
    final p = _copy(_p) as GradeParams;
    if (_showMask && p['qualifier'] is Map) (p['qualifier'] as Map)['show_mask'] = true;
    return p;
  }

  void _live() => widget.onLive(_liveParams);

  void _commit() {
    _clean();
    widget.onCommit(_copy(_p) as GradeParams);
    if (_showMask) _live();
  }

  /// Drops neutral entries so a reset control really removes its part of the grade.
  void _clean() {
    for (final k in wheelKeys) {
      final w = _p[k];
      if (w is List && w.every((v) => (v as num).abs() < 1e-6)) _p.remove(k);
    }
    if (_num(_p['contrast'], 1) == 1) _p.remove('contrast');
    if (_num(_p['saturation'], 1) == 1) _p.remove('saturation');
    final hsl = _p['hsl'];
    if (hsl is Map) {
      hsl.removeWhere((_, v) => v is List && v.every((x) => (x as num).abs() < 1e-6));
      if (hsl.isEmpty) _p.remove('hsl');
    }
    final curves = _p['curves'];
    if (curves is Map) {
      curves.removeWhere((_, v) => v is List && v.length == 2 && _identity(v));
      if (curves.isEmpty) _p.remove('curves');
    }
  }

  static bool _identity(List pts) {
    final a = pts[0] as List, b = pts[1] as List;
    return _num(a[0], -1) == 0 && _num(a[1], -1) == 0 && _num(b[0], -1) == 1 && _num(b[1], -1) == 1;
  }

  /// Eyedropper result (0..1 RGB from the preview): select that colour in the qualifier.
  void pickColor(Color c) {
    final hsv = HSVColor.fromColor(c);
    final r = c.r, g = c.g, b = c.b;
    final luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    setState(() {
      _picking = false;
      _page = 3;
      _p['qualifier'] = {
        ...((_p['qualifier'] as Map?)?.cast<String, dynamic>() ?? const <String, dynamic>{}),
        'enabled': true,
        'hue': [hsv.hue, 36.0],
        'sat': [math.max(0.0, hsv.saturation - 0.3), 1.0],
        'lum': [math.max(0.0, luma - 0.35), math.min(1.0, luma + 0.35)],
        'softness': _num((_p['qualifier'] as Map?)?['softness'], 0.1),
      };
    });
    widget.onPickMode?.call(false);
    _commit();
  }

  // ---------------------------------------------------------------- pages

  Widget _chips(List<String> names, int selected, ValueChanged<int> onTap, {List<Color>? colors}) => SizedBox(
        height: 34,
        child: ListView(scrollDirection: Axis.horizontal, children: [
          for (var i = 0; i < names.length; i++)
            Padding(
              padding: const EdgeInsets.only(right: 6),
              child: ChoiceChip(
                label: Text(names[i], style: const TextStyle(fontSize: 12)),
                selected: i == selected,
                avatar: colors == null ? null : CircleAvatar(backgroundColor: colors[i], radius: 6),
                visualDensity: VisualDensity.compact,
                onSelected: (_) => setState(() => onTap(i)),
              ),
            ),
        ]),
      );

  Widget _slider(String label, double value, double min, double max, ValueChanged<double> set,
          {double? reset, String Function(double)? show}) =>
      Row(children: [
        GestureDetector(
          onDoubleTap: reset == null
              ? null
              : () {
                  setState(() => set(reset));
                  _commit();
                },
          child: SizedBox(width: 84, child: Text(label, style: const TextStyle(fontSize: 12, color: VsColors.fg2))),
        ),
        Expanded(
          child: Slider(
            value: value.clamp(min, max),
            min: min,
            max: max,
            onChanged: (v) {
              setState(() => set(v));
              _live();
            },
            onChangeEnd: (_) => _commit(),
          ),
        ),
        SizedBox(
            width: 38,
            child: Text(show != null ? show(value) : (value * 100).round().toString(),
                textAlign: TextAlign.right, style: const TextStyle(fontSize: 11, color: VsColors.fg3))),
      ]);

  Widget _wheelsPage() {
    final key = wheelKeys[_wheel];
    final w = (_p[key] as List?)?.map((v) => (v as num).toDouble()).toList() ?? [0.0, 0.0, 0.0, 0.0];
    final (angle, dist) = widget.vs.balanceToWheel(w.sublist(0, 3));
    void setWheel(List<double> balance, double master) => _p[key] = [...balance, master];
    return Column(children: [
      _chips(wheelNames, _wheel, (i) => _wheel = i),
      const SizedBox(height: 4),
      Expanded(
        child: Row(children: [
          AspectRatio(
            aspectRatio: 1,
            child: ColorWheelView(
              angle: angle,
              distance: dist,
              onChanged: (a, d) {
                setState(() => setWheel(widget.vs.wheelToBalance(a, d), w[3]));
                _live();
              },
              onEnd: _commit,
              onReset: () {
                setState(() => setWheel([0, 0, 0], w[3]));
                _commit();
              },
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
              _slider('Độ sáng', w[3], -1, 1, (v) => setWheel(w.sublist(0, 3), v), reset: 0),
              _slider('Tương phản', _num(_p['contrast'], 1), 0, 2, (v) => _p['contrast'] = v, reset: 1),
              _slider('Bão hoà', _num(_p['saturation'], 1), 0, 2, (v) => _p['saturation'] = v, reset: 1),
            ]),
          ),
        ]),
      ),
    ]);
  }

  List<List<double>> _curvePoints() {
    final c = (_p['curves'] as Map?)?[curveKeys[_curve]] as List?;
    final pts = <List<double>>[
      for (final p in c ?? const []) [(p as List)[0], p[1]].map((v) => (v as num).toDouble()).toList()
    ];
    if (pts.isEmpty || pts.first[0] > 0) pts.insert(0, [0, 0]);
    if (pts.last[0] < 1) pts.add([1, 1]);
    return pts;
  }

  Widget _curvesPage() => Column(children: [
        _chips(curveNames, _curve, (i) => _curve = i, colors: curveColors),
        const SizedBox(height: 4),
        Expanded(
          child: Row(children: [
            AspectRatio(
              aspectRatio: 1,
              child: CurveView(
                vs: widget.vs,
                points: _curvePoints(),
                color: curveColors[_curve],
                onChanged: (pts) {
                  setState(() => (_p['curves'] ??= <String, dynamic>{})[curveKeys[_curve]] = pts);
                  _live();
                },
                onEnd: _commit,
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
                const Text('Chạm để thêm điểm, kéo để chỉnh, giữ lâu để xoá điểm.',
                    style: TextStyle(fontSize: 11, color: VsColors.fg3)),
                TextButton(
                  onPressed: () {
                    setState(() => (_p['curves'] as Map?)?.remove(curveKeys[_curve]));
                    _commit();
                  },
                  child: const Text('Đặt lại đường cong'),
                ),
              ]),
            ),
          ]),
        ),
      ]);

  Widget _hslPage() {
    final band = _bands[_band];
    final v = ((_p['hsl'] as Map?)?[band.id] as List?)?.map((x) => (x as num).toDouble()).toList() ?? [0.0, 0.0, 0.0];
    void set(int i, double x) {
      final hsl = (_p['hsl'] ??= <String, dynamic>{}) as Map;
      final cur = [...v];
      cur[i] = x;
      hsl[band.id] = cur;
    }

    return Column(children: [
      _chips([for (final b in _bands) b.name], _band, (i) => _band = i,
          colors: [for (final b in _bands) HSVColor.fromAHSV(1, b.hue, 0.8, 0.9).toColor()]),
      _slider('Sắc độ', v[0], -1, 1, (x) => set(0, x), reset: 0),
      _slider('Bão hoà', v[1], -1, 1, (x) => set(1, x), reset: 0),
      _slider('Độ sáng', v[2], -1, 1, (x) => set(2, x), reset: 0),
    ]);
  }

  Widget _qualifierPage() {
    final q = (_p['qualifier'] as Map?)?.cast<String, dynamic>();
    final on = q != null && (q['enabled'] ?? true) == true;
    List<double> pair(String k, double a, double b) =>
        (q?[k] as List?)?.map((x) => (x as num).toDouble()).toList() ?? [a, b];
    final hue = pair('hue', 30, 40), sat = pair('sat', 0.15, 1), lum = pair('lum', 0, 1);
    Map<String, dynamic> qual() => (_p['qualifier'] ??= <String, dynamic>{'enabled': true}) as Map<String, dynamic>;
    Widget range(String label, List<double> v, String key) => Row(children: [
          SizedBox(width: 84, child: Text(label, style: const TextStyle(fontSize: 12, color: VsColors.fg2))),
          Expanded(
            child: RangeSlider(
              values: RangeValues(v[0].clamp(0, 1), v[1].clamp(0, 1)),
              onChanged: (r) {
                setState(() => qual()[key] = [r.start, r.end]);
                _live();
              },
              onChangeEnd: (_) => _commit(),
            ),
          ),
        ]);
    return ListView(padding: EdgeInsets.zero, children: [
      Row(children: [
        Switch(
          value: on,
          onChanged: (v) {
            setState(() {
              if (v) {
                qual()['enabled'] = true;
              } else {
                _p.remove('qualifier');
              }
            });
            _commit();
          },
        ),
        const Expanded(child: Text('Chọn vùng màu', style: TextStyle(fontSize: 13), overflow: TextOverflow.ellipsis)),
        FilledButton.tonalIcon(
          icon: const Icon(Icons.colorize, size: 18),
          label: Text(_picking ? 'Chạm vào hình…' : 'Lấy màu'),
          onPressed: () {
            setState(() => _picking = !_picking);
            widget.onPickMode?.call(_picking);
          },
        ),
      ]),
      if (on) ...[
        _slider('Màu', hue[0], 0, 359, (x) => qual()['hue'] = [x, hue[1]], show: (x) => '${x.round()}°'),
        _slider('Độ rộng', hue[1], 5, 360, (x) => qual()['hue'] = [hue[0], x], show: (x) => '${x.round()}°'),
        range('Bão hoà', sat, 'sat'),
        range('Độ sáng', lum, 'lum'),
        _slider('Độ mềm', _num(q['softness'], 0.1), 0, 0.5, (x) => qual()['softness'] = x),
        const Divider(height: 8),
        _slider('Sắc độ', _num(q['hue_shift'], 0), -1, 1, (x) => qual()['hue_shift'] = x, reset: 0),
        _slider('Bão hoà', _num(q['saturation'], 0), -1, 1, (x) => qual()['saturation'] = x, reset: 0),
        _slider('Độ sáng', _num(q['exposure'], 0), -1, 1, (x) => qual()['exposure'] = x, reset: 0),
        Wrap(crossAxisAlignment: WrapCrossAlignment.center, children: [
          Row(mainAxisSize: MainAxisSize.min, children: [
            Checkbox(
              value: q['invert'] == true,
              onChanged: (v) {
                setState(() => qual()['invert'] = v ?? false);
                _commit();
              },
            ),
            const Text('Đảo vùng chọn', style: TextStyle(fontSize: 12)),
          ]),
          Row(mainAxisSize: MainAxisSize.min, children: [
            Checkbox(
              value: _showMask,
              onChanged: (v) {
                setState(() => _showMask = v ?? false);
                _showMask ? _live() : widget.onCommit(_copy(_p) as GradeParams);
              },
            ),
            const Text('Hiện vùng chọn', style: TextStyle(fontSize: 12)),
          ]),
        ]),
      ] else
        const Padding(
          padding: EdgeInsets.all(8),
          child: Text('Chỉ chỉnh một màu (da, bầu trời, lá cây…): bấm "Lấy màu" rồi chạm vào màu đó trên khung hình.',
              style: TextStyle(fontSize: 12, color: VsColors.fg3)),
        ),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    final pages = [_wheelsPage, _curvesPage, _hslPage, _qualifierPage];
    return Container(
      color: VsColors.panel,
      height: 300,
      padding: const EdgeInsets.fromLTRB(10, 4, 10, 4),
      child: Column(children: [
        Row(children: [
          Expanded(
            child: SegmentedButton<int>(
              showSelectedIcon: false,
              style: const ButtonStyle(visualDensity: VisualDensity.compact),
              segments: const [
                ButtonSegment(value: 0, label: Text('Bánh xe', style: TextStyle(fontSize: 12))),
                ButtonSegment(value: 1, label: Text('Đường cong', style: TextStyle(fontSize: 12))),
                ButtonSegment(value: 2, label: Text('HSL', style: TextStyle(fontSize: 12))),
                ButtonSegment(value: 3, label: Text('Vùng màu', style: TextStyle(fontSize: 12))),
              ],
              selected: {_page},
              onSelectionChanged: (s) => setState(() => _page = s.first),
            ),
          ),
        ]),
        const SizedBox(height: 6),
        Expanded(child: pages[_page]()),
        Row(children: [
          Expanded(
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Row(children: [
                Listener(
                  onPointerDown: (_) => widget.onLive(null),
                  onPointerUp: (_) => widget.onLive(_liveParams),
                  child: TextButton(onPressed: () {}, child: const Text('Giữ: xem gốc')),
                ),
                TextButton(
                  onPressed: _p.isEmpty
                      ? null
                      : () {
                          setState(() => _p = <String, dynamic>{});
                          _commit();
                        },
                  child: const Text('Đặt lại'),
                ),
                TextButton(
                  onPressed: _p.isEmpty ? null : () => widget.onApplyAll(_copy(_p) as GradeParams),
                  child: const Text('Áp dụng tất cả'),
                ),
              ]),
            ),
          ),
          FilledButton(onPressed: widget.onDone, child: const Text('Xong')),
        ]),
      ]),
    );
  }
}

/// Colour wheel: drag the puck towards the colour to push; double-tap to centre.
class ColorWheelView extends StatelessWidget {
  const ColorWheelView(
      {super.key,
      required this.angle,
      required this.distance,
      required this.onChanged,
      required this.onEnd,
      required this.onReset});
  final double angle, distance; // degrees (0 = red, counter-clockwise), 0..1
  final void Function(double angle, double distance) onChanged;
  final VoidCallback onEnd, onReset;

  @override
  Widget build(BuildContext context) => LayoutBuilder(builder: (context, box) {
        final size = box.biggest.shortestSide;
        void at(Offset p) {
          final r = size / 2 - 8;
          final dx = (p.dx - size / 2) / r, dy = -(p.dy - size / 2) / r;
          var a = math.atan2(dy, dx) * 180 / math.pi;
          if (a < 0) a += 360;
          onChanged(a, math.min(1.0, math.sqrt(dx * dx + dy * dy)));
        }

        return GestureDetector(
          onPanStart: (d) => at(d.localPosition),
          onPanUpdate: (d) => at(d.localPosition),
          onPanEnd: (_) => onEnd(),
          onDoubleTap: onReset,
          child: CustomPaint(size: Size.square(size), painter: _WheelPainter(angle, distance)),
        );
      });
}

class _WheelPainter extends CustomPainter {
  _WheelPainter(this.angle, this.distance);
  final double angle, distance;

  @override
  void paint(Canvas canvas, Size size) {
    final c = size.center(Offset.zero);
    final r = size.shortestSide / 2 - 8;
    // Hue ring, counter-clockwise from 3 o'clock (screen y points down, so sweep negatively).
    final colors = [for (var i = 0; i <= 12; i++) HSVColor.fromAHSV(1, (360 - i * 30) % 360, 0.75, 0.8).toColor()];
    canvas.drawCircle(
        c, r, Paint()..shader = SweepGradient(colors: colors).createShader(Rect.fromCircle(center: c, radius: r)));
    canvas.drawCircle(
        c,
        r,
        Paint()
          ..shader = const RadialGradient(
              colors: [Color(0xFF282C34), Color(0x5A282C34), Color(0x00282C34)],
              stops: [0, 0.85, 1]).createShader(Rect.fromCircle(center: c, radius: r)));
    final line = Paint()
      ..color = VsColors.line
      ..style = PaintingStyle.stroke;
    canvas.drawCircle(c, r, line);
    canvas.drawLine(c.translate(-6, 0), c.translate(6, 0), line);
    canvas.drawLine(c.translate(0, -6), c.translate(0, 6), line);
    final a = angle * math.pi / 180;
    final puck = c + Offset(math.cos(a), -math.sin(a)) * distance * r;
    canvas.drawCircle(
        puck,
        9,
        Paint()
          ..color =
              distance > 0 ? HSVColor.fromAHSV(1, angle % 360, distance.clamp(0, 1), 1).toColor() : VsColors.raised);
    canvas.drawCircle(
        puck,
        9,
        Paint()
          ..color = Colors.white
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2);
  }

  @override
  bool shouldRepaint(_WheelPainter old) => old.angle != angle || old.distance != distance;
}

/// Curve editor: tap to add a point, drag to move, long-press a point to remove it.
class CurveView extends StatefulWidget {
  const CurveView(
      {super.key,
      required this.vs,
      required this.points,
      required this.color,
      required this.onChanged,
      required this.onEnd});
  final VStudio vs;
  final List<List<double>> points;
  final Color color;
  final ValueChanged<List<List<double>>> onChanged;
  final VoidCallback onEnd;

  @override
  State<CurveView> createState() => _CurveViewState();
}

class _CurveViewState extends State<CurveView> {
  int _drag = -1;

  @override
  Widget build(BuildContext context) => LayoutBuilder(builder: (context, box) {
        final s = box.biggest.shortestSide;
        const pad = 8.0;
        final side = s - 2 * pad;
        Offset toLocal(List<double> p) => Offset(pad + p[0] * side, pad + (1 - p[1]) * side);
        List<double> toCurve(Offset o) =>
            [((o.dx - pad) / side).clamp(0.0, 1.0), (1 - (o.dy - pad) / side).clamp(0.0, 1.0)];
        int hit(Offset o) {
          for (var i = 0; i < widget.points.length; i++) {
            if ((toLocal(widget.points[i]) - o).distance < 22) return i; // finger-sized
          }
          return -1;
        }

        List<List<double>> copy() => [
              for (final p in widget.points) [...p]
            ];
        return GestureDetector(
          onPanStart: (d) {
            _drag = hit(d.localPosition);
            if (_drag < 0) {
              final c = toCurve(d.localPosition);
              final pts = copy();
              var i = 0;
              while (i < pts.length && pts[i][0] < c[0]) {
                i++;
              }
              if (i == 0 || i == pts.length) return;
              pts.insert(i, c);
              _drag = i;
              widget.onChanged(pts);
            }
          },
          onPanUpdate: (d) {
            if (_drag < 0 || _drag >= widget.points.length) return;
            final pts = copy();
            final c = toCurve(d.localPosition);
            if (_drag == 0 || _drag == pts.length - 1) {
              c[0] = pts[_drag][0]; // ends move up and down only
            } else {
              c[0] = c[0].clamp(pts[_drag - 1][0] + 0.01, pts[_drag + 1][0] - 0.01);
            }
            pts[_drag] = c;
            widget.onChanged(pts);
          },
          onPanEnd: (_) {
            _drag = -1;
            widget.onEnd();
          },
          onTapUp: (d) {
            if (hit(d.localPosition) >= 0) return;
            final c = toCurve(d.localPosition);
            final pts = copy();
            var i = 0;
            while (i < pts.length && pts[i][0] < c[0]) {
              i++;
            }
            if (i == 0 || i == pts.length) return;
            pts.insert(i, c);
            widget.onChanged(pts);
            widget.onEnd();
          },
          onLongPressStart: (d) {
            final i = hit(d.localPosition);
            if (i <= 0 || i >= widget.points.length - 1) return;
            widget.onChanged(copy()..removeAt(i));
            widget.onEnd();
          },
          child: CustomPaint(
            size: Size.square(s),
            painter: _CurvePainter(widget.vs.gradeCurve(widget.points, 129), widget.points, widget.color, pad),
          ),
        );
      });
}

class _CurvePainter extends CustomPainter {
  _CurvePainter(this.samples, this.points, this.color, this.pad);
  final List<double> samples;
  final List<List<double>> points;
  final Color color;
  final double pad;

  @override
  void paint(Canvas canvas, Size size) {
    final side = size.shortestSide - 2 * pad;
    final area = Rect.fromLTWH(pad, pad, side, side);
    canvas.drawRect(area, Paint()..color = VsColors.panel2);
    final grid = Paint()..color = VsColors.line;
    for (var i = 1; i < 4; i++) {
      canvas.drawLine(Offset(pad + side * i / 4, pad), Offset(pad + side * i / 4, pad + side), grid);
      canvas.drawLine(Offset(pad, pad + side * i / 4), Offset(pad + side, pad + side * i / 4), grid);
    }
    canvas.drawLine(area.bottomLeft, area.topRight, grid);
    if (samples.isNotEmpty) {
      final path = Path();
      for (var i = 0; i < samples.length; i++) {
        final o = Offset(pad + side * i / (samples.length - 1), pad + side * (1 - samples[i]));
        i == 0 ? path.moveTo(o.dx, o.dy) : path.lineTo(o.dx, o.dy);
      }
      canvas.drawPath(
          path,
          Paint()
            ..color = color
            ..style = PaintingStyle.stroke
            ..strokeWidth = 2);
    }
    for (final p in points) {
      final o = Offset(pad + p[0] * side, pad + (1 - p[1]) * side);
      canvas.drawCircle(o, 6, Paint()..color = VsColors.raised);
      canvas.drawCircle(
          o,
          6,
          Paint()
            ..color = Colors.white
            ..style = PaintingStyle.stroke
            ..strokeWidth = 1.5);
    }
  }

  @override
  bool shouldRepaint(_CurvePainter old) => true;
}
