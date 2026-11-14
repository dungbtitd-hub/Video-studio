// Video effects on the phone: zoom, rotate and reflections (water, lake, mirror...) for the
// selected clip. Three groups, one effect of each per clip. The thumbnails show each effect on
// the clip's own frame; the chosen one plays in its tile so the motion can be judged. A tap
// applies at once (one undo step); the sliders commit when the finger lifts.
import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../core/project_model.dart';
import '../core/vstudio.dart';
import 'theme.dart';

/// What the sheet asks for: [fx] '' removes the group's effect; [line] < 0 is the default.
typedef VideoFxSetter = void Function(String group, String fx, double amount, double speed, double line);

class VideoFxSheet extends StatefulWidget {
  const VideoFxSheet({super.key, required this.vs, required this.clip, required this.onSet});
  final VStudio vs;
  final ClipView clip;
  final VideoFxSetter onSet;

  @override
  State<VideoFxSheet> createState() => _VideoFxSheetState();
}

class _VideoFxSheetState extends State<VideoFxSheet> {
  static const tileW = 88, tileH = 50;
  static const groups = ['zoom', 'rotate', 'reflect'];
  late final List<VideoFxInfo> _all = widget.vs.videoFx();
  late final Map<String, Map<String, dynamic>> _on = {
    for (final e in widget.clip.videoFx.entries) e.key: Map.of(e.value)
  };
  late String _group = groups.firstWhere(_on.containsKey, orElse: () => 'zoom');
  final Map<String, ui.Image> _thumbs = {};
  ui.Image? _playing; // the chosen effect's tile, moving
  Timer? _timer;
  double _t = 1.6;
  bool _decoding = false;
  (Uint8List, int, int)? _picture;
  late double _amount, _speed, _line;

  @override
  void initState() {
    super.initState();
    _picture = _clipFrame();
    _loadGroup();
  }

  @override
  void dispose() {
    _timer?.cancel();
    for (final i in [..._thumbs.values, if (_playing != null) _playing!]) {
      i.dispose();
    }
    super.dispose();
  }

  /// The middle frame of the clip's thumbnail strip, or null (a built-in landscape is used).
  (Uint8List, int, int)? _clipFrame() {
    final t = widget.vs.thumbnails(widget.clip.assetId);
    if (t == null || t.count == 0) return null;
    final size = t.width * t.height * 4, i = t.count ~/ 2;
    return (Uint8List.sublistView(t.rgba, i * size, (i + 1) * size), t.width, t.height);
  }

  Uint8List? _icon(String id, double seconds) {
    final p = _picture;
    return widget.vs.videoFxIcon(id, seconds, tileW, tileH, picture: p?.$1, pw: p?.$2 ?? 0, ph: p?.$3 ?? 0);
  }

  List<VideoFxInfo> get _shown => _all.where((f) => f.group == _group).toList();
  String get _current => (_on[_group]?['fx'] ?? '') as String;
  double _param(String k, double d) => ((_on[_group]?[k] ?? d) as num).toDouble();
  double _defaultLine(String fx) => fx == 'reflect_sky' ? 0.4 : 0.62;

  void _loadGroup() {
    final fx = _current;
    _amount = _param('amount', 1);
    _speed = _param('speed', 1);
    final line = _param('line', -1);
    _line = line >= 0 ? line : _defaultLine(fx);
    for (final f in _shown) {
      if (_thumbs.containsKey(f.id)) continue;
      final px = _icon(f.id, 1.6);
      if (px == null) continue;
      ui.decodeImageFromPixels(px, tileW, tileH, ui.PixelFormat.rgba8888, (img) {
        if (!mounted) return img.dispose();
        setState(() => _thumbs[f.id] = img);
      });
    }
    _play();
  }

  /// Plays the chosen effect in its tile (a 4 s loop, about 12 frames a second).
  void _play() {
    _timer?.cancel();
    _timer = null;
    final fx = _current;
    if (fx.isEmpty || !(_all.where((f) => f.id == fx).firstOrNull?.animated ?? false)) {
      if (_playing != null) {
        setState(() {
          _playing!.dispose();
          _playing = null;
        });
      }
      return;
    }
    _t = 1.6;
    _timer = Timer.periodic(const Duration(milliseconds: 80), (_) {
      if (_decoding) return; // never queue frames: skip one instead
      _t = (_t + 0.08 * _speed) % 4.0;
      final px = _icon(fx, _t);
      if (px == null) return;
      _decoding = true;
      ui.decodeImageFromPixels(px, tileW, tileH, ui.PixelFormat.rgba8888, (img) {
        _decoding = false;
        if (!mounted || _current != fx) return img.dispose();
        setState(() {
          _playing?.dispose();
          _playing = img;
        });
      });
    });
  }

  void _set(String fx) {
    final info = _all.where((f) => f.id == fx).firstOrNull;
    setState(() {
      if (fx.isEmpty) {
        _on.remove(_group);
      } else {
        // A new effect starts from its defaults; the sliders then tune it.
        _amount = 1;
        _speed = 1;
        _line = _defaultLine(fx);
        _on[_group] = {'fx': fx, 'amount': 1.0, 'speed': 1.0, if (info?.usesLine ?? false) 'line': _line};
      }
    });
    widget.onSet(_group, fx, 1, 1, info?.usesLine ?? false ? _line : -1);
    _play();
  }

  void _commitSliders() {
    final fx = _current;
    if (fx.isEmpty) return;
    final line = _group == 'reflect' ? _line : -1.0;
    _on[_group] = {'fx': fx, 'amount': _amount, 'speed': _speed, if (line >= 0) 'line': line};
    widget.onSet(_group, fx, _amount, _speed, line);
  }

  Widget _tile(VideoFxInfo f) {
    final on = f.id == _current;
    final img = on && _playing != null ? _playing : _thumbs[f.id];
    return GestureDetector(
      key: Key('vfx-${f.id}'),
      onTap: on ? null : () => _set(f.id),
      child: Container(
        width: tileW + 8.0,
        margin: const EdgeInsets.symmetric(horizontal: 3),
        child: Column(children: [
          AnimatedContainer(
            duration: const Duration(milliseconds: 160),
            width: tileW.toDouble(),
            height: tileH.toDouble(),
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(6),
              border: Border.all(color: on ? VsColors.accent : Colors.transparent, width: 2),
            ),
            clipBehavior: Clip.antiAlias,
            child: img == null ? const ColoredBox(color: VsColors.panel2) : RawImage(image: img, fit: BoxFit.cover),
          ),
          const SizedBox(height: 4),
          Text(f.name,
              maxLines: 2,
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 11, color: on ? VsColors.accent : VsColors.fg2)),
        ]),
      ),
    );
  }

  Widget _slider(String label, String key, double min, double max, double value, ValueChanged<double> set,
      String Function(double) text) {
    final enabled = _current.isNotEmpty;
    return Row(children: [
      SizedBox(width: 64, child: Text(label, style: const TextStyle(color: VsColors.fg2, fontSize: 12))),
      Expanded(
        child: Slider(
          key: Key(key),
          min: min,
          max: max,
          value: value.clamp(min, max).toDouble(),
          onChanged: enabled ? (v) => setState(() => set(v)) : null,
          onChangeEnd: enabled ? (_) => _commitSliders() : null,
        ),
      ),
      SizedBox(width: 44, child: Text(text(value), style: const TextStyle(color: VsColors.fg2, fontSize: 12))),
    ]);
  }

  @override
  Widget build(BuildContext context) {
    final names = {for (final f in _all) f.group: f.groupName};
    final using = [
      for (final g in groups)
        if (_all.where((f) => f.id == _on[g]?['fx']).firstOrNull case final f?) f.name
    ];
    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.only(top: 10, bottom: 8),
        child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Wrap(spacing: 8, children: [
              for (final g in groups)
                ChoiceChip(
                  key: Key('vfx-group-$g'),
                  label: Text(names[g] ?? g),
                  selected: g == _group,
                  onSelected: (_) {
                    if (g == _group) return;
                    setState(() => _group = g);
                    _loadGroup();
                  },
                ),
            ]),
          ),
          const SizedBox(height: 8),
          SizedBox(
            height: tileH + 44,
            child: ListView(
              key: const Key('vfx-list'),
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: 8),
              children: [for (final f in _shown) _tile(f)],
            ),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Column(children: [
              _slider('Mức độ', 'vfx-amount', 0, 2, _amount, (v) => _amount = v, (v) => '${(v * 100).round()}%'),
              _slider('Tốc độ', 'vfx-speed', 0.25, 4, _speed, (v) => _speed = v, (v) => '${v.toStringAsFixed(2)}x'),
              if (_group == 'reflect')
                _slider('Mặt nước', 'vfx-line', 0.1, 0.9, _line, (v) => _line = v, (v) => '${(v * 100).round()}%'),
            ]),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Row(children: [
              Expanded(
                child: Text(using.isEmpty ? '' : 'Đang dùng: ${using.join(' · ')}',
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(color: VsColors.fg2, fontSize: 11)),
              ),
              TextButton(
                key: const Key('vfx-remove'),
                onPressed: _current.isEmpty ? null : () => _set(''),
                child: const Text('Bỏ hiệu ứng nhóm này'),
              ),
            ]),
          ),
        ]),
      ),
    );
  }
}
