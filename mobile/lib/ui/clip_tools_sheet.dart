// Clip tools on the phone (FR-015, FR-026, FR-028): play backwards, freeze the frame at the
// playhead, chroma key (key colour guessed from the preview, similarity / softness / spill)
// and stabilisation (measured in the background; strength re-smooths without measuring again).
import 'dart:async';

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

const _chromaKey = 'builtin.chroma_key';
const _stabilize = 'builtin.stabilize';

class ClipToolsSheet extends StatefulWidget {
  const ClipToolsSheet(
      {super.key, required this.vs, required this.clipId, required this.playhead, required this.onEdit, this.onFrozen});
  final VStudio vs;
  final String clipId;
  final double playhead;
  final void Function(void Function(VStudio vs) edit) onEdit;

  /// The freeze clip's id, to select it.
  final void Function(String clipId)? onFrozen;

  @override
  State<ClipToolsSheet> createState() => _ClipToolsSheetState();
}

class _ClipToolsSheetState extends State<ClipToolsSheet> {
  bool _reversed = false, _freeze = false;
  Map<String, dynamic>? _key, _stab;
  double _similarity = 0.4, _smoothness = 0.1, _spill = 0.5, _strength = 0.7, _freezeSecs = 3, _progress = 0;
  String _color = '#00ff00';
  Timer? _poll;
  String? _note;

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void dispose() {
    _poll?.cancel();
    super.dispose();
  }

  void _load() {
    try {
      final s = widget.vs.clipSound(widget.clipId);
      _reversed = s.reversed;
      _freeze = s.freeze;
      _key = widget.vs.effect(widget.clipId, _chromaKey);
      _stab = widget.vs.effect(widget.clipId, _stabilize);
    } on VStudioException catch (e) {
      _note = e.message;
    }
    final k = _key;
    if (k != null) {
      _color = k['color'] as String? ?? _color;
      _similarity = (k['similarity'] as num?)?.toDouble() ?? _similarity;
      _smoothness = (k['smoothness'] as num?)?.toDouble() ?? _smoothness;
      _spill = (k['spill'] as num?)?.toDouble() ?? _spill;
    }
    _strength = (_stab?['strength'] as num?)?.toDouble() ?? _strength;
    if (mounted) setState(() {});
  }

  void _edit(void Function(VStudio vs) f) {
    widget.onEdit(f);
    _load();
  }

  void _commitKey(bool on) => _edit((vs) => vs.setEffect([widget.clipId], _chromaKey, on
      ? {'color': _color, 'similarity': _similarity, 'smoothness': _smoothness, 'spill': _spill}
      : null));

  void _startStabilize() {
    try {
      widget.vs.startToolJob('stabilize', widget.clipId, _strength);
    } on VStudioException catch (e) {
      setState(() => _note = e.message);
      return;
    }
    setState(() {
      _note = 'Đang đo độ rung…';
      _progress = 0;
    });
    _poll = Timer.periodic(const Duration(milliseconds: 200), (t) {
      final (st, p, err) = widget.vs.toolJobStatus();
      if (st == 1) {
        setState(() => _progress = p);
        return;
      }
      t.cancel();
      widget.onEdit((_) {});
      _load();
      setState(() => _note = st == 2 ? 'Đã chống rung' : st == -2 ? 'Đã hủy' : 'Không đo được: $err');
    });
  }

  Color get _swatch => Color(int.parse('ff${_color.substring(1)}', radix: 16));

  @override
  Widget build(BuildContext context) {
    final busy = _poll?.isActive == true;
    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const Text('Công cụ clip', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Phát ngược'),
            value: _reversed,
            onChanged: _freeze ? null : (v) => _edit((vs) => vs.setReversed([widget.clipId], v)),
          ),
          Row(children: [
            Expanded(
              child: OutlinedButton.icon(
                icon: const Icon(Icons.pause_presentation),
                label: Text('Đóng băng khung ${_freezeSecs.toStringAsFixed(1)} s'),
                onPressed: _freeze
                    ? null
                    : () {
                        String? id;
                        widget.onEdit((vs) => id = vs.freezeFrame(widget.clipId, widget.playhead, seconds: _freezeSecs));
                        if (id != null) {
                          widget.onFrozen?.call(id!);
                          Navigator.of(context).maybePop();
                        }
                      },
              ),
            ),
            SizedBox(
              width: 120,
              child: Slider(
                  value: _freezeSecs, min: 0.5, max: 10, onChanged: (v) => setState(() => _freezeSecs = v)),
            ),
          ]),
          const Divider(height: 24),
          Row(children: [
            const Expanded(child: Text('Tách nền màu (chroma key)', style: TextStyle(color: VsColors.fg2))),
            Switch(value: _key != null, onChanged: _commitKey),
          ]),
          Row(children: [
            Container(
              width: 28,
              height: 28,
              decoration: BoxDecoration(color: _swatch, borderRadius: BorderRadius.circular(6)),
            ),
            const SizedBox(width: 8),
            Text(_color, style: const TextStyle(color: VsColors.fg3)),
          ]),
          Wrap(spacing: 4, children: [
            for (final c in ['#00ff00', '#0047bb'])
              TextButton(
                onPressed: () {
                  _color = c;
                  _commitKey(true);
                },
                child: Text(c == '#00ff00' ? 'Xanh lá' : 'Xanh dương'),
              ),
            TextButton(
              onPressed: () {
                _color = widget.vs.guessKeyColor();
                _commitKey(true);
              },
              child: const Text('Tự nhận'),
            ),
          ]),
          if (_key != null) ...[
            _slider('Tương đồng', _similarity, (v) => _similarity = v),
            _slider('Làm mềm viền', _smoothness, (v) => _smoothness = v),
            _slider('Khử ám màu', _spill, (v) => _spill = v),
          ],
          const Divider(height: 24),
          const Text('Chống rung', style: TextStyle(color: VsColors.fg2)),
          Row(children: [
            Expanded(
              child: Slider(
                value: _strength,
                onChanged: (v) => setState(() => _strength = v),
                onChangeEnd: (v) {
                  if (_stab != null) _edit((vs) => vs.setStabilizeStrength(widget.clipId, v));
                },
              ),
            ),
            if (busy)
              TextButton(onPressed: () => widget.vs.cancelToolJob(), child: const Text('Hủy'))
            else if (_stab != null)
              TextButton(onPressed: () => _edit((vs) => vs.setEffect([widget.clipId], _stabilize, null)), child: const Text('Bỏ'))
            else
              FilledButton(onPressed: _startStabilize, child: const Text('Chống rung')),
          ]),
          if (busy) LinearProgressIndicator(value: _progress),
          if (_note != null)
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: Text(_note!, style: const TextStyle(color: VsColors.fg3, fontSize: 12)),
            ),
        ]),
      ),
    );
  }

  Widget _slider(String label, double value, void Function(double) set) => Row(children: [
        SizedBox(width: 96, child: Text(label, style: const TextStyle(color: VsColors.fg2, fontSize: 13))),
        Expanded(
          child: Slider(
            value: value,
            onChanged: (v) => setState(() => set(v)),
            onChangeEnd: (_) => _commitKey(true),
          ),
        ),
      ]);
}
