// Sound, clip tools, favourites and the tablet / foldable workspace.
// The FFI parts run when VSTUDIO_LIB (and VSTUDIO_TEST_MEDIA) are set, like ffi_test.dart.
import 'dart:io';
import 'dart:ui' show DisplayFeature, DisplayFeatureState, DisplayFeatureType, Rect, Size;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/favorites.dart';
import 'package:video_studio/core/vstudio.dart';
import 'package:video_studio/ui/audio_sheet.dart';
import 'package:video_studio/ui/clip_tools_sheet.dart';
import 'package:video_studio/ui/workspace_layout.dart';

void main() {
  final lib = Platform.environment['VSTUDIO_LIB'];
  final media = Platform.environment['VSTUDIO_TEST_MEDIA'];
  final skip = (lib == null || lib.isEmpty || media == null) ? 'VSTUDIO_LIB / VSTUDIO_TEST_MEDIA not set' : null;

  group('workspace layout', () {
    DisplayFeature hinge(Rect r) =>
        DisplayFeature(bounds: r, type: DisplayFeatureType.hinge, state: DisplayFeatureState.postureHalfOpened);

    test('phone portrait keeps the column', () {
      expect(workspaceLayoutFor(const Size(390, 700), const [], 80).kind, WorkspaceKind.phone);
    });
    test('tablet landscape: preview left, controls right', () {
      final l = workspaceLayoutFor(const Size(1280, 720), const [], 80);
      expect(l.kind, WorkspaceKind.sideBySide);
      expect(l.split, closeTo(742, 1));
    });
    test('unfolded foldable: split at a vertical hinge', () {
      final l = workspaceLayoutFor(const Size(884, 800), [hinge(const Rect.fromLTWH(440, 0, 4, 904))], 104);
      expect((l.kind, l.split, l.gap), (WorkspaceKind.sideBySide, 440.0, 4.0));
    });
    test('half folded on a table: preview above the fold', () {
      final l = workspaceLayoutFor(const Size(884, 800), [hinge(const Rect.fromLTWH(0, 500, 884, 0))], 104);
      expect((l.kind, l.split), (WorkspaceKind.tabletop, 396.0));
    });
    test('phone landscape goes side by side', () {
      expect(workspaceLayoutFor(const Size(800, 360), const [], 56).kind, WorkspaceKind.sideBySide);
    });
  });

  test('favourites: toggle, order, persist', () {
    final dir = Directory.systemTemp.createTempSync('vs-fav');
    addTearDown(() => dir.deleteSync(recursive: true));
    final f = FavoritesStore.load('${dir.path}/favorites.json');
    expect(f.toggle('look', 'b'), isTrue);
    expect(f.toggle('look', 'c'), isTrue);
    expect(f.ordered('look', ['a', 'b', 'c', 'd'], (String x) => x), ['c', 'b', 'a', 'd']);
    expect(f.toggle('look', 'b'), isFalse);
    final back = FavoritesStore.load('${dir.path}/favorites.json');
    expect(back.list('look'), ['c']);
  });

  test('sound and clip tools through dart:ffi', () {
    final vs = VStudio.create('ffi-tools', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    final clip = vs.appendClip(track, vs.importMedia(media!));
    expect(vs.eqPresets().map((p) => p.id), containsAll(['flat', 'voice', 'bass']));
    vs.setClipAudio([clip], {'fade_in': 0.5, 'eq': {'preset': 'voice'}});
    expect(vs.clipSound(clip).fadeIn, 0.5);
    expect(vs.clipSound(clip).eq?['preset'], 'voice');
    final a = vs.detachAudio(clip);
    expect(vs.clipSound(clip).detached, isTrue);
    expect(vs.project().tracks.any((t) => t.audio && t.clips.any((c) => c.id == a)), isTrue);
    final at = vs.project().tracks.firstWhere((t) => t.audio);
    vs.setTrackMix(at.id, pan: -0.5, solo: true);
    final mixed = vs.project().tracks.firstWhere((t) => t.id == at.id);
    expect((mixed.pan, mixed.solo), (-0.5, true));
    vs.setReversed([clip], true);
    expect(vs.clipSound(clip).reversed, isTrue);
    vs.setEffect([clip], 'builtin.chroma_key', {'color': '#00ff00', 'similarity': 0.4});
    expect(vs.effect(clip, 'builtin.chroma_key')?['color'], '#00ff00');
    final still = vs.freezeFrame(clip, 1.0, seconds: 2);
    expect(vs.clipSound(still).freeze, isTrue);
    expect(() => vs.setReversed(['nope'], true), throwsA(isA<VStudioException>()));
    // Stabilisation in the background, applied when its status is read.
    vs.startToolJob('stabilize', clip, 0.7);
    var st = 1;
    for (var i = 0; i < 300 && st == 1; i++) {
      sleep(const Duration(milliseconds: 20));
      st = vs.toolJobStatus().$1;
    }
    expect(st, 2);
    expect(vs.effect(clip, 'builtin.stabilize')?['zoom'], isNotNull);
    expect(vs.undo(), isTrue);
    expect(vs.effect(clip, 'builtin.stabilize'), isNull);
  }, skip: skip);

  testWidgets('audio and clip tool sheets fit a phone', (tester) async {
    final vs = VStudio.create('ffi-tool-sheets', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.addTrack(audio: false, name: 'V1');
    final clip = vs.appendClip(track, vs.importMedia(media!));
    var edits = 0;
    void onEdit(void Function(VStudio) f) {
      f(vs);
      edits++;
    }

    Widget host(Widget child) => MaterialApp(
        home: Scaffold(body: Align(alignment: Alignment.bottomCenter, child: SizedBox(width: 360, height: 560, child: child))));
    await tester.pumpWidget(host(AudioSheet(vs: vs, clipId: clip, onEdit: onEdit)));
    await tester.tap(find.text('Tăng bass'));
    await tester.pump();
    expect(vs.clipSound(clip).eq?['preset'], 'bass');
    expect(find.text('Mixer'), findsOneWidget);
    await tester.pumpWidget(host(ClipToolsSheet(vs: vs, clipId: clip, playhead: 0.5, onEdit: onEdit)));
    await tester.tap(find.text('Phát ngược'));
    await tester.pump();
    expect(vs.clipSound(clip).reversed, isTrue);
    expect(edits, 2);
    expect(tester.takeException(), isNull); // no overflow
  }, skip: skip != null);
}
