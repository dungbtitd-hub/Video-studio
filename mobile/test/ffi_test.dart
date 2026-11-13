// End-to-end check of the FFI contract against a real libvstudio build.
// Runs when VSTUDIO_LIB points at the library (CI builds it first), else skips:
//   VSTUDIO_LIB=../build/capi/libvstudio.so flutter test
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/vstudio.dart';

void main() {
  final lib = Platform.environment['VSTUDIO_LIB'];
  final skip = (lib == null || lib.isEmpty) ? 'VSTUDIO_LIB not set' : null;
  final media = Platform.environment['VSTUDIO_TEST_MEDIA'];

  test('session, edits, undo through dart:ffi', () {
    final vs = VStudio.create('ffi', width: 640, height: 360);
    addTearDown(vs.dispose);
    expect(vs.version, startsWith('0.1'));
    final track = vs.addTrack(audio: false, name: 'V1');
    expect(vs.project().tracks.single.id, track);
    expect(vs.undo(), isTrue);
    expect(vs.project().tracks, isEmpty);
    expect(vs.redo(), isTrue);
    expect(() => vs.removeClip('missing'), throwsA(isA<VStudioException>()));
    expect(vs.modulesJson(), contains('builtin.color_adjust'));
  }, skip: skip);

  test('import, split and preview a real file', () async {
    final vs = VStudio.create('ffi', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    final asset = vs.importMedia(media!);
    vs.appendClip(track, asset);
    expect(vs.splitAt(1.0), 1);
    expect(vs.project().tracks.single.clips.length, 2);
    vs.setPreviewSize(320, 180);
    vs.seek(1.5);
    PreviewFrame? f;
    for (var i = 0; i < 200 && f == null; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
      f = vs.takeFrame();
    }
    expect(f, isNotNull);
    expect(f!.width, 320);
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));

  test('thumbnails, waveform and relink of a real file', () async {
    final vs = VStudio.create('ffi-lib', width: 640, height: 360);
    addTearDown(vs.dispose);
    final asset = vs.importMedia(media!);
    expect(vs.missingAssets(), isEmpty);
    final serial = vs.librarySerial;
    vs.requestThumbnails(asset, count: 4, height: 36);
    vs.requestWaveform(asset, bucketsPerSecond: 50);
    ThumbStrip? t;
    WaveformData? w;
    for (var i = 0; i < 1000 && (t == null || w == null); i++) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
      if (vs.librarySerial == serial) continue;
      t ??= vs.thumbnails(asset);
      w ??= vs.waveform(asset);
    }
    expect(t, isNotNull);
    expect(t!.count, 4);
    expect(t.height, 36);
    expect(t.rgba.length, 4 * t.width * 36 * 4);
    expect(w, isNotNull);
    expect(w!.buckets, greaterThan(0));
    // Relink to the same file through another path form is accepted and undoable.
    vs.relink(asset, File(media).absolute.path);
    expect(vs.undo(), isTrue);
    expect(() => vs.relink('nope', media), throwsA(isA<VStudioException>()));
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));
}
