#include "vstudio/looks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "detail/parallel.hpp"
#include "vstudio/pixel.hpp"

namespace vstudio {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::array<double, 3> rgb(const Json& p, const char* key, double def) {
    std::array<double, 3> out{def, def, def};
    if (auto it = p.find(key); it != p.end()) {
        if (it->is_array() && it->size() == 3)
            for (int i = 0; i < 3; ++i) out[i] = (*it)[i].get<double>();
        else if (it->is_number())
            out.fill(it->get<double>());
    }
    return out;
}

}  // namespace

// Parameters (all optional, defaults leave the image unchanged):
//   exposure    stops, -2..2              contrast  0.5..1.5 (S-curve, ends fixed)
//   temperature -1..1 (+ warmer)          tint      -1..1 (+ magenta)
//   lift [r,g,b] -0.2..0.2 (shadows)      gamma [r,g,b] 0.5..2 (midtones)
//   gain [r,g,b] 0.5..1.5 (highlights)    fade 0..0.3 (lifted blacks)
//   saturation 0..2   vibrance -1..1 (boosts muted colours more)   mono true/false
//   shadows / highlights [r,g,b] -0.2..0.2 split-toning tints
//   vignette 0..1     strength 0..1 (mix with the original)
void applyColorLook(VideoFrame& f, const Json& p) {
    if (f.empty()) return;
    const double strength = std::clamp(p.value("strength", 1.0), 0.0, 1.0);
    if (strength <= 0) return;
    const double exposure = std::pow(2.0, std::clamp(p.value("exposure", 0.0), -4.0, 4.0));
    const double k = std::clamp(p.value("contrast", 1.0), 0.0, 2.0) - 1.0;  // S-curve amount
    const double temp = std::clamp(p.value("temperature", 0.0), -1.0, 1.0);
    const double tint = std::clamp(p.value("tint", 0.0), -1.0, 1.0);
    const double fade = std::clamp(p.value("fade", 0.0), 0.0, 0.5);
    const auto lift = rgb(p, "lift", 0.0), gamma = rgb(p, "gamma", 1.0), gain = rgb(p, "gain", 1.0);
    const std::array<double, 3> wb{1.0 + 0.12 * temp, 1.0 - 0.10 * tint, 1.0 - 0.12 * temp};

    // Per-channel tone curve as a table: built once per frame, so the per-pixel cost is a
    // lookup. 256 entries for 8-bit frames; 4097 (interpolated) for deep ones.
    auto curve = [&](int c, double v) {
        const double g = std::max(0.05, gamma[c]);
        double x = v * exposure * wb[c];
        x = gain[c] * x + lift[c] * (1.0 - x);
        x = std::pow(std::clamp(x, 0.0, 1.0), 1.0 / g);
        x = x - k * std::sin(2 * kPi * x) / (2 * kPi);  // slope 1+k at mid-grey, 0 and 1 fixed
        x = fade + x * (1.0 - fade);
        return static_cast<float>(std::clamp(x, 0.0, 1.0));
    };
    constexpr int kDeepSteps = 4096;
    std::array<std::array<std::uint8_t, 256>, 3> lut{};
    std::array<std::array<float, 256>, 3> lutf{};
    std::array<std::vector<float>, 3> deepLut;
    for (int c = 0; c < 3; ++c) {
        if (f.deep()) {
            deepLut[c].resize(kDeepSteps + 2);
            for (int v = 0; v <= kDeepSteps; ++v) deepLut[c][v] = curve(c, static_cast<double>(v) / kDeepSteps);
            deepLut[c][kDeepSteps + 1] = deepLut[c][kDeepSteps];
            continue;
        }
        for (int v = 0; v < 256; ++v) {
            lutf[c][v] = curve(c, v / 255.0);
            lut[c][v] = static_cast<std::uint8_t>(std::lround(lutf[c][v] * 255.0));
        }
    }
    auto deepCurve = [&](int c, float v) {
        const float pos = std::clamp(v, 0.0f, 1.0f) * kDeepSteps;
        const int i = static_cast<int>(pos);
        const float t = pos - i;
        return deepLut[c][i] + (deepLut[c][i + 1] - deepLut[c][i]) * t;
    };
    const float sat = static_cast<float>(std::clamp(p.value("saturation", 1.0), 0.0, 3.0));
    const float vib = static_cast<float>(std::clamp(p.value("vibrance", 0.0), -1.0, 1.0));
    const bool mono = p.value("mono", false);
    const auto shd = rgb(p, "shadows", 0.0), hid = rgb(p, "highlights", 0.0);
    const float sh[3] = {float(shd[0]), float(shd[1]), float(shd[2])}, hi[3] = {float(hid[0]), float(hid[1]), float(hid[2])};
    const bool split = sh[0] != 0 || sh[1] != 0 || sh[2] != 0 || hi[0] != 0 || hi[1] != 0 || hi[2] != 0;
    const bool colour = mono || sat != 1.0f || vib != 0.0f || split;
    const float vig = static_cast<float>(std::clamp(p.value("vignette", 0.0), 0.0, 1.0));
    const float cx = f.width / 2.0f, cy = f.height / 2.0f, inv = 1.0f / std::sqrt(cx * cx + cy * cy);
    const float st = static_cast<float>(strength);

    // Vignette as a table over the squared distance to the centre (0 at the centre, 1 in the
    // corners): no square root per pixel. 1024 steps are finer than an 8-bit output step.
    constexpr int kVigSteps = 1024;
    std::array<float, kVigSteps + 1> vigLut{};
    if (vig > 0.0f)
        for (int i = 0; i <= kVigSteps; ++i) {
            const float d = std::sqrt(static_cast<float>(i) / kVigSteps);
            const float t = std::clamp((d - 0.45f) / 0.55f, 0.0f, 1.0f);
            vigLut[i] = 1.0f - vig * t * t * (3.0f - 2.0f * t);
        }
    std::vector<float> dx2(vig > 0.0f ? static_cast<std::size_t>(f.width) : 0);
    for (int x = 0; x < static_cast<int>(dx2.size()); ++x) {
        const float dx = (x + 0.5f - cx) * inv;
        dx2[x] = dx * dx;
    }

    // Colour, split toning and vignette on curve output (floats 0..1).
    auto grade = [&](float& r, float& g, float& b, int x, float dy2) {
        if (colour) {
            const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            float s = mono ? 0.0f : sat;
            if (vib != 0.0f && !mono)
                s *= 1.0f + vib * (1.0f - (std::max(r, std::max(g, b)) - std::min(r, std::min(g, b))));
            r = l + (r - l) * s;
            g = l + (g - l) * s;
            b = l + (b - l) * s;
            if (split) {
                const float ws = (1.0f - l) * (1.0f - l), wh = l * l;
                r += ws * sh[0] + wh * hi[0];
                g += ws * sh[1] + wh * hi[1];
                b += ws * sh[2] + wh * hi[2];
            }
        }
        if (vig > 0.0f) {
            const float d2 = std::min(1.0f, dx2[static_cast<std::size_t>(x)] + dy2);
            const float m = vigLut[static_cast<std::size_t>(d2 * kVigSteps + 0.5f)];
            r *= m;
            g *= m;
            b *= m;
        }
    };

    if (f.deep()) {
        detail::parallelRows(f.height, static_cast<std::size_t>(f.width) * f.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            std::uint16_t* row = f.half.data() + static_cast<std::size_t>(y) * f.width * 4;
            const float dy = (y + 0.5f - cy) * inv, dy2 = dy * dy;
            for (int x = 0; x < f.width; ++x) {
                std::uint16_t* px = row + static_cast<std::size_t>(x) * 4;
                const float in[3] = {halfToFloat(px[0]), halfToFloat(px[1]), halfToFloat(px[2])};
                float r = deepCurve(0, in[0]), g = deepCurve(1, in[1]), b = deepCurve(2, in[2]);
                grade(r, g, b, x, dy2);
                const float out[3] = {r, g, b};
                for (int c = 0; c < 3; ++c)
                    px[c] = floatToHalf(in[c] + (std::clamp(out[c], 0.0f, 1.0f) - in[c]) * st);
            }
        }
        });
        return;
    }
    const std::size_t pixels = static_cast<std::size_t>(f.width) * f.height;
    if (!colour && vig == 0.0f && st == 1.0f) {  // tone curve only: pure table lookup
        std::uint8_t* px = f.rgba.data();
        for (std::size_t i = 0; i < pixels; ++i, px += 4) {
            px[0] = lut[0][px[0]];
            px[1] = lut[1][px[1]];
            px[2] = lut[2][px[2]];
        }
        return;
    }
    // Row at a time in three float planes: gather through the curve tables, then a
    // branch-free pass the compiler vectorises (the options are template flags), then
    // blend back to 8 bits.
    const int w = f.width;
    const float keep = 1.0f - st, st255 = st * 255.0f;
    const float sVib = mono ? 0.0f : vib, sSat = mono ? 0.0f : sat;
    struct Planes {
        float *R, *G, *B, *V;
    };
    auto rowPass = [&]<bool kColour, bool kVib, bool kSplit, bool kVig>(Planes q) {
        float* __restrict R = q.R;
        float* __restrict G = q.G;
        float* __restrict B = q.B;
        const float* __restrict V = q.V;
        for (int x = 0; x < w; ++x) {
            float r = R[x], g = G[x], b = B[x];
            if constexpr (kColour) {
                const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                float sx = sSat;
                if constexpr (kVib) {
                    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
                    sx *= 1.0f + sVib * (1.0f - (mx - mn));
                }
                r = l + (r - l) * sx;
                g = l + (g - l) * sx;
                b = l + (b - l) * sx;
                if constexpr (kSplit) {
                    const float ws = (1.0f - l) * (1.0f - l), wh = l * l;
                    r += ws * sh[0] + wh * hi[0];
                    g += ws * sh[1] + wh * hi[1];
                    b += ws * sh[2] + wh * hi[2];
                }
            }
            if constexpr (kVig) {
                const float m = V[x];
                r *= m;
                g *= m;
                b *= m;
            }
            R[x] = std::clamp(r, 0.0f, 1.0f) * st255;
            G[x] = std::clamp(g, 0.0f, 1.0f) * st255;
            B[x] = std::clamp(b, 0.0f, 1.0f) * st255;
        }
    };
    using Pass = void (*)(decltype(rowPass)&, Planes);
    // Pick the specialisation once per frame.
    auto pick = [&]<bool C, bool Vb, bool S, bool Vg>() -> Pass {
        return +[](decltype(rowPass)& fn, Planes q) { fn.template operator()<C, Vb, S, Vg>(q); };
    };
    const bool useVib = colour && !mono && vib != 0.0f;
    Pass pass = nullptr;
    const int key = (colour ? 8 : 0) | (useVib ? 4 : 0) | (colour && split ? 2 : 0) | (vig > 0.0f ? 1 : 0);
    switch (key) {
        case 0: pass = pick.template operator()<false, false, false, false>(); break;
        case 1: pass = pick.template operator()<false, false, false, true>(); break;
        case 8: pass = pick.template operator()<true, false, false, false>(); break;
        case 9: pass = pick.template operator()<true, false, false, true>(); break;
        case 10: pass = pick.template operator()<true, false, true, false>(); break;
        case 11: pass = pick.template operator()<true, false, true, true>(); break;
        case 12: pass = pick.template operator()<true, true, false, false>(); break;
        case 13: pass = pick.template operator()<true, true, false, true>(); break;
        case 14: pass = pick.template operator()<true, true, true, false>(); break;
        default: pass = pick.template operator()<true, true, true, true>(); break;
    }
    // Large frames split their rows across cores (detail/parallel.hpp); each part has its own planes.
    detail::parallelRows(f.height, pixels, [&](int y0, int y1) {
        std::vector<float> buf(static_cast<std::size_t>(w) * 4);
        float* R = buf.data();
        float* G = R + w;
        float* B = G + w;
        float* V = B + w;
        for (int y = y0; y < y1; ++y) {
            std::uint8_t* row = f.rgba.data() + static_cast<std::size_t>(y) * w * 4;
            const float dy = (y + 0.5f - cy) * inv, dy2 = dy * dy;
            for (int x = 0; x < w; ++x) {
                const std::uint8_t* px = row + static_cast<std::size_t>(x) * 4;
                R[x] = lutf[0][px[0]];
                G[x] = lutf[1][px[1]];
                B[x] = lutf[2][px[2]];
            }
            if (vig > 0.0f)
                for (int x = 0; x < w; ++x)
                    V[x] = vigLut[static_cast<std::size_t>(std::min(1.0f, dx2[x] + dy2) * kVigSteps + 0.5f)];
            pass(rowPass, Planes{R, G, B, V});
            for (int x = 0; x < w; ++x) {
                std::uint8_t* px = row + static_cast<std::size_t>(x) * 4;
                px[0] = static_cast<std::uint8_t>(px[0] * keep + R[x] + 0.5f);
                px[1] = static_cast<std::uint8_t>(px[1] * keep + G[x] + 0.5f);
                px[2] = static_cast<std::uint8_t>(px[2] * keep + B[x] + 0.5f);
            }
        }
    });
}

const std::vector<ColorLook>& colorLooks() {
    // Parameters chosen by eye on varied footage (skin, sky, foliage, night).
    static const std::vector<ColorLook> looks{
        {"vivid", "Rực rỡ", "Cơ bản", {{"contrast", 1.12}, {"saturation", 1.15}, {"vibrance", 0.35}}},
        {"natural", "Tự nhiên", "Cơ bản", {{"contrast", 1.05}, {"vibrance", 0.2}, {"temperature", 0.05}}},
        {"bright", "Tươi sáng", "Cơ bản", {{"exposure", 0.25}, {"contrast", 0.95}, {"vibrance", 0.25}, {"fade", 0.03}}},
        {"teal_orange", "Điện ảnh Teal & Orange", "Điện ảnh",
         {{"contrast", 1.15}, {"saturation", 1.05}, {"shadows", {-0.05, 0.02, 0.07}}, {"highlights", {0.07, 0.03, -0.05}},
          {"vignette", 0.25}}},
        {"cinematic_moody", "Điện ảnh trầm", "Điện ảnh",
         {{"exposure", -0.2}, {"contrast", 1.2}, {"saturation", 0.85}, {"lift", {0.0, 0.01, 0.03}},
          {"shadows", {-0.02, 0.0, 0.04}}, {"vignette", 0.35}}},
        {"bleach", "Bạc màu (Bleach bypass)", "Điện ảnh", {{"contrast", 1.3}, {"saturation", 0.55}, {"gain", 1.04}}},
        {"vintage", "Phim cổ điển", "Cổ điển",
         {{"contrast", 0.9}, {"saturation", 0.8}, {"fade", 0.08}, {"temperature", 0.2},
          {"shadows", {0.03, 0.0, 0.05}}, {"highlights", {0.04, 0.03, -0.03}}, {"vignette", 0.3}}},
        {"retro_70s", "Thập niên 70", "Cổ điển",
         {{"contrast", 0.92}, {"saturation", 0.9}, {"fade", 0.06}, {"temperature", 0.35}, {"tint", 0.1},
          {"gamma", {1.05, 1.0, 0.92}}}},
        {"sepia", "Nâu Sepia", "Cổ điển",
         {{"mono", true}, {"contrast", 1.05}, {"shadows", {0.06, 0.02, -0.04}}, {"highlights", {0.08, 0.04, -0.04}},
          {"fade", 0.04}}},
        {"bw_contrast", "Đen trắng tương phản", "Đen trắng", {{"mono", true}, {"contrast", 1.35}, {"vignette", 0.2}}},
        {"bw_soft", "Đen trắng mềm", "Đen trắng", {{"mono", true}, {"contrast", 0.9}, {"fade", 0.06}}},
        {"warm_sunset", "Hoàng hôn ấm", "Ánh sáng",
         {{"temperature", 0.45}, {"tint", 0.08}, {"contrast", 1.08}, {"vibrance", 0.25},
          {"highlights", {0.05, 0.02, -0.04}}}},
        {"cool_blue", "Xanh lạnh", "Ánh sáng",
         {{"temperature", -0.45}, {"contrast", 1.06}, {"saturation", 0.95}, {"shadows", {-0.02, 0.0, 0.05}}}},
        {"night_neon", "Đêm neon", "Ánh sáng",
         {{"exposure", -0.1}, {"contrast", 1.2}, {"saturation", 1.25}, {"tint", 0.2},
          {"shadows", {0.03, -0.03, 0.08}}, {"highlights", {0.05, -0.01, 0.06}}, {"vignette", 0.3}}},
        {"portrait", "Chân dung da đẹp", "Chủ đề",
         {{"exposure", 0.12}, {"contrast", 0.94}, {"saturation", 0.95}, {"temperature", 0.12}, {"tint", 0.05},
          {"fade", 0.02}}},
        {"landscape", "Phong cảnh", "Chủ đề", {{"contrast", 1.12}, {"saturation", 1.1}, {"vibrance", 0.45}, {"gain", {1.0, 1.02, 1.03}}}},
        {"food", "Ẩm thực", "Chủ đề", {{"exposure", 0.1}, {"contrast", 1.08}, {"temperature", 0.2}, {"vibrance", 0.4}}},
        {"pastel", "Pastel nhẹ", "Chủ đề",
         {{"exposure", 0.2}, {"contrast", 0.85}, {"saturation", 0.8}, {"fade", 0.1}, {"tint", 0.06}}},
    };
    return looks;
}

const ColorLook* findColorLook(const std::string& id) {
    for (const auto& l : colorLooks())
        if (l.id == id) return &l;
    return nullptr;
}

}  // namespace vstudio
