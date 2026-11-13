import Accelerate
import CoreVideo
import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }

  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)
    if let registrar = engineBridge.pluginRegistry.registrar(forPlugin: "VStudioPreview") {
      VStudioPreview.register(with: registrar)
    }
  }
}

/// Native preview on iOS: the core's video thread hands each frame to `push`
/// (vs_preview_set_sink), which fills a CVPixelBuffer that Flutter shows as a
/// texture. No frame crosses into Dart. Channel "vstudio/preview", same as Android.
final class VStudioPreview: NSObject, FlutterPlugin, FlutterTexture {
  private let textures: FlutterTextureRegistry
  private var textureId: Int64 = 0
  private var session: OpaquePointer?
  private let lock = NSLock()
  private var latest: CVPixelBuffer?
  private var pool: CVPixelBufferPool?
  private var poolSize = (0, 0)
  private var loggedFirst = false

  init(textures: FlutterTextureRegistry) {
    self.textures = textures
  }

  static func register(with registrar: FlutterPluginRegistrar) {
    let instance = VStudioPreview(textures: registrar.textures())
    let channel = FlutterMethodChannel(name: "vstudio/preview", binaryMessenger: registrar.messenger())
    registrar.addMethodCallDelegate(instance, channel: channel)
  }

  func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    switch call.method {
    case "create":
      release()
      let args = call.arguments as? [String: Any]
      let address = (args?["session"] as? NSNumber)?.intValue ?? 0
      guard let raw = UnsafeMutableRawPointer(bitPattern: address) else {
        result(FlutterError(code: "no_session", message: "missing session", details: nil))
        return
      }
      session = OpaquePointer(raw)
      textureId = textures.register(self)
      vs_preview_set_sink(session, { user, rgba, w, h, stride, _ in
        guard let user = user, let rgba = rgba else { return }
        Unmanaged<VStudioPreview>.fromOpaque(user).takeUnretainedValue()
          .push(rgba, Int(w), Int(h), Int(stride))
      }, Unmanaged.passUnretained(self).toOpaque())
      result(textureId)
    case "dispose":
      release()
      result(nil)
    default:
      result(FlutterMethodNotImplemented)
    }
  }

  private func release() {
    if let s = session {
      vs_preview_set_sink(s, nil, nil)  // after this the sink is never called again
      session = nil
    }
    if textureId != 0 {
      textures.unregisterTexture(textureId)
      textureId = 0
    }
    lock.lock()
    latest = nil
    lock.unlock()
  }

  // Core video thread.
  private func push(_ rgba: UnsafePointer<UInt8>, _ w: Int, _ h: Int, _ stride: Int) {
    if pool == nil || poolSize != (w, h) {
      let attrs: [String: Any] = [
        kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        kCVPixelBufferWidthKey as String: w,
        kCVPixelBufferHeightKey as String: h,
        kCVPixelBufferIOSurfacePropertiesKey as String: [:],
        kCVPixelBufferMetalCompatibilityKey as String: true,
      ]
      pool = nil
      CVPixelBufferPoolCreate(nil, nil, attrs as CFDictionary, &pool)
      poolSize = (w, h)
    }
    guard let pool = pool else { return }
    var out: CVPixelBuffer?
    guard CVPixelBufferPoolCreatePixelBuffer(nil, pool, &out) == kCVReturnSuccess, let buf = out else { return }
    CVPixelBufferLockBaseAddress(buf, [])
    // RGBA -> BGRA (Flutter textures on iOS are BGRA).
    var src = vImage_Buffer(data: UnsafeMutableRawPointer(mutating: rgba), height: vImagePixelCount(h),
                            width: vImagePixelCount(w), rowBytes: stride)
    var dst = vImage_Buffer(data: CVPixelBufferGetBaseAddress(buf), height: vImagePixelCount(h),
                            width: vImagePixelCount(w), rowBytes: CVPixelBufferGetBytesPerRow(buf))
    let map: [UInt8] = [2, 1, 0, 3]
    vImagePermuteChannels_ARGB8888(&src, &dst, map, vImage_Flags(kvImageNoFlags))
    CVPixelBufferUnlockBaseAddress(buf, [])
    lock.lock()
    latest = buf
    lock.unlock()
    textures.textureFrameAvailable(textureId)
    if !loggedFirst {
      loggedFirst = true
      NSLog("vstudio: native preview first frame %dx%d", w, h)
    }
  }

  func copyPixelBuffer() -> Unmanaged<CVPixelBuffer>? {
    lock.lock()
    defer { lock.unlock() }
    guard let b = latest else { return nil }
    return Unmanaged.passRetained(b)
  }
}
