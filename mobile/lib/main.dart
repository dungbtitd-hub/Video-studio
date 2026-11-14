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
import 'ui/media_cache.dart';
import 'ui/recent_strip.dart';
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
  // Unfinished projects shown at the bottom when the app opens, until work starts.
  List<ProjectCard> _recent = const [];
  bool _recentShown = false;

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
      FavoritesStore.init(store.root.path);
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
      _loadRecent();
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
    final store = _store;
    if (store != null) vs.initPresets('${store.root.path}/presets.json'); // shared by every project
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

  /// Lists the other unfinished projects (small card files only, no media) and slides the strip in.
  void _loadRecent() {
    final store = _store;
    if (store == null || !mounted) return;
    try {
      final cards = VStudio.recentProjects(store.projectsDir.path, exclude: _packagePath);
      setState(() {
        _recent = cards;
        _recentShown = cards.isNotEmpty;
      });
    } catch (e) {
      debugPrint('vstudio: recent projects unavailable: $e');
    }
  }

  void _hideRecent() {
    if (_recentShown) setState(() => _recentShown = false);
  }

  /// Saves (if needed), releases the current session, then opens [path] or a new project.
  Future<void> _switchProject({String? path, String? newName}) async {
    final store = _store, old = _vs;
    if (store == null || old == null || path == _packagePath) return;
    _hideRecent();
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
        _recentShown = false; // work started: the "continue" strip slides away
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

  static const _importPackageChoice = '#package';
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
            ListTile(
                leading: const Icon(Icons.move_to_inbox_outlined),
                title: const Text('Nhập gói dự án (.vstudiopkg)'),
                subtitle: const Text('Dự án đóng gói từ máy tính, kèm media', style: TextStyle(fontSize: 11)),
                onTap: () => Navigator.pop(context, _importPackageChoice)),
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
    if (choice == _importPackageChoice) return _importPackage();
    if (choice.isEmpty) {
      await _switchProject(newName: 'Dự án ${projects.length + 1}');
    } else {
      await _switchProject(path: choice);
    }
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

  static String _ago(DateTime t) {
    final d = DateTime.now().difference(t);
    if (d.inMinutes < 1) return 'vừa xong';
    if (d.inHours < 1) return '${d.inMinutes} phút trước';
    if (d.inDays < 1) return '${d.inHours} giờ trước';
    return '${t.day}/${t.month}/${t.year}';
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

  /// Phone: preview on top, controls and timeline below. Tablet / unfolded foldable
  /// (Galaxy Z Fold, foldable iPhone) held wide: preview on the left, controls on the right,
  /// split at the hinge when there is one so nothing sits across the fold. Half-folded flat
  /// on a table (horizontal hinge): preview above the fold, controls below it.
  Widget _adaptiveLayout(BuildContext context, {required Widget preview, required List<Widget> controls}) {
    return LayoutBuilder(builder: (context, box) {
      final mq = MediaQuery.of(context);
      final mode = workspaceLayoutFor(box.biggest, mq.displayFeatures, mq.padding.top + kToolbarHeight);
      final scrollControls = SingleChildScrollView(
        child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: controls),
      );
      switch (mode.kind) {
        case WorkspaceKind.phone:
          return Column(children: [Expanded(child: preview), ...controls]);
        case WorkspaceKind.sideBySide:
          return Row(children: [
            SizedBox(width: mode.split, child: preview),
            SizedBox(width: mode.gap),
            Expanded(child: Container(color: VsColors.panel, child: scrollControls)),
          ]);
        case WorkspaceKind.tabletop:
          return Column(children: [
            SizedBox(height: mode.split, child: preview),
            SizedBox(height: mode.gap),
            Expanded(child: scrollControls),
          ]);
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Scaffold(body: Center(child: Padding(padding: const EdgeInsets.all(24), child: Text(_error!))));
    }
    final project = _project;
    if (project == null) return const Scaffold(body: Center(child: CircularProgressIndicator()));
    final keys = _keyState();
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
      body: Stack(children: [
        _adaptiveLayout(
          context,
          preview: Stack(children: [
                Positioned.fill(
                  child: Container(
                    color: Colors.black,
                    alignment: Alignment.center,
                    child: _textureId != null && (_project?.duration ?? 0) > 0
                        ? AspectRatio(aspectRatio: 720 / 1280, child: _coverable(Texture(textureId: _textureId!), keys))
                        : _frame == null
                            ? const Text('Nhập video để bắt đầu', style: TextStyle(color: VsColors.fg3))
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
              ]),
          controls: [
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
            if (_gradeMode && _selectedMediaClip != null)
              _gradePanel(_vs!, _selectedMediaClip!)
            else if (_coverMode && _selectedVideoClip != null)
              _coverPanel(_vs!, _selectedVideoClip!)
            else if (_selectedClip != null)
              KeyframeBar(
                state: keys,
                video: _selectedVideoClip != null,
                hasKeys: _selectedClip!.animated,
                onToggle: _toggleKeyframe,
                onJump: _jumpKeyframe,
                onVolume: (v) {
                  _edit((vs) => vs.setPropertyAt(_selectedClip!.id, _playhead, {'volume': v}));
                },
              ),
            SizedBox(
              height: 22 + 56.0 * (project.tracks.isEmpty ? 1 : project.tracks.length) + 8,
              child: TimelineView(
                project: project,
                playhead: _playhead,
                selectedClip: _selected,
                onSeek: _seek,
                onSelect: (id) => setState(() {
                  if (id != _selected) _activeCover = null;
                  _selected = id;
                  if (_selectedVideoClip == null) _coverMode = false;
                  if (_selectedMediaClip == null) _gradeMode = false;
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
            for (final a in project.assets.where((a) => _media.missing.contains(a.id)))
              Material(
                color: const Color(0xFF4A1C1C),
                child: ListTile(
                  dense: true,
                  leading: const Icon(Icons.link_off, color: Color(0xFFFF8A80)),
                  title:
                      Text('Không tìm thấy file: ${a.name}', style: const TextStyle(color: VsColors.fg, fontSize: 13)),
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
        Positioned(
          left: 0,
          right: 0,
          bottom: 0,
          child: RecentStrip(
            cards: _recent,
            shown: _recentShown,
            onOpen: (c) => _switchProject(path: c.path),
            onClose: _hideRecent,
            onAll: _projectsMenu,
          ),
        ),
      ]),
      bottomNavigationBar: BottomAppBar(
        color: VsColors.panel,
        // Scrolls sideways when the tools do not fit a narrow phone.
        child: SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          child: Row(children: [
            _Tool(icon: Icons.add_photo_alternate_outlined, label: 'Nhập', onTap: _import),
            _Tool(icon: Icons.content_cut, label: 'Tách', onTap: () => _edit((vs) => vs.splitAt(_playhead))),
            _Tool(icon: Icons.title, label: 'Chữ', onTap: _vs == null ? null : _text),
            _Tool(icon: Icons.emoji_emotions_outlined, label: 'Sticker', onTap: _vs == null ? null : _stickers),
            _Tool(icon: Icons.ac_unit, label: 'Lớp phủ', onTap: _vs == null ? null : _overlays),
            _Tool(icon: Icons.closed_caption_outlined, label: 'Phụ đề', onTap: _vs == null ? null : _subtitles),
            _Tool(icon: Icons.auto_awesome, label: 'Lọc màu', onTap: _selectedMediaClip == null ? null : _looks),
            _Tool(
                icon: Icons.palette_outlined,
                label: 'Chỉnh màu',
                onTap: _selectedMediaClip == null
                    ? null
                    : () => setState(() {
                          _startScopes();
                          _gradeMode = true;
                          _coverMode = false;
                          _status = 'Kéo để xem ngay; nhấc tay là lưu (hoàn tác được)';
                        })),
            _Tool(icon: Icons.zoom_in_map, label: 'Hiệu ứng', onTap: _selectedMediaClip == null ? null : _videoFx),
            _Tool(icon: Icons.graphic_eq, label: 'Âm thanh', onTap: _vs == null ? null : _audio),
            _Tool(
                icon: _recording ? Icons.stop_circle : Icons.mic_none,
                label: _recording ? 'Dừng' : 'Thu âm',
                onTap: _vs == null ? null : _voiceOver),
            _Tool(icon: Icons.speed, label: 'Tốc độ', onTap: _selectedMediaClip == null ? null : _speed),
            _Tool(icon: Icons.handyman_outlined, label: 'Công cụ', onTap: _selectedMediaClip == null ? null : _clipTools),
            _Tool(icon: Icons.blur_on, label: 'Che mờ', onTap: _selectedMediaClip == null ? null : _enterCovers),
            _Tool(icon: Icons.vignette_outlined, label: 'Mặt nạ', onTap: _selectedMediaClip == null ? null : _enterMasks),
            _Tool(icon: Icons.animation, label: 'Chuyển cảnh', onTap: _selectedVideoClip == null ? null : _transitions),
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
