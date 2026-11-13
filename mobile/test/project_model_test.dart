import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/project_model.dart';

// JSON shaped exactly like the core's schema v0 output (see core/src/model.cpp).
const sample = '''
{"format":"vstudio","schema_version":0,"project_id":"p","name":"Demo","canvas":{"width":1080,"height":1920},
 "fps":"30000/1001","timebase":"1/705600000","audio":{"sample_rate":48000,"channels":2},
 "assets":[{"asset_id":"a1","uri":"media/clip.mp4","media_type":"video","duration":7056000000,"fps":"30/1"}],
 "tracks":[
  {"track_id":"t0","type":"video","name":"V1","order":0,"clips":[
    {"clip_id":"c1","asset_id":"a1","timeline_in":0,"source_in":0,"source_out":1411200000,"speed":"1/1"},
    {"clip_id":"c2","asset_id":"a1","timeline_in":1411200000,"source_in":1411200000,"source_out":2822400000,"speed":"2/1"}]},
  {"track_id":"t1","type":"audio","name":"A1","order":1,"clips":[]},
  {"track_id":"t2","type":"video","name":"V2","order":2,"enabled":false,"locked":true,"clips":[]}]}
''';

void main() {
  test('parses schema v0 project JSON', () {
    final p = ProjectView.fromJson(sample);
    expect(p.name, 'Demo');
    expect(p.fps, closeTo(29.97, 0.01));
    expect(p.tracks.map((t) => t.name), ['V2', 'V1', 'A1']); // top video first, audio last
    final v1 = p.tracks[1];
    expect(v1.clips[0].duration, closeTo(2.0, 1e-9));
    expect(v1.clips[1].start, closeTo(2.0, 1e-9));
    expect(v1.clips[1].duration, closeTo(1.0, 1e-9)); // 2 s of source at 2x
    expect(p.duration, closeTo(3.0, 1e-9));
    expect(v1.clips[0].name, 'clip.mp4');
  });

  test('track flags and order within type', () {
    final p = ProjectView.fromJson(sample);
    final v2 = p.tracks[0], v1 = p.tracks[1], a1 = p.tracks[2];
    expect((v2.enabled, v2.locked, v2.muted), (false, true, false));
    expect((v1.enabled, v1.locked, v1.muted), (true, false, false)); // defaults when absent
    expect(v2.indexInType, 1); // top layer
    expect(v1.indexInType, 0);
    expect(a1.indexInType, 0);
    expect(a1.audio, isTrue);
  });

  test('tick conversion is exact for whole frames', () {
    expect(secondsToTicks(1.0), ticksPerSecond);
    expect(ticksToSeconds(secondsToTicks(12.5)), 12.5);
  });
}
