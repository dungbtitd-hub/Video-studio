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
        modulesJson =
            lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>('vs_modules_json'),
        sessionCreate = lib.lookupFunction<Pointer<VsSession> Function(CStr, Int32, Int32, Int32, Int32),
            Pointer<VsSession> Function(CStr, int, int, int, int)>('vs_session_create'),
        sessionOpen =
            lib.lookupFunction<Pointer<VsSession> Function(CStr), Pointer<VsSession> Function(CStr)>('vs_session_open'),
        sessionDestroy = lib
            .lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>('vs_session_destroy'),
        lastError =
            lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>('vs_last_error'),
        save = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr), int Function(Pointer<VsSession>, CStr)>(
            'vs_save'),
        autosave =
            lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_autosave'),
        projectJson =
            lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>('vs_project_json'),
        projectRevision = lib.lookupFunction<Int64 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>(
            'vs_project_revision'),
        duration =
            lib.lookupFunction<Int64 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_duration'),
        importMedia =
            lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr), CStr Function(Pointer<VsSession>, CStr)>(
                'vs_import_media'),
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
        removeTrack =
            lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr), int Function(Pointer<VsSession>, CStr)>(
                'vs_remove_track'),
        renameTrack = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr)>('vs_rename_track'),
        setTrackFlags = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32, Int32, Int32),
            int Function(Pointer<VsSession>, CStr, int, int, int)>('vs_set_track_flags'),
        moveTrack = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32),
            int Function(Pointer<VsSession>, CStr, int)>('vs_move_track'),
        snapClipMove = lib.lookupFunction<Int64 Function(Pointer<VsSession>, CStr, Int64, Int64, Int64, Pointer<Int32>),
            int Function(Pointer<VsSession>, CStr, int, int, int, Pointer<Int32>)>('vs_snap_clip_move'),
        rippleDelete =
            lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr), int Function(Pointer<VsSession>, CStr)>(
                'vs_ripple_delete'),
        isDirty =
            lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_is_dirty'),
        snapTime = lib.lookupFunction<Int64 Function(Pointer<VsSession>, Int64, Int64, CStr, Int64, Pointer<Int32>),
            int Function(Pointer<VsSession>, int, int, CStr, int, Pointer<Int32>)>('vs_snap_time'),
        removeClip =
            lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr), int Function(Pointer<VsSession>, CStr)>(
                'vs_remove_clip'),
        undo = lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_undo'),
        redo = lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_redo'),
        previewSetSize = lib.lookupFunction<Void Function(Pointer<VsSession>, Int32, Int32),
            void Function(Pointer<VsSession>, int, int)>('vs_preview_set_size'),
        previewSeek =
            lib.lookupFunction<Void Function(Pointer<VsSession>, Int64), void Function(Pointer<VsSession>, int)>(
                'vs_preview_seek'),
        play = lib.lookupFunction<Void Function(Pointer<VsSession>, Int64), void Function(Pointer<VsSession>, int)>(
            'vs_play'),
        pause = lib.lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>('vs_pause'),
        isPlaying =
            lib.lookupFunction<Int32 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_is_playing'),
        playhead =
            lib.lookupFunction<Int64 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_playhead'),
        previewTakeFrame = lib.lookupFunction<
            Int32 Function(Pointer<VsSession>, Pointer<Uint8>, Int64, Pointer<Int64>, Pointer<Int32>, Pointer<Int32>,
                Pointer<Int64>),
            int Function(Pointer<VsSession>, Pointer<Uint8>, int, Pointer<Int64>, Pointer<Int32>, Pointer<Int32>,
                Pointer<Int64>)>('vs_preview_take_frame'),
        looksJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_looks_json'),
        setClipLook = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Double),
            int Function(Pointer<VsSession>, CStr, CStr, double)>('vs_set_clip_look'),
        logFormatsJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_log_formats_json'),
        setClipColorSpace = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Double),
            int Function(Pointer<VsSession>, CStr, CStr, double)>('vs_set_clip_color_space'),
        setClipLut = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Double),
            int Function(Pointer<VsSession>, CStr, CStr, double)>('vs_set_clip_lut'),
        suggestedLogFormat =
            lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr), CStr Function(Pointer<VsSession>, CStr)>(
                'vs_suggested_log_format'),
        addKeyframe = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32, Int64),
            int Function(Pointer<VsSession>, CStr, int, int)>('vs_add_keyframe'),
        removeKeyframe = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32, Int64),
            int Function(Pointer<VsSession>, CStr, int, int)>('vs_remove_keyframe'),
        clearKeyframes = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32),
            int Function(Pointer<VsSession>, CStr, int)>('vs_clear_keyframes'),
        setPropertyAt = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int64, CStr),
            int Function(Pointer<VsSession>, CStr, int, CStr)>('vs_set_property_at'),
        zoomToRegion = lib.lookupFunction<
            Int32 Function(Pointer<VsSession>, CStr, Int64, Double, Double, Double, Double),
            int Function(Pointer<VsSession>, CStr, int, double, double, double, double)>('vs_zoom_to_region'),
        clipStateAt = lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr, Int64),
            CStr Function(Pointer<VsSession>, CStr, int)>('vs_clip_state_at'),
        coverStylesJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_cover_styles_json'),
        coverShapesJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_cover_shapes_json'),
        addCover = lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr, CStr),
            CStr Function(Pointer<VsSession>, CStr, CStr)>('vs_add_cover'),
        updateCover = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr, CStr)>('vs_update_cover'),
        removeCover = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr)>('vs_remove_cover'),
        setCoverAt = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Int64, CStr),
            int Function(Pointer<VsSession>, CStr, CStr, int, CStr)>('vs_set_cover_at'),
        keyCover = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Int64),
            int Function(Pointer<VsSession>, CStr, CStr, int)>('vs_key_cover'),
        clearCoverKeys = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Int64),
            int Function(Pointer<VsSession>, CStr, CStr, int)>('vs_clear_cover_keys'),
        coverStateAt = lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr, CStr, Int64),
            CStr Function(Pointer<VsSession>, CStr, CStr, int)>('vs_cover_state_at'),
        coversJson =
            lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr), CStr Function(Pointer<VsSession>, CStr)>(
                'vs_covers_json'),
        coverCanvasRect = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Int64, Pointer<Double>),
            int Function(Pointer<VsSession>, CStr, CStr, int, Pointer<Double>)>('vs_cover_canvas_rect'),
        setCoverCanvasRect = lib.lookupFunction<
            Int32 Function(Pointer<VsSession>, CStr, CStr, Int64, Double, Double, Double, Double),
            int Function(
                Pointer<VsSession>, CStr, CStr, int, double, double, double, double)>('vs_set_cover_canvas_rect'),
        coverPreview = lib.lookupFunction<Int32 Function(CStr, CStr, Pointer<Uint8>, Int32, Int32),
            int Function(CStr, CStr, Pointer<Uint8>, int, int)>('vs_cover_preview'),
        trackCoverStart = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, Int64, Int64),
            int Function(Pointer<VsSession>, CStr, CStr, int, int)>('vs_track_cover_start'),
        trackStatus = lib.lookupFunction<Int32 Function(Pointer<VsSession>, Pointer<Double>),
            int Function(Pointer<VsSession>, Pointer<Double>)>('vs_track_status'),
        trackCancel =
            lib.lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>('vs_track_cancel'),
        transitionsJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_transitions_json'),
        setTransition = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32, CStr, Int64),
            int Function(Pointer<VsSession>, CStr, int, CStr, int)>('vs_set_transition'),
        setTransitionEverywhere = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int64),
            int Function(Pointer<VsSession>, CStr, int)>('vs_set_transition_everywhere'),
        cutPointsJson = lib
            .lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>('vs_cut_points_json'),
        transitionPreview = lib.lookupFunction<
            Int32 Function(CStr, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>, Int32, Int32, Double),
            int Function(
                CStr, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>, int, int, double)>('vs_transition_preview'),
        videoFxJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_video_fx_json'),
        setClipVideoFx = lib.lookupFunction<
            Int32 Function(Pointer<VsSession>, CStr, CStr, CStr, Double, Double, Double),
            int Function(Pointer<VsSession>, CStr, CStr, CStr, double, double, double)>('vs_set_clip_video_fx'),
        videoFxIcon = lib.lookupFunction<
            Int32 Function(CStr, Double, Pointer<Uint8>, Int32, Int32, Pointer<Uint8>, Int32, Int32),
            int Function(CStr, double, Pointer<Uint8>, int, int, Pointer<Uint8>, int, int)>('vs_video_fx_icon'),
        fontsJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_fonts_json'),
        registerFont = lib.lookupFunction<Int32 Function(CStr, CStr, Pointer<Uint8>, Int64),
            int Function(CStr, CStr, Pointer<Uint8>, int)>('vs_register_font'),
        textAnimationsJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_text_animations_json'),
        textPresetsJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_text_presets_json'),
        stickersJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_stickers_json'),
        defaultTextJson = lib.lookupFunction<CStr Function(CStr), CStr Function(CStr)>('vs_default_text_json'),
        defaultStickerJson = lib.lookupFunction<CStr Function(CStr), CStr Function(CStr)>('vs_default_sticker_json'),
        addGeneratedClip = lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr, Int64, Int64, CStr),
            CStr Function(Pointer<VsSession>, CStr, int, int, CStr)>('vs_add_generated_clip'),
        updateClipContent = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr)>('vs_update_clip_content'),
        generatedRect = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int64, Pointer<Double>),
            int Function(Pointer<VsSession>, CStr, int, Pointer<Double>)>('vs_generated_rect'),
        stickerIcon = lib.lookupFunction<Int32 Function(CStr, Double, Pointer<Uint8>, Int32, Int32),
            int Function(CStr, double, Pointer<Uint8>, int, int)>('vs_sticker_icon'),
        overlaysJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_overlays_json'),
        livePropertyAt = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int64, CStr),
            int Function(Pointer<VsSession>, CStr, int, CStr)>('vs_live_property_at'),
        liveOverlayRegion = lib.lookupFunction<
            Int32 Function(Pointer<VsSession>, CStr, Int64, Double, Double, Double, Double),
            int Function(Pointer<VsSession>, CStr, int, double, double, double, double)>('vs_live_overlay_region'),
        liveClipContent = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr)>('vs_live_clip_content'),
        liveEnd =
            lib.lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>('vs_live_end'),
        recentProjectsJson = lib.lookupFunction<CStr Function(CStr, CStr, Int32, Int32, CStr),
            CStr Function(CStr, CStr, int, int, CStr)>('vs_recent_projects_json'),
        projectPoster = lib.lookupFunction<Int32 Function(CStr, Pointer<Uint8>, Int64, Pointer<Int32>, Pointer<Int32>),
            int Function(CStr, Pointer<Uint8>, int, Pointer<Int32>, Pointer<Int32>)>('vs_project_poster'),
        defaultOverlayJson = lib.lookupFunction<CStr Function(CStr), CStr Function(CStr)>('vs_default_overlay_json'),
        overlayIcon = lib.lookupFunction<Int32 Function(CStr, Double, Pointer<Uint8>, Int32, Int32),
            int Function(CStr, double, Pointer<Uint8>, int, int)>('vs_overlay_icon'),
        setOverlayRegion = lib.lookupFunction<
            Int32 Function(Pointer<VsSession>, CStr, Int64, Double, Double, Double, Double),
            int Function(Pointer<VsSession>, CStr, int, double, double, double, double)>('vs_set_overlay_region'),
        contentPreview = lib.lookupFunction<Int32 Function(CStr, Double, Double, Pointer<Uint8>, Int32, Int32),
            int Function(CStr, double, double, Pointer<Uint8>, int, int)>('vs_content_preview'),
        lookPreview = lib.lookupFunction<Int32 Function(CStr, Double, Pointer<Uint8>, Int32, Int32),
            int Function(CStr, double, Pointer<Uint8>, int, int)>('vs_look_preview'),
        setClipGrade = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr)>('vs_set_clip_grade'),
        clipGradeJson =
            lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr), CStr Function(Pointer<VsSession>, CStr)>(
                'vs_clip_grade_json'),
        hslBandsJson = lib.lookupFunction<CStr Function(), CStr Function()>('vs_hsl_bands_json'),
        gradeCurve =
            lib.lookupFunction<Int32 Function(CStr, Int32, Pointer<Float>), int Function(CStr, int, Pointer<Float>)>(
                'vs_grade_curve'),
        wheelToBalance = lib.lookupFunction<Void Function(Double, Double, Pointer<Double>),
            void Function(double, double, Pointer<Double>)>('vs_wheel_to_balance'),
        balanceToWheel = lib.lookupFunction<Void Function(Pointer<Double>, Pointer<Double>, Pointer<Double>),
            void Function(Pointer<Double>, Pointer<Double>, Pointer<Double>)>('vs_balance_to_wheel'),
        setEffectPreview = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr, CStr)>('vs_set_effect_preview'),
        previewPickColor = lib.lookupFunction<Int32 Function(Pointer<VsSession>, Double, Double, Pointer<Double>),
            int Function(Pointer<VsSession>, double, double, Pointer<Double>)>('vs_preview_pick_color'),
        previewScope = lib.lookupFunction<Int32 Function(Pointer<VsSession>, Int32, Int32, Int32, Pointer<Uint8>),
            int Function(Pointer<VsSession>, int, int, int, Pointer<Uint8>)>('vs_preview_scope'),
        gradePreview = lib.lookupFunction<Int32 Function(CStr, Pointer<Uint8>, Int32, Int32),
            int Function(CStr, Pointer<Uint8>, int, int)>('vs_grade_preview'),
        exportPresetsJson = lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>(
            'vs_export_presets_json'),
        exportStartPreset = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr)>('vs_export_start_preset'),
        exportFormatsJson = lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>(
            'vs_export_formats_json'),
        exportPresetsFormatJson = lib.lookupFunction<CStr Function(Pointer<VsSession>, CStr),
            CStr Function(Pointer<VsSession>, CStr)>('vs_export_presets_format_json'),
        exportStartPresetFormat = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr, CStr)>('vs_export_start_preset_format'),
        exportStart = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32, Int32, Int32, Int32, Int64),
            int Function(Pointer<VsSession>, CStr, int, int, int, int, int)>('vs_export_start'),
        exportStatus = lib.lookupFunction<Int32 Function(Pointer<VsSession>, Pointer<Double>),
            int Function(Pointer<VsSession>, Pointer<Double>)>('vs_export_status'),
        exportCancel = lib
            .lookupFunction<Void Function(Pointer<VsSession>), void Function(Pointer<VsSession>)>('vs_export_cancel'),
        requestThumbnails = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32, Int32),
            int Function(Pointer<VsSession>, CStr, int, int)>('vs_request_thumbnails'),
        requestWaveform = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, Int32),
            int Function(Pointer<VsSession>, CStr, int)>('vs_request_waveform'),
        librarySerial = lib
            .lookupFunction<Int64 Function(Pointer<VsSession>), int Function(Pointer<VsSession>)>('vs_library_serial'),
        thumbnails = lib.lookupFunction<
            Int32 Function(
                Pointer<VsSession>, CStr, Pointer<Uint8>, Int64, Pointer<Int32>, Pointer<Int32>, Pointer<Int32>),
            int Function(Pointer<VsSession>, CStr, Pointer<Uint8>, int, Pointer<Int32>, Pointer<Int32>,
                Pointer<Int32>)>('vs_thumbnails'),
        waveform = lib.lookupFunction<Int64 Function(Pointer<VsSession>, CStr, Pointer<Int8>, Int64, Pointer<Int32>),
            int Function(Pointer<VsSession>, CStr, Pointer<Int8>, int, Pointer<Int32>)>('vs_waveform'),
        missingAssetsJson = lib.lookupFunction<CStr Function(Pointer<VsSession>), CStr Function(Pointer<VsSession>)>(
            'vs_missing_assets_json'),
        relinkAsset = lib.lookupFunction<Int32 Function(Pointer<VsSession>, CStr, CStr),
            int Function(Pointer<VsSession>, CStr, CStr)>('vs_relink_asset');

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
  final int Function(Pointer<VsSession>, CStr) removeTrack;
  final int Function(Pointer<VsSession>, CStr, CStr) renameTrack;
  final int Function(Pointer<VsSession>, CStr, int, int, int) setTrackFlags;
  final int Function(Pointer<VsSession>, CStr, int) moveTrack;
  final int Function(Pointer<VsSession>, CStr, int, int, int, Pointer<Int32>) snapClipMove;
  final int Function(Pointer<VsSession>, CStr) removeClip;
  final int Function(Pointer<VsSession>, CStr) rippleDelete;
  final int Function(Pointer<VsSession>) isDirty;
  final int Function(Pointer<VsSession>, int, int, CStr, int, Pointer<Int32>) snapTime;
  final int Function(Pointer<VsSession>) undo;
  final int Function(Pointer<VsSession>) redo;
  final void Function(Pointer<VsSession>, int, int) previewSetSize;
  final void Function(Pointer<VsSession>, int) previewSeek;
  final void Function(Pointer<VsSession>, int) play;
  final void Function(Pointer<VsSession>) pause;
  final int Function(Pointer<VsSession>) isPlaying;
  final int Function(Pointer<VsSession>) playhead;
  final int Function(
          Pointer<VsSession>, Pointer<Uint8>, int, Pointer<Int64>, Pointer<Int32>, Pointer<Int32>, Pointer<Int64>)
      previewTakeFrame;
  final int Function(Pointer<VsSession>, CStr, int, int, int, int, int) exportStart;
  final CStr Function(Pointer<VsSession>) exportPresetsJson;
  final CStr Function() looksJson;
  final int Function(Pointer<VsSession>, CStr, CStr, double) setClipLook;
  final int Function(CStr, double, Pointer<Uint8>, int, int) lookPreview;
  final int Function(Pointer<VsSession>, CStr, CStr) setClipGrade;
  final CStr Function(Pointer<VsSession>, CStr) clipGradeJson;
  final CStr Function() hslBandsJson;
  final int Function(CStr, int, Pointer<Float>) gradeCurve;
  final void Function(double, double, Pointer<Double>) wheelToBalance;
  final void Function(Pointer<Double>, Pointer<Double>, Pointer<Double>) balanceToWheel;
  final int Function(Pointer<VsSession>, CStr, CStr, CStr) setEffectPreview;
  final int Function(CStr, Pointer<Uint8>, int, int) gradePreview;
  final int Function(Pointer<VsSession>, double, double, Pointer<Double>) previewPickColor;
  final int Function(Pointer<VsSession>, int, int, int, Pointer<Uint8>) previewScope;
  final CStr Function() logFormatsJson;
  final int Function(Pointer<VsSession>, CStr, CStr, double) setClipColorSpace;
  final int Function(Pointer<VsSession>, CStr, CStr, double) setClipLut;
  final CStr Function(Pointer<VsSession>, CStr) suggestedLogFormat;
  final CStr Function() coverStylesJson;
  final CStr Function() coverShapesJson;
  final CStr Function(Pointer<VsSession>, CStr, CStr) addCover;
  final int Function(Pointer<VsSession>, CStr, CStr, CStr) updateCover;
  final int Function(Pointer<VsSession>, CStr, CStr) removeCover;
  final int Function(Pointer<VsSession>, CStr, CStr, int, CStr) setCoverAt;
  final int Function(Pointer<VsSession>, CStr, CStr, int) keyCover;
  final int Function(Pointer<VsSession>, CStr, CStr, int) clearCoverKeys;
  final CStr Function(Pointer<VsSession>, CStr, CStr, int) coverStateAt;
  final CStr Function(Pointer<VsSession>, CStr) coversJson;
  final int Function(Pointer<VsSession>, CStr, CStr, int, Pointer<Double>) coverCanvasRect;
  final int Function(Pointer<VsSession>, CStr, CStr, int, double, double, double, double) setCoverCanvasRect;
  final int Function(CStr, CStr, Pointer<Uint8>, int, int) coverPreview;
  final int Function(Pointer<VsSession>, CStr, CStr, int, int) trackCoverStart;
  final int Function(Pointer<VsSession>, Pointer<Double>) trackStatus;
  final void Function(Pointer<VsSession>) trackCancel;
  final CStr Function() transitionsJson;
  final int Function(Pointer<VsSession>, CStr, int, CStr, int) setTransition;
  final int Function(Pointer<VsSession>, CStr, int) setTransitionEverywhere;
  final CStr Function(Pointer<VsSession>) cutPointsJson;
  final int Function(CStr, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>, int, int, double) transitionPreview;
  final CStr Function() videoFxJson;
  final int Function(Pointer<VsSession>, CStr, CStr, CStr, double, double, double) setClipVideoFx;
  final int Function(CStr, double, Pointer<Uint8>, int, int, Pointer<Uint8>, int, int) videoFxIcon;
  final CStr Function() fontsJson;
  final int Function(CStr, CStr, Pointer<Uint8>, int) registerFont;
  final CStr Function() textAnimationsJson;
  final CStr Function() textPresetsJson;
  final CStr Function() stickersJson;
  final CStr Function(CStr) defaultTextJson;
  final CStr Function(CStr) defaultStickerJson;
  final CStr Function(Pointer<VsSession>, CStr, int, int, CStr) addGeneratedClip;
  final int Function(Pointer<VsSession>, CStr, CStr) updateClipContent;
  final int Function(Pointer<VsSession>, CStr, int, Pointer<Double>) generatedRect;
  final int Function(CStr, double, Pointer<Uint8>, int, int) stickerIcon;
  final int Function(CStr, double, double, Pointer<Uint8>, int, int) contentPreview;
  final CStr Function() overlaysJson;
  final CStr Function(CStr, CStr, int, int, CStr) recentProjectsJson;
  final int Function(Pointer<VsSession>, CStr, int, CStr) livePropertyAt;
  final int Function(Pointer<VsSession>, CStr, int, double, double, double, double) liveOverlayRegion;
  final int Function(Pointer<VsSession>, CStr, CStr) liveClipContent;
  final void Function(Pointer<VsSession>) liveEnd;
  final int Function(CStr, Pointer<Uint8>, int, Pointer<Int32>, Pointer<Int32>) projectPoster;
  final CStr Function(CStr) defaultOverlayJson;
  final int Function(CStr, double, Pointer<Uint8>, int, int) overlayIcon;
  final int Function(Pointer<VsSession>, CStr, int, double, double, double, double) setOverlayRegion;
  final int Function(Pointer<VsSession>, CStr, int, int) addKeyframe;
  final int Function(Pointer<VsSession>, CStr, int, int) removeKeyframe;
  final int Function(Pointer<VsSession>, CStr, int) clearKeyframes;
  final int Function(Pointer<VsSession>, CStr, int, CStr) setPropertyAt;
  final int Function(Pointer<VsSession>, CStr, int, double, double, double, double) zoomToRegion;
  final CStr Function(Pointer<VsSession>, CStr, int) clipStateAt;
  final int Function(Pointer<VsSession>, CStr, CStr) exportStartPreset;
  final CStr Function(Pointer<VsSession>) exportFormatsJson;
  final CStr Function(Pointer<VsSession>, CStr) exportPresetsFormatJson;
  final int Function(Pointer<VsSession>, CStr, CStr, CStr) exportStartPresetFormat;
  final int Function(Pointer<VsSession>, Pointer<Double>) exportStatus;
  final void Function(Pointer<VsSession>) exportCancel;
  final int Function(Pointer<VsSession>, CStr, int, int) requestThumbnails;
  final int Function(Pointer<VsSession>, CStr, int) requestWaveform;
  final int Function(Pointer<VsSession>) librarySerial;
  final int Function(Pointer<VsSession>, CStr, Pointer<Uint8>, int, Pointer<Int32>, Pointer<Int32>, Pointer<Int32>)
      thumbnails;
  final int Function(Pointer<VsSession>, CStr, Pointer<Int8>, int, Pointer<Int32>) waveform;
  final CStr Function(Pointer<VsSession>) missingAssetsJson;
  final int Function(Pointer<VsSession>, CStr, CStr) relinkAsset;
}
