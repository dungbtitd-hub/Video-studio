// Touch timeline: header toggles, long-press drag across tracks of the same
// type, snapping, and locked tracks. Pure widget test (no core library).
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/project_model.dart';
import 'package:video_studio/ui/timeline.dart';

const _s = 705600000; // ticks per second
String _project({bool lockV1 = false}) => '''
{"name":"T","fps":"30/1",
 "assets":[{"asset_id":"a","uri":"m/a.mp4","media_type":"video","duration":${20 * _s}}],
 "tracks":[
  {"track_id":"v1","type":"video","name":"V1","order":0,"locked":$lockV1,"clips":[
    {"clip_id":"c1","asset_id":"a","timeline_in":0,"source_in":0,"source_out":${2 * _s}}]},
  {"track_id":"v2","type":"video","name":"V2","order":1,"clips":[
    {"clip_id":"c2","asset_id":"a","timeline_in":${4 * _s},"source_in":0,"source_out":${2 * _s}}]},
  {"track_id":"a1","type":"audio","name":"A1","order":2,"muted":true,"clips":[]}]}
''';

void main() {
  // Rows top-down: V2 (0), V1 (1), A1 (2). 40 px/s; lanes start after the header column.
  const px = 40.0;
  Offset at(WidgetTester tester, double seconds, int row) =>
      tester.getTopLeft(find.byType(CustomPaint).last) +
      Offset(seconds * px, TimelineView.rulerH + row * TimelineView.trackH + TimelineView.trackH / 2);

  Future<List<String>> pump(WidgetTester tester,
      {bool lockV1 = false, double? Function(String, double, double)? snap}) async {
    final log = <String>[];
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SizedBox(
          width: 800,
          height: 220,
          child: TimelineView(
            project: ProjectView.fromJson(_project(lockV1: lockV1)),
            playhead: 0,
            selectedClip: null,
            onSeek: (_) {},
            onSelect: (_) {},
            onMove: (clip, track, start) => log.add('move $clip $track ${start.toStringAsFixed(2)}'),
            onTrackFlags: (t, {required enabled, required locked, required muted}) =>
                log.add('flags ${t.id} $enabled $locked $muted'),
            onTrackMenu: (t) => log.add('menu ${t?.id}'),
            snapMove: snap,
          ),
        ),
      ),
    ));
    return log;
  }

  Future<void> longDrag(WidgetTester tester, Offset from, Offset to) async {
    final g = await tester.startGesture(from);
    await tester.pump(kLongPressTimeout + const Duration(milliseconds: 50));
    await g.moveTo(Offset.lerp(from, to, 0.5)!);
    await tester.pump();
    await g.moveTo(to);
    await tester.pump();
    await g.up();
    await tester.pump();
  }

  testWidgets('header toggles and track menu', (tester) async {
    final log = await pump(tester);
    expect(find.text('V2'), findsOneWidget);
    await tester.tap(find.byIcon(Icons.lock_open).first); // V2
    await tester.tap(find.byIcon(Icons.visibility).last); // V1
    await tester.tap(find.byIcon(Icons.volume_off)); // A1 is muted -> unmute
    await tester.longPress(find.text('A1'));
    await tester.tap(find.text(' Track'));
    expect(log, ['flags v2 true true false', 'flags v1 false false false', 'flags a1 true false false', 'menu a1', 'menu null']);
  });

  testWidgets('drag moves onto another video track, not onto audio', (tester) async {
    final log = await pump(tester);
    await longDrag(tester, at(tester, 5, 0), at(tester, 8, 1)); // c2: V2 -> V1, +3 s
    await longDrag(tester, at(tester, 1, 1), at(tester, 1, 2)); // c1 onto audio: stays, no change
    expect(log, ['move c2 v1 7.00']);
  });

  testWidgets('snap callback decides the start', (tester) async {
    final log = await pump(tester, snap: (clip, start, tol) => (start - 6).abs() <= tol ? 6.0 : null);
    await longDrag(tester, at(tester, 5, 0), at(tester, 7.1, 0)); // proposes 6.1 -> 6
    expect(log, ['move c2 v2 6.00']);
  });

  testWidgets('locked track: clips do not move and do not accept drops', (tester) async {
    final log = await pump(tester, lockV1: true);
    await longDrag(tester, at(tester, 1, 1), at(tester, 3, 1)); // c1 on locked V1
    await longDrag(tester, at(tester, 5, 0), at(tester, 9, 1)); // c2 dropped on locked V1 stays on V2
    expect(log, ['move c2 v2 8.00']);
  });
}
