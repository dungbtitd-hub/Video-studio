// Text and stickers on the phone. The box on the preview and the maths run without
// the core; the sheets and the C API run when VSTUDIO_LIB points at a build:
//   VSTUDIO_LIB=../build/capi/libvstudio.so flutter test test/text_stickers_test.dart
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/project_model.dart';
import 'package:video_studio/core/vstudio.dart';
import 'package:video_studio/ui/text_sticker.dart';

const _s = 705600000; // ticks per second

void main() {
  final lib = Platform.environment['VSTUDIO_LIB'];
  final skip = (lib == null || lib.isEmpty) ? 'VSTUDIO_LIB not set' : null;

  test('merge patch: nested objects merge, null removes', () {
    final c = mergePatch({
      'text': 'a',
      'stroke': {'color': '#000000', 'width': 0.05},
      'color2': '#ff0000'
    }, {
      'stroke': {'width': 0.1},
      'color2': null,
      'size': 120
    });
    expect(c, {
      'text': 'a',
      'stroke': {'color': '#000000', 'width': 0.1},
      'size': 120
    });
  });

  test('moving the box moves the clip; scaling keeps the anchor in step', () {
    const canvas = Size(1000, 500);
    // Box centred on the canvas, clip at its default place: moving the box 0.1 right moves x by 100 px.
    var t = objectTransform(
        canvas, const Rect.fromLTWH(0.4, 0.4, 0.2, 0.2), const Rect.fromLTWH(0.5, 0.4, 0.2, 0.2), 0, 0, 1);
    expect(t.x, closeTo(100, 1e-9));
    expect(t.y, closeTo(0, 1e-9));
    expect(t.scale, closeTo(1, 1e-9));
    // Doubling the box about its own centre doubles the scale and keeps the box centre where it was.
    t = objectTransform(
        canvas, const Rect.fromLTWH(0.4, 0.4, 0.2, 0.2), const Rect.fromLTWH(0.3, 0.3, 0.4, 0.4), 20, -10, 1.5);
    expect(t.scale, closeTo(3, 1e-9));
    // The anchor was 20 px right of the box centre; it is now 40 px right of it.
    expect(t.x, closeTo(40, 1e-9));
    expect(t.y, closeTo(-20, 1e-9));
  });

  test('project view: text and sticker clips carry content and a label', () {
    stickerNames['heart_red'] = 'Tim đỏ';
    final p = ProjectView.fromJson('''
{"name":"T","fps":"30/1","assets":[],
 "tracks":[{"track_id":"v1","type":"video","name":"V1","order":0,"clips":[
   {"clip_id":"t1","asset_id":"","timeline_in":0,"source_in":0,"source_out":${3 * _s},
    "content":{"kind":"text","text":"Xin   chào\\nbạn"}},
   {"clip_id":"s1","asset_id":"","timeline_in":${3 * _s},"source_in":0,"source_out":${2 * _s},
    "content":{"kind":"sticker","sticker":"heart_red"}}]}]}
''');
    final clips = p.tracks.single.clips;
    expect(clips[0].kind, 'text');
    expect(clips[0].generated, isTrue);
    expect(clips[0].name, 'T  Xin chào bạn');
    expect(clips[1].name, '★  Tim đỏ');
    expect(p.duration, closeTo(5, 1e-9));
  });

  group('box on the preview', () {
    Future<List<(Rect, Rect)>> pumpOverlay(WidgetTester tester, {bool freeShape = false}) async {
      final changes = <(Rect, Rect)>[];
      await tester.pumpWidget(MaterialApp(
        home: Center(
          child: SizedBox(
            width: 400,
            height: 300, // 16:9 canvas letterboxed: 400 x 225 from y = 37.5
            child: ObjectOverlay(
              canvasSize: const Size(1920, 1080),
              rect: const Rect.fromLTWH(0.25, 0.2, 0.5, 0.4),
              onChanged: (a, b) => changes.add((a, b)),
              freeShape: freeShape,
            ),
          ),
        ),
      ));
      return changes;
    }

    testWidgets('one finger moves it; one change per drag', (tester) async {
      final changes = await pumpOverlay(tester);
      final box = tester.getRect(find.byType(ObjectOverlay));
      final g = await tester.startGesture(box.topLeft + const Offset(200, 120));
      for (var i = 1; i <= 4; i++) {
        await g.moveBy(const Offset(10, 5.625));
        await tester.pump();
      }
      expect(changes, isEmpty);
      await g.up();
      await tester.pump();
      final (from, to) = changes.single;
      expect(from, const Rect.fromLTWH(0.25, 0.2, 0.5, 0.4));
      expect(to.left, closeTo(0.35, 0.02));
      expect(to.top, closeTo(0.3, 0.02));
      expect(to.width, closeTo(0.5, 1e-6));
    });

    testWidgets('the corner handle scales about the centre, keeping the shape', (tester) async {
      final changes = await pumpOverlay(tester);
      final box = tester.getRect(find.byType(ObjectOverlay));
      final corner = box.topLeft + const Offset(300, 37.5 + 0.6 * 225);
      final g = await tester.startGesture(corner);
      for (var i = 1; i <= 4; i++) {
        await g.moveBy(const Offset(10, 5.625));
        await tester.pump();
      }
      await g.up();
      await tester.pump();
      final (_, to) = changes.single;
      expect(to.center.dx, closeTo(0.5, 1e-6));
      expect(to.center.dy, closeTo(0.4, 1e-6));
      expect(to.width / to.height, closeTo(0.5 / 0.4, 1e-6));
      expect(to.width, greaterThan(0.55));
    });

    testWidgets('an overlay region: the corner resizes freely, top-left stays', (tester) async {
      final changes = await pumpOverlay(tester, freeShape: true);
      final box = tester.getRect(find.byType(ObjectOverlay));
      final g = await tester.startGesture(box.topLeft + const Offset(300, 37.5 + 0.6 * 225));
      for (var i = 1; i <= 4; i++) {
        await g.moveBy(const Offset(10, -5));
        await tester.pump();
      }
      await g.up();
      await tester.pump();
      final (_, to) = changes.single;
      expect(to.left, closeTo(0.25, 1e-6));
      expect(to.top, closeTo(0.2, 1e-6));
      expect(to.width, closeTo(0.6, 0.01));
      expect(to.height, closeTo(0.4 - 20 / 225, 0.01));
    });
  });

  test('core: catalogs, add, edit, box, icons', () {
    final vs = VStudio.create('ffi-text', width: 640, height: 360);
    addTearDown(vs.dispose);
    final cat = vs.textCatalog();
    expect(cat.fonts.map((f) => f.id), contains('be-vietnam-pro-bold'));
    expect(cat.animations.length, greaterThanOrEqualTo(30));
    expect(cat.animations.where((a) => a.perLetter), isNotEmpty);
    expect(cat.loops, isNotEmpty);
    expect(cat.presets.length, greaterThanOrEqualTo(12));
    expect(cat.stickers.length, greaterThanOrEqualTo(100));
    expect(cat.stickerCategories.first, 'Mặt cười');

    final text = vs.defaultText('Xin chào');
    expect(text['kind'], 'text');
    final id = vs.addGeneratedClip(text, 1, 3);
    var clip = vs.project().tracks.expand((t) => t.clips).firstWhere((c) => c.id == id);
    expect(clip.name, 'T  Xin chào');
    expect(clip.start, closeTo(1, 1e-9));
    expect(clip.duration, closeTo(3, 1e-9));

    vs.updateClipContent(id, {'text': 'Tạm biệt', 'size': 150});
    clip = vs.project().tracks.expand((t) => t.clips).firstWhere((c) => c.id == id);
    expect(clip.content!['text'], 'Tạm biệt');
    expect(clip.content!['size'], 150);
    expect(vs.undo(), isTrue);
    clip = vs.project().tracks.expand((t) => t.clips).firstWhere((c) => c.id == id);
    expect(clip.content!['text'], 'Xin chào');

    final r = vs.generatedRect(id, 2.5);
    expect(r, isNotNull);
    expect(r!.$1 + r.$3 / 2, closeTo(0.5, 0.05)); // centred

    // Moving it keeps the box in step.
    vs.setPropertyAt(id, 2.5, {'x': 64});
    expect(vs.generatedRect(id, 2.5)!.$1, closeTo(r.$1 + 0.1, 0.01));

    final sticker = vs.addGeneratedClip(vs.defaultSticker('heart_red'), 1, 2);
    expect(vs.project().tracks.expand((t) => t.clips).firstWhere((c) => c.id == sticker).name, '★  Tim đỏ');

    final icon = vs.stickerIcon('heart_red', 48)!;
    var opaque = 0;
    for (var i = 3; i < icon.length; i += 4) {
      if (icon[i] > 200) opaque++;
    }
    expect(opaque, greaterThan(48 * 48 / 5));
    expect(vs.stickerIcon('no-such-sticker', 48), isNull);
    // Overlay effects: full frame when added, then a region; an icon for the library.
    final overlays = vs.overlayCatalog();
    expect(overlays.length, greaterThanOrEqualTo(20));
    expect(overlays.map((o) => o.id), containsAll(['snow', 'snowflakes', 'confetti', 'fireworks']));
    final snow = vs.addGeneratedClip(vs.defaultOverlay('snow'), 0, 5);
    expect(vs.project().tracks.expand((t) => t.clips).firstWhere((c) => c.id == snow).name, '✦  Tuyết rơi');
    var box = vs.generatedRect(snow, 1)!;
    expect(box.$1, closeTo(0, 1e-6));
    expect(box.$3, closeTo(1, 1e-6));
    vs.setOverlayRegion(snow, 1, 0, 0.5, 1, 0.5);
    box = vs.generatedRect(snow, 1)!;
    expect(box.$2, closeTo(0.5, 1e-6));
    expect(box.$4, closeTo(0.5, 1e-6));
    final ov = vs.overlayIcon('snow', 64, 36)!;
    expect(ov[3], 255);
    expect(vs.overlayIcon('nope', 64, 36), isNull);
    final px = vs.contentPreview(text, 1.5, 3, 160, 90)!;
    expect(px.length, 160 * 90 * 4);
    expect(px.where((v) => v != 0), isNotEmpty);
  }, skip: skip);

  testWidgets('text sheet: typing commits once, presets and fonts patch, no overflow', (tester) async {
    final vs = VStudio.create('ffi-text-sheet', width: 640, height: 360);
    addTearDown(vs.dispose);
    final patches = <Map<String, dynamic>>[];
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          child: SizedBox(width: 360, child: TextSheet(vs: vs, content: vs.defaultText('Chào'), onPatch: patches.add)),
        ),
      ),
    ));
    await tester.enterText(find.byKey(const Key('text-field')), 'Chào bạn');
    await tester.pump(const Duration(milliseconds: 300));
    await tester.enterText(find.byKey(const Key('text-field')), 'Chào bạn nhé');
    await tester.pump(const Duration(milliseconds: 700));
    expect(patches, [
      {'text': 'Chào bạn nhé'}
    ]);
    await tester.tap(find.byKey(const Key('font-be-vietnam-pro')));
    await tester.tap(find.byKey(const Key('align-left')));
    expect(patches.sublist(1), [
      {'font': 'be-vietnam-pro'},
      {'align': 'left'}
    ]);

    await tester.tap(find.text('Kiểu'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const Key('preset-outline')));
    await tester.tap(find.byKey(const Key('color-#ffd54a')));
    await tester.pump();
    expect(patches[3].containsKey('font'), isTrue); // a preset without a font goes back to the default
    expect(patches[4], {'color': '#ffd54a', 'color2': null});

    await tester.tap(find.text('Hiệu ứng'));
    await tester.pumpAndSettle(const Duration(milliseconds: 100));
    await tester.ensureVisible(find.byKey(const Key('anim-in-typewriter')));
    await tester.pump();
    await tester.tap(find.byKey(const Key('anim-in-typewriter')));
    await tester.pump();
    expect(patches.last, {
      'in': {'type': 'typewriter', 'duration': 0.4}
    });
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 300))); // the preview plays
    await tester.pump(const Duration(milliseconds: 80));
    expect(find.byKey(const Key('anim-preview')), findsOneWidget);
    expect(tester.takeException(), isNull); // no overflow at phone width
    await tester.pumpWidget(const SizedBox()); // stops the preview timer
  }, skip: skip != null);

  testWidgets('sticker sheet: categories, a tap adds, then size and animation', (tester) async {
    final vs = VStudio.create('ffi-sticker-sheet', width: 640, height: 360);
    addTearDown(vs.dispose);
    final picked = <String>[];
    final patches = <Map<String, dynamic>>[];
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          child: SizedBox(
            width: 360,
            child: StickerSheet(
              vs: vs,
              content: null,
              onPick: (id) {
                picked.add(id);
                return vs.defaultSticker(id);
              },
              onPatch: patches.add,
            ),
          ),
        ),
      ),
    ));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 200))); // icons decode
    await tester.pump();
    await tester.ensureVisible(find.byKey(const Key('sticker-cat-Trái tim')));
    await tester.pump();
    await tester.tap(find.byKey(const Key('sticker-cat-Trái tim')));
    await tester.pump();
    await tester.tap(find.byKey(const Key('sticker-heart_red')));
    await tester.pump();
    expect(picked, ['heart_red']);
    await tester.tap(find.text('Hiệu ứng'));
    await tester.pumpAndSettle(const Duration(milliseconds: 100));
    await tester.tap(find.byKey(const Key('sticker-flip')));
    await tester.drag(find.byKey(const Key('sticker-size')), const Offset(40, 0));
    await tester.pump();
    expect(patches[0], {'flip': true});
    expect(patches[1]['size'], isA<double>());
    await tester.tap(find.text('Lặp lại'));
    await tester.pump();
    await tester.ensureVisible(find.byKey(const Key('anim-loop-pulse')));
    await tester.pump();
    await tester.tap(find.byKey(const Key('anim-loop-pulse')));
    await tester.pump();
    expect(patches.last, {
      'loop': {'type': 'pulse', 'speed': 1.0}
    });
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  }, skip: skip != null);

  testWidgets('overlay sheet: categories, a tap adds, then density, colour and full frame', (tester) async {
    final vs = VStudio.create('ffi-overlay-sheet', width: 640, height: 360);
    addTearDown(vs.dispose);
    final picked = <String>[];
    final patches = <Map<String, dynamic>>[];
    var full = 0;
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: Align(
          alignment: Alignment.bottomCenter,
          child: SizedBox(
            width: 360,
            child: OverlaySheet(
              vs: vs,
              content: null,
              onPick: (id) {
                picked.add(id);
                return vs.defaultOverlay(id);
              },
              onPatch: patches.add,
              onFullFrame: () => full++,
            ),
          ),
        ),
      ),
    ));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 200))); // icons decode
    await tester.pump();
    await tester.tap(find.byKey(const Key('overlay-snow')));
    await tester.pump();
    expect(picked, ['snow']);
    await tester.ensureVisible(find.byKey(const Key('overlay-cat-Lễ hội')));
    await tester.pump();
    await tester.tap(find.byKey(const Key('overlay-cat-Lễ hội')));
    await tester.pump();
    expect(find.byKey(const Key('overlay-confetti')), findsOneWidget);
    expect(find.byKey(const Key('overlay-snow')), findsNothing);
    await tester.tap(find.text('Chỉnh'));
    await tester.pumpAndSettle(const Duration(milliseconds: 100));
    await tester.drag(find.byKey(const Key('overlay-density')), const Offset(60, 0));
    await tester.pump();
    expect(patches.last['density'], greaterThan(1.0));
    await tester.ensureVisible(find.byKey(const Key('overlay-color-#8ecae6')));
    await tester.pump();
    await tester.tap(find.byKey(const Key('overlay-color-#8ecae6')));
    await tester.pump();
    expect(patches.last, {'color': '#8ecae6'});
    await tester.ensureVisible(find.byKey(const Key('overlay-full')));
    await tester.pump();
    await tester.tap(find.byKey(const Key('overlay-full')));
    expect(full, 1);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  }, skip: skip != null);
}
