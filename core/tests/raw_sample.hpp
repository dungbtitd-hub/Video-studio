// A tiny camera RAW file for tests: an uncompressed DNG (TIFF + Bayer CFA), 14-bit, whose
// scene is a horizontal grey ramp. Every RAW decoder (LibRaw, Apple's RAW engine) reads DNG.
#pragma once

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace vtest {

inline void writeTestDng(const std::string& path, int W, int H, int orientation = 1) {
    struct Tag {
        std::uint16_t id, type;
        std::uint32_t count;
        std::vector<std::uint8_t> data;  // little endian payload
    };
    auto le16 = [](std::vector<std::uint8_t>& v, std::uint32_t x) {
        v.push_back(static_cast<std::uint8_t>(x));
        v.push_back(static_cast<std::uint8_t>(x >> 8));
    };
    auto le32 = [](std::vector<std::uint8_t>& v, std::uint32_t x) {
        for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
    };
    enum : std::uint16_t { BYTE = 1, ASCII = 2, SHORT = 3, LONG = 4, RATIONAL = 5, SRATIONAL = 10 };
    std::vector<Tag> tags;
    auto shorts = [&](std::uint16_t id, std::vector<std::uint32_t> xs) {
        Tag t{id, SHORT, static_cast<std::uint32_t>(xs.size()), {}};
        for (auto x : xs) le16(t.data, x);
        tags.push_back(t);
    };
    auto longs = [&](std::uint16_t id, std::vector<std::uint32_t> xs) {
        Tag t{id, LONG, static_cast<std::uint32_t>(xs.size()), {}};
        for (auto x : xs) le32(t.data, x);
        tags.push_back(t);
    };
    auto bytes = [&](std::uint16_t id, std::vector<std::uint8_t> xs) {
        tags.push_back({id, BYTE, static_cast<std::uint32_t>(xs.size()), xs});
    };
    auto ascii = [&](std::uint16_t id, const std::string& s) {
        Tag t{id, ASCII, static_cast<std::uint32_t>(s.size() + 1), {}};
        t.data.assign(s.begin(), s.end());
        t.data.push_back(0);
        tags.push_back(t);
    };
    auto rationals = [&](std::uint16_t id, std::uint16_t type, std::vector<double> xs) {
        Tag t{id, type, static_cast<std::uint32_t>(xs.size()), {}};
        for (double x : xs) {
            le32(t.data, static_cast<std::uint32_t>(static_cast<std::int32_t>(x * 10000.0 + (x < 0 ? -0.5 : 0.5))));
            le32(t.data, 10000);
        }
        tags.push_back(t);
    };
    const std::uint32_t white = 16383;  // 14-bit sensor
    const std::uint32_t imageBytes = static_cast<std::uint32_t>(W) * H * 2;
    longs(254, {0});
    longs(256, {static_cast<std::uint32_t>(W)});
    longs(257, {static_cast<std::uint32_t>(H)});
    shorts(258, {16});
    shorts(259, {1});
    shorts(262, {32803});  // CFA
    ascii(271, "VideoStudio");
    ascii(272, "Test");
    longs(273, {0});  // strip offset, patched below
    shorts(274, {static_cast<std::uint32_t>(orientation)});
    shorts(277, {1});
    longs(278, {static_cast<std::uint32_t>(H)});
    longs(279, {imageBytes});
    shorts(284, {1});
    shorts(33421, {2, 2});
    bytes(33422, {0, 1, 1, 2});  // RGGB
    bytes(50706, {1, 4, 0, 0});
    ascii(50708, "VideoStudio Test");
    longs(50714, {0});
    longs(50717, {white});
    // XYZ -> camera: the camera "sees" linear Rec.709 / sRGB.
    rationals(50721, SRATIONAL,
              {3.2406, -1.5372, -0.4986, -0.9689, 1.8758, 0.0415, 0.0557, -0.2040, 1.0570});
    rationals(50728, RATIONAL, {1, 1, 1});
    shorts(50778, {21});  // D65

    // Layout: header, IFD, out-of-line tag data, image.
    std::vector<std::uint8_t> out{'I', 'I', 42, 0};
    le32(out, 8);
    const std::uint32_t ifdSize = 2 + static_cast<std::uint32_t>(tags.size()) * 12 + 4;
    std::uint32_t extra = 8 + ifdSize;
    std::vector<std::uint8_t> extraData;
    std::vector<std::uint8_t> ifd;
    le16(ifd, static_cast<std::uint32_t>(tags.size()));
    std::size_t stripOffsetPos = 0;
    for (const auto& t : tags) {
        le16(ifd, t.id);
        le16(ifd, t.type);
        le32(ifd, t.count);
        if (t.id == 273) stripOffsetPos = 8 + ifd.size();
        if (t.data.size() <= 4) {
            std::vector<std::uint8_t> v = t.data;
            v.resize(4, 0);
            ifd.insert(ifd.end(), v.begin(), v.end());
        } else {
            le32(ifd, extra + static_cast<std::uint32_t>(extraData.size()));
            extraData.insert(extraData.end(), t.data.begin(), t.data.end());
            if (extraData.size() % 2) extraData.push_back(0);
        }
    }
    le32(ifd, 0);
    out.insert(out.end(), ifd.begin(), ifd.end());
    out.insert(out.end(), extraData.begin(), extraData.end());
    const auto stripOffset = static_cast<std::uint32_t>(out.size());
    for (int i = 0; i < 4; ++i) out[stripOffsetPos + i] = static_cast<std::uint8_t>(stripOffset >> (8 * i));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            // Scene-linear grey ramp, 2% .. 90% of white, same on every CFA site (neutral).
            const double v = 0.02 + 0.88 * x / (W - 1);
            le16(out, static_cast<std::uint32_t>(v * white + 0.5));
        }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
}

}  // namespace vtest
