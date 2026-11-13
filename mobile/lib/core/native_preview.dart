// Native preview: the core renders each preview frame straight into a Flutter
// texture from its own thread (Android: Surface via vs_preview_set_sink). Dart
// only shows Texture(textureId); no frame is copied through FFI.
import 'dart:io';

import 'package:flutter/services.dart';

import 'vstudio.dart';

class NativePreview {
  static const _channel = MethodChannel('vstudio/preview');

  static bool get supported => Platform.isAndroid;

  /// Returns the texture id, or null when this platform falls back to frame polling.
  static Future<int?> create(VStudio vs, int width, int height) async {
    if (!supported) return null;
    return _channel.invokeMethod<int>('create', {
      'session': vs.nativeHandle,
      'width': width,
      'height': height,
    });
  }

  static Future<void> dispose() async {
    if (supported) await _channel.invokeMethod<void>('dispose');
  }
}
