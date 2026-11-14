// "Tiếp tục dự án": when the app opens, the projects not exported since their last change slide up
// at the bottom of the screen (poster, name, when, length). One tap opens one. The strip slides
// away when a project is opened, when editing starts, or with ✕.
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../core/vstudio.dart';
import 'theme.dart';

/// "2 giờ trước", "Hôm qua"... for a save time.
String agoText(DateTime t, {DateTime? now}) {
  final d = (now ?? DateTime.now()).difference(t);
  if (d.inMinutes < 1) return 'Vừa xong';
  if (d.inHours < 1) return '${d.inMinutes} phút trước';
  if (d.inDays < 1) return '${d.inHours} giờ trước';
  if (d.inDays < 2) return 'Hôm qua';
  if (d.inDays < 7) return '${d.inDays} ngày trước';
  return '${t.day}/${t.month}/${t.year}';
}

/// 83 -> "1:23", 3725 -> "1:02:05".
String lengthText(double seconds) {
  final s = seconds.round();
  String two(int v) => v.toString().padLeft(2, '0');
  return s >= 3600 ? '${s ~/ 3600}:${two(s ~/ 60 % 60)}:${two(s % 60)}' : '${s ~/ 60}:${two(s % 60)}';
}

class RecentStrip extends StatelessWidget {
  const RecentStrip(
      {super.key,
      required this.cards,
      required this.shown,
      required this.onOpen,
      required this.onClose,
      required this.onAll});
  final List<ProjectCard> cards;
  final bool shown;
  final ValueChanged<ProjectCard> onOpen;
  final VoidCallback onClose, onAll;

  @override
  Widget build(BuildContext context) {
    final visible = shown && cards.isNotEmpty;
    // Slides and fades together; never pops in or out.
    return IgnorePointer(
      ignoring: !visible,
      child: AnimatedSlide(
        offset: visible ? Offset.zero : const Offset(0, 0.25),
        duration: const Duration(milliseconds: 320),
        curve: visible ? Curves.easeOutCubic : Curves.easeInCubic,
        child: AnimatedOpacity(
          opacity: visible ? 1 : 0,
          duration: const Duration(milliseconds: 260),
          child: Container(
            key: const Key('recent-strip'),
            margin: const EdgeInsets.fromLTRB(8, 0, 8, 8),
            padding: const EdgeInsets.fromLTRB(12, 4, 4, 10),
            decoration: BoxDecoration(
              color: VsColors.panel,
              borderRadius: BorderRadius.circular(14),
              border: Border.all(color: VsColors.line),
              boxShadow: const [BoxShadow(color: Colors.black54, blurRadius: 18, offset: Offset(0, 6))],
            ),
            child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
              Row(children: [
                Expanded(
                  child: Text('Tiếp tục dự án chưa xong (${cards.length})',
                      style: const TextStyle(fontWeight: FontWeight.w600, color: VsColors.fg)),
                ),
                TextButton(key: const Key('recent-all'), onPressed: onAll, child: const Text('Tất cả')),
                IconButton(
                    key: const Key('recent-close'),
                    icon: const Icon(Icons.close, size: 20, color: VsColors.fg2),
                    tooltip: 'Ẩn',
                    onPressed: onClose),
              ]),
              SizedBox(
                height: 128,
                child: ListView.separated(
                  scrollDirection: Axis.horizontal,
                  itemCount: cards.length,
                  separatorBuilder: (_, __) => const SizedBox(width: 10),
                  itemBuilder: (context, i) => _Card(card: cards[i], onTap: () => onOpen(cards[i])),
                ),
              ),
            ]),
          ),
        ),
      ),
    );
  }
}

class _Card extends StatefulWidget {
  const _Card({required this.card, required this.onTap});
  final ProjectCard card;
  final VoidCallback onTap;

  @override
  State<_Card> createState() => _CardState();
}

class _CardState extends State<_Card> {
  ui.Image? _poster;

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  void didUpdateWidget(_Card old) {
    super.didUpdateWidget(old);
    if (old.card.path != widget.card.path || old.card.savedAt != widget.card.savedAt) _load();
  }

  void _load() {
    if (!widget.card.hasPoster) return;
    final p = VStudio.projectPoster(widget.card.path);
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
    final meta = [agoText(c.savedAt), if (c.duration > 0) lengthText(c.duration)].join(' · ');
    return Material(
      color: VsColors.panel2,
      borderRadius: BorderRadius.circular(10),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        key: Key('recent-${c.name}'),
        onTap: widget.onTap,
        child: SizedBox(
          width: 132,
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Container(
              height: 74,
              width: double.infinity,
              color: const Color(0xFF0B0D10),
              alignment: Alignment.center,
              child: _poster == null
                  ? const Icon(Icons.movie_outlined, color: VsColors.fg3)
                  : RawImage(image: _poster, fit: BoxFit.contain),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(8, 6, 8, 0),
              child: Text(c.name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13, color: VsColors.fg)),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(8, 2, 8, 0),
              child: Text(meta,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontSize: 11, color: VsColors.fg3)),
            ),
          ]),
        ),
      ),
    );
  }
}
