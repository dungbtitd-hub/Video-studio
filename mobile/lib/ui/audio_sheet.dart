// Sound on the phone (FR-011, FR-025): the selected clip's volume, fade in/out, equaliser
// presets, noise reduction (measured in the background) and "Tách âm thanh"; below it a small
// mixer with every track that carries sound (volume, pan, mute, solo). Sliders commit when
// the finger lifts (one undo step each).
import 'dart:async';

import 'package:flutter/material.dart';

import '../core/project_model.dart';
import '../core/vstudio.dart';
import 'theme.dart';

class AudioSheet extends StatefulWidget {
  const AudioSheet({super.key, required this.vs, required this.clipId, required this.onEdit});
  final VStudio vs;
  final String? clipId;

  /// Runs an edit through the app (refresh, autosave, error message).
  final void Function(void Function(VStudio vs) edit) onEdit;

  @override
  State<AudioSheet> createState() => _AudioSheetState();
}

class _AudioSheetState extends State<AudioSheet> {
  late final List<EqPresetInfo> _presets = widget.vs.eqPresets();
  ClipSound? _sound;
  double _volume = 1, _fadeIn = 0, _fadeOut = 0, _denoise = 0.6;
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
    final id = widget.clipId;
    ClipSound? s;
    if (id != null) {
      try {
        s = widget.vs.clipSound(id);
      } on VStudioException {
        s = null;
      }
    }
    setState(() {
      _sound = s;
      if (s != null) {
        _volume = s.volume;
        _fadeIn = s.fadeIn;
        _fadeOut = s.fadeOut;
        _denoise = (s.denoise?['amount'] as num?)?.toDouble() ?? _denoise;
      }
    });
  }

  void _edit(void Function(VStudio vs) f) {
    widget.onEdit(f);
    _load();
  }

  String get _eqPreset {
    final eq = _sound?.eq;
    if (eq == null) return 'flat';
    if (eq.containsKey('gains')) return 'custom';
    return eq['preset'] as String? ?? 'flat';
  }

  void _startDenoise() {
    final id = widget.clipId!;
    try {
      widget.vs.startToolJob('noise', id, _denoise);
    } on VStudioException catch (e) {
      setState(() => _note = e.message);
      return;
    }
    setState(() => _note = 'Đang đo tiếng ồn nền…');
    _poll = Timer.periodic(const Duration(milliseconds: 200), (t) {
      final (st, _, err) = widget.vs.toolJobStatus();
      if (st == 1) return;
      t.cancel();
      widget.onEdit((_) {}); // the result was applied by the core: refresh the app
      _load();
      setState(() => _note = st == 2 ? 'Đã khử tiếng ồn nền' : 'Không đo được: $err');
    });
  }

  @override
  Widget build(BuildContext context) {
    final s = _sound;
    final project = widget.vs.project();
    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const Text('Âm thanh', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
          if (s == null)
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 8),
              child: Text('Chọn một clip có âm thanh để chỉnh fade, EQ, khử ồn.', style: TextStyle(color: VsColors.fg3)),
            )
          else ...[
            _row('Âm lượng', '${(_volume * 100).round()}%', Slider(
              value: _volume.clamp(0, 4),
              max: 4,
              onChanged: (v) => setState(() => _volume = v),
              onChangeEnd: (v) => _edit((vs) => vs.setClipVolume(widget.clipId!, v)),
            )),
            _row('Hiện dần', '${_fadeIn.toStringAsFixed(1)} s', Slider(
              value: _fadeIn.clamp(0, 10),
              max: 10,
              onChanged: (v) => setState(() => _fadeIn = v),
              onChangeEnd: (v) => _edit((vs) => vs.setClipAudio([widget.clipId!], {'fade_in': v})),
            )),
            _row('Tắt dần', '${_fadeOut.toStringAsFixed(1)} s', Slider(
              value: _fadeOut.clamp(0, 10),
              max: 10,
              onChanged: (v) => setState(() => _fadeOut = v),
              onChangeEnd: (v) => _edit((vs) => vs.setClipAudio([widget.clipId!], {'fade_out': v})),
            )),
            const SizedBox(height: 6),
            const Text('Bộ cân bằng (EQ)', style: TextStyle(color: VsColors.fg2)),
            const SizedBox(height: 6),
            Wrap(spacing: 6, runSpacing: 6, children: [
              for (final p in _presets)
                ChoiceChip(
                  label: Text(p.name),
                  selected: _eqPreset == p.id,
                  onSelected: (_) => _edit((vs) =>
                      vs.setClipAudio([widget.clipId!], {'eq': p.id == 'flat' ? null : {'preset': p.id}})),
                ),
              if (_eqPreset == 'custom') const ChoiceChip(label: Text('Tùy chỉnh'), selected: true),
            ]),
            const SizedBox(height: 10),
            Row(children: [
              const Text('Khử tiếng ồn', style: TextStyle(color: VsColors.fg2)),
              Expanded(
                child: Slider(
                  value: _denoise,
                  onChanged: (v) => setState(() => _denoise = v),
                  onChangeEnd: (v) {
                    final d = s.denoise;
                    if (d != null) _edit((vs) => vs.setClipAudio([widget.clipId!], {'denoise': {...d, 'amount': v}}));
                  },
                ),
              ),
              _poll?.isActive == true
                  ? const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
                  : TextButton(
                      onPressed: s.denoise != null
                          ? () => _edit((vs) => vs.setClipAudio([widget.clipId!], {'denoise': null}))
                          : _startDenoise,
                      child: Text(s.denoise != null ? 'Bỏ' : 'Khử ồn'),
                    ),
            ]),
            const SizedBox(height: 4),
            OutlinedButton.icon(
              icon: const Icon(Icons.call_split),
              label: Text(s.detached ? 'Âm thanh đã tách ra track riêng' : 'Tách âm thanh ra track riêng'),
              onPressed: s.detached ? null : () => _edit((vs) => vs.detachAudio(widget.clipId!)),
            ),
          ],
          if (_note != null)
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: Text(_note!, style: const TextStyle(color: VsColors.fg3, fontSize: 12)),
            ),
          const Divider(height: 24),
          const Text('Mixer', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
          for (final t in project.tracks.where((t) => t.audio || t.clips.any((c) => !c.generated)))
            _MixerStrip(track: t, onEdit: _edit),
        ]),
      ),
    );
  }

  Widget _row(String label, String value, Widget slider) => Row(children: [
        SizedBox(width: 72, child: Text(label, style: const TextStyle(color: VsColors.fg2))),
        Expanded(child: slider),
        SizedBox(width: 48, child: Text(value, textAlign: TextAlign.right, style: const TextStyle(color: VsColors.fg3))),
      ]);
}

class _MixerStrip extends StatefulWidget {
  const _MixerStrip({required this.track, required this.onEdit});
  final TrackView track;
  final void Function(void Function(VStudio vs) edit) onEdit;

  @override
  State<_MixerStrip> createState() => _MixerStripState();
}

class _MixerStripState extends State<_MixerStrip> {
  late double _volume = widget.track.volume, _pan = widget.track.pan;

  @override
  void didUpdateWidget(_MixerStrip old) {
    super.didUpdateWidget(old);
    _volume = widget.track.volume;
    _pan = widget.track.pan;
  }

  @override
  Widget build(BuildContext context) {
    final t = widget.track;
    return Padding(
      padding: const EdgeInsets.only(top: 6),
      child: Row(children: [
        SizedBox(width: 72, child: Text(t.name, overflow: TextOverflow.ellipsis)),
        Expanded(
          flex: 3,
          child: Slider(
            value: _volume.clamp(0, 2),
            max: 2,
            onChanged: (v) => setState(() => _volume = v),
            onChangeEnd: (v) => widget.onEdit((vs) => vs.setTrackVolume(t.id, v)),
          ),
        ),
        Expanded(
          flex: 2,
          child: Slider(
            value: _pan,
            min: -1,
            max: 1,
            onChanged: (v) => setState(() => _pan = v),
            onChangeEnd: (v) => widget.onEdit((vs) => vs.setTrackMix(t.id, pan: v, solo: t.solo)),
          ),
        ),
        _Toggle(label: 'M', on: t.muted, onTap: () => widget.onEdit((vs) =>
            vs.setTrackFlags(t.id, enabled: t.enabled, locked: t.locked, muted: !t.muted))),
        _Toggle(label: 'S', on: t.solo, onTap: () => widget.onEdit((vs) => vs.setTrackMix(t.id, pan: _pan, solo: !t.solo))),
      ]),
    );
  }
}

class _Toggle extends StatelessWidget {
  const _Toggle({required this.label, required this.on, required this.onTap});
  final String label;
  final bool on;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(left: 4),
        child: InkWell(
          onTap: onTap,
          child: Container(
            width: 30,
            height: 30,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: on ? VsColors.accent : VsColors.raised,
              borderRadius: BorderRadius.circular(6),
            ),
            child: Text(label, style: TextStyle(color: on ? Colors.black : VsColors.fg2, fontWeight: FontWeight.w600)),
          ),
        ),
      );
}
