// Metal compositor. One render pass per output frame: clear to opaque black,
// one textured quad per layer with straight-alpha "over" blending, then a blit
// into a shared buffer that is copied into the caller's RGBA frame.
//
// Matches the CPU reference compositeOver(): fit-then-scale geometry, pixel
// centre sampling with clamp-to-edge bilinear filtering, and an exact texel
// copy when the layer is unscaled at an integer offset.
#include "vstudio/apple/metal_compositor.hpp"

#include "vstudio/grade.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <TargetConditionals.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <mutex>

namespace vstudio::apple {
namespace {

const char* kShader = R"(
#include <metal_stdlib>
using namespace metal;

struct VOut { float4 pos [[position]]; };
struct Quad { float4 rect; };  // NDC x0, y0 (top), x1, y1 (bottom), one pixel larger than the layer
// rect: layer in output pixels (left, top, right, bottom). Coverage and texture
// coordinates come from the pixel centre, exactly as the CPU reference computes
// them, so edges do not depend on the rasteriser's rules or snapping.
struct Layer { float4 rect; float opacity; uint exact; int2 offset; uint graded; uint pad0, pad1, pad2; float4 g[4]; };

vertex VOut vs_quad(uint vid [[vertex_id]], constant Quad& q [[buffer(0)]]) {
    float2 t = float2(float(vid & 1u), float(vid >> 1));
    VOut o;
    o.pos = float4(mix(q.rect.x, q.rect.z, t.x), mix(q.rect.y, q.rect.w, t.y), 0.0, 1.0);
    return o;
}

// ---- colour grade: the same steps and tables as applyColorGrade() on the CPU ----
// Channels are picked with one-hot masks, not v[ch]: some mobile drivers get dynamic component indexing wrong.
// gt: RGBA32Float, 4098 x 2 (see GpuGrade in grade.hpp), read() only.
float tone(texture2d<float> gt, float v, float4 m) {
    float pos = clamp(v, 0.0, 1.0) * 4096.0;
    uint i = min(uint(pos), 4095u);
    float t = pos - float(i);
    return mix(dot(gt.read(uint2(i, 0)), m), dot(gt.read(uint2(i + 1, 0)), m), t);
}
float4 hueRow(texture2d<float> gt, float h) {
    float pos = h - 360.0 * floor(h / 360.0);  // hue shifts may leave 0..360
    uint i = uint(clamp(int(pos), 0, 359));
    float t = pos - float(i);
    return mix(gt.read(uint2(i, 1)), gt.read(uint2(i + 1, 1)), t);
}
float3 rgb2hsv(float3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    float s = mx > 1e-6 ? d / mx : 0.0;
    float h = 0.0;
    if (d > 1e-6) {
        float k = 60.0 / d;
        if (mx == c.r) { h = (c.g - c.b) * k; if (h < 0.0) h += 360.0; }
        else if (mx == c.g) h = (c.b - c.r) * k + 120.0;
        else h = (c.r - c.g) * k + 240.0;
    }
    return float3(h, s, mx);
}
float3 hsv2rgb(float3 c) {
    float h = c.x, s = c.y, v = c.z;
    if (s <= 0.0) return float3(v);
    h = h - 360.0 * floor(h / 360.0);
    float hp = h / 60.0;
    int i = min(int(hp), 5);
    float f = hp - float(i), p = v * (1.0 - s), q = v * (1.0 - s * f), t = v * (1.0 - s * (1.0 - f));
    if (i == 0) return float3(v, t, p);
    if (i == 1) return float3(q, v, p);
    if (i == 2) return float3(p, v, t);
    if (i == 3) return float3(p, q, v);
    if (i == 4) return float3(t, p, v);
    return float3(v, p, q);
}
float rangeMask(float v, float lo, float hi, float soft) {
    if (v >= lo && v <= hi) return 1.0;
    float d = v < lo ? lo - v : v - hi;
    if (d >= soft) return 0.0;
    float t = 1.0 - d / soft;
    return t * t * (3.0 - 2.0 * t);
}
float3 grade(float3 c, texture2d<float> gt, constant float4* g) {
    if (g[0].y > 0.5) c = float3(tone(gt, c.r, float4(1, 0, 0, 0)), tone(gt, c.g, float4(0, 1, 0, 0)), tone(gt, c.b, float4(0, 0, 1, 0)));
    if (g[0].x != 1.0) {
        float l = dot(c, float3(0.2126, 0.7152, 0.0722));
        c = l + (c - l) * g[0].x;
    }
    bool hsl = g[0].z > 0.5, qual = g[0].w > 0.5;
    if (!hsl && !qual) return c;
    c = max(c, float3(0.0));
    float3 hsv = rgb2hsv(c);
    if (hsl && hsv.y > 0.0) {
        float4 a = hueRow(gt, hsv.x);
        float w = min(1.0, hsv.y * 2.0);
        hsv.x += a.x * w;
        hsv.z *= 1.0 + a.z * 0.5 * w;
        hsv.y = clamp(hsv.y * (1.0 + (a.y - 1.0) * w), 0.0, 1.0);
    }
    if (qual) {
        float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
        float m = hueRow(gt, hsv.x).w * rangeMask(hsv.y, g[1].x, g[1].y, g[2].x) * rangeMask(luma, g[1].z, g[1].w, g[2].x);
        if (g[2].y > 0.5) m = 1.0 - m;
        if (g[2].z > 0.5) return float3(m);
        if (m > 0.0) {
            hsv.x += g[2].w * m;
            hsv.y = clamp(hsv.y * (1.0 + (g[3].x - 1.0) * m), 0.0, 1.0);
            hsv.z *= 1.0 + (g[3].y - 1.0) * m;
        }
    }
    return hsv2rgb(hsv);
}

fragment float4 fs_layer(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                         texture2d<float> gradeTab [[texture(1)]], constant Layer& l [[buffer(0)]]) {
    float2 p = in.pos.xy;  // pixel centre (x + 0.5, y + 0.5)
    if (p.x < l.rect.x || p.x >= l.rect.z || p.y < l.rect.y || p.y >= l.rect.w) discard_fragment();
    float4 c;
    if (l.exact != 0u) {
        c = tex.read(uint2(int2(floor(p)) - l.offset));
    } else {
        constexpr sampler s(filter::linear, address::clamp_to_edge, coord::normalized);
        c = tex.sample(s, (p - l.rect.xy) / (l.rect.zw - l.rect.xy));
    }
    if (l.graded != 0u) c.rgb = clamp(grade(c.rgb, gradeTab, l.g), 0.0, 1.0);
    c.a *= l.opacity;
    return c;
}
)";

struct QuadU {
    float rect[4];
};
struct LayerU {
    float rect[4];
    float opacity;
    uint32_t exact;
    int32_t offset[2];
    uint32_t graded;
    uint32_t pad[3];
    float g[16];
};
static_assert(sizeof(LayerU) == 112, "matches the MSL Layer struct");

class MetalCompositor final : public ICompositor {
public:
    static std::shared_ptr<MetalCompositor> create() {
        auto c = std::shared_ptr<MetalCompositor>(new MetalCompositor());
        return c->init() ? c : nullptr;
    }

    std::string name() const override { return "apple.metal_compositor"; }
    bool supportsGrade() const override { return true; }

    bool composite(VideoFrame& out, const std::vector<CompositeLayer>& layers, std::string* error) override {
        if (out.empty()) return true;
        std::lock_guard lk(mu_);
        @autoreleasepool {
            const int W = out.width, H = out.height;
            if (!ensureTarget(W, H)) return fail(error, "cannot allocate the output texture");
            id<MTLCommandBuffer> cb = [queue_ commandBuffer];
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = target_;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
            [enc setRenderPipelineState:pipeline_];
            MTLViewport vp{0, 0, double(W), double(H), 0, 1};
            [enc setViewport:vp];
            for (const auto& l : layers) {
                if (!l.frame || l.frame->empty()) continue;
                const VideoFrame& src = *l.frame;
                const Transform& tr = l.transform;
                if (tr.opacity <= 0.0 || tr.scale <= 0.0) continue;
                // Geometry shared with compositeOver() (snapped, so float == double).
                const LayerRect r = layerRect(src.width, src.height, W, H, tr);
                const double w = r.width, h = r.height, left = r.left, top = r.top;
                if (left >= W || top >= H || left + w <= 0 || top + h <= 0) continue;
                id<MTLTexture> tex = texture(l.frame);
                if (!tex) {
                    [enc endEncoding];
                    return fail(error, "cannot upload a layer texture");
                }
                const long ileft = std::lround(left), itop = std::lround(top);
                const bool exact = r.unscaled;
                QuadU q{{float((left - 1) / W * 2.0 - 1.0), float(1.0 - (top - 1) / H * 2.0),
                         float((left + w + 1) / W * 2.0 - 1.0), float(1.0 - (top + h + 1) / H * 2.0)}};
                // CPU reference quantises opacity to 1/255.
                const double op = std::lround(std::clamp(tr.opacity, 0.0, 1.0) * 255.0) / 255.0;
                LayerU u{{float(left), float(top), float(left + w), float(top + h)},
                         float(op),
                         exact ? 1u : 0u,
                         {int32_t(ileft), int32_t(itop)},
                         l.grade ? 1u : 0u,
                         {0, 0, 0},
                         {}};
                id<MTLTexture> gtab = l.grade ? gradeTexture(l.grade) : dummyGrade_;
                if (!gtab) {
                    [enc endEncoding];
                    return fail(error, "cannot upload a grade table");
                }
                if (l.grade) std::memcpy(u.g, l.grade->u, sizeof u.g);
                [enc setVertexBytes:&q length:sizeof q atIndex:0];
                [enc setFragmentBytes:&u length:sizeof u atIndex:0];
                [enc setFragmentTexture:tex atIndex:0];
                [enc setFragmentTexture:gtab atIndex:1];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            }
            [enc endEncoding];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromTexture:target_
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(NSUInteger(W), NSUInteger(H), 1)
                            toBuffer:readback_
                   destinationOffset:0
              destinationBytesPerRow:NSUInteger(W) * 4
            destinationBytesPerImage:NSUInteger(W) * H * 4];
            [blit endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status != MTLCommandBufferStatusCompleted) {
                return fail(error, cb.error ? cb.error.localizedDescription.UTF8String : "command buffer failed");
            }
            std::memcpy(out.rgba.data(), readback_.contents, out.rgba.size());
        }
        return true;
    }

private:
    MetalCompositor() = default;

    static bool fail(std::string* error, const char* what) {
        if (error) *error = what;
        return false;
    }

    bool init() {
        device_ = MTLCreateSystemDefaultDevice();
        if (!device_) return false;
        queue_ = [device_ newCommandQueue];
        NSError* err = nil;
        id<MTLLibrary> lib = [device_ newLibraryWithSource:@(kShader) options:nil error:&err];
        if (!lib || !queue_) return false;
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [lib newFunctionWithName:@"vs_quad"];
        d.fragmentFunction = [lib newFunctionWithName:@"fs_layer"];
        auto* ca = d.colorAttachments[0];
        ca.pixelFormat = MTLPixelFormatRGBA8Unorm;
        ca.blendingEnabled = YES;
        ca.rgbBlendOperation = MTLBlendOperationAdd;
        ca.alphaBlendOperation = MTLBlendOperationAdd;
        ca.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipeline_ = [device_ newRenderPipelineStateWithDescriptor:d error:&err];
        // Bound for ungraded layers so the shader's grade texture slot is never empty.
        MTLTextureDescriptor* gd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                                      width:1
                                                                                     height:1
                                                                                  mipmapped:NO];
        gd.usage = MTLTextureUsageShaderRead;
        dummyGrade_ = [device_ newTextureWithDescriptor:gd];
        return pipeline_ != nil && dummyGrade_ != nil;
    }

    bool ensureTarget(int W, int H) {
        if (target_ && int(target_.width) == W && int(target_.height) == H) return true;
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:NSUInteger(W)
                                                                                    height:NSUInteger(H)
                                                                                 mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget;
        d.storageMode = MTLStorageModePrivate;
        target_ = [device_ newTextureWithDescriptor:d];
        readback_ = [device_ newBufferWithLength:NSUInteger(W) * H * 4 options:MTLResourceStorageModeShared];
        return target_ && readback_;
    }

    // Source textures, keyed by frame identity: a frame that stays on screen
    // (still image, paused playhead, cached scrub frame) is uploaded once.
    // Textures of frames that are gone are reused for new frames of that size.
    id<MTLTexture> texture(const std::shared_ptr<const VideoFrame>& f) {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->key == f.get() && !it->frame.expired() && it->frame.lock() == f) {
                cache_.splice(cache_.begin(), cache_, it);
                return cache_.front().tex;
            }
        }
        // 10-bit sources are graded in float: half floats straight from the decoder.
        const MTLPixelFormat fmt = f->deep() ? MTLPixelFormatRGBA16Float : MTLPixelFormatRGBA8Unorm;
        id<MTLTexture> tex = nil;
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->frame.expired() && int(it->tex.width) == f->width && int(it->tex.height) == f->height &&
                it->tex.pixelFormat == fmt) {
                tex = it->tex;
                cache_.erase(it);
                break;
            }
        }
        if (!tex) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                                                         width:NSUInteger(f->width)
                                                                                        height:NSUInteger(f->height)
                                                                                     mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead;
#if TARGET_OS_OSX
            d.storageMode = MTLStorageModeManaged;  // valid on Intel and Apple silicon Macs
#else
            d.storageMode = MTLStorageModeShared;
#endif
            tex = [device_ newTextureWithDescriptor:d];
            if (!tex) return nil;
        }
        [tex replaceRegion:MTLRegionMake2D(0, 0, NSUInteger(f->width), NSUInteger(f->height))
               mipmapLevel:0
                 withBytes:f->deep() ? static_cast<const void*>(f->half.data()) : static_cast<const void*>(f->rgba.data())
               bytesPerRow:NSUInteger(f->width) * (f->deep() ? 8 : 4)];
        cache_.push_front({f.get(), f, tex});
        while (cache_.size() > kMaxTextures) cache_.pop_back();
        return tex;
    }

    // Grade tables by grade identity (gpuGrade() reuses one object per parameter set).
    id<MTLTexture> gradeTexture(const std::shared_ptr<const GpuGrade>& g) {
        for (auto it = grades_.begin(); it != grades_.end(); ++it)
            if (it->key == g.get() && it->grade.lock() == g) {
                grades_.splice(grades_.begin(), grades_, it);
                return grades_.front().tex;
            }
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                                     width:GpuGrade::kTableWidth
                                                                                    height:2
                                                                                 mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead;
#if TARGET_OS_OSX
        d.storageMode = MTLStorageModeManaged;
#else
        d.storageMode = MTLStorageModeShared;
#endif
        id<MTLTexture> tex = [device_ newTextureWithDescriptor:d];
        if (!tex) return nil;
        [tex replaceRegion:MTLRegionMake2D(0, 0, GpuGrade::kTableWidth, 2)
               mipmapLevel:0
                 withBytes:g->table.data()
               bytesPerRow:NSUInteger(GpuGrade::kTableWidth) * 16];
        grades_.push_front({g.get(), g, tex});
        while (grades_.size() > 8) grades_.pop_back();
        return tex;
    }

    struct GradeTex {
        const GpuGrade* key;
        std::weak_ptr<const GpuGrade> grade;
        id<MTLTexture> tex;
    };
    struct Cached {
        const VideoFrame* key;
        std::weak_ptr<const VideoFrame> frame;
        id<MTLTexture> tex;
    };
    static constexpr std::size_t kMaxTextures = 24;

    std::mutex mu_;
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    id<MTLRenderPipelineState> pipeline_ = nil;
    id<MTLTexture> target_ = nil;
    id<MTLBuffer> readback_ = nil;
    id<MTLTexture> dummyGrade_ = nil;
    std::list<Cached> cache_;
    std::list<GradeTex> grades_;
};

}  // namespace

std::shared_ptr<ICompositor> createMetalCompositor() {
    // Probe once: on a machine without Metal the registry may ask again, and
    // creating a device is not free.
    static std::once_flag once;
    static bool available = false;
    std::call_once(once, [] { available = MTLCreateSystemDefaultDevice() != nil; });
    if (!available) return nullptr;
    return MetalCompositor::create();
}

void registerMetalCompositor(ModuleRegistry& r, int priority) {
    r.add<ICompositor>({.id = "apple.metal_compositor",
                        .displayName = "Metal compositor",
                        .capabilities = {"composite", "gpu"},
                        .platforms = {"macos", "ios"},
                        .priority = priority},
                       [] { return createMetalCompositor(); });
}

}  // namespace vstudio::apple
