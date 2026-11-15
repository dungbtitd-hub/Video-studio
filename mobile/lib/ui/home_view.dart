// Start screen (MOB-01 in the approved prototype): the app opens here, never straight into the
// editor. "Dự án mới" (9:16 by default, or one of the aspect buttons), then the recent projects,
// newest first; "⋯" on a project opens or deletes it.
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'recent_strip.dart' show agoText, lengthText;
import 'theme.dart';

/// Canvas sizes for the aspect buttons.
const Map<String, (int, int)> kAspects = {'9:16': (1080, 1920), '16:9': (1920, 1080), '1:1': (1080, 1080)};

/// "16:9" for a canvas, or "" when it is none of the usual ones.
String aspectLabel(int w, int h) {
  if (w <= 0 || h <= 0) return '';
  for (final e in {...kAspects, '4:5': (1080, 1350)}.entries) {
    if ((w * e.value.$2 - h * e.value.$1).abs() <= w * e.value.$2 * 0.01) return e.key;
  }
  return '$w×$h';
}

class HomeView extends StatelessWidget {
  const HomeView({
    super.key,
    required this.projects,
    required this.onNew,
    required this.onOpen,
    required this.onDelete,
    required this.onImportPackage,
  });

  final List<ProjectCard> projects;
  final ValueChanged<String> onNew; // aspect key of kAspects
  final ValueChanged<ProjectCard> onOpen, onDelete;
  final VoidCallback onImportPackage;

  Future<void> _menu(BuildContext context, ProjectCard p) async {
    final action = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: VsColors.panel,
      builder: (context) => SafeArea(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 14, 16, 6),
            child: Text(p.name, style: const TextStyle(color: VsColors.fg2, fontSize: 13)),
          ),
          ListTile(
              leading: const Icon(Icons.edit_outlined),
              title: const Text('Mở dự án'),
              onTap: () => Navigator.pop(context, 'open')),
          ListTile(
              key: const Key('home-delete'),
              leading: const Icon(Icons.delete_outline, color: VsColors.bad),
              title: const Text('Xóa dự án', style: TextStyle(color: VsColors.bad)),
              onTap: () => Navigator.pop(context, 'delete')),
        ]),
      ),
    );
    if (!context.mounted || action == null) return;
    switch (action) {
      case 'open':
        onOpen(p);
      case 'delete':
        final ok = await showDialog<bool>(
          context: context,
          builder: (context) => AlertDialog(
            title: const Text('Xóa dự án?'),
            content: Text('"${p.name}" sẽ bị xóa khỏi máy. Video và ảnh gốc trong thư viện không bị xóa.'),
            actions: [
              TextButton(onPressed: () => Navigator.pop(context, false), child: const Text('Hủy')),
              TextButton(
                  key: const Key('home-delete-ok'),
                  onPressed: () => Navigator.pop(context, true),
                  child: const Text('Xóa', style: TextStyle(color: VsColors.bad))),
            ],
          ),
        );
        if (ok == true) onDelete(p);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 720),
            child: ListView(padding: const EdgeInsets.fromLTRB(18, 14, 18, 18), children: [
              const Text('Video Studio',
                  style: TextStyle(fontSize: 26, fontWeight: FontWeight.w800, color: VsColors.fg)),
              const SizedBox(height: 2),
              const Text('Dựng nhanh trên điện thoại, không cần mạng.',
                  style: TextStyle(color: VsColors.fg3, fontSize: 13)),
              const SizedBox(height: 18),
              Material(
                color: VsColors.accent,
                borderRadius: BorderRadius.circular(16),
                child: InkWell(
                  key: const Key('home-new'),
                  borderRadius: BorderRadius.circular(16),
                  onTap: () => onNew('9:16'),
                  child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 20),
                    child: Row(children: [
                      Container(
                        width: 44,
                        height: 44,
                        decoration: BoxDecoration(color: const Color(0x33000000), borderRadius: BorderRadius.circular(10)),
                        child: const Icon(Icons.add, color: VsColors.fg, size: 28),
                      ),
                      const Expanded(
                        child: Column(children: [
                          Text('Dự án mới',
                              style: TextStyle(fontSize: 17, fontWeight: FontWeight.w700, color: VsColors.fg)),
                          Text('Chọn video, ảnh từ thư viện', style: TextStyle(fontSize: 12, color: Color(0xCCFFFFFF))),
                        ]),
                      ),
                      const SizedBox(width: 44),
                    ]),
                  ),
                ),
              ),
              const SizedBox(height: 12),
              Row(children: [
                for (final (i, e) in kAspects.entries.indexed) ...[
                  if (i > 0) const SizedBox(width: 10),
                  Expanded(child: _AspectButton(label: e.key, w: e.value.$1, h: e.value.$2, onTap: () => onNew(e.key))),
                ],
              ]),
              const SizedBox(height: 22),
              Row(children: [
                const Expanded(
                  child: Text('Dự án gần đây',
                      style: TextStyle(fontSize: 15, fontWeight: FontWeight.w700, color: VsColors.fg)),
                ),
                TextButton.icon(
                  onPressed: onImportPackage,
                  icon: const Icon(Icons.move_to_inbox_outlined, size: 18),
                  label: const Text('Nhập gói', style: TextStyle(fontSize: 12)),
                ),
              ]),
              const SizedBox(height: 6),
              if (projects.isEmpty)
                const Padding(
                  padding: EdgeInsets.symmetric(vertical: 24),
                  child: Text('Chưa có dự án nào. Bấm "Dự án mới" để bắt đầu.',
                      textAlign: TextAlign.center, style: TextStyle(color: VsColors.fg3)),
                ),
              for (final p in projects)
                _ProjectRow(card: p, onTap: () => onOpen(p), onMore: () => _menu(context, p)),
            ]),
          ),
        ),
      ),
    );
  }
}

class _AspectButton extends StatelessWidget {
  const _AspectButton({required this.label, required this.w, required this.h, required this.onTap});
  final String label;
  final int w, h;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final k = 24 / (w > h ? w : h);
    return Material(
      color: VsColors.panel,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12), side: const BorderSide(color: VsColors.line)),
      child: InkWell(
        key: Key('home-new-$label'),
        borderRadius: BorderRadius.circular(12),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 14),
          child: Column(children: [
            Container(
              width: w * k,
              height: h * k,
              decoration: BoxDecoration(
                  border: Border.all(color: VsColors.fg, width: 1.6), borderRadius: BorderRadius.circular(3)),
            ),
            const SizedBox(height: 8),
            Text(label, style: const TextStyle(color: VsColors.fg2, fontSize: 12)),
          ]),
        ),
      ),
    );
  }
}

class _ProjectRow extends StatefulWidget {
  const _ProjectRow({required this.card, required this.onTap, required this.onMore});
  final ProjectCard card;
  final VoidCallback onTap, onMore;

  @override
  State<_ProjectRow> createState() => _ProjectRowState();
}

class _ProjectRowState extends State<_ProjectRow> {
  ui.Image? _poster;

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void didUpdateWidget(_ProjectRow old) {
    super.didUpdateWidget(old);
    if (old.card.path != widget.card.path || old.card.savedAt != widget.card.savedAt) _load();
  }

  void _load() {
    if (!widget.card.hasPoster) return;
    PosterImage? p;
    try {
      p = VStudio.projectPoster(widget.card.path);
    } catch (_) {
      return; // no core library (widget tests): the placeholder stays
    }
    if (p == null) return;
    ui.decodeImageFromPixels(p.rgba, p.width, p.height, ui.PixelFormat.rgba8888, (img) {
      if (!mounted) return img.dispose();
      setState(() {
        _poster?.dispose();
        _poster = img;
      });
    });
  }

  @override
  void dispose() {
    _poster?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final c = widget.card;
    final meta = [
      if (c.duration > 0) lengthText(c.duration),
      if (aspectLabel(c.width, c.height).isNotEmpty) aspectLabel(c.width, c.height),
      'sửa ${agoText(c.savedAt).toLowerCase()}',
    ].join(' · ');
    return InkWell(
      key: Key('home-project-${c.name}'),
      onTap: widget.onTap,
      borderRadius: BorderRadius.circular(12),
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 7),
        child: Row(children: [
          ClipRRect(
            borderRadius: BorderRadius.circular(10),
            child: Container(
              width: 64,
              height: 64,
              color: const Color(0xFF0B0D10),
              child: _poster == null
                  ? const Icon(Icons.movie_outlined, color: VsColors.fg3)
                  : RawImage(image: _poster, fit: BoxFit.cover),
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text(c.name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontWeight: FontWeight.w700, color: VsColors.fg, fontSize: 14)),
              const SizedBox(height: 2),
              Text(meta, maxLines: 1, style: const TextStyle(color: VsColors.fg3, fontSize: 12)),
            ]),
          ),
          IconButton(
            key: Key('home-more-${c.name}'),
            icon: const Icon(Icons.more_horiz, color: VsColors.fg3),
            tooltip: 'Tùy chọn',
            onPressed: widget.onMore,
          ),
        ]),
      ),
    );
  }
}
