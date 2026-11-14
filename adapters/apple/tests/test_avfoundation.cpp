// Apple AVFoundation module: lazy registration plus the shared backend
// conformance suite (runs on the macOS CI job).
#include "backend_conformance.hpp"
#include "vstudio/apple/avfoundation_module.hpp"

#include <ImageIO/ImageIO.h>

#include <cstdint>
#include <vector>

using namespace vstudio;

TEST("apple: module registers lazily") {
    ModuleRegistry r;
    apple::registerModule(r);
    CHECK(!r.loaded("apple.avfoundation"));
    auto b = r.best<IMediaBackend>("decode");
    CHECK(b != nullptr);
    CHECK(r.loaded("apple.avfoundation"));
    CHECK_EQ(b->name(), std::string("apple.avfoundation"));
}

static vtest::BackendSuite suite("apple", [] { return apple::createBackend(); });

namespace {

// A 64x32 JPEG photo, left half red and right half blue, stored with EXIF orientation 6
// (a phone held upright: shown turned 90 degrees clockwise, so 32x64 with red on top).
std::string makePortraitPhoto() {
    const std::string path = (vtest::conformance::workDir() / "upright.jpg").string();
    constexpr int W = 64, H = 32;
    std::vector<std::uint8_t> px(static_cast<size_t>(W) * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            std::uint8_t* p = &px[(static_cast<size_t>(y) * W + x) * 4];
            const bool left = x < W / 2;
            p[0] = left ? 255 : 0;
            p[1] = 0;
            p[2] = left ? 0 : 255;
            p[3] = 255;
        }
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(px.data(), W, H, 8, W * 4, cs, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CFStringRef path_cf = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path_cf, kCFURLPOSIXPathStyle, false);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, CFSTR("public.jpeg"), 1, nullptr);
    int orientation = 6;
    float quality = 1.0f;
    CFNumberRef o = CFNumberCreate(nullptr, kCFNumberIntType, &orientation);
    CFNumberRef q = CFNumberCreate(nullptr, kCFNumberFloatType, &quality);
    const void* keys[] = {kCGImagePropertyOrientation, kCGImageDestinationLossyCompressionQuality};
    const void* vals[] = {o, q};
    CFDictionaryRef props = CFDictionaryCreate(nullptr, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CGImageDestinationAddImage(dst, img, props);
    const bool ok = CGImageDestinationFinalize(dst);
    CFRelease(props);
    CFRelease(q);
    CFRelease(o);
    CFRelease(dst);
    CFRelease(url);
    CFRelease(path_cf);
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    if (!ok) throw std::runtime_error("cannot write JPEG");
    return path;
}

}  // namespace

TEST("apple: a JPEG photo is read upright (EXIF), at any time and size") {
    auto b = apple::createBackend();
    const std::string path = makePortraitPhoto();
    std::string err;
    auto pr = b->probe(path, &err);
    CHECK(pr.has_value());
    CHECK(pr->isStillImage);
    CHECK_EQ(pr->width, 32);
    CHECK_EQ(pr->height, 64);
    CHECK_EQ(pr->rotation, 0);  // already turned by the decoder
    auto d = b->createDecoder();
    CHECK(d->open(path, &err));
    for (const double s : {0.0, 3.0, 0.0}) {
        auto f = d->videoFrameAt(Time::fromSeconds(s));
        CHECK(f.has_value());
        CHECK_EQ(f->width, 32);
        CHECK_EQ(f->height, 64);
        const std::uint8_t* top = &f->rgba[(8 * 32 + 16) * 4];
        const std::uint8_t* bottom = &f->rgba[(56 * 32 + 16) * 4];
        CHECK(top[0] > 180 && top[2] < 80);     // red on top
        CHECK(bottom[2] > 180 && bottom[0] < 80);  // blue below
    }
    d->setMaxOutputSize(16, 16);  // preview size: fitted, aspect kept
    auto small = d->videoFrameAt(Time::zero());
    CHECK(small.has_value());
    CHECK_EQ(small->width, 8);
    CHECK_EQ(small->height, 16);
}
