// Read-only view of the schema v0 project JSON produced by the core
// (vs_project_json). The UI draws from this; all edits go through the core.
import 'dart:convert';
import 'dart:math' as math;

const int ticksPerSecond = 705600000;

double ticksToSeconds(int ticks) => ticks / ticksPerSecond;

/// Seconds of timeline that source span [a, b] takes with a speed ramp: keys (source s, x speed),
/// linear between keys, held outside, times [base]. Same closed form as Clip::curveElapsed.
double curveElapsed(List<(double, double)> keys, double base, double a, double b) {
  if (b <= a || keys.isEmpty) return b > a ? (b - a) / base : 0;
  double rate(double s) {
    if (s <= keys.first.$1) return base * keys.first.$2;
    if (s >= keys.last.$1) return base * keys.last.$2;
    for (var i = 1; i < keys.length; i++) {
      if (s <= keys[i].$1) {
        final (t0, v0) = keys[i - 1];
        final (t1, v1) = keys[i];
        return base * (t1 > t0 ? v0 + (v1 - v0) * (s - t0) / (t1 - t0) : v1);
      }
    }
    return base * keys.last.$2;
  }

  // Break [a, b] at the keys: the rate is linear on each piece.
  final cuts = <double>[a, for (final k in keys) if (k.$1 > a && k.$1 < b) k.$1, b];
  var t = 0.0;
  for (var i = 1; i < cuts.length; i++) {
    final x0 = cuts[i - 1], x1 = cuts[i], r0 = rate(x0), r1 = rate(x1);
    if ((r1 - r0).abs() < 1e-12) {
      t += (x1 - x0) / r0;
    } else {
      final k = (r1 - r0) / (x1 - x0);
      t += math.log(r1 / r0) / k;
    }
  }
  return t;
}

int secondsToTicks(double s) => (s * ticksPerSecond).round();

class ClipView {
  ClipView(
      {required this.id,
      required this.assetId,
      required this.start,
      required this.duration,
      required this.name,
      this.sourceIn = 0,
      this.speed = 1,
      this.look,
      this.lookStrength = 1,
      this.colorSpace,
      this.lut,
      this.grade,
      this.pictureKeys = const [],
      this.audioKeys = const [],
      this.videoFx = const {},
      this.content});
  final String id;
  final String assetId;
  final double start; // seconds on the timeline
  final double duration;
  final String name;
  final double sourceIn; // source seconds at `start`
  final double speed; // source seconds per timeline second
  final String? look; // colour look id (builtin.color_look), if any
  final double lookStrength;
  final String? colorSpace; // Log/HDR input id (builtin.color_space), if any
  final String? lut; // .cube path (builtin.lut), if any
  final Map<String, dynamic>? grade; // pro grade params (builtin.color_grade), if any
  // Keyframes inside the clip, as timeline seconds (sorted). Picture (zoom, position,
  // opacity) and audio (volume) keys are independent.
  final List<double> pictureKeys;
  final List<double> audioKeys;
  // Video effects by group ("zoom", "rotate", "reflect"): their params {"fx", "amount", "speed", "line"}.
  final Map<String, Map<String, dynamic>> videoFx;
  // Text and sticker clips have no media: what they draw ({"kind": "text" | "sticker", ...}).
  final Map<String, dynamic>? content;
  String get kind => (content?['kind'] ?? '') as String;
  bool get generated => content != null;
  double get end => start + duration;
  bool get animated => pictureKeys.isNotEmpty || audioKeys.isNotEmpty;
}

/// Sticker id -> Vietnamese name, filled from the core's catalog (for clip labels).
final Map<String, String> stickerNames = {};

/// Overlay effect names by id (filled from the core's catalog), for clip labels.
final Map<String, String> overlayNames = {};

/// The label drawn on a text or sticker clip.
String generatedLabel(Map<String, dynamic> content) {
  switch (content['kind']) {
    case 'text':
      final t = ((content['text'] ?? '') as String).replaceAll(RegExp(r'\s+'), ' ').trim();
      return 'T  ${t.isEmpty ? '(chữ trống)' : t}';
    case 'subtitle':
      final t = ((content['text'] ?? '') as String).replaceAll(RegExp(r'\s+'), ' ').trim();
      return 'CC  ${t.isEmpty ? '(dòng trống)' : t}';
    case 'sticker':
      final id = (content['sticker'] ?? '') as String;
      return '★  ${stickerNames[id] ?? 'Sticker'}';
    case 'overlay':
      final id = (content['overlay'] ?? '') as String;
      return '✦  ${overlayNames[id] ?? 'Lớp phủ'}';
  }
  return (content['kind'] ?? '?') as String;
}

class TrackView {
  TrackView(
      {required this.id,
      required this.type,
      required this.name,
      required this.clips,
      this.enabled = true,
      this.locked = false,
      this.muted = false,
      this.indexInType = 0,
      this.subtitle,
      this.volume = 1,
      this.pan = 0,
      this.solo = false});
  final String id;
  final String type; // "video" | "audio"
  final String name;
  final List<ClipView> clips;
  final bool enabled; // video: shown in the picture
  final bool locked; // no edits; clips stay visible
  final bool muted; // audio: silent
  final int indexInType; // order among tracks of this type in the project (0 = bottom video layer / first audio)
  final Map<String, dynamic>? subtitle; // subtitle track settings (style, position, lang), else null
  final double volume; // mixer: track gain
  final double pan; // mixer: -1 left .. 1 right
  final bool solo; // mixer: only soloed tracks are heard
  bool get audio => type == 'audio';
  bool get isSubtitle => subtitle != null;
}

class AssetView {
  AssetView({required this.id, required this.name, required this.type, required this.duration});
  final String id;
  final String name;
  final String type;
  final double duration;
}

class ProjectView {
  ProjectView(
      {required this.name,
      required this.fps,
      required this.tracks,
      required this.assets,
      this.canvasWidth = 1920,
      this.canvasHeight = 1080});
  final String name;
  final double fps;
  final int canvasWidth, canvasHeight; // output frame size; keyframe positions are in these pixels
  final List<TrackView> tracks; // display order: top video first, then audio
  final List<AssetView> assets;

  double get duration => tracks.expand((t) => t.clips).fold(0.0, (m, c) => c.end > m ? c.end : m);

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
    TrackView toView(Map<String, dynamic> t, int indexInType) {
      final clips = <ClipView>[];
      for (final c in (t['clips'] as List? ?? const [])) {
        final m = c as Map<String, dynamic>;
        final speed = _rational((m['speed'] ?? '1/1') as String);
        Map<String, dynamic>? effect(String type) => (m['effects'] as List? ?? const [])
            .cast<Map<String, dynamic>>()
            .where((e) => e['type'] == type && (e['enabled'] ?? true) == true)
            .map((e) => e['params'] as Map<String, dynamic>?)
            .firstOrNull;
        final look = effect('builtin.color_look');
        final srcDur = ticksToSeconds((m['source_out'] as num).toInt() - (m['source_in'] as num).toInt());
        final start = ticksToSeconds((m['timeline_in'] as num).toInt());
        final sourceIn = ticksToSeconds((m['source_in'] as num).toInt());
        final clipSpeed = speed > 0 ? speed : 1.0;
        final sourceOut = ticksToSeconds((m['source_out'] as num).toInt());
        final curve = [
          for (final k in (m['speed_curve'] as List? ?? const []).cast<Map<String, dynamic>>())
            (ticksToSeconds((k['t'] as num).toInt()), (k['v'] as num).toDouble())
        ];
        final reversed = m['reverse'] == true;
        // Timeline seconds from the clip start to source time `s` (speed ramps included).
        double sinceStart(double s) {
          if (curve.isEmpty) return (reversed ? sourceOut - s : s - sourceIn) / clipSpeed;
          return reversed ? curveElapsed(curve, clipSpeed, s, sourceOut) : curveElapsed(curve, clipSpeed, sourceIn, s);
        }

        final clipDur = curve.isEmpty ? srcDur / clipSpeed : curveElapsed(curve, clipSpeed, sourceIn, sourceOut);
        // Keys are stored in source time; shown where they fall on the timeline.
        List<double> keys(List<String> props) {
          final out = <double>{};
          final kf = m['keyframes'] as Map<String, dynamic>? ?? const {};
          for (final p in props) {
            for (final k in (kf[p] as List? ?? const []).cast<Map<String, dynamic>>()) {
              final t = start + sinceStart(ticksToSeconds((k['t'] as num).toInt()));
              if (t >= start - 1e-6 && t <= start + clipDur + 1e-6) out.add((t * 1e6).roundToDouble() / 1e6);
            }
          }
          return out.toList()..sort();
        }

        final content = m['content'] as Map<String, dynamic>?;
        clips.add(ClipView(
          id: m['clip_id'] as String,
          assetId: m['asset_id'] as String,
          start: start,
          duration: clipDur,
          name: content != null ? generatedLabel(content) : assets[m['asset_id']]?.name ?? '?',
          sourceIn: sourceIn,
          // Average speed for the filmstrip and waveform (exact for constant speed).
          speed: curve.isEmpty || clipDur <= 0 ? clipSpeed : srcDur / clipDur,
          look: look?['look'] as String?,
          lookStrength: ((look?['strength'] ?? 1) as num).toDouble(),
          colorSpace: effect('builtin.color_space')?['input'] as String?,
          lut: effect('builtin.lut')?['path'] as String?,
          grade: effect('builtin.color_grade'),
          pictureKeys: keys(const ['scale', 'x', 'y', 'opacity']),
          audioKeys: keys(const ['volume']),
          videoFx: {
            for (final g in const ['zoom', 'rotate', 'reflect'])
              if (effect('builtin.fx_$g') case final p? when (p['fx'] ?? '') != '') g: p,
          },
          content: content,
        ));
      }
      return TrackView(
        id: t['track_id'] as String,
        type: t['type'] as String,
        name: (t['name'] ?? '') as String,
        clips: clips,
        enabled: (t['enabled'] ?? true) as bool,
        locked: (t['locked'] ?? false) as bool,
        muted: (t['muted'] ?? false) as bool,
        indexInType: indexInType,
        subtitle: t['subtitle'] is Map<String, dynamic> ? t['subtitle'] as Map<String, dynamic> : null,
        volume: (t['volume'] as num?)?.toDouble() ?? 1,
        pan: (t['pan'] as num?)?.toDouble() ?? 0,
        solo: t['solo'] == true,
      );
    }

    List<TrackView> ofType(String type) {
      final list = raw.where((t) => t['type'] == type).toList();
      return [for (var i = 0; i < list.length; i++) toView(list[i], i)];
    }

    final video = ofType('video').reversed;
    final audio = ofType('audio');
    return ProjectView(
      name: (j['name'] ?? '') as String,
      fps: _rational(j['fps'] as String),
      canvasWidth: ((j['canvas'] as Map?)?['width'] ?? 1920) as int,
      canvasHeight: ((j['canvas'] as Map?)?['height'] ?? 1080) as int,
      tracks: [...video, ...audio],
      assets: assets.values.toList(),
    );
  }
}

/// A place on a video track where a transition can go: a cut between two touching
/// clips ([from] and [to] set), or the free start ([to] only) / end ([from] only) of a clip.
class CutPoint {
  const CutPoint({required this.trackId, required this.at, this.from, this.to, this.type, this.duration = 0});
  factory CutPoint.fromJson(String trackId, Map<String, dynamic> j) => CutPoint(
      trackId: trackId,
      at: ticksToSeconds((j['at'] as num).toInt()),
      from: j['from'] as String?,
      to: j['to'] as String?,
      type: j['type'] as String?,
      duration: ticksToSeconds((j['duration'] as num? ?? 0).toInt()));
  final String trackId;
  final double at; // seconds on the timeline
  final String? from, to;
  final String? type; // null: no transition here
  final double duration; // seconds, as played
  bool get joined => from != null && to != null;

  /// The clip that stores it and its edge (0 = start, 1 = end).
  String get clipId => to ?? from!;
  int get edge => to != null ? 0 : 1;

  /// Where it plays on the timeline (start, end) in seconds.
  (double, double) get window => joined
      ? (at - duration / 2, at + duration / 2)
      : edge == 0
          ? (at, at + duration)
          : (at - duration, at);
}
