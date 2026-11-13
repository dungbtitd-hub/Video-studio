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
  String get style => (json['style'] ?? 'blur') as String;
  String get shape => (json['shape'] ?? 'rect') as String;
  bool get invert => json['invert'] == true;
  bool get following => json['following'] == true;
}

enum TrackState { idle, running, done, lost, cancelled, failed }

class TransitionInfo {
  const TransitionInfo(this.id, this.name, this.category);
  final String id, name, category;
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
    return _catalog = c;
  }

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

  /// A copy of [rgba] with grade [params] applied (thumbnails).
  Uint8List? gradePreview(Map<String, dynamic> params, Uint8List rgba, int width, int height) => using((a) {
        final buf = a<Uint8>(rgba.length);
        buf.asTypedList(rgba.length).setAll(0, rgba);
        if (_b.gradePreview(jsonEncode(params).toNativeUtf8(allocator: a), buf, width, height) != 0) return null;
        return Uint8List.fromList(buf.asTypedList(rgba.length));
      });

  /// Shared presets, sized for this project (its aspect is kept).
  List<ExportPresetView> exportPresets() => (jsonDecode(_take(_b.exportPresetsJson(_s))) as List)
      .map((e) => ExportPresetView.fromJson(e as Map<String, dynamic>))
      .toList();
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
}
