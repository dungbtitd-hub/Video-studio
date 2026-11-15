// "Đoạn áp dụng": the part of the video an effect plays in, chosen with the fixed playhead.
//
//   1. Swipe the timeline to where it should start, tap "Điểm đầu".
//   2. Swipe on to where it should end, tap "Điểm cuối". The part is lit on the timeline.
//   3. Pick what happens in it: zoom in gradually, sound up/down gradually, a video effect,
//      a filter, a speed. Only that part changes (the clip is cut at both ends when needed).
import 'package:flutter/material.dart';

import 'theme.dart';

/// A chosen part of the timeline, in seconds (start < end once both points are set).
class TimeRange {
  const TimeRange(this.start, this.end);
  final double start;
  final double? end; // null until "Điểm cuối"
  bool get complete => end != null && end! > start;
  double get length => complete ? end! - start : 0;
}

/// Step-by-step guide for the range.
String rangeGuide(TimeRange? r) {
  if (r == null) return 'Bước 1: vuốt timeline tới chỗ bắt đầu, bấm "Điểm đầu".';
  if (!r.complete) return 'Bước 2: vuốt tiếp tới chỗ kết thúc, bấm "Điểm cuối".';
  return 'Bước 3: chọn hiệu ứng cho đoạn này. Chỉ đoạn sáng trên timeline thay đổi.';
}

String rangeText(TimeRange r) {
  String t(double s) => '${s ~/ 60}:${(s % 60).toStringAsFixed(1).padLeft(4, '0')}';
  return r.complete ? '${t(r.start)} – ${t(r.end!)}  (${r.length.toStringAsFixed(1)} s)' : 'Từ ${t(r.start)}';
}

/// What can be done to a range.
enum RangeAction { zoom, volume, effect, filter, speed, color }

class RangeBar extends StatelessWidget {
  const RangeBar({
    super.key,
    required this.range,
    required this.onStart,
    required this.onEnd,
    required this.onClear,
    required this.onAction,
    this.video = true,
  });

  final TimeRange? range;
  final VoidCallback onStart, onEnd, onClear;
  final void Function(RangeAction action, double value) onAction;
  final bool video; // picture actions only for video / photo clips

  Widget _chip(Key key, String label, VoidCallback? onTap, {IconData? icon}) => Padding(
        padding: const EdgeInsets.only(right: 6, bottom: 6),
        child: ActionChip(
          key: key,
          avatar: icon == null ? null : Icon(icon, size: 16),
          label: Text(label, style: const TextStyle(fontSize: 12)),
          visualDensity: VisualDensity.compact,
          onPressed: onTap,
        ),
      );

  @override
  Widget build(BuildContext context) {
    final r = range;
    final done = r != null && r.complete;
    return Container(
      color: VsColors.panel,
      padding: const EdgeInsets.fromLTRB(12, 8, 8, 4),
      child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Row(children: [
          const Text('Đoạn áp dụng', style: TextStyle(fontWeight: FontWeight.w700, fontSize: 13)),
          const SizedBox(width: 8),
          if (r != null)
            Expanded(
              child: Text(rangeText(r),
                  key: const Key('range-text'),
                  style: const TextStyle(color: VsColors.accent, fontSize: 12),
                  overflow: TextOverflow.ellipsis),
            )
          else
            const Spacer(),
          if (r != null)
            IconButton(
              key: const Key('range-clear'),
              tooltip: 'Bỏ đoạn',
              visualDensity: VisualDensity.compact,
              icon: const Icon(Icons.close, size: 18),
              onPressed: onClear,
            ),
        ]),
        Text(rangeGuide(r), key: const Key('range-guide'), style: const TextStyle(fontSize: 12, color: VsColors.fg2)),
        const SizedBox(height: 6),
        Row(children: [
          Expanded(
            child: FilledButton.tonal(
              key: const Key('range-start'),
              style: FilledButton.styleFrom(
                  backgroundColor: r == null ? VsColors.accent : VsColors.raised,
                  foregroundColor: r == null ? VsColors.accentInk : VsColors.fg),
              onPressed: onStart,
              child: const Text('[ Điểm đầu'),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: FilledButton.tonal(
              key: const Key('range-end'),
              style: FilledButton.styleFrom(
                  backgroundColor: r != null && !r.complete ? VsColors.accent : VsColors.raised,
                  foregroundColor: r != null && !r.complete ? VsColors.accentInk : VsColors.fg),
              onPressed: r == null ? null : onEnd,
              child: const Text('Điểm cuối ]'),
            ),
          ),
        ]),
        const SizedBox(height: 6),
        if (done)
          SingleChildScrollView(
              key: const Key('range-actions'),
              scrollDirection: Axis.horizontal,
              child: Row(children: [
                if (video) ...[
                  _chip(const Key('range-zoom-1.3'), 'Zoom dần 1.3×', () => onAction(RangeAction.zoom, 1.3),
                      icon: Icons.zoom_in),
                  _chip(const Key('range-zoom-1.6'), 'Zoom dần 1.6×', () => onAction(RangeAction.zoom, 1.6)),
                  _chip(const Key('range-zoom-2'), 'Zoom dần 2×', () => onAction(RangeAction.zoom, 2)),
                  _chip(const Key('range-zoom-out'), 'Thu nhỏ dần', () => onAction(RangeAction.zoom, 1 / 1.6),
                      icon: Icons.zoom_out),
                ],
                _chip(const Key('range-vol-0'), 'Nhỏ dần tới tắt', () => onAction(RangeAction.volume, 0),
                    icon: Icons.volume_down),
                _chip(const Key('range-vol-2'), 'To dần ×2', () => onAction(RangeAction.volume, 2),
                    icon: Icons.volume_up),
                if (video) ...[
                  _chip(const Key('range-effect'), 'Hiệu ứng…', () => onAction(RangeAction.effect, 0),
                      icon: Icons.auto_awesome),
                  _chip(const Key('range-filter'), 'Bộ lọc…', () => onAction(RangeAction.filter, 0),
                      icon: Icons.filter_vintage_outlined),
                  _chip(const Key('range-color'), 'Chỉnh màu…', () => onAction(RangeAction.color, 0), icon: Icons.tune),
                ],
                _chip(const Key('range-slow'), 'Chậm 0.5×', () => onAction(RangeAction.speed, 0.5),
                    icon: Icons.slow_motion_video),
                _chip(const Key('range-fast'), 'Nhanh 2×', () => onAction(RangeAction.speed, 2),
                    icon: Icons.fast_forward),
              ])),
      ]),
    );
  }
}
