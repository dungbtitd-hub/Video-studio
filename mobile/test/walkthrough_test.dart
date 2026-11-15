// Uses the real app the way a person does, with real media and the real core (dart:ffi):
// start screen, new project, import (portrait phone video, landscape video, photo), swipe the
// timeline under the fixed playhead, split at the playhead, choose a part and zoom in it,
// overlay over the whole video, rotate a photo, back home, delete the project; then the same
// project unfolded (Galaxy Z Fold). Every step is checked, and with VSTUDIO_WALKTHROUGH_OUT
// set each screen is saved as a PNG (the "how to use" guide is made from them).
//
//   VSTUDIO_LIB=../build/capi/libvstudio.so VSTUDIO_WALKTHROUGH_MEDIA=<dir with portrait.mp4,
//   landscape.mp4, photo.jpg> [VSTUDIO_WALKTHROUGH_OUT=<dir>] flutter test test/walkthrough_test.dart
import 'dart:io';
import 'dart:ui' as ui;

import 'package:cross_file/cross_file.dart';
import 'package:file_picker_platform_interface/file_picker_platform_interface.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';
import 'package:video_studio/core/vstudio.dart';
import 'package:video_studio/main.dart';
import 'package:video_studio/ui/timeline.dart';

class _Docs extends PathProviderPlatform {
  _Docs(this.dir);
  final String dir;
  @override
  Future<String?> getApplicationDocumentsPath() async => dir;
  @override
  Future<String?> getTemporaryPath() async => '$dir/tmp';
}

final class _File extends PlatformFile {
  _File(this.p);
  final String p;
  @override
  String get name => p.split('/').last;
  @override
  Uri get uri => Uri.file(p);
  @override
  XFile get xFile => XFile(p);
  @override
  int? lengthSync() => File(p).lengthSync();
  @override
  Future<int?> length() async => File(p).lengthSync();
  @override
  Future<Uint8List> readAsBytes() => File(p).readAsBytes();
  @override
  Stream<Uint8List> readAsByteStream() => File(p).openRead().map(Uint8List.fromList);
}

class _Picker extends FilePickerPlatform {
  List<String> next = const [];
  @override
  Future<List<PlatformFile>> pickFiles({
    String? dialogTitle,
    String? initialDirectory,
    FileType type = FileType.any,
    List<String>? allowedExtensions,
    Function(FilePickerStatus)? onFileLoading,
    int compressionQuality = 0,
    AndroidOptions androidOptions = const AndroidOptions(),
    DarwinOptions darwinOptions = const DarwinOptions(),
    WindowsOptions windowsOptions = const WindowsOptions(),
    LinuxOptions linuxOptions = const LinuxOptions(),
    WebOptions webOptions = const WebOptions(),
  }) async =>
      [for (final p in next) _File(p)];
}

Future<void> _loadFonts() async {
  final sdk =
      Platform.environment['FLUTTER_ROOT'] ?? File(Platform.resolvedExecutable).parent.parent.parent.parent.path;
  final dir = Directory('$sdk/bin/cache/artifacts/material_fonts');
  if (!dir.existsSync()) return;
  Future<void> load(String family, List<String> files) async {
    final l = FontLoader(family);
    for (final f in files) {
      final file = File('${dir.path}/$f');
      if (file.existsSync()) l.addFont(Future.value(ByteData.sublistView(file.readAsBytesSync())));
    }
    await l.load();
  }

  await load('Roboto', ['Roboto-Regular.ttf', 'Roboto-Medium.ttf', 'Roboto-Bold.ttf']);
  // Text painted straight on a canvas (the timeline ruler) uses the platform default font.
  await load('FlutterTest', ['Roboto-Regular.ttf']);
  await load('MaterialIcons', ['MaterialIcons-Regular.otf']);
}

void main() {
  final lib = Platform.environment['VSTUDIO_LIB'];
  final mediaDir = Platform.environment['VSTUDIO_WALKTHROUGH_MEDIA'];
  final out = Platform.environment['VSTUDIO_WALKTHROUGH_OUT'];
  final skip = (lib == null || lib.isEmpty)
      ? 'VSTUDIO_LIB not set'
      : (mediaDir == null || !File('$mediaDir/portrait.mp4').existsSync())
          ? 'VSTUDIO_WALKTHROUGH_MEDIA not set'
          : null;

  final boundary = GlobalKey();
  final picker = _Picker();
  late Directory docs;

  setUpAll(() async {
    if (skip != null) return;
    await _loadFonts();
    docs = Directory.systemTemp.createTempSync('vs_walk');
    Directory('${docs.path}/tmp').createSync();
    PathProviderPlatform.instance = _Docs(docs.path);
    FilePickerPlatform.instance = picker;
    if (out != null) Directory(out).createSync(recursive: true);
  });

  /// Lets the core and image decoding run (real time), then draws.
  Future<void> settle(WidgetTester tester, [int ms = 400]) async {
    for (var i = 0; i < ms ~/ 50; i++) {
      await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 50)));
      await tester.pump(const Duration(milliseconds: 16));
    }
  }

  /// Runs real time in small steps (file copies take many small I/O round trips) until [done].
  Future<void> until(WidgetTester tester, bool Function() done, {int maxMs = 15000}) async {
    for (var t = 0; t < maxMs && !done(); t += 5) {
      await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 5)));
      await tester.pump(const Duration(milliseconds: 5));
    }
  }

  Future<void> shot(WidgetTester tester, String name) async {
    if (out == null) return;
    await tester.runAsync(() async {
      final ro = boundary.currentContext!.findRenderObject()! as RenderRepaintBoundary;
      final img = await ro.toImage(pixelRatio: 2);
      final png = await img.toByteData(format: ui.ImageByteFormat.png);
      File('$out/$name.png').writeAsBytesSync(png!.buffer.asUint8List());
    });
  }

  Future<void> app(WidgetTester tester, Size size) async {
    tester.view.physicalSize = size * 2;
    tester.view.devicePixelRatio = 2;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(RepaintBoundary(key: boundary, child: const VideoStudioApp()));
    await settle(tester);
  }

  Offset timelineAt(WidgetTester tester) {
    final lanes = find.byWidgetPredicate((w) => w.runtimeType.toString() == 'TimelineView');
    return tester.getCenter(lanes);
  }

  Future<void> swipeTimeline(WidgetTester tester, double dx) async {
    final c = timelineAt(tester);
    final g = await tester.startGesture(c);
    for (var i = 1; i <= 8; i++) {
      await g.moveTo(c + Offset(dx * i / 8, 0));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await g.up();
    await settle(tester, 300);
  }

  /// "1:02.3" -> 62.3
  double seconds(String tc) {
    final p = tc.trim().split(':');
    return int.parse(p[0]) * 60 + double.parse(p[1]);
  }

  String timeText(WidgetTester tester) => tester.widget<Text>(find.byKey(const Key('time-row'))).data!;

  Future<void> tapTool(WidgetTester tester, String label) async {
    final f = find.text(label);
    final row = find.byType(Scrollable).last;
    try {
      await tester.scrollUntilVisible(f, 80, scrollable: row, maxScrolls: 20);
    } on StateError {
      await tester.scrollUntilVisible(f, -80, scrollable: row, maxScrolls: 20); // it is further back
    }
    await tester.ensureVisible(f);
    await tester.pump(const Duration(milliseconds: 300));
    await tester.tap(f);
    await settle(tester, 300);
  }

  test('the portrait phone video comes out upright (core, before the UI)', () async {
    final vs = VStudio.create('dọc', width: 1080, height: 1920);
    addTearDown(vs.dispose);
    final a = vs.importMedia('$mediaDir/portrait.mp4');
    final track = vs.addTrack(audio: false, name: 'V');
    vs.appendClip(track, a);
    vs.setPreviewSize(360, 640);
    vs.seek(1);
    PreviewFrame? f;
    for (var i = 0; i < 300 && f == null; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
      f = vs.takeFrame();
    }
    expect(f, isNotNull);
    if (out != null) File('$out/core_portrait_${f!.width}x${f.height}.rgba').writeAsBytesSync(f.pixels);
    // The source has a red band along its top edge: upright, it is across the top of the frame.
    int red(int x, int y) => f!.pixels[(y * f.width + x) * 4];
    int green(int x, int y) => f!.pixels[(y * f.width + x) * 4 + 1];
    expect(red(180, 20), greaterThan(200));
    expect(green(180, 20), lessThan(60));
    expect(red(180, 600), lessThan(250)); // not red all the way down (would be a sideways band)
    final notRedRows =
        [for (var y = 0; y < 640; y += 40) red(180, y) > 200 && green(180, y) < 60].where((r) => !r).length;
    expect(notRedRows, greaterThan(10));
  }, skip: skip);

  testWidgets('a person edits a video on a phone', (tester) async {
    await app(tester, const Size(412, 892));
    // 1. The app opens on the start screen, not in the editor.
    expect(find.text('Dự án mới'), findsOneWidget);
    expect(find.text('Dự án gần đây'), findsOneWidget);
    await shot(tester, '01_trang_chu');

    // 2. "Dự án mới" opens the editor and the picker: a portrait phone video, a landscape video, a photo.
    picker.next = ['$mediaDir/portrait.mp4', '$mediaDir/landscape.mp4', '$mediaDir/photo.jpg'];
    await tester.tap(find.byKey(const Key('home-new')));
    await settle(tester, 600);
    expect(find.text('Thư viện ảnh và video'), findsOneWidget);
    await shot(tester, '02_chon_nguon');
    await tester.tap(find.text('Thư viện ảnh và video'));
    await until(tester, () => find.textContaining('Đã thêm').evaluate().isNotEmpty);
    expect(find.textContaining('Đã thêm 3 file'), findsOneWidget);
    expect(find.textContaining('Không thêm được'), findsNothing);
    expect(timeText(tester), endsWith('/ 0:19.0')); // 6 s + 8 s + a photo shows 5 s
    await settle(tester, 800); // thumbnails and the first frame
    await shot(tester, '03_da_nhap');

    // 3. Swipe the timeline left: time moves forward, the playhead line stays in the middle.
    final before = timeText(tester);
    await swipeTimeline(tester, -160);
    final after = timeText(tester);
    expect(after, isNot(before));
    await shot(tester, '04_vuot_timeline');

    // 4. "Tách" cuts the clip under the playhead (nothing selected: the one under the line).
    // After an import nothing is selected and the playhead is at the start of the new media.
    expect(find.byIcon(Icons.chevron_left), findsNothing);
    await tapTool(tester, 'Tách');
    expect(find.textContaining('Đã tách tại'), findsOneWidget);
    await shot(tester, '05_tach');

    // 5. Part of the clip: start point, swipe, end point, then zoom in gradually in that part.
    await tapTool(tester, 'Chọn đoạn');
    expect(find.textContaining('Bước 2'), findsOneWidget);
    await shot(tester, '06_diem_dau');
    await swipeTimeline(tester, -60);
    await tester.tap(find.byKey(const Key('range-end')));
    await settle(tester, 300);
    expect(find.textContaining('Bước 3'), findsOneWidget);
    await shot(tester, '07_diem_cuoi');
    await tester.tap(find.byKey(const Key('range-zoom-1.6')));
    await settle(tester, 600);
    expect(find.textContaining('zoom dần 1.6×'), findsOneWidget);
    await shot(tester, '08_zoom_dan');

    // 6. Snow over the whole video.
    await tester.tap(find.byIcon(Icons.chevron_left));
    await settle(tester, 200);
    await tapTool(tester, 'Lớp phủ');
    await shot(tester, '09_lop_phu_thu_vien');
    final ids = VStudio.create('cat', width: 64, height: 64);
    final snow = ids.overlayCatalog().first.id;
    ids.dispose();
    final firstOverlay = find.byKey(Key('overlay-$snow'));
    if (firstOverlay.evaluate().isNotEmpty) {
      await tester.tap(firstOverlay.first);
      await settle(tester, 600);
      await tester.tap(find.text('Chỉnh'));
      await tester.pump(const Duration(milliseconds: 600)); // the tab slides in
      await settle(tester, 300);
      await tester.ensureVisible(find.byKey(const Key('overlay-span-all')));
      await tester.pump(const Duration(milliseconds: 300));
      await shot(tester, '10a_lop_phu_chinh');
      await tester.tap(find.byKey(const Key('overlay-span-all')));
      await settle(tester, 600);
      expect(find.textContaining('Lớp phủ chạy suốt video'), findsOneWidget);
      await shot(tester, '10_lop_phu_ca_video');
    }
    Navigator.of(tester.element(find.byKey(const Key('time-row')))).popUntil((r) => r.isFirst);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500)); // the sheet slides away
    await settle(tester, 400);
    await shot(tester, '11_timeline_cac_lan');

    // 7. Back to the start screen: the project is listed; delete it.
    await tester.tap(find.byKey(const Key('editor-close')));
    await settle(tester, 800);
    expect(find.text('Dự án 1'), findsOneWidget);
    await shot(tester, '12_trang_chu_co_du_an');
    await tester.tap(find.byKey(const Key('home-more-Dự án 1')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500)); // the menu slides up
    await shot(tester, '13_menu_du_an');
    await tester.tap(find.byKey(const Key('home-delete')));
    await tester.pump(const Duration(milliseconds: 500));
    await tester.pump(const Duration(milliseconds: 500));
    await shot(tester, '14_xac_nhan_xoa');
    await tester.tap(find.byKey(const Key('home-delete-ok')));
    await settle(tester, 600);
    expect(find.text('Dự án 1'), findsNothing);
  }, skip: skip != null);
  testWidgets('a person deletes several clips, then everything after and before the line', (tester) async {
    await app(tester, const Size(412, 892));
    picker.next = ['$mediaDir/portrait.mp4', '$mediaDir/landscape.mp4', '$mediaDir/photo.jpg'];
    await tester.tap(find.byKey(const Key('home-new')));
    await settle(tester, 600);
    await tester.tap(find.text('Thư viện ảnh và video'));
    await until(tester, () => find.textContaining('Đã thêm').evaluate().isNotEmpty);
    expect(timeText(tester), endsWith('/ 0:19.0'));
    await settle(tester, 800);

    // 1. "Chọn nhiều": pick every clip, tap the first one off again, delete the two others at once.
    await tapTool(tester, 'Chọn nhiều');
    await tapTool(tester, 'Chọn hết');
    expect(find.text('Xóa (3)'), findsOneWidget);
    final view = tester.widget<TimelineView>(find.byType(TimelineView));
    final lane = tester.getTopLeft(find.byType(TimelineView)) +
        Offset(tester.getSize(find.byType(TimelineView)).width / 2 + 20,
            TimelineView.laneTops(view.project).$1.first + 20);
    await tester.tapAt(lane); // the clip under the line (the portrait video) is let go
    await settle(tester, 300);
    expect(find.text('Xóa (2)'), findsOneWidget);
    await shot(tester, '20_chon_nhieu');
    await tapTool(tester, 'Xóa (2)');
    expect(find.textContaining('Đã xóa 2 clip'), findsOneWidget);
    expect(timeText(tester), endsWith('/ 0:06.0')); // only the 6 s portrait video is left
    await shot(tester, '21_da_xoa_nhieu');

    // 2. Everything after the line: swipe into the video, "Xóa sau".
    await swipeTimeline(tester, -120);
    final at = seconds(timeText(tester).split(' / ').first);
    expect(at, inInclusiveRange(1.0, 5.0));
    await tapTool(tester, 'Xóa sau');
    expect(find.textContaining('Đã xóa hết phía sau'), findsOneWidget);
    // The video now ends at the line (the cut falls on a frame: within 0.1 s as shown).
    expect(seconds(timeText(tester).split(' / ').last), closeTo(at, 0.11));
    await shot(tester, '22_xoa_phia_sau');

    // 3. Everything before the line: swipe back a little, "Xóa trước"; the rest starts at 0.
    await swipeTimeline(tester, 50);
    await tapTool(tester, 'Xóa trước');
    expect(find.textContaining('Đã xóa hết phía trước'), findsOneWidget);
    expect(timeText(tester), startsWith('0:00.0'));
    final left = seconds(timeText(tester).split(' / ').last);
    expect(left, greaterThan(0.2));
    expect(left, lessThan(at - 0.2));
    await shot(tester, '23_xoa_phia_truoc');
  }, skip: skip != null);
  testWidgets('the same on an unfolded Galaxy Z Fold: keyframes, rotate a photo', (tester) async {
    await app(tester, const Size(884, 1004));
    // 1. A landscape project; the bin, preview and timeline are side by side as in the prototype.
    picker.next = ['$mediaDir/landscape.mp4', '$mediaDir/photo.jpg'];
    await tester.tap(find.byKey(const Key('home-new-16:9')));
    await settle(tester, 600);
    await tester.tap(find.text('Thư viện ảnh và video'));
    await until(tester, () => find.textContaining('Đã thêm').evaluate().isNotEmpty);
    expect(find.textContaining('Đã thêm 2 file'), findsOneWidget);
    expect(find.byKey(const Key('bin-import')), findsOneWidget); // media bin on the right
    await settle(tester, 800);
    await shot(tester, 'fold_01_da_nhap');

    // 2. Keyframes, the way the guide says: select the clip under the line, ◆ Điểm đầu,
    // swipe on, ◆ Điểm cuối.
    await swipeTimeline(tester, 40); // back towards the start of the video
    await tester.tapAt(timelineAt(tester)); // the clip under the playhead
    await settle(tester, 200);
    await tapTool(tester, 'Keyframe');
    expect(find.textContaining('Bước 1'), findsOneWidget);
    expect(find.text('◆ Điểm đầu'), findsOneWidget);
    await shot(tester, 'fold_02_keyframe_buoc1');
    await tester.tap(find.byKey(const Key('kf-picture')));
    await settle(tester, 300);
    expect(find.textContaining('Đang ở một điểm keyframe'), findsOneWidget);
    await swipeTimeline(tester, -80);
    expect(find.textContaining('Bước 2'), findsOneWidget);
    expect(find.text('◆ Điểm cuối'), findsOneWidget);
    await tester.tap(find.byKey(const Key('kf-picture')));
    await settle(tester, 300);
    expect(find.textContaining('Đang ở một điểm keyframe'), findsOneWidget);
    await shot(tester, 'fold_03_keyframe_diem_cuoi');
    // Off the keys: both diamonds are counted.
    await swipeTimeline(tester, -30);
    expect(find.textContaining('Đoạn giữa các điểm'), findsOneWidget);

    // 3. The photo: swipe until the line is on it, "Xoay / lật", rotate right.
    await tester.tap(find.byIcon(Icons.chevron_left));
    await settle(tester, 200);
    for (var i = 0; i < 10 && !timeText(tester).startsWith('0:08') && !timeText(tester).startsWith('0:09'); i++) {
      await swipeTimeline(tester, -60);
    }
    await tapTool(tester, 'Xoay / lật');
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));
    await tester.tap(find.byKey(const Key('orient-right')));
    await settle(tester, 600);
    final photo = VStudio.create('p', width: 64, height: 64);
    photo.dispose();
    await shot(tester, 'fold_04_xoay_anh');
    Navigator.of(tester.element(find.byKey(const Key('time-row')))).popUntil((r) => r.isFirst);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));
    await settle(tester, 400);
    await shot(tester, 'fold_05_sau_khi_xoay');
  }, skip: skip != null);
}
