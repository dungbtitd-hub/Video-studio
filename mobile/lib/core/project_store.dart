// Where mobile projects live on the device:
//   <documents>/VideoStudio/
//     media/              imported files, copied here (the picker's copies are temporary)
//     Name.vstudio/       projects
//     last_project        path of the project to reopen at start
// The core stores media paths relative to the package's parent folder, so
// every project refers to "media/x.mp4". Relative paths keep working when iOS
// moves the app container (updates, restores from backup).
import 'dart:io';

class ProjectInfo {
  ProjectInfo(this.path, this.name, this.modified);
  final String path;
  final String name;
  final DateTime modified;
}

class ProjectStore {
  ProjectStore(this.root);
  final Directory root;

  Directory get mediaDir => Directory('${root.path}/media');
  Directory get projectsDir => root; // packages sit next to media/ so media paths stay relative
  File get _last => File('${root.path}/last_project');

  Future<void> ensure() async {
    await mediaDir.create(recursive: true);
  }

  /// A new, unused package path for [name] (the folder is created by the first save).
  Future<String> newProjectPath(String name) async {
    await ensure();
    final base = _safe(name);
    var path = '${projectsDir.path}/$base.vstudio';
    for (var i = 2; await Directory(path).exists(); i++) {
      path = '${projectsDir.path}/$base $i.vstudio';
    }
    return path;
  }

  /// Saved projects, most recently modified first.
  Future<List<ProjectInfo>> list() async {
    if (!await projectsDir.exists()) return [];
    final out = <ProjectInfo>[];
    await for (final e in projectsDir.list()) {
      if (e is! Directory || !e.path.endsWith('.vstudio')) continue;
      final json = File('${e.path}/project.json');
      if (!await json.exists()) continue;
      final name = e.uri.pathSegments.where((s) => s.isNotEmpty).last.replaceAll('.vstudio', '');
      out.add(ProjectInfo(e.path, name, await json.lastModified()));
    }
    out.sort((a, b) => b.modified.compareTo(a.modified));
    return out;
  }

  Future<String?> lastProject() async {
    if (!await _last.exists()) return null;
    final p = (await _last.readAsString()).trim();
    return p.isNotEmpty && await File('$p/project.json').exists() ? p : null;
  }

  Future<void> setLastProject(String path) async {
    await ensure();
    await _last.writeAsString(path);
  }

  /// Copies a picked file into media/ (unique name) unless it is already there.
  /// Streams the copy, so multi-GB videos do not load into memory.
  Future<String> adoptMedia(String path) async {
    await ensure();
    final src = File(path);
    if (src.absolute.parent.path == mediaDir.absolute.path) return src.path;
    final name = src.uri.pathSegments.last;
    final dot = name.lastIndexOf('.');
    final stem = dot > 0 ? name.substring(0, dot) : name, ext = dot > 0 ? name.substring(dot) : '';
    var dest = File('${mediaDir.path}/$name');
    for (var i = 2; await dest.exists(); i++) {
      // Same file picked again: reuse it instead of storing a second copy.
      if (await dest.length() == await src.length()) return dest.path;
      dest = File('${mediaDir.path}/$stem ($i)$ext');
    }
    final tmp = File('${dest.path}.part');
    await src.openRead().pipe(tmp.openWrite());
    await tmp.rename(dest.path); // never leaves a half-copied file under the final name
    return dest.path;
  }

  static String _safe(String name) {
    final s = name.replaceAll(RegExp(r'[\\/:*?"<>|]'), '_').trim();
    return s.isEmpty ? 'Dự án' : s;
  }
}
