import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import 'core/project_model.dart';
import 'core/native_preview.dart';
import 'core/vstudio.dart';
import 'ui/media_cache.dart';
import 'ui/theme.dart';
import 'ui/timeline.dart';

void main() => runApp(const VideoStudioApp());

class VideoStudioApp extends StatelessWidget {
  const VideoStudioApp({super.key});

  @override
  Widget build(BuildContext context) =>
      MaterialApp(title: 'Video Studio', theme: buildTheme(), home: const EditorPage(), debugShowCheckedModeBanner: false);
}

class EditorPage extends StatefulWidget {
  const EditorPage({super.key});

  @override
  State<EditorPage> createState() => _EditorPageState();
}

class _EditorPageState extends State<EditorPage> with SingleTickerProviderStateMixin {
  VStudio? _vs;
  String? _error;
  ProjectView? _project;
  int _revision = -1;
  ui.Image? _frame;
  int? _textureId; // native preview texture (Android); null = poll frames through FFI
  bool _decoding = false;
  double _playhead = 0;
  bool _playing = false;
  String? _selected;
  String? _status;
  double? _exportProgress;
  late final Ticker _ticker;
  final MediaCache _media = MediaCache();
  Timer? _autosave;

  @override
  void initState() {
    super.initState();
    try {
      _vs = VStudio.create('Dự án mới', width: 1080, height: 1920);
      _vs!.setPreviewSize(720, 1280);
      _refresh();
      debugPrint('vstudio: core ${_vs!.version} ready'); // CI smoke test looks for this line
      _attachNativePreview();
    } catch (e) {
      _error = 'Không nạp được Video Studio Core: $e';
      debugPrint('vstudio: core failed: $e');
    }
    // Per-frame UI tick: polls the core for a new preview frame / playhead.
    // Nothing here decodes; it only copies a ready frame.
    _ticker = createTicker(_onTick)..start();
    _autosave = Timer.periodic(const Duration(seconds: 10), (_) {
      try {
        _vs?.autosave();
      } catch (_) {}
    });
  }

  Future<void> _attachNativePreview() async {
    final vs = _vs;
    if (vs == null || !NativePreview.supported) return;
    try {
      final id = await NativePreview.create(vs, 720, 1280);
      if (!mounted || id == null) return;
      setState(() => _textureId = id);
      vs.seek(_playhead); // first frame
      debugPrint('vstudio: native preview texture $id');
    } catch (e) {
      debugPrint('vstudio: native preview unavailable, polling frames: $e');
    }
  }

  @override
  void dispose() {
    _ticker.dispose();
    _autosave?.cancel();
    _media.dispose();
    final vs = _vs;
    // The surface must be detached before the session it renders from is destroyed.
    if (_textureId != null) {
      NativePreview.dispose().whenComplete(() => vs?.dispose());
    } else {
      vs?.dispose();
    }
    super.dispose();
  }

  void _refresh() {
    final vs = _vs;
    if (vs == null) return;
    if (vs.revision != _revision) {
      _revision = vs.revision;
      _project = vs.project();
      _media.sync(vs, _project!);
    }
  }

  void _onTick(Duration _) {
    final vs = _vs;
    if (vs == null) return;
    _media.poll(vs);
    final playing = vs.playing;
    if (playing) {
      final ph = vs.playhead;
      final end = _project?.duration ?? 0;
      if (ph >= end && end > 0) {
        vs.pause();
        _playhead = end;
      } else {
        _playhead = ph;
      }
    }
    if (_textureId == null && !_decoding) {
      final f = vs.takeFrame();
      if (f != null) {
        _decoding = true;
        ui.decodeImageFromPixels(f.pixels, f.width, f.height, ui.PixelFormat.rgba8888, (img) {
          _decoding = false;
          if (!mounted) return;
          setState(() {
            _frame?.dispose();
            _frame = img;
          });
        });
      }
    }
    if (_exportProgress != null) {
      final (state, p) = vs.exportStatus();
      if (state == ExportState.running) {
        _exportProgress = p;
      } else {
        _exportProgress = null;
        _status = switch (state) {
          ExportState.done => 'Đã xuất video',
          ExportState.cancelled => 'Đã hủy xuất',
          _ => 'Xuất lỗi: ${vs.lastError}',
        };
      }
    }
    if (playing || playing != _playing || _exportProgress != null) setState(() => _playing = playing);
  }

  void _edit(void Function(VStudio vs) f) {
    final vs = _vs;
    if (vs == null) return;
    try {
      f(vs);
      setState(_refresh);
    } on VStudioException catch (e) {
      setState(() => _status = e.message);
    }
  }

  Future<void> _import() async {
    final files = await FilePicker.pickFiles(type: FileType.media);
    if (files.isEmpty) return;
    _edit((vs) {
      var track = _project?.tracks.where((t) => t.type == 'video').firstOrNull?.id;
      track ??= vs.addTrack(audio: false, name: 'Video 1');
      for (final f in files) {
        if (f.path == null) continue;
        final asset = vs.importMedia(f.path!);
        _selected = vs.appendClip(track, asset);
      }
    });
    _vs?.seek(_playhead);
  }

  Future<void> _export() async {
    if (_exportProgress != null) {
      _vs?.cancelExport();
      return;
    }
    final dir = Directory.systemTemp.path; // P0: app sandbox; V0.1 saves to Photos/Files
    final out = '$dir/video_studio_${DateTime.now().millisecondsSinceEpoch}.mp4';
    _edit((vs) {
      vs.startExport(out, width: 720, height: 1280);
      _exportProgress = 0;
      _status = 'Đang xuất: $out';
    });
  }

  // Media moved or deleted: pick the file again (the core keeps every clip and its edits).
  Future<void> _relink(String assetId) async {
    final files = await FilePicker.pickFiles(type: FileType.media);
    final path = files.firstOrNull?.path;
    if (path == null) return;
    _media.invalidate(assetId);
    _edit((vs) {
      vs.relink(assetId, path);
      _status = 'Đã liên kết lại file';
    });
    _vs?.seek(_playhead);
  }

  void _seek(double t) {
    setState(() => _playhead = t.clamp(0, _project?.duration ?? 0).toDouble());
    if (_vs?.playing ?? false) {
      _vs!.play(_playhead);
    } else {
      _vs?.seek(_playhead);
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Scaffold(body: Center(child: Padding(padding: const EdgeInsets.all(24), child: Text(_error!))));
    }
    final project = _project!;
    return Scaffold(
      appBar: AppBar(
        title: Text(project.name, style: const TextStyle(fontSize: 16)),
        actions: [
          IconButton(icon: const Icon(Icons.undo), onPressed: () => _edit((vs) => vs.undo())),
          IconButton(icon: const Icon(Icons.redo), onPressed: () => _edit((vs) => vs.redo())),
          Padding(
            padding: const EdgeInsets.only(right: 12),
            child: FilledButton(
              onPressed: project.duration > 0 ? _export : null,
              child: Text(_exportProgress == null ? 'Xuất' : '${(_exportProgress! * 100).round()}%  ✕'),
            ),
          ),
        ],
      ),
      body: Column(
        children: [
          Expanded(
            child: Container(
              color: Colors.black,
              alignment: Alignment.center,
              child: _textureId != null && (_project?.duration ?? 0) > 0
                  ? AspectRatio(aspectRatio: 720 / 1280, child: Texture(textureId: _textureId!))
                  : _frame == null
                      ? const Text('Nhập video để bắt đầu', style: TextStyle(color: VsColors.fg3))
                      : RawImage(image: _frame, fit: BoxFit.contain),
            ),
          ),
          Container(
            color: VsColors.panel,
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
            child: Row(children: [
              Text('${_playhead.toStringAsFixed(2)} s',
                  style: const TextStyle(fontFeatures: [ui.FontFeature.tabularFigures()], color: VsColors.fg2)),
              const Spacer(),
              IconButton(
                iconSize: 34,
                color: VsColors.accent,
                icon: Icon(_playing ? Icons.pause_circle_filled : Icons.play_circle_fill),
                onPressed: () {
                  final vs = _vs!;
                  if (vs.playing) {
                    vs.pause();
                  } else {
                    vs.play(_playhead >= project.duration ? 0 : _playhead);
                  }
                },
              ),
              const Spacer(),
              Text('${project.duration.toStringAsFixed(1)} s', style: const TextStyle(color: VsColors.fg3)),
            ]),
          ),
          SizedBox(
            height: 22 + 56.0 * (project.tracks.isEmpty ? 1 : project.tracks.length) + 8,
            child: TimelineView(
              project: project,
              playhead: _playhead,
              selectedClip: _selected,
              onSeek: _seek,
              onSelect: (id) => setState(() => _selected = id),
              onMove: (clip, track, start) => _edit((vs) => vs.moveClip(clip, track, start)),
              media: _media,
            ),
          ),
          for (final a in project.assets.where((a) => _media.missing.contains(a.id)))
            Material(
              color: const Color(0xFF4A1C1C),
              child: ListTile(
                dense: true,
                leading: const Icon(Icons.link_off, color: Color(0xFFFF8A80)),
                title: Text('Không tìm thấy file: ${a.name}', style: const TextStyle(color: VsColors.fg, fontSize: 13)),
                trailing: TextButton(onPressed: () => _relink(a.id), child: const Text('Chọn lại')),
              ),
            ),
          if (_status != null)
            Container(
              width: double.infinity,
              color: VsColors.panel,
              padding: const EdgeInsets.fromLTRB(12, 2, 12, 2),
              child: Text(_status!, style: const TextStyle(color: VsColors.fg3, fontSize: 11), maxLines: 1),
            ),
        ],
      ),
      bottomNavigationBar: BottomAppBar(
        color: VsColors.panel,
        child: Row(mainAxisAlignment: MainAxisAlignment.spaceAround, children: [
          _Tool(icon: Icons.add_photo_alternate_outlined, label: 'Nhập', onTap: _import),
          _Tool(icon: Icons.content_cut, label: 'Tách', onTap: () => _edit((vs) => vs.splitAt(_playhead))),
          _Tool(
              icon: Icons.delete_outline,
              label: 'Xóa',
              onTap: _selected == null
                  ? null
                  : () => _edit((vs) {
                        vs.removeClip(_selected!);
                        _selected = null;
                      })),
        ]),
      ),
    );
  }
}

class _Tool extends StatelessWidget {
  const _Tool({required this.icon, required this.label, required this.onTap});
  final IconData icon;
  final String label;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) => InkWell(
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            Icon(icon, color: onTap == null ? VsColors.fg3 : VsColors.fg),
            Text(label, style: TextStyle(fontSize: 11, color: onTap == null ? VsColors.fg3 : VsColors.fg2)),
          ]),
        ),
      );
}
