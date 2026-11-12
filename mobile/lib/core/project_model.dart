// Read-only view of the schema v0 project JSON produced by the core
// (vs_project_json). The UI draws from this; all edits go through the core.
import 'dart:convert';

const int ticksPerSecond = 705600000;

double ticksToSeconds(int ticks) => ticks / ticksPerSecond;
int secondsToTicks(double s) => (s * ticksPerSecond).round();

class ClipView {
  ClipView({required this.id, required this.assetId, required this.start, required this.duration, required this.name});
  final String id;
  final String assetId;
  final double start; // seconds on the timeline
  final double duration;
  final String name;
  double get end => start + duration;
}

class TrackView {
  TrackView({required this.id, required this.type, required this.name, required this.clips});
  final String id;
  final String type; // "video" | "audio"
  final String name;
  final List<ClipView> clips;
}

class AssetView {
  AssetView({required this.id, required this.name, required this.type, required this.duration});
  final String id;
  final String name;
  final String type;
  final double duration;
}

class ProjectView {
  ProjectView({required this.name, required this.fps, required this.tracks, required this.assets});
  final String name;
  final double fps;
  final List<TrackView> tracks; // display order: top video first, then audio
  final List<AssetView> assets;

  double get duration =>
      tracks.expand((t) => t.clips).fold(0.0, (m, c) => c.end > m ? c.end : m);

  static double _rational(String s) {
    final parts = s.split('/');
    final n = double.parse(parts[0]);
    final d = parts.length > 1 ? double.parse(parts[1]) : 1.0;
    return d == 0 ? 0 : n / d;
  }

  factory ProjectView.fromJson(String source) {
    final j = jsonDecode(source) as Map<String, dynamic>;
    final assets = <String, AssetView>{};
    for (final a in (j['assets'] as List? ?? const [])) {
      final m = a as Map<String, dynamic>;
      final uri = m['uri'] as String;
      assets[m['asset_id'] as String] = AssetView(
        id: m['asset_id'] as String,
        name: uri.split('/').last,
        type: m['media_type'] as String,
        duration: ticksToSeconds((m['duration'] as num).toInt()),
      );
    }
    final raw = (j['tracks'] as List? ?? const []).cast<Map<String, dynamic>>().toList()
      ..sort((a, b) => ((a['order'] ?? 0) as int).compareTo((b['order'] ?? 0) as int));
    TrackView toView(Map<String, dynamic> t) {
      final clips = <ClipView>[];
      for (final c in (t['clips'] as List? ?? const [])) {
        final m = c as Map<String, dynamic>;
        final speed = _rational((m['speed'] ?? '1/1') as String);
        final srcDur = ticksToSeconds((m['source_out'] as num).toInt() - (m['source_in'] as num).toInt());
        clips.add(ClipView(
          id: m['clip_id'] as String,
          assetId: m['asset_id'] as String,
          start: ticksToSeconds((m['timeline_in'] as num).toInt()),
          duration: speed > 0 ? srcDur / speed : srcDur,
          name: assets[m['asset_id']]?.name ?? '?',
        ));
      }
      return TrackView(id: t['track_id'] as String, type: t['type'] as String, name: (t['name'] ?? '') as String, clips: clips);
    }

    final video = raw.where((t) => t['type'] == 'video').map(toView).toList().reversed;
    final audio = raw.where((t) => t['type'] == 'audio').map(toView);
    return ProjectView(
      name: (j['name'] ?? '') as String,
      fps: _rational(j['fps'] as String),
      tracks: [...video, ...audio],
      assets: assets.values.toList(),
    );
  }
}
