#include "vstudio/pixel.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace vstudio {

namespace {

float decodeHalf(std::uint16_t h) {
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000) << 16;
    std::uint32_t exp = (h >> 10) & 0x1f, mant = h & 0x3ff, bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {  // subnormal: normalise
            exp = 127 - 15 + 1;
            while (!(mant & 0x400)) {
                mant <<= 1;
                --exp;
            }
            bits = sign | (exp << 23) | ((mant & 0x3ff) << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000 | (mant << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

const std::array<float, 65536>& halfTable() {
    static const auto t = [] {
        std::array<float, 65536> a{};
        for (std::uint32_t i = 0; i < 65536; ++i) a[i] = decodeHalf(static_cast<std::uint16_t>(i));
        return a;
    }();
    return t;
}

// 4x4 Bayer matrix, centred: offsets in (-0.5, 0.5) of one 8-bit step.
constexpr float kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

}  // namespace

float halfToFloat(std::uint16_t h) { return halfTable()[h]; }

std::uint16_t floatToHalf(float f) {
    std::uint32_t x;
    std::memcpy(&x, &f, 4);
    const std::uint32_t sign = (x >> 16) & 0x8000;
    const std::uint32_t absx = x & 0x7fffffff;
    if (absx >= 0x7f800000) return static_cast<std::uint16_t>(sign | 0x7c00 | (absx > 0x7f800000 ? 0x200 : 0));
    if (absx >= 0x477ff000) return static_cast<std::uint16_t>(sign | 0x7c00);  // overflow -> inf
    if (absx < 0x38800000) {  // subnormal half (or zero)
        if (absx < 0x33000000) return static_cast<std::uint16_t>(sign);
        const std::uint32_t e = absx >> 23, m = (absx & 0x7fffff) | 0x800000;
        const std::uint32_t shift = 126 - e;  // 113 + 13
        std::uint32_t r = m >> shift;
        const std::uint32_t rem = m & ((1u << shift) - 1), halfway = 1u << (shift - 1);
        if (rem > halfway || (rem == halfway && (r & 1))) ++r;
        return static_cast<std::uint16_t>(sign | r);
    }
    std::uint32_t r = absx - 0x38000000;  // rebias exponent 127 -> 15
    r = (r + 0xfff + ((r >> 13) & 1)) >> 13;  // round to nearest even
    return static_cast<std::uint16_t>(sign | r);
}

void convertFrame(VideoFrame& f, PixelFormat to) {
    if (f.format == to) return;
    const std::size_t n = static_cast<std::size_t>(f.width) * f.height * 4;
    if (to == PixelFormat::RGBA16F) {
        // 256 possible values per channel: one table.
        std::array<std::uint16_t, 256> t{};
        for (int v = 0; v < 256; ++v) t[v] = floatToHalf(v / 255.0f);
        f.half.resize(n);
        for (std::size_t i = 0; i < n; ++i) f.half[i] = t[f.rgba[i]];
        f.rgba.clear();
        f.rgba.shrink_to_fit();
    } else {
        const auto& tab = halfTable();
        f.rgba.resize(n);
        for (int y = 0; y < f.height; ++y) {
            const std::size_t row = static_cast<std::size_t>(y) * f.width * 4;
            for (int x = 0; x < f.width; ++x) {
                // Full-step ordered dither, except for values that already sit on an 8-bit
                // level (8-bit sources, within half-float rounding): those come back unchanged.
                const float d = (kBayer[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f - 0.5f;
                const std::size_t i = row + static_cast<std::size_t>(x) * 4;
                for (int c = 0; c < 3; ++c) {
                    const float v = tab[f.half[i + c]] * 255.0f, near = std::round(v);
                    const float o = std::abs(v - near) < 0.08f ? near : v + d;
                    f.rgba[i + c] = static_cast<std::uint8_t>(std::clamp(o + 0.5f, 0.0f, 255.0f));
                }
                // Alpha: no dither (edges must stay exact).
                f.rgba[i + 3] = static_cast<std::uint8_t>(std::clamp(tab[f.half[i + 3]] * 255.0f + 0.5f, 0.0f, 255.0f));
            }
        }
        f.half.clear();
        f.half.shrink_to_fit();
    }
    f.format = to;
}

VideoFrame convertedFrame(const VideoFrame& f, PixelFormat to) {
    VideoFrame c = f;
    convertFrame(c, to);
    return c;
}

}  // namespace vstudio
