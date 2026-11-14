// "Bám theo đối tượng" (FR-028): the selected text or sticker follows the object under it in the
// video below, from the playhead to the clip's end. Tracking runs in the core's background job;
// this only polls its status and shows progress, so the UI never waits.
import 'dart:async';

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

class FollowButton extends StatefulWidget {
  const FollowButton({super.key, required this.vs, required this.clipId, required this.playhead, required this.onDone});
  final VStudio vs;

  /// The text or sticker to move (null: nothing selected yet).
  final String? Function() clipId;
  final double Function() playhead;

  /// Called after the keys were applied (one undo step), so the editor refreshes.
  final VoidCallback onDone;

  @override
  State<FollowButton> createState() => _FollowButtonState();
}

class _FollowButtonState extends State<FollowButton> {
  Timer? _poll;
  double _progress = 0;
  bool _size = false;
  String? _note;

  @override
  void dispose() {
    if (_poll?.isActive == true) widget.vs.cancelToolJob();
    _poll?.cancel();
    super.dispose();
  }

  void _start() {
    final id = widget.clipId();
    if (id == null) return;
    try {
      widget.vs.startFollow(id, widget.playhead(), size: _size);
    } on VStudioException catch (e) {
      setState(() => _note = e.message.contains('no video') ? 'Không có video nào bên dưới ở playhead' : e.message);
      return;
    }
    setState(() {
      _note = 'Đang dò đối tượng…';
      _progress = 0;
    });
    _poll = Timer.periodic(const Duration(milliseconds: 200), (t) {
      final (st, p, err) = widget.vs.toolJobStatus();
      if (st == 1) {
        setState(() => _progress = p);
        return;
      }
      t.cancel();
      if (st == 2) widget.onDone();
      setState(() => _note = st == 2
          ? 'Đang bám theo đối tượng'
          : st == -2
              ? 'Đã dừng'
              : 'Không bám theo được: $err');
    });
  }

  void _stop() {
    widget.vs.cancelToolJob();
  }

  @override
  Widget build(BuildContext context) {
    final busy = _poll?.isActive == true;
    return Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Row(children: [
        Expanded(
          child: OutlinedButton.icon(
            key: const Key('follow-motion'),
            icon: Icon(busy ? Icons.stop : Icons.track_changes, size: 18),
            label: Text(busy ? 'Dừng ${(_progress * 100).round()}%' : 'Bám theo đối tượng'),
            onPressed: busy ? _stop : _start,
          ),
        ),
        const SizedBox(width: 8),
        FilterChip(
          key: const Key('follow-size'),
          label: const Text('cả kích thước', style: TextStyle(fontSize: 12)),
          visualDensity: VisualDensity.compact,
          selected: _size,
          onSelected: busy ? null : (v) => setState(() => _size = v),
        ),
      ]),
      if (busy) LinearProgressIndicator(value: _progress),
      if (_note != null)
        Padding(
          padding: const EdgeInsets.only(top: 4),
          child: Text(_note!, style: const TextStyle(fontSize: 12, color: VsColors.fg3)),
        ),
    ]);
  }
}
