// "Preset của tôi" on mobile (FR-018, FR-022). Styles: the selected clip's colour and effects
// saved under a name, applied to another clip with one tap (long-press: rename, delete, apply
// to every clip). Export: a quality, format, frame rate, bitrate and sound saved together.
// Presets live in one file shared by every project (VStudio.initPresets).
import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

Future<String?> _askName(BuildContext context, String title, {String initial = ''}) {
  final c = TextEditingController(text: initial);
  return showDialog<String>(
    context: context,
    builder: (context) => AlertDialog(
      title: Text(title),
      content: TextField(
        key: const Key('preset-name'),
        controller: c,
        autofocus: true,
        maxLength: 60,
        decoration: const InputDecoration(hintText: 'Tên preset'),
        onSubmitted: (v) => Navigator.pop(context, v),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context), child: const Text('Huỷ')),
        TextButton(key: const Key('preset-name-ok'), onPressed: () => Navigator.pop(context, c.text), child: const Text('Lưu')),
      ],
    ),
  );
}

/// A row of the user's style presets under the looks.
class MyStylePresets extends StatefulWidget {
  const MyStylePresets({super.key, required this.vs, required this.clipId, required this.allClips, required this.onEdit});
  final VStudio vs;
  final String clipId;
  final List<String> Function() allClips; // "Áp dụng cho tất cả"
  final void Function(void Function(VStudio vs)) onEdit; // one undo step, refreshes the editor

  @override
  State<MyStylePresets> createState() => _MyStylePresetsState();
}

class _MyStylePresetsState extends State<MyStylePresets> {
  late List<UserPresetView> _presets = widget.vs.userPresets('style');
  String? _note;

  void _reload([String? note]) => setState(() {
        _presets = widget.vs.userPresets('style');
        _note = note;
      });

  Future<void> _save() async {
    final name = (await _askName(context, 'Lưu màu và hiệu ứng của clip'))?.trim();
    if (name == null || name.isEmpty || !mounted) return;
    try {
      final summary = widget.vs.saveStylePreset(widget.clipId, name);
      _reload('Đã lưu "$name": $summary');
    } on VStudioException catch (e) {
      _reload(e.message);
    }
  }

  void _apply(UserPresetView p, List<String> clips) {
    widget.onEdit((vs) => vs.applyStylePreset(clips, p.name));
    setState(() => _note = 'Đã áp dụng "${p.name}"');
  }

  Future<void> _menu(UserPresetView p) async {
    final action = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: VsColors.panel,
      builder: (context) => SafeArea(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
            child: Text('${p.name} · ${p.summary}', style: const TextStyle(color: VsColors.fg2, fontSize: 13)),
          ),
          ListTile(
              leading: const Icon(Icons.done_all),
              title: const Text('Áp dụng cho tất cả clip'),
              onTap: () => Navigator.pop(context, 'all')),
          ListTile(
              leading: const Icon(Icons.edit_outlined),
              title: const Text('Đổi tên'),
              onTap: () => Navigator.pop(context, 'rename')),
          ListTile(
              leading: const Icon(Icons.delete_outline),
              title: const Text('Xoá preset'),
              onTap: () => Navigator.pop(context, 'delete')),
        ]),
      ),
    );
    if (!mounted) return;
    try {
      switch (action) {
        case 'all':
          _apply(p, widget.allClips());
        case 'rename':
          final to = (await _askName(context, 'Đổi tên preset', initial: p.name))?.trim();
          if (to == null || to.isEmpty || to == p.name) return;
          widget.vs.renamePreset('style', p.name, to);
          _reload();
        case 'delete':
          widget.vs.removePreset('style', p.name);
          _reload('Đã xoá "${p.name}"');
      }
    } on VStudioException catch (e) {
      _reload(e.message.contains('already exists') ? 'Đã có preset tên này' : e.message);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, mainAxisSize: MainAxisSize.min, children: [
        SizedBox(
          height: 40,
          child: ListView(scrollDirection: Axis.horizontal, children: [
            const Padding(
              padding: EdgeInsets.only(right: 8, top: 10),
              child: Text('Của tôi', style: TextStyle(color: VsColors.fg2, fontSize: 12)),
            ),
            ActionChip(
              key: const Key('save-style-preset'),
              avatar: const Icon(Icons.add, size: 16),
              label: const Text('Lưu', style: TextStyle(fontSize: 12)),
              visualDensity: VisualDensity.compact,
              onPressed: _save,
            ),
            for (final p in _presets)
              Padding(
                padding: const EdgeInsets.only(left: 6),
                child: GestureDetector(
                  onLongPress: () => _menu(p),
                  child: ActionChip(
                    key: Key('style-preset-${p.name}'),
                    label: Text(p.name, style: const TextStyle(fontSize: 12)),
                    tooltip: p.summary,
                    visualDensity: VisualDensity.compact,
                    onPressed: () => _apply(p, [widget.clipId]),
                  ),
                ),
              ),
          ]),
        ),
        if (_note != null)
          Text(_note!, maxLines: 2, overflow: TextOverflow.ellipsis, style: const TextStyle(color: VsColors.fg3, fontSize: 11)),
      ]),
    );
  }
}

/// "Lưu thành preset của tôi" in the export sheet: returns the saved name, or null.
Future<String?> askExportPreset(BuildContext context, VStudio vs, String format, List<ExportPresetView> qualities) {
  return showDialog<String>(
    context: context,
    builder: (context) => _ExportPresetDialog(vs: vs, format: format, qualities: qualities),
  );
}

class _ExportPresetDialog extends StatefulWidget {
  const _ExportPresetDialog({required this.vs, required this.format, required this.qualities});
  final VStudio vs;
  final String format;
  final List<ExportPresetView> qualities;

  @override
  State<_ExportPresetDialog> createState() => _ExportPresetDialogState();
}

class _ExportPresetDialogState extends State<_ExportPresetDialog> {
  final _name = TextEditingController();
  late String _base = widget.qualities.any((q) => q.id == 'fhd1080') ? 'fhd1080' : widget.qualities.first.id;
  double _fps = 0;
  double _mbps = 0;
  bool _audio = true;
  String? _error;

  Map<String, dynamic> get _data =>
      {'base': _base, 'format': widget.format, 'fps': _fps, 'video_mbps': _mbps, 'audio': _audio};

  @override
  void dispose() {
    _name.dispose();
    super.dispose();
  }

  void _save() {
    final name = _name.text.trim();
    if (name.isEmpty) return setState(() => _error = 'Đặt tên cho preset');
    try {
      widget.vs.saveExportPreset(name, _data);
      Navigator.pop(context, name);
    } on VStudioException catch (e) {
      setState(() => _error = e.message);
    }
  }

  @override
  Widget build(BuildContext context) {
    ExportInfo? info;
    try {
      info = widget.vs.exportPresetInfo(data: _data);
    } on VStudioException {
      info = null;
    }
    DropdownButton<T> drop<T>(Key key, T value, List<(T, String)> items, ValueChanged<T> on) => DropdownButton<T>(
          key: key,
          value: value,
          isExpanded: true,
          items: [for (final (v, label) in items) DropdownMenuItem(value: v, child: Text(label))],
          onChanged: (v) => setState(() => on(v as T)),
        );
    return AlertDialog(
      title: const Text('Lưu preset xuất'),
      content: SingleChildScrollView(
        child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          TextField(
            key: const Key('export-preset-name'),
            controller: _name,
            maxLength: 60,
            decoration: const InputDecoration(hintText: 'vd. Kênh YouTube của tôi'),
          ),
          drop(const Key('export-preset-base'), _base, [for (final q in widget.qualities) (q.id, q.name)], (v) => _base = v),
          drop(const Key('export-preset-fps'), _fps, [
            (0.0, 'Khung/giây theo dự án'),
            for (final r in const [24.0, 25.0, 29.97, 30.0, 50.0, 60.0]) (r, '$r khung/giây'.replaceAll('.0 ', ' ')),
          ], (v) => _fps = v),
          drop(const Key('export-preset-mbps'), _mbps, [
            (0.0, 'Bitrate tự động'),
            for (final m in const [4.0, 8.0, 12.0, 16.0, 24.0, 40.0, 60.0]) (m, '${m.round()} Mbit/s'),
          ], (v) => _mbps = v),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Có âm thanh'),
            value: _audio,
            onChanged: (v) => setState(() => _audio = v),
          ),
          if (info != null)
            Text('${info.summary}\n${info.width} × ${info.height} · khoảng ${(info.estimatedBytes / 1e6).round()} MB',
                style: const TextStyle(color: VsColors.fg2, fontSize: 12)),
          if (_error != null) Text(_error!, style: const TextStyle(color: Color(0xFFFF6B6B), fontSize: 12)),
        ]),
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context), child: const Text('Huỷ')),
        TextButton(key: const Key('export-preset-save'), onPressed: _save, child: const Text('Lưu')),
      ],
    );
  }
}
