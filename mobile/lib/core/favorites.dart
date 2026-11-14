// Favourites (FR-022): looks, transitions, effects... starred by the user, kept in one small
// JSON file in the app's data folder ({"look": ["id", ...], ...}, newest first; the same
// format as the desktop's favorites.json). Loaded once, written on every change.
import 'dart:convert';
import 'dart:io';

class FavoritesStore {
  FavoritesStore._(this._file, this._items);

  static FavoritesStore? _shared;

  /// The app-wide store; [dir] is the app support folder (set at start-up).
  static FavoritesStore get instance => _shared ??= FavoritesStore._(null, {});
  static void init(String dir) => _shared = FavoritesStore.load('$dir/favorites.json');

  factory FavoritesStore.load(String path) {
    final items = <String, List<String>>{};
    try {
      final f = File(path);
      if (f.existsSync()) {
        final j = jsonDecode(f.readAsStringSync()) as Map<String, dynamic>;
        for (final e in j.entries) {
          if (e.value is List) items[e.key] = [for (final x in e.value as List) if (x is String) x];
        }
      }
    } catch (_) {
      items.clear(); // broken file: start empty, rewritten on the next change
    }
    return FavoritesStore._(path, items);
  }

  final String? _file;
  final Map<String, List<String>> _items;

  bool contains(String category, String id) => _items[category]?.contains(id) ?? false;
  List<String> list(String category) => List.unmodifiable(_items[category] ?? const []);

  /// Adds or removes; returns the new state.
  bool toggle(String category, String id) {
    final l = _items.putIfAbsent(category, () => []);
    final on = !l.remove(id);
    if (on) l.insert(0, id);
    _save();
    return on;
  }

  /// [ids] with the favourites first (most recently starred first), the rest in order.
  List<T> ordered<T>(String category, List<T> items, String Function(T) idOf) {
    final fav = list(category);
    final first = [for (final id in fav) ...items.where((x) => idOf(x) == id)];
    return [...first, ...items.where((x) => !fav.contains(idOf(x)))];
  }

  void _save() {
    final f = _file;
    if (f == null) return;
    try {
      final tmp = File('$f.tmp');
      tmp.parent.createSync(recursive: true);
      tmp.writeAsStringSync(jsonEncode(_items));
      tmp.renameSync(f);
    } catch (_) {
      // a convenience: never break editing over it
    }
  }
}
