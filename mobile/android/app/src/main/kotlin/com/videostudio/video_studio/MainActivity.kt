package com.videostudio.video_studio

import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        PreviewChannel(flutterEngine)
        // Launched with --ez vstudio.openEditor true (CI smoke test): the app skips its start
        // screen and opens a new empty project, so the native preview has something to draw.
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "vstudio/launch").setMethodCallHandler { call, result ->
            if (call.method == "openEditor") result.success(intent?.getBooleanExtra("vstudio.openEditor", false) ?: false)
            else result.notImplemented()
        }
    }
}
