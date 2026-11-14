// End-to-end check of the FFI contract against a real libvstudio build.
// Runs when VSTUDIO_LIB points at the library (CI builds it first), else skips:
//   VSTUDIO_LIB=../build/capi/libvstudio.so flutter test
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/transfer.dart';
import 'package:video_studio/core/vstudio.dart';
import 'package:video_studio/ui/cover_panel.dart';
import 'package:video_studio/ui/grade_panel.dart';
import 'package:video_studio/ui/looks_sheet.dart';
import 'package:video_studio/ui/recent_strip.dart';
import 'package:video_studio/ui/transition_sheet.dart';
import 'package:video_studio/ui/video_fx_sheet.dart';
import 'package:video_studio/core/project_model.dart';

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
    // Export presets: sized from the 640x360 canvas, unknown ids refused.
    final presets = vs.exportPresets();
    expect(presets.map((p) => p.id), containsAll(['social', 'fhd1080', 'uhd2160']));
    final fhd = presets.firstWhere((p) => p.id == 'fhd1080');
    expect((fhd.width, fhd.height), (1920, 1080));
    expect(() => vs.startExportPreset('/tmp/x.mp4', 'nope'), throwsA(isA<VStudioException>()));
    final formats = vs.exportFormats();
    expect(formats.map((f) => f.id), ['h264', 'hevc', 'hevc10']);
    final h264 = vs.exportPresets('h264').firstWhere((p) => p.id == 'fhd1080');
    final ten = vs.exportPresets('hevc10').firstWhere((p) => p.id == 'fhd1080');
    expect(ten.videoBitrate, lessThan(h264.videoBitrate));
    expect(() => vs.startExportPresetFormat('/tmp/x.mp4', 'fhd1080', 'nope'), throwsA(isA<VStudioException>()));
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
    // One-tap looks: both halves at once, one undo step, visible in the project view.
    final ids = vs.project().tracks.single.clips.map((c) => c.id).toList();
    expect(vs.looks().map((l) => l.id), contains('teal_orange'));
    vs.setClipLook(ids, 'teal_orange', strength: 0.6);
    expect(vs.project().tracks.single.clips.map((c) => (c.look, c.lookStrength)), everyElement(('teal_orange', 0.6)));
    vs.setClipLook([ids.first], 'bw_contrast');
    expect(vs.project().tracks.single.clips.first.look, 'bw_contrast');
    expect(vs.undo(), isTrue);
    expect(vs.project().tracks.single.clips.first.look, 'teal_orange');
    vs.setClipLook(ids, '');
    expect(vs.project().tracks.single.clips.every((c) => c.look == null), isTrue);
    final grey = vs.lookPreview('bw_soft', Uint8List.fromList([200, 40, 10, 255]), 1, 1)!;
    expect(grey[0], grey[1]);
    expect(grey[1], grey[2]);
    expect(vs.lookPreview('nope', Uint8List(4), 1, 1), isNull);
    // Right half dragged 0.05 s past the playhead at 3.0 snaps back onto it.
    final right = vs.project().tracks.single.clips[1];
    expect(vs.snapClipMove(right.id, 3.05, 0.1, playhead: 3.0), closeTo(3.0, 1e-9));
    expect(vs.snapClipMove(right.id, 3.5, 0.1, playhead: 3.0), isNull);
    // A real export through a preset finishes and writes a file.
    final out = '${Directory.systemTemp.path}/vs_ffi_export_${DateTime.now().microsecondsSinceEpoch}.mp4';
    addTearDown(() => File(out).existsSync() ? File(out).deleteSync() : null);
    vs.startExportPreset(out, 'hd720');
    var (state, _) = vs.exportStatus();
    for (var i = 0; i < 3000 && state == ExportState.running; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
      (state, _) = vs.exportStatus();
    }
    expect(state, ExportState.done, reason: vs.lastError);
    expect(File(out).lengthSync(), greaterThan(1000));
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

  test('ripple delete, save and reopen with media next to the project', () async {
    final tmp = Directory.systemTemp.createTempSync('vs_ffi');
    addTearDown(() => tmp.deleteSync(recursive: true));
    // Same layout as ProjectStore: media/ next to the packages.
    final root = '${tmp.path}/a';
    final mediaCopy = File('$root/media/clip.mp4')..createSync(recursive: true);
    File(media!).copySync(mediaCopy.path);
    final pkg = '$root/Film.vstudio';
    final vs = VStudio.create('Film', width: 640, height: 360);
    vs.save(pkg);
    final track = vs.addTrack(audio: false, name: 'V1');
    final asset = vs.importMedia(mediaCopy.path);
    vs.appendClip(track, asset);
    vs.appendClip(track, asset);
    final clips = vs.project().tracks.single.clips;
    final len = clips.first.duration;
    vs.rippleDelete(clips.first.id);
    expect(vs.project().tracks.single.clips.single.start, closeTo(0, 1e-9)); // second clip pulled to 0
    expect(vs.undo(), isTrue);
    expect(vs.project().tracks.single.clips[1].start, closeTo(len, 1e-9));
    expect(vs.dirty, isTrue);
    vs.save(pkg);
    expect(vs.dirty, isFalse);
    vs.dispose();
    // Moving the whole folder (like an iOS container move) keeps the media found.
    Directory(root).renameSync('${tmp.path}/b');
    final again = VStudio.open('${tmp.path}/b/Film.vstudio');
    addTearDown(again.dispose);
    expect(again.project().tracks.single.clips.length, 2);
    expect(again.missingAssets(), isEmpty);
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));

  test('a desktop project moves to the phone as one .vstudiopkg file', () async {
    final tmp = Directory.systemTemp.createTempSync('vs_xfer');
    addTearDown(() => tmp.deleteSync(recursive: true));
    final desk = '${tmp.path}/desktop';
    final clip = File('$desk/media/clip.mp4')..createSync(recursive: true);
    File(media!).copySync(clip.path);
    final vs = VStudio.create('Trip', width: 640, height: 360);
    final track = vs.addTrack(audio: false, name: 'V1');
    vs.appendClip(track, vs.importMedia(clip.path));
    vs.save('$desk/Trip.vstudio');
    vs.dispose();

    final progress = <double>[];
    final packed =
        await packSavedProject('$desk/Trip.vstudio', '${tmp.path}/Trip.vstudiopkg', onProgress: progress.add);
    expect(packed.ok, isTrue, reason: packed.error);
    expect(packed.missing, isEmpty);
    expect(progress.last, 1.0);

    // The phone's store root: projects and media/ side by side.
    final phone = '${tmp.path}/phone';
    final opened = await unpackPackage('${tmp.path}/Trip.vstudiopkg', phone);
    expect(opened.ok, isTrue, reason: opened.error);
    expect(opened.packageDir, endsWith('Trip.vstudio'));
    final again = VStudio.open(opened.packageDir);
    addTearDown(again.dispose);
    expect(again.project().tracks.single.clips.length, 1);
    expect(again.missingAssets(), isEmpty);
    expect(File('$phone/media/clip.mp4').lengthSync(), clip.lengthSync());

    // Cancelling stops at the first progress report and leaves nothing behind.
    final cancel = TransferCancel()..cancel();
    addTearDown(cancel.dispose);
    final stopped = await unpackPackage('${tmp.path}/Trip.vstudiopkg', '${tmp.path}/other', cancel: cancel);
    expect(stopped.cancelled, isTrue);
    expect(File('${tmp.path}/other/media/clip.mp4').existsSync(), isFalse);

    final bad = await unpackPackage('${tmp.path}/nope.vstudiopkg', phone);
    expect(bad.ok, isFalse);
    expect(bad.error, isNotEmpty);
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

  test('Log input and LUT go through the core and show up on the clip', () {
    final tmp = Directory.systemTemp.createTempSync('vs_log');
    addTearDown(() => tmp.deleteSync(recursive: true));
    final vs = VStudio.create('Log', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    final asset = vs.importMedia(media!);
    vs.appendClip(track, asset);
    final clip = vs.project().tracks.single.clips.single.id;
    expect(
        vs.logFormats().map((f) => f.id), containsAll(['slog3', 'vlog', 'clog3', 'nlog', 'applelog', 'dlog', 'hlg']));
    expect(vs.suggestedLogFormat(asset), ''); // an ordinary Rec.709 test clip
    vs.setClipColorSpace([clip], 'applelog');
    expect(vs.project().tracks.single.clips.single.colorSpace, 'applelog');
    expect(() => vs.setClipLut([clip], '${tmp.path}/missing.cube'), throwsA(isA<VStudioException>()));
    final cube = File('${tmp.path}/warm.cube')
      ..writeAsStringSync('LUT_3D_SIZE 2\n${List.filled(8, '0.6 0.5 0.4').join('\n')}\n');
    vs.setClipLut([clip], cube.path);
    expect(vs.project().tracks.single.clips.single.lut, cube.path);
    expect(vs.undo(), isTrue);
    expect(vs.project().tracks.single.clips.single.lut, isNull);
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));

  test('keyframes: zoom between two points, audio keys separate, undo', () {
    final vs = VStudio.create('Keys', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    vs.appendClip(track, vs.importMedia(media!));
    final clip = vs.project().tracks.single.clips.single.id;
    // Start key at 0.2 s, then zoom 2x on the top-left quarter at 1.0 s: auto-keyed.
    expect(vs.toggleKeyframe(clip, KeyGroup.picture, 0.2), isTrue);
    vs.zoomToRegion(clip, 1.0, 0, 0, 0.5, 0.5);
    var st = vs.clipStateAt(clip, 1.0);
    expect(st.pictureHere, isTrue);
    expect(st.scale, closeTo(2, 1e-6));
    expect(st.x, closeTo(320, 1e-6)); // the quarter's centre moved to the frame centre
    expect(st.y, closeTo(180, 1e-6));
    expect(vs.clipStateAt(clip, 0.1).scale, 1);
    final mid = vs.clipStateAt(clip, 0.6).scale;
    expect(mid, inInclusiveRange(1.2, 1.8));
    // Audio: its own keys; picture untouched.
    vs.addKeyframe(clip, KeyGroup.audio, 0.4);
    vs.setPropertyAt(clip, 1.2, {'volume': 0.25});
    st = vs.clipStateAt(clip, 1.2);
    expect(st.volume, closeTo(0.25, 1e-6));
    expect(st.audioHere && !st.pictureHere, isTrue);
    final c = vs.project().tracks.single.clips.single;
    expect(c.pictureKeys, [closeTo(0.2, 1e-3), closeTo(1.0, 1e-3)]);
    expect(c.audioKeys, [closeTo(0.4, 1e-3), closeTo(1.2, 1e-3)]);
    // ◆ again removes; undo brings it back.
    expect(vs.toggleKeyframe(clip, KeyGroup.picture, 1.0), isFalse);
    expect(vs.project().tracks.single.clips.single.pictureKeys.length, 1);
    expect(vs.undo(), isTrue);
    expect(vs.project().tracks.single.clips.single.pictureKeys.length, 2);
    vs.clearKeyframes(clip, KeyGroup.audio);
    expect(vs.project().tracks.single.clips.single.audioKeys, isEmpty);
    expect(vs.project().tracks.single.clips.single.pictureKeys.length, 2);
    expect(() => vs.setPropertyAt(clip, 1.0, {'nope': 1}), throwsA(isA<VStudioException>()));
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));

  testWidgets('look strip: previews render, one tap applies, no overflow', (tester) async {
    final vs = VStudio.create('ffi-looks', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    vs.appendClip(track, vs.importMedia(media!));
    final clip = vs.project().tracks.single.clips.single;
    final applied = <String>[];
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          child: SizedBox(
            width: 360,
            child: LooksSheet(
              vs: vs,
              clip: clip,
              onApply: (look, strength) => applied.add('$look ${strength.toStringAsFixed(1)}'),
              onApplyAll: (look, strength) => applied.add('all $look'),
              onInput: (input) => applied.add('input $input'),
              onPickLut: () async => null,
            ),
          ),
        ),
      ),
    ));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 300))); // image decodes
    await tester.pump();
    expect(find.byType(RawImage), findsWidgets);
    await tester.tap(find.text('Rực rỡ'));
    await tester.pump();
    await tester.tap(find.text('Áp dụng cho tất cả'));
    await tester.pump();
    expect(applied, ['vivid 1.0', 'all vivid']);
    // Log input picker: lists the camera formats; choosing one reports it.
    await tester.tap(find.byKey(const Key('log-input')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Sony S-Log3 / S-Gamut3.Cine').last);
    await tester.pumpAndSettle();
    expect(applied.last, 'input slog3');
    expect(tester.takeException(), isNull); // no overflow at phone width
  }, skip: (skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null)) != null);

  test('grade: commit, read back, live preview, helpers', () {
    final vs = VStudio.create('ffi-grade', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    vs.appendClip(track, vs.importMedia(media!));
    final clip = vs.project().tracks.single.clips.single.id;
    expect(vs.hslBands().map((b) => b.id), containsAll(['red', 'orange', 'blue', 'magenta']));
    vs.setClipGrade([
      clip
    ], {
      'saturation': 1.3,
      'lift': [0.0, 0.0, 0.1, 0.05]
    });
    expect(vs.clipGrade(clip)['saturation'], 1.3);
    expect(vs.project().tracks.single.clips.single.grade?['saturation'], 1.3);
    vs.setEffectPreview(clip, 'builtin.color_grade', {'saturation': 0.0}); // no undo step
    expect(vs.clipGrade(clip)['saturation'], 1.3);
    vs.setEffectPreview('', '', null);
    expect(vs.undo(), isTrue);
    expect(vs.clipGrade(clip), isEmpty);
    expect(() => vs.setClipGrade(['nope'], {'saturation': 2}), throwsA(isA<VStudioException>()));
    final b = vs.wheelToBalance(240, 0.5);
    final (angle, dist) = vs.balanceToWheel(b);
    expect(angle, closeTo(240, 1e-6));
    expect(dist, closeTo(0.5, 1e-6));
    final c = vs.gradeCurve([
      [0.5, 0.7]
    ], 11);
    expect(c[5], closeTo(0.7, 1e-3));
    expect(vs.previewScope(9, 64, 32), isNull); // bad kind
    final g = vs.gradePreview({'saturation': 0.0}, Uint8List.fromList([200, 40, 10, 255]), 1, 1)!;
    expect(g[0], g[1]);
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));

  testWidgets('grade panel: live while dragging, one commit on release, fits a phone', (tester) async {
    final vs = VStudio.create('ffi-grade-ui', width: 640, height: 360);
    addTearDown(vs.dispose);
    final live = <Map<String, dynamic>?>[];
    final commits = <Map<String, dynamic>>[];
    final key = GlobalKey<GradePanelState>();
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          child: SizedBox(
            width: 360,
            child: GradePanel(
              key: key,
              vs: vs,
              clipId: 'c1',
              params: const {},
              onLive: live.add,
              onCommit: commits.add,
              onApplyAll: (_) {},
              onDone: () {},
            ),
          ),
        ),
      ),
    ));
    // Drag the wheel puck towards blue: previews while moving, commits once.
    final wheel = tester.getRect(find.byType(ColorWheelView));
    final g = await tester.startGesture(wheel.center);
    for (var i = 0; i < 4; i++) {
      await g.moveBy(const Offset(-6, 8));
      await tester.pump();
    }
    expect(live, isNotEmpty);
    expect(commits, isEmpty);
    await g.up();
    await tester.pump();
    expect(commits, hasLength(1));
    expect(commits.single['lift'], isA<List>());
    // Curves page: a tap adds a point, committed at once.
    await tester.tap(find.text('Đường cong'));
    await tester.pump();
    final curve = tester.getRect(find.byType(CurveView));
    await tester.tapAt(curve.center + const Offset(0, -20));
    await tester.pump();
    expect((commits.last['curves'] as Map)['master'], hasLength(3));
    // Eyedropper result fills the qualifier and commits.
    key.currentState!.pickColor(const Color(0xFF3C78E6));
    await tester.pump();
    final q = commits.last['qualifier'] as Map;
    expect((q['hue'] as List).first, closeTo(219, 3));
    expect(find.text('Chọn vùng màu'), findsOneWidget);
    // Reset: an empty grade (removes the effect).
    await tester.ensureVisible(find.text('Đặt lại'));
    await tester.pump();
    await tester.tap(find.text('Đặt lại'));
    await tester.pump();
    expect(commits.last, isEmpty);
    await tester.pump(const Duration(seconds: 1)); // let double-tap / long-press timers expire
    expect(tester.takeException(), isNull); // no overflow at phone width
  }, skip: skip != null);

  test('covers: add, region from the preview, keyed follow, tracking in the background', () async {
    final vs = VStudio.create('Covers', width: 640, height: 360);
    addTearDown(vs.dispose);
    expect(vs.coverStyles().length, greaterThanOrEqualTo(36));
    expect(vs.coverShapes().map((e) => e.$1), containsAll(['rect', 'ellipse', 'heart']));
    final track = vs.addTrack(audio: false, name: 'V1');
    vs.appendClip(track, vs.importMedia(media!));
    final clip = vs.project().tracks.single.clips.single.id;
    final cover = vs.addCover(clip, {'style': 'mosaic', 'shape': 'ellipse'});
    expect(vs.covers(clip).single.$1, cover);
    var st = vs.coverStateAt(clip, cover, 0.5);
    expect((st.style, st.shape, st.following), ('mosaic', 'ellipse', false));
    // A box drawn on the preview (canvas 0..1) reads back the same on the canvas.
    vs.setCoverCanvasRect(clip, cover, 0.5, 0.1, 0.2, 0.3, 0.25);
    final r = vs.coverCanvasRect(clip, cover, 0.5)!;
    expect([
      r.$1,
      r.$2,
      r.$3,
      r.$4
    ], [
      for (final v in [0.1, 0.2, 0.3, 0.25]) closeTo(v, 1e-6)
    ]);
    // Static cover: the region holds everywhere. Following: keyed, moves between keys.
    expect(vs.coverCanvasRect(clip, cover, 1.5)!.$1, closeTo(0.1, 1e-6));
    vs.setCoverFollowing(clip, cover, 0.5, true);
    expect(vs.coverStateAt(clip, cover, 0.5).following, isTrue);
    vs.setCoverCanvasRect(clip, cover, 1.5, 0.5, 0.2, 0.3, 0.25);
    expect(vs.coverCanvasRect(clip, cover, 1.0)!.$1, inExclusiveRange(0.1, 0.5));
    vs.updateCover(clip, cover, {'style': 'blur', 'invert': true});
    vs.setCoverAt(clip, cover, 1.0, {'amount': 0.9});
    st = vs.coverStateAt(clip, cover, 1.0);
    expect((st.style, st.invert), ('blur', true));
    expect(st.amount, closeTo(0.9, 1e-6));
    expect(vs.undo(), isTrue);
    expect(vs.coverStateAt(clip, cover, 1.0).amount, isNot(closeTo(0.9, 1e-6)));
    // Thumbnails: the style changes the middle of the picture.
    final px = Uint8List(32 * 20 * 4)..fillRange(0, 32 * 20 * 4, 128);
    for (var i = 0; i < px.length; i += 37) {
      px[i] = 255;
    }
    expect(vs.coverPreview('black', 'rect', px, 32, 20)![(10 * 32 + 16) * 4], lessThan(10));
    expect(vs.coverPreview('nope', 'rect', px, 32, 20), isNull);
    // Tracking runs off the UI thread; polling applies the keys once.
    vs.setCoverFollowing(clip, cover, 0.5, false);
    final before = vs.revision;
    vs.startTrackCover(clip, cover, 0.2);
    expect(() => vs.startTrackCover(clip, cover, 0.2), throwsA(isA<VStudioException>()));
    var (state, _) = vs.trackStatus();
    for (var i = 0; i < 600 && state == TrackState.running; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
      (state, _) = vs.trackStatus();
    }
    expect(state, anyOf(TrackState.done, TrackState.lost), reason: vs.lastError);
    expect(vs.revision, greaterThan(before));
    expect(vs.coverStateAt(clip, cover, 1.0).following, isTrue);
    vs.removeCover(clip, cover);
    expect(vs.covers(clip), isEmpty);
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));

  testWidgets('cover panel: style thumbnails, tap to add or restyle, no overflow', (tester) async {
    final vs = VStudio.create('ffi-covers', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    vs.appendClip(track, vs.importMedia(media!));
    final clip = vs.project().tracks.single.clips.single.id;
    final events = <String>[];
    Widget panel(String? active) => MaterialApp(
          home: Scaffold(
            body: Align(
              alignment: Alignment.bottomCenter,
              child: SizedBox(
                width: 360,
                child: CoverPanel(
                  vs: vs,
                  covers: active == null ? const [] : [active],
                  active: active,
                  state: active == null ? null : vs.coverStateAt(clip, active, 0.5),
                  tracking: false,
                  trackProgress: 0,
                  onSelect: (id) => events.add('select'),
                  onAdd: (style, shape) => events.add('add $style $shape'),
                  onPatch: (p) => events.add('patch ${p.entries.single.key}=${p.entries.single.value}'),
                  onAmount: (v) => events.add('amount'),
                  onFollow: (on) => events.add('follow $on'),
                  onTrack: () => events.add('track'),
                  onRemove: () => events.add('remove'),
                  onDone: () => events.add('done'),
                ),
              ),
            ),
          ),
        );
    await tester.pumpWidget(panel(null));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 300))); // image decodes
    await tester.pump();
    expect(find.byType(RawImage), findsWidgets);
    await tester.tap(find.byKey(const Key('cover-style-frosted')));
    await tester.pump();
    expect(events.last, 'add frosted rect');
    expect(tester.widget<FilledButton>(find.byKey(const Key('cover-track'))).onPressed, isNull);
    final cover = vs.addCover(clip, {'style': 'blur'});
    await tester.pumpWidget(panel(cover));
    await tester.pump();
    await tester.tap(find.byKey(const Key('cover-style-frosted')));
    await tester.tap(find.text('Elip'));
    await tester.tap(find.byKey(const Key('cover-track')));
    await tester.tap(find.byKey(const Key('cover-follow')));
    await tester.tap(find.byKey(const Key('cover-done')));
    await tester.pump();
    expect(events.skip(1), ['patch style=frosted', 'patch shape=ellipse', 'track', 'follow true', 'done']);
    expect(tester.takeException(), isNull); // no overflow at phone width
  }, skip: (skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null)) != null);

  test('transitions: places on the track, set at a cut, everywhere, previews, undo', () {
    final vs = VStudio.create('Transitions', width: 640, height: 360);
    addTearDown(vs.dispose);
    expect(vs.transitions().length, greaterThanOrEqualTo(30));
    final track = vs.addTrack(audio: false, name: 'V1');
    final asset = vs.importMedia(media!);
    vs.appendClip(track, asset);
    vs.appendClip(track, asset);
    final clips = vs.project().tracks.single.clips;
    var cuts = vs.cutPoints()[track]!;
    expect(cuts.map((c) => (c.from, c.to)), [(null, clips[0].id), (clips[0].id, clips[1].id), (clips[1].id, null)]);
    expect(cuts.every((c) => c.type == null), isTrue);
    // The end of the first clip touches the second: that is the cut, stored on the second clip's start.
    vs.setTransition(clips[0].id, 1, 'dip_black', seconds: 0.8);
    cuts = vs.cutPoints()[track]!;
    expect(cuts[1].type, 'dip_black');
    expect(cuts[1].duration, closeTo(0.8, 1e-6));
    expect(cuts[1].clipId, clips[1].id);
    vs.setTransition(clips[1].id, 1, 'fade', seconds: 9); // free end: at most half the clip
    expect(vs.cutPoints()[track]![2].duration, closeTo(clips[1].duration / 2, 1e-3));
    expect(vs.setTransitionEverywhere('push_left'), 1);
    expect(vs.cutPoints()[track]![1].type, 'push_left');
    expect(vs.undo(), isTrue);
    expect(vs.cutPoints()[track]![1].type, 'dip_black');
    expect(() => vs.setTransition(clips[0].id, 0, 'nope'), throwsA(isA<VStudioException>()));
    vs.setTransition(clips[1].id, 0, null);
    expect(vs.cutPoints()[track]![1].type, isNull);
    // Thumbnails: the start is the first picture, the end the second.
    final a = Uint8List(8 * 8 * 4)..fillRange(0, 256, 255);
    final b = Uint8List(8 * 8 * 4);
    for (var i = 3; i < b.length; i += 4) {
      b[i] = 255;
    }
    expect(vs.transitionPreview('wipe_left', a, b, 8, 8, 0)![0], 255);
    expect(vs.transitionPreview('wipe_left', a, b, 8, 8, 1)![0], 0);
    expect(vs.transitionPreview('nope', a, b, 8, 8, 0.5), isNull);
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));

  testWidgets('transition sheet: thumbnails, tap applies and plays, length, apply to all, no overflow', (tester) async {
    final vs = VStudio.create('ffi-transitions', width: 640, height: 360);
    addTearDown(vs.dispose);
    final events = <String>[];
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          child: SizedBox(
            width: 360,
            child: TransitionSheet(
              vs: vs,
              cut: const CutPoint(trackId: 't', at: 2, from: 'a', to: 'b'),
              onApply: (type, s) => events.add('apply $type ${s.toStringAsFixed(1)}'),
              onApplyAll: (type, s) => events.add('all $type'),
              onRemove: () => events.add('remove'),
              onDuration: (s) => events.add('length ${s.toStringAsFixed(1)}'),
            ),
          ),
        ),
      ),
    ));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 300))); // image decodes
    await tester.pump();
    expect(find.byType(RawImage), findsWidgets);
    expect(tester.widget<OutlinedButton>(find.byKey(const Key('transition-all'))).onPressed, isNull);
    await tester.tap(find.byKey(const Key('transition-dip_black')));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 300)));
    await tester.pump(const Duration(milliseconds: 120));
    expect(find.byKey(const Key('transition-playing')), findsOneWidget);
    await tester.drag(find.byKey(const Key('transition-duration')), const Offset(60, 0));
    await tester.pump();
    await tester.tap(find.byKey(const Key('transition-all')));
    await tester.tap(find.byKey(const Key('transition-remove')));
    await tester.pump();
    expect(events.first, 'apply dip_black 0.5');
    expect(events[1], startsWith('length '));
    expect(events.sublist(2), ['all dip_black', 'remove']);
    expect(tester.takeException(), isNull); // no overflow at phone width
    await tester.pumpWidget(const SizedBox()); // stops the preview timer
  }, skip: (skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null)) != null);

  test('video effects: one per group on a clip, shown on the clip, thumbnails move, undo', () {
    final vs = VStudio.create('ffi-vfx', width: 640, height: 360);
    addTearDown(vs.dispose);
    final fx = vs.videoFx();
    expect(fx.map((f) => f.group).toSet(), {'zoom', 'rotate', 'reflect'});
    expect(fx.firstWhere((f) => f.id == 'reflect_water').usesLine, isTrue);
    final track = vs.addTrack(audio: false, name: 'V1');
    vs.appendClip(track, vs.importMedia(media!));
    final clip = vs.project().tracks.single.clips.single.id;
    vs.setClipVideoFx([clip], 'zoom', 'zoom_in');
    vs.setClipVideoFx([clip], 'reflect', 'reflect_water', amount: 1.5, line: 0.5);
    vs.setClipVideoFx([clip], 'zoom', 'zoom_shake'); // replaces the zoom, keeps the reflection
    var c = vs.project().tracks.single.clips.single;
    expect(c.videoFx.keys.toSet(), {'zoom', 'reflect'});
    expect(c.videoFx['zoom']!['fx'], 'zoom_shake');
    expect(c.videoFx['reflect']!['line'], closeTo(0.5, 1e-9));
    expect(() => vs.setClipVideoFx([clip], 'zoom', 'reflect_water'), throwsA(isA<VStudioException>()));
    vs.setClipVideoFx([clip], 'reflect', '');
    expect(vs.project().tracks.single.clips.single.videoFx.keys, ['zoom']);
    expect(vs.undo(), isTrue);
    c = vs.project().tracks.single.clips.single;
    expect(c.videoFx.containsKey('reflect'), isTrue);
    // Thumbnails: the spin turns over time; a picture can stand in for the landscape.
    final a = vs.videoFxIcon('rot_spin', 0.3, 32, 18)!, b = vs.videoFxIcon('rot_spin', 1.3, 32, 18)!;
    expect(a.length, 32 * 18 * 4);
    expect(a, isNot(equals(b)));
    final red = Uint8List(16 * 16 * 4);
    for (var i = 0; i < red.length; i += 4) {
      red[i] = 255;
      red[i + 3] = 255;
    }
    final own = vs.videoFxIcon('flip_h', 0, 8, 8, picture: red, pw: 16, ph: 16)!;
    expect((own[0], own[1], own[3]), (255, 0, 255));
    expect(vs.videoFxIcon('nope', 0, 8, 8), isNull);
  }, skip: skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null));

  testWidgets('video effect sheet: groups, tap applies and plays, sliders, remove, no overflow', (tester) async {
    final vs = VStudio.create('ffi-vfx-sheet', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    vs.appendClip(track, vs.importMedia(media!));
    final clip = vs.project().tracks.single.clips.single;
    final events = <String>[];
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          child: SizedBox(
            width: 360,
            child: VideoFxSheet(
              vs: vs,
              clip: clip,
              onSet: (g, fx, amount, speed, line) =>
                  events.add('$g $fx ${amount.toStringAsFixed(1)} ${line.toStringAsFixed(2)}'),
            ),
          ),
        ),
      ),
    ));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 300))); // image decodes
    await tester.pump();
    expect(find.byType(RawImage), findsWidgets);
    expect(find.byKey(const Key('vfx-line')), findsNothing); // only for reflections
    expect(tester.widget<TextButton>(find.byKey(const Key('vfx-remove'))).onPressed, isNull);
    await tester.tap(find.byKey(const Key('vfx-zoom_out')));
    await tester.pump();
    await tester.tap(find.byKey(const Key('vfx-group-reflect')));
    await tester.pump();
    expect(find.byKey(const Key('vfx-line')), findsOneWidget);
    await tester.tap(find.byKey(const Key('vfx-reflect_water')));
    await tester.pump();
    await tester.drag(find.byKey(const Key('vfx-amount')), const Offset(60, 0));
    await tester.pump();
    expect(find.textContaining('Đang dùng:'), findsOneWidget);
    await tester.tap(find.byKey(const Key('vfx-remove')));
    await tester.pump();
    expect(events.sublist(0, 2), ['zoom zoom_out 1.0 -1.00', 'reflect reflect_water 1.0 0.62']);
    expect(events[2], startsWith('reflect reflect_water 1.'));
    expect(events[2], isNot('reflect reflect_water 1.0 0.62'));
    expect(events.last, startsWith('reflect  '));
    expect(tester.takeException(), isNull); // no overflow at phone width
    await tester.pumpWidget(const SizedBox()); // stops the playing tile
  }, skip: (skip ?? (media == null ? 'VSTUDIO_TEST_MEDIA not set' : null)) != null);

  testWidgets('recent projects: saved ones slide up at the bottom, a tap opens one', (tester) async {
    final tmp = Directory.systemTemp.createTempSync('vs_recent');
    addTearDown(() => tmp.deleteSync(recursive: true));
    for (final name in ['Du lịch', 'Sinh nhật']) {
      final vs = VStudio.create(name, width: 640, height: 360);
      vs.save('${tmp.path}/$name.vstudio');
      vs.dispose();
    }
    var cards = VStudio.recentProjects(tmp.path);
    expect(cards.map((c) => c.name).toSet(), {'Du lịch', 'Sinh nhật'});
    expect(cards.every((c) => !c.exported && c.width == 640), isTrue);
    cards = VStudio.recentProjects(tmp.path, exclude: '${tmp.path}/Du lịch.vstudio');
    expect(cards.single.name, 'Sinh nhật');
    expect(VStudio.projectPoster('${tmp.path}/nothing.vstudio'), isNull);

    ProjectCard? opened;
    var shown = true;
    Widget strip() => MaterialApp(
        home: Scaffold(
            body: Align(
                alignment: Alignment.bottomCenter,
                child: RecentStrip(
                    cards: VStudio.recentProjects(tmp.path),
                    shown: shown,
                    onOpen: (c) => opened = c,
                    onClose: () => shown = false,
                    onAll: () {}))));
    await tester.pumpWidget(strip());
    await tester.pumpAndSettle();
    expect(find.text('Tiếp tục dự án chưa xong (2)'), findsOneWidget);
    await tester.tap(find.byKey(const Key('recent-Sinh nhật')));
    expect(opened?.name, 'Sinh nhật');
    await tester.tap(find.byKey(const Key('recent-close')));
    await tester.pumpWidget(strip());
    await tester.pumpAndSettle();
    final fade = tester.widget<AnimatedOpacity>(find.byType(AnimatedOpacity));
    expect(fade.opacity, 0); // slid away
    expect(agoText(DateTime(2026, 1, 1, 10), now: DateTime(2026, 1, 1, 12)), '2 giờ trước');
    expect(lengthText(83), '1:23');
  }, skip: skip != null);

  test('live preview of a gesture: nothing edited, no undo step', () {
    final vs = VStudio.create('ffi-live', width: 640, height: 360);
    addTearDown(vs.dispose);
    final clip = vs.addGeneratedClip(vs.defaultSticker('heart_red'), 0, 3);
    final before = vs.project().tracks.last.clips.single.content;
    vs.livePropertyAt(clip, 0, {'scale': 2});
    vs.liveClipContent(clip, {'size': 200});
    expect(vs.project().tracks.last.clips.single.content, before);
    expect(vs.clipStateAt(clip, 0).scale, 1);
    expect(() => vs.livePropertyAt(clip, 0, {'nope': 1}), throwsA(isA<VStudioException>()));
    vs.liveEnd();
    vs.setPropertyAt(clip, 0, {'scale': 2}); // the gesture's end: one undo step
    expect(vs.clipStateAt(clip, 0).scale, 2);
    expect(vs.undo(), isTrue);
    expect(vs.clipStateAt(clip, 0).scale, 1);
  }, skip: skip);
}
