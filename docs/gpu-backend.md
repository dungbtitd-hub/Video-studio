# GPU backend (V0.1 step 1)

## Decision

Compositing is a module of kind `compositor` (`core/include/vstudio/compositor.hpp`).
Each platform gets a **native** GPU module behind that one interface; there is no
cross-platform GPU abstraction library in the core.

| Platform | Module | Status |
|---|---|---|
| all | `builtin.cpu_compositor` (reference, fallback) | done |
| macOS, iOS | `apple.metal_compositor` (Metal) | done in this step |
| Windows | D3D11 compositor | next |
| Android | GLES 3 / Vulkan compositor | after Windows |
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

## Metal compositor

One render pass per frame: clear to opaque black, one textured quad per layer
(straight-alpha "over" blending, clamp-to-edge bilinear, exact texel copy when unscaled),
then a blit to a shared buffer that becomes the output frame. Source textures are kept
per frame identity, so a still image or paused playhead is uploaded once; textures of
frames that are gone are reused for new frames of the same size.

Next steps: keep the output on the GPU for preview (present the texture directly instead of
reading it back), and import decoder output as textures (CVPixelBuffer to MTLTexture) so
4K sources are never copied through the CPU.
