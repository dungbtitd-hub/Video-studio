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

  test('tracks: flags, rename, reorder, remove, snap', () {
    final vs = VStudio.create('ffi-tracks', width: 640, height: 360);
    addTearDown(vs.dispose);
    final v1 = vs.addTrack(audio: false, name: 'V1');
    final v2 = vs.addTrack(audio: false, name: 'V2');
    vs.setTrackFlags(v1, enabled: false, locked: true, muted: false);
    final t = vs.project().tracks.firstWhere((x) => x.id == v1);
    expect((t.enabled, t.locked), (false, true));
    vs.renameTrack(v2, 'Top');
    expect(vs.project().tracks.first.name, 'Top'); // top video layer shown first
    vs.moveTrack(v2, 0);
    expect(vs.project().tracks.first.id, v1);
    expect(vs.undo(), isTrue);
    expect(vs.project().tracks.first.id, v2);
    vs.removeTrack(v2);
    expect(vs.project().tracks.single.id, v1);
    expect(() => vs.removeTrack('nope'), throwsA(isA<VStudioException>()));
    // No clips: nothing to snap to except 0 and the playhead.
    expect(() => vs.snapClipMove('nope', 1, 0.1), returnsNormally);
  }, skip: skip);

  test('import, split and preview a real file', () async {
    final vs = VStudio.create('ffi', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    final asset = vs.importMedia(media!);
    vs.appendClip(track, asset);
    expect(vs.splitAt(1.0), 1);
    expect(vs.project().tracks.single.clips.length, 2);
    // Right half dragged 0.05 s past the playhead at 3.0 snaps back onto it.
    final right = vs.project().tracks.single.clips[1];
    expect(vs.snapClipMove(right.id, 3.05, 0.1, playhead: 3.0), closeTo(3.0, 1e-9));
    expect(vs.snapClipMove(right.id, 3.5, 0.1, playhead: 3.0), isNull);
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
