// Transitions on the phone: tap the badge under a cut on the timeline (or the
// "Chuyển cảnh" tool), pick one of the thumbnails, set the length. The chosen
// transition plays in a small preview at the top so the motion can be judged.
import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../core/project_model.dart';
import '../core/vstudio.dart';
import 'theme.dart';

class TransitionSheet extends StatefulWidget {
  const TransitionSheet({
    super.key,
    required this.vs,
    required this.cut,
    required this.onApply,
    required this.onApplyAll,
    required this.onRemove,
    required this.onDuration,
  });
  final VStudio vs;
  final CutPoint cut;
  final void Function(String type, double seconds) onApply;
  final void Function(String type, double seconds) onApplyAll;
  final VoidCallback onRemove;
  final ValueChanged<double> onDuration; // length of the transition already set here

  @override
  State<TransitionSheet> createState() => _TransitionSheetState();
}

class _TransitionSheetState extends State<TransitionSheet> {
  static const tw = 72, th = 40; // thumbnails
  static const pw = 160, ph = 90; // the playing preview
  late final List<TransitionInfo> _all = widget.vs.transitions();
  final Map<String, ui.Image> _thumbs = {};
  final List<ui.Image> _frames = [];
  Timer? _timer;
  int _frame = 0;
  String? _type;
  late double _seconds;

  @override
  void initState() {
    super.initState();
    _type = widget.cut.type;
    _seconds = widget.cut.type != null && widget.cut.duration > 0 ? widget.cut.duration : 0.5;
    final a = _picture(tw, th, false), b = _picture(tw, th, true);
    for (final t in _all) {
      final px = widget.vs.transitionPreview(t.id, a, b, tw, th, 0.45);
      if (px == null) continue;
      ui.decodeImageFromPixels(px, tw, th, ui.PixelFormat.rgba8888, (img) {
        if (!mounted) return img.dispose();
        setState(() => _thumbs[t.id] = img);
      });
    }
    if (_type != null) _play(_type!);
  }

  @override
  void dispose() {
    _timer?.cancel();
    for (final i in [..._thumbs.values, ..._frames]) {
      i.dispose();
    }
    super.dispose();
  }

  /// Two clearly different pictures: a cool landscape and a warm sunset.
  static Uint8List _picture(int w, int h, bool warm) {
    final px = Uint8List(w * h * 4);
    for (var y = 0; y < h; y++) {
      for (var x = 0; x < w; x++) {
        final i = (y * w + x) * 4;
        final t = y / h;
        int r, g, b;
        if (warm) {
          (r, g, b) = ((250 - 60 * t).round(), (160 - 80 * t).round(), (60 + 40 * t).round());
          final dx = x - w * 0.68, dy = y - h * 0.4;
          if (dx * dx + dy * dy < (h * 0.2) * (h * 0.2)) (r, g, b) = (255, 240, 120);
          if (y > h * 0.75) (r, g, b) = (90, 50, 30);
        } else {
          (r, g, b) = ((60 + 40 * t).round(), (120 + 60 * t).round(), (220 - 60 * t).round());
          if (y > h - (w * 0.32 - (x - w * 0.32).abs()) * h / w * 1.4) (r, g, b) = (40, 90, 50);
        }
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
      }
    }
    return px;
  }

  /// Renders the transition into a short loop (with a hold on each picture) and plays it.
  void _play(String type) {
    _timer?.cancel();
    for (final f in _frames) {
      f.dispose();
    }
    _frames.clear();
    final a = _picture(pw, ph, false), b = _picture(pw, ph, true);
    const steps = 18;
    final images = List<ui.Image?>.filled(steps + 1, null);
    var pending = steps + 1;
    for (var i = 0; i <= steps; i++) {
      final px = widget.vs.transitionPreview(type, a, b, pw, ph, i / steps);
      if (px == null) return;
      ui.decodeImageFromPixels(px, pw, ph, ui.PixelFormat.rgba8888, (img) {
        if (!mounted || _type != type) return img.dispose();
        images[i] = img;
        if (--pending > 0) return;
        setState(() {
          _frames.addAll(images.whereType<ui.Image>());
          _frame = 0;
        });
        // Hold A, play, hold B: the loop reads like a real cut.
        const hold = 8;
        var tick = 0;
        _timer = Timer.periodic(const Duration(milliseconds: 50), (_) {
          tick = (tick + 1) % (_frames.length + hold * 2);
          setState(() => _frame = math.min(_frames.length - 1, math.max(0, tick - hold)));
        });
      });
    }
  }

  String get _title => widget.cut.joined
      ? 'Chuyển cảnh giữa hai clip'
      : widget.cut.edge == 0
          ? 'Đầu clip: hiện dần lên'
          : 'Cuối clip: mờ dần đi';

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
        child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Row(children: [
            Expanded(child: Text(_title, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600))),
            SizedBox(
              width: pw * 0.75,
              height: ph * 0.75,
              child: ClipRRect(
                borderRadius: BorderRadius.circular(6),
                child: _frames.isEmpty
                    ? const ColoredBox(color: Colors.black26)
                    : RawImage(key: const Key('transition-playing'), image: _frames[_frame], fit: BoxFit.cover),
              ),
            ),
          ]),
          const SizedBox(height: 8),
          SizedBox(
            height: 230,
            child: GridView.builder(
              gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
                  maxCrossAxisExtent: tw + 16.0, mainAxisExtent: th + 26.0, crossAxisSpacing: 4, mainAxisSpacing: 4),
              itemCount: _all.length,
              itemBuilder: (context, i) {
                final t = _all[i];
                final on = t.id == _type;
                return GestureDetector(
                  key: Key('transition-${t.id}'),
                  onTap: () {
                    setState(() => _type = t.id);
                    widget.onApply(t.id, _seconds);
                    _play(t.id);
                  },
                  child: Column(children: [
                    Container(
                      width: tw.toDouble(),
                      height: th.toDouble(),
                      decoration: BoxDecoration(
                        border: Border.all(color: on ? VsColors.accent : Colors.transparent, width: 2),
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: _thumbs[t.id] == null ? null : RawImage(image: _thumbs[t.id], fit: BoxFit.cover),
                    ),
                    Text(t.name,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(fontSize: 10, color: on ? VsColors.fg : VsColors.fg2)),
                  ]),
                );
              },
            ),
          ),
          Row(children: [
            const Text('Độ dài', style: TextStyle(fontSize: 12, color: VsColors.fg2)),
            Expanded(
              child: Slider(
                key: const Key('transition-duration'),
                min: 0.1,
                max: 3.0,
                divisions: 29,
                value: _seconds.clamp(0.1, 3.0).toDouble(),
                onChanged: (v) => setState(() => _seconds = v),
                onChangeEnd: (v) {
                  if (_type != null) widget.onDuration(v);
                },
              ),
            ),
            SizedBox(
                width: 52, child: Text('${_seconds.toStringAsFixed(1)} giây', style: const TextStyle(fontSize: 12))),
          ]),
          Row(children: [
            Expanded(
              child: OutlinedButton(
                key: const Key('transition-all'),
                onPressed: _type == null ? null : () => widget.onApplyAll(_type!, _seconds),
                child: const Text('Áp dụng cho tất cả'),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: OutlinedButton(
                key: const Key('transition-remove'),
                onPressed: _type == null
                    ? null
                    : () {
                        widget.onRemove();
                        _timer?.cancel();
                        setState(() => _type = null);
                      },
                child: const Text('Bỏ chuyển cảnh'),
              ),
            ),
          ]),
        ]),
      ),
    );
  }
}
