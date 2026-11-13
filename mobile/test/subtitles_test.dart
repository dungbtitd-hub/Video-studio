// Subtitles on the phone: the project view, the C API (import, lines, look, export), the AI
// translation runner (with a fake service: no network in tests) and the "Phụ đề" sheet.
//   VSTUDIO_LIB=../build/capi/libvstudio.so flutter test test/subtitles_test.dart
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/project_model.dart';
import 'package:video_studio/core/subtitle_translate.dart';
import 'package:video_studio/core/vstudio.dart';
import 'package:video_studio/ui/subtitle_sheet.dart';

const _s = 705600000; // ticks per second

const _srt = '1\n00:00:00,500 --> 00:00:01,500\nXin chào\n\n2\n00:00:02,000 --> 00:00:03,000\nTạm biệt\n';

/// The open protocol, answered locally: every text prefixed with "EN ".
Future<(int, String)> _fakeService(TranslationRequest r) async {
  final req = jsonDecode(r.body) as Map<String, dynamic>;
  return (
    200,
    jsonEncode({
      'items': [
        for (final it in (req['items'] as List).cast<Map<String, dynamic>>()) {'id': it['id'], 'text': 'EN ${it['text']}'}
      ]
    })
  );
}

void main() {
  final lib = Platform.environment['VSTUDIO_LIB'];
  final skip = (lib == null || lib.isEmpty) ? 'VSTUDIO_LIB not set' : null;

  test('project view: subtitle tracks carry their settings, lines a label', () {
    final p = ProjectView.fromJson('''
{"name":"T","fps":"30/1","assets":[],
 "tracks":[{"track_id":"s1","type":"video","name":"Phụ đề","order":0,
   "subtitle":{"lang":"vi","style":{"size":56},"position":{"anchor":"bottom"}},
   "clips":[{"clip_id":"c1","asset_id":"","timeline_in":0,"source_in":0,"source_out":${2 * _s},
     "content":{"kind":"subtitle","text":"Xin  chào\\nbạn"}}]}]}
''');
    final t = p.tracks.single;
    expect(t.isSubtitle, isTrue);
    expect(t.subtitle!['lang'], 'vi');
    expect(t.clips.single.name, 'CC  Xin chào bạn');
  });

  test('core: import, add, style, shift, export', () {
    final vs = VStudio.create('ffi-sub', width: 640, height: 360);
    addTearDown(vs.dispose);
    final cat = vs.subtitleCatalog();
    expect(cat.styles.length, greaterThanOrEqualTo(12));
    expect(cat.positions.length, 7);
    expect(cat.languages.map((l) => l['code']), contains('vi'));
    expect(cat.providers.map((p) => p['provider']), containsAll(['anthropic', 'openai', 'gemini', 'custom']));

    final track = vs.importSubtitles(_srt, name: 'Gốc', lang: 'vi');
    var t = vs.project().tracks.firstWhere((t) => t.id == track);
    expect(t.isSubtitle, isTrue);
    expect(t.clips.length, 2);
    expect(t.clips.first.start, closeTo(0.5, 1e-6));

    final line = vs.addSubtitle(4, 'Thêm', trackId: track);
    t = vs.project().tracks.firstWhere((t) => t.id == track);
    expect(t.clips.last.id, line);
    vs.setSubtitleSettings(track, {
      'position': {'anchor': 'top'}
    });
    final r = vs.generatedRect(line, 4.5)!;
    expect(r.$2, lessThan(0.3)); // at the top
    vs.shiftSubtitles(track, 1);
    expect(vs.exportSubtitles(track), contains('00:00:01,500 --> 00:00:02,500'));
    expect(vs.exportSubtitles(track, format: 'vtt'), startsWith('WEBVTT'));
    expect(() => vs.importSubtitles('nothing here'), throwsA(isA<VStudioException>()));
  }, skip: skip);

  test('AI translation: batches through a service, retried, applied bilingual', () async {
    final vs = VStudio.create('ffi-sub-ai', width: 640, height: 360);
    addTearDown(vs.dispose);
    final track = vs.importSubtitles(_srt, name: 'Gốc', lang: 'vi');
    final job = vs.beginTranslation(track, {'provider': 'custom', 'url': 'https://example.invalid/t'},
        from: 'vi', to: 'en', batchSize: 1);
    addTearDown(job.dispose);
    expect(job.count, 2);
    expect(job.request(0).url, 'https://example.invalid/t');
    var calls = 0;
    final runner = TranslationRunner(job, send: (r) async {
      calls++;
      if (calls == 1) return (500, 'oops'); // the first try fails: retried
      return _fakeService(r);
    }, parallel: 1);
    final progress = <int>[];
    final out = await runner.run(progress: (d, n) => progress.add(d));
    expect(out.failed, 0);
    expect(calls, 3);
    expect(progress.last, 2);
    final en = job.apply(2);
    final t = vs.project().tracks.firstWhere((t) => t.id == en);
    expect(t.clips.map((c) => c.content!['text']), ['EN Xin chào', 'EN Tạm biệt']);
    expect(t.subtitle!['position']['anchor'], 'above');
    // A connector without an address is refused before anything is sent.
    final bad = vs.beginTranslation(track, {'provider': 'openai', 'url': ''}, to: 'en');
    addTearDown(bad.dispose);
    expect(() => bad.request(0), throwsA(isA<VStudioException>()));
  }, skip: skip);

  testWidgets('sheet: add a line, type it, style, place, translate', (tester) async {
    final vs = VStudio.create('ffi-sub-sheet', width: 640, height: 360);
    addTearDown(vs.dispose);
    String? selected;
    var edits = 0;
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          child: SizedBox(
            width: 380,
            child: SubtitleSheet(
              vs: vs,
              project: vs.project,
              playhead: () => 1.0,
              selected: () => selected,
              onEdited: () => edits++,
              onSelect: (id, at) => selected = id,
              send: _fakeService,
            ),
          ),
        ),
      ),
    ));
    await tester.tap(find.byKey(const Key('sub-add')));
    await tester.pump();
    expect(selected, isNotNull);
    final track = vs.project().tracks.firstWhere((t) => t.isSubtitle);
    expect(track.clips.single.start, closeTo(1, 1e-6));
    await tester.enterText(find.byKey(const Key('sub-text')), 'Xin chào');
    await tester.pump(const Duration(milliseconds: 700));
    expect(vs.project().tracks.firstWhere((t) => t.isSubtitle).clips.single.content!['text'], 'Xin chào');
    expect(find.byKey(const Key('sub-lines')), findsOneWidget);

    await tester.tap(find.text('Kiểu'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const Key('sub-style-soft_box')));
    await tester.pump();
    expect(vs.project().tracks.firstWhere((t) => t.isSubtitle).subtitle!['style']['box']['opacity'], 0.55);

    await tester.tap(find.text('Vị trí'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const Key('sub-pos-top')));
    await tester.pump();
    expect(vs.project().tracks.firstWhere((t) => t.isSubtitle).subtitle!['position']['anchor'], 'top');

    await tester.tap(find.text('Dịch AI'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const Key('sub-provider')));
    await tester.pumpAndSettle();
    await tester.tap(find.textContaining('API mở').last);
    await tester.pumpAndSettle();
    await tester.enterText(find.widgetWithText(TextField, 'Địa chỉ API'), 'https://example.invalid/t');
    await tester.scrollUntilVisible(find.byKey(const Key('sub-translate')), 120,
        scrollable: find.ancestor(of: find.byKey(const Key('sub-provider')), matching: find.byType(Scrollable)).first);
    await tester.tap(find.byKey(const Key('sub-translate')));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 200)));
    await tester.pump();
    final subs = vs.project().tracks.where((t) => t.isSubtitle).toList();
    expect(subs.length, 2);
    expect(subs.expand((t) => t.clips).map((c) => c.content!['text']), contains('EN Xin chào'));
    expect(edits, greaterThan(3));
    expect(tester.takeException(), isNull);
  }, skip: skip != null);
}
