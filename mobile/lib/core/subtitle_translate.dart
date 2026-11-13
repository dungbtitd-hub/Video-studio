// AI translation of a subtitle track on the phone. The core builds every request and reads every
// reply (vs_translation_*); this file only sends the bytes with dart:io, a few batches at once,
// retrying a batch twice before giving up on it. Connector settings (address, model, key) are
// kept in the app's own folder, never in the project.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:path_provider/path_provider.dart';

import 'vstudio.dart';

/// Sends one request; returns the status code and body. Replaceable in tests.
typedef HttpSend = Future<(int, String)> Function(TranslationRequest r);

Future<(int, String)> httpSend(TranslationRequest r) async {
  final client = HttpClient()..connectionTimeout = const Duration(seconds: 20);
  try {
    final req = await client.openUrl(r.method, Uri.parse(r.url));
    for (final (k, v) in r.headers) {
      req.headers.set(k, v);
    }
    req.add(utf8.encode(r.body));
    final res = await req.close().timeout(const Duration(minutes: 3));
    final body = await res.transform(utf8.decoder).join().timeout(const Duration(minutes: 3));
    return (res.statusCode, body);
  } finally {
    client.close(force: true);
  }
}

class TranslationOutcome {
  TranslationOutcome({required this.failed, required this.lastError, required this.cancelled});
  final int failed; // batches that kept their source text
  final String lastError;
  final bool cancelled;
}

/// Runs every batch of [t]. [progress] gets (done, total) after each batch.
class TranslationRunner {
  TranslationRunner(this.t, {this.send = httpSend, this.parallel = 3, this.maxAttempts = 3});
  final SubtitleTranslation t;
  final HttpSend send;
  final int parallel, maxAttempts;
  bool _cancel = false;

  void cancel() => _cancel = true;

  Future<TranslationOutcome> run({void Function(int done, int total)? progress}) async {
    final total = t.count;
    var next = 0, failed = 0;
    var lastError = '';
    Future<void> worker() async {
      while (!_cancel) {
        if (next >= total) return;
        final i = next++;
        var ok = false;
        for (var attempt = 1; attempt <= maxAttempts && !_cancel && !ok; attempt++) {
          String? err;
          try {
            final (status, body) = await send(t.request(i));
            err = body.isEmpty ? 'HTTP $status' : t.accept(i, body);
            if (err != null && status >= 400) err = 'HTTP $status: $err';
          } on VStudioException catch (e) {
            err = e.message;
            attempt = maxAttempts; // the config is unusable: no retry
          } catch (e) {
            err = e.toString();
          }
          if (err == null) {
            ok = true;
          } else {
            lastError = err;
            if (err.startsWith('HTTP 401') || err.startsWith('HTTP 403') || err.startsWith('HTTP 404')) break;
            if (attempt < maxAttempts) await Future<void>.delayed(Duration(milliseconds: 1500 * attempt));
          }
        }
        if (!ok && !_cancel) failed++;
        progress?.call(t.done, total);
      }
    }

    await Future.wait([for (var k = 0; k < parallel; k++) worker()]);
    return TranslationOutcome(failed: failed, lastError: lastError, cancelled: _cancel);
  }
}

/// Connector settings per preset id, plus the last choices, in the app support folder.
class TranslationSettings {
  TranslationSettings(this.data);
  final Map<String, dynamic> data;

  static Future<File> _file() async => File('${(await getApplicationSupportDirectory()).path}/translate.json');

  static Future<TranslationSettings> load() async {
    try {
      final f = await _file();
      if (await f.exists()) return TranslationSettings(jsonDecode(await f.readAsString()) as Map<String, dynamic>);
    } catch (_) {}
    return TranslationSettings({});
  }

  Future<void> save() async {
    try {
      final f = await _file();
      await f.writeAsString(jsonEncode(data));
    } catch (_) {}
  }

  Map<String, dynamic> preset(String id) =>
      (data['presets'] as Map<String, dynamic>? ?? const {})[id] as Map<String, dynamic>? ?? {};
  void setPreset(String id, Map<String, dynamic> v) {
    final all = Map<String, dynamic>.of(data['presets'] as Map<String, dynamic>? ?? const {});
    all[id] = v;
    data['presets'] = all;
  }
}
