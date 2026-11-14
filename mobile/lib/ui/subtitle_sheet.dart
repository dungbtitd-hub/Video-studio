// "Phụ đề" on the phone: lines of a subtitle track (add at the playhead, type, import/export a
// file), the track's look (styles, fonts, size, colour, outline, background), where it sits
// (7 places, margin, or drag it on the preview) and AI translation through any connector.
// Every change is one undo step in the core; what the sheet shows is what exports.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';

import '../core/project_model.dart';
import '../core/subtitle_translate.dart';
import '../core/vstudio.dart';
import 'text_sticker.dart';
import 'theme.dart';

class SubtitleSheet extends StatefulWidget {
  const SubtitleSheet({
    super.key,
    required this.vs,
    required this.project,
    required this.playhead,
    required this.selected,
    required this.onEdited,
    required this.onSelect,
    this.send = httpSend,
  });
  final VStudio vs;
  final ProjectView Function() project; // the current project (after edits)
  final double Function() playhead;
  final String? Function() selected; // selected clip id
  final VoidCallback onEdited; // refresh the editor, save
  final void Function(String clipId, double at) onSelect; // select a line and jump there
  final HttpSend send;

  @override
  State<SubtitleSheet> createState() => _SubtitleSheetState();
}

class _SubtitleSheetState extends State<SubtitleSheet> {
  late final SubtitleCatalog _cat = widget.vs.subtitleCatalog();
  late final TextCatalog _fonts = widget.vs.textCatalog();
  final TextEditingController _text = TextEditingController();
  String? _shownLine;
  Timer? _typing;
  String? _track;
  bool _lineOnly = false;
  final Map<String, double> _dragging = {};
  String? _message;
  // AI
  TranslationSettings? _settings;
  String _preset = 'anthropic';
  final _url = TextEditingController(), _model = TextEditingController(), _key = TextEditingController();
  final _notes = TextEditingController();
  String _from = '', _to = 'en';
  int _mode = 2;
  TranslationRunner? _runner;
  double? _progress;

  @override
  void initState() {
    super.initState();
    TranslationSettings.load().then((s) {
      if (!mounted) return;
      setState(() {
        _settings = s;
        _preset = (s.data['preset'] ?? 'anthropic') as String;
        _to = (s.data['to'] ?? 'en') as String;
        _from = (s.data['from'] ?? '') as String;
        _mode = (s.data['mode'] ?? 2) as int;
        _loadPreset();
      });
    });
    _loadPreset();
  }

  @override
  void dispose() {
    _commitText();
    _runner?.cancel();
    for (final c in [_text, _url, _model, _key, _notes]) {
      c.dispose();
    }
    super.dispose();
  }

  // ---- state from the project

  List<TrackView> get _tracks => [
        for (final t in widget.project().tracks)
          if (t.isSubtitle) t
      ];

  TrackView? get _current {
    final tracks = _tracks;
    final sel = widget.selected();
    for (final t in tracks) {
      if (t.clips.any((c) => c.id == sel)) return t;
    }
    for (final t in tracks) {
      if (t.id == _track) return t;
    }
    return tracks.isEmpty ? null : tracks.first; // display order: the top one first
  }

  ClipView? get _line {
    final sel = widget.selected();
    for (final c in _current?.clips ?? const <ClipView>[]) {
      if (c.id == sel && c.kind == 'subtitle') return c;
    }
    return null;
  }

  Map<String, dynamic> _sub(String key) {
    final s = _current?.subtitle?[key];
    return s is Map<String, dynamic> ? s : const {};
  }

  Map<String, dynamic> get _style {
    final st = Map<String, dynamic>.of(_sub('style'));
    final own = _line?.content?['style'];
    return own is Map<String, dynamic> ? mergePatch(st, own) : st;
  }

  void _edit(void Function() f) {
    try {
      f();
      setState(() => _message = null);
      widget.onEdited();
    } on VStudioException catch (e) {
      setState(() => _message = e.message);
    }
  }

  void _applyStyle(Map<String, dynamic> patch) {
    final line = _line, t = _current;
    if (t == null) return;
    _edit(() => _lineOnly && line != null
        ? widget.vs.updateClipContent(line.id, {'style': patch})
        : widget.vs.setSubtitleSettings(t.id, {'style': patch}));
  }

  void _applyPosition(Map<String, dynamic> patch) {
    final line = _line, t = _current;
    if (t == null) return;
    if (_lineOnly && line != null) {
      final own = line.content?['position'];
      final base = own is Map<String, dynamic> ? own : _sub('position');
      _edit(() => widget.vs.updateClipContent(line.id, {'position': mergePatch(base, patch)}));
    } else {
      _edit(() => widget.vs.setSubtitleSettings(t.id, {'position': patch}));
    }
  }

  void _commitText() {
    _typing?.cancel();
    _typing = null;
    final id = _shownLine;
    if (id == null) return;
    String? now;
    for (final t in widget.project().tracks) {
      for (final c in t.clips) {
        if (c.id == id) now = c.content?['text'] as String?;
      }
    }
    if (now != null && now != _text.text) _edit(() => widget.vs.updateClipContent(id, {'text': _text.text}));
  }

  void _add() {
    _commitText();
    _edit(() {
      final id = widget.vs.addSubtitle(widget.playhead(), '', trackId: _current?.id);
      final t = _current;
      final start = t?.clips.where((c) => c.id == id).firstOrNull?.start ?? widget.playhead();
      widget.onSelect(id, start + 0.15);
    });
  }

  Future<void> _import() async {
    final r = await FilePicker.pickFiles(type: FileType.custom, allowedExtensions: const ['srt', 'vtt', 'ass', 'ssa']);
    final f = r.firstOrNull;
    final path = f?.path;
    if (f == null || path == null) return;
    final bytes = await File(path).readAsBytes();
    final name = f.name.replaceAll(RegExp(r'\.[^.]+$'), '');
    _edit(() => _track = widget.vs.importSubtitles(utf8.decode(bytes, allowMalformed: true), name: name));
  }

  Future<void> _export() async {
    final t = _current;
    if (t == null) return;
    try {
      final text = widget.vs.exportSubtitles(t.id);
      final dir = await getApplicationDocumentsDirectory();
      final file = File('${dir.path}/${t.name.replaceAll(RegExp(r'[\\/:*?"<>|]'), '_')}.srt');
      await file.writeAsString(text);
      setState(() => _message = 'Đã lưu ${file.path}');
    } on VStudioException catch (e) {
      setState(() => _message = e.message);
    }
  }

  // ---- AI

  Map<String, dynamic> get _presetInfo =>
      _cat.providers.firstWhere((p) => p['id'] == _preset, orElse: () => _cat.providers.first);

  void _loadPreset() {
    final saved = _settings?.preset(_preset) ?? const {};
    _url.text = (saved['url'] ?? _presetInfo['url'] ?? '') as String;
    _model.text = (saved['model'] ?? _presetInfo['model'] ?? '') as String;
    _key.text = (saved['key'] ?? '') as String;
  }

  void _savePreset() {
    final s = _settings;
    if (s == null) return;
    s.setPreset(_preset, {'url': _url.text, 'model': _model.text, 'key': _key.text});
    s.data
      ..['preset'] = _preset
      ..['to'] = _to
      ..['from'] = _from
      ..['mode'] = _mode;
    s.save();
  }

  Future<void> _translate() async {
    final t = _current;
    if (t == null || _runner != null) return;
    _savePreset();
    final SubtitleTranslation job;
    try {
      job = widget.vs.beginTranslation(
          t.id,
          {
            'provider': _presetInfo['provider'],
            'url': _url.text.trim(),
            'model': _model.text.trim(),
            'api_key': _key.text.trim(),
          },
          from: _from,
          to: _to,
          notes: _notes.text.trim());
      job.request(0); // an unusable connector shows now
    } on VStudioException catch (e) {
      setState(() => _message = e.message);
      return;
    }
    final runner = TranslationRunner(job, send: widget.send);
    setState(() {
      _runner = runner;
      _progress = 0;
      _message = 'Đang dịch…';
    });
    final out = await runner.run(progress: (d, n) {
      if (mounted) setState(() => _progress = n == 0 ? 1 : d / n);
    });
    String msg;
    if (out.cancelled) {
      msg = 'Đã dừng dịch';
    } else if (job.done == 0) {
      msg = 'Không dịch được: ${out.lastError}';
    } else {
      try {
        _track = job.apply(_mode);
        widget.onEdited();
        msg = out.failed == 0
            ? 'Đã dịch xong'
            : 'Đã dịch; ${out.failed} nhóm dòng chưa dịch được (giữ bản gốc): ${out.lastError}';
      } on VStudioException catch (e) {
        msg = e.message;
      }
    }
    job.dispose();
    if (!mounted) return;
    setState(() {
      _runner = null;
      _progress = null;
      _message = msg;
    });
  }

  // ---- widgets

  Widget _slider(String label, String key, double value, double min, double max, void Function(double) commit) {
    final v = _dragging[key] ?? value;
    return Row(children: [
      SizedBox(width: 72, child: Text(label, style: const TextStyle(fontSize: 12, color: VsColors.fg2))),
      Expanded(
        child: Slider(
          key: Key('sub-$key'),
          min: min,
          max: max,
          value: v.clamp(min, max).toDouble(),
          onChanged: (x) => setState(() => _dragging[key] = x),
          onChangeEnd: (x) {
            _dragging.remove(key);
            commit(x);
          },
        ),
      ),
    ]);
  }

  Widget _messageLine() => _message == null
      ? const SizedBox.shrink()
      : Padding(
          padding: const EdgeInsets.only(top: 6),
          child: Text(_message!, style: const TextStyle(fontSize: 11, color: VsColors.fg3)),
        );

  Widget _linesTab() {
    final tracks = _tracks, t = _current, line = _line;
    if (line?.id != _shownLine) {
      _shownLine = line?.id;
      _text.text = (line?.content?['text'] ?? '') as String;
    }
    return Column(children: [
      const SizedBox(height: 6),
      Row(children: [
        if (tracks.length > 1)
          Expanded(
            child: DropdownButton<String>(
              key: const Key('sub-track'),
              isExpanded: true,
              value: t?.id,
              items: [
                for (final x in tracks)
                  DropdownMenuItem(value: x.id, child: Text(x.name, overflow: TextOverflow.ellipsis))
              ],
              onChanged: (id) => setState(() => _track = id),
            ),
          )
        else
          const Spacer(),
        IconButton(
            key: const Key('sub-import'),
            tooltip: 'Nhập .srt/.vtt/.ass',
            icon: const Icon(Icons.file_open_outlined),
            onPressed: _import),
        IconButton(
            key: const Key('sub-export'),
            tooltip: 'Xuất .srt',
            icon: const Icon(Icons.save_alt),
            onPressed: t == null ? null : _export),
        FilledButton.icon(
            key: const Key('sub-add'), onPressed: _add, icon: const Icon(Icons.add), label: const Text('Thêm dòng')),
      ]),
      if (line != null)
        TextField(
          key: const Key('sub-text'),
          controller: _text,
          minLines: 1,
          maxLines: 3,
          decoration: const InputDecoration(isDense: true, border: OutlineInputBorder(), hintText: 'Nhập phụ đề'),
          onChanged: (_) {
            _typing?.cancel();
            _typing = Timer(const Duration(milliseconds: 600), _commitText);
          },
        ),
      _messageLine(),
      Expanded(
        child: t == null
            ? const Center(
                child: Text('Bấm “Thêm dòng” để chèn phụ đề tại đầu phát, hoặc nhập tệp .srt/.vtt/.ass.',
                    textAlign: TextAlign.center, style: TextStyle(color: VsColors.fg3, fontSize: 12)))
            : ListView.builder(
                key: const Key('sub-lines'),
                itemCount: t.clips.length,
                itemExtent: 36,
                itemBuilder: (_, i) {
                  final c = t.clips[i];
                  final on = c.id == widget.selected();
                  return InkWell(
                    onTap: () {
                      _commitText();
                      widget.onSelect(c.id, c.start + 0.01);
                      setState(() {});
                    },
                    child: Container(
                      color: on ? VsColors.raised : null,
                      padding: const EdgeInsets.symmetric(horizontal: 6),
                      child: Row(children: [
                        SizedBox(
                            width: 64,
                            child: Text(_clock(c.start), style: const TextStyle(fontSize: 11, color: VsColors.fg3))),
                        Expanded(
                          child: Text(((c.content?['text'] ?? '') as String).replaceAll('\n', ' ⏎ '),
                              maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 13)),
                        ),
                      ]),
                    ),
                  );
                },
              ),
      ),
    ]);
  }

  static String _clock(double s) {
    final ms = (s * 1000).round();
    String two(int v) => v.toString().padLeft(2, '0');
    return '${two(ms ~/ 60000)}:${two(ms ~/ 1000 % 60)}.${two(ms % 1000 ~/ 10)}';
  }

  Widget _onlyLineSwitch() => _line == null
      ? const SizedBox.shrink()
      : SwitchListTile(
          key: const Key('sub-line-only'),
          dense: true,
          contentPadding: EdgeInsets.zero,
          title: const Text('Chỉ dòng đang chọn', style: TextStyle(fontSize: 13)),
          value: _lineOnly,
          onChanged: (v) => setState(() => _lineOnly = v),
        );

  Widget _styleTab() {
    if (_current == null) return const Center(child: Text('Chưa có phụ đề', style: TextStyle(color: VsColors.fg3)));
    final st = _style;
    final font = (st['font'] ?? 'be-vietnam-pro-bold') as String;
    final color = ((st['color'] ?? '#ffffff') as String).toLowerCase();
    double sub(String k, String f) => ((st[k] is Map ? st[k][f] : null) ?? 0 as num).toDouble();
    return ListView(padding: const EdgeInsets.only(top: 6), children: [
      _onlyLineSwitch(),
      SizedBox(
        height: 64,
        child: ListView(scrollDirection: Axis.horizontal, children: [
          for (final s in _cat.styles)
            GestureDetector(
              key: Key('sub-style-${s['id']}'),
              onTap: () => _applyStyle(Map<String, dynamic>.of(s['style'] as Map<String, dynamic>)),
              child: Container(
                width: 100,
                margin: const EdgeInsets.only(right: 6),
                child: Column(children: [
                  Container(
                    height: 40,
                    decoration: BoxDecoration(color: const Color(0xFF465C78), borderRadius: BorderRadius.circular(4)),
                    child: CoreImage(
                      cacheKey: 'substyle|${s['id']}',
                      w: 200,
                      h: 80,
                      render: () => widget.vs.contentPreview({
                        'font': 'be-vietnam-pro-bold',
                        'size': 56,
                        ...(s['style'] as Map<String, dynamic>),
                        'kind': 'subtitle',
                        'text': 'Phụ đề Aa',
                      }, 1, 2, 200, 80),
                    ),
                  ),
                  Text(s['name'] as String,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontSize: 10, color: VsColors.fg2)),
                ]),
              ),
            ),
        ]),
      ),
      const SizedBox(height: 6),
      SizedBox(
        height: 40,
        child: ListView(scrollDirection: Axis.horizontal, children: [
          for (final f in _fonts.fonts)
            Padding(
              padding: const EdgeInsets.only(right: 6),
              child: ChoiceChip(
                key: Key('sub-font-${f.id}'),
                label: Text(f.name, style: const TextStyle(fontSize: 12)),
                selected: f.id == font,
                onSelected: (_) => _applyStyle({'font': f.id}),
              ),
            ),
        ]),
      ),
      _slider('Cỡ chữ', 'size', ((st['size'] ?? 56) as num).toDouble(), 16, 200,
          (v) => _applyStyle({'size': v.roundToDouble()})),
      Wrap(spacing: 6, runSpacing: 6, children: [
        for (final s in textSwatches)
          GestureDetector(
            key: Key('sub-color-$s'),
            onTap: () => _applyStyle({'color': s, 'color2': null}),
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
          sub('stroke', 'width'),
          0,
          0.2,
          (v) => _applyStyle({
                'stroke': {'width': v}
              })),
      _slider(
          'Nền',
          'box',
          sub('box', 'opacity'),
          0,
          1,
          (v) => _applyStyle({
                'box': {'opacity': v}
              })),
    ]);
  }

  Widget _positionTab() {
    if (_current == null) return const Center(child: Text('Chưa có phụ đề', style: TextStyle(color: VsColors.fg3)));
    final Object? own = _lineOnly ? (_line?.content ?? const {})['position'] : null;
    final pos = own is Map<String, dynamic> ? own : _sub('position');
    bool isOn(Map<String, dynamic> p) =>
        (pos['anchor'] ?? 'bottom') == p['anchor'] &&
        (((pos['x'] ?? 0.5) as num) - ((p['x'] ?? 0.5) as num)).abs() < 0.01 &&
        (pos['h'] ?? 'center') == (p['h'] ?? 'center');
    return ListView(padding: const EdgeInsets.only(top: 6), children: [
      _onlyLineSwitch(),
      Wrap(spacing: 6, runSpacing: 6, children: [
        for (final p in _cat.positions)
          ChoiceChip(
            key: Key('sub-pos-${p['id']}'),
            label: Text(p['name'] as String, style: const TextStyle(fontSize: 12)),
            selected: isOn(p['position'] as Map<String, dynamic>),
            onSelected: (_) => _applyPosition(Map<String, dynamic>.of(p['position'] as Map<String, dynamic>)),
          ),
      ]),
      _slider('Cách mép', 'margin', ((pos['margin'] ?? 0.06) as num).toDouble(), 0, 0.3,
          (v) => _applyPosition({'margin': (v * 100).roundToDouble() / 100})),
      const Padding(
        padding: EdgeInsets.only(top: 4),
        child: Text('Hoặc kéo phụ đề trên khung xem trước để đặt ở bất kỳ đâu; kéo góc để đổi cỡ chữ.',
            style: TextStyle(fontSize: 11, color: VsColors.fg3)),
      ),
      Row(children: [
        const Text('Khớp giờ', style: TextStyle(fontSize: 12, color: VsColors.fg2)),
        const Spacer(),
        for (final d in const [-0.5, -0.1, 0.1, 0.5])
          TextButton(
            key: Key('sub-shift-$d'),
            onPressed: () => _edit(() => widget.vs.shiftSubtitles(_current!.id, d)),
            child: Text('${d > 0 ? '+' : ''}${d}s'),
          ),
      ]),
      _messageLine(),
    ]);
  }

  Widget _aiTab() {
    if (_current == null) return const Center(child: Text('Chưa có phụ đề', style: TextStyle(color: VsColors.fg3)));
    final running = _runner != null;
    final langs = _cat.languages;
    return ListView(padding: const EdgeInsets.only(top: 6), children: [
      DropdownButton<String>(
        key: const Key('sub-provider'),
        isExpanded: true,
        value: _preset,
        items: [
          for (final p in _cat.providers)
            DropdownMenuItem(
                value: p['id'] as String, child: Text(p['name'] as String, overflow: TextOverflow.ellipsis))
        ],
        onChanged: running
            ? null
            : (v) => setState(() {
                  _savePreset();
                  _preset = v!;
                  _loadPreset();
                }),
      ),
      Text((_presetInfo['hint'] ?? '') as String, style: const TextStyle(fontSize: 11, color: VsColors.fg3)),
      TextField(controller: _url, decoration: const InputDecoration(isDense: true, labelText: 'Địa chỉ API')),
      TextField(controller: _model, decoration: const InputDecoration(isDense: true, labelText: 'Model')),
      TextField(
          key: const Key('sub-key'),
          controller: _key,
          obscureText: true,
          decoration: const InputDecoration(isDense: true, labelText: 'Khóa API (lưu trên máy này)')),
      Row(children: [
        Expanded(
          child: DropdownButton<String>(
            isExpanded: true,
            value: _from,
            items: [
              const DropdownMenuItem(value: '', child: Text('Tự nhận biết')),
              for (final l in langs) DropdownMenuItem(value: l['code'] as String, child: Text(l['name'] as String)),
            ],
            onChanged: running ? null : (v) => setState(() => _from = v!),
          ),
        ),
        const Padding(padding: EdgeInsets.symmetric(horizontal: 6), child: Icon(Icons.arrow_forward, size: 16)),
        Expanded(
          child: DropdownButton<String>(
            key: const Key('sub-to'),
            isExpanded: true,
            value: _to,
            items: [
              for (final l in langs) DropdownMenuItem(value: l['code'] as String, child: Text(l['name'] as String))
            ],
            onChanged: running ? null : (v) => setState(() => _to = v!),
          ),
        ),
      ]),
      TextField(
          controller: _notes,
          decoration: const InputDecoration(isDense: true, labelText: 'Ghi chú cho AI (tên riêng, thuật ngữ…)')),
      const SizedBox(height: 6),
      SegmentedButton<int>(
        segments: const [
          ButtonSegment(value: 2, label: Text('Song ngữ')),
          ButtonSegment(value: 1, label: Text('Track mới')),
          ButtonSegment(value: 0, label: Text('Thay thế')),
        ],
        selected: {_mode},
        onSelectionChanged: running ? null : (s) => setState(() => _mode = s.first),
      ),
      const SizedBox(height: 6),
      if (running) ...[
        LinearProgressIndicator(value: _progress),
        TextButton(onPressed: () => _runner?.cancel(), child: const Text('Dừng')),
      ] else
        FilledButton.icon(
          key: const Key('sub-translate'),
          onPressed: _translate,
          icon: const Icon(Icons.translate),
          label: const Text('Dịch bằng AI'),
        ),
      _messageLine(),
    ]);
  }

  @override
  Widget build(BuildContext context) => SheetFrame(
        tabs: const ['Dòng', 'Kiểu', 'Vị trí', 'Dịch AI'],
        children: [_linesTab(), _styleTab(), _positionTab(), _aiTab()],
      );
}
