import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:gal/gal.dart';
import 'package:path_provider/path_provider.dart';

import 'core/project_model.dart';
import 'core/native_preview.dart';
import 'core/project_store.dart';
import 'core/vstudio.dart';
import 'ui/media_cache.dart';
import 'ui/theme.dart';
import 'ui/timeline.dart';

void main() => runApp(const VideoStudioApp());

class VideoStudioApp extends StatelessWidget {
  const VideoStudioApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
      title: 'Video Studio', theme: buildTheme(), home: const EditorPage(), debugShowCheckedModeBanner: false);
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
  MediaCache _media = MediaCache();
  ProjectStore? _store;
  String? _packagePath; // the open project's .vstudio folder
  Timer? _saveTimer;
  late final AppLifecycleListener _lifecycle;

  @override
  void initState() {
    super.initState();
    // Per-frame UI tick: polls the core for a new preview frame / playhead.
    // Nothing here decodes; it only copies a ready frame.
    _ticker = createTicker(_onTick)..start();
    // Leaving the app (home button, call, task switch) saves right away: the OS may kill it later.
    _lifecycle = AppLifecycleListener(onInactive: _saveNow, onPause: _saveNow, onDetach: _saveNow);
    _openInitial();
  }

  /// Reopens the last project, or starts a new one saved under the app's documents.
  Future<void> _openInitial() async {
    try {
      final docs = await getApplicationDocumentsDirectory();
      final store = ProjectStore(Directory('${docs.path}/VideoStudio'));
      await store.ensure();
      _store = store;
      final last = await store.lastProject();
      VStudio vs;
      String path;
      if (last != null) {
        vs = VStudio.open(last);
        path = last;
      } else {
        (vs, path) = await _createProject(store, 'Dự án mới');
      }
      if (!mounted) return vs.dispose();
      await _attach(vs, path);
      debugPrint('vstudio: core ${vs.version} ready'); // CI smoke test looks for this line
    } catch (e) {
      setState(() => _error = 'Không nạp được Video Studio Core: $e');
      debugPrint('vstudio: core failed: $e');
    }
  }

  Future<(VStudio, String)> _createProject(ProjectStore store, String name) async {
    final path = await store.newProjectPath(name);
    final vs = VStudio.create(name, width: 1080, height: 1920);
    vs.save(path); // a package from the start: media paths are stored relative to it
    return (vs, path);
  }

  Future<void> _attach(VStudio vs, String path) async {
    vs.setPreviewSize(720, 1280);
    setState(() {
      _vs = vs;
      _packagePath = path;
      _revision = -1;
      _playhead = 0;
      _selected = null;
      _refresh();
    });
    await _store?.setLastProject(path);
    await _attachNativePreview();
    vs.seek(0);
  }

  /// Saves (if needed), releases the current session, then opens [path] or a new project.
  Future<void> _switchProject({String? path, String? newName}) async {
    final store = _store, old = _vs;
    if (store == null || old == null || path == _packagePath) return;
    _saveNow();
    _saveTimer?.cancel();
    old.pause();
    // The native surface renders from the old session: detach it before destroying the session.
    if (_textureId != null) {
      await NativePreview.dispose();
      _textureId = null;
    }
    setState(() {
      _vs = null;
      _project = null;
      _frame?.dispose();
      _frame = null;
    });
    old.dispose();
    _media.dispose();
    _media = MediaCache();
    try {
      final (vs, p) = path != null ? (VStudio.open(path), path) : await _createProject(store, newName ?? 'Dự án mới');
      await _attach(vs, p);
    } catch (e) {
      setState(() => _error = 'Không mở được dự án: $e');
    }
  }

  // Edits only touch metadata, so a full save is small and atomic (temp file + rename).
  // Coalesced: one save 1.5 s after the last edit, and at once when the app goes to background.
  void _scheduleSave() {
    _saveTimer?.cancel();
    _saveTimer = Timer(const Duration(milliseconds: 1500), _saveNow);
  }

  void _saveNow() {
    final vs = _vs;
    if (vs == null || _packagePath == null || !vs.dirty) return;
    try {
      vs.save(_packagePath!);
    } on VStudioException catch (e) {
      if (mounted) setState(() => _status = 'Lưu lỗi: ${e.message}');
    }
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
    _saveNow();
    _saveTimer?.cancel();
    _lifecycle.dispose();
    _ticker.dispose();
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
        if (state == ExportState.done) {
          _saveExportToPhotos();
        } else if (_exportPath != null) {
          File(_exportPath!).delete().ignore(); // cancelled or failed: no half-written file left behind
          _exportPath = null;
        }
        _status = switch (state) {
          ExportState.done => 'Đã xuất video, đang lưu vào Ảnh…',
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
      _scheduleSave();
    } on VStudioException catch (e) {
      setState(() => _status = e.message);
    }
  }

  static const _mediaExtensions = [
    'mp4', 'mov', 'm4v', 'mkv', 'webm', 'avi', '3gp', //
    'mp3', 'm4a', 'aac', 'wav', 'flac', 'ogg', //
    'jpg', 'jpeg', 'png', 'webp', 'heic',
  ];

  Future<void> _import() async {
    // Photos = the system photo library; Files = Files app / document providers (Drive, USB, ...).
    final source = await showModalBottomSheet<FileType>(
      context: context,
      backgroundColor: VsColors.panel,
      builder: (context) => SafeArea(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          ListTile(
              leading: const Icon(Icons.photo_library_outlined),
              title: const Text('Thư viện ảnh và video'),
              onTap: () => Navigator.pop(context, FileType.media)),
          ListTile(
              leading: const Icon(Icons.folder_open),
              title: const Text('Tệp'),
              onTap: () => Navigator.pop(context, FileType.custom)),
        ]),
      ),
    );
    if (source == null) return;
    final files = await FilePicker.pickFiles(
        type: source, allowedExtensions: source == FileType.custom ? _mediaExtensions : null);
    final paths = files.map((f) => f.path).whereType<String>().toList();
    if (paths.isEmpty || _store == null) return;
    // Pickers hand out temporary copies: keep our own under the app's media folder.
    final kept = <String>[];
    for (var i = 0; i < paths.length; i++) {
      if (mounted) setState(() => _status = 'Đang chép ${i + 1}/${paths.length}…');
      try {
        kept.add(await _store!.adoptMedia(paths[i]));
      } catch (e) {
        if (mounted) setState(() => _status = 'Không chép được file: $e');
      }
    }
    if (!mounted || kept.isEmpty) return;
    _edit((vs) {
      for (final path in kept) {
        final asset = vs.importMedia(path);
        // The core's probe decides: an asset with no picture goes to an audio track.
        final audioOnly = vs.project().assets.firstWhere((a) => a.id == asset).type == 'audio';
        final type = audioOnly ? 'audio' : 'video';
        var track = (vs.project().tracks.where((t) => t.type == type && !t.locked).firstOrNull)?.id;
        track ??= vs.addTrack(audio: audioOnly, name: audioOnly ? 'Audio 1' : 'Video 1');
        _selected = vs.appendClip(track, asset);
      }
      _status = 'Đã nhập ${kept.length} file';
    });
    _vs?.seek(_playhead);
  }

  Future<void> _projectsMenu() async {
    final store = _store;
    if (store == null) return;
    _saveNow();
    final projects = await store.list();
    if (!mounted) return;
    final choice = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: VsColors.panel,
      isScrollControlled: true,
      builder: (context) => SafeArea(
        child: ConstrainedBox(
          constraints: BoxConstraints(maxHeight: MediaQuery.sizeOf(context).height * 0.7),
          child: ListView(shrinkWrap: true, children: [
            ListTile(
                leading: const Icon(Icons.add),
                title: const Text('Dự án mới'),
                onTap: () => Navigator.pop(context, '')),
            const Divider(height: 1),
            for (final p in projects)
              ListTile(
                leading: Icon(p.path == _packagePath ? Icons.check : Icons.movie_outlined),
                title: Text(p.name),
                subtitle: Text(_ago(p.modified), style: const TextStyle(fontSize: 11)),
                onTap: () => Navigator.pop(context, p.path),
              ),
          ]),
        ),
      ),
    );
    if (choice == null || !mounted) return;
    if (choice.isEmpty) {
      await _switchProject(newName: 'Dự án ${projects.length + 1}');
    } else {
      await _switchProject(path: choice);
    }
  }

  static String _ago(DateTime t) {
    final d = DateTime.now().difference(t);
    if (d.inMinutes < 1) return 'vừa xong';
    if (d.inHours < 1) return '${d.inMinutes} phút trước';
    if (d.inDays < 1) return '${d.inHours} giờ trước';
    return '${t.day}/${t.month}/${t.year}';
  }

  String? _exportPath; // file being written; moved to Photos when done

  Future<void> _export() async {
    final vs = _vs;
    if (vs == null) return;
    if (_exportProgress != null) {
      vs.cancelExport();
      return;
    }
    final presets = vs.exportPresets();
    final preset = await showModalBottomSheet<ExportPresetView>(
      context: context,
      backgroundColor: VsColors.panel,
      isScrollControlled: true,
      builder: (context) => SafeArea(
        child: ConstrainedBox(
          constraints: BoxConstraints(maxHeight: MediaQuery.sizeOf(context).height * 0.75),
          child: ListView(shrinkWrap: true, children: [
            const Padding(
              padding: EdgeInsets.fromLTRB(16, 14, 16, 6),
              child: Text('Xuất video', style: TextStyle(color: VsColors.fg, fontSize: 16)),
            ),
            for (final p in presets)
              ListTile(
                title: Text(p.name),
                subtitle: Text(
                    '${p.width} × ${p.height} · ${p.fps.round()} khung/giây · khoảng ${_size(p.estimatedBytes)}\n'
                    '${p.description}',
                    style: const TextStyle(fontSize: 12)),
                isThreeLine: true,
                onTap: () => Navigator.pop(context, p),
              ),
          ]),
        ),
      ),
    );
    if (preset == null || !mounted) return;
    // Written to the app's temporary folder, then handed to the photo library.
    final out = '${Directory.systemTemp.path}/${_safeName(_project?.name ?? 'video')} ${preset.height}p.mp4';
    _exportPath = out;
    _edit((vs) {
      vs.startExportPreset(out, preset.id);
      _exportProgress = 0;
      _status = 'Đang xuất ${preset.name}…';
    });
  }

  Future<void> _saveExportToPhotos() async {
    final path = _exportPath;
    _exportPath = null;
    if (path == null) return;
    try {
      if (!await Gal.hasAccess() && !await Gal.requestAccess()) {
        if (mounted) setState(() => _status = 'Chưa có quyền lưu vào Ảnh. File nằm tại: $path');
        return;
      }
      await Gal.putVideo(path, album: 'Video Studio');
      await File(path).delete();
      if (mounted) setState(() => _status = 'Đã lưu video vào Ảnh (album Video Studio)');
    } catch (e) {
      if (mounted) setState(() => _status = 'Không lưu được vào Ảnh: $e. File nằm tại: $path');
    }
  }

  static String _size(int bytes) {
    final mb = bytes / 1e6;
    return mb >= 1000 ? '${(mb / 1000).toStringAsFixed(1)} GB' : '${mb < 10 ? mb.toStringAsFixed(1) : mb.round()} MB';
  }

  static String _safeName(String s) => s.replaceAll(RegExp(r'[\\/:*?"<>|]'), '_');

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

  // Track actions (long-press a track header, or "+ Track" above the headers).
  Future<void> _trackMenu(TrackView? t) async {
    final project = _project;
    if (project == null) return;
    final sameType = t == null ? 0 : project.tracks.where((x) => x.audio == t.audio).length;
    // Shown top-down: for video a higher index is a higher layer.
    final up = t == null ? -1 : (t.audio ? t.indexInType - 1 : t.indexInType + 1);
    final down = t == null ? -1 : (t.audio ? t.indexInType + 1 : t.indexInType - 1);
    final action = await showModalBottomSheet<String>(
      context: context,
      backgroundColor: VsColors.panel,
      builder: (context) => SafeArea(
        child: Column(mainAxisSize: MainAxisSize.min, children: [
          if (t != null)
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
              child: Text(t.name, style: const TextStyle(color: VsColors.fg2, fontSize: 13)),
            ),
          ListTile(
              leading: const Icon(Icons.video_library_outlined),
              title: const Text('Thêm track video'),
              onTap: () => Navigator.pop(context, 'addVideo')),
          ListTile(
              leading: const Icon(Icons.audiotrack_outlined),
              title: const Text('Thêm track âm thanh'),
              onTap: () => Navigator.pop(context, 'addAudio')),
          if (t != null) ...[
            const Divider(height: 1),
            ListTile(
                leading: const Icon(Icons.edit_outlined),
                title: const Text('Đổi tên'),
                onTap: () => Navigator.pop(context, 'rename')),
            ListTile(
                leading: const Icon(Icons.arrow_upward),
                title: const Text('Lên trên'),
                enabled: up >= 0 && up < sameType,
                onTap: () => Navigator.pop(context, 'up')),
            ListTile(
                leading: const Icon(Icons.arrow_downward),
                title: const Text('Xuống dưới'),
                enabled: down >= 0 && down < sameType,
                onTap: () => Navigator.pop(context, 'down')),
            ListTile(
                leading: const Icon(Icons.delete_outline),
                title: const Text('Xóa track'),
                onTap: () => Navigator.pop(context, 'remove')),
          ],
        ]),
      ),
    );
    if (!mounted || action == null) return;
    String nextName(bool audio) =>
        '${audio ? 'Audio' : 'Video'} ${project.tracks.where((x) => x.audio == audio).length + 1}';
    switch (action) {
      case 'addVideo':
        _edit((vs) => vs.addTrack(audio: false, name: nextName(false)));
      case 'addAudio':
        _edit((vs) => vs.addTrack(audio: true, name: nextName(true)));
      case 'up':
        _edit((vs) => vs.moveTrack(t!.id, up));
      case 'down':
        _edit((vs) => vs.moveTrack(t!.id, down));
      case 'remove':
        _edit((vs) => vs.removeTrack(t!.id));
      case 'rename':
        final controller = TextEditingController(text: t!.name);
        final name = await showDialog<String>(
          context: context,
          builder: (context) => AlertDialog(
            title: const Text('Đổi tên track'),
            content: TextField(controller: controller, autofocus: true),
            actions: [
              TextButton(onPressed: () => Navigator.pop(context), child: const Text('Hủy')),
              TextButton(onPressed: () => Navigator.pop(context, controller.text.trim()), child: const Text('Lưu')),
            ],
          ),
        );
        controller.dispose();
        if (name != null && name.isNotEmpty) _edit((vs) => vs.renameTrack(t.id, name));
    }
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
    final project = _project;
    if (project == null) return const Scaffold(body: Center(child: CircularProgressIndicator()));
    return Scaffold(
      appBar: AppBar(
        title: Text(project.name, style: const TextStyle(fontSize: 16)),
        leading: IconButton(icon: const Icon(Icons.folder_outlined), tooltip: 'Dự án', onPressed: _projectsMenu),
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
              onTrackFlags: (t, {required enabled, required locked, required muted}) =>
                  _edit((vs) => vs.setTrackFlags(t.id, enabled: enabled, locked: locked, muted: muted)),
              onTrackMenu: _trackMenu,
              snapMove: (clip, start, tol) => _vs?.snapClipMove(clip, start, tol, playhead: _playhead),
              onTrim: (clip, {required start, required at}) => _edit((vs) => vs.trimClip(clip, start: start, at: at)),
              snapTime: (t, tol, clip) => _vs?.snapTime(t, tol, excludeClip: clip, playhead: _playhead),
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
          _Tool(
              icon: Icons.format_indent_decrease,
              label: 'Xóa dồn',
              onTap: _selected == null
                  ? null
                  : () => _edit((vs) {
                        vs.rippleDelete(_selected!);
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
