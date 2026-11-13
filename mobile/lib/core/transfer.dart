// Moving a project between devices as one ".vstudiopkg" file (project + media).
// Packing and unpacking copy gigabytes, so they run on a background isolate
// with their own core session; the UI isolate only receives progress.
import 'dart:convert';
import 'dart:ffi';
import 'dart:isolate';

import 'package:ffi/ffi.dart';

import 'vstudio_bindings.dart';

class TransferResult {
  TransferResult.fromJson(Map<String, dynamic> j)
      : ok = j['ok'] == true,
        cancelled = j['cancelled'] == true,
        error = (j['error'] as String?) ?? '',
        missing = [for (final m in (j['missing'] as List? ?? const [])) m as String],
        packageDir = (j['package_dir'] as String?) ?? '';
  TransferResult.failed(this.error)
      : ok = false,
        cancelled = false,
        missing = const [],
        packageDir = '';
  final bool ok, cancelled;
  final String error;
  final List<String> missing; // pack: asset ids whose media was not found (left out)
  final String packageDir; // unpack: the new project's .vstudio folder
}

/// Cancels a running transfer. The flag lives in native memory so the busy
/// background isolate can read it between chunks.
class TransferCancel {
  final Pointer<Int32> _flag = calloc<Int32>();
  void cancel() => _flag.value = 1;
  int get _address => _flag.address;
  void dispose() => calloc.free(_flag);
}

typedef _ProgressC = Int32 Function(Double, Pointer<Void>);
typedef _PackC = CStr Function(Pointer<VsSession>, CStr, Pointer<NativeFunction<_ProgressC>>, Pointer<Void>);
typedef _PackD = CStr Function(Pointer<VsSession>, CStr, Pointer<NativeFunction<_ProgressC>>, Pointer<Void>);
typedef _UnpackC = CStr Function(CStr, CStr, Pointer<NativeFunction<_ProgressC>>, Pointer<Void>);

/// Unpacks `packageFile` into `destRoot` (project folder + media/ beside it).
Future<TransferResult> unpackPackage(String packageFile, String destRoot,
        {void Function(double)? onProgress, TransferCancel? cancel}) =>
    _run(onProgress, cancel, (lib, cb) {
      final unpack = lib.lookupFunction<_UnpackC, _UnpackC>('vs_transfer_unpack');
      return using(
          (a) => unpack(packageFile.toNativeUtf8(allocator: a), destRoot.toNativeUtf8(allocator: a), cb, nullptr));
    });

/// Packs the saved project at `packageDir` into `outFile`. Save first: the
/// background isolate opens its own session, so editing can continue.
Future<TransferResult> packSavedProject(String packageDir, String outFile,
        {void Function(double)? onProgress, TransferCancel? cancel}) =>
    _run(onProgress, cancel, (lib, cb) {
      final open =
          lib.lookupFunction<Pointer<VsSession> Function(CStr), Pointer<VsSession> Function(CStr)>('vs_session_open');
      final destroy = lib
          .lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>('vs_session_destroy');
      final pack = lib.lookupFunction<_PackC, _PackD>('vs_transfer_pack');
      return using((a) {
        final s = open(packageDir.toNativeUtf8(allocator: a));
        if (s == nullptr) return nullptr;
        try {
          return pack(s, outFile.toNativeUtf8(allocator: a), cb, nullptr);
        } finally {
          destroy(s);
        }
      });
    });

Future<TransferResult> _run(void Function(double)? onProgress, TransferCancel? cancel,
    CStr Function(DynamicLibrary lib, Pointer<NativeFunction<_ProgressC>> cb) call) async {
  final port = ReceivePort();
  port.listen((m) {
    if (m is double) onProgress?.call(m);
  });
  final send = port.sendPort;
  final cancelAddress = cancel?._address ?? 0;
  try {
    final json = await Isolate.run(() {
      final lib = openVStudioLibrary();
      final free = lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('vs_free');
      var lastPercent = -1;
      // Called synchronously on this isolate's thread while the core works.
      final cb = NativeCallable<_ProgressC>.isolateLocal((double p, Pointer<Void> _) {
        final pct = (p * 100).floor();
        if (pct != lastPercent) {
          lastPercent = pct;
          send.send(p);
        }
        return cancelAddress != 0 && Pointer<Int32>.fromAddress(cancelAddress).value != 0 ? 1 : 0;
      }, exceptionalReturn: 1);
      try {
        final r = call(lib, cb.nativeFunction);
        if (r == nullptr) return '{"ok":false,"error":"cannot open project"}';
        final s = r.toDartString();
        free(r.cast());
        return s;
      } finally {
        cb.close();
      }
    });
    return TransferResult.fromJson(jsonDecode(json) as Map<String, dynamic>);
  } catch (e) {
    return TransferResult.failed('$e');
  } finally {
    port.close();
  }
}
