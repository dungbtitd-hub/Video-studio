// Covering part of the picture on the phone: pick a style (thumbnails), a
// shape and a strength in the panel under the preview; drag the region on the
// preview with one finger (or its corner to resize, two fingers to scale).
// "Bám theo" follows a moving subject in the background.
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

/// The active cover on the preview. Moves locally while the finger is down;
/// [onChanged] (canvas 0..1) once on release, so one drag is one undo step.
class CoverOverlay extends StatefulWidget {
  const CoverOverlay(
      {super.key, required this.canvasSize, required this.rect, required this.shape, required this.onChanged});
  final Size canvasSize;
  final Rect rect; // canvas 0..1
  final String shape;
  final ValueChanged<Rect> onChanged;

  @override
  State<CoverOverlay> createState() => _CoverOverlayState();
}

enum _Drag { none, move, corner, scale }

class _CoverOverlayState extends State<CoverOverlay> {
  Rect? _live; // canvas 0..1 while dragging
  _Drag _drag = _Drag.none;
  Rect _start = Rect.zero;
  Offset _from = Offset.zero;
  static const handle = 28.0; // finger-sized corner handle

  Rect _canvas(Size box) {
    final c = widget.canvasSize;
    final k = math.min(box.width / c.width, box.height / c.height);
    return Rect.fromCenter(center: box.center(Offset.zero), width: c.width * k, height: c.height * k);
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(builder: (context, box) {
        final cv = _canvas(box.biggest);
        Rect toLocal(Rect n) => Rect.fromLTWH(
            cv.left + n.left * cv.width, cv.top + n.top * cv.height, n.width * cv.width, n.height * cv.height);
        Rect toNorm(Rect l) => Rect.fromLTWH(
            (l.left - cv.left) / cv.width, (l.top - cv.top) / cv.height, l.width / cv.width, l.height / cv.height);
        final r = toLocal(_live ?? widget.rect);
        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onScaleStart: (d) {
            _start = r;
            _from = d.localFocalPoint;
            final nearCorner = (d.localFocalPoint - r.bottomRight).distance < handle;
            _drag = d.pointerCount >= 2
                ? _Drag.scale
                : nearCorner
                    ? _Drag.corner
                    : _Drag.move;
          },
          onScaleUpdate: (d) {
            final delta = d.localFocalPoint - _from;
            Rect n;
            switch (_drag) {
              case _Drag.move:
                n = _start.shift(delta);
              case _Drag.corner:
                n = Rect.fromPoints(_start.topLeft, _start.bottomRight + delta);
              case _Drag.scale:
                n = Rect.fromCenter(
                    center: _start.center + delta, width: _start.width * d.scale, height: _start.height * d.scale);
              case _Drag.none:
                return;
            }
            if (n.width < 12 || n.height < 12) return;
            setState(() => _live = toNorm(n));
          },
          onScaleEnd: (_) {
            final live = _live;
            _drag = _Drag.none;
            if (live != null) widget.onChanged(live);
            // Keep showing the new place until the parent passes the committed rect.
            setState(() => _live = null);
          },
          child: CustomPaint(painter: _CoverPainter(r, widget.shape), size: box.biggest),
        );
      });
}

class _CoverPainter extends CustomPainter {
  _CoverPainter(this.r, this.shape);
  final Rect r;
  final String shape;

  @override
  void paint(Canvas canvas, Size size) {
    final fill = Paint()..color = VsColors.accent.withValues(alpha: 0.16);
    final line = Paint()
      ..color = VsColors.accent
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.6;
    if (shape == 'ellipse' || shape == 'circle') {
      final e =
          shape == 'circle' ? Rect.fromCenter(center: r.center, width: r.shortestSide, height: r.shortestSide) : r;
      canvas.drawOval(e, fill);
      canvas.drawOval(e, line);
    } else {
      canvas.drawRect(r, fill);
      canvas.drawRect(r, line);
    }
    final h = Paint()..color = VsColors.accent;
    for (final c in [r.topLeft, r.topRight, r.bottomLeft]) {
      canvas.drawRect(Rect.fromCenter(center: c, width: 8, height: 8), h);
    }
    canvas.drawCircle(r.bottomRight, 9, h); // the resize handle
  }

  @override
  bool shouldRepaint(_CoverPainter old) => old.r != r || old.shape != shape;
}

/// Panel under the preview while covering.
class CoverPanel extends StatefulWidget {
  const CoverPanel({
    super.key,
    required this.vs,
    required this.covers,
    required this.active,
    required this.state,
    required this.tracking,
    required this.trackProgress,
    required this.onSelect,
    required this.onAdd,
    required this.onPatch,
    required this.onAmount,
    required this.onFollow,
    required this.onTrack,
    required this.onRemove,
    required this.onDone,
  });
  final VStudio vs;
  final List<String> covers; // ids on the clip, in order
  final String? active;
  final CoverState? state;
  final bool tracking;
  final double trackProgress;
  final ValueChanged<String> onSelect;
  final void Function(String style, String shape) onAdd;
  final ValueChanged<Map<String, Object?>> onPatch; // style, shape, invert
  final ValueChanged<double> onAmount;
  final ValueChanged<bool> onFollow;
  final VoidCallback onTrack; // start or stop
  final VoidCallback onRemove;
  final VoidCallback onDone;

  @override
  State<CoverPanel> createState() => _CoverPanelState();
}

class _CoverPanelState extends State<CoverPanel> {
  late final List<CoverStyleInfo> _styles = widget.vs.coverStyles();
  late final List<(String, String)> _shapes = widget.vs.coverShapes();
  final Map<String, ui.Image> _thumbs = {};
  String _shape = 'rect';
  double? _amount; // while the slider is held

  static const tw = 64, th = 40;

  @override
  void initState() {
    super.initState();
    _shape = widget.state?.shape ?? 'rect';
    _renderThumbs();
  }

  @override
  void dispose() {
    for (final i in _thumbs.values) {
      i.dispose();
    }
    super.dispose();
  }

  // A small picture with a striped "logo", shown under each style.
  static Uint8List _sample() {
    final px = Uint8List(tw * th * 4);
    for (var y = 0; y < th; y++) {
      for (var x = 0; x < tw; x++) {
        final i = (y * tw + x) * 4;
        var r = 80 + 120 * y ~/ th, g = 140 + 60 * y ~/ th, b = 210 - 50 * y ~/ th;
        if (x > tw * 0.25 && x < tw * 0.75 && y > th * 0.25 && y < th * 0.75) {
          r = 220;
          g = 40;
          b = 40;
          if ((y ~/ 3) % 2 == 0) r = g = b = 255;
        }
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
      }
    }
    return px;
  }

  void _renderThumbs() {
    final base = _sample();
    for (final s in _styles) {
      final px = widget.vs.coverPreview(s.id, _shape, base, tw, th);
      if (px == null) continue;
      ui.decodeImageFromPixels(px, tw, th, ui.PixelFormat.rgba8888, (img) {
        if (!mounted) return img.dispose();
        setState(() {
          _thumbs[s.id]?.dispose();
          _thumbs[s.id] = img;
        });
      });
    }
  }

  @override
  void didUpdateWidget(CoverPanel old) {
    super.didUpdateWidget(old);
    final shape = widget.state?.shape;
    if (shape != null && shape != _shape) {
      _shape = shape;
      _renderThumbs();
    }
  }

  @override
  Widget build(BuildContext context) {
    final st = widget.state;
    final active = widget.active != null && st != null;
    return Container(
      color: VsColors.panel,
      padding: const EdgeInsets.fromLTRB(8, 4, 8, 4),
      child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Row(children: [
          Expanded(
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Row(children: [
                for (var i = 0; i < widget.covers.length; i++)
                  Padding(
                    padding: const EdgeInsets.only(right: 4),
                    child: ChoiceChip(
                      label: Text('Vùng ${i + 1}', style: const TextStyle(fontSize: 12)),
                      selected: widget.covers[i] == widget.active,
                      visualDensity: VisualDensity.compact,
                      onSelected: (_) => widget.onSelect(widget.covers[i]),
                    ),
                  ),
                ActionChip(
                  key: const Key('cover-add'),
                  avatar: const Icon(Icons.add, size: 16),
                  label: const Text('Thêm vùng', style: TextStyle(fontSize: 12)),
                  visualDensity: VisualDensity.compact,
                  onPressed: () => widget.onAdd(st?.style ?? 'blur', _shape),
                ),
              ]),
            ),
          ),
          TextButton(key: const Key('cover-done'), onPressed: widget.onDone, child: const Text('Xong')),
        ]),
        SizedBox(
          height: th + 22.0,
          child: ListView(scrollDirection: Axis.horizontal, children: [
            for (final s in _styles)
              GestureDetector(
                key: Key('cover-style-${s.id}'),
                onTap: () => active ? widget.onPatch({'style': s.id}) : widget.onAdd(s.id, _shape),
                child: Padding(
                  padding: const EdgeInsets.only(right: 6),
                  child: Column(children: [
                    Container(
                      width: tw.toDouble(),
                      height: th.toDouble(),
                      decoration: BoxDecoration(
                        border: Border.all(color: st?.style == s.id ? VsColors.accent : Colors.transparent, width: 2),
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: _thumbs[s.id] == null ? null : RawImage(image: _thumbs[s.id], fit: BoxFit.cover),
                    ),
                    Text(s.name, style: const TextStyle(fontSize: 10, color: VsColors.fg2)),
                  ]),
                ),
              ),
          ]),
        ),
        SizedBox(
          height: 34,
          child: ListView(scrollDirection: Axis.horizontal, children: [
            for (final (id, name) in _shapes)
              Padding(
                padding: const EdgeInsets.only(right: 4),
                child: ChoiceChip(
                  label: Text(name, style: const TextStyle(fontSize: 11)),
                  selected: _shape == id,
                  visualDensity: VisualDensity.compact,
                  onSelected: (_) {
                    setState(() => _shape = id);
                    _renderThumbs();
                    if (active) widget.onPatch({'shape': id});
                  },
                ),
              ),
          ]),
        ),
        Row(children: [
          const Text('Độ mạnh', style: TextStyle(fontSize: 12, color: VsColors.fg2)),
          Expanded(
            child: Slider(
              key: const Key('cover-amount'),
              value: (_amount ?? st?.amount ?? 0.5).clamp(0, 1).toDouble(),
              onChanged: active ? (v) => setState(() => _amount = v) : null,
              onChangeEnd: active
                  ? (v) {
                      setState(() => _amount = null);
                      widget.onAmount(v);
                    }
                  : null,
            ),
          ),
        ]),
        Wrap(spacing: 6, crossAxisAlignment: WrapCrossAlignment.center, children: [
          FilledButton.tonalIcon(
            key: const Key('cover-track'),
            icon: Icon(widget.tracking ? Icons.stop : Icons.my_location, size: 16),
            label: Text(widget.tracking ? 'Dừng ${(widget.trackProgress * 100).round()}%' : 'Bám theo',
                style: const TextStyle(fontSize: 12)),
            onPressed: active || widget.tracking ? widget.onTrack : null,
          ),
          FilterChip(
            key: const Key('cover-follow'),
            label: const Text('Theo keyframe', style: TextStyle(fontSize: 12)),
            selected: st?.following ?? false,
            visualDensity: VisualDensity.compact,
            onSelected: active ? widget.onFollow : null,
          ),
          FilterChip(
            label: const Text('Che bên ngoài', style: TextStyle(fontSize: 12)),
            selected: st?.invert ?? false,
            visualDensity: VisualDensity.compact,
            onSelected: active ? (v) => widget.onPatch({'invert': v}) : null,
          ),
          IconButton(
            key: const Key('cover-remove'),
            tooltip: 'Xóa vùng che',
            icon: const Icon(Icons.delete_outline),
            onPressed: active ? widget.onRemove : null,
          ),
        ]),
      ]),
    );
  }
}
