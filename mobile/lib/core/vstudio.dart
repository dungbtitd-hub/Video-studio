// Dart facade over the C API. Every call returns quickly: edits only touch
// the timeline model inside the core; decoding runs on core threads.
import 'dart:convert';
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'project_model.dart';
import 'vstudio_bindings.dart';

class VStudioException implements Exception {
  VStudioException(this.message);
  final String message;
  @override
  String toString() => message;
}

class PreviewFrame {
  PreviewFrame(this.width, this.height, this.pixels, this.ptsSeconds);
  final int width;
  final int height;
  final Uint8List pixels; // RGBA8
  final double ptsSeconds;
}

/// `count` frames of `width` x `height` RGBA stacked top to bottom (one image of width x height*count).
class ThumbStrip {
  ThumbStrip(this.width, this.height, this.count, this.rgba);
  final int width;
  final int height;
  final int count;
  final Uint8List rgba;
}

/// Peak envelope of the mono mix: (min, max) per bucket in -127..127.
class WaveformData {
  WaveformData(this.bucketsPerSecond, this.peaks) : loudest = peaks.fold(0, (m, v) => v.abs() > m ? v.abs() : m);
  final int bucketsPerSecond;
  final Int8List peaks;
  final int loudest;
  int get buckets => peaks.length ~/ 2;
}

enum ExportState { idle, running, done, failed, cancelled }

class LookInfo {
  const LookInfo(this.id, this.name, this.category);
  final String id, name, category;
}

/// One colour band of the HSL mixer (grade.hpp): id stored in the project, UI name, centre hue.
class HslBandInfo {
  const HslBandInfo(this.id, this.name, this.hue);
  final String id, name;
  final double hue;
}

class LogFormatInfo {
  const LogFormatInfo(this.id, this.name, this.vendor);
  final String id, name, vendor;
}

class ExportPresetView {
  ExportPresetView.fromJson(Map<String, dynamic> j)
      : id = j['id'] as String,
        name = j['name'] as String,
        description = j['description'] as String,
        width = j['width'] as int,
        height = j['height'] as int,
        fps = (j['fps'] as num).toDouble(),
        videoBitrate = j['video_bitrate'] as int,
        estimatedBytes = j['estimated_bytes'] as int;
  final String id, name, description;
  final int width, height, videoBitrate, estimatedBytes;
  final double fps;
}

class ExportFormatView {
  ExportFormatView.fromJson(Map<String, dynamic> j)
      : id = j['id'] as String,
        name = j['name'] as String,
        description = j['description'] as String;
  final String id, name, description;
}

class CoverStyleInfo {
  const CoverStyleInfo(this.id, this.name, this.category, this.animated);
  final String id, name, category;
  final bool animated;
}

/// A cover's values at one moment.
class CoverState {
  CoverState(this.json);
  final Map<String, dynamic> json;
  double _d(String k, double fallback) => ((json[k] ?? fallback) as num).toDouble();
  double get x => _d('x', 0.35);
  double get y => _d('y', 0.35);
  double get w => _d('w', 0.3);
  double get h => _d('h', 0.3);
  double get amount => _d('amount', 0.5);
  double get feather => _d('feather', 0.15);
  double get rotation => _d('rotation', 0);
  String get style => (json['style'] ?? 'blur') as String;
  String get shape => (json['shape'] ?? 'rect') as String;
  bool get invert => json['invert'] == true;
  bool get following => json['following'] == true;
  /// Masks change the clip's transparency instead of drawing over it.
  bool get isMask => style.startsWith('mask_');
}

enum TrackState { idle, running, done, lost, cancelled, failed }

class SpeedPreset {
  const SpeedPreset(this.id, this.name, this.points);
  final String id, name;
  final List<(double, double)> points;
}

class ClipSpeed {
  const ClipSpeed(
      {required this.can,
      required this.why,
      required this.speed,
      required this.points,
      required this.preset,
      required this.seconds});
  final bool can;
  final String why;
  final double speed;
  final List<(double, double)> points;
  final String preset;
  final double seconds;
}

class TransitionInfo {
  const TransitionInfo(this.id, this.name, this.category);
  final String id, name, category;
}

/// A video effect that moves the picture: zoom, rotate or reflection (one of each group per clip).
class VideoFxInfo {
  const VideoFxInfo(this.id, this.name, this.group, this.groupName, {this.animated = true, this.usesLine = false});
  final String id, name, group, groupName;
  final bool animated, usesLine;
}

/// Keyframe groups: picture (zoom, position, opacity) and audio (volume) are independent.
enum KeyGroup { picture, audio }

class ClipKeyState {
  ClipKeyState.fromJson(Map<String, dynamic> j)
      : inside = j['inside'] == true,
        scale = (j['scale'] as num).toDouble(),
        x = (j['x'] as num).toDouble(),
        y = (j['y'] as num).toDouble(),
        opacity = (j['opacity'] as num).toDouble(),
        volume = (j['volume'] as num).toDouble(),
        pictureHere = j['picture_here'] == true,
        audioHere = j['audio_here'] == true,
        pictureAnimated = j['picture_animated'] == true,
        audioAnimated = j['audio_animated'] == true;
  final bool inside; // the time is inside the clip
  final double scale, x, y, opacity, volume;
  final bool pictureHere, audioHere; // a key sits at that time
  final bool pictureAnimated, audioAnimated;
}

class FontInfo {
  const FontInfo(this.id, this.name, this.style);
  final String id, name, style;
}

/// A text/sticker in or out animation; [perLetter] ones move each letter on its own.
class TextAnimationInfo {
  const TextAnimationInfo(this.id, this.name, {this.perLetter = false});
  final String id, name;
  final bool perLetter;
}

class TextPresetInfo {
  const TextPresetInfo(this.id, this.name, this.style);
  final String id, name;
  final Map<String, dynamic> style; // merged into the text content
}

class StickerInfo {
  const StickerInfo(this.id, this.name, this.category);
  final String id, name, category;
}

/// An overlay effect (snow, rain, confetti, light leaks...): [tintable] ones take a colour.
class OverlayInfo {
  const OverlayInfo(this.id, this.name, this.category, {this.tintable = false});
  final String id, name, category;
  final bool tintable;
}

/// A saved project as the start screen lists it (read from small files in its package, never media).
class ProjectCard {
  ProjectCard(this.path, this.name, this.savedAt, this.duration, this.clips, this.width, this.height,
      {this.exported = false, this.hasPoster = false});
  factory ProjectCard.fromJson(Map<String, dynamic> e) => ProjectCard(
      e['path'] as String,
      e['name'] as String,
      DateTime.fromMillisecondsSinceEpoch(((e['savedAt'] ?? 0) as num).toInt() * 1000),
      ((e['duration'] ?? 0) as num).toDouble(),
      ((e['clips'] ?? 0) as num).toInt(),
      ((e['width'] ?? 0) as num).toInt(),
      ((e['height'] ?? 0) as num).toInt(),
      exported: e['exported'] == true,
      hasPoster: e['hasPoster'] == true);
  final String path, name;
  final DateTime savedAt;
  final double duration;
  final int clips, width, height;
  final bool exported, hasPoster;
}

/// A project's poster: RGBA8 pixels.
class PosterImage {
  PosterImage(this.width, this.height, this.rgba);
  final int width, height;
  final Uint8List rgba;
}

/// Catalogs for the text and sticker tools (fixed for the app's lifetime).
class TextCatalog {
  TextCatalog(this.fonts, this.animations, this.loops, this.presets, this.stickers);
  final List<FontInfo> fonts;
  final List<TextAnimationInfo> animations, loops;
  final List<TextPresetInfo> presets;
  final List<StickerInfo> stickers;
  List<String> get stickerCategories {
    final out = <String>[];
    for (final s in stickers) {
      if (!out.contains(s.category)) out.add(s.category);
    }
    return out;
  }
}

class SubtitleCatalog {
  SubtitleCatalog({required this.styles, required this.positions, required this.languages, required this.providers});
  final List<Map<String, dynamic>> styles; // {"id","name","style"}
  final List<Map<String, dynamic>> positions; // {"id","name","position"}
  final List<Map<String, dynamic>> languages; // {"code","name","english"}
  final List<Map<String, dynamic>> providers; // {"id","name","provider","url","model","needs_key","hint"}
}

/// One HTTP request of a translation, built by the core.
class TranslationRequest {
  TranslationRequest(this.method, this.url, this.headers, this.body);
  final String method, url, body;
  final List<(String, String)> headers;
}

/// A translation in progress (vs_translation). Free it with [dispose].
class SubtitleTranslation {
  SubtitleTranslation._(this._vs, this._t);
  final VStudio _vs;
  Pointer<VsTranslation> _t;

  int get count => _t == nullptr ? 0 : _vs._b.translationCount(_t);
  int get done => _t == nullptr ? 0 : _vs._b.translationDone(_t);
  String get error => _t == nullptr ? '' : _vs._b.translationError(_t).toDartString();

  TranslationRequest request(int i) {
    final p = _vs._b.translationRequestJson(_t, i);
    if (p == nullptr) throw VStudioException(error);
    final j = jsonDecode(p.toDartString()) as Map<String, dynamic>;
    _vs._b.free(p.cast());
    return TranslationRequest(j['method'] as String, j['url'] as String,
        [for (final h in (j['headers'] as List)) ((h as List)[0] as String, h[1] as String)], j['body'] as String);
  }

  /// The reply for batch [i]; null when accepted, else why it was not (retry).
  String? accept(int i, String body) =>
      using((a) => _vs._b.translationAccept(_t, i, body.toNativeUtf8(allocator: a)) == 0 ? null : error);

  /// Puts the result in the project (mode 0 replace, 1 new track, 2 bilingual); returns the track id.
  String apply(int mode) => _vs._take(_vs._b.translationApply(_vs._s, _t, mode));

  void dispose() {
    if (_t == nullptr) return;
    _vs._b.translationFree(_t);
    _t = nullptr;
  }
}

class VStudio {
  VStudio._(this._b, this._s);

  static VStudioBindings? _shared;
  static VStudioBindings get _bindings => _shared ??= VStudioBindings(openVStudioLibrary());

  final VStudioBindings _b;
  Pointer<VsSession> _s;

  // Preview frame buffer reused between polls (no per-frame allocation in Dart).
  Pointer<Uint8> _frameBuf = nullptr;
  int _frameCap = 0;
  final Pointer<Int64> _serial = calloc<Int64>();
  final Pointer<Int32> _w = calloc<Int32>();
  final Pointer<Int32> _h = calloc<Int32>();
  final Pointer<Int64> _pts = calloc<Int64>();

  static VStudio create(String name, {int width = 1080, int height = 1920, int fps = 30}) {
    final b = _bindings;
    final s = using((a) => b.sessionCreate(name.toNativeUtf8(allocator: a), width, height, fps, 1));
    if (s == nullptr) throw VStudioException(b.lastError(nullptr).toDartString());
    return VStudio._(b, s);
  }

  static VStudio open(String packageDir) {
    final b = _bindings;
    final s = using((a) => b.sessionOpen(packageDir.toNativeUtf8(allocator: a)));
    if (s == nullptr) throw VStudioException(b.lastError(nullptr).toDartString());
    return VStudio._(b, s);
  }

  void dispose() {
    if (_s == nullptr) return;
    _b.sessionDestroy(_s);
    _s = nullptr;
    if (_frameBuf != nullptr) calloc.free(_frameBuf);
    calloc.free(_serial);
    calloc.free(_w);
    calloc.free(_h);
    calloc.free(_pts);
  }

  String get version => _b.version().toDartString();

  /// Projects in [folder], newest first; [unfinishedOnly] keeps those changed since their last export,
  /// [exclude] (the open project) is left out. Instant: reads each package's small card file.
  static List<ProjectCard> recentProjects(String folder, {bool unfinishedOnly = true, int max = 12, String? exclude}) =>
      using((a) {
        final p = _bindings.recentProjectsJson(nullptr, folder.toNativeUtf8(allocator: a), unfinishedOnly ? 1 : 0, max,
            exclude == null ? nullptr : exclude.toNativeUtf8(allocator: a));
        if (p == nullptr) return <ProjectCard>[];
        final json = p.toDartString();
        _bindings.free(p.cast());
        return [for (final e in (jsonDecode(json) as List).cast<Map<String, dynamic>>()) ProjectCard.fromJson(e)];
      });

  /// The small picture saved with a project, or null.
  static PosterImage? projectPoster(String packageDir) => using((a) {
        final dir = packageDir.toNativeUtf8(allocator: a);
        final w = a<Int32>(), h = a<Int32>();
        if (_bindings.projectPoster(dir, nullptr, 0, w, h) != 1) return null;
        final n = w.value * h.value * 4;
        final buf = a<Uint8>(n);
        if (_bindings.projectPoster(dir, buf, n, w, h) != 1) return null;
        return PosterImage(w.value, h.value, Uint8List.fromList(buf.asTypedList(n)));
      });

  String _take(Pointer<Utf8> p) {
    if (p == nullptr) throw VStudioException(_b.lastError(_s).toDartString());
    final out = p.toDartString();
    _b.free(p.cast());
    return out;
  }

  void _check(int rc) {
    if (rc != 0) throw VStudioException(_b.lastError(_s).toDartString());
  }

  ProjectView project() {
    if (stickerNames.isEmpty) textCatalog(); // clip labels show sticker names
    return ProjectView.fromJson(_take(_b.projectJson(_s)));
  }

  int get revision => _b.projectRevision(_s);
  String modulesJson() => _take(_b.modulesJson(_s));

  String importMedia(String path) => using((a) => _take(_b.importMedia(_s, path.toNativeUtf8(allocator: a))));
  String addTrack({required bool audio, required String name}) =>
      using((a) => _take(_b.addTrack(_s, audio ? 1 : 0, name.toNativeUtf8(allocator: a))));
  String appendClip(String trackId, String assetId) =>
      using((a) => _take(_b.appendClip(_s, trackId.toNativeUtf8(allocator: a), assetId.toNativeUtf8(allocator: a))));
  void moveClip(String clipId, String trackId, double start) => using((a) => _check(
      _b.moveClip(_s, clipId.toNativeUtf8(allocator: a), trackId.toNativeUtf8(allocator: a), secondsToTicks(start))));
  void trimClip(String clipId, {required bool start, required double at}) =>
      using((a) => _check(_b.trimClip(_s, clipId.toNativeUtf8(allocator: a), start ? 0 : 1, secondsToTicks(at))));
  int splitAt(double t) {
    final n = _b.splitAt(_s, secondsToTicks(t));
    if (n < 0) throw VStudioException(_b.lastError(_s).toDartString());
    return n;
  }

  void removeTrack(String trackId) => using((a) => _check(_b.removeTrack(_s, trackId.toNativeUtf8(allocator: a))));
  void renameTrack(String trackId, String name) =>
      using((a) => _check(_b.renameTrack(_s, trackId.toNativeUtf8(allocator: a), name.toNativeUtf8(allocator: a))));
  void setTrackFlags(String trackId, {required bool enabled, required bool locked, required bool muted}) => using((a) =>
      _check(_b.setTrackFlags(_s, trackId.toNativeUtf8(allocator: a), enabled ? 1 : 0, locked ? 1 : 0, muted ? 1 : 0)));

  /// Reorders within tracks of the same type; 0 = bottom video layer / first audio track.
  void moveTrack(String trackId, int indexInType) =>
      using((a) => _check(_b.moveTrack(_s, trackId.toNativeUtf8(allocator: a), indexInType)));

  /// While dragging a clip: the start to use, or null when nothing is within [tolerance] seconds.
  /// Read-only and cheap (binary search per track), safe on every pointer move.
  double? snapClipMove(String clipId, double proposedStart, double tolerance, {double? playhead}) => using((a) {
        final snapped = a<Int32>();
        final t = _b.snapClipMove(_s, clipId.toNativeUtf8(allocator: a), secondsToTicks(proposedStart),
            secondsToTicks(tolerance), playhead == null ? -1 : secondsToTicks(playhead), snapped);
        return snapped.value != 0 ? ticksToSeconds(t) : null;
      });

  /// Deletes the clip and closes the gap on its track (one undo step).
  void rippleDelete(String clipId) => using((a) => _check(_b.rippleDelete(_s, clipId.toNativeUtf8(allocator: a))));
  bool get dirty => _b.isDirty(_s) != 0;

  /// For trimming: the edge time to use, or null when no edge/playhead is within [tolerance].
  double? snapTime(double t, double tolerance, {String? excludeClip, double? playhead}) => using((a) {
        final snapped = a<Int32>();
        final r = _b.snapTime(_s, secondsToTicks(t), secondsToTicks(tolerance),
            (excludeClip ?? '').toNativeUtf8(allocator: a), playhead == null ? -1 : secondsToTicks(playhead), snapped);
        return snapped.value != 0 ? ticksToSeconds(r) : null;
      });

  void removeClip(String clipId) => using((a) => _check(_b.removeClip(_s, clipId.toNativeUtf8(allocator: a))));
  bool undo() => _b.undo(_s) == 1;
  bool redo() => _b.redo(_s) == 1;
  void save(String packageDir) => using((a) => _check(_b.save(_s, packageDir.toNativeUtf8(allocator: a))));
  void autosave() => _check(_b.autosave(_s));

  /// Address of the native vs_session, for platform code that attaches a preview surface.
  /// Detach that surface before [dispose].
  int get nativeHandle => _s.address;

  void setPreviewSize(int w, int h) => _b.previewSetSize(_s, w, h);
  void seek(double t) => _b.previewSeek(_s, secondsToTicks(t));
  void play(double from) => _b.play(_s, secondsToTicks(from));
  void pause() => _b.pause(_s);
  bool get playing => _b.isPlaying(_s) == 1;
  double get playhead => ticksToSeconds(_b.playhead(_s));

  /// Newest preview frame if one arrived since the last call, else null.
  PreviewFrame? takeFrame() {
    var rc = _b.previewTakeFrame(_s, _frameBuf, _frameCap, _serial, _w, _h, _pts);
    if (rc == -1) {
      if (_frameBuf != nullptr) calloc.free(_frameBuf);
      _frameCap = _w.value * _h.value * 4;
      _frameBuf = calloc<Uint8>(_frameCap);
      rc = _b.previewTakeFrame(_s, _frameBuf, _frameCap, _serial, _w, _h, _pts);
    }
    if (rc != 1) return null;
    final bytes = Uint8List.fromList(_frameBuf.asTypedList(_w.value * _h.value * 4));
    return PreviewFrame(_w.value, _h.value, bytes, ticksToSeconds(_pts.value));
  }

  void startExport(String outPath, {int width = 0, int height = 0}) =>
      using((a) => _check(_b.exportStart(_s, outPath.toNativeUtf8(allocator: a), width, height, 0, 0, 0)));

  /// One-tap colour looks (same catalog and rendering as desktop).
  List<LookInfo> looks() {
    final p = _b.looksJson();
    if (p == nullptr) return const [];
    final list = jsonDecode(_take(p)) as List;
    return [
      for (final e in list.cast<Map<String, dynamic>>())
        LookInfo(e['id'] as String, e['name'] as String, e['category'] as String)
    ];
  }

  /// Applies [lookId] ('' removes) to [clipIds] as one undo step.
  void setClipLook(List<String> clipIds, String lookId, {double strength = 1}) => using((a) => _check(
      _b.setClipLook(_s, jsonEncode(clipIds).toNativeUtf8(allocator: a), lookId.toNativeUtf8(allocator: a), strength)));

  /// Log/HDR inputs the core can convert to Rec.709 (S-Log3, V-Log, Apple Log, HLG, ...).
  List<LogFormatInfo> logFormats() {
    final p = _b.logFormatsJson();
    if (p == nullptr) return const [];
    return [
      for (final e in (jsonDecode(_take(p)) as List).cast<Map<String, dynamic>>())
        LogFormatInfo(e['id'] as String, e['name'] as String, e['vendor'] as String)
    ];
  }

  /// Converts Log/HDR clips to Rec.709 ('' removes); always before other colour effects.
  void setClipColorSpace(List<String> clipIds, String input, {double exposure = 0}) =>
      using((a) => _check(_b.setClipColorSpace(
          _s, jsonEncode(clipIds).toNativeUtf8(allocator: a), input.toNativeUtf8(allocator: a), exposure)));

  /// 3D LUT (.cube): absolute, or relative to the project's media folder ('' removes).
  void setClipLut(List<String> clipIds, String path, {double strength = 1}) => using((a) => _check(
      _b.setClipLut(_s, jsonEncode(clipIds).toNativeUtf8(allocator: a), path.toNativeUtf8(allocator: a), strength)));

  /// What the asset's file says it is ('hlg'), or '' when unknown.
  String suggestedLogFormat(String assetId) {
    final p = using((a) => _b.suggestedLogFormat(_s, assetId.toNativeUtf8(allocator: a)));
    return p == nullptr ? '' : _take(p);
  }

  // ---- keyframes inside one clip; times are timeline seconds, every call is one undo step ----

  /// Adds a key of [group] at [t] holding the clip's current values there.
  void addKeyframe(String clipId, KeyGroup group, double t) =>
      using((a) => _check(_b.addKeyframe(_s, clipId.toNativeUtf8(allocator: a), group.index, secondsToTicks(t))));

  /// Removes the [group] key at [t] (within half a frame); false when there was none.
  bool removeKeyframe(String clipId, KeyGroup group, double t) => using((a) {
        final rc = _b.removeKeyframe(_s, clipId.toNativeUtf8(allocator: a), group.index, secondsToTicks(t));
        if (rc < 0) throw VStudioException(_b.lastError(_s).toDartString());
        return rc == 1;
      });

  /// ◇ button: removes the key at [t], or adds one. Returns true when a key was added.
  bool toggleKeyframe(String clipId, KeyGroup group, double t) {
    if (removeKeyframe(clipId, group, t)) return false;
    addKeyframe(clipId, group, t);
    return true;
  }

  void clearKeyframes(String clipId, KeyGroup group) =>
      using((a) => _check(_b.clearKeyframes(_s, clipId.toNativeUtf8(allocator: a), group.index)));

  /// Sets values at [t] ("scale", "x", "y", "opacity", "volume"): a key when that property
  /// is animated, otherwise the clip's own value. x/y: canvas pixels from the centre.
  void setPropertyAt(String clipId, double t, Map<String, double> values) => using((a) => _check(_b.setPropertyAt(
      _s, clipId.toNativeUtf8(allocator: a), secondsToTicks(t), jsonEncode(values).toNativeUtf8(allocator: a))));

  // ---- live preview during a gesture (pinch, corner drag, slider) ----
  // The preview shows the edit at once; the project and the undo history stay as they are until the
  // gesture ends with the real edit (one undo step), or liveEnd() drops it.

  void livePropertyAt(String clipId, double t, Map<String, double> values) => using((a) => _check(_b.livePropertyAt(
      _s, clipId.toNativeUtf8(allocator: a), secondsToTicks(t), jsonEncode(values).toNativeUtf8(allocator: a))));
  void liveOverlayRegion(String clipId, double t, double x, double y, double w, double h) =>
      using((a) => _check(_b.liveOverlayRegion(_s, clipId.toNativeUtf8(allocator: a), secondsToTicks(t), x, y, w, h)));
  void liveClipContent(String clipId, Map<String, dynamic> patch) => using((a) =>
      _check(_b.liveClipContent(_s, clipId.toNativeUtf8(allocator: a), jsonEncode(patch).toNativeUtf8(allocator: a))));
  void liveEnd() => _b.liveEnd(_s);

  /// Zooms so the region (0..1 of the frame) fills the frame at [t].
  void zoomToRegion(String clipId, double t, double x, double y, double w, double h) =>
      using((a) => _check(_b.zoomToRegion(_s, clipId.toNativeUtf8(allocator: a), secondsToTicks(t), x, y, w, h)));

  /// The clip's animated values at [t] and whether keys sit there.
  ClipKeyState clipStateAt(String clipId, double t) => ClipKeyState.fromJson(
      jsonDecode(using((a) => _take(_b.clipStateAt(_s, clipId.toNativeUtf8(allocator: a), secondsToTicks(t)))))
          as Map<String, dynamic>);

  // ---- covers: blur / mosaic / bars over a region (x, y, w, h: 0..1 of the clip's source frame) ----

  List<CoverStyleInfo> coverStyles() => [
        for (final e in (jsonDecode(_take(_b.coverStylesJson())) as List).cast<Map<String, dynamic>>())
          CoverStyleInfo(e['id'] as String, e['name'] as String, e['category'] as String, e['animated'] == true)
      ];
  List<(String, String)> coverShapes() => [
        for (final e in (jsonDecode(_take(_b.coverShapesJson())) as List).cast<Map<String, dynamic>>())
          (e['id'] as String, e['name'] as String)
      ];

  /// Adds a cover (centred box unless [params] say otherwise); returns its id.
  String addCover(String clipId, [Map<String, Object?> params = const {}]) => using(
      (a) => _take(_b.addCover(_s, clipId.toNativeUtf8(allocator: a), jsonEncode(params).toNativeUtf8(allocator: a))));
  void updateCover(String clipId, String coverId, Map<String, Object?> patch) => using((a) => _check(_b.updateCover(
      _s,
      clipId.toNativeUtf8(allocator: a),
      coverId.toNativeUtf8(allocator: a),
      jsonEncode(patch).toNativeUtf8(allocator: a))));
  void removeCover(String clipId, String coverId) =>
      using((a) => _check(_b.removeCover(_s, clipId.toNativeUtf8(allocator: a), coverId.toNativeUtf8(allocator: a))));

  /// Region / amount / rotation at [t]: keyed when the cover follows motion.
  void setCoverAt(String clipId, String coverId, double t, Map<String, double> values) =>
      using((a) => _check(_b.setCoverAt(_s, clipId.toNativeUtf8(allocator: a), coverId.toNativeUtf8(allocator: a),
          secondsToTicks(t), jsonEncode(values).toNativeUtf8(allocator: a))));
  void setCoverFollowing(String clipId, String coverId, double t, bool on) => using((a) {
        final c = clipId.toNativeUtf8(allocator: a), id = coverId.toNativeUtf8(allocator: a);
        _check(on ? _b.keyCover(_s, c, id, secondsToTicks(t)) : _b.clearCoverKeys(_s, c, id, secondsToTicks(t)));
      });
  CoverState coverStateAt(String clipId, String coverId, double t) => CoverState(jsonDecode(using((a) => _take(_b
          .coverStateAt(_s, clipId.toNativeUtf8(allocator: a), coverId.toNativeUtf8(allocator: a), secondsToTicks(t)))))
      as Map<String, dynamic>);
  List<(String, Map<String, dynamic>)> covers(String clipId) => [
        for (final e in (jsonDecode(using((a) => _take(_b.coversJson(_s, clipId.toNativeUtf8(allocator: a))))) as List)
            .cast<Map<String, dynamic>>())
          (e['id'] as String, e['params'] as Map<String, dynamic>)
      ];

  /// The cover as shown on the canvas at [t] (0..1 of the output frame): (x, y, w, h), or null.
  (double, double, double, double)? coverCanvasRect(String clipId, String coverId, double t) => using((a) {
        final out = a<Double>(4);
        final rc = _b.coverCanvasRect(
            _s, clipId.toNativeUtf8(allocator: a), coverId.toNativeUtf8(allocator: a), secondsToTicks(t), out);
        return rc == 0 ? (out[0], out[1], out[2], out[3]) : null;
      });

  /// Sets the cover from a rectangle drawn on the preview (canvas 0..1).
  void setCoverCanvasRect(String clipId, String coverId, double t, double x, double y, double w, double h) =>
      using((a) => _check(_b.setCoverCanvasRect(
          _s, clipId.toNativeUtf8(allocator: a), coverId.toNativeUtf8(allocator: a), secondsToTicks(t), x, y, w, h)));

  /// A copy of [rgba] with the style over its middle, for style thumbnails.
  Uint8List? coverPreview(String style, String shape, Uint8List rgba, int width, int height) => using((a) {
        final buf = a<Uint8>(rgba.length);
        buf.asTypedList(rgba.length).setAll(0, rgba);
        if (_b.coverPreview(style.toNativeUtf8(allocator: a), shape.toNativeUtf8(allocator: a), buf, width, height) !=
            0) {
          return null;
        }
        return Uint8List.fromList(buf.asTypedList(rgba.length));
      });

  /// Follows the subject from [from] to the clip's end on a background thread.
  void startTrackCover(String clipId, String coverId, double from) => using((a) => _check(_b.trackCoverStart(
      _s, clipId.toNativeUtf8(allocator: a), coverId.toNativeUtf8(allocator: a), secondsToTicks(from), 0)));

  /// Poll once per frame: when it reports done/lost, the keys were applied (one undo step).
  (TrackState, double) trackStatus() => using((a) {
        final p = a<Double>();
        final st = _b.trackStatus(_s, p);
        return (
          switch (st) {
            1 => TrackState.running,
            2 => TrackState.done,
            -3 => TrackState.lost,
            -2 => TrackState.cancelled,
            -1 => TrackState.failed,
            _ => TrackState.idle,
          },
          p.value
        );
      });
  void cancelTrack() => _b.trackCancel(_s);

  // ---- transitions between clips (fade, dip to black, slides, wipes...) ----

  List<TransitionInfo> transitions() => [
        for (final e in (jsonDecode(_take(_b.transitionsJson())) as List).cast<Map<String, dynamic>>())
          TransitionInfo(e['id'] as String, e['name'] as String, e['category'] as String)
      ];

  /// Sets (or with [type] null/empty removes) the transition at a clip's start (edge 0) or end (edge 1).
  void setTransition(String clipId, int edge, String? type, {double seconds = 0.5}) =>
      using((a) => _check(_b.setTransition(_s, clipId.toNativeUtf8(allocator: a), edge,
          (type ?? '').toNativeUtf8(allocator: a), secondsToTicks(seconds))));

  /// The same transition on every cut between touching clips; returns how many.
  int setTransitionEverywhere(String? type, {double seconds = 0.5}) => using((a) {
        final n = _b.setTransitionEverywhere(_s, (type ?? '').toNativeUtf8(allocator: a), secondsToTicks(seconds));
        if (n < 0) throw VStudioException(lastError);
        return n;
      });

  /// Every place a transition can go, per video track id.
  Map<String, List<CutPoint>> cutPoints() {
    final j = jsonDecode(_take(_b.cutPointsJson(_s))) as Map<String, dynamic>;
    return {
      for (final e in j.entries)
        e.key: [for (final c in (e.value as List).cast<Map<String, dynamic>>()) CutPoint.fromJson(e.key, c)]
    };
  }

  /// [a] blended into [b] (both width x height RGBA) at [progress] 0..1, for thumbnails.
  Uint8List? transitionPreview(String type, Uint8List a, Uint8List b, int width, int height, double progress) =>
      using((al) {
        final pa = al<Uint8>(a.length), pb = al<Uint8>(b.length), po = al<Uint8>(a.length);
        pa.asTypedList(a.length).setAll(0, a);
        pb.asTypedList(b.length).setAll(0, b);
        if (_b.transitionPreview(type.toNativeUtf8(allocator: al), pa, pb, po, width, height, progress) != 0) {
          return null;
        }
        return Uint8List.fromList(po.asTypedList(a.length));
      });

  // ---- video effects: zoom, rotate, reflection (video_fx.hpp) ----

  static List<VideoFxInfo>? _videoFx;

  /// Every video effect, in catalogue order (read once).
  List<VideoFxInfo> videoFx() => _videoFx ??= [
        for (final e in (jsonDecode(_take(_b.videoFxJson())) as List).cast<Map<String, dynamic>>())
          VideoFxInfo(e['id'] as String, e['name'] as String, e['group'] as String, e['groupName'] as String,
              animated: e['animated'] == true, usesLine: e['usesLine'] == true)
      ];

  /// Puts effect [fxId] of [group] ("zoom", "rotate", "reflect") on the clips, replacing that group's
  /// effect; '' removes it. [line] < 0 keeps the default water line. One undo step.
  void setClipVideoFx(List<String> clipIds, String group, String fxId,
          {double amount = 1, double speed = 1, double line = -1}) =>
      using((a) => _check(_b.setClipVideoFx(_s, jsonEncode(clipIds).toNativeUtf8(allocator: a),
          group.toNativeUtf8(allocator: a), fxId.toNativeUtf8(allocator: a), amount, speed, line)));

  /// A [width] x [height] thumbnail of effect [fxId] at [seconds] (of a 4 s loop), over [picture]
  /// ([pw] x [ph] RGBA) or, without one, a small built-in landscape.
  Uint8List? videoFxIcon(String fxId, double seconds, int width, int height,
          {Uint8List? picture, int pw = 0, int ph = 0}) =>
      using((al) {
        Pointer<Uint8> pic = nullptr;
        if (picture != null && pw > 0 && ph > 0) {
          pic = al<Uint8>(picture.length);
          pic.asTypedList(picture.length).setAll(0, picture);
        }
        final out = al<Uint8>(width * height * 4);
        if (_b.videoFxIcon(fxId.toNativeUtf8(allocator: al), seconds, pic, pw, ph, out, width, height) != 0) {
          return null;
        }
        return Uint8List.fromList(out.asTypedList(width * height * 4));
      });

  // ---- text and stickers: clips without media, drawn by the core from their "content" ----

  static TextCatalog? _catalog;

  /// Fonts, animations, text presets and stickers (read once, then cached).
  TextCatalog textCatalog() {
    final cached = _catalog;
    if (cached != null) return cached;
    List<Map<String, dynamic>> list(Object? j) => (j as List).cast<Map<String, dynamic>>();
    final anims = jsonDecode(_take(_b.textAnimationsJson())) as Map<String, dynamic>;
    TextAnimationInfo anim(Map<String, dynamic> e) =>
        TextAnimationInfo(e['id'] as String, e['name'] as String, perLetter: e['per_letter'] == true);
    final c = TextCatalog(
      [
        for (final e in list(jsonDecode(_take(_b.fontsJson()))))
          FontInfo(e['id'] as String, e['name'] as String, (e['style'] ?? '') as String)
      ],
      [for (final e in list(anims['animations'])) anim(e)],
      [for (final e in list(anims['loops'])) anim(e)],
      [
        for (final e in list(jsonDecode(_take(_b.textPresetsJson()))))
          TextPresetInfo(e['id'] as String, e['name'] as String, e['style'] as Map<String, dynamic>)
      ],
      [
        for (final e in list(jsonDecode(_take(_b.stickersJson()))))
          StickerInfo(e['id'] as String, e['name'] as String, e['category'] as String)
      ],
    );
    for (final st in c.stickers) {
      stickerNames[st.id] = st.name;
    }
    overlayCatalog();
    return _catalog = c;
  }

  static List<OverlayInfo>? _overlays;

  /// Overlay effects in display order (read once, then cached).
  List<OverlayInfo> overlayCatalog() {
    final cached = _overlays;
    if (cached != null) return cached;
    final list = [
      for (final e in (jsonDecode(_take(_b.overlaysJson())) as List).cast<Map<String, dynamic>>())
        OverlayInfo(e['id'] as String, e['name'] as String, e['category'] as String, tintable: e['tintable'] == true)
    ];
    for (final o in list) {
      overlayNames[o.id] = o.name;
    }
    return _overlays = list;
  }

  /// Content for a new overlay covering the whole frame (sized to the canvas when added).
  Map<String, dynamic> defaultOverlay(String overlayId) =>
      jsonDecode(using((a) => _take(_b.defaultOverlayJson(overlayId.toNativeUtf8(allocator: a)))))
          as Map<String, dynamic>;

  /// Overlay [id] at [seconds] over a dark backdrop, an opaque [width] x [height] RGBA thumbnail.
  Uint8List? overlayIcon(String id, int width, int height, {double seconds = 2}) => using((a) {
        final n = width * height * 4;
        final buf = a<Uint8>(n);
        if (_b.overlayIcon(id.toNativeUtf8(allocator: a), seconds, buf, width, height) != 0) return null;
        return Uint8List.fromList(buf.asTypedList(n));
      });

  /// The region an overlay covers, as seen at [t] (0..1 of the frame). One undo step.
  void setOverlayRegion(String clipId, double t, double x, double y, double w, double h) =>
      using((a) => _check(_b.setOverlayRegion(_s, clipId.toNativeUtf8(allocator: a), secondsToTicks(t), x, y, w, h)));

  /// Adds a TrueType/OpenType font (e.g. a company font); false when the bytes are not a usable font.
  bool registerFont(String id, String name, Uint8List bytes) => using((a) {
        final buf = a<Uint8>(bytes.length);
        buf.asTypedList(bytes.length).setAll(0, bytes);
        return _b.registerFont(id.toNativeUtf8(allocator: a), name.toNativeUtf8(allocator: a), buf, bytes.length) == 0;
      });

  /// Content for a new text clip / sticker clip.
  Map<String, dynamic> defaultText(String text) =>
      jsonDecode(using((a) => _take(_b.defaultTextJson(text.toNativeUtf8(allocator: a))))) as Map<String, dynamic>;
  Map<String, dynamic> defaultSticker(String stickerId) =>
      jsonDecode(using((a) => _take(_b.defaultStickerJson(stickerId.toNativeUtf8(allocator: a)))))
          as Map<String, dynamic>;

  /// Adds a text/sticker clip at [at] for [seconds] (on [trackId], or the topmost free overlay track,
  /// a new one when needed). One undo step; returns the clip id.
  String addGeneratedClip(Map<String, dynamic> content, double at, double seconds, {String? trackId}) =>
      using((a) => _take(_b.addGeneratedClip(_s, jsonEncode(content).toNativeUtf8(allocator: a), secondsToTicks(at),
          secondsToTicks(seconds), (trackId ?? '').toNativeUtf8(allocator: a))));

  /// JSON merge patch into the clip's content (null removes a key). One undo step.
  void updateClipContent(String clipId, Map<String, dynamic> patch) => using((a) => _check(
      _b.updateClipContent(_s, clipId.toNativeUtf8(allocator: a), jsonEncode(patch).toNativeUtf8(allocator: a))));

  /// The text/sticker box on the canvas at [t] (0..1 of the frame), or null outside the clip.
  (double, double, double, double)? generatedRect(String clipId, double t) => using((a) {
        final out = a<Double>(4);
        final rc = _b.generatedRect(_s, clipId.toNativeUtf8(allocator: a), secondsToTicks(t), out);
        return rc == 0 ? (out[0], out[1], out[2], out[3]) : null;
      });

  /// Sticker [id] drawn into a transparent [size] x [size] RGBA image, its idle motion at [seconds].
  Uint8List? stickerIcon(String id, int size, {double seconds = 0}) => using((a) {
        final n = size * size * 4;
        final buf = a<Uint8>(n);
        if (_b.stickerIcon(id.toNativeUtf8(allocator: a), seconds, buf, size, size) != 0) return null;
        return Uint8List.fromList(buf.asTypedList(n));
      });

  /// [content] at [local] seconds of a clip lasting [length], fitted into a transparent RGBA image.
  Uint8List? contentPreview(Map<String, dynamic> content, double local, double length, int width, int height) =>
      using((a) {
        final n = width * height * 4;
        final buf = a<Uint8>(n);
        if (_b.contentPreview(jsonEncode(content).toNativeUtf8(allocator: a), local, length, buf, width, height) != 0) {
          return null;
        }
        return Uint8List.fromList(buf.asTypedList(n));
      });

  // ---- subtitles: a subtitle track holds lines {"kind": "subtitle", "text"}; its look is on the track ----

  static SubtitleCatalog? _subCatalog;

  /// Styles, positions, languages and AI connectors (read once).
  SubtitleCatalog subtitleCatalog() {
    final cached = _subCatalog;
    if (cached != null) return cached;
    List<Map<String, dynamic>> list(String s) => (jsonDecode(s) as List).cast<Map<String, dynamic>>();
    return _subCatalog = SubtitleCatalog(
      styles: list(_take(_b.subtitleStylesJson())),
      positions: list(_take(_b.subtitlePositionsJson())),
      languages: list(_take(_b.subtitleLanguagesJson())),
      providers: list(_take(_b.translationProvidersJson())),
    );
  }

  /// A new subtitle track on top of the picture; returns its id.
  String addSubtitleTrack({String name = 'Phụ đề', Map<String, dynamic>? settings}) =>
      using((a) => _take(_b.addSubtitleTrack(
          _s, name.toNativeUtf8(allocator: a), jsonEncode(settings ?? const {}).toNativeUtf8(allocator: a))));

  /// A line at [at] on [trackId] (or the topmost subtitle track, created when needed); returns the clip id.
  String addSubtitle(double at, String text, {String? trackId, double seconds = 2}) =>
      using((a) => _take(_b.addSubtitle(_s, (trackId ?? '').toNativeUtf8(allocator: a), secondsToTicks(at),
          text.toNativeUtf8(allocator: a), secondsToTicks(seconds))));

  /// The text of an .srt / .vtt / .ass file as a new track; returns its id.
  String importSubtitles(String fileText, {String name = 'Phụ đề', String lang = '', double offset = 0}) =>
      using((a) => _take(_b.importSubtitles(_s, fileText.toNativeUtf8(allocator: a), name.toNativeUtf8(allocator: a),
          lang.toNativeUtf8(allocator: a), secondsToTicks(offset))));

  /// The track as an .srt (or .vtt) file.
  String exportSubtitles(String trackId, {String format = 'srt'}) => using(
      (a) => _take(_b.exportSubtitles(_s, trackId.toNativeUtf8(allocator: a), format.toNativeUtf8(allocator: a))));

  /// JSON merge patch into the track's subtitle settings (style, position, max_width, lang).
  void setSubtitleSettings(String trackId, Map<String, dynamic> patch) => using((a) => _check(
      _b.setSubtitleSettings(_s, trackId.toNativeUtf8(allocator: a), jsonEncode(patch).toNativeUtf8(allocator: a))));

  void shiftSubtitles(String trackId, double seconds) =>
      using((a) => _check(_b.shiftSubtitles(_s, trackId.toNativeUtf8(allocator: a), secondsToTicks(seconds))));

  /// An AI translation of a track: the app sends [SubtitleTranslation.request]s, hands back the replies.
  SubtitleTranslation beginTranslation(String trackId, Map<String, dynamic> config,
      {String from = '', required String to, String notes = '', int batchSize = 40}) {
    final t = using((a) => _b.translationBegin(
        _s,
        trackId.toNativeUtf8(allocator: a),
        jsonEncode(config).toNativeUtf8(allocator: a),
        from.toNativeUtf8(allocator: a),
        to.toNativeUtf8(allocator: a),
        notes.toNativeUtf8(allocator: a),
        batchSize));
    if (t == nullptr) throw VStudioException(_b.lastError(_s).toDartString());
    return SubtitleTranslation._(this, t);
  }

  /// A copy of [rgba] (width x height) with the look applied, for look thumbnails.
  Uint8List? lookPreview(String lookId, Uint8List rgba, int width, int height, {double strength = 1}) => using((a) {
        final buf = a<Uint8>(rgba.length);
        buf.asTypedList(rgba.length).setAll(0, rgba);
        if (_b.lookPreview(lookId.toNativeUtf8(allocator: a), strength, buf, width, height) != 0) return null;
        return Uint8List.fromList(buf.asTypedList(rgba.length));
      });

  // ---- pro colour grade (grade.hpp): same effect, same maths as desktop ----

  /// Commits [params] as the clips' grade (one undo step); null or neutral removes it.
  void setClipGrade(List<String> clipIds, Map<String, dynamic>? params) => using((a) => _check(_b.setClipGrade(
      _s, jsonEncode(clipIds).toNativeUtf8(allocator: a), jsonEncode(params).toNativeUtf8(allocator: a))));

  /// The clip's grade parameters ({} when it has none).
  Map<String, dynamic> clipGrade(String clipId) => using((a) {
        final p = _b.clipGradeJson(_s, clipId.toNativeUtf8(allocator: a));
        if (p == nullptr) throw VStudioException(_b.lastError(_s).toDartString());
        return jsonDecode(_take(p)) as Map<String, dynamic>;
      });

  /// Shows [params] on the preview while a control is dragged, without an undo step;
  /// [clipId] '' clears. Commit with [setClipGrade] when the finger lifts.
  void setEffectPreview(String clipId, String type, Map<String, dynamic>? params) =>
      using((a) => _check(_b.setEffectPreview(_s, clipId.toNativeUtf8(allocator: a), type.toNativeUtf8(allocator: a),
          (params == null ? '' : jsonEncode(params)).toNativeUtf8(allocator: a))));

  List<HslBandInfo> hslBands() {
    final p = _b.hslBandsJson();
    if (p == nullptr) return const [];
    return [
      for (final e in (jsonDecode(_take(p)) as List).cast<Map<String, dynamic>>())
        HslBandInfo(e['id'] as String, e['name'] as String, (e['hue'] as num).toDouble())
    ];
  }

  /// The applied curve through [points] ([[x, y], ...] in 0..1), sampled at [n] inputs.
  Float32List gradeCurve(List<List<double>> points, int n) => using((a) {
        final out = a<Float>(n);
        if (_b.gradeCurve(jsonEncode(points).toNativeUtf8(allocator: a), n, out) != 0) return Float32List(0);
        return Float32List.fromList(out.asTypedList(n));
      });

  /// Wheel position (angle in degrees, 0 = red, counter-clockwise; distance 0..1) to the stored balance.
  List<double> wheelToBalance(double angleDeg, double distance) => using((a) {
        final rgb = a<Double>(3);
        _b.wheelToBalance(angleDeg, distance, rgb);
        return [rgb[0], rgb[1], rgb[2]];
      });

  (double angle, double distance) balanceToWheel(List<double> rgb) => using((a) {
        final p = a<Double>(3), ang = a<Double>(1), dist = a<Double>(1);
        for (var i = 0; i < 3; i++) {
          p[i] = i < rgb.length ? rgb[i] : 0;
        }
        _b.balanceToWheel(p, ang, dist);
        return (ang.value, dist.value);
      });

  /// Eyedropper: average colour around ([x], [y]) (0..1 of the frame) of the newest preview frame.
  (double r, double g, double b)? pickPreviewColor(double x, double y) => using((a) {
        final rgb = a<Double>(3);
        if (_b.previewPickColor(_s, x, y, rgb) != 0) return null;
        return (rgb[0], rgb[1], rgb[2]);
      });

  /// Scope of the newest preview frame as RGBA ([kind]: 0 waveform, 1 RGB parade, 2 vectorscope,
  /// 3 histogram); null before the first frame.
  Uint8List? previewScope(int kind, int width, int height) => using((a) {
        final n = width * height * 4;
        final buf = a<Uint8>(n);
        if (_b.previewScope(_s, kind, width, height, buf) != 0) return null;
        return Uint8List.fromList(buf.asTypedList(n));
      });

  /// A copy of [rgba] with grade [params] applied (thumbnails).
  Uint8List? gradePreview(Map<String, dynamic> params, Uint8List rgba, int width, int height) => using((a) {
        final buf = a<Uint8>(rgba.length);
        buf.asTypedList(rgba.length).setAll(0, rgba);
        if (_b.gradePreview(jsonEncode(params).toNativeUtf8(allocator: a), buf, width, height) != 0) return null;
        return Uint8List.fromList(buf.asTypedList(rgba.length));
      });

  /// Shared presets, sized for this project (its aspect is kept); bitrate and size for [format].
  List<ExportPresetView> exportPresets([String format = 'h264']) => using((a) =>
      (jsonDecode(_take(_b.exportPresetsFormatJson(_s, format.toNativeUtf8(allocator: a)))) as List)
          .map((e) => ExportPresetView.fromJson(e as Map<String, dynamic>))
          .toList());

  /// Video formats: h264, hevc, hevc10 (H.265 10-bit).
  List<ExportFormatView> exportFormats() => (jsonDecode(_take(_b.exportFormatsJson(_s))) as List)
      .map((e) => ExportFormatView.fromJson(e as Map<String, dynamic>))
      .toList();
  void startExportPresetFormat(String outPath, String presetId, String format) => using((a) => _check(
      _b.exportStartPresetFormat(_s, outPath.toNativeUtf8(allocator: a), presetId.toNativeUtf8(allocator: a),
          format.toNativeUtf8(allocator: a))));
  void startExportPreset(String outPath, String presetId) => using(
      (a) => _check(_b.exportStartPreset(_s, outPath.toNativeUtf8(allocator: a), presetId.toNativeUtf8(allocator: a))));

  (ExportState, double) exportStatus() => using((a) {
        final p = a<Double>();
        final st = _b.exportStatus(_s, p);
        final state = switch (st) {
          1 => ExportState.running,
          2 => ExportState.done,
          -1 => ExportState.failed,
          -2 => ExportState.cancelled,
          _ => ExportState.idle,
        };
        return (state, p.value);
      });

  void cancelExport() => _b.exportCancel(_s);

  // ---- media library: computed on a core background worker, cached on disk ----

  void requestThumbnails(String assetId, {int count = 12, int height = 54}) =>
      using((a) => _b.requestThumbnails(_s, assetId.toNativeUtf8(allocator: a), count, height));
  void requestWaveform(String assetId, {int bucketsPerSecond = 100}) =>
      using((a) => _b.requestWaveform(_s, assetId.toNativeUtf8(allocator: a), bucketsPerSecond));

  /// Increases whenever a thumbnail strip or waveform becomes ready: poll it once per frame.
  int get librarySerial => _b.librarySerial(_s);

  ThumbStrip? thumbnails(String assetId) => using((a) {
        final id = assetId.toNativeUtf8(allocator: a);
        final n = a<Int32>(), w = a<Int32>(), h = a<Int32>();
        if (_b.thumbnails(_s, id, nullptr, 0, n, w, h) != 1) return null;
        final size = n.value * w.value * h.value * 4;
        final buf = a<Uint8>(size);
        if (_b.thumbnails(_s, id, buf, size, n, w, h) != 1) return null;
        return ThumbStrip(w.value, h.value, n.value, Uint8List.fromList(buf.asTypedList(size)));
      });

  WaveformData? waveform(String assetId) => using((a) {
        final id = assetId.toNativeUtf8(allocator: a);
        final bps = a<Int32>();
        final n = _b.waveform(_s, id, nullptr, 0, bps);
        if (n <= 0) return null;
        final buf = a<Int8>(n * 2);
        _b.waveform(_s, id, buf, n * 2, bps);
        return WaveformData(bps.value, Int8List.fromList(buf.asTypedList(n * 2)));
      });

  /// Assets whose media file is gone (moved, renamed, deleted).
  List<String> missingAssets() => (jsonDecode(_take(_b.missingAssetsJson(_s))) as List).cast<String>();

  /// Points an asset at another file; undoable.
  void relink(String assetId, String path) =>
      using((a) => _check(_b.relinkAsset(_s, assetId.toNativeUtf8(allocator: a), path.toNativeUtf8(allocator: a))));
  String get lastError => _b.lastError(_s).toDartString();

  // ---- sound and clip tools (vs_tool_command: JSON in, JSON out) ----

  /// Runs one tool command; throws [VStudioException] when the core refuses it.
  Map<String, dynamic> tool(Map<String, dynamic> command) {
    final out = jsonDecode(using((a) => _take(_b.toolCommand(_s, jsonEncode(command).toNativeUtf8(allocator: a)))))
        as Map<String, dynamic>;
    if (out['ok'] != true) throw VStudioException('${out['error']}');
    return out;
  }

  List<EqPresetInfo> eqPresets() => [
        for (final e in (tool({'cmd': 'eq_presets'})['presets'] as List).cast<Map<String, dynamic>>())
          EqPresetInfo(e['id'] as String, e['name'] as String,
              [for (final g in e['gains'] as List) (g as num).toDouble()])
      ];

  ClipSound clipSound(String clipId) => ClipSound(tool({'cmd': 'clip_audio', 'clip': clipId}));
  void setClipAudio(List<String> clipIds, Map<String, dynamic> patch) =>
      tool({'cmd': 'set_clip_audio', 'clips': clipIds, 'patch': patch});
  String detachAudio(String clipId) => tool({'cmd': 'detach_audio', 'clip': clipId})['clip'] as String;

  /// A recorded take at [at] seconds on a free "Thu âm" track. Returns the clip id.
  String placeAudio(String assetId, double at) =>
      tool({'cmd': 'place_audio', 'asset': assetId, 'at': secondsToTicks(at)})['clip'] as String;
  void setTrackMix(String trackId, {double pan = 0, bool solo = false}) =>
      tool({'cmd': 'set_track_mix', 'track': trackId, 'pan': pan, 'solo': solo});
  void setTrackVolume(String trackId, double volume) =>
      tool({'cmd': 'set_track_volume', 'track': trackId, 'volume': volume});
  void setClipVolume(String clipId, double volume) => tool({'cmd': 'set_clip_volume', 'clip': clipId, 'volume': volume});
  void setReversed(List<String> clipIds, bool on) => tool({'cmd': 'set_reversed', 'clips': clipIds, 'on': on});

  /// Ready-made speed ramps: (id, name, points as (u 0..1 of the clip, speed)).
  List<SpeedPreset> speedPresets() => [
        for (final e in (tool({'cmd': 'speed_presets'})['presets'] as List).cast<Map<String, dynamic>>())
          SpeedPreset(e['id'] as String, e['name'] as String, _points(e['points'] as List))
      ];
  ClipSpeed clipSpeed(String clipId) {
    final o = tool({'cmd': 'clip_speed', 'clip': clipId});
    return ClipSpeed(
      can: o['can'] == true,
      why: o['why'] as String? ?? '',
      speed: (o['speed'] as num).toDouble(),
      points: _points(o['points'] as List),
      preset: o['preset'] as String? ?? '',
      seconds: ticksToSeconds((o['duration'] as num).toInt()),
    );
  }

  /// [speed] 0.1..10; [points] empty = constant. The clips after it move with its end.
  void setSpeed(List<String> clipIds, double speed, {List<(double, double)> points = const [], String preset = ''}) =>
      tool({
        'cmd': 'set_speed',
        'clips': clipIds,
        'speed': speed,
        'points': [for (final (u, v) in points) [u, v]],
        'preset': preset,
      });
  static List<(double, double)> _points(List raw) => [
        for (final p in raw.cast<List>()) ((p[0] as num).toDouble(), (p[1] as num).toDouble())
      ];
  String freezeFrame(String clipId, double at, {double seconds = 3}) => tool({
        'cmd': 'freeze_frame',
        'clip': clipId,
        'at': secondsToTicks(at),
        'duration': secondsToTicks(seconds)
      })['clip'] as String;
  void setEffect(List<String> clipIds, String type, Map<String, dynamic>? params) =>
      tool({'cmd': 'set_effect', 'clips': clipIds, 'type': type, 'params': params});
  Map<String, dynamic>? effect(String clipId, String type) =>
      tool({'cmd': 'effect', 'clip': clipId, 'type': type})['params'] as Map<String, dynamic>?;
  String guessKeyColor() => tool({'cmd': 'guess_key_color'})['color'] as String;

  /// Background job: "noise" (then noise reduction at [value]) or "stabilize" (strength [value]).
  void startToolJob(String job, String clipId, double value) =>
      tool({'cmd': 'start', 'job': job, 'clip': clipId, 'value': value});

  /// (status, progress, error): 1 running, 2 done (applied, one undo step), -1 failed, -2 cancelled.
  (int, double, String) toolJobStatus() {
    final r = tool({'cmd': 'job_status'});
    return (r['status'] as int, (r['progress'] as num).toDouble(), r['error'] as String? ?? '');
  }

  void cancelToolJob() => tool({'cmd': 'job_cancel'});
  void setStabilizeStrength(String clipId, double strength) =>
      tool({'cmd': 'stabilize_strength', 'clip': clipId, 'strength': strength});
}

class EqPresetInfo {
  const EqPresetInfo(this.id, this.name, this.gains);
  final String id, name;
  final List<double> gains;
}

/// A clip's sound settings (clip "audio" in the project) and its direction.
class ClipSound {
  ClipSound(Map<String, dynamic> j)
      : audio = (j['audio'] as Map?)?.cast<String, dynamic>() ?? const {},
        volume = (j['volume'] as num?)?.toDouble() ?? 1,
        reversed = j['reversed'] == true,
        freeze = j['freeze'] == true;
  final Map<String, dynamic> audio;
  final double volume;
  final bool reversed, freeze;
  double get fadeIn => (audio['fade_in'] as num?)?.toDouble() ?? 0;
  double get fadeOut => (audio['fade_out'] as num?)?.toDouble() ?? 0;
  bool get detached => audio['detached'] == true;
  Map<String, dynamic>? get eq => (audio['eq'] as Map?)?.cast<String, dynamic>();
  Map<String, dynamic>? get denoise => (audio['denoise'] as Map?)?.cast<String, dynamic>();
}
