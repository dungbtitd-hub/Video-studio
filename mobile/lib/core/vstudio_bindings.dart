// dart:ffi bindings for capi/include/vstudio/vstudio_c.h.
// Keep in sync with the header; the C API is the only contract between
// Flutter and the C++ core.
// ignore_for_file: non_constant_identifier_names, camel_case_types

import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

final class VsSession extends Opaque {}

/// UTF-8 C string as passed across the C API.
typedef CStr = Pointer<Utf8>;

DynamicLibrary openVStudioLibrary() {
  if (Platform.isIOS || Platform.isMacOS) {
    // Linked statically into the app on iOS (see native/README.md).
    return DynamicLibrary.process();
  }
  // Desktop test runs can point at a freshly built library.
  final override = Platform.environment['VSTUDIO_LIB'];
  if (override != null && override.isNotEmpty) return DynamicLibrary.open(override);
  if (Platform.isAndroid || Platform.isLinux) return DynamicLibrary.open('libvstudio.so');
  if (Platform.isWindows) return DynamicLibrary.open('vstudio.dll');
  throw UnsupportedError('Unsupported platform');
}

class VStudioBindings {
  VStudioBindings(DynamicLibrary lib)
      : version = lib.lookupFunction<CStr Function(), CStr Function()>('vs_version'),
        ticksPerSecond = lib.lookupFunction<Int64 Function(), int Function()>('vs_ticks_per_second'),
        free = lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('vs_free'),
        modulesJson = lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>(
            'vs_modules_json'),
        sessionCreate = lib.lookupFunction<
            Pointer<VsSession> Function(CStr, Int32, Int32, Int32, Int32),
            Pointer<VsSession> Function(CStr, int, int, int, int)>('vs_session_create'),
        sessionOpen = lib.lookupFunction<Pointer<VsSession> Function(CStr), Pointer<VsSession> Function(CStr)>(
            'vs_session_open'),
        sessionDestroy = lib.lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>(
            'vs_session_destroy'),
        lastError = lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>(
            'vs_last_error'),
        save = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr), int Function(Pointer<VsSession>, CStr)>(
            'vs_save'),
        autosave = lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>(
            'vs_autosave'),
        projectJson = lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>(
            'vs_project_json'),
        projectRevision = lib.lookupFunction<Int64 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>(
            'vs_project_revision'),
        duration = lib.lookupFunction<Int64 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>(
            'vs_duration'),
        importMedia = lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr),
            CStr Function(Pointer<VsSession>, CStr)>('vs_import_media'),
        addTrack = lib.lookupFunction<CStr Function(Pointer<VsSession>, Int32, CStr),
            CStr Function(Pointer<VsSession>, int, CStr)>('vs_add_track'),
        appendClip = lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr, CStr),
            CStr Function(Pointer<VsSession>, CStr, CStr)>('vs_append_clip'),
        moveClip = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Int64),
            int Function(Pointer<VsSession>, CStr, CStr, int)>('vs_move_clip'),
        trimClip = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32, Int64),
            int Function(Pointer<VsSession>, CStr, int, int)>('vs_trim_clip'),
        splitAt = lib.lookupFunction<Int32 Function(Pointer<VsSession>, Int64), int Function(Pointer<VsSession>, int)>(
            'vs_split_at'),
        removeClip = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr), int Function(Pointer<VsSession>, CStr)>(
            'vs_remove_clip'),
        undo = lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_undo'),
        redo = lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_redo'),
        previewSetSize = lib.lookupFunction<Void Function(Pointer<VsSession>, Int32, Int32),
            void Function(Pointer<VsSession>, int, int)>('vs_preview_set_size'),
        previewSeek = lib.lookupFunction<Void Function(Pointer<VsSession>, Int64), void Function(Pointer<VsSession>, int)>(
            'vs_preview_seek'),
        play = lib.lookupFunction<Void Function(Pointer<VsSession>, Int64), void Function(Pointer<VsSession>, int)>(
            'vs_play'),
        pause = lib.lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>('vs_pause'),
        isPlaying = lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>(
            'vs_is_playing'),
        playhead = lib.lookupFunction<Int64 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>(
            'vs_playhead'),
        previewTakeFrame = lib.lookupFunction<
            Int32 Function(Pointer<VsSession>, Pointer<Uint8>, Int64, Pointer<Int64>, Pointer<Int32>, Pointer<Int32>,
                Pointer<Int64>),
            int Function(Pointer<VsSession>, Pointer<Uint8>, int, Pointer<Int64>, Pointer<Int32>, Pointer<Int32>,
                Pointer<Int64>)>('vs_preview_take_frame'),
        exportStart = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32, Int32, Int32, Int32, Int64),
            int Function(Pointer<VsSession>, CStr, int, int, int, int, int)>('vs_export_start'),
        exportStatus = lib.lookupFunction<Int32 Function(Pointer<VsSession>, Pointer<Double>),
            int Function(Pointer<VsSession>, Pointer<Double>)>('vs_export_status'),
        exportCancel = lib.lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>(
            'vs_export_cancel');

  final CStr Function() version;
  final int Function() ticksPerSecond;
  final void Function(Pointer<Void>) free;
  final CStr Function(Pointer<VsSession>) modulesJson;
  final Pointer<VsSession> Function(CStr, int, int, int, int) sessionCreate;
  final Pointer<VsSession> Function(CStr) sessionOpen;
  final void Function(Pointer<VsSession>) sessionDestroy;
  final CStr Function(Pointer<VsSession>) lastError;
  final int Function(Pointer<VsSession>, CStr) save;
  final int Function(Pointer<VsSession>) autosave;
  final CStr Function(Pointer<VsSession>) projectJson;
  final int Function(Pointer<VsSession>) projectRevision;
  final int Function(Pointer<VsSession>) duration;
  final CStr Function(Pointer<VsSession>, CStr) importMedia;
  final CStr Function(Pointer<VsSession>, int, CStr) addTrack;
  final CStr Function(Pointer<VsSession>, CStr, CStr) appendClip;
  final int Function(Pointer<VsSession>, CStr, CStr, int) moveClip;
  final int Function(Pointer<VsSession>, CStr, int, int) trimClip;
  final int Function(Pointer<VsSession>, int) splitAt;
  final int Function(Pointer<VsSession>, CStr) removeClip;
  final int Function(Pointer<VsSession>) undo;
  final int Function(Pointer<VsSession>) redo;
  final void Function(Pointer<VsSession>, int, int) previewSetSize;
  final void Function(Pointer<VsSession>, int) previewSeek;
  final void Function(Pointer<VsSession>, int) play;
  final void Function(Pointer<VsSession>) pause;
  final int Function(Pointer<VsSession>) isPlaying;
  final int Function(Pointer<VsSession>) playhead;
  final int Function(Pointer<VsSession>, Pointer<Uint8>, int, Pointer<Int64>, Pointer<Int32>, Pointer<Int32>,
      Pointer<Int64>) previewTakeFrame;
  final int Function(Pointer<VsSession>, CStr, int, int, int, int, int) exportStart;
  final int Function(Pointer<VsSession>, Pointer<Double>) exportStatus;
  final void Function(Pointer<VsSession>) exportCancel;
}
