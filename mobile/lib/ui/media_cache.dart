// Thumbnails and waveforms for the timeline, as the UI needs them.
// The core computes them on a background worker; this class only asks for
// them, polls one integer per frame (librarySerial) and turns finished strips
// into GPU images off the UI thread (decodeImageFromPixels).
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';

import '../core/project_model.dart';
import '../core/vstudio.dart';

class ThumbImage {
  ThumbImage(this.image, this.width, this.height, this.count);
  final ui.Image image; // width x (height * count), frame i at y = i * height
  final int width;
  final int height;
  final int count;
}

class MediaCache extends ChangeNotifier {
  final Map<String, ThumbImage> _thumbs = {};
  final Map<String, WaveformData> _waves = {};
  final Set<String> _requested = {};
  final Set<String> _decoding = {};
  Set<String> missing = {};
  int _serial = -1;

  ThumbImage? thumbs(String assetId) => _thumbs[assetId];
  WaveformData? waveform(String assetId) => _waves[assetId];

  /// Call when the project's asset list may have changed (cheap when it did not).
  void sync(VStudio vs, ProjectView project) {
    final ids = project.assets.map((a) => a.id).toSet();
    if (ids.every(_requested.contains) && _requested.every(ids.contains)) return;
    missing = vs.missingAssets().toSet();
    for (final a in project.assets) {
      if (_requested.contains(a.id) || missing.contains(a.id)) continue;
      _requested.add(a.id);
      if (a.type != 'audio') vs.requestThumbnails(a.id);
      vs.requestWaveform(a.id);
    }
    _requested.removeWhere((id) => !ids.contains(id));
    notifyListeners();
  }

  /// After a relink: forget what was built from the old file.
  void invalidate(String assetId) {
    _requested.remove(assetId);
    _thumbs.remove(assetId)?.image.dispose();
    _waves.remove(assetId);
  }

  /// Once per frame: one FFI call unless something new is ready.
  void poll(VStudio vs) {
    final s = vs.librarySerial;
    if (s == _serial) return;
    _serial = s;
    var changed = false;
    for (final id in _requested) {
      if (!_waves.containsKey(id)) {
        final w = vs.waveform(id);
        if (w != null) {
          _waves[id] = w;
          changed = true;
        }
      }
      if (!_thumbs.containsKey(id) && !_decoding.contains(id)) {
        final t = vs.thumbnails(id);
        if (t == null) continue;
        _decoding.add(id);
        ui.decodeImageFromPixels(t.rgba, t.width, t.height * t.count, ui.PixelFormat.rgba8888, (img) {
          _decoding.remove(id);
          _thumbs[id]?.image.dispose();
          _thumbs[id] = ThumbImage(img, t.width, t.height, t.count);
          notifyListeners();
        });
      }
    }
    if (changed) notifyListeners();
  }

  @override
  void dispose() {
    for (final t in _thumbs.values) {
      t.image.dispose();
    }
    super.dispose();
  }
}
