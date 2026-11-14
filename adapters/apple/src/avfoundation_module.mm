#include "vstudio/apple/avfoundation_module.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <utility>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "vstudio/pixel.hpp"

// The synchronous AVAsset property getters are deprecated from macOS 13 / iOS 16
// in favour of async loading, but we target macOS 11 / iOS 14 and every call
// here already runs on a background thread, so blocking is what we want.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace vstudio::apple {

namespace {

void setErr(std::string* out, const std::string& m) {
    if (out) *out = m;
}

std::string str(NSError* e) { return e ? std::string(e.localizedDescription.UTF8String ?: "") : std::string(); }

NSURL* urlFor(const std::string& path) {
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
}

AVURLAsset* openAsset(const std::string& path, std::string* error) {
    if (![[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:path.c_str()]]) {
        setErr(error, "cannot open " + path);
        return nil;
    }
    AVURLAsset* a = [AVURLAsset URLAssetWithURL:urlFor(path)
                                        options:@{AVURLAssetPreferPreciseDurationAndTimingKey : @YES}];
    if (!a.readable) {
        setErr(error, "unsupported or corrupt media: " + path);
        return nil;
    }
    return a;
}

Time fromCM(CMTime t) {
    if (!CMTIME_IS_NUMERIC(t)) return Time::zero();
    return Time::fromTimebase(t.value, {1, t.timescale});
}

CMTime toCM(Time t) { return CMTimeMake(t.toTimebase({1, 1'000'000}), 1'000'000); }

std::string fourcc(FourCharCode c) {
    std::string s;
    for (int sh = 24; sh >= 0; sh -= 8) {
        const char ch = static_cast<char>((c >> sh) & 0xFF);
        if (ch != ' ' && ch != 0) s += ch;
    }
    return s;
}

// nominalFrameRate is a float: map the usual NTSC rates back to exact rationals.
Rational rateFromFloat(float f) {
    if (f <= 0) return {30, 1};
    for (int base : {24, 30, 60, 120}) {
        if (std::fabs(f - base * 1000.0 / 1001.0) < 0.01) return {base * 1000, 1001};
    }
    if (std::fabs(f - std::round(f)) < 0.01) return {static_cast<int>(std::lround(f)), 1};
    return {static_cast<int>(std::lround(f * 1000)), 1000};
}


// ---- photos (JPEG, HEIC, PNG, ...): ImageIO, upright from their EXIF orientation ----
// AVFoundation reads only audio and video; the core's photo clips need these too.

bool isImagePath(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || path.find_first_of("/\\", dot) != std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* e : {"jpg", "jpeg", "png", "heic", "heif", "webp", "gif", "bmp", "tif", "tiff", "avif"})
        if (ext == e) return true;
    return false;
}

struct CFDel {
    void operator()(CFTypeRef r) const {
        if (r) CFRelease(r);
    }
};
template <class T>
using CFPtr = std::unique_ptr<std::remove_pointer_t<T>, CFDel>;

CFPtr<CGImageSourceRef> openImage(const std::string& path, std::string* error) {
    CFPtr<CGImageSourceRef> src(CGImageSourceCreateWithURL((__bridge CFURLRef)urlFor(path), nullptr));
    if (!src || CGImageSourceGetCount(src.get()) < 1) {
        setErr(error, "unsupported or corrupt media: " + path);
        return nullptr;
    }
    return src;
}

int dictInt(CFDictionaryRef d, CFStringRef key, int fallback) {
    auto n = d ? static_cast<CFNumberRef>(CFDictionaryGetValue(d, key)) : nullptr;
    int v = fallback;
    if (n && CFGetTypeID(n) == CFNumberGetTypeID()) CFNumberGetValue(n, kCFNumberIntType, &v);
    return v;
}

std::optional<ProbeResult> probeImage(const std::string& path, std::string* error) {
    @autoreleasepool {
        auto src = openImage(path, error);
        if (!src) return std::nullopt;
        CFPtr<CFDictionaryRef> props(CGImageSourceCopyPropertiesAtIndex(src.get(), 0, nullptr));
        int w = dictInt(props.get(), kCGImagePropertyPixelWidth, 0), h = dictInt(props.get(), kCGImagePropertyPixelHeight, 0);
        if (w <= 0 || h <= 0) {
            setErr(error, "unsupported or corrupt media: " + path);
            return std::nullopt;
        }
        // Frames come out already turned upright (decodeImage), so the size is the upright one.
        if (dictInt(props.get(), kCGImagePropertyOrientation, 1) >= 5) std::swap(w, h);
        ProbeResult r;
        r.hasVideo = true;
        r.isStillImage = true;
        r.duration = Time::zero();
        r.fps = {30, 1};
        r.width = w;
        r.height = h;
        r.videoCodec = "image";
        r.bitDepth = 8;
        return r;
    }
}

// The photo, upright, fitted inside maxW x maxH (0 = full size), straight-alpha RGBA8.
std::optional<VideoFrame> decodeImage(const std::string& path, int fullW, int fullH, int maxW, int maxH, std::string* error) {
    @autoreleasepool {
        auto src = openImage(path, error);
        if (!src) return std::nullopt;
        int maxSide = std::max(fullW, fullH);
        if (maxW > 0 && maxH > 0 && (fullW > maxW || fullH > maxH)) {
            const double k = std::min(static_cast<double>(maxW) / fullW, static_cast<double>(maxH) / fullH);
            maxSide = std::max(1, static_cast<int>(std::lround(std::max(fullW, fullH) * k)));
        }
        NSDictionary* opts = @{
            (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
            (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform : @YES,  // EXIF orientation
            (__bridge NSString*)kCGImageSourceShouldCacheImmediately : @YES,
            (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize : @(maxSide),
        };
        CFPtr<CGImageRef> img(CGImageSourceCreateThumbnailAtIndex(src.get(), 0, (__bridge CFDictionaryRef)opts));
        if (!img) {
            setErr(error, "unsupported or corrupt media: " + path);
            return std::nullopt;
        }
        const int w = static_cast<int>(CGImageGetWidth(img.get())), h = static_cast<int>(CGImageGetHeight(img.get()));
        VideoFrame out;
        out.allocate(w, h);
        CFPtr<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
        CFPtr<CGContextRef> ctx(CGBitmapContextCreate(out.rgba.data(), w, h, 8, static_cast<size_t>(w) * 4, srgb.get(),
                                                      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big));
        if (!ctx) {
            setErr(error, "out of memory decoding " + path);
            return std::nullopt;
        }
        CGContextDrawImage(ctx.get(), CGRectMake(0, 0, w, h), img.get());
        // CoreGraphics only draws premultiplied; the core works with straight alpha.
        for (size_t i = 0; i < out.rgba.size(); i += 4) {
            const int a = out.rgba[i + 3];
            if (a == 255 || a == 0) continue;
            for (int c = 0; c < 3; ++c) out.rgba[i + c] = static_cast<std::uint8_t>(std::min(255, out.rgba[i + c] * 255 / a));
        }
        return out;
    }
}

std::optional<ProbeResult> probeFile(const std::string& path, std::string* error) {
    if (isImagePath(path)) return probeImage(path, error);
    @autoreleasepool {
        AVURLAsset* asset = openAsset(path, error);
        if (!asset) {
            // No extension to go by (a picker's copy): it may still be a photo.
            std::string imageError;
            if (auto r = probeImage(path, &imageError)) return r;
            return std::nullopt;
        }
        AVAssetTrack* v = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
        AVAssetTrack* a = [asset tracksWithMediaType:AVMediaTypeAudio].firstObject;
        if (!v && !a) {
            setErr(error, "no audio or video track");
            return std::nullopt;
        }
        ProbeResult r;
        if (v) {
            r.hasVideo = true;
            const CGSize sz = v.naturalSize;
            r.width = static_cast<int>(std::lround(sz.width));
            r.height = static_cast<int>(std::lround(sz.height));
            r.fps = rateFromFloat(v.nominalFrameRate);
            r.vfr = v.minFrameDuration.timescale > 0 && v.nominalFrameRate > 0 &&
                    std::fabs(CMTimeGetSeconds(v.minFrameDuration) * v.nominalFrameRate - 1.0) > 0.05;
            const CGAffineTransform t = v.preferredTransform;
            r.rotation = (static_cast<int>(std::lround(std::atan2(t.b, t.a) * 180.0 / M_PI)) + 360) % 360;
            if (id desc = v.formatDescriptions.firstObject) {
                auto fd = (__bridge CMFormatDescriptionRef)desc;
                r.videoCodec = fourcc(CMFormatDescriptionGetMediaSubType(fd));
                // Colour description: bit depth and transfer, named like FFmpeg does.
                if (auto bits = (CFNumberRef)CMFormatDescriptionGetExtension(fd, kCMFormatDescriptionExtension_BitsPerComponent);
                    bits && CFGetTypeID(bits) == CFNumberGetTypeID())
                    CFNumberGetValue(bits, kCFNumberIntType, &r.bitDepth);
                if (auto tf = (CFStringRef)CMFormatDescriptionGetExtension(fd, kCMFormatDescriptionExtension_TransferFunction);
                    tf && CFGetTypeID(tf) == CFStringGetTypeID()) {
                    if (CFEqual(tf, kCVImageBufferTransferFunction_ITU_R_2100_HLG)) r.transfer = "arib-std-b67";
                    else if (CFEqual(tf, kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ)) r.transfer = "smpte2084";
                    else if (CFEqual(tf, kCVImageBufferTransferFunction_ITU_R_709_2)) r.transfer = "bt709";
                }
                if (auto pr = (CFStringRef)CMFormatDescriptionGetExtension(fd, kCMFormatDescriptionExtension_ColorPrimaries);
                    pr && CFGetTypeID(pr) == CFStringGetTypeID()) {
                    if (CFEqual(pr, kCVImageBufferColorPrimaries_ITU_R_2020)) r.primaries = "bt2020";
                    else if (CFEqual(pr, kCVImageBufferColorPrimaries_ITU_R_709_2)) r.primaries = "bt709";
                }
            }
            r.duration = fromCM(v.timeRange.duration);
        }
        if (a) {
            r.hasAudio = true;
            if (id desc = a.formatDescriptions.firstObject) {
                auto fd = (__bridge CMAudioFormatDescriptionRef)desc;
                r.audioCodec = fourcc(CMFormatDescriptionGetMediaSubType(fd));
                if (const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription(fd)) {
                    r.sampleRate = static_cast<int>(asbd->mSampleRate);
                    r.channels = static_cast<int>(asbd->mChannelsPerFrame);
                }
            }
            if (!r.hasVideo) r.duration = fromCM(a.timeRange.duration);
        }
        if (r.duration == Time::zero()) r.duration = fromCM(asset.duration);
        return r;
    }
}

// ------------------------------------------------------------------ decoder

class Decoder final : public IMediaDecoder {
public:
    bool open(const std::string& path, std::string* error) override {
        auto pr = probeFile(path, error);
        if (!pr) return false;
        probe_ = *pr;
        path_ = path;
        if (probe_.isStillImage) return true;  // a photo: ImageIO, decoded once (stillFrame)
        asset_ = openAsset(path, error);
        return asset_ != nil;
    }

    void setMaxOutputSize(int w, int h) override {
        maxW_ = w;
        maxH_ = h;
        still_.reset();
    }
    void setDeepOutput(bool on) override { deep_ = on; }

    std::optional<VideoFrame> videoFrameAt(Time t) override {
        if (!probe_.hasVideo) return std::nullopt;
        if (probe_.isStillImage) {
            if (!still_) {
                std::string err;
                still_ = decodeImage(path_, probe_.width, probe_.height, maxW_, maxH_, &err);
                if (!still_) return std::nullopt;
            }
            VideoFrame f = *still_;
            f.pts = t;
            return f;
        }
        @autoreleasepool {
            if (cur_ && t >= curPts_ && (!pendingValid_ || t < pendingPts_)) return *cur_;
            if (!vout_ || !cur_ || t < curPts_ || t - curPts_ > Time::fromSeconds(2)) {
                if (!startVideo(t)) return std::nullopt;
            }
            for (;;) {
                if (pendingValid_) {
                    if (pendingPts_ <= t || !cur_) {
                        cur_ = std::make_shared<VideoFrame>(std::move(pending_));
                        curPts_ = pendingPts_;
                        pendingValid_ = false;
                    } else {
                        break;
                    }
                }
                if (!readVideo()) break;
                pendingValid_ = true;
            }
            if (!cur_) return std::nullopt;
            return *cur_;
        }
    }

    AudioBuffer readAudio(Time t, std::int64_t frames, int sr, int ch) override {
        AudioBuffer out;
        out.sampleRate = sr;
        out.channels = ch;
        out.samples.assign(static_cast<size_t>(std::max<std::int64_t>(0, frames)) * ch, 0.0f);
        if (!probe_.hasAudio || frames <= 0) return out;
        @autoreleasepool {
            const std::int64_t want = t.toSample(sr);
            const std::int64_t have = static_cast<std::int64_t>(fifo_.size()) / ch;
            if (!aout_ || sr != aRate_ || ch != aCh_ || fifoStart_ < 0 || want < fifoStart_ ||
                want > fifoStart_ + have + sr) {
                if (!startAudio(t, sr, ch)) return out;
            }
            while (fifoStart_ < 0 || fifoStart_ + static_cast<std::int64_t>(fifo_.size()) / ch < want + frames) {
                if (!readAudioChunk()) break;
            }
            if (fifoStart_ < 0) return out;
            const std::int64_t total = static_cast<std::int64_t>(fifo_.size()) / ch;
            const std::int64_t from = std::max(want, fifoStart_), to = std::min(want + frames, fifoStart_ + total);
            for (std::int64_t s = from; s < to; ++s)
                std::memcpy(&out.samples[static_cast<size_t>(s - want) * ch],
                            &fifo_[static_cast<size_t>(s - fifoStart_) * ch], sizeof(float) * ch);
            const std::int64_t drop = std::clamp<std::int64_t>(want + frames - fifoStart_, 0, total);
            fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<std::ptrdiff_t>(drop) * ch);
            fifoStart_ += drop;
        }
        return out;
    }

private:
    void targetSize(int& w, int& h) const {
        w = probe_.width;
        h = probe_.height;
        if (maxW_ > 0 && maxH_ > 0 && (w > maxW_ || h > maxH_)) {
            const double k = std::min(static_cast<double>(maxW_) / w, static_cast<double>(maxH_) / h);
            w = std::max(2, static_cast<int>(w * k) & ~1);
            h = std::max(2, static_cast<int>(h * k) & ~1);
        }
    }

    bool startVideo(Time t) {
        [vreader_ cancelReading];
        vreader_ = nil;
        vout_ = nil;
        cur_.reset();
        pendingValid_ = false;
        AVAssetTrack* track = [asset_ tracksWithMediaType:AVMediaTypeVideo].firstObject;
        if (!track) return false;
        NSError* e = nil;
        vreader_ = [AVAssetReader assetReaderWithAsset:asset_ error:&e];
        if (!vreader_) return false;
        int w, h;
        targetSize(w, h);
        // VideoToolbox decodes and scales straight to the preview size: BGRA, or half-float
        // RGBA for 10-bit sources when deep output is asked (no rounding to 8 bits).
        const bool deepNow = deep_ && probe_.bitDepth > 8;
        NSDictionary* settings = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(deepNow ? kCVPixelFormatType_64RGBAHalf : kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey : @(w),
            (id)kCVPixelBufferHeightKey : @(h),
        };
        vout_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:settings];
        vout_.alwaysCopiesSampleData = NO;
        if (![vreader_ canAddOutput:vout_]) return false;
        [vreader_ addOutput:vout_];
        // Start slightly early so the frame on screen at t is included.
        const Time start = std::max(Time::zero(), t - Time::fromMilliseconds(100));
        vreader_.timeRange = CMTimeRangeMake(toCM(start), kCMTimePositiveInfinity);
        return [vreader_ startReading];
    }

    bool readVideo() {
        if (!vout_ || vreader_.status != AVAssetReaderStatusReading) return false;
        CMSampleBufferRef sb = [vout_ copyNextSampleBuffer];
        if (!sb) return false;
        bool ok = false;
        if (CVImageBufferRef px = CMSampleBufferGetImageBuffer(sb)) {
            int w, h;
            targetSize(w, h);
            if (CVPixelBufferGetPixelFormatType(px) == kCVPixelFormatType_64RGBAHalf)
                copyHalf(px, pending_, w, h);
            else
                copyBgra(px, pending_, w, h);
            pendingPts_ = fromCM(CMSampleBufferGetPresentationTimeStamp(sb));
            pending_.pts = pendingPts_;
            ok = true;
        }
        CFRelease(sb);
        return ok || readVideo();  // skip sample buffers without an image (markers)
    }

    // BGRA pixel buffer -> RGBA frame of w x h (point-sampled if the decoder did not scale).
    static void copyBgra(CVPixelBufferRef px, VideoFrame& out, int w, int h) {
        CVPixelBufferLockBaseAddress(px, kCVPixelBufferLock_ReadOnly);
        const int sw = static_cast<int>(CVPixelBufferGetWidth(px));
        const int sh = static_cast<int>(CVPixelBufferGetHeight(px));
        const size_t bpr = CVPixelBufferGetBytesPerRow(px);
        const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(px));
        out.allocate(w, h);
        std::vector<int> cols(static_cast<size_t>(w));
        for (int x = 0; x < w; ++x) cols[static_cast<size_t>(x)] = std::min(sw - 1, static_cast<int>((2LL * x + 1) * sw / (2LL * w)));
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = base + static_cast<size_t>(std::min(sh - 1, static_cast<int>((2LL * y + 1) * sh / (2LL * h)))) * bpr;
            uint8_t* d = out.rgba.data() + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, d += 4) {
                const uint8_t* s = row + static_cast<size_t>(cols[static_cast<size_t>(x)]) * 4;
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 255;
            }
        }
        CVPixelBufferUnlockBaseAddress(px, kCVPixelBufferLock_ReadOnly);
    }

    // Half-float RGBA pixel buffer -> RGBA16F frame (same layout, opaque alpha).
    static void copyHalf(CVPixelBufferRef px, VideoFrame& out, int w, int h) {
        CVPixelBufferLockBaseAddress(px, kCVPixelBufferLock_ReadOnly);
        const int sw = static_cast<int>(CVPixelBufferGetWidth(px));
        const int sh = static_cast<int>(CVPixelBufferGetHeight(px));
        const size_t bpr = CVPixelBufferGetBytesPerRow(px);
        const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(px));
        out.allocate(w, h, PixelFormat::RGBA16F);
        const uint16_t one = floatToHalf(1.0f);
        for (int y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(
                base + static_cast<size_t>(std::min(sh - 1, static_cast<int>((2LL * y + 1) * sh / (2LL * h)))) * bpr);
            uint16_t* d = out.half.data() + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, d += 4) {
                const uint16_t* s = row + static_cast<size_t>(std::min(sw - 1, static_cast<int>((2LL * x + 1) * sw / (2LL * w)))) * 4;
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = one;
            }
        }
        CVPixelBufferUnlockBaseAddress(px, kCVPixelBufferLock_ReadOnly);
    }

    bool startAudio(Time t, int sr, int ch) {
        [areader_ cancelReading];
        areader_ = nil;
        aout_ = nil;
        fifo_.clear();
        fifoStart_ = -1;
        aRate_ = sr;
        aCh_ = ch;
        AVAssetTrack* track = [asset_ tracksWithMediaType:AVMediaTypeAudio].firstObject;
        if (!track) return false;
        NSError* e = nil;
        areader_ = [AVAssetReader assetReaderWithAsset:asset_ error:&e];
        if (!areader_) return false;
        // Core Audio converts to interleaved float at the requested rate and channel count.
        AudioChannelLayout layout{};
        layout.mChannelLayoutTag = ch == 1 ? kAudioChannelLayoutTag_Mono : kAudioChannelLayoutTag_Stereo;
        NSDictionary* settings = @{
            AVFormatIDKey : @(kAudioFormatLinearPCM),
            AVLinearPCMIsFloatKey : @YES,
            AVLinearPCMBitDepthKey : @32,
            AVLinearPCMIsNonInterleaved : @NO,
            AVLinearPCMIsBigEndianKey : @NO,
            AVSampleRateKey : @(sr),
            AVNumberOfChannelsKey : @(ch),
            AVChannelLayoutKey : [NSData dataWithBytes:&layout length:sizeof(layout)],
        };
        aout_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:settings];
        aout_.alwaysCopiesSampleData = NO;
        if (![areader_ canAddOutput:aout_]) return false;
        [areader_ addOutput:aout_];
        areader_.timeRange = CMTimeRangeMake(toCM(t), kCMTimePositiveInfinity);
        return [areader_ startReading];
    }

    bool readAudioChunk() {
        if (!aout_ || areader_.status != AVAssetReaderStatusReading) return false;
        CMSampleBufferRef sb = [aout_ copyNextSampleBuffer];
        if (!sb) return false;
        CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
        const size_t bytes = bb ? CMBlockBufferGetDataLength(bb) : 0;
        if (bytes) {
            if (fifoStart_ < 0) fifoStart_ = fromCM(CMSampleBufferGetPresentationTimeStamp(sb)).toSample(aRate_, Rounding::Nearest);
            const size_t old = fifo_.size();
            fifo_.resize(old + bytes / sizeof(float));
            CMBlockBufferCopyDataBytes(bb, 0, (bytes / sizeof(float)) * sizeof(float), fifo_.data() + old);
        }
        CFRelease(sb);
        return true;
    }

    ProbeResult probe_;
    std::string path_;
    std::optional<VideoFrame> still_;  // a photo, at the current output size
    AVURLAsset* asset_ = nil;
    int maxW_ = 0, maxH_ = 0;
    bool deep_ = false;

    AVAssetReader* vreader_ = nil;
    AVAssetReaderTrackOutput* vout_ = nil;
    std::shared_ptr<VideoFrame> cur_;
    Time curPts_;
    VideoFrame pending_;
    Time pendingPts_;
    bool pendingValid_ = false;

    AVAssetReader* areader_ = nil;
    AVAssetReaderTrackOutput* aout_ = nil;
    int aRate_ = 0, aCh_ = 0;
    std::vector<float> fifo_;
    std::int64_t fifoStart_ = -1;
};

// ------------------------------------------------------------------ encoder

class Encoder final : public IMediaEncoder {
public:
    ~Encoder() override {
        clearQueues();
        if (writer_ && !finished_ && writer_.status == AVAssetWriterStatusWriting) [writer_ cancelWriting];
        if (afmt_) CFRelease(afmt_);
    }

    bool open(const std::string& path, const ExportSettings& s, std::string* error) override {
        @autoreleasepool {
            s_ = s;
            NSURL* url = urlFor(path);
            [[NSFileManager defaultManager] removeItemAtURL:url error:nil];  // AVAssetWriter refuses to overwrite
            NSError* e = nil;
            writer_ = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&e];
            if (!writer_) return fail(error, "cannot write " + path + ": " + str(e));

            const double fps = std::max(1.0, s.fps.toDouble());
            // H.265 runs on the media engine of every Apple-silicon Mac and iPhone (VideoToolbox);
            // Main 10 takes 10-bit 4:2:0 buffers that writeVideo() fills straight from the float frame.
            const bool hevc = s.videoCodec == "hevc" || s.videoCodec == "h265";
            tenBit_ = s.bitDepth > 8;
            if (tenBit_ && !hevc) return fail(error, "10-bit export needs H.265 (videoCodec \"hevc\")");
            NSDictionary* video = @{
                AVVideoCodecKey : hevc ? AVVideoCodecTypeHEVC : AVVideoCodecTypeH264,
                AVVideoWidthKey : @(s.width),
                AVVideoHeightKey : @(s.height),
                // Tag the colour space so every decoder converts back with the same matrix.
                AVVideoColorPropertiesKey : @{
                    AVVideoColorPrimariesKey : AVVideoColorPrimaries_ITU_R_709_2,
                    AVVideoTransferFunctionKey : AVVideoTransferFunction_ITU_R_709_2,
                    AVVideoYCbCrMatrixKey : AVVideoYCbCrMatrix_ITU_R_709_2,
                },
                AVVideoCompressionPropertiesKey : @{
                    AVVideoAverageBitRateKey : @(s.videoBitrate),
                    AVVideoMaxKeyFrameIntervalKey : @(s.gopSize > 0 ? s.gopSize : static_cast<int>(std::lround(fps * 2))),
                    // kVTProfileLevel_HEVC_Main10_AutoLevel / _Main_AutoLevel (VideoToolbox constants' values).
                    AVVideoProfileLevelKey : hevc ? (tenBit_ ? @"HEVC_Main10_AutoLevel" : @"HEVC_Main_AutoLevel")
                                                  : AVVideoProfileLevelH264HighAutoLevel,
                },
            };
            vin_ = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:video];
            vin_.expectsMediaDataInRealTime = NO;
            adaptor_ = [AVAssetWriterInputPixelBufferAdaptor
                assetWriterInputPixelBufferAdaptorWithAssetWriterInput:vin_
                                           sourcePixelBufferAttributes:@{
                                               (id)kCVPixelBufferPixelFormatTypeKey :
                                                   @(tenBit_ ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
                                                             : kCVPixelFormatType_32BGRA),
                                               (id)kCVPixelBufferWidthKey : @(s.width),
                                               (id)kCVPixelBufferHeightKey : @(s.height),
                                           }];
            if (![writer_ canAddInput:vin_])
                return fail(error, hevc ? (tenBit_ ? "no H.265 10-bit encoder on this device" : "H.265 encoding is not available")
                                        : "H.264 encoding is not available");
            [writer_ addInput:vin_];

            if (s.includeAudio) {
                AudioChannelLayout layout{};
                layout.mChannelLayoutTag = s.channels == 1 ? kAudioChannelLayoutTag_Mono : kAudioChannelLayoutTag_Stereo;
                NSDictionary* audio = @{
                    AVFormatIDKey : @(kAudioFormatMPEG4AAC),
                    AVSampleRateKey : @(s.sampleRate),
                    AVNumberOfChannelsKey : @(s.channels),
                    AVEncoderBitRateKey : @(s.audioBitrate),
                    AVChannelLayoutKey : [NSData dataWithBytes:&layout length:sizeof(layout)],
                };
                ain_ = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:audio];
                ain_.expectsMediaDataInRealTime = NO;
                if (![writer_ canAddInput:ain_]) return fail(error, "AAC encoding is not available");
                [writer_ addInput:ain_];

                AudioStreamBasicDescription asbd{};
                asbd.mSampleRate = s.sampleRate;
                asbd.mFormatID = kAudioFormatLinearPCM;
                asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
                asbd.mChannelsPerFrame = static_cast<UInt32>(s.channels);
                asbd.mBitsPerChannel = 32;
                asbd.mBytesPerFrame = asbd.mBytesPerPacket = 4 * asbd.mChannelsPerFrame;
                asbd.mFramesPerPacket = 1;
                if (CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, 0, nullptr, 0, nullptr, nullptr, &afmt_) != noErr)
                    return fail(error, "cannot describe PCM audio");
            }
            if (![writer_ startWriting]) return fail(error, "cannot start writer: " + str(writer_.error));
            [writer_ startSessionAtSourceTime:kCMTimeZero];
            return true;
        }
    }

    // Deep frames are taken by every codec (an 8-bit export converts them here).
    [[nodiscard]] bool supportsDeep() const override { return true; }

    bool writeVideo(const VideoFrame& f, std::string* error) override {
        if (f.width != s_.width || f.height != s_.height) return fail(error, "frame size mismatch");
        @autoreleasepool {
            CVPixelBufferRef px = nullptr;
            if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, adaptor_.pixelBufferPool, &px) != kCVReturnSuccess)
                return fail(error, "cannot allocate a pixel buffer");
            CVPixelBufferLockBaseAddress(px, 0);
            if (tenBit_) {
                fillTenBit(f, px);
            } else if (f.deep()) {
                VideoFrame f8 = f;
                convertFrame(f8, PixelFormat::RGBA8);
                fillBgra(f8, px);
            } else {
                fillBgra(f, px);
            }
            CVPixelBufferUnlockBaseAddress(px, 0);
            const CMTime pts = CMTimeMake(videoIndex_ * s_.fps.den, static_cast<int32_t>(s_.fps.num));
            ++videoIndex_;
            vq_.push_back({px, pts});
            return pump(false, error);
        }
    }

    static void fillBgra(const VideoFrame& f, CVPixelBufferRef px) {
        {
            auto* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(px));
            const size_t bpr = CVPixelBufferGetBytesPerRow(px);
            for (int y = 0; y < f.height; ++y) {
                const uint8_t* s = f.rgba.data() + static_cast<size_t>(y) * f.width * 4;
                uint8_t* d = base + static_cast<size_t>(y) * bpr;
                for (int x = 0; x < f.width; ++x, s += 4, d += 4) {
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    d[3] = 255;
                }
            }
        }
    }

    // RGB 0..1 -> Rec.709 limited-range 10-bit 4:2:0, two planes (Y; interleaved CbCr),
    // 10 bits in the high bits of 16-bit words. Chroma is the average of each 2x2 block.
    static void fillTenBit(const VideoFrame& f, CVPixelBufferRef px) {
        auto rgb = [&](int x, int y, float* o) {
            const std::size_t i = (static_cast<std::size_t>(y) * f.width + x) * 4;
            for (int k = 0; k < 3; ++k)
                o[k] = f.deep() ? std::clamp(halfToFloat(f.half[i + k]), 0.0f, 1.0f) : f.rgba[i + k] / 255.0f;
        };
        auto word = [](double v, double lo, double range) {
            return static_cast<std::uint16_t>(std::clamp(std::lround(lo + v * range), 0L, 1023L) << 6);
        };
        auto* yBase = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(px, 0));
        auto* cBase = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(px, 1));
        const size_t yBpr = CVPixelBufferGetBytesPerRowOfPlane(px, 0), cBpr = CVPixelBufferGetBytesPerRowOfPlane(px, 1);
        for (int y = 0; y < f.height; y += 2) {
            for (int x = 0; x < f.width; x += 2) {
                double cb = 0, cr = 0;
                int n = 0;
                for (int dy = 0; dy < 2 && y + dy < f.height; ++dy)
                    for (int dx = 0; dx < 2 && x + dx < f.width; ++dx, ++n) {
                        float c[3];
                        rgb(x + dx, y + dy, c);
                        const double l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
                        reinterpret_cast<std::uint16_t*>(yBase + (y + dy) * yBpr)[x + dx] = word(l, 64, 876);
                        cb += (c[2] - l) / 1.8556;
                        cr += (c[0] - l) / 1.5748;
                    }
                auto* c = reinterpret_cast<std::uint16_t*>(cBase + (y / 2) * cBpr) + x;
                c[0] = word(cb / n, 512, 896);
                c[1] = word(cr / n, 512, 896);
            }
        }
    }

    bool writeAudio(const AudioBuffer& a, std::string* error) override {
        if (!ain_ || a.samples.empty()) return true;
        @autoreleasepool {
            const std::int64_t frames = a.frames();
            const size_t bytes = a.samples.size() * sizeof(float);
            CMBlockBufferRef block = nullptr;
            if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault, nullptr, 0, bytes,
                                                   kCMBlockBufferAssureMemoryNowFlag, &block) != noErr)
                return fail(error, "cannot allocate audio buffer");
            CMBlockBufferReplaceDataBytes(a.samples.data(), block, 0, bytes);
            CMSampleBufferRef sb = nullptr;
            const CMTime pts = CMTimeMake(audioFrames_, s_.sampleRate);
            const OSStatus st = CMAudioSampleBufferCreateReadyWithPacketDescriptions(
                kCFAllocatorDefault, block, afmt_, static_cast<CMItemCount>(frames), pts, nullptr, &sb);
            CFRelease(block);
            if (st != noErr) return fail(error, "cannot wrap audio samples");
            audioFrames_ += frames;
            aq_.push_back(sb);
            return pump(false, error);
        }
    }

    bool finish(std::string* error) override {
        if (finished_) return true;
        finished_ = true;
        @autoreleasepool {
            if (!pump(true, error)) return false;
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            [writer_ finishWritingWithCompletionHandler:^{
              dispatch_semaphore_signal(done);
            }];
            dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
            if (writer_.status != AVAssetWriterStatusCompleted) return fail(error, "cannot finish MP4: " + str(writer_.error));
            return true;
        }
    }

private:
    bool fail(std::string* e, const std::string& m) {
        setErr(e, m);
        return false;
    }

    // AVAssetWriter interleaves tracks: an input reports "not ready" while it
    // waits for the other track to catch up. So samples are queued per track
    // and handed over whenever their input is ready; the caller only blocks
    // when a queue is long (bounded memory) or when flushing at the end.
    bool pump(bool flush, std::string* error) {
        constexpr size_t kMaxQueued = 32;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        for (;;) {
            bool progressed = false;
            while (!vq_.empty() && vin_.readyForMoreMediaData) {
                const auto [px, pts] = vq_.front();
                vq_.pop_front();
                const BOOL ok = [adaptor_ appendPixelBuffer:px withPresentationTime:pts];
                CVPixelBufferRelease(px);
                if (!ok) return fail(error, "video encode failed: " + str(writer_.error));
                progressed = true;
            }
            while (!aq_.empty() && ain_.readyForMoreMediaData) {
                CMSampleBufferRef sb = aq_.front();
                aq_.pop_front();
                const BOOL ok = [ain_ appendSampleBuffer:sb];
                CFRelease(sb);
                if (!ok) return fail(error, "audio encode failed: " + str(writer_.error));
                progressed = true;
            }
            if (flush) {
                // A track with nothing left must say so, or the writer keeps
                // holding the other track back waiting to interleave.
                if (aq_.empty() && ain_ && !aFinished_) {
                    [ain_ markAsFinished];
                    aFinished_ = true;
                    progressed = true;
                }
                if (vq_.empty() && !vFinished_) {
                    [vin_ markAsFinished];
                    vFinished_ = true;
                    progressed = true;
                }
            }
            if (writer_.status == AVAssetWriterStatusFailed) return fail(error, "encoder failed: " + str(writer_.error));
            const bool done = flush ? (vq_.empty() && aq_.empty()) : (vq_.size() < kMaxQueued && aq_.size() < kMaxQueued);
            if (done) return true;
            if (progressed) deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            else if (std::chrono::steady_clock::now() > deadline)
                return fail(error, "encoder stalled (queued video " + std::to_string(vq_.size()) + ", audio " +
                                       std::to_string(aq_.size()) + ", status " + std::to_string(static_cast<int>(writer_.status)) + ")");
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }

    void clearQueues() {
        for (auto& v : vq_) CVPixelBufferRelease(v.first);
        for (auto* a : aq_) CFRelease(a);
        vq_.clear();
        aq_.clear();
    }

    ExportSettings s_;
    AVAssetWriter* writer_ = nil;
    AVAssetWriterInput* vin_ = nil;
    AVAssetWriterInput* ain_ = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor_ = nil;
    CMAudioFormatDescriptionRef afmt_ = nullptr;
    std::deque<std::pair<CVPixelBufferRef, CMTime>> vq_;
    std::deque<CMSampleBufferRef> aq_;
    std::int64_t videoIndex_ = 0, audioFrames_ = 0;
    bool tenBit_ = false;
    bool finished_ = false, vFinished_ = false, aFinished_ = false;
};

class Backend final : public IMediaBackend {
public:
    std::string name() const override { return "apple.avfoundation"; }
    std::optional<ProbeResult> probe(const std::string& path, std::string* error) override { return probeFile(path, error); }
    std::unique_ptr<IMediaDecoder> createDecoder() override { return std::make_unique<Decoder>(); }
    std::unique_ptr<IMediaEncoder> createEncoder() override { return std::make_unique<Encoder>(); }
};

}  // namespace

std::shared_ptr<IMediaBackend> createBackend() { return std::make_shared<Backend>(); }

void registerModule(ModuleRegistry& r, int priority) {
    r.add<IMediaBackend>({.id = "apple.avfoundation",
                          .displayName = "Apple AVFoundation",
                          .capabilities = {"probe", "decode", "encode.h264", "encode.hevc", "encode.aac"},
                          .platforms = {"macos", "ios"},
                          .priority = priority},
                         [] { return createBackend(); });
}

}  // namespace vstudio::apple
