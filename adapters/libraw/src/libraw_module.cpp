// LibRaw develops the sensor data: camera white balance, camera colour matrix to
// Rec.709 primaries, Rec.709 transfer curve, 16 bits per channel. Preview sizes use
// LibRaw's half-size mode (one pixel per 2x2 Bayer block: no demosaicing, about 4x
// faster); the result is then box-filtered down to the asked size.
#include "vstudio/libraw/libraw_module.hpp"

#include <libraw/libraw.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

#include "vstudio/pixel.hpp"

namespace vstudio::libraw {

namespace {

std::string why(int e) { return libraw_strerror(e); }

bool openRaw(LibRaw& raw, const std::string& path, std::string* error) {
#if defined(_WIN32)
    // Unicode paths on Windows: LibRaw takes wide strings there.
    const std::wstring w = std::filesystem::path(std::u8string(path.begin(), path.end())).wstring();
    const int e = raw.open_file(w.c_str());
#else
    const int e = raw.open_file(path.c_str());
#endif
    if (e != LIBRAW_SUCCESS) {
        if (error) *error = why(e);
        return false;
    }
    return true;
}

class Decoder final : public IStillDecoder {
public:
    std::string name() const override { return "libraw " + std::string(LibRaw::version()); }

    std::optional<ProbeResult> probe(const std::string& path, std::string* error) override {
        auto raw = std::make_unique<LibRaw>();
        if (!openRaw(*raw, path, error)) return std::nullopt;
        const auto& sz = raw->imgdata.sizes;
        if (sz.width == 0 || sz.height == 0) {
            if (error) *error = "no picture in this RAW file";
            return std::nullopt;
        }
        ProbeResult r;
        r.hasVideo = true;
        r.isStillImage = true;
        r.duration = Time::zero();
        r.videoCodec = "raw";
        const bool turned = (sz.flip & 4) != 0;  // 90 / 270 degrees: the picture comes out turned
        r.width = turned ? sz.height : sz.width;
        r.height = turned ? sz.width : sz.height;
        r.bitDepth = std::max(10, static_cast<int>(raw->imgdata.color.raw_bps ? raw->imgdata.color.raw_bps : 12));
        r.transfer = "bt709";
        r.primaries = "bt709";
        return r;
    }

    std::optional<VideoFrame> decode(const std::string& path, int maxW, int maxH, bool deep, std::string* error) override {
        auto raw = std::make_unique<LibRaw>();
        if (!openRaw(*raw, path, error)) return std::nullopt;
        auto& p = raw->imgdata.params;
        p.output_bps = 16;
        p.output_color = 1;  // sRGB primaries = Rec.709
        p.gamm[0] = 1 / 2.222;  // Rec.709 transfer
        p.gamm[1] = 4.5;
        p.use_camera_wb = 1;
        p.use_camera_matrix = 1;
        p.user_qual = 3;  // AHD
        const auto& sz = raw->imgdata.sizes;
        const bool turned = (sz.flip & 4) != 0;
        const int fullW = turned ? sz.height : sz.width, fullH = turned ? sz.width : sz.height;
        if (maxW > 0 && maxH > 0 && maxW * 2 <= fullW && maxH * 2 <= fullH) p.half_size = 1;
        int e = raw->unpack();
        if (e == LIBRAW_SUCCESS) e = raw->dcraw_process();
        if (e != LIBRAW_SUCCESS) {
            if (error) *error = why(e);
            return std::nullopt;
        }
        libraw_processed_image_t* img = raw->dcraw_make_mem_image(&e);
        if (!img) {
            if (error) *error = why(e);
            return std::nullopt;
        }
        std::unique_ptr<libraw_processed_image_t, void (*)(libraw_processed_image_t*)> hold(img, LibRaw::dcraw_clear_mem);
        if (img->type != LIBRAW_IMAGE_BITMAP || img->colors < 1 || img->bits != 16) {
            if (error) *error = "unexpected developed image format";
            return std::nullopt;
        }
        const int w = img->width, h = img->height, cols = img->colors;
        const auto* px = reinterpret_cast<const std::uint16_t*>(img->data);
        // Output size: fit inside the box, aspect kept.
        int ow = w, oh = h;
        if (maxW > 0 && maxH > 0 && (w > maxW || h > maxH)) {
            const double k = std::min(static_cast<double>(maxW) / w, static_cast<double>(maxH) / h);
            ow = std::max(1, static_cast<int>(std::lround(w * k)));
            oh = std::max(1, static_cast<int>(std::lround(h * k)));
        }
        VideoFrame f;
        f.allocate(ow, oh, deep ? PixelFormat::RGBA16F : PixelFormat::RGBA8);
        const std::uint16_t one = floatToHalf(1.0f);
        // Box filter: each output pixel averages the source pixels it covers.
        for (int y = 0; y < oh; ++y) {
            const int y0 = static_cast<int>(static_cast<long long>(y) * h / oh);
            const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(y + 1) * h / oh));
            for (int x = 0; x < ow; ++x) {
                const int x0 = static_cast<int>(static_cast<long long>(x) * w / ow);
                const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(x + 1) * w / ow));
                double acc[3] = {0, 0, 0};
                for (int sy = y0; sy < y1; ++sy) {
                    const std::uint16_t* row = px + (static_cast<std::size_t>(sy) * w + x0) * cols;
                    for (int sx = x0; sx < x1; ++sx, row += cols)
                        for (int c = 0; c < 3; ++c) acc[c] += row[std::min(c, cols - 1)];
                }
                const double n = static_cast<double>(y1 - y0) * (x1 - x0) * 65535.0;
                const std::size_t o = (static_cast<std::size_t>(y) * ow + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    const double v = acc[c] / n;
                    if (deep) f.half[o + c] = floatToHalf(static_cast<float>(v));
                    else f.rgba[o + c] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
                }
                if (deep) f.half[o + 3] = one;
                else f.rgba[o + 3] = 255;
            }
        }
        return f;
    }
};

}  // namespace

std::shared_ptr<IStillDecoder> createDecoder() { return std::make_shared<Decoder>(); }

void registerModule(ModuleRegistry& r, int priority) {
    r.add<IStillDecoder>({.id = "libraw",
                          .displayName = "LibRaw (ảnh RAW)",
                          .capabilities = {"decode.raw"},
                          .priority = priority},
                         [] { return createDecoder(); });
}

}  // namespace vstudio::libraw
