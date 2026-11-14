// "Xoay / lật" (rotate / flip) for a video or photo clip: quarter turns clockwise, mirror
// left-right, flip upside down. Applied to the decoded picture by the core (orientation.hpp),
// so the clip still fits the frame; one undo step per tap.
import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

class OrientSheet extends StatefulWidget {
  const OrientSheet({super.key, required this.vs, required this.clipIds, required this.onEdit});
  final VStudio vs;
  final List<String> clipIds; // the first one's state is shown
  final void Function(void Function(VStudio vs) edit) onEdit;

  @override
  State<OrientSheet> createState() => _OrientSheetState();
}

class _OrientSheetState extends State<OrientSheet> {
  late (int, bool, bool) _o = _read();

  (int, bool, bool) _read() {
    try {
      return widget.vs.clipOrientation(widget.clipIds.first);
    } on VStudioException {
      return (0, false, false);
    }
  }

  void _set(int rotate, bool flipH, bool flipV) {
    widget.onEdit((vs) => vs.setClipOrientation(widget.clipIds, rotate % 360, flipH: flipH, flipV: flipV));
    setState(() => _o = _read());
  }

  Widget _button(Key key, IconData icon, String label, VoidCallback onTap, {bool on = false}) => Expanded(
        child: Material(
          color: on ? VsColors.accent.withValues(alpha: 0.2) : VsColors.panel2,
          borderRadius: BorderRadius.circular(12),
          child: InkWell(
            key: key,
            borderRadius: BorderRadius.circular(12),
            onTap: onTap,
            child: Padding(
              padding: const EdgeInsets.symmetric(vertical: 14),
              child: Column(children: [
                Icon(icon, color: on ? VsColors.accent : VsColors.fg),
                const SizedBox(height: 6),
                Text(label, style: const TextStyle(fontSize: 12, color: VsColors.fg2)),
              ]),
            ),
          ),
        ),
      );

  @override
  Widget build(BuildContext context) {
    final (r, h, v) = _o;
    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 14, 16, 16),
        child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            const Expanded(
                child: Text('Xoay / lật', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700))),
            Text(r == 0 ? 'Gốc' : 'Đã xoay $r°', style: const TextStyle(color: VsColors.fg3, fontSize: 12)),
          ]),
          const SizedBox(height: 12),
          Row(children: [
            _button(const Key('orient-left'), Icons.rotate_left, 'Xoay trái', () => _set(r + 270, h, v)),
            const SizedBox(width: 8),
            _button(const Key('orient-right'), Icons.rotate_right, 'Xoay phải', () => _set(r + 90, h, v)),
            const SizedBox(width: 8),
            _button(const Key('orient-flip-h'), Icons.flip, 'Lật ngang', () => _set(r, !h, v), on: h),
            const SizedBox(width: 8),
            _button(const Key('orient-flip-v'), Icons.flip, 'Lật dọc', () => _set(r, h, !v), on: v),
          ]),
          const SizedBox(height: 10),
          Row(children: [
            const Expanded(
              child: Text('Hình xoay vẫn vừa khung. Video quay dọc từ điện thoại tự đứng đúng chiều.',
                  style: TextStyle(fontSize: 11, color: VsColors.fg3)),
            ),
            TextButton(
              key: const Key('orient-reset'),
              onPressed: r == 0 && !h && !v ? null : () => _set(0, false, false),
              child: const Text('Về gốc'),
            ),
          ]),
        ]),
      ),
    );
  }
}
