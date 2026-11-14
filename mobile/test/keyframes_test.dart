// Keyframe UI without the core: two-finger zoom on the preview reports the
// right canvas maths, the keyframe bar's buttons, and diamonds on the timeline.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/project_model.dart';
import 'package:video_studio/core/vstudio.dart';
import 'package:video_studio/ui/keyframes.dart';
import 'package:video_studio/ui/timeline.dart';

const _s = 705600000;

void main() {
  testWidgets('pinch on the preview: canvas-space zoom about the fingers', (tester) async {
    final commits = <(double, Offset, Offset)>[];
    var resets = 0;
    await tester.pumpWidget(MaterialApp(
      home: Center(
        child: SizedBox(
          width: 400,
          height: 225, // a 1920x1080 canvas fills it: 1 px = 4.8 canvas px
          child: PreviewZoom(
            enabled: true,
            canvasSize: const Size(1920, 1080),
            onCommit: (f, from, to) => commits.add((f, from, to)),
            onReset: () => resets++,
            child: const ColoredBox(color: Colors.grey),
          ),
        ),
      ),
    ));
    final box = tester.getRect(find.byType(PreviewZoom));
    // Two fingers around (300, 56) of the box spread apart to twice the distance.
    final c = box.topLeft + const Offset(300, 56.25);
    final a = await tester.startGesture(c - const Offset(20, 0));
    final b = await tester.startGesture(c + const Offset(20, 0));
    await tester.pump();
    for (var i = 1; i <= 4; i++) {
      await a.moveTo(c - Offset(20.0 + 5 * i, 0));
      await b.moveTo(c + Offset(20.0 + 5 * i, 0));
      await tester.pump();
    }
    expect(find.textContaining('×'), findsOneWidget); // live zoom badge
    await a.up();
    await b.up();
    await tester.pump();
    expect(commits, hasLength(1));
    final (f, from, to) = commits.single;
    expect(f, closeTo(2, 0.05));
    // (300, 56.25) in the box is (480, -270) canvas px from the centre.
    expect(from.dx, closeTo(480, 6));
    expect(from.dy, closeTo(-270, 6));
    expect((to - from).distance, lessThan(6)); // fingers spread around the same point
    await tester.pump(const Duration(milliseconds: 300));
    // One finger does nothing; a double tap resets.
    final one = await tester.startGesture(c);
    await one.moveBy(const Offset(30, 0));
    await one.up();
    await tester.pump(const Duration(milliseconds: 400));
    expect(commits, hasLength(1));
    await tester.tapAt(c);
    await tester.pump(const Duration(milliseconds: 50));
    await tester.tapAt(c);
    await tester.pumpAndSettle();
    expect(resets, 1);
  });

  testWidgets('keyframe bar: separate picture/audio keys, jumps, volume on release', (tester) async {
    final log = <String>[];
    Future<void> pump(ClipKeyState? st, {bool video = true}) => tester.pumpWidget(MaterialApp(
          home: Scaffold(
            body: KeyframeBar(
              state: st,
              video: video,
              hasKeys: true,
              onToggle: (g) => log.add('toggle ${g.name}'),
              onJump: (fwd) => log.add('jump $fwd'),
              onVolume: (v) => log.add('volume ${v.toStringAsFixed(2)}'),
            ),
          ),
        ));
    ClipKeyState state({bool pic = false, bool aud = false, double volume = 1}) => ClipKeyState.fromJson({
          'inside': true,
          'scale': 1,
          'x': 0,
          'y': 0,
          'opacity': 1,
          'volume': volume,
          'picture_here': pic,
          'audio_here': aud,
          'picture_animated': pic,
          'audio_animated': aud,
        });
    await pump(state(pic: true));
    expect(find.text('◆ Hình'), findsOneWidget);
    expect(find.text('◇ Âm thanh'), findsOneWidget);
    await tester.tap(find.byKey(const Key('kf-picture')));
    await tester.tap(find.byKey(const Key('kf-audio')));
    await tester.tap(find.byKey(const Key('kf-next')));
    await tester.tap(find.byKey(const Key('kf-prev')));
    expect(log, ['toggle picture', 'toggle audio', 'jump true', 'jump false']);
    log.clear();
    // Dragging the slider commits once, on release.
    await tester.drag(find.byKey(const Key('kf-volume')), const Offset(-60, 0));
    await tester.pump();
    expect(log.where((e) => e.startsWith('volume')), hasLength(1));
    // Audio-only clip: no picture keys; outside a clip: nothing.
    await pump(state(), video: false);
    log.clear();
    await tester.tap(find.byKey(const Key('kf-picture')));
    expect(log, isEmpty);
    await pump(null);
    await tester.tap(find.byKey(const Key('kf-audio')));
    expect(log, isEmpty);
  });

  testWidgets('timeline: diamonds from source-time keys, tap one to jump', (tester) async {
    // Clip at 1 s on the timeline, source from 2 s, at 1x: source key 2.5 s is timeline 1.5 s.
    final project = ProjectView.fromJson('''
{"name":"K","fps":"30/1","canvas":{"width":1080,"height":1920},
 "assets":[{"asset_id":"a","uri":"m/a.mp4","media_type":"video","duration":${20 * _s}}],
 "tracks":[{"track_id":"v1","type":"video","name":"V1","order":0,"clips":[
   {"clip_id":"c1","asset_id":"a","timeline_in":${1 * _s},"source_in":${2 * _s},"source_out":${6 * _s},
    "keyframes":{"scale":[{"t":${(2.5 * _s).round()},"v":1},{"t":${4 * _s},"v":2}],
                 "x":[{"t":${4 * _s},"v":0}],
                 "volume":[{"t":${3 * _s},"v":1},{"t":${9 * _s},"v":0}]}}]}]}
''');
    final clip = project.tracks.single.clips.single;
    expect(project.canvasWidth, 1080);
    expect(clip.pictureKeys, [1.5, 3.0]);
    expect(clip.audioKeys, [2.0]); // 9 s is past the clip's end (source 6 s)
    final seeks = <double>[];
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SizedBox(
          width: 800,
          height: 120,
          child: TimelineView(
            project: project,
            playhead: 0,
            selectedClip: 'c1',
            onSeek: seeks.add,
            onSelect: (_) {},
            onMove: (_, __, ___) {},
          ),
        ),
      ),
    ));
    final lanes = tester.getTopLeft(find.byType(CustomPaint).last);
    const px = 40.0, top = TimelineView.rulerH + 4.0;
    await tester.tapAt(lanes + const Offset(3.0 * px + 4, top + 6)); // picture diamond, near enough
    await tester.pump(const Duration(milliseconds: 500));
    await tester.tapAt(lanes + const Offset(2.0 * px - 3, top + TimelineView.trackH - 10 - 6)); // audio diamond
    await tester.pump(const Duration(milliseconds: 500));
    await tester.tapAt(lanes + const Offset(2.5 * px, top + 20)); // no key there: plain select
    await tester.pump(const Duration(milliseconds: 500));
    expect(seeks, [3.0, 2.0]);
  });
}
