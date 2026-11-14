// Transition badges on the touch timeline, without the core: a badge under each cut
// between two clips (and at the free edges of the selected clip), tap reports the place.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/project_model.dart';
import 'package:video_studio/ui/timeline.dart';

const _s = 705600000; // ticks per second
const _project = '''
{"name":"T","fps":"30/1",
 "assets":[{"asset_id":"a","uri":"m/a.mp4","media_type":"video","duration":${20 * _s}}],
 "tracks":[
  {"track_id":"v1","type":"video","name":"V1","order":0,"clips":[
    {"clip_id":"c1","asset_id":"a","timeline_in":0,"source_in":0,"source_out":${2 * _s}},
    {"clip_id":"c2","asset_id":"a","timeline_in":${2 * _s},"source_in":0,"source_out":${3 * _s}}]}]}
''';

void main() {
  const px = 40.0; // the timeline's starting zoom
  final cuts = {
    'v1': const [
      CutPoint(trackId: 'v1', at: 0, to: 'c1'),
      CutPoint(trackId: 'v1', at: 2, from: 'c1', to: 'c2', type: 'fade', duration: 1),
      CutPoint(trackId: 'v1', at: 5, from: 'c2'),
    ]
  };

  Future<List<CutPoint>> pump(WidgetTester tester, {String? selected}) async {
    final tapped = <CutPoint>[];
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SizedBox(
          width: 600,
          height: 120,
          child: TimelineView(
            project: ProjectView.fromJson(_project),
            playhead: 0,
            selectedClip: selected,
            onSeek: (_) {},
            onSelect: (_) {},
            onMove: (_, __, ___) {},
            cuts: cuts,
            onCut: tapped.add,
          ),
        ),
      ),
    ));
    return tapped;
  }

  Offset badge(WidgetTester tester, CutPoint c) =>
      tester.getTopLeft(find.byType(CustomPaint).last) + cutBadge(0, c, 0, px).center;

  test('a cut plays centred on it; free edges play inside the clip', () {
    expect(cuts['v1']![1].window, (1.5, 2.5));
    expect(const CutPoint(trackId: 'v', at: 0, to: 'c', type: 'fade', duration: 0.4).window, (0.0, 0.4));
    expect(const CutPoint(trackId: 'v', at: 5, from: 'c', type: 'fade', duration: 0.5).window, (4.5, 5.0));
    expect(cuts['v1']![1].clipId, 'c2');
    expect(cuts['v1']![2].edge, 1);
  });

  testWidgets('tap the badge of a cut: reports that place, not the clip under it', (tester) async {
    final tapped = await pump(tester);
    await tester.tapAt(badge(tester, cuts['v1']![1]));
    expect(tapped.single.clipId, 'c2');
    expect(tapped.single.joined, isTrue);
    // A free start shows no badge unless its clip is selected.
    await tester.tapAt(badge(tester, cuts['v1']![0]));
    expect(tapped, hasLength(1));
  });

  testWidgets('the selected clip offers its free start and end', (tester) async {
    final tapped = await pump(tester, selected: 'c2');
    await tester.tapAt(badge(tester, cuts['v1']![2]));
    expect(tapped.single.edge, 1);
    expect(tapped.single.from, 'c2');
  });
}
