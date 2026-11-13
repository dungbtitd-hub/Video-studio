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

  ProjectView project() => ProjectView.fromJson(_take(_b.projectJson(_s)));
  int get revision => _b.projectRevision(_s);
  String modulesJson() => _take(_b.modulesJson(_s));

  String importMedia(String path) => using((a) => _take(_b.importMedia(_s, path.toNativeUtf8(allocator: a))));
  String addTrack({required bool audio, required String name}) =>
      using((a) => _take(_b.addTrack(_s, audio ? 1 : 0, name.toNativeUtf8(allocator: a))));
  String appendClip(String trackId, String assetId) => using((a) =>
      _take(_b.appendClip(_s, trackId.toNativeUtf8(allocator: a), assetId.toNativeUtf8(allocator: a))));
  void moveClip(String clipId, String trackId, double start) => using((a) => _check(_b.moveClip(
      _s, clipId.toNativeUtf8(allocator: a), trackId.toNativeUtf8(allocator: a), secondsToTicks(start))));
  void trimClip(String clipId, {required bool start, required double at}) =>
      using((a) => _check(_b.trimClip(_s, clipId.toNativeUtf8(allocator: a), start ? 0 : 1, secondsToTicks(at))));
  int splitAt(double t) {
    final n = _b.splitAt(_s, secondsToTicks(t));
    if (n < 0) throw VStudioException(_b.lastError(_s).toDartString());
    return n;
  }

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
  void relink(String assetId, String path) => using((a) =>
      _check(_b.relinkAsset(_s, assetId.toNativeUtf8(allocator: a), path.toNativeUtf8(allocator: a))));
  String get lastError => _b.lastError(_s).toDartString();
}
