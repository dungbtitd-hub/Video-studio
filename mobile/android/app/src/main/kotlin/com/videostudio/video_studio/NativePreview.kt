package com.videostudio.video_studio

import android.view.Surface
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import io.flutter.view.TextureRegistry

/** JNI side in mobile/native/android/preview_surface.cpp. */
object NativePreview {
    init {
        System.loadLibrary("vstudio_jni")
    }

    @JvmStatic external fun attach(session: Long, surface: Surface): Long
    @JvmStatic external fun detach(handle: Long)
}

/**
 * Channel "vstudio/preview": gives Dart a Flutter texture whose Surface the core
 * renders into from its own thread (see vs_preview_set_sink).
 */
class PreviewChannel(engine: FlutterEngine) {
    private val textures = engine.renderer
    private var producer: TextureRegistry.SurfaceProducer? = null
    private var session = 0L
    private var handle = 0L

    init {
        MethodChannel(engine.dartExecutor.binaryMessenger, "vstudio/preview").setMethodCallHandler { call, result ->
            when (call.method) {
                "create" -> {
                    release()
                    session = (call.argument<Number>("session") ?: 0).toLong()
                    val p = textures.createSurfaceProducer()
                    p.setSize(call.argument<Int>("width") ?: 720, call.argument<Int>("height") ?: 1280)
                    p.setCallback(object : TextureRegistry.SurfaceProducer.Callback {
                        override fun onSurfaceAvailable() = attach()
                        override fun onSurfaceCleanup() = detach()
                    })
                    producer = p
                    attach()
                    result.success(p.id())
                }
                "dispose" -> {
                    release()
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        }
    }

    private fun attach() {
        detach()
        val p = producer ?: return
        handle = NativePreview.attach(session, p.surface)
    }

    private fun detach() {
        if (handle != 0L) NativePreview.detach(handle)
        handle = 0L
    }

    private fun release() {
        detach()
        producer?.release()
        producer = null
    }
}
