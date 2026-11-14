// "Media" pane on wide screens (unfolded foldables, tablets), as in the prototype: what the
// project has imported, ready to add again; "Nhập" brings more from the phone's library.
// Tap a tile to add it at the end of the main video lane.
import 'package:flutter/material.dart';

import '../core/project_model.dart';
import 'media_cache.dart';
import 'recent_strip.dart' show lengthText;
import 'theme.dart';

class MediaBin extends StatefulWidget {
  const MediaBin({super.key, required this.project, required this.media, required this.onAdd, required this.onImport});
  final ProjectView project;
  final MediaCache media;
  final ValueChanged<String> onAdd; // asset id
  final VoidCallback onImport;

  @override
  State<MediaBin> createState() => _MediaBinState();
}

class _MediaBinState extends State<MediaBin> {
  String _kind = 'video';

  @override
  Widget build(BuildContext context) {
    final assets = widget.project.assets.where((a) => _kind == 'all' || a.type == _kind).toList();
    return Padding(
      padding: const EdgeInsets.fromLTRB(14, 12, 14, 8),
      child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Row(children: [
          const Expanded(child: Text('Media', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700))),
          FilledButton.icon(
            key: const Key('bin-import'),
            onPressed: widget.onImport,
            icon: const Icon(Icons.add, size: 18),
            label: const Text('Nhập'),
          ),
        ]),
        const SizedBox(height: 8),
        Wrap(spacing: 6, children: [
          for (final (k, label) in const [('video', 'Video'), ('image', 'Ảnh'), ('audio', 'Âm thanh'), ('all', 'Tất cả')])
            ChoiceChip(
              key: Key('bin-$k'),
              label: Text(label, style: const TextStyle(fontSize: 12)),
              selected: _kind == k,
              showCheckmark: false,
              visualDensity: VisualDensity.compact,
              onSelected: (_) => setState(() => _kind = k),
            ),
        ]),
        const SizedBox(height: 8),
        Expanded(
          child: assets.isEmpty
              ? const Center(
                  child: Text('Chưa có gì. Bấm "Nhập" để chọn video, ảnh từ máy.',
                      textAlign: TextAlign.center, style: TextStyle(color: VsColors.fg3)))
              : AnimatedBuilder(
                  animation: widget.media,
                  builder: (context, _) => GridView.extent(
                    maxCrossAxisExtent: 120,
                    mainAxisSpacing: 6,
                    crossAxisSpacing: 6,
                    children: [for (final a in assets) _Tile(asset: a, media: widget.media, onTap: () => widget.onAdd(a.id))],
                  ),
                ),
        ),
        const Text('Chạm để thêm vào cuối video.', style: TextStyle(fontSize: 11, color: VsColors.fg3)),
      ]),
    );
  }
}

class _Tile extends StatelessWidget {
  const _Tile({required this.asset, required this.media, required this.onTap});
  final AssetView asset;
  final MediaCache media;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final t = media.thumbs(asset.id);
    return Material(
      color: const Color(0xFF0B0D10),
      borderRadius: BorderRadius.circular(8),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        key: Key('bin-asset-${asset.name}'),
        onTap: onTap,
        child: Stack(fit: StackFit.expand, children: [
          if (t != null)
            CustomPaint(painter: _FirstFrame(t))
          else
            Icon(asset.type == 'audio' ? Icons.music_note : Icons.movie_outlined, color: VsColors.fg3),
          Positioned(
            right: 4,
            bottom: 4,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
              decoration: BoxDecoration(color: Colors.black54, borderRadius: BorderRadius.circular(3)),
              child: Text(asset.type == 'image' ? 'ẢNH' : lengthText(asset.duration),
                  style: const TextStyle(fontSize: 10, color: VsColors.fg)),
            ),
          ),
        ]),
      ),
    );
  }
}

/// The middle frame of a thumbnail strip, covering the tile.
class _FirstFrame extends CustomPainter {
  _FirstFrame(this.t);
  final ThumbImage t;

  @override
  void paint(Canvas canvas, Size size) {
    final i = t.count ~/ 2;
    final src = Rect.fromLTWH(0, i * t.height.toDouble(), t.width.toDouble(), t.height.toDouble());
    final k = (size.width / src.width) > (size.height / src.height) ? size.width / src.width : size.height / src.height;
    final w = src.width * k, h = src.height * k;
    canvas.drawImageRect(t.image, src, Rect.fromLTWH((size.width - w) / 2, (size.height - h) / 2, w, h),
        Paint()..filterQuality = FilterQuality.medium);
  }

  @override
  bool shouldRepaint(_FirstFrame old) => old.t != t;
}
