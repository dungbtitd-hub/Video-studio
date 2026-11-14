// One-tap colour looks on mobile: a strip of previews rendered by the core on
// the selected clip's own frame. Tapping applies at once (one undo step);
// strength is committed when the finger lifts from the slider. Above the strip:
// the clip's Log/HDR input (camera Log -> Rec.709) and an optional .cube LUT.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../core/project_model.dart';
import '../core/vstudio.dart';
import '../core/favorites.dart';
import 'theme.dart';

class LooksSheet extends StatefulWidget {
  const LooksSheet({
    super.key,
    required this.vs,
    required this.clip,
    required this.onApply,
    required this.onApplyAll,
    this.onInput,
    this.onPickLut,
    this.onClearLut,
    this.myPresets,
  });

  final VStudio vs;
  final ClipView clip;
  final void Function(String lookId, double strength) onApply; // '' removes
  final void Function(String lookId, double strength) onApplyAll;
  final void Function(String input)? onInput; // '' = normal Rec.709
  final Future<String?> Function()? onPickLut; // returns the applied LUT path, or null
  final VoidCallback? onClearLut;

  /// "Của tôi": the user's saved styles (user_presets.dart), under the buttons.
  final Widget? myPresets;

  @override
  State<LooksSheet> createState() => _LooksSheetState();
}

class _LooksSheetState extends State<LooksSheet> {
  static const tileW = 88, tileH = 50;
  late final List<LookInfo> _looks = widget.vs.looks();
  final Map<String, ui.Image> _previews = {};
  late String _current = widget.clip.look ?? '';
  late double _strength = widget.clip.look == null ? 1 : widget.clip.lookStrength;
  late final List<LogFormatInfo> _inputs = widget.vs.logFormats();
  late String _input = widget.clip.colorSpace ?? '';
  late String? _lut = widget.clip.lut;
  late final String _suggested = widget.vs.suggestedLogFormat(widget.clip.assetId);

  @override
  void initState() {
    super.initState();
    _renderPreviews();
  }

  @override
  void dispose() {
    for (final i in _previews.values) {
      i.dispose();
    }
    super.dispose();
  }

  /// Middle frame of the clip's thumbnail strip (scaled to a tile), or a sample picture.
  (Uint8List, int, int) _base() {
    final t = widget.vs.thumbnails(widget.clip.assetId);
    if (t != null && t.count > 0) {
      final frame = t.count ~/ 2;
      final out = Uint8List(tileW * tileH * 4);
      for (var y = 0; y < tileH; y++) {
        final sy = y * t.height ~/ tileH;
        for (var x = 0; x < tileW; x++) {
          final sx = x * t.width ~/ tileW;
          final s = ((frame * t.height + sy) * t.width + sx) * 4, d = (y * tileW + x) * 4;
          out.setRange(d, d + 4, t.rgba, s);
        }
      }
      return (out, tileW, tileH);
    }
    final out = Uint8List(tileW * tileH * 4);
    for (var y = 0; y < tileH; y++) {
      for (var x = 0; x < tileW; x++) {
        final d = (y * tileW + x) * 4, sky = y < tileH * 0.6;
        out[d] = sky ? 80 + y * 3 : 60;
        out[d + 1] = sky ? 130 + y : 120;
        out[d + 2] = sky ? 200 - y : 60;
        out[d + 3] = 255;
      }
    }
    return (out, tileW, tileH);
  }

  void _renderPreviews() {
    final (base, w, h) = _base();
    void decode(String id, Uint8List px) => ui.decodeImageFromPixels(px, w, h, ui.PixelFormat.rgba8888, (img) {
          if (!mounted) return img.dispose();
          setState(() => _previews[id] = img);
        });
    decode('', base);
    for (final l in _looks) {
      final px = widget.vs.lookPreview(l.id, base, w, h);
      if (px != null) decode(l.id, px);
    }
  }

  void _tap(String id) {
    setState(() => _current = id);
    widget.onApply(id, _strength);
  }

  Widget _tile(String id, String name) {
    final selected = id == _current;
    final img = _previews[id];
    final fav = id.isNotEmpty && FavoritesStore.instance.contains('look', id);
    return GestureDetector(
      onTap: () => _tap(id),
      // Long press: add to / remove from favourites (listed first).
      onLongPress: id.isEmpty ? null : () => setState(() => FavoritesStore.instance.toggle('look', id)),
      child: Container(
        width: tileW + 8.0,
        margin: const EdgeInsets.symmetric(horizontal: 3),
        child: Column(children: [
          Container(
            width: tileW.toDouble(),
            height: tileH.toDouble(),
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(6),
              border: Border.all(color: selected ? VsColors.accent : Colors.transparent, width: 2),
            ),
            clipBehavior: Clip.antiAlias,
            child: img == null ? const ColoredBox(color: VsColors.panel2) : RawImage(image: img, fit: BoxFit.cover),
          ),
          const SizedBox(height: 4),
          Text(fav ? '★ $name' : name,
              maxLines: 2,
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 11, color: selected ? VsColors.accent : VsColors.fg2)),
        ]),
      ),
    );
  }

  /// Log/HDR input and LUT: flat-looking Log footage needs these before any look.
  Widget _sourceRow() {
    final hint = _input.isEmpty && _suggested.isNotEmpty
        ? _inputs.where((f) => f.id == _suggested).map((f) => f.name).firstOrNull
        : null;
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 0, 12, 6),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const Text('Đầu vào', style: TextStyle(color: VsColors.fg2, fontSize: 12)),
          const SizedBox(width: 8),
          Expanded(
            child: DropdownButton<String>(
              key: const Key('log-input'),
              isExpanded: true,
              value: _input,
              style: const TextStyle(fontSize: 12, color: VsColors.fg),
              dropdownColor: VsColors.panel2,
              items: [
                const DropdownMenuItem(value: '', child: Text('Bình thường (Rec.709)')),
                for (final f in _inputs)
                  DropdownMenuItem(value: f.id, child: Text(f.name, overflow: TextOverflow.ellipsis)),
              ],
              onChanged: (v) {
                setState(() => _input = v ?? '');
                widget.onInput!(_input);
              },
            ),
          ),
          const SizedBox(width: 8),
          Flexible(
            child: TextButton(
              onPressed: widget.onPickLut == null
                  ? null
                  : () async {
                      final p = await widget.onPickLut!();
                      if (p != null && mounted) setState(() => _lut = p);
                    },
              child: Text(_lut == null ? '+ LUT' : 'LUT: ${_lut!.split('/').last.replaceAll('.cube', '')}',
                  maxLines: 1, overflow: TextOverflow.ellipsis),
            ),
          ),
          if (_lut != null)
            IconButton(
              tooltip: 'Bỏ LUT',
              icon: const Icon(Icons.close, size: 16),
              onPressed: () {
                setState(() => _lut = null);
                widget.onClearLut?.call();
              },
            ),
        ]),
        if (hint != null)
          Text('File này là $hint: chọn ở "Đầu vào" để hiện màu đúng.',
              style: const TextStyle(color: Color(0xFFE0B050), fontSize: 11)),
      ]),
    );
  }

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.only(top: 12, bottom: 8),
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          if (widget.onInput != null) _sourceRow(),
          SizedBox(
            height: tileH + 44,
            child: ListView(
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: 8),
              children: [
                _tile('', 'Gốc'),
                for (final l in FavoritesStore.instance.ordered('look', _looks, (LookInfo x) => x.id)) _tile(l.id, l.name),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Row(children: [
              const Text('Độ mạnh', style: TextStyle(color: VsColors.fg2, fontSize: 12)),
              Expanded(
                child: Slider(
                  value: _strength,
                  onChanged: _current.isEmpty ? null : (v) => setState(() => _strength = v),
                  onChangeEnd: _current.isEmpty ? null : (v) => widget.onApply(_current, v),
                ),
              ),
              Text('${(_strength * 100).round()}%', style: const TextStyle(color: VsColors.fg2, fontSize: 12)),
            ]),
          ),
          Row(children: [
            for (final (label, action) in [
              ('Áp dụng cho tất cả', () => widget.onApplyAll(_current, _strength)),
              ('Bỏ bộ lọc', () => _tap('')),
            ])
              Expanded(
                child: TextButton(
                  onPressed: _current.isEmpty ? null : action,
                  child: Text(label, maxLines: 1, overflow: TextOverflow.ellipsis),
                ),
              ),
          ]),
          if (widget.myPresets != null) widget.myPresets!,
        ]),
      ),
    );
  }
}
