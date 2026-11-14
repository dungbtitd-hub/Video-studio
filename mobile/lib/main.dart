import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:gal/gal.dart';
import 'package:path_provider/path_provider.dart';
import 'package:record/record.dart';

import 'core/favorites.dart';
import 'core/project_model.dart';
import 'core/native_preview.dart';
import 'core/project_store.dart';
import 'core/transfer.dart';
import 'core/vstudio.dart';
import 'ui/audio_sheet.dart';
import 'ui/clip_tools_sheet.dart';
import 'ui/cover_panel.dart';
import 'ui/follow_button.dart';
import 'ui/grade_panel.dart';
import 'ui/keyframes.dart';
import 'ui/looks_sheet.dart';
import 'ui/media_bin.dart';
import 'ui/orient_sheet.dart';
import 'ui/range_bar.dart';
import 'ui/media_cache.dart';
import 'ui/home_view.dart';
import 'ui/speed_sheet.dart';
import 'ui/subtitle_sheet.dart';
import 'ui/text_sticker.dart';
import 'ui/theme.dart';
import 'ui/timeline.dart';
import 'ui/transition_sheet.dart';
import 'ui/user_presets.dart';
import 'ui/video_fx_sheet.dart';
import 'ui/workspace_layout.dart';

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
  // Covers (blur / mosaic... over a region) of the selected video clip.
  bool _coverMode = false;
  bool _gradeMode = false; // "Chỉnh màu" panel under the preview
  bool _picking = false; // eyedropper: next tap on the preview picks a colour
  final _gradeKey = GlobalKey<GradePanelState>();
  Timer? _scopeTimer; // refreshes the scope overlay while grading
  ui.Image? _scope;
  int _scopeKind = 0; // 0 waveform, 1 parade, 2 vectorscope, 3 histogram
  String? _activeCover;
  bool _tracking = false;
  double _trackProgress = 0;
  // Transitions: every place one can go (per video track), and the one being edited.
  Map<String, List<CutPoint>> _cuts = const {};
  CutPoint? _selectedCut;
  // Start screen (home) with the saved projects; the editor opens only when one is chosen.
  bool _home = true;
  List<ProjectCard> _projects = const [];
  bool _fullscreen = false; // preview only
  // "Đoạn áp dụng": the part an effect plays in, set with the playhead (range_bar.dart).
  TimeRange? _range;
  bool _keyMode = false; // the selected clip's panel shows keyframes instead of the range

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

  /// Opens the start screen: the saved projects, newest first. Nothing is opened yet.
  Future<void> _openInitial() async {
    try {
      final docs = await getApplicationDocumentsDirectory();
      final store = ProjectStore(Directory('${docs.path}/VideoStudio'));
      await store.ensure();
      _store = store;
      FavoritesStore.init(store.root.path);
      final version = VStudio.coreVersion; // loads the core library
      await _loadProjects();
      debugPrint('vstudio: core $version ready'); // CI smoke test looks for this line
    } catch (e) {
      setState(() => _error = 'Không nạp được Video Studio Core: $e');
      debugPrint('vstudio: core failed: $e');
    }
  }

  /// Every saved project for the start screen (small card files only, never media).
  Future<void> _loadProjects() async {
    final store = _store;
    if (store == null) return;
    var cards = <ProjectCard>[];
    try {
      cards = VStudio.recentProjects(store.projectsDir.path, unfinishedOnly: false, max: 500);
    } catch (e) {
      debugPrint('vstudio: project cards unavailable: $e');
    }
    // Packages saved before cards existed: listed from the folder.
    final known = cards.map((c) => c.path).toSet();
    for (final p in await store.list()) {
      if (!known.contains(p.path)) cards.add(ProjectCard(p.path, p.name, p.modified, 0, 0, 0, 0));
    }
    cards.sort((a, b) => b.savedAt.compareTo(a.savedAt));
    if (mounted) setState(() => _projects = cards);
  }

  Future<(VStudio, String)> _createProject(ProjectStore store, String name, {String aspect = '9:16'}) async {
    final path = await store.newProjectPath(name);
    final (w, h) = kAspects[aspect] ?? (1080, 1920);
    final vs = VStudio.create(name, width: w, height: h);
    vs.save(path); // a package from the start: media paths are stored relative to it
    return (vs, path);
  }

  /// Preview pixels for the project's frame shape (long side 1280).
  (int, int) _previewSize(VStudio vs) {
    final p = vs.project();
    final w = p.canvasWidth, h = p.canvasHeight;
    if (w <= 0 || h <= 0) return (720, 1280);
    return w >= h ? (1280, (1280 * h / w).round() & ~1) : ((1280 * w / h).round() & ~1, 1280);
  }

  Future<void> _attach(VStudio vs, String path) async {
    final (pw, ph) = _previewSize(vs);
    vs.setPreviewSize(pw, ph);
    final store = _store;
    if (store != null) vs.initPresets('${store.root.path}/presets.json'); // shared by every project
    setState(() {
      _vs = vs;
      _home = false;
      _packagePath = path;
      _revision = -1;
      _playhead = 0;
      _selected = null;
      _status = null;
      _refresh();
    });
    await _store?.setLastProject(path);
    await _attachNativePreview();
    vs.seek(0);
  }

  /// Saves and releases the open project (if any).
  Future<void> _closeProject() async {
    final old = _vs;
    if (old == null) return;
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
      _coverMode = false;
      _gradeMode = false;
      _fullscreen = false;
    });
    _stopScopes();
    old.dispose();
    _media.dispose();
    _media = MediaCache();
  }

  /// ✕ in the editor: back to the start screen.
  Future<void> _goHome() async {
    await _closeProject();
    setState(() => _home = true);
    await _loadProjects();
  }

  /// Opens [path], or a new project with [aspect] (then the media picker, as in the prototype).
  Future<void> _switchProject({String? path, String? aspect}) async {
    final store = _store;
    if (store == null || (path != null && path == _packagePath && _vs != null)) return;
    await _closeProject();
    try {
      final (vs, p) = path != null
          ? (VStudio.open(path), path)
          : await _createProject(store, 'Dự án ${_projects.length + 1}', aspect: aspect ?? '9:16');
      await _attach(vs, p);
      if (path == null && mounted) await _import();
    } catch (e) {
      setState(() {
        _home = true;
        _status = null;
      });
      _snack('Không mở được dự án: $e');
    }
  }

  Future<void> _deleteProject(ProjectCard p) async {
    if (p.path == _packagePath) await _closeProject();
    try {
      await Directory(p.path).delete(recursive: true);
      _snack('Đã xóa "${p.name}"');
    } catch (e) {
      _snack('Không xóa được dự án: $e');
    }
    await _loadProjects();
  }

  void _snack(String text) {
    if (!mounted) return;
    ScaffoldMessenger.of(context)
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(content: Text(text), behavior: SnackBarBehavior.floating));
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
    _stopScopes();
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
      _cuts = vs.cutPoints();
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
    if (_tracking) _pollTrack(vs);
    if (playing || playing != _playing || _exportProgress != null) setState(() => _playing = playing);
  }

  void _pollTrack(VStudio vs) {
    final (state, p) = vs.trackStatus();
    if (state == TrackState.running) {
      if ((p - _trackProgress).abs() >= 0.01) setState(() => _trackProgress = p);
      return;
    }
    setState(() {
      _tracking = false;
      _refresh(); // the keys were applied as one undo step
      _status = switch (state) {
        TrackState.done => 'Vùng che đã bám theo đối tượng. Kéo vùng ở chỗ lệch để sửa.',
        TrackState.lost => 'Mất dấu đối tượng ở giữa clip: phần đã bám được giữ lại, kéo vùng ở chỗ còn lại.',
        TrackState.cancelled => 'Đã dừng bám theo',
        _ => 'Không bám theo được: ${vs.lastError}',
      };
    });
    if (state == TrackState.done || state == TrackState.lost) {
      _scheduleSave();
      vs.seek(_playhead);
    }
  }

  void _edit(void Function(VStudio vs) f) {
    final vs = _vs;
    if (vs == null) return;
    try {
      f(vs);
      setState(() {
        _refresh();
      });
      _scheduleSave();
    } on VStudioException catch (e) {
      vs.liveEnd(); // a refused edit drops any live preview of it
      setState(() => _status = e.message);
    }
  }

  static const _mediaExtensions = [
    'mp4', 'mov', 'm4v', 'mkv', 'webm', 'avi', '3gp', //
    'mp3', 'm4a', 'aac', 'wav', 'flac', 'ogg', //
    'jpg', 'jpeg', 'png', 'webp', 'heic', //
    // Camera RAW photos (developed by LibRaw / Apple's RAW engine in the core).
    'dng', 'cr2', 'cr3', 'crw', 'nef', 'nrw', 'arw', 'srf', 'sr2', 'raf', 'orf', 'rw2', 'pef', 'srw', 'x3f', //
    '3fr', 'fff', 'iiq', 'erf', 'mef', 'mos', 'mrw', 'kdc', 'dcr', 'rwl',
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
              subtitle: const Text('Cả ảnh RAW từ máy ảnh (DNG, CR3, NEF, ARW, RAF…)'),
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
    _addMedia(kept);
  }

  /// The lane media goes on: the main video lane (bottom-most video track without texts,
  /// stickers, overlays or subtitles), made at the bottom when there is none.
  String _mediaLane(VStudio vs) {
    final lanes = vs.project().tracks.where((t) => t.takesMedia && !t.locked).toList();
    if (lanes.isNotEmpty) return lanes.last.id; // display order is top first
    final id = vs.addTrack(audio: false, name: 'Video');
    vs.moveTrack(id, 0);
    return id;
  }

  /// Imports [paths] and lays them on the timeline: pictures on the main video lane, sound on a
  /// sound lane. A file that cannot be read is reported by name; the others still go in.
  void _addMedia(List<String> paths) {
    final failed = <String>[];
    var added = 0;
    _edit((vs) {
      for (final path in paths) {
        try {
          final asset = vs.importMedia(path);
          // The core's probe decides: an asset with no picture goes to an audio track.
          final audioOnly = vs.project().assets.firstWhere((a) => a.id == asset).type == 'audio';
          String track;
          if (audioOnly) {
            track = vs.project().tracks.where((t) => t.audio && !t.locked).firstOrNull?.id ??
                vs.addTrack(audio: true, name: 'Âm thanh');
          } else {
            track = _mediaLane(vs);
          }
          _selected = vs.appendClip(track, asset);
          added++;
        } on VStudioException catch (e) {
          failed.add('${path.split('/').last}: ${_why(e.message)}');
        }
      }
      _status = added > 0 ? 'Đã thêm $added file' : null;
    });
    if (failed.isNotEmpty) {
      showDialog<void>(
        context: context,
        builder: (context) => AlertDialog(
          title: Text(failed.length == 1 ? 'Không thêm được 1 file' : 'Không thêm được ${failed.length} file'),
          content: Text(failed.join('\n')),
          actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('Đóng'))],
        ),
      );
    }
    if (added > 0 && _selected != null) {
      final c = _selectedClip;
      if (c != null) _seek(c.start);
    }
    _vs?.seek(_playhead);
  }

  /// The core's reasons, in the user's words.
  static String _why(String m) {
    if (m.contains('Android 11')) return 'máy cần Android 11 trở lên để mở ảnh';
    if (m.contains('unsupported') || m.contains('corrupt')) return 'định dạng chưa hỗ trợ hoặc file bị hỏng';
    if (m.contains('cannot open')) return 'không đọc được file';
    if (m.contains('no audio or video')) return 'file không có hình hay tiếng';
    return m;
  }

  TransferCancel? _transfer;

  /// Opens a project packed on another device: project + media are unpacked
  /// into the app's store (media reused when already there), then opened.
  Future<void> _importPackage() async {
    final store = _store;
    if (store == null || _transfer != null) return;
    // Any type: Android pickers do not know the .vstudiopkg extension.
    final files = await FilePicker.pickFiles(type: FileType.any);
    final path = files.map((f) => f.path).whereType<String>().firstOrNull;
    if (path == null || !mounted) return;
    final cancel = _transfer = TransferCancel();
    setState(() => _status = 'Đang mở gói dự án…');
    final r = await unpackPackage(path, store.root.path, cancel: cancel, onProgress: (p) {
      if (mounted) setState(() => _status = 'Đang mở gói dự án ${(p * 100).floor()}%');
    });
    _transfer = null;
    cancel.dispose();
    // The picker's temporary copy is no longer needed (it can be gigabytes).
    final tmp = (await getTemporaryDirectory()).path;
    if (path.startsWith(tmp)) {
      try {
        File(path).deleteSync();
      } catch (_) {}
    }
    if (!mounted) return;
    if (!r.ok) {
      setState(() => _status = r.cancelled ? 'Đã hủy mở gói' : 'Không mở được gói: ${r.error}');
      return;
    }
    await _switchProject(path: r.packageDir);
    if (mounted) setState(() => _status = 'Đã mở dự án từ gói');
  }

  ClipView? get _selectedVideoClip {
    for (final t in _project?.tracks ?? const <TrackView>[]) {
      if (t.audio) continue;
      for (final c in t.clips) {
        if (c.id == _selected) return c;
      }
    }
    return null;
  }

  /// The selected text, sticker or overlay clip.
  ClipView? get _selectedObject {
    final c = _selectedVideoClip;
    return c != null && c.generated ? c : null;
  }

  /// The selected video clip with media (looks and covers work on its picture).
  ClipView? get _selectedMediaClip {
    final c = _selectedVideoClip;
    return c != null && !c.generated ? c : null;
  }

  ClipView? get _selectedClip {
    for (final t in _project?.tracks ?? const <TrackView>[]) {
      for (final c in t.clips) {
        if (c.id == _selected) return c;
      }
    }
    return null;
  }

  // ---- keyframes: on the clip selected, at the playhead ----

  ClipKeyState? _keyState() {
    final vs = _vs, clip = _selectedClip;
    if (vs == null || clip == null) return null;
    try {
      final st = vs.clipStateAt(clip.id, _playhead);
      return st.inside ? st : null;
    } on VStudioException {
      return null;
    }
  }

  void _toggleKeyframe(KeyGroup group) {
    final clip = _selectedClip;
    if (clip == null) return;
    _edit((vs) {
      final added = vs.toggleKeyframe(clip.id, group, _playhead);
      final what = group == KeyGroup.picture ? 'hình ảnh' : 'âm thanh';
      _status = added
          ? (group == KeyGroup.picture
              ? 'Đã đặt keyframe hình ảnh. Sang điểm khác rồi dùng hai ngón tay phóng to vùng cần zoom.'
              : 'Đã đặt keyframe âm thanh. Sang điểm khác rồi kéo thanh âm lượng.')
          : 'Đã bỏ keyframe $what';
    });
    _vs?.seek(_playhead);
  }

  void _jumpKeyframe(bool forward) {
    final clip = _selectedClip;
    if (clip == null) return;
    final keys = {...clip.pictureKeys, ...clip.audioKeys}.toList()..sort();
    final halfFrame = 0.5 / (_project!.fps > 0 ? _project!.fps : 30);
    final next = forward
        ? keys.where((k) => k > _playhead + halfFrame).firstOrNull
        : keys.where((k) => k < _playhead - halfFrame).lastOrNull;
    if (next != null) _seek(next);
  }

  /// Two-finger pinch on the preview, already shown on screen: commit it as one edit.
  void _commitZoom(double f, Offset from, Offset to) {
    final vs = _vs, clip = _selectedVideoClip;
    if (vs == null || clip == null) return;
    final st = vs.clipStateAt(clip.id, _playhead);
    if (!st.inside) return;
    // The point under the fingers ends where the fingers are, everything scaled by f.
    final scale = (st.scale * f).clamp(0.05, 20).toDouble();
    final k = scale / st.scale;
    _edit((vs) {
      vs.setPropertyAt(
          clip.id, _playhead, {'scale': scale, 'x': to.dx + k * (st.x - from.dx), 'y': to.dy + k * (st.y - from.dy)});
      if (!st.pictureAnimated) {
        _status = 'Đã phóng to cả clip. Muốn zoom dần: chạm ◇ Hình ở điểm bắt đầu trước, rồi phóng to ở điểm sau.';
      }
    });
    vs.seek(_playhead);
  }

  void _resetZoom() {
    final clip = _selectedVideoClip;
    if (clip == null) return;
    _edit((vs) => vs.setPropertyAt(clip.id, _playhead, {'scale': 1, 'x': 0, 'y': 0}));
    _vs?.seek(_playhead);
  }

  // ---- text and stickers: clips without media, drawn by the core ----

  /// Length of a new text or sticker clip.
  static const _objectSeconds = 3.0;

  /// Length of a new overlay effect.
  static const _overlaySeconds = 5.0;

  /// Adds a text/sticker/overlay at the playhead and selects it; returns its content.
  Map<String, dynamic>? _addObject(Map<String, dynamic> content, {double seconds = _objectSeconds}) {
    final vs = _vs;
    if (vs == null) return null;
    final dur = _project?.duration ?? 0;
    final at = _playhead >= dur - 0.05 && dur > 0 ? math.max(0.0, dur - seconds) : _playhead;
    _edit((vs) {
      _selected = vs.addGeneratedClip(content, at, seconds);
      _coverMode = false;
      _activeCover = null;
    });
    if (_selectedObject == null) return null;
    _seek(at + 0.7); // past the in animation, so the new object shows
    return _selectedObject!.content;
  }

  void _patchObject(String clipId, Map<String, dynamic> patch) {
    _edit((vs) => vs.updateClipContent(clipId, patch));
    _vs?.seek(_playhead);
  }

  /// "Chữ": edits the selected text, or adds one.
  Future<void> _text() async {
    final vs = _vs;
    if (vs == null) return;
    var clip = _selectedObject;
    if (clip == null || clip.kind != 'text') {
      if (_addObject(vs.defaultText('Nhập chữ')) == null) return;
      clip = _selectedObject!;
    }
    final id = clip.id;
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.transparent,
      isScrollControlled: true,
      builder: (_) => TextSheet(
        vs: vs,
        content: clip!.content!,
        onPatch: (p) => _patchObject(id, p),
        follow: FollowButton(vs: vs, clipId: () => id, playhead: () => _playhead, onDone: () => _edit((_) {})),
      ),
    );
  }

  /// "Sticker": the library; picking one adds it at the playhead.
  Future<void> _stickers() async {
    final vs = _vs;
    if (vs == null) return;
    final selected = _selectedObject?.kind == 'sticker' ? _selectedObject : null;
    var target = selected?.id;
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.transparent,
      isScrollControlled: true,
      builder: (_) => StickerSheet(
        vs: vs,
        content: selected?.content,
        onPick: (sticker) {
          final c = _addObject(vs.defaultSticker(sticker));
          target = _selectedObject?.id;
          return c;
        },
        onPatch: (p) {
          if (target != null) _patchObject(target!, p);
        },
        onLive: (p) {
          if (target != null) _live((vs) => vs.liveClipContent(target!, p));
        },
        scale: () {
          final id = target;
          if (id == null) return 1.0;
          try {
            final st = vs.clipStateAt(id, _playhead);
            return st.inside ? st.scale : 1.0;
          } on VStudioException {
            return 1.0;
          }
        },
        follow: FollowButton(vs: vs, clipId: () => target, playhead: () => _playhead, onDone: () => _edit((_) {})),
      ),
    );
  }

  /// A gesture's live preview: the picture follows at once, nothing is edited or saved.
  void _live(void Function(VStudio vs) f) {
    final vs = _vs;
    if (vs == null) return;
    try {
      f(vs);
    } on VStudioException {
      // refused (e.g. too small): the preview keeps the last good one
    }
  }

  /// "Lớp phủ": overlay effects; picking one lays it over the whole frame from the playhead.
  Future<void> _overlays() async {
    final vs = _vs;
    if (vs == null) return;
    final selected = _selectedObject?.kind == 'overlay' ? _selectedObject : null;
    var target = selected?.id;
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.transparent,
      isScrollControlled: true,
      builder: (_) => OverlaySheet(
        vs: vs,
        content: selected?.content,
        onPick: (id) {
          final c = _addObject(vs.defaultOverlay(id), seconds: _overlaySeconds);
          target = _selectedObject?.id;
          return c;
        },
        onPatch: (p) {
          if (target != null) _patchObject(target!, p);
        },
        onFullFrame: () {
          final id = target;
          if (id == null) return;
          _edit((vs) => vs.setOverlayRegion(id, _playhead, 0, 0, 1, 1));
          vs.seek(_playhead);
        },
        onSpan: (all) {
          final id = target;
          final project = _project;
          if (id == null || project == null) return;
          // The end of everything else (the overlay itself does not count).
          final end = project.tracks
              .expand((t) => t.clips)
              .where((c) => c.id != id)
              .fold<double>(0, (m, c) => c.end > m ? c.end : m);
          final start = all ? 0.0 : _playhead.clamp(0.0, end).toDouble(); // "từ đây": from the playhead
          if (end - start < 0.05) {
            setState(() => _status = 'Chưa có video để phủ theo: nhập video trước');
            return;
          }
          _edit((vs) {
            vs.setGeneratedSpan(id, start, end);
            _status = all ? 'Lớp phủ chạy suốt video' : 'Lớp phủ chạy tới hết video';
          });
          vs.seek(_playhead);
        },
      ),
    );
  }

  /// "Phụ đề": lines, look, place and AI translation of subtitle tracks.
  Future<void> _subtitles() async {
    final vs = _vs;
    if (vs == null) return;
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.transparent,
      isScrollControlled: true,
      builder: (_) => SubtitleSheet(
        vs: vs,
        project: () {
          _refresh();
          return _project!;
        },
        playhead: () => _playhead,
        selected: () => _selected,
        onEdited: () {
          setState(_refresh);
          _scheduleSave();
          _vs?.seek(_playhead);
        },
        onSelect: (id, at) {
          setState(() => _selected = id);
          _seek(at);
        },
      ),
    );
  }

  /// A subtitle line dragged on the preview moves the whole track (or the line, when it has its own
  /// place); pulling a corner changes the text size. Rects are 0..1 of the frame.
  void _commitSubtitle(ClipView clip, Rect from, Rect to) {
    final track = _project?.tracks.where((t) => t.clips.any((c) => c.id == clip.id)).firstOrNull;
    if (track == null || from.width <= 0) return;
    final k = to.width / from.width;
    if ((k - 1).abs() > 0.01) {
      final style = track.subtitle?['style'];
      final size = (((style is Map ? style['size'] : null) ?? 56) as num).toDouble() * k;
      _edit((vs) => vs.setSubtitleSettings(track.id, {
            'style': {'size': size.clamp(12, 400).roundToDouble()}
          }));
    } else {
      final pos = {
        'anchor': 'custom',
        'h': 'center',
        'x': to.center.dx.clamp(0.0, 1.0),
        'y': to.center.dy.clamp(0.0, 1.0),
      };
      _edit((vs) => clip.content?['position'] is Map
          ? vs.updateClipContent(clip.id, {'position': pos})
          : vs.setSubtitleSettings(track.id, {'position': pos}));
    }
    _vs?.seek(_playhead);
  }

  /// The object's box was dragged / pinched on the preview: one edit (a key when it is animated).
  /// The selected text/sticker/overlay follows the fingers live (one edit when they lift).
  void _previewObject(Rect from, Rect to) {
    final clip = _selectedObject, project = _project;
    // Subtitles are placed by their track's position: only the box follows the finger until it lifts.
    if (clip == null || project == null || clip.kind == 'subtitle') return;
    _live((vs) {
      if (clip.kind == 'overlay') return vs.liveOverlayRegion(clip.id, _playhead, to.left, to.top, to.width, to.height);
      final st = vs.clipStateAt(clip.id, _playhead);
      if (!st.inside) return;
      final t = objectTransform(
          Size(project.canvasWidth.toDouble(), project.canvasHeight.toDouble()), from, to, st.x, st.y, st.scale);
      vs.livePropertyAt(clip.id, _playhead, {'scale': t.scale, 'x': t.x, 'y': t.y});
    });
  }

  void _commitObject(Rect from, Rect to) {
    final vs = _vs, clip = _selectedObject, project = _project;
    if (vs == null || clip == null || project == null) return;
    if (clip.kind == 'overlay') {
      _edit((vs) => vs.setOverlayRegion(clip.id, _playhead, to.left, to.top, to.width, to.height));
      vs.seek(_playhead);
      return;
    }
    if (clip.kind == 'subtitle') {
      _commitSubtitle(clip, from, to);
      return;
    }
    final st = vs.clipStateAt(clip.id, _playhead);
    if (!st.inside) return;
    final t = objectTransform(
        Size(project.canvasWidth.toDouble(), project.canvasHeight.toDouble()), from, to, st.x, st.y, st.scale);
    _edit((vs) => vs.setPropertyAt(clip.id, _playhead, {'scale': t.scale, 'x': t.x, 'y': t.y}));
    vs.seek(_playhead);
  }

  /// The box of the selected text/sticker over the preview, while nothing plays.
  Widget? _objectBox(Widget preview, ClipKeyState? keys) {
    final vs = _vs, clip = _selectedObject;
    if (vs == null || clip == null || keys == null || _playing) return null; // keys: the playhead is inside it
    final (double, double, double, double)? r;
    try {
      r = vs.generatedRect(clip.id, _playhead);
    } on VStudioException {
      return null;
    }
    if (r == null) return null;
    return Stack(fit: StackFit.expand, children: [
      preview,
      ObjectOverlay(
        key: const Key('object-overlay'),
        canvasSize: Size(_project!.canvasWidth.toDouble(), _project!.canvasHeight.toDouble()),
        rect: Rect.fromLTWH(r.$1, r.$2, r.$3, r.$4),
        onChanged: _commitObject,
        onChanging: _previewObject,
        onDoubleTap: switch (clip.kind) { 'text' => _text, 'overlay' => _overlays, 'subtitle' => _subtitles, _ => _stickers },
        freeShape: clip.kind == 'overlay',
      ),
    ]);
  }

  // ---- transitions: between touching clips, or a clip's free start / end ----

  CutPoint? _cutFor(String clipId, int edge) {
    for (final list in _cuts.values) {
      for (final c in list) {
        if (edge == 0 ? c.to == clipId : c.from == clipId) return c;
      }
    }
    return null;
  }

  Future<void> _transitions([CutPoint? at]) async {
    final vs = _vs;
    final cut = at ?? (_selectedVideoClip == null ? null : _cutFor(_selectedVideoClip!.id, 0));
    if (vs == null || cut == null) return;
    setState(() {
      _selectedCut = cut;
      _selected = cut.clipId;
    });
    // The sheet edits through these; each change is one undo step.
    void set(String? type, double seconds) {
      _edit((vs) => vs.setTransition(cut.clipId, cut.edge, type, seconds: seconds));
      _vs?.seek(cut.joined ? cut.at - 0.3 : _playhead);
    }

    await showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.black26,
      builder: (context) => TransitionSheet(
        vs: vs,
        cut: cut,
        onApply: (type, seconds) {
          set(type, seconds);
          _status = 'Đã đặt chuyển cảnh. Chạm ▶ để xem.';
        },
        onDuration: (seconds) => set(_cutFor(cut.clipId, cut.edge)?.type ?? cut.type, seconds),
        onRemove: () => set(null, 0.5),
        onApplyAll: (type, seconds) => _edit((vs) {
          final n = vs.setTransitionEverywhere(type, seconds: seconds);
          _status = 'Đã đặt chuyển cảnh cho $n chỗ nối';
        }),
      ),
    );
    if (mounted) setState(() => _selectedCut = null);
  }

  // ---- covers: on the selected video clip, edited at the playhead ----

  List<String> _coverIds() {
    final vs = _vs, clip = _selectedVideoClip;
    if (vs == null || clip == null) return const [];
    try {
      return [for (final (id, _) in vs.covers(clip.id)) id];
    } on VStudioException {
      return const [];
    }
  }

  /// Masks live with the covers (same region editing, keyframes and tracking); entering
  /// from the Mặt nạ tool starts with a soft oval mask when the clip has none yet.
  void _enterMasks() {
    final ids = _coverIds();
    setState(() {
      _gradeMode = false;
      _coverMode = true;
      _activeCover = ids.isEmpty ? null : ids.first;
      _status = 'Mặt nạ: phần ngoài vùng trong suốt để lộ track bên dưới. Chọn hình, kéo vùng, chỉnh viền mềm.';
    });
    if (ids.isEmpty) _addCover('mask_keep', 'ellipse');
  }

  void _enterCovers() {
    final ids = _coverIds();
    setState(() {
      _gradeMode = false;
      _coverMode = true;
      _activeCover = ids.isEmpty ? null : ids.first;
      _status = 'Chọn kiểu che, rồi kéo vùng trên khung hình. Kéo góc dưới bên phải hoặc dùng hai ngón để đổi cỡ.';
    });
    if (ids.isEmpty) _addCover('blur', 'rect');
  }

  void _addCover(String style, String shape) {
    final clip = _selectedVideoClip;
    if (clip == null) return;
    _edit((vs) => _activeCover = vs.addCover(clip.id, {'style': style, 'shape': shape}));
    _vs?.seek(_playhead);
  }

  /// Any change to the active cover, then a fresh preview frame.
  void _coverEdit(void Function(VStudio vs, String clip, String cover) f) {
    final clip = _selectedVideoClip, cover = _activeCover;
    if (clip == null || cover == null) return;
    _edit((vs) => f(vs, clip.id, cover));
    _vs?.seek(_playhead);
  }

  void _toggleTrack() {
    final vs = _vs, clip = _selectedVideoClip, cover = _activeCover;
    if (vs == null) return;
    if (_tracking) {
      vs.cancelTrack();
      return;
    }
    if (clip == null || cover == null) return;
    try {
      vs.startTrackCover(clip.id, cover, _playhead);
      setState(() {
        _tracking = true;
        _trackProgress = 0;
        _status = 'Đang bám theo đối tượng từ vị trí hiện tại đến cuối clip…';
      });
    } on VStudioException catch (e) {
      setState(() => _status = e.message);
    }
  }

  /// The active cover over the preview, while covering and the clip is on screen.
  Widget _coverable(Widget preview, ClipKeyState? keys) {
    if (_picking) {
      return LayoutBuilder(
        builder: (context, box) => GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTapUp: (d) {
            final c = _vs?.pickPreviewColor(d.localPosition.dx / box.maxWidth, d.localPosition.dy / box.maxHeight);
            setState(() => _picking = false);
            if (c != null) _gradeKey.currentState?.pickColor(Color.from(alpha: 1, red: c.$1, green: c.$2, blue: c.$3));
          },
          child: preview,
        ),
      );
    }
    final vs = _vs, clip = _selectedVideoClip, cover = _activeCover;
    if (!_coverMode || vs == null || clip == null || cover == null || keys == null || _playing) {
      return _objectBox(preview, keys) ?? _zoomable(preview, keys);
    }
    final (double, double, double, double)? r;
    final String shape;
    try {
      r = vs.coverCanvasRect(clip.id, cover, _playhead);
      shape = vs.coverStateAt(clip.id, cover, _playhead).shape;
    } on VStudioException {
      return preview;
    }
    if (r == null) return preview;
    return Stack(fit: StackFit.expand, children: [
      preview,
      CoverOverlay(
        canvasSize: Size(_project!.canvasWidth.toDouble(), _project!.canvasHeight.toDouble()),
        rect: Rect.fromLTWH(r.$1, r.$2, r.$3, r.$4),
        shape: shape,
        onChanged: (n) => _coverEdit(
            (vs, clip, cover) => vs.setCoverCanvasRect(clip, cover, _playhead, n.left, n.top, n.width, n.height)),
      ),
    ]);
  }

  static const _scopeW = 192, _scopeH = 108;

  /// Scope of what the preview shows, a few times a second while grading (a couple of ms each).
  void _startScopes() {
    _scopeTimer?.cancel();
    _scopeTimer = Timer.periodic(const Duration(milliseconds: 250), (_) {
      final vs = _vs;
      if (vs == null || !_gradeMode) return _stopScopes();
      final px = vs.previewScope(_scopeKind, _scopeW, _scopeH);
      if (px == null) return;
      ui.decodeImageFromPixels(px, _scopeW, _scopeH, ui.PixelFormat.rgba8888, (img) {
        if (!mounted || !_gradeMode) return img.dispose();
        setState(() {
          _scope?.dispose();
          _scope = img;
        });
      });
    });
  }

  void _stopScopes() {
    _scopeTimer?.cancel();
    _scopeTimer = null;
    _scope?.dispose();
    _scope = null;
  }

  Widget _gradePanel(VStudio vs, ClipView clip) => GradePanel(
        key: _gradeKey,
        vs: vs,
        clipId: clip.id,
        params: clip.grade ?? const <String, dynamic>{},
        onLive: (p) {
          try {
            // null = compare with the original: preview the clip without its grade.
            vs.setEffectPreview(clip.id, 'builtin.color_grade', p);
          } on VStudioException catch (e) {
            setState(() => _status = e.message);
          }
        },
        onCommit: (p) {
          _edit((vs) => vs.setClipGrade([clip.id], p.isEmpty ? null : p));
          _vs?.seek(_playhead);
        },
        onApplyAll: (p) {
          final ids = [
            for (final t in _project!.tracks)
              if (!t.audio && !t.locked)
                for (final c in t.clips)
                  if (!c.generated) c.id
          ];
          _edit((vs) {
            vs.setClipGrade(ids, p);
            _status = 'Đã áp dụng chỉnh màu cho ${ids.length} clip';
          });
          _vs?.seek(_playhead);
        },
        onPickMode: (on) => setState(() {
          _picking = on;
          _status = on ? 'Chạm vào màu cần chỉnh trên khung hình' : null;
        }),
        onDone: () {
          vs.setEffectPreview('', '', null);
          _stopScopes();
          setState(() {
            _gradeMode = false;
            _picking = false;
            _status = null;
          });
        },
      );

  Widget _coverPanel(VStudio vs, ClipView clip) {
    final ids = _coverIds();
    final active = ids.contains(_activeCover) ? _activeCover : null;
    CoverState? st;
    if (active != null) {
      try {
        st = vs.coverStateAt(clip.id, active, _playhead);
      } on VStudioException {
        st = null;
      }
    }
    return CoverPanel(
      vs: vs,
      covers: ids,
      active: active,
      state: st,
      tracking: _tracking,
      trackProgress: _trackProgress,
      onSelect: (id) => setState(() => _activeCover = id),
      onAdd: _addCover,
      onPatch: (patch) => _coverEdit((vs, clip, cover) => vs.updateCover(clip, cover, patch)),
      onAmount: (v) => _coverEdit((vs, clip, cover) => vs.setCoverAt(clip, cover, _playhead, {'amount': v})),
      onFollow: (on) => _coverEdit((vs, clip, cover) {
        vs.setCoverFollowing(clip, cover, _playhead, on);
        _status = on
            ? 'Vùng che theo keyframe: sang thời điểm khác rồi kéo vùng, nó sẽ di chuyển mượt giữa các điểm.'
            : 'Vùng che đứng yên';
      }),
      onTrack: _toggleTrack,
      onRemove: () => _coverEdit((vs, clip, cover) {
        vs.removeCover(clip, cover);
        final rest = ids.where((i) => i != cover);
        _activeCover = rest.isEmpty ? null : rest.first;
      }),
      onDone: () => setState(() {
        _coverMode = false;
        _status = null;
      }),
    );
  }

  /// Picks a .cube LUT, keeps a copy next to the media (so the project and its
  /// package stay self-contained) and applies it. Returns the stored path.
  Future<String?> _pickLut(String clipId) async {
    final store = _store;
    if (store == null) return null;
    final files = await FilePicker.pickFiles(type: FileType.any);
    final path = files.map((f) => f.path).whereType<String>().firstOrNull;
    if (path == null || !mounted) return null;
    if (!path.toLowerCase().endsWith('.cube')) {
      setState(() => _status = 'Chỉ dùng được LUT 3D dạng .cube');
      return null;
    }
    final kept = await store.adoptMedia(path);
    String? applied;
    _edit((vs) {
      vs.setClipLut([clipId], kept);
      applied = kept;
    });
    _vs?.seek(_playhead);
    return applied;
  }

  Future<void> _looks() async {
    final vs = _vs, clip = _selectedVideoClip;
    if (vs == null || clip == null) return;
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.black26, // keep the preview visible while trying looks
      builder: (context) => LooksSheet(
        vs: vs,
        clip: clip,
        onApply: (look, strength) {
          _edit((vs) => vs.setClipLook([clip.id], look, strength: strength));
          _vs?.seek(_playhead);
        },
        onInput: (input) {
          _edit((vs) => vs.setClipColorSpace([clip.id], input));
          _vs?.seek(_playhead);
        },
        onPickLut: () => _pickLut(clip.id),
        onClearLut: () {
          _edit((vs) => vs.setClipLut([clip.id], ''));
          _vs?.seek(_playhead);
        },
        myPresets: MyStylePresets(
          vs: vs,
          clipId: clip.id,
          allClips: () => [
            for (final t in _project!.tracks)
              if (!t.audio && !t.locked) ...t.clips.where((c) => c.kind.isEmpty).map((c) => c.id)
          ],
          onEdit: (f) {
            _edit(f);
            _vs?.seek(_playhead);
          },
        ),
        onApplyAll: (look, strength) {
          final ids = [
            for (final t in _project!.tracks)
              if (!t.audio && !t.locked) ...t.clips.map((c) => c.id)
          ];
          _edit((vs) {
            vs.setClipLook(ids, look, strength: strength);
            _status = 'Đã áp dụng bộ lọc cho ${ids.length} clip';
          });
          _vs?.seek(_playhead);
        },
      ),
    );
  }

  /// Zoom, rotate and reflection effects for the selected media clip.
  Future<void> _audio() async {
    final vs = _vs;
    if (vs == null) return;
    await showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.black26,
      builder: (context) => FractionallySizedBox(
        heightFactor: 0.7,
        child: AudioSheet(vs: vs, clipId: _selectedClip?.generated == false ? _selectedClip!.id : null, onEdit: _edit),
      ),
    );
  }

  Future<void> _speed() async {
    final vs = _vs, clip = _selectedMediaClip;
    if (vs == null || clip == null) return;
    await showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.black26,
      builder: (context) => FractionallySizedBox(
        heightFactor: 0.6,
        child: SpeedSheet(vs: vs, clipId: clip.id, onEdit: _edit),
      ),
    );
  }

  Future<void> _clipTools() async {
    final vs = _vs, clip = _selectedMediaClip;
    if (vs == null || clip == null) return;
    await showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.black26,
      builder: (context) => FractionallySizedBox(
        heightFactor: 0.7,
        child: ClipToolsSheet(
          vs: vs,
          clipId: clip.id,
          playhead: _playhead,
          onEdit: _edit,
          onFrozen: (id) => setState(() => _selected = id),
        ),
      ),
    );
  }

  // ---- voice-over (FR-020): the microphone records while the timeline plays ----
  AudioRecorder? _recorder;
  double _recordStart = 0;
  bool get _recording => _recorder != null;

  Future<void> _voiceOver() async {
    final vs = _vs, store = _store;
    if (vs == null || store == null) return;
    final rec = _recorder;
    if (rec != null) {
      _recorder = null;
      final path = await rec.stop();
      await rec.dispose();
      vs.pause();
      if (path == null) {
        setState(() => _status = 'Ghi âm không thành công');
        return;
      }
      _edit((vs) {
        final asset = vs.importMedia(path);
        vs.placeAudio(asset, _recordStart);
      });
      setState(() => _status = 'Đã thêm bản ghi âm vào track "Thu âm"');
      return;
    }
    final r = AudioRecorder();
    if (!await r.hasPermission()) {
      await r.dispose();
      setState(() => _status = 'Cần quyền micro để thu âm');
      return;
    }
    final dir = Directory('${store.root.path}/recordings');
    await dir.create(recursive: true);
    final file = '${dir.path}/thu-am-${DateTime.now().millisecondsSinceEpoch}.m4a';
    await r.start(const RecordConfig(encoder: AudioEncoder.aacLc, sampleRate: 48000, numChannels: 1), path: file);
    _recordStart = _playhead;
    vs.play(_playhead);
    setState(() {
      _recorder = r;
      _status = 'Đang thu âm… chạm "Dừng" để kết thúc';
    });
  }

  Future<void> _videoFx() async {
    final vs = _vs, clip = _selectedMediaClip;
    if (vs == null || clip == null) return;
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.black26, // keep the preview visible while trying effects
      builder: (context) => VideoFxSheet(
        vs: vs,
        clip: clip,
        onSet: (group, fx, amount, speed, line) {
          _edit((vs) => vs.setClipVideoFx([clip.id], group, fx, amount: amount, speed: speed, line: line));
          _vs?.seek(_playhead);
        },
      ),
    );
  }

  String? _exportPath; // file being written; moved to Photos when done

  String _exportFormat = 'h264';

  Future<void> _export() async {
    final vs = _vs;
    if (vs == null) return;
    if (_exportProgress != null) {
      vs.cancelExport();
      return;
    }
    final formats = vs.exportFormats();
    var format = _exportFormat;
    // (built-in quality, format) or (null, user preset name).
    final chosen = await showModalBottomSheet<(ExportPresetView?, String)>(
      context: context,
      backgroundColor: VsColors.panel,
      isScrollControlled: true,
      builder: (context) => StatefulBuilder(builder: (context, setSheet) {
        final presets = vs.exportPresets(format);
        final mine = vs.userPresets('export');
        final note = formats.where((f) => f.id == format).firstOrNull?.description ?? '';
        return SafeArea(
          child: ConstrainedBox(
            constraints: BoxConstraints(maxHeight: MediaQuery.sizeOf(context).height * 0.8),
            child: ListView(shrinkWrap: true, children: [
              const Padding(
                padding: EdgeInsets.fromLTRB(16, 14, 16, 6),
                child: Text('Xuất video', style: TextStyle(color: VsColors.fg, fontSize: 16)),
              ),
              // H.264 / H.265 / H.265 10-bit: 10-bit keeps graded Log footage free of banding.
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                child: Wrap(spacing: 8, children: [
                  for (final f in formats)
                    ChoiceChip(
                      key: ValueKey('format-${f.id}'),
                      label: Text(f.name),
                      selected: f.id == format,
                      onSelected: (_) => setSheet(() => format = f.id),
                    ),
                ]),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 4, 16, 4),
                child: Text(note, style: const TextStyle(color: VsColors.fg2, fontSize: 12)),
              ),
              for (final p in presets)
                ListTile(
                  title: Text(p.name),
                  subtitle: Text(
                      '${p.width} × ${p.height} · ${p.fps.round()} khung/giây · khoảng ${_size(p.estimatedBytes)}\n'
                      '${p.description}',
                      style: const TextStyle(fontSize: 12)),
                  isThreeLine: true,
                  onTap: () => Navigator.pop(context, (p, format)),
                ),
              // The user's own presets (FR-018): their own format, frame rate and bitrate.
              if (mine.isNotEmpty)
                const Padding(
                  padding: EdgeInsets.fromLTRB(16, 10, 16, 0),
                  child: Text('Preset của tôi', style: TextStyle(color: VsColors.fg2, fontSize: 13)),
                ),
              for (final m in mine)
                Builder(builder: (context) {
                  final info = vs.exportPresetInfo(name: m.name);
                  return ListTile(
                    key: ValueKey('user-export-${m.name}'),
                    leading: const Icon(Icons.star_outline),
                    title: Text(m.name),
                    subtitle: Text('${info.width} × ${info.height} · khoảng ${_size(info.estimatedBytes)}\n${m.summary}',
                        style: const TextStyle(fontSize: 12)),
                    isThreeLine: true,
                    onTap: () => Navigator.pop(context, (null, m.name)),
                    trailing: IconButton(
                      tooltip: 'Xoá preset',
                      icon: const Icon(Icons.delete_outline, size: 20),
                      onPressed: () => setSheet(() => vs.removePreset('export', m.name)),
                    ),
                  );
                }),
              ListTile(
                key: const ValueKey('save-export-preset'),
                leading: const Icon(Icons.add),
                title: const Text('Lưu thành preset của tôi…'),
                subtitle: const Text('Chất lượng, định dạng đang chọn, khung/giây, bitrate, âm thanh',
                    style: TextStyle(fontSize: 12)),
                onTap: () async {
                  await askExportPreset(context, vs, format, presets);
                  setSheet(() {});
                },
              ),
            ]),
          ),
        );
      }),
    );
    if (chosen == null || !mounted) return;
    final (preset, fmtOrName) = chosen;
    if (preset != null) _exportFormat = fmtOrName;
    // Written to the app's temporary folder, then handed to the photo library.
    final height = preset?.height ?? vs.exportPresetInfo(name: fmtOrName).height;
    final out = '${Directory.systemTemp.path}/${_safeName(_project?.name ?? 'video')} ${height}p.mp4';
    _exportPath = out;
    _edit((vs) {
      if (preset != null) {
        vs.startExportPresetFormat(out, preset.id, fmtOrName);
      } else {
        vs.startExportUserPreset(out, fmtOrName);
      }
      _exportProgress = 0;
      _status = 'Đang xuất ${preset?.name ?? fmtOrName}…';
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

  /// The preview, pinchable when a video clip is selected under the playhead and nothing plays.
  Widget _zoomable(Widget preview, ClipKeyState? keys) => PreviewZoom(
        enabled: keys != null && _selectedMediaClip != null && !_playing,
        canvasSize: Size(_project!.canvasWidth.toDouble(), _project!.canvasHeight.toDouble()),
        onCommit: _commitZoom,
        onReset: _resetZoom,
        child: preview,
      );

  // ---- the playhead is the anchor: tools act on the selected clip, else on the clip under it ----

  /// The clip under the playhead: on the main video lane first, then the other lanes top-down.
  ClipView? _clipUnderPlayhead() {
    final tracks = _project?.tracks ?? const <TrackView>[];
    bool inside(ClipView c) => _playhead >= c.start - 1e-6 && _playhead < c.end - 1e-6;
    for (final t in tracks.reversed.where((t) => t.takesMedia)) {
      for (final c in t.clips) {
        if (inside(c)) return c;
      }
    }
    for (final t in tracks) {
      for (final c in t.clips) {
        if (inside(c)) return c;
      }
    }
    return null;
  }

  /// Selects the clip under the playhead when nothing is selected. False when there is none.
  bool _target({bool media = false}) {
    if (_selected != null && (!media || _selectedMediaClip != null)) return true;
    ClipView? c = _clipUnderPlayhead();
    if (media && c != null && c.generated) {
      c = null;
      for (final t in (_project?.tracks ?? const <TrackView>[]).reversed.where((t) => t.takesMedia)) {
        for (final x in t.clips) {
          if (_playhead >= x.start && _playhead < x.end) c = x;
        }
      }
    }
    if (c == null) {
      setState(() => _status = media
          ? 'Vuốt timeline để đưa thanh playback vào một clip video hoặc ảnh'
          : 'Vuốt timeline để đưa thanh playback vào một clip');
      return false;
    }
    setState(() => _selected = c!.id);
    return true;
  }

  /// A tool that needs a clip: runs on the selected one, or the one under the playhead.
  VoidCallback _onClip(void Function() f, {bool media = false}) => () {
        if (_target(media: media)) f();
      };

  /// "Tách": cuts the selected clip (or the one under the playhead) at the playhead.
  void _split() {
    if (!_target()) return;
    final c = _selectedClip;
    if (c == null) return;
    final eps = 0.5 / (_project!.fps > 0 ? _project!.fps : 30);
    if (_playhead <= c.start + eps || _playhead >= c.end - eps) {
      setState(() => _status = 'Thanh playback đang ở mép clip: vuốt vào giữa clip để tách');
      return;
    }
    _edit((vs) {
      vs.splitClip(c.id, _playhead);
      _status = 'Đã tách tại ${_tc(_playhead)}';
    });
  }

  void _deleteSelected({bool ripple = false}) {
    if (!_target()) return;
    final id = _selected!;
    _edit((vs) {
      ripple ? vs.rippleDelete(id) : vs.removeClip(id);
      _selected = null;
    });
    _vs?.seek(_playhead);
  }

  Future<void> _orient() async {
    final vs = _vs, clip = _selectedMediaClip;
    if (vs == null || clip == null) return;
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: VsColors.panel,
      barrierColor: Colors.black26,
      builder: (context) => OrientSheet(
        vs: vs,
        clipIds: [clip.id],
        onEdit: (f) {
          _edit(f);
          _vs?.seek(_playhead);
        },
      ),
    );
  }

  /// Edits the selected text / sticker / overlay with its own sheet.
  void _editObject() {
    switch (_selectedObject?.kind) {
      case 'text':
        _text();
      case 'sticker':
        _stickers();
      case 'overlay':
        _overlays();
      case 'subtitle':
        _subtitles();
    }
  }

  void _startGrade() => setState(() {
        _startScopes();
        _gradeMode = true;
        _coverMode = false;
        _status = 'Kéo để xem ngay; nhấc tay là lưu (hoàn tác được)';
      });

  void _togglePlay() {
    final vs = _vs, project = _project;
    if (vs == null || project == null) return;
    if (vs.playing) {
      vs.pause();
    } else {
      vs.play(_playhead >= project.duration ? 0 : _playhead);
    }
  }

  /// "0:02.2" for the time row.
  static String _tc(double s) {
    final t = s < 0 ? 0.0 : s;
    final m = t ~/ 60, sec = t - m * 60;
    return '$m:${sec.toStringAsFixed(1).padLeft(4, '0')}';
  }

  /// "+" on the timeline: the media bin on a wide screen, the photo library on a phone.
  void _addButton(bool wide) {
    if (wide) {
      setState(() {
        _selected = null;
        _gradeMode = false;
        _coverMode = false;
        _stopScopes();
        _status = 'Chạm một mục trong "Media" để thêm vào cuối video';
      });
    } else {
      _import();
    }
  }

  // ---- "Đoạn áp dụng": start and end points set with the playhead ----

  void _rangeStart() {
    if (!_target()) return;
    final c = _selectedClip!;
    setState(() {
      _range = TimeRange(_playhead.clamp(c.start, c.end).toDouble(), null);
      _status = null;
    });
  }

  void _rangeEnd() {
    final r = _range, c = _selectedClip;
    if (r == null || c == null) return;
    var a = r.start, b = _playhead.clamp(c.start, c.end).toDouble();
    if (b < a) (a, b) = (b, a);
    if (b - a < 0.1) {
      setState(() => _status = 'Đoạn quá ngắn: vuốt timeline xa hơn rồi bấm "Điểm cuối"');
      return;
    }
    setState(() => _range = TimeRange(a, b));
  }

  /// Cuts the selected clip at both ends of the range; returns the middle piece (now selected).
  String? _isolateRange() {
    final r = _range, c = _selectedClip;
    if (r == null || !r.complete || c == null) return null;
    final eps = 1 / (_project!.fps > 0 ? _project!.fps : 30);
    var mid = c.id;
    _edit((vs) {
      if (r.start > c.start + eps) mid = vs.splitClip(c.id, r.start);
      if (r.end! < c.end - eps) vs.splitClip(mid, r.end!);
      _selected = mid;
    });
    return _selected == mid ? mid : null;
  }

  Future<void> _rangeAction(RangeAction action, double value) async {
    final r = _range, c = _selectedClip, vs = _vs;
    if (r == null || !r.complete || c == null || vs == null) return;
    final a = r.start, b = r.end!;
    switch (action) {
      case RangeAction.zoom:
      case RangeAction.volume:
        final group = action == RangeAction.zoom ? KeyGroup.picture : KeyGroup.audio;
        _edit((vs) {
          // A key at each end; the start keeps today's look, the end gets the new one.
          final st = vs.clipStateAt(c.id, a);
          if (!(group == KeyGroup.picture ? st.pictureHere : st.audioHere)) vs.addKeyframe(c.id, group, a);
          final en = vs.clipStateAt(c.id, b);
          if (!(group == KeyGroup.picture ? en.pictureHere : en.audioHere)) vs.addKeyframe(c.id, group, b);
          if (group == KeyGroup.picture) {
            vs.setPropertyAt(c.id, b, {'scale': (st.scale * value).clamp(0.05, 20).toDouble(), 'x': st.x * value, 'y': st.y * value});
            _status = value >= 1
                ? 'Đã đặt zoom dần ${value.toStringAsFixed(1)}× từ ${_tc(a)} tới ${_tc(b)}. Bấm ▶ để xem.'
                : 'Đã đặt thu nhỏ dần từ ${_tc(a)} tới ${_tc(b)}. Bấm ▶ để xem.';
          } else {
            vs.setPropertyAt(c.id, b, {'volume': value});
            _status = value == 0
                ? 'Âm thanh nhỏ dần tới tắt từ ${_tc(a)} tới ${_tc(b)}'
                : 'Âm thanh to dần ×${value.toStringAsFixed(0)} từ ${_tc(a)} tới ${_tc(b)}';
          }
        });
        _seek(a);
      case RangeAction.speed:
        final mid = _isolateRange();
        if (mid == null) return;
        _edit((vs) {
          vs.setSpeed([mid], value);
          _status = value < 1 ? 'Đoạn đã chọn chạy chậm $value×' : 'Đoạn đã chọn chạy nhanh ${value.toStringAsFixed(0)}×';
        });
        setState(() => _range = null);
        _seek(a);
      case RangeAction.effect:
      case RangeAction.filter:
      case RangeAction.color:
        if (_isolateRange() == null) return;
        setState(() {
          _range = null;
          _status = 'Đoạn đã chọn được tách riêng: hiệu ứng chỉ áp vào đoạn này';
        });
        _seek(a);
        if (action == RangeAction.effect) await _videoFx();
        if (action == RangeAction.filter) await _looks();
        if (action == RangeAction.color) _startGrade();
    }
  }

  // ---- layout (MOB-04 in the approved prototype) ----

  Widget _preview(ProjectView project, ClipKeyState? keys) {
    final aspect = project.canvasWidth > 0 && project.canvasHeight > 0 ? project.canvasWidth / project.canvasHeight : 9 / 16;
    return Stack(children: [
      Positioned.fill(
        child: Container(
          color: Colors.black,
          alignment: Alignment.center,
          padding: const EdgeInsets.all(8),
          child: _textureId != null && project.duration > 0
              ? AspectRatio(aspectRatio: aspect, child: _coverable(Texture(textureId: _textureId!), keys))
              : _frame == null
                  ? const Text('Bấm + để thêm video, ảnh', style: TextStyle(color: VsColors.fg3))
                  : AspectRatio(
                      aspectRatio: _frame!.width / _frame!.height,
                      child: _coverable(RawImage(image: _frame, fit: BoxFit.fill), keys)),
        ),
      ),
      if (_gradeMode && _scope != null)
        Positioned(
          left: 8,
          top: 8,
          child: GestureDetector(
            // Tap: next scope (waveform, parade, vectorscope, histogram).
            onTap: () => setState(() => _scopeKind = (_scopeKind + 1) % 4),
            child: Opacity(
              opacity: 0.9,
              child: RawImage(image: _scope, width: _scopeW.toDouble(), height: _scopeH.toDouble()),
            ),
          ),
        ),
    ]);
  }

  Widget _timeRow(ProjectView project) => Container(
        color: VsColors.bg,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        height: 48,
        child: Row(children: [
          Expanded(
            child: Text('${_tc(_playhead)} / ${_tc(project.duration)}',
                key: const Key('time-row'),
                style: const TextStyle(fontFeatures: [ui.FontFeature.tabularFigures()], color: VsColors.fg, fontSize: 13)),
          ),
          IconButton(
            key: const Key('play'),
            iconSize: 30,
            color: VsColors.fg,
            icon: Icon(_playing ? Icons.pause : Icons.play_arrow),
            onPressed: _togglePlay,
          ),
          Expanded(
            child: Align(
              alignment: Alignment.centerRight,
              child: IconButton(
                key: const Key('fullscreen'),
                tooltip: 'Toàn màn hình',
                color: VsColors.fg,
                icon: const Icon(Icons.fullscreen),
                onPressed: () => setState(() => _fullscreen = true),
              ),
            ),
          ),
        ]),
      );

  /// Colour, covers, or the keyframe guide of the selected clip (null: nothing to show).
  Widget? _clipPanel(ClipKeyState? keys) {
    if (_gradeMode && _selectedMediaClip != null) return _gradePanel(_vs!, _selectedMediaClip!);
    if (_coverMode && _selectedVideoClip != null) return _coverPanel(_vs!, _selectedVideoClip!);
    final c = _selectedClip;
    if (c == null || c.generated) return null;
    if (!_keyMode) {
      return RangeBar(
        range: _range,
        video: _selectedVideoClip != null,
        onStart: _rangeStart,
        onEnd: _rangeEnd,
        onClear: () => setState(() => _range = null),
        onAction: _rangeAction,
      );
    }
    return KeyframeBar(
      state: keys,
      video: _selectedVideoClip != null,
      hasKeys: c.animated,
      pictureKeys: c.pictureKeys.length,
      audioKeys: c.audioKeys.length,
      onToggle: _toggleKeyframe,
      onJump: _jumpKeyframe,
      onVolume: (v) => _edit((vs) => vs.setPropertyAt(c.id, _playhead, {'volume': v})),
    );
  }

  Widget _timeline(ProjectView project, {required bool wide}) {
    final h = TimelineView.heightFor(project);
    final lanes = TimelineView.laneTops(project).$1;
    final main = project.tracks.lastIndexWhere((t) => t.takesMedia);
    final addTop = main >= 0 ? lanes[main] + (TimelineView.laneHeight(project.tracks[main]) - 40) / 2 : TimelineView.rulerH + 10;
    return Container(
      color: VsColors.bg,
      child: SingleChildScrollView(
        child: SizedBox(
          height: h < 120 ? 120 : h,
          child: Stack(children: [
            Positioned.fill(
              child: TimelineView(
                project: project,
                playhead: _playhead,
                selectedClip: _selected,
                onSeek: _seek,
                onScrub: (on) {
                  if (on && (_vs?.playing ?? false)) _vs!.pause();
                },
                range: _range == null ? null : (_range!.start, _range!.end),
                onSelect: (id) => setState(() {
                  if (id != _selected) {
                    _activeCover = null;
                    _range = null;
                  }
                  _selected = id;
                  _selectedCut = null;
                  if (_selectedVideoClip == null) _coverMode = false;
                  if (_selectedMediaClip == null && _gradeMode) {
                    _gradeMode = false;
                    _stopScopes();
                  }
                  if (_coverMode) _activeCover = _coverIds().firstOrNull;
                }),
                onMove: (clip, track, start) => _edit((vs) => vs.moveClip(clip, track, start)),
                onTrackFlags: (t, {required enabled, required locked, required muted}) =>
                    _edit((vs) => vs.setTrackFlags(t.id, enabled: enabled, locked: locked, muted: muted)),
                onTrackMenu: _trackMenu,
                snapMove: (clip, start, tol) => _vs?.snapClipMove(clip, start, tol, playhead: _playhead),
                onTrim: (clip, {required start, required at}) => _edit((vs) => vs.trimClip(clip, start: start, at: at)),
                snapTime: (t, tol, clip) => _vs?.snapTime(t, tol, excludeClip: clip, playhead: _playhead),
                media: _media,
                cuts: _cuts,
                selectedCut: _selectedCut,
                onCut: _transitions,
              ),
            ),
            Positioned(
              right: 10,
              top: addTop,
              child: Material(
                color: VsColors.fg,
                borderRadius: BorderRadius.circular(10),
                elevation: 3,
                child: InkWell(
                  key: const Key('timeline-add'),
                  borderRadius: BorderRadius.circular(10),
                  onTap: () => _addButton(wide),
                  child: const SizedBox(width: 40, height: 40, child: Icon(Icons.add, color: Colors.black, size: 26)),
                ),
              ),
            ),
          ]),
        ),
      ),
    );
  }

  /// The tools under the timeline change with what is selected (as in the prototype).
  List<Widget> _tools() {
    final c = _selectedClip;
    Widget back() => _Tool(
        icon: Icons.chevron_left,
        label: '',
        accent: true,
        onTap: () => setState(() {
              _selected = null;
              _gradeMode = false;
              _coverMode = false;
              _selectedCut = null;
              _range = null;
              _stopScopes();
            }));
    final split = _Tool(icon: Icons.vertical_split_outlined, label: 'Tách', onTap: _split);
    final range = _Tool(
        icon: Icons.straighten,
        label: 'Chọn đoạn',
        onTap: () {
          if (!_target()) return;
          setState(() => _keyMode = false);
          _rangeStart();
        });
    final del = _Tool(icon: Icons.delete_outline, label: 'Xóa', danger: true, onTap: () => _deleteSelected());
    if (c == null) {
      return [
        split,
        range,
        _Tool(icon: Icons.music_note_outlined, label: 'Âm thanh', onTap: _audio),
        _Tool(
            icon: _recording ? Icons.stop_circle : Icons.mic_none, label: _recording ? 'Dừng' : 'Thu âm', onTap: _voiceOver),
        _Tool(icon: Icons.title, label: 'Chữ', onTap: _text),
        _Tool(icon: Icons.emoji_emotions_outlined, label: 'Sticker', onTap: _stickers),
        _Tool(icon: Icons.ac_unit, label: 'Lớp phủ', onTap: _overlays),
        _Tool(icon: Icons.closed_caption_outlined, label: 'Phụ đề', onTap: _subtitles),
        _Tool(icon: Icons.auto_awesome, label: 'Hiệu ứng', onTap: _onClip(_videoFx, media: true)),
        _Tool(icon: Icons.filter_vintage_outlined, label: 'Bộ lọc', onTap: _onClip(_looks, media: true)),
        _Tool(icon: Icons.tune, label: 'Điều chỉnh', onTap: _onClip(_startGrade, media: true)),
        _Tool(icon: Icons.rotate_90_degrees_cw_outlined, label: 'Xoay / lật', onTap: _onClip(_orient, media: true)),
        _Tool(icon: Icons.speed, label: 'Tốc độ', onTap: _onClip(_speed, media: true)),
      ];
    }
    if (c.generated) {
      return [
        back(),
        _Tool(icon: Icons.edit_outlined, label: 'Sửa', onTap: _editObject),
        split,
        _Tool(icon: Icons.copy_outlined, label: 'Thêm ${c.kind == 'text' ? 'chữ' : 'mới'}', onTap: () {
          setState(() => _selected = null);
          _editObject();
        }),
        del,
      ];
    }
    if (_selectedTrack?.audio ?? false) {
      return [
        back(),
        _Tool(icon: Icons.volume_up_outlined, label: 'Âm lượng', onTap: _audio),
        split,
        _Tool(icon: Icons.speed, label: 'Tốc độ', onTap: _speed),
        _Tool(
            icon: _keyMode ? Icons.straighten : Icons.diamond_outlined,
            label: _keyMode ? 'Đoạn áp dụng' : 'Keyframe',
            onTap: () => setState(() => _keyMode = !_keyMode)),
        del,
        _Tool(icon: Icons.format_indent_decrease, label: 'Xóa dồn', onTap: () => _deleteSelected(ripple: true)),
      ];
    }
    return [
      back(),
      split,
      _Tool(icon: Icons.speed, label: 'Tốc độ', onTap: _speed),
      _Tool(icon: Icons.volume_up_outlined, label: 'Âm lượng', onTap: _audio),
      _Tool(icon: Icons.view_agenda_outlined, label: 'Chuyển cảnh', onTap: () => _transitions()),
      _Tool(icon: Icons.filter_vintage_outlined, label: 'Bộ lọc', onTap: _looks),
      _Tool(icon: Icons.tune, label: 'Điều chỉnh', onTap: _startGrade),
      _Tool(icon: Icons.auto_awesome, label: 'Hiệu ứng', onTap: _videoFx),
      _Tool(icon: Icons.rotate_90_degrees_cw_outlined, label: 'Xoay / lật', onTap: _orient),
      _Tool(
          icon: _keyMode ? Icons.straighten : Icons.diamond_outlined,
          label: _keyMode ? 'Đoạn áp dụng' : 'Keyframe',
          onTap: () => setState(() => _keyMode = !_keyMode)),
      _Tool(icon: Icons.blur_on, label: 'Che mờ', onTap: _enterCovers),
      _Tool(icon: Icons.vignette_outlined, label: 'Mặt nạ', onTap: _enterMasks),
      _Tool(icon: Icons.handyman_outlined, label: 'Công cụ', onTap: _clipTools),
      del,
      _Tool(icon: Icons.format_indent_decrease, label: 'Xóa dồn', onTap: () => _deleteSelected(ripple: true)),
    ];
  }

  TrackView? get _selectedTrack {
    for (final t in _project?.tracks ?? const <TrackView>[]) {
      if (t.clips.any((c) => c.id == _selected)) return t;
    }
    return null;
  }

  Widget _topBar(ProjectView project) => Container(
        color: VsColors.bg,
        height: 52,
        padding: const EdgeInsets.symmetric(horizontal: 4),
        child: Row(children: [
          IconButton(key: const Key('editor-close'), tooltip: 'Về trang chủ', icon: const Icon(Icons.close), onPressed: _goHome),
          IconButton(tooltip: 'Hoàn tác', icon: const Icon(Icons.undo), onPressed: () => _edit((vs) => vs.undo())),
          Expanded(
            child: Text(project.name,
                textAlign: TextAlign.center,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
          ),
          IconButton(tooltip: 'Làm lại', icon: const Icon(Icons.redo), onPressed: () => _edit((vs) => vs.redo())),
          Padding(
            padding: const EdgeInsets.only(left: 4, right: 8),
            child: FilledButton(
              onPressed: project.duration > 0 ? _export : null,
              child: Text(_exportProgress == null ? 'Xuất' : '${(_exportProgress! * 100).round()}%  ✕'),
            ),
          ),
        ]),
      );

  List<Widget> _notes(ProjectView project) => [
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
            padding: const EdgeInsets.fromLTRB(12, 3, 12, 3),
            child: Text(_status!, style: const TextStyle(color: VsColors.fg2, fontSize: 12), maxLines: 2),
          ),
      ];

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Scaffold(body: Center(child: Padding(padding: const EdgeInsets.all(24), child: Text(_error!))));
    }
    if (_home) {
      return HomeView(
        projects: _projects,
        onNew: (aspect) => _switchProject(aspect: aspect),
        onOpen: (p) => _switchProject(path: p.path),
        onDelete: _deleteProject,
        onImportPackage: _importPackage,
      );
    }
    final project = _project;
    if (project == null) return const Scaffold(body: Center(child: CircularProgressIndicator()));
    final keys = _keyState();
    if (_fullscreen) {
      return Scaffold(
        backgroundColor: Colors.black,
        body: GestureDetector(
          onTap: () => setState(() => _fullscreen = false),
          child: SafeArea(child: _preview(project, keys)),
        ),
      );
    }
    return Scaffold(
      body: SafeArea(
        child: LayoutBuilder(builder: (context, box) {
          final mq = MediaQuery.of(context);
          final mode = workspaceLayoutFor(box.biggest, mq.displayFeatures, mq.padding.top);
          final wide = mode.kind == WorkspaceKind.sideBySide;
          final panel = _clipPanel(keys);
          final timeline = _timeline(project, wide: wide);
          final maxTimeline = box.maxHeight * (wide ? 0.34 : 0.3);
          final tools = Container(
            color: VsColors.panel,
            height: 68,
            child: ListView(scrollDirection: Axis.horizontal, children: _tools()),
          );
          Widget body;
          if (wide) {
            // Unfolded (Galaxy Z Fold, foldable iPhone) or a tablet: preview left, the panel
            // right (media bin, colour, keyframes...), the timeline across the whole width.
            final sideW = (box.maxWidth * 0.42).clamp(300.0, 520.0);
            body = Column(children: [
              _topBar(project),
              Expanded(
                child: Row(children: [
                  Expanded(child: Column(children: [Expanded(child: _preview(project, keys)), _timeRow(project)])),
                  if (mode.gap > 0) SizedBox(width: mode.gap),
                  Container(
                    width: sideW,
                    decoration: const BoxDecoration(
                        color: VsColors.panel, border: Border(left: BorderSide(color: VsColors.line))),
                    child: panel != null
                        ? SingleChildScrollView(child: panel)
                        : MediaBin(
                            project: project,
                            media: _media,
                            onAdd: (assetId) => _edit((vs) {
                              final track = _mediaLane(vs);
                              _selected = vs.appendClip(track, assetId);
                            }),
                            onImport: _import,
                          ),
                  ),
                ]),
              ),
              const Divider(height: 1, color: VsColors.line),
              ConstrainedBox(constraints: BoxConstraints(maxHeight: maxTimeline), child: timeline),
              ..._notes(project),
              tools,
            ]);
          } else {
            body = Column(children: [
              _topBar(project),
              Expanded(child: _preview(project, keys)),
              _timeRow(project),
              if (panel != null) ConstrainedBox(constraints: BoxConstraints(maxHeight: box.maxHeight * 0.3), child: SingleChildScrollView(child: panel)),
              ConstrainedBox(constraints: BoxConstraints(maxHeight: maxTimeline), child: timeline),
              ..._notes(project),
              tools,
            ]);
          }
          return body;
        }),
      ),
    );
  }
}

class _Tool extends StatelessWidget {
  const _Tool({required this.icon, required this.label, required this.onTap, this.accent = false, this.danger = false});
  final IconData icon;
  final String label;
  final VoidCallback? onTap;
  final bool accent, danger;

  @override
  Widget build(BuildContext context) {
    final color = onTap == null
        ? VsColors.fg3
        : accent
            ? VsColors.accent
            : danger
                ? VsColors.bad
                : VsColors.fg;
    return InkWell(
      onTap: onTap,
      child: SizedBox(
        width: label.isEmpty ? 48 : 70,
        child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
          Icon(icon, color: color, size: 24),
          if (label.isNotEmpty) ...[
            const SizedBox(height: 4),
            Text(label,
                textAlign: TextAlign.center,
                maxLines: 2,
                style: TextStyle(fontSize: 11, height: 1.1, color: onTap == null ? VsColors.fg3 : VsColors.fg2)),
          ],
        ]),
      ),
    );
  }
}
