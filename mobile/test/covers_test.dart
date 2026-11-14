// Cover region on the preview without the core: one finger moves it, the
// corner handle resizes it, and each drag is reported once, in canvas 0..1.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/ui/cover_panel.dart';

void main() {
  Future<List<Rect>> pumpOverlay(WidgetTester tester) async {
    final changes = <Rect>[];
    await tester.pumpWidget(MaterialApp(
      home: Center(
        child: SizedBox(
          width: 400,
          height: 300, // a 16:9 canvas letterboxed: 400 x 225 from y = 37.5
          child: CoverOverlay(
            canvasSize: const Size(1920, 1080),
            rect: const Rect.fromLTWH(0.25, 0.2, 0.5, 0.4),
            shape: 'rect',
            onChanged: changes.add,
          ),
        ),
      ),
    ));
    return changes;
  }

  testWidgets('one finger moves the cover; one change per drag', (tester) async {
    final changes = await pumpOverlay(tester);
    final box = tester.getRect(find.byType(CoverOverlay));
    final g = await tester.startGesture(box.topLeft + const Offset(200, 120));
    for (var i = 1; i <= 4; i++) {
      await g.moveBy(const Offset(10, 5.625));
      await tester.pump();
    }
    expect(changes, isEmpty); // nothing committed while the finger is down
    await g.up();
    await tester.pump();
    expect(changes, hasLength(1));
    final r = changes.single;
    // 40 px of 400 = 0.1 canvas widths, 22.5 px of 225 = 0.1 canvas heights.
    expect(r.left, closeTo(0.35, 0.02));
    expect(r.top, closeTo(0.3, 0.02));
    expect(r.width, closeTo(0.5, 1e-6));
    expect(r.height, closeTo(0.4, 1e-6));
  });

  testWidgets('the corner handle resizes, the opposite corner stays', (tester) async {
    final changes = await pumpOverlay(tester);
    final box = tester.getRect(find.byType(CoverOverlay));
    // Bottom-right of the cover: (0.75 * 400, 37.5 + 0.6 * 225).
    final corner = box.topLeft + const Offset(300, 172.5);
    final g = await tester.startGesture(corner);
    for (var i = 1; i <= 4; i++) {
      await g.moveBy(const Offset(-10, -5.625));
      await tester.pump();
    }
    await g.up();
    await tester.pump();
    final r = changes.single;
    expect(r.left, closeTo(0.25, 1e-6));
    expect(r.top, closeTo(0.2, 1e-6));
    expect(r.width, closeTo(0.4, 0.02));
    expect(r.height, closeTo(0.3, 0.02));
  });
}
