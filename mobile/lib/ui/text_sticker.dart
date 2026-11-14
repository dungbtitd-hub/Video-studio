// Text and stickers on the phone: the "Chữ" and "Sticker" tools open a sheet to
// type, style and animate; the selected text or sticker gets a box on the preview
// to drag, pinch or resize (committed as one edit when the fingers lift).
// Everything is drawn by the core, so what the sheet shows is what exports.
import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

/// RFC 7386 merge patch, as the core applies it (null removes a key).
Map<String, dynamic> mergePatch(Map<String, dynamic> target, Map<String, dynamic> patch) {
  final out = Map<String, dynamic>.of(target);
  for (final e in patch.entries) {
    if (e.value == null) {
      out.remove(e.key);
    } else if (e.value is Map<String, dynamic>) {
      final now = out[e.key];
      out[e.key] = mergePatch(now is Map<String, dynamic> ? now : const {}, e.value as Map<String, dynamic>);
    } else {
      out[e.key] = e.value;
    }
  }
  return out;
}

/// Images rendered once and kept for the app's lifetime (sticker icons, style tiles).
class _ImageCache {
  static final Map<String, ui.Image> images = {};
  static final Set<String> pending = {};
}

/// A small image drawn by the core on first use; [render] returns RGBA [w] x [h].
class CoreImage extends StatefulWidget {
  const CoreImage({super.key, required this.cacheKey, required this.w, required this.h, required this.render});
  final String cacheKey;
  final int w, h;
  final Uint8List? Function() render;

  @override
  State<CoreImage> createState() => _CoreImageState();
}

class _CoreImageState extends State<CoreImage> {
  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void didUpdateWidget(CoreImage old) {
    super.didUpdateWidget(old);
    if (old.cacheKey != widget.cacheKey) _load();
  }

  void _load() {
    final key = widget.cacheKey;
    if (_ImageCache.images.containsKey(key) || _ImageCache.pending.contains(key)) return;
    _ImageCache.pending.add(key);
    final px = widget.render();
    if (px == null) return;
    ui.decodeImageFromPixels(px, widget.w, widget.h, ui.PixelFormat.rgba8888, (img) {
      _ImageCache.pending.remove(key);
      _ImageCache.images[key] = img;
      if (mounted) setState(() {});
    });
  }

  @override
  Widget build(BuildContext context) {
    final img = _ImageCache.images[widget.cacheKey];
    return img == null ? const SizedBox.expand() : RawImage(image: img, fit: BoxFit.contain);
  }
}

// ---------------------------------------------------------------- animations

/// In / out / loop choice for a text or sticker, with a small preview that plays
/// in → about 1.5 s at rest (loops show) → out, over and over.
class AnimationPicker extends StatefulWidget {
  const AnimationPicker({super.key, required this.vs, required this.content, required this.onPatch});
  final VStudio vs;
  final Map<String, dynamic> content;
  final ValueChanged<Map<String, dynamic>> onPatch;

  @override
  State<AnimationPicker> createState() => _AnimationPickerState();
}

class _AnimationPickerState extends State<AnimationPicker> {
  static const pw = 192, ph = 108;
  late final TextCatalog _cat = widget.vs.textCatalog();
  String _which = 'in';
  double _t = 0;
  ui.Image? _frame;
  bool _busy = false;
  Timer? _timer;
  double? _dragLen; // duration slider while dragging

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(const Duration(milliseconds: 40), (_) => _tick());
  }

  @override
  void didUpdateWidget(AnimationPicker old) {
    super.didUpdateWidget(old);
    for (final k in const ['in', 'out', 'loop']) {
      if (_spec(old.content, k)['type'] != _spec(widget.content, k)['type']) _t = 0; // replay the change
    }
  }

  @override
  void dispose() {
    _timer?.cancel();
    _frame?.dispose();
    super.dispose();
  }

  static Map<String, dynamic> _spec(Map<String, dynamic> c, String k) {
    final s = c[k];
    return s is Map<String, dynamic> ? s : const {};
  }

  double _len(String k, double def) {
    final s = _spec(widget.content, k);
    return ((s['type'] ?? '') as String).isEmpty ? 0 : ((s['duration'] ?? def) as num).toDouble();
  }

  void _tick() {
    if (_busy) return; // the last frame is still being decoded
    final length = _len('in', 0.6) + 1.5 + _len('out', 0.5);
    _t += 0.04;
    if (_t > length + 0.5) _t = 0;
    final px = widget.vs.contentPreview(widget.content, math.min(_t, length), length, pw, ph);
    if (px == null) return;
    _busy = true;
    ui.decodeImageFromPixels(px, pw, ph, ui.PixelFormat.rgba8888, (img) {
      _busy = false;
      if (!mounted) return img.dispose();
      setState(() {
        _frame?.dispose();
        _frame = img;
      });
    });
  }

  @override
  Widget build(BuildContext context) {
    final spec = _spec(widget.content, _which);
    final current = (spec['type'] ?? '') as String;
    final list = _which == 'loop' ? _cat.loops : _cat.animations;
    final def = _which == 'out' ? 0.5 : 0.6;
    final len = _dragLen ?? ((spec['duration'] ?? def) as num).toDouble();
    void pick(String type) {
      final s = <String, dynamic>{'type': type};
      if (_which == 'loop') {
        s['speed'] = 1.0;
      } else {
        s['duration'] = len;
      }
      widget.onPatch({_which: s});
    }

    return Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      SegmentedButton<String>(
        key: const Key('anim-which'),
        showSelectedIcon: false,
        style: const ButtonStyle(visualDensity: VisualDensity.compact),
        segments: const [
          ButtonSegment(value: 'in', label: Text('Hiện vào')),
          ButtonSegment(value: 'out', label: Text('Biến mất')),
          ButtonSegment(value: 'loop', label: Text('Lặp lại')),
        ],
        selected: {_which},
        onSelectionChanged: (s) => setState(() => _which = s.first),
      ),
      const SizedBox(height: 6),
      Row(children: [
        Container(
          width: pw * 0.6,
          height: ph * 0.6,
          decoration: BoxDecoration(color: const Color(0xFF465C78), borderRadius: BorderRadius.circular(6)),
          clipBehavior: Clip.antiAlias,
          child: _frame == null ? null : RawImage(key: const Key('anim-preview'), image: _frame, fit: BoxFit.contain),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: _which == 'loop'
              ? const Text('Lặp suốt lúc chữ hoặc sticker đang hiện',
                  style: TextStyle(fontSize: 12, color: VsColors.fg3))
              : Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Text('Độ dài ${len.toStringAsFixed(1)} giây',
                      style: const TextStyle(fontSize: 12, color: VsColors.fg2)),
                  Slider(
                    key: const Key('anim-duration'),
                    min: 0.1,
                    max: 3,
                    divisions: 29,
                    value: len.clamp(0.1, 3).toDouble(),
                    onChanged: current.isEmpty ? null : (v) => setState(() => _dragLen = v),
                    onChangeEnd: (v) {
                      _dragLen = null;
                      widget.onPatch({
                        _which: {'type': current, 'duration': v}
                      });
                    },
                  ),
                ]),
        ),
      ]),
      const SizedBox(height: 6),
      SizedBox(
        height: 132,
        child: SingleChildScrollView(
          child: Wrap(spacing: 6, runSpacing: 2, children: [
            for (final a in [const TextAnimationInfo('', 'Không'), ...list])
              ChoiceChip(
                key: Key('anim-$_which-${a.id}'),
                label: Text(a.name, style: const TextStyle(fontSize: 12)),
                visualDensity: VisualDensity.compact,
                selected: a.id == current,
                onSelected: (_) => pick(a.id),
              ),
          ]),
        ),
      ),
    ]);
  }
}

// ---------------------------------------------------------------- text

const textSwatches = [
  '#ffffff', '#000000', '#ffd54a', '#ff8a3d', '#ff4d6d', '#ff5fd2', //
  '#b47cff', '#4d8dff', '#3fd0ff', '#3fe0a0', '#8bd346', '#8a5a3c',
];

/// The text tool: what it says, its look, how it comes and goes.
class TextSheet extends StatefulWidget {
  const TextSheet({super.key, required this.vs, required this.content, required this.onPatch, this.follow});
  final VStudio vs;
  final Map<String, dynamic> content;
  final ValueChanged<Map<String, dynamic>> onPatch; // one undo step each

  /// Shown under the animations: "Bám theo đối tượng" (FR-028).
  final Widget? follow;

  @override
  State<TextSheet> createState() => _TextSheetState();
}

class _TextSheetState extends State<TextSheet> {
  late final TextCatalog _cat = widget.vs.textCatalog();
  late Map<String, dynamic> _c = widget.content;
  late final TextEditingController _text = TextEditingController(text: (_c['text'] ?? '') as String);
  Timer? _typing;
  final Map<String, double> _dragging = {}; // slider values while dragging

  @override
  void dispose() {
    _commitText();
    _text.dispose();
    super.dispose();
  }

  void _patch(Map<String, dynamic> p) {
    setState(() => _c = mergePatch(_c, p));
    widget.onPatch(p);
  }

  void _commitText() {
    _typing?.cancel();
    _typing = null;
    if (_text.text != _c['text']) _patch({'text': _text.text});
  }

  double _sub(String key, String field) {
    final v = _c[key];
    return v is Map ? ((v[field] ?? 0) as num).toDouble() : 0;
  }

  Widget _slider(
      String label, String key, double value, double min, double max, Map<String, dynamic> Function(double) p,
      {int? divisions}) {
    final v = _dragging[key] ?? value;
    return Row(children: [
      SizedBox(width: 64, child: Text(label, style: const TextStyle(fontSize: 12, color: VsColors.fg2))),
      Expanded(
        child: Slider(
          key: Key('text-$key'),
          min: min,
          max: max,
          divisions: divisions,
          value: v.clamp(min, max).toDouble(),
          onChanged: (x) => setState(() => _dragging[key] = x),
          onChangeEnd: (x) {
            _dragging.remove(key);
            _patch(p(x));
          },
        ),
      ),
    ]);
  }

  Widget _textTab() {
    final font = (_c['font'] ?? 'be-vietnam-pro-bold') as String;
    return ListView(padding: const EdgeInsets.only(top: 8), children: [
      TextField(
        key: const Key('text-field'),
        controller: _text,
        minLines: 1,
        maxLines: 3,
        decoration: const InputDecoration(isDense: true, border: OutlineInputBorder(), hintText: 'Nhập chữ'),
        onChanged: (_) {
          _typing?.cancel();
          _typing = Timer(const Duration(milliseconds: 600), _commitText);
        },
        onSubmitted: (_) => _commitText(),
      ),
      const SizedBox(height: 8),
      SizedBox(
        height: 56,
        child: ListView(scrollDirection: Axis.horizontal, children: [
          for (final f in _cat.fonts)
            GestureDetector(
              key: Key('font-${f.id}'),
              onTap: () => _patch({'font': f.id}),
              child: Container(
                width: 112,
                margin: const EdgeInsets.only(right: 6),
                decoration: BoxDecoration(
                  color: VsColors.raised,
                  border: Border.all(color: f.id == font ? VsColors.accent : Colors.transparent, width: 2),
                  borderRadius: BorderRadius.circular(6),
                ),
                child: CoreImage(
                  cacheKey: 'font|${f.id}',
                  w: 224,
                  h: 96,
                  render: () => widget.vs.contentPreview({
                    ...widget.vs.defaultText('Chữ Việt'),
                    'font': f.id,
                    'in': const <String, dynamic>{},
                    'out': const <String, dynamic>{},
                    'stroke': {'width': 0},
                  }, 1, 2, 224, 96),
                ),
              ),
            ),
        ]),
      ),
      _slider('Cỡ chữ', 'size', ((_c['size'] ?? 96) as num).toDouble(), 16, 400, (v) => {'size': v.roundToDouble()}),
      Row(children: [
        for (final (a, icon) in const [
          ('left', Icons.format_align_left),
          ('center', Icons.format_align_center),
          ('right', Icons.format_align_right)
        ])
          IconButton(
            key: Key('align-$a'),
            isSelected: (_c['align'] ?? 'center') == a,
            icon: Icon(icon),
            onPressed: () => _patch({'align': a}),
          ),
        IconButton(
          key: const Key('italic'),
          isSelected: _c['italic'] == true,
          icon: const Icon(Icons.format_italic),
          onPressed: () => _patch({'italic': _c['italic'] != true}),
        ),
      ]),
    ]);
  }

  Widget _styleTab() {
    final color = ((_c['color'] ?? '#ffffff') as String).toLowerCase();
    return ListView(padding: const EdgeInsets.only(top: 8), children: [
      SizedBox(
        height: 64,
        child: ListView(scrollDirection: Axis.horizontal, children: [
          for (final p in _cat.presets)
            GestureDetector(
              key: Key('preset-${p.id}'),
              onTap: () => _patch({
                ...p.style,
                if (!p.style.containsKey('font')) 'font': null, // back to the default font
                'color2': p.style['color2'], // null clears a gradient
              }),
              child: Container(
                width: 96,
                margin: const EdgeInsets.only(right: 6),
                child: Column(children: [
                  Container(
                    height: 40,
                    decoration: BoxDecoration(color: const Color(0xFF465C78), borderRadius: BorderRadius.circular(4)),
                    child: CoreImage(
                      cacheKey: 'preset|${p.id}',
                      w: 192,
                      h: 80,
                      render: () => widget.vs.contentPreview({
                        ...widget.vs.defaultText('Aa Chữ'),
                        ...p.style,
                        'in': const <String, dynamic>{},
                        'out': const <String, dynamic>{},
                      }, 1, 2, 192, 80),
                    ),
                  ),
                  Text(p.name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontSize: 10, color: VsColors.fg2)),
                ]),
              ),
            ),
        ]),
      ),
      const SizedBox(height: 6),
      Wrap(spacing: 6, runSpacing: 6, children: [
        for (final s in textSwatches)
          GestureDetector(
            key: Key('color-$s'),
            onTap: () => _patch({'color': s, 'color2': null}),
            child: Container(
              width: 28,
              height: 28,
              decoration: BoxDecoration(
                color: Color(int.parse('ff${s.substring(1)}', radix: 16)),
                shape: BoxShape.circle,
                border: Border.all(color: s == color ? VsColors.accent : VsColors.line, width: s == color ? 3 : 1),
              ),
            ),
          ),
      ]),
      _slider(
          'Viền',
          'stroke',
          _sub('stroke', 'width'),
          0,
          0.2,
          (v) => {
                'stroke': {'width': v}
              }),
      _slider(
          'Bóng đổ',
          'shadow',
          _sub('shadow', 'opacity'),
          0,
          1,
          (v) => {
                'shadow': {'opacity': v}
              }),
      _slider(
          'Nền chữ',
          'box',
          _sub('box', 'opacity'),
          0,
          1,
          (v) => {
                'box': {'opacity': v}
              }),
    ]);
  }

  @override
  Widget build(BuildContext context) => SheetFrame(
        tabs: const ['Chữ', 'Kiểu', 'Hiệu ứng'],
        children: [
          _textTab(),
          _styleTab(),
          SingleChildScrollView(
              child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            AnimationPicker(vs: widget.vs, content: _c, onPatch: _patch),
            if (widget.follow != null) ...[const SizedBox(height: 8), widget.follow!],
          ])),
        ],
      );
}

/// Tabs in a bottom sheet that leaves the preview visible above it.
class SheetFrame extends StatelessWidget {
  const SheetFrame({super.key, required this.tabs, required this.children, this.initial = 0});
  final List<String> tabs;
  final List<Widget> children;
  final int initial;

  @override
  Widget build(BuildContext context) => SafeArea(
        child: Padding(
          // Lifts the sheet above the keyboard while typing.
          padding: EdgeInsets.only(bottom: MediaQuery.viewInsetsOf(context).bottom),
          child: DefaultTabController(
            length: tabs.length,
            initialIndex: initial,
            child: SizedBox(
              height: 360,
              child: Column(children: [
                TabBar(tabs: [for (final t in tabs) Tab(height: 36, text: t)]),
                Expanded(
                  child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 12),
                    child: TabBarView(children: children),
                  ),
                ),
              ]),
            ),
          ),
        ),
      );
}

// ---------------------------------------------------------------- stickers

/// The sticker tool: pick from the library (adds one at the playhead), then size and animate it.
class StickerSheet extends StatefulWidget {
  const StickerSheet(
      {super.key,
      required this.vs,
      required this.content,
      required this.onPick,
      required this.onPatch,
      this.onLive,
      this.scale,
      this.follow});
  final VStudio vs;
  final Map<String, dynamic>? content; // the selected sticker, if any
  final Map<String, dynamic>? Function(String stickerId) onPick; // returns the new sticker's content
  final ValueChanged<Map<String, dynamic>> onPatch;

  /// While the size slider moves: the sticker follows live (committed with [onPatch] on release).
  final ValueChanged<Map<String, dynamic>>? onLive;

  /// The sticker's scale at the playhead (pinch / corner drag), so "Cỡ" shows the size as seen.
  final double Function()? scale;

  /// Shown under the animations: "Bám theo đối tượng" (FR-028).
  final Widget? follow;

  @override
  State<StickerSheet> createState() => _StickerSheetState();
}

class _StickerSheetState extends State<StickerSheet> {
  static const icon = 112; // px rendered (shown at 56)
  late final TextCatalog _cat = widget.vs.textCatalog();
  late final List<String> _categories = _cat.stickerCategories;
  late String _category = _categories.first;
  late Map<String, dynamic>? _c = widget.content;
  double? _size; // while dragging

  void _patch(Map<String, dynamic> p) {
    final c = _c;
    if (c == null) return;
    setState(() => _c = mergePatch(c, p));
    widget.onPatch(p);
  }

  Widget _library() {
    final list = _cat.stickers.where((s) => s.category == _category).toList();
    return Column(children: [
      SizedBox(
        height: 44,
        child: ListView(scrollDirection: Axis.horizontal, children: [
          for (final c in _categories)
            Padding(
              padding: const EdgeInsets.only(right: 6, top: 6),
              child: ChoiceChip(
                key: Key('sticker-cat-$c'),
                label: Text(c, style: const TextStyle(fontSize: 12)),
                visualDensity: VisualDensity.compact,
                selected: c == _category,
                onSelected: (_) => setState(() => _category = c),
              ),
            ),
        ]),
      ),
      Expanded(
        child: GridView.builder(
          gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
              maxCrossAxisExtent: 64, mainAxisSpacing: 6, crossAxisSpacing: 6),
          itemCount: list.length,
          itemBuilder: (context, i) {
            final s = list[i];
            final on = _c?['sticker'] == s.id;
            return Tooltip(
              message: s.name,
              child: GestureDetector(
                key: Key('sticker-${s.id}'),
                onTap: () {
                  final c = widget.onPick(s.id);
                  if (c != null) setState(() => _c = c);
                },
                child: Container(
                  padding: const EdgeInsets.all(4),
                  decoration: BoxDecoration(
                    color: VsColors.raised,
                    border: Border.all(color: on ? VsColors.accent : Colors.transparent, width: 2),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: CoreImage(
                      cacheKey: 'sticker|${s.id}',
                      w: icon,
                      h: icon,
                      render: () => widget.vs.stickerIcon(s.id, icon, seconds: 0.8)),
                ),
              ),
            );
          },
        ),
      ),
    ]);
  }

  Widget _settings() {
    final c = _c;
    if (c == null) {
      return const Center(
          child: Text('Chọn một sticker để chỉnh cỡ và hiệu ứng', style: TextStyle(color: VsColors.fg3)));
    }
    final scale = math.max(0.05, widget.scale?.call() ?? 1.0);
    final size = _size ?? ((c['size'] ?? 320) as num).toDouble() * scale;
    return SingleChildScrollView(
      child: Column(children: [
        Row(children: [
          const SizedBox(width: 64, child: Text('Cỡ', style: TextStyle(fontSize: 12, color: VsColors.fg2))),
          Expanded(
            child: Slider(
              key: const Key('sticker-size'),
              min: 20,
              max: 2000,
              value: size.clamp(20, 2000).toDouble(),
              onChanged: (v) {
                setState(() => _size = v);
                widget.onLive?.call({'size': (v / scale).roundToDouble()});
              },
              onChangeEnd: (v) {
                _size = null;
                _patch({'size': (v / scale).roundToDouble()});
              },
            ),
          ),
          SizedBox(
              width: 56, child: Text('${size.round()} px', style: const TextStyle(fontSize: 12, color: VsColors.fg2))),
        ]),
        Wrap(spacing: 8, children: [
          FilterChip(
            key: const Key('sticker-flip'),
            label: const Text('Lật ngang'),
            selected: c['flip'] == true,
            onSelected: (on) => _patch({'flip': on}),
          ),
          FilterChip(
            key: const Key('sticker-alive'),
            label: const Text('Chuyển động riêng'),
            selected: c['alive'] != false,
            onSelected: (on) => _patch({'alive': on}),
          ),
        ]),
        AnimationPicker(vs: widget.vs, content: c, onPatch: _patch),
        if (widget.follow != null) ...[const SizedBox(height: 8), widget.follow!],
      ]),
    );
  }

  @override
  Widget build(BuildContext context) => SheetFrame(
        tabs: const ['Sticker', 'Hiệu ứng'],
        initial: widget.content == null ? 0 : 1,
        children: [_library(), _settings()],
      );
}

// ---------------------------------------------------------------- overlays

/// The overlay tool: pick an effect (laid over the whole frame from the playhead), then tune it.
/// The region it covers is dragged on the preview; how long, on the timeline.
class OverlaySheet extends StatefulWidget {
  const OverlaySheet(
      {super.key,
      required this.vs,
      required this.content,
      required this.onPick,
      required this.onPatch,
      required this.onFullFrame,
      this.onSpan});
  final VStudio vs;
  final Map<String, dynamic>? content; // the selected overlay, if any
  /// "Cả video" (true) or "Từ đây đến hết" (false): how long the selected overlay plays.
  final ValueChanged<bool>? onSpan;
  final Map<String, dynamic>? Function(String overlayId) onPick; // returns the new overlay's content
  final ValueChanged<Map<String, dynamic>> onPatch;
  final VoidCallback onFullFrame;

  @override
  State<OverlaySheet> createState() => _OverlaySheetState();
}

class _OverlaySheetState extends State<OverlaySheet> {
  static const iconW = 160, iconH = 90; // px rendered (shown at half)
  late final List<OverlayInfo> _all = widget.vs.overlayCatalog();
  late final List<String> _categories =
      [for (final o in _all) o.category].fold<List<String>>([], (l, c) => l.contains(c) ? l : (l..add(c)));
  late String _category = _categories.first;
  late Map<String, dynamic>? _c = widget.content;
  final Map<String, double> _dragging = {};

  static const _tints = ['#ffffff', '#ffd166', '#ff8fab', '#8ecae6', '#b8f2a1', '#c8b6ff'];

  void _patch(Map<String, dynamic> p) {
    final c = _c;
    if (c == null) return;
    setState(() => _c = mergePatch(c, p));
    widget.onPatch(p);
  }

  Widget _library() {
    final list = _all.where((o) => o.category == _category).toList();
    return Column(children: [
      SizedBox(
        height: 44,
        child: ListView(scrollDirection: Axis.horizontal, children: [
          for (final c in _categories)
            Padding(
              padding: const EdgeInsets.only(right: 6, top: 6),
              child: ChoiceChip(
                key: Key('overlay-cat-$c'),
                label: Text(c, style: const TextStyle(fontSize: 12)),
                visualDensity: VisualDensity.compact,
                selected: c == _category,
                onSelected: (_) => setState(() => _category = c),
              ),
            ),
        ]),
      ),
      Expanded(
        child: GridView.builder(
          gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
              maxCrossAxisExtent: 112, mainAxisSpacing: 6, crossAxisSpacing: 6, childAspectRatio: 1.2),
          itemCount: list.length,
          itemBuilder: (context, i) {
            final o = list[i];
            final on = _c?['overlay'] == o.id;
            return GestureDetector(
              key: Key('overlay-${o.id}'),
              onTap: () {
                final c = widget.onPick(o.id);
                if (c != null) setState(() => _c = c);
              },
              child: Column(children: [
                Container(
                  decoration: BoxDecoration(
                    border: Border.all(color: on ? VsColors.accent : Colors.transparent, width: 2),
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(4),
                    child: AspectRatio(
                      aspectRatio: 16 / 9,
                      child: CoreImage(
                          cacheKey: 'overlay|${o.id}',
                          w: iconW,
                          h: iconH,
                          render: () => widget.vs.overlayIcon(o.id, iconW, iconH)),
                    ),
                  ),
                ),
                const SizedBox(height: 2),
                Text(o.name,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(fontSize: 11, color: VsColors.fg2)),
              ]),
            );
          },
        ),
      ),
    ]);
  }

  Widget _slider(String key, String label, double min, double max, double value, ValueChanged<double> commit) {
    final v = (_dragging[key] ?? value).clamp(min, max).toDouble();
    return Row(children: [
      SizedBox(width: 76, child: Text(label, style: const TextStyle(fontSize: 12, color: VsColors.fg2))),
      Expanded(
        child: Slider(
          key: Key('overlay-$key'),
          min: min,
          max: max,
          value: v,
          onChanged: (x) => setState(() => _dragging[key] = x),
          onChangeEnd: (x) {
            _dragging.remove(key);
            commit(double.parse(x.toStringAsFixed(2)));
          },
        ),
      ),
    ]);
  }

  Widget _settings() {
    final c = _c;
    if (c == null) {
      return const Center(child: Text('Chọn một lớp phủ để chỉnh', style: TextStyle(color: VsColors.fg3)));
    }
    double num_(String k, double d) => ((c[k] ?? d) as num).toDouble();
    final inFx = c['in'] is Map ? c['in'] as Map : const {};
    final fade = (inFx['type'] ?? '') == '' ? 0.0 : ((inFx['duration'] ?? 0.8) as num).toDouble();
    final info = _all.where((o) => o.id == c['overlay']).firstOrNull;
    return SingleChildScrollView(
      child: Column(children: [
        _slider('density', 'Mật độ', 0.1, 3, num_('density', 1), (v) => _patch({'density': v})),
        _slider('speed', 'Tốc độ', 0, 3, num_('speed', 1), (v) => _patch({'speed': v})),
        _slider('size', 'Cỡ hạt', 0.3, 3, num_('size', 1), (v) => _patch({'size': v})),
        _slider('opacity', 'Độ đậm', 0.05, 1, num_('opacity', 1), (v) => _patch({'opacity': v})),
        _slider('feather', 'Viền mờ', 0, 0.4, num_('feather', 0.12), (v) => _patch({'feather': v})),
        _slider('fade', 'Hiện/ẩn dần', 0, 3, fade, (v) {
          final fx = {'type': v > 0 ? 'fade' : '', 'duration': v};
          _patch({'in': fx, 'out': fx});
        }),
        if (info?.tintable ?? false)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 4),
            child: Wrap(spacing: 8, children: [
              ChoiceChip(
                key: const Key('overlay-color-none'),
                label: const Text('Màu gốc'),
                selected: c['color'] == null,
                onSelected: (_) => _patch({'color': null}),
              ),
              for (final hex in _tints)
                GestureDetector(
                  key: Key('overlay-color-$hex'),
                  onTap: () => _patch({'color': hex}),
                  child: Container(
                    width: 28,
                    height: 28,
                    decoration: BoxDecoration(
                      color: Color(int.parse('ff${hex.substring(1)}', radix: 16)),
                      shape: BoxShape.circle,
                      border: Border.all(color: c['color'] == hex ? VsColors.accent : Colors.black26, width: 2),
                    ),
                  ),
                ),
            ]),
          ),
        const Text('Thời lượng', style: TextStyle(fontSize: 12, color: VsColors.fg2)),
        Wrap(spacing: 8, children: [
          ActionChip(
            key: const Key('overlay-span-all'),
            avatar: const Icon(Icons.straighten, size: 16),
            label: const Text('Cả video'),
            onPressed: widget.onSpan == null ? null : () => widget.onSpan!(true),
          ),
          ActionChip(
            key: const Key('overlay-span-rest'),
            avatar: const Icon(Icons.last_page, size: 16),
            label: const Text('Từ đây đến hết'),
            onPressed: widget.onSpan == null ? null : () => widget.onSpan!(false),
          ),
        ]),
        const Text('Kéo hai đầu của lớp phủ trên timeline để co giãn tự do, không giới hạn độ dài.',
            style: TextStyle(fontSize: 11, color: VsColors.fg3)),
        const SizedBox(height: 6),
        const Text('Vùng phủ', style: TextStyle(fontSize: 12, color: VsColors.fg2)),
        Align(
          alignment: Alignment.centerLeft,
          child: TextButton.icon(
            key: const Key('overlay-full'),
            onPressed: widget.onFullFrame,
            icon: const Icon(Icons.fullscreen, size: 18),
            label: const Text('Phủ toàn khung'),
          ),
        ),
        const Text('Kéo khung trên màn xem để chỉ phủ một phần của hình.',
            style: TextStyle(fontSize: 11, color: VsColors.fg3)),
      ]),
    );
  }

  @override
  Widget build(BuildContext context) => SheetFrame(
        tabs: const ['Lớp phủ', 'Chỉnh'],
        initial: widget.content == null ? 0 : 1,
        children: [_library(), _settings()],
      );
}

// ---------------------------------------------------------------- on the preview

/// Box around the selected text or sticker: drag to move, pinch or pull the
/// corner to resize, double-tap to edit. [onChanged] gets (before, after) in canvas 0..1.
class ObjectOverlay extends StatefulWidget {
  const ObjectOverlay(
      {super.key,
      required this.canvasSize,
      required this.rect,
      required this.onChanged,
      this.onDoubleTap,
      this.onChanging,
      this.freeShape = false});
  final Size canvasSize;
  final Rect rect;

  /// The corner resizes freely (an overlay's region) instead of scaling with the shape kept.
  final bool freeShape;
  final void Function(Rect from, Rect to) onChanged;

  /// While the fingers move: the object follows live (nothing is edited until [onChanged]).
  final void Function(Rect from, Rect to)? onChanging;
  final VoidCallback? onDoubleTap;

  @override
  State<ObjectOverlay> createState() => _ObjectOverlayState();
}

class _ObjectOverlayState extends State<ObjectOverlay> {
  Rect? _live; // canvas 0..1 while dragging
  bool _corner = false;
  Offset _from = Offset.zero;
  Rect _start = Rect.zero;
  static const handle = 28.0;

  void _setLive(Rect n) {
    setState(() => _live = n);
    widget.onChanging?.call(widget.rect, n);
  }

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
          onDoubleTap: widget.onDoubleTap,
          onScaleStart: (d) {
            _start = r;
            _from = d.localFocalPoint;
            _corner = d.pointerCount < 2 && (d.localFocalPoint - r.bottomRight).distance < handle;
          },
          onScaleUpdate: (d) {
            final delta = d.localFocalPoint - _from;
            double k;
            Offset centre;
            if (_corner && widget.freeShape) {
              // The top-left stays, the bottom-right corner follows the finger.
              final br = _start.bottomRight + delta;
              if (br.dx - _start.left < 16 || br.dy - _start.top < 16) return;
              _setLive(toNorm(Rect.fromPoints(_start.topLeft, br)));
              return;
            }
            if (_corner) {
              // Scales about the centre, keeping the shape: the corner follows the finger.
              final half0 = _start.bottomRight - _start.center;
              final half1 = _start.bottomRight + delta - _start.center;
              k = (half1.dx * half0.dx + half1.dy * half0.dy) / math.max(1e-6, half0.distanceSquared);
              centre = _start.center;
            } else {
              k = d.scale;
              centre = _start.center + delta;
            }
            if (_start.shortestSide * k < 10) return;
            final n = Rect.fromCenter(center: centre, width: _start.width * k, height: _start.height * k);
            _setLive(toNorm(n));
          },
          onScaleEnd: (_) {
            final live = _live;
            if (live != null) widget.onChanged(widget.rect, live);
            setState(() => _live = null);
          },
          child: CustomPaint(painter: _ObjectPainter(r), size: box.biggest),
        );
      });
}

class _ObjectPainter extends CustomPainter {
  _ObjectPainter(this.r);
  final Rect r;

  @override
  void paint(Canvas canvas, Size size) {
    final line = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.4;
    // Dashed box, readable on any picture (white with a dark shadow line).
    final shadow = Paint()
      ..color = Colors.black54
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.6;
    final path = Path()..addRect(r);
    for (final m in path.computeMetrics()) {
      for (var d = 0.0; d < m.length; d += 10) {
        final seg = m.extractPath(d, math.min(d + 6, m.length));
        canvas.drawPath(seg, shadow);
        canvas.drawPath(seg, line);
      }
    }
    final h = Paint()..color = VsColors.accent;
    canvas.drawCircle(r.bottomRight, 9, Paint()..color = Colors.black54);
    canvas.drawCircle(r.bottomRight, 8, h); // the resize handle
  }

  @override
  bool shouldRepaint(_ObjectPainter old) => old.r != r;
}

/// Where the clip's transform goes so its box moves from [from] to [to] (canvas 0..1):
/// the box scales about its own centre, so the point the transform anchors on scales with it.
/// [x], [y]: the clip's position now (canvas px from the centre), [scale] its scale.
({double x, double y, double scale}) objectTransform(
    Size canvas, Rect from, Rect to, double x, double y, double scale) {
  final k = from.width > 0 ? to.width / from.width : 1.0;
  final w = canvas.width, h = canvas.height;
  return (
    x: to.center.dx * w - w / 2 - (from.center.dx * w - w / 2 - x) * k,
    y: to.center.dy * h - h / 2 - (from.center.dy * h - h / 2 - y) * k,
    scale: (scale * k).clamp(0.05, 20).toDouble(),
  );
}
