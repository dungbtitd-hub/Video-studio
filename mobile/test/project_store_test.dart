import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:video_studio/core/project_store.dart';

void main() {
  late Directory tmp;
  late ProjectStore store;
  setUp(() async {
    tmp = await Directory.systemTemp.createTemp('vs_store');
    store = ProjectStore(Directory('${tmp.path}/VideoStudio'));
  });
  tearDown(() => tmp.delete(recursive: true));

  test('new project paths are unique and sanitized', () async {
    final a = await store.newProjectPath('Phim: Tết/2027');
    expect(a, endsWith('/VideoStudio/Phim_ Tết_2027.vstudio'));
    await Directory(a).create(recursive: true);
    expect(await store.newProjectPath('Phim: Tết/2027'), endsWith('/VideoStudio/Phim_ Tết_2027 2.vstudio'));
  });

  test('media is copied once into media/, never half-written', () async {
    final src = File('${tmp.path}/picker_cache/clip.mp4')..createSync(recursive: true);
    src.writeAsBytesSync(List.filled(1 << 20, 7));
    final kept = await store.adoptMedia(src.path);
    expect(kept, '${store.mediaDir.path}/clip.mp4');
    expect(File(kept).lengthSync(), 1 << 20);
    expect(await store.adoptMedia(src.path), kept); // same file picked again: reused
    expect(await store.adoptMedia(kept), kept); // already in media/
    final other = File('${tmp.path}/picker_cache/x/clip.mp4')..createSync(recursive: true);
    other.writeAsBytesSync([1, 2, 3]);
    expect(await store.adoptMedia(other.path), '${store.mediaDir.path}/clip (2).mp4');
    expect(store.mediaDir.listSync().where((e) => e.path.endsWith('.part')), isEmpty);
  });

  test('list and last project only report saved packages', () async {
    final a = await store.newProjectPath('A');
    expect(await store.list(), isEmpty);
    await store.setLastProject(a);
    expect(await store.lastProject(), isNull); // never saved
    File('$a/project.json')
      ..createSync(recursive: true)
      ..writeAsStringSync('{}');
    final b = await store.newProjectPath('B');
    File('$b/project.json')
      ..createSync(recursive: true)
      ..writeAsStringSync('{}');
    File('$a/project.json').setLastModifiedSync(DateTime(2020));
    expect((await store.list()).map((p) => p.name), ['B', 'A']);
    expect(await store.lastProject(), a);
  });
}
