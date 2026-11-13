# GPU backend (V0.1 step 1)

## Decision

Compositing is a module of kind `compositor` (`core/include/vstudio/compositor.hpp`).
Each platform gets a **native** GPU module behind that one interface; there is no
cross-platform GPU abstraction library in the core.

| Platform | Module | Status |
|---|---|---|
| all | `builtin.cpu_compositor` (reference, fallback) | done |
| macOS, iOS | `apple.metal_compositor` (Metal) | done in this step |
| Windows | `windows.d3d11_compositor` (Direct3D 11) | done |
| Android | `android.gles_compositor` (OpenGL ES 3, offscreen EGL) | done; Vulkan later if needed |
| Linux (dev/CI only) | CPU | no GPU module planned |

Why native modules rather than one abstraction (bgfx, Dawn/WebGPU, Qt RHI):

- The core stays free of a large dependency; each module is small (~250 lines for Metal)
  and loaded lazily, in line with the modular rule of the project.
- Zero-copy paths later need the native API anyway (VideoToolbox CVPixelBuffer to
  Metal texture, MediaCodec SurfaceTexture to GLES, D3D11 video decode).
- Qt RHI only covers the desktop app, not the Flutter mobile apps.

## Rules every compositor follows

- Same picture as the CPU reference: checked by `core/tests/compositor_conformance.hpp`
  (random scaled, offset, translucent layers; at most 0.3% of pixels may differ by more than 3/255).
- Thread-safe: preview and export may composite at the same time.
- No device, or device lost: the factory returns nullptr or `composite()` returns false,
  and the Renderer uses the CPU compositor. A missing GPU never breaks preview or export.
- Colour grade (`supportsGrade()`, all three GPU compositors): a layer may carry a `GpuGrade`
  (one RGBA32F 4098x2 table + 16 floats, cached per parameter set). The shader runs the grade after
  sampling and before blending; 10-bit layers are uploaded as RGBA16F so the grade works in float.
  See `docs/color-pipeline.md` ("Chỉnh màu trên GPU").

## Metal compositor

One render pass per frame: clear to opaque black, one textured quad per layer
(straight-alpha "over" blending, clamp-to-edge bilinear, exact texel copy when unscaled),
then a blit to a shared buffer that becomes the output frame. Source textures are kept
per frame identity, so a still image or paused playhead is uploaded once; textures of
frames that are gone are reused for new frames of the same size.

## Direct3D 11 compositor

Same design and shader logic as Metal (HLSL, compiled at start-up by `d3dcompiler_47.dll`, which is
loaded at run time). The module only uses a hardware device; tests also accept WARP so the suite runs
on GPU-less CI runners.

## OpenGL ES 3 compositor (Android)

Same shader logic in GLSL ES 3.00, drawn into an offscreen framebuffer of a pbuffer EGL context.
The context is current only during one call, so any thread may composite. Layers or outputs larger
than `GL_MAX_TEXTURE_SIZE` (2048 on the weakest GLES 3 devices) fail the call and that renderer
continues on the CPU. GLES 3 was chosen over Vulkan first because every API 26+ device has it, and
the MediaCodec output path (SurfaceTexture) is GLES.

Next steps: keep the output on the GPU for preview (present the texture directly instead of
reading it back), and import decoder output as textures (CVPixelBuffer to MTLTexture) so
4K sources are never copied through the CPU.
