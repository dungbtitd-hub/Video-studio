// Keyframes on the quick-edit screen.
//
// PreviewZoom: pinch (and drag) the picture with two fingers to zoom on a
// subject. While the fingers move only the shown image is transformed (no
// core work, no lag); on release one edit is committed: a keyframe when the
// clip's picture is animated, otherwise the clip's own zoom. Double-tap resets.
//
// KeyframeBar: ◇ Hình / ◇ Âm thanh add or remove a key at the playhead (the two
// groups are independent), ◀◆ / ◆▶ jump between keys, and the volume slider sets
// the sound level at the playhead (a key when the sound is animated).
import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

/// A finished pinch, in canvas pixels relative to the frame centre: the point
/// that was under the fingers ([from]) ends at [to], everything scaled by [scale].
typedef ZoomCommit = void Function(double scale, Offset from, Offset to);

class PreviewZoom extends StatefulWidget {
  const PreviewZoom({
    super.key,
    required this.enabled,
    required this.canvasSize,
    required this.onCommit,
    required this.child,
    this.onReset,
  });
  final bool enabled;
  final Size canvasSize; // project frame size in pixels
  final ZoomCommit onCommit;
  final VoidCallback? onReset;
  final Widget child; // the preview surface (texture or image), canvas letterboxed inside

  @override
  State<PreviewZoom> createState() => PreviewZoomState();
}

class PreviewZoomState extends State<PreviewZoom> {
  // Raw pointers (not a scale recogniser): the zoom follows the fingers from the
  // first pixel, without losing the touch slop at the start of the pinch.
  final Map<int, Offset> _now = {};
  Offset _from = Offset.zero, _to = Offset.zero; // local coordinates
  double _scale = 1, _span0 = 1;
  bool _active = false, _holding = false;
  Rect _canvas = Rect.zero;
  Timer? _release;

  /// Where the canvas is drawn inside a box of [size] (contained, centred).
  Rect canvasRect(Size size) {
    final c = widget.canvasSize;
    final k = math.min(size.width / c.width, size.height / c.height);
    final w = c.width * k, h = c.height * k;
    return Rect.fromLTWH((size.width - w) / 2, (size.height - h) / 2, w, h);
  }

  Offset _toCanvas(Offset local) => Offset((local.dx - _canvas.center.dx) / _canvas.width * widget.canvasSize.width,
      (local.dy - _canvas.center.dy) / _canvas.height * widget.canvasSize.height);

  (Offset, double) _focalSpan(Map<int, Offset> pts) {
    final p = pts.values.take(2).toList();
    return ((p[0] + p[1]) / 2, (p[0] - p[1]).distance);
  }

  void _pointerDown(PointerDownEvent e) {
    _now[e.pointer] = e.localPosition;
    if (_now.length == 2 && !_active) {
      _release?.cancel();
      final (focal, span) = _focalSpan(_now);
      setState(() {
        _active = true;
        _holding = false;
        _from = focal;
        _to = focal;
        _span0 = math.max(span, 1);
        _scale = 1;
      });
    }
  }

  void _pointerMove(PointerMoveEvent e) {
    if (!_now.containsKey(e.pointer)) return;
    _now[e.pointer] = e.localPosition;
    if (!_active || _now.length < 2) return;
    final (focal, span) = _focalSpan(_now);
    setState(() {
      _scale = (span / _span0).clamp(0.2, 12).toDouble();
      _to = focal;
    });
  }

  void _pointerUp(PointerEvent e) {
    _now.remove(e.pointer);
    if (!_active) return;
    _active = false;
    final moved = (_to - _from).distance > 4 || (_scale - 1).abs() > 0.02;
    if (moved && e is! PointerCancelEvent) widget.onCommit(_scale, _toCanvas(_from), _toCanvas(_to));
    // Keep showing the result until the core's new frame is up.
    setState(() => _holding = moved);
    _release = Timer(const Duration(milliseconds: 220), () {
      if (mounted) setState(() => _holding = false);
    });
  }

  @override
  void dispose() {
    _release?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!widget.enabled) return widget.child;
    return LayoutBuilder(builder: (context, box) {
      _canvas = canvasRect(box.biggest);
      // Shown while the fingers move: the same maths the core will apply.
      final m = Matrix4.identity()
        ..translate(_to.dx, _to.dy)
        ..scale(_scale, _scale)
        ..translate(-_from.dx, -_from.dy);
      return Listener(
        onPointerDown: _pointerDown,
        onPointerMove: _pointerMove,
        onPointerUp: _pointerUp,
        onPointerCancel: _pointerUp,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onDoubleTap: widget.onReset,
          child: Stack(fit: StackFit.expand, children: [
            ClipRect(
              child: _active || _holding ? Transform(transform: m, child: widget.child) : widget.child,
            ),
            if (_active)
              Positioned(
                left: _canvas.left + 8,
                top: _canvas.top + 8,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(color: Colors.black54, borderRadius: BorderRadius.circular(4)),
                  child: Text('×${_scale.toStringAsFixed(_scale < 10 ? 1 : 0)}',
                      style: const TextStyle(color: VsColors.fg, fontSize: 12)),
                ),
              ),
          ]),
        ),
      );
    });
  }
}

/// What the keyframe bar tells the user to do next, from the keys the clip already has.
/// Keys are always set at the fixed playhead: scrub the timeline under it, then tap.
String keyframeGuide({required bool inside, required bool video, required int keys, required bool here}) {
  if (!inside) return 'Vuốt timeline để đưa thanh playback vào clip đang chọn.';
  if (here) {
    return video
        ? 'Đang ở một điểm keyframe: dùng hai ngón tay phóng to / kéo hình, hoặc kéo âm lượng. Bấm ◆ lần nữa để bỏ điểm này.'
        : 'Đang ở một điểm keyframe: kéo âm lượng. Bấm ◆ lần nữa để bỏ điểm này.';
  }
  if (keys == 0) return 'Bước 1: vuốt timeline tới chỗ bắt đầu, bấm ◆ Điểm đầu.';
  if (keys == 1) return 'Bước 2: vuốt tiếp tới chỗ kết thúc, bấm ◆ Điểm cuối, rồi chỉnh hình hoặc âm lượng ở đó.';
  return 'Đoạn giữa các điểm ◆ chuyển dần. Chạm ◆ trên clip để nhảy tới, hoặc bấm ◆ Thêm điểm ở chỗ khác.';
}

/// Label of the main keyframe button.
String keyframeAction({required int keys, required bool here}) =>
    here ? '◆ Bỏ điểm này' : (keys == 0 ? '◆ Điểm đầu' : (keys == 1 ? '◆ Điểm cuối' : '◆ Thêm điểm'));

class KeyframeBar extends StatefulWidget {
  const KeyframeBar({
    super.key,
    required this.state,
    required this.video,
    required this.hasKeys,
    required this.onToggle,
    required this.onJump,
    required this.onVolume,
    this.pictureKeys = 0,
    this.audioKeys = 0,
  });
  final ClipKeyState? state; // null: no clip under the playhead
  final bool video; // the clip has a picture
  final bool hasKeys; // the clip has keys to jump between
  final int pictureKeys, audioKeys; // keys the clip has (for the step-by-step guide)
  final void Function(KeyGroup group) onToggle;
  final void Function(bool forward) onJump;
  final ValueChanged<double> onVolume; // gain, 1 = unchanged

  @override
  State<KeyframeBar> createState() => _KeyframeBarState();
}

class _KeyframeBarState extends State<KeyframeBar> {
  double? _dragging; // volume while the slider is held

  @override
  Widget build(BuildContext context) {
    final st = widget.state;
    final ok = st != null && st.inside;
    final volume = _dragging ?? (ok ? st.volume : 1.0);
    // The picture is the main group for video; sound alone for audio clips.
    final picture = widget.video;
    final keys = picture ? widget.pictureKeys : widget.audioKeys;
    final here = ok && (picture ? st.pictureHere : st.audioHere);
    return Container(
      color: VsColors.panel,
      padding: const EdgeInsets.fromLTRB(8, 6, 4, 0),
      child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        const Padding(
          padding: EdgeInsets.only(bottom: 2),
          child: Text('Keyframe', style: TextStyle(fontWeight: FontWeight.w700, fontSize: 13)),
        ),
        Text(keyframeGuide(inside: ok, video: picture, keys: keys, here: here),
            key: const Key('kf-guide'), style: const TextStyle(fontSize: 12, color: VsColors.fg2)),
        Wrap(crossAxisAlignment: WrapCrossAlignment.center, children: [
          IconButton(
            key: const Key('kf-prev'),
            tooltip: 'Keyframe trước',
            visualDensity: VisualDensity.compact,
            icon: const Text('◀◆', style: TextStyle(fontSize: 12)),
            onPressed: widget.hasKeys ? () => widget.onJump(false) : null,
          ),
          FilledButton.tonal(
            key: Key(picture ? 'kf-picture' : 'kf-audio'),
            style: FilledButton.styleFrom(
              visualDensity: VisualDensity.compact,
              backgroundColor: here ? VsColors.accent : VsColors.raised,
              foregroundColor: here ? VsColors.accentInk : VsColors.fg,
            ),
            onPressed: ok ? () => widget.onToggle(picture ? KeyGroup.picture : KeyGroup.audio) : null,
            child: Text(keyframeAction(keys: keys, here: here), style: const TextStyle(fontSize: 12)),
          ),
          if (picture)
            Padding(
              padding: const EdgeInsets.only(left: 4),
              child: ChoiceChip(
                key: const Key('kf-audio'),
                label: Text('${ok && st.audioHere ? '◆' : '◇'} Âm thanh', style: const TextStyle(fontSize: 12)),
                selected: ok && st.audioHere,
                showCheckmark: false,
                visualDensity: VisualDensity.compact,
                materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                selectedColor: VsColors.accent.withValues(alpha: 0.25),
                onSelected: ok ? (_) => widget.onToggle(KeyGroup.audio) : null,
              ),
            ),
          IconButton(
            key: const Key('kf-next'),
            tooltip: 'Keyframe sau',
            visualDensity: VisualDensity.compact,
            icon: const Text('◆▶', style: TextStyle(fontSize: 12)),
            onPressed: widget.hasKeys ? () => widget.onJump(true) : null,
          ),
        ]),
        // Volume on its own row: the bar also fits the narrow side panel of a foldable.
        Row(children: [
          const Padding(
            padding: EdgeInsets.only(left: 12),
            child: Icon(Icons.volume_up_outlined, size: 18, color: VsColors.fg2),
          ),
          Expanded(
            child: Slider(
              key: const Key('kf-volume'),
              value: volume.clamp(0, 4).toDouble(),
              max: 4,
              divisions: 80,
              onChanged: ok ? (v) => setState(() => _dragging = v) : null,
              onChangeEnd: ok
                  ? (v) {
                      setState(() => _dragging = null);
                      widget.onVolume(v);
                    }
                  : null,
            ),
          ),
          SizedBox(
            width: 40,
            child: Text('${(volume * 100).round()}%', style: const TextStyle(fontSize: 11, color: VsColors.fg2)),
          ),
        ]),
      ]),
    );
  }
}
