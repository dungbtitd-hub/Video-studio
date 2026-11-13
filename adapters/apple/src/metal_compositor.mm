// Metal compositor. One render pass per output frame: clear to opaque black,
// one textured quad per layer with straight-alpha "over" blending, then a blit
// into a shared buffer that is copied into the caller's RGBA frame.
//
// Matches the CPU reference compositeOver(): fit-then-scale geometry, pixel
// centre sampling with clamp-to-edge bilinear filtering, and an exact texel
// copy when the layer is unscaled at an integer offset.
#include "vstudio/apple/metal_compositor.hpp"

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

struct VOut { float4 pos [[position]]; float2 uv; };
struct Quad { float4 rect; };  // NDC x0, y0 (top), x1, y1 (bottom)
struct Layer { float opacity; uint exact; int2 offset; };

vertex VOut vs_quad(uint vid [[vertex_id]], constant Quad& q [[buffer(0)]]) {
    float2 t = float2(float(vid & 1u), float(vid >> 1));
    VOut o;
    o.pos = float4(mix(q.rect.x, q.rect.z, t.x), mix(q.rect.y, q.rect.w, t.y), 0.0, 1.0);
    o.uv = t;
    return o;
}

fragment float4 fs_layer(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                         constant Layer& l [[buffer(0)]]) {
    float4 c;
    if (l.exact != 0u) {
        c = tex.read(uint2(int2(floor(in.pos.xy)) - l.offset));
    } else {
        constexpr sampler s(filter::linear, address::clamp_to_edge, coord::normalized);
        c = tex.sample(s, in.uv);
    }
    c.a *= l.opacity;
    return c;
}
)";

struct QuadU {
    float rect[4];
};
struct LayerU {
    float opacity;
    uint32_t exact;
    int32_t offset[2];
};

class MetalCompositor final : public ICompositor {
public:
    static std::shared_ptr<MetalCompositor> create() {
        auto c = std::shared_ptr<MetalCompositor>(new MetalCompositor());
        return c->init() ? c : nullptr;
    }

    std::string name() const override { return "apple.metal_compositor"; }

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
            [enc setViewport:(MTLViewport){0, 0, double(W), double(H), 0, 1}];
            for (const auto& l : layers) {
                if (!l.frame || l.frame->empty()) continue;
                const VideoFrame& src = *l.frame;
                const Transform& tr = l.transform;
                if (tr.opacity <= 0.0 || tr.scale <= 0.0) continue;
                // Geometry identical to compositeOver().
                const double fit = std::min(double(W) / src.width, double(H) / src.height);
                const double s = fit * tr.scale;
                const double w = src.width * s, h = src.height * s;
                const double left = (W - w) / 2.0 + tr.x, top = (H - h) / 2.0 + tr.y;
                if (left >= W || top >= H || left + w <= 0 || top + h <= 0) continue;
                id<MTLTexture> tex = texture(l.frame);
                if (!tex) {
                    [enc endEncoding];
                    return fail(error, "cannot upload a layer texture");
                }
                const long ileft = std::lround(left), itop = std::lround(top);
                const bool exact = std::abs(s - 1.0) < 1e-9 && std::abs(left - ileft) < 1e-6 && std::abs(top - itop) < 1e-6;
                QuadU q{{float(left / W * 2.0 - 1.0), float(1.0 - top / H * 2.0), float((left + w) / W * 2.0 - 1.0),
                         float(1.0 - (top + h) / H * 2.0)}};
                // CPU reference quantises opacity to 1/255.
                const double op = std::lround(std::clamp(tr.opacity, 0.0, 1.0) * 255.0) / 255.0;
                LayerU u{float(op), exact ? 1u : 0u, {int32_t(ileft), int32_t(itop)}};
                [enc setVertexBytes:&q length:sizeof q atIndex:0];
                [enc setFragmentBytes:&u length:sizeof u atIndex:0];
                [enc setFragmentTexture:tex atIndex:0];
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
        return pipeline_ != nil;
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
        id<MTLTexture> tex = nil;
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->frame.expired() && int(it->tex.width) == f->width && int(it->tex.height) == f->height) {
                tex = it->tex;
                cache_.erase(it);
                break;
            }
        }
        if (!tex) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
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
                 withBytes:f->rgba.data()
               bytesPerRow:NSUInteger(f->width) * 4];
        cache_.push_front({f.get(), f, tex});
        while (cache_.size() > kMaxTextures) cache_.pop_back();
        return tex;
    }

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
    std::list<Cached> cache_;
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
