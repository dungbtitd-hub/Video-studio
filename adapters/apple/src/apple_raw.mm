// CIRAWFilter develops the RAW file (camera white balance and profile, lens corrections,
// orientation) at the asked scale, and Core Image renders it on the GPU straight into a
// half-float (or 8-bit) Rec.709 buffer.
#include "vstudio/apple/apple_raw.hpp"

#import <CoreImage/CoreImage.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>

namespace vstudio::apple {

namespace {

NSURL* fileUrl(const std::string& path) {
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
}

std::string str(NSError* e) { return e ? std::string(e.localizedDescription.UTF8String) : std::string(); }

class API_AVAILABLE(macos(12.0), ios(15.0)) RawDecoder final : public IStillDecoder {
public:
    RawDecoder() {
        rec709_ = CGColorSpaceCreateWithName(kCGColorSpaceITUR_709);
        ctx_ = [CIContext contextWithOptions:@{kCIContextCacheIntermediates : @NO}];
    }
    ~RawDecoder() override { CGColorSpaceRelease(rec709_); }

    std::string name() const override { return "apple.raw (Core Image)"; }

    std::optional<ProbeResult> probe(const std::string& path, std::string* error) override {
        @autoreleasepool {
            CIRAWFilter* f = [CIRAWFilter filterWithImageURL:fileUrl(path)];
            CIImage* img = f ? f.outputImage : nil;
            if (!img || CGRectIsInfinite(img.extent) || CGRectIsEmpty(img.extent)) {
                if (error) *error = "not a RAW file Core Image can read";
                return std::nullopt;
            }
            const CGRect e = CGRectIntegral(img.extent);
            ProbeResult r;
            r.hasVideo = true;
            r.isStillImage = true;
            r.duration = Time::zero();
            r.videoCodec = "raw";
            r.width = static_cast<int>(e.size.width);
            r.height = static_cast<int>(e.size.height);
            r.bitDepth = 14;
            r.transfer = "bt709";
            r.primaries = "bt709";
            return r;
        }
    }

    std::optional<VideoFrame> decode(const std::string& path, int maxW, int maxH, bool deep, std::string* error) override {
        @autoreleasepool {
            CIRAWFilter* f = [CIRAWFilter filterWithImageURL:fileUrl(path)];
            CIImage* full = f ? f.outputImage : nil;
            if (!full || CGRectIsEmpty(full.extent)) {
                if (error) *error = "not a RAW file Core Image can read";
                return std::nullopt;
            }
            const CGSize n = full.extent.size;
            double k = 1.0;
            if (maxW > 0 && maxH > 0) k = std::min({1.0, maxW / n.width, maxH / n.height});
            if (k < 1.0) {
                f.scaleFactor = static_cast<float>(k);
                f.draftModeEnabled = k <= 0.5;  // preview: faster develop, no visible difference at that size
            }
            CIImage* img = f.outputImage;
            const CGRect e = CGRectIntegral(img.extent);
            const int w = std::max(1, static_cast<int>(std::lround(n.width * k)));
            const int h = std::max(1, static_cast<int>(std::lround(n.height * k)));
            const CGRect bounds = CGRectMake(e.origin.x, e.origin.y, w, h);
            VideoFrame out;
            out.allocate(w, h, deep ? PixelFormat::RGBA16F : PixelFormat::RGBA8);
            void* dst = deep ? static_cast<void*>(out.half.data()) : static_cast<void*>(out.rgba.data());
            [ctx_ render:img
                toBitmap:dst
                rowBytes:static_cast<ptrdiff_t>(w) * (deep ? 8 : 4)
                  bounds:bounds
                  format:deep ? kCIFormatRGBAh : kCIFormatRGBA8
              colorSpace:rec709_];
            return out;
        }
    }

private:
    CGColorSpaceRef rec709_ = nullptr;
    CIContext* ctx_ = nil;
};

}  // namespace

std::shared_ptr<IStillDecoder> createRawDecoder() {
    if (@available(macOS 12.0, iOS 15.0, *)) return std::make_shared<RawDecoder>();
    return nullptr;
}

void registerRawModule(ModuleRegistry& r, int priority) {
    r.add<IStillDecoder>({.id = "apple.raw",
                          .displayName = "Apple RAW (Core Image)",
                          .capabilities = {"decode.raw"},
                          .platforms = {"macos", "ios"},
                          .priority = priority},
                         [] { return createRawDecoder(); });
}

}  // namespace vstudio::apple
