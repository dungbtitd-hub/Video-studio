#include "vstudio/fonts.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"

namespace vstudio::embedded {
struct File {
    const char* name;
    const std::uint8_t* data;
    std::size_t size;
};
extern const File kFiles[];
extern const std::size_t kFileCount;
}  // namespace vstudio::embedded

namespace vstudio::text {

struct Font::Impl {
    std::shared_ptr<const std::vector<std::uint8_t>> owned;  // registered fonts own their bytes
    const std::uint8_t* data = nullptr;
    stbtt_fontinfo info{};
    double scale = 1;  // font units -> em
    mutable std::mutex mu;
    mutable std::unordered_map<int, vec::Path> outlines;
    mutable std::unordered_map<char32_t, int> glyphs;
};

namespace {

struct Bundled {
    const char* id;
    const char* file;
    const char* name;
    const char* style;
};
// Order = order in the UI.
constexpr Bundled kBundled[] = {
    {"be-vietnam-pro-bold", "BeVietnamPro-Bold.ttf", "Be Vietnam Pro Đậm", "Sans"},
    {"be-vietnam-pro", "BeVietnamPro-Regular.ttf", "Be Vietnam Pro", "Sans"},
    {"be-vietnam-pro-extrabold", "BeVietnamPro-ExtraBold.ttf", "Be Vietnam Pro Đậm nét", "Sans"},
    {"bungee", "Bungee-Regular.ttf", "Bungee", "Display"},
    {"lobster", "Lobster-Regular.ttf", "Lobster", "Script"},
    {"pacifico", "Pacifico-Regular.ttf", "Pacifico", "Script"},
    {"patrick-hand", "PatrickHand-Regular.ttf", "Patrick Hand", "Hand"},
};

struct Registered {
    FontInfo info;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
};

struct Registry {
    std::mutex mu;
    std::map<std::string, std::shared_ptr<const Font>> loaded;
    std::vector<Registered> extra;
};

Registry& registry() {
    static Registry r;
    return r;
}

std::shared_ptr<Font::Impl> open(const std::uint8_t* data, std::shared_ptr<const std::vector<std::uint8_t>> owned) {
    auto impl = std::make_shared<Font::Impl>();
    impl->owned = std::move(owned);
    impl->data = data;
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || !stbtt_InitFont(&impl->info, data, offset)) return nullptr;
    const float s = stbtt_ScaleForMappingEmToPixels(&impl->info, 1.0f);
    if (!(s > 0)) return nullptr;
    impl->scale = s;
    return impl;
}

const std::uint8_t* bundledData(const char* file, std::size_t* size = nullptr) {
    for (std::size_t i = 0; i < embedded::kFileCount; ++i)
        if (std::string(embedded::kFiles[i].name) == file) {
            if (size) *size = embedded::kFiles[i].size;
            return embedded::kFiles[i].data;
        }
    return nullptr;
}

std::shared_ptr<const Font> load(const std::string& id) {
    for (const auto& b : kBundled)
        if (id == b.id)
            if (const auto* d = bundledData(b.file))
                if (auto impl = open(d, nullptr)) return std::make_shared<const Font>(std::move(impl), id);
    for (const auto& r : registry().extra)
        if (r.info.id == id)
            if (auto impl = open(r.bytes->data(), r.bytes)) return std::make_shared<const Font>(std::move(impl), id);
    return nullptr;
}

}  // namespace

std::vector<FontInfo> fontCatalog() {
    std::vector<FontInfo> out;
    for (const auto& b : kBundled)
        if (bundledData(b.file)) out.push_back({b.id, b.name, b.style, true});
    std::lock_guard lock(registry().mu);
    for (const auto& r : registry().extra) out.push_back(r.info);
    return out;
}

const char* defaultFontId() { return kBundled[0].id; }

bool registerFont(const std::string& id, const std::string& name, std::vector<std::uint8_t> bytes,
                  const std::string& style) {
    if (id.empty()) return false;
    for (const auto& b : kBundled)
        if (id == b.id) return false;
    auto shared = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    if (shared->size() < 12 || !open(shared->data(), shared)) return false;
    auto& r = registry();
    std::lock_guard lock(r.mu);
    std::erase_if(r.extra, [&](const Registered& x) { return x.info.id == id; });
    r.extra.push_back({{id, name.empty() ? id : name, style, false}, std::move(shared)});
    r.loaded.erase(id);
    return true;
}

std::shared_ptr<const Font> Font::get(const std::string& id) {
    auto& r = registry();
    std::lock_guard lock(r.mu);
    const std::string want = id.empty() ? defaultFontId() : id;
    if (auto it = r.loaded.find(want); it != r.loaded.end()) return it->second;
    auto f = load(want);
    if (!f) {
        if (auto it = r.loaded.find(defaultFontId()); it != r.loaded.end()) return it->second;
        f = load(defaultFontId());
        r.loaded[defaultFontId()] = f;
        return f;
    }
    r.loaded[want] = f;
    return f;
}

Font::Font(std::shared_ptr<Impl> impl, std::string id) : impl_(std::move(impl)), id_(std::move(id)) {
    int a = 0, d = 0, g = 0;
    stbtt_GetFontVMetrics(&impl_->info, &a, &d, &g);
    ascent_ = -a * impl_->scale;
    descent_ = -d * impl_->scale;
    lineGap_ = g * impl_->scale;
}

Font::~Font() = default;

int Font::glyph(char32_t cp) const {
    std::lock_guard lock(impl_->mu);
    if (auto it = impl_->glyphs.find(cp); it != impl_->glyphs.end()) return it->second;
    const int g = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
    impl_->glyphs[cp] = g;
    return g;
}

double Font::advance(int glyph) const {
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, glyph, &adv, &lsb);
    return adv * impl_->scale;
}

double Font::kerning(int left, int right) const {
    if (left <= 0 || right <= 0) return 0;
    return stbtt_GetGlyphKernAdvance(&impl_->info, left, right) * impl_->scale;
}

const vec::Path& Font::outline(int glyph) const {
    std::lock_guard lock(impl_->mu);
    if (auto it = impl_->outlines.find(glyph); it != impl_->outlines.end()) return it->second;
    vec::Path p;
    stbtt_vertex* v = nullptr;
    const int n = stbtt_GetGlyphShape(&impl_->info, glyph, &v);
    const double s = impl_->scale;
    bool open = false;
    for (int i = 0; i < n; ++i) {
        const double x = v[i].x * s, y = -v[i].y * s;
        switch (v[i].type) {
            case STBTT_vmove:
                if (open) p.close();
                p.moveTo(x, y);
                open = true;
                break;
            case STBTT_vline: p.lineTo(x, y); break;
            case STBTT_vcurve: p.quadTo(v[i].cx * s, -v[i].cy * s, x, y); break;
            case STBTT_vcubic: p.cubicTo(v[i].cx * s, -v[i].cy * s, v[i].cx1 * s, -v[i].cy1 * s, x, y); break;
            default: break;
        }
    }
    if (open) p.close();
    if (v) stbtt_FreeShape(&impl_->info, v);
    return impl_->outlines.emplace(glyph, std::move(p)).first->second;
}

// ---------------------------------------------------------------- UTF-8

namespace {

// (base, combining mark) -> precomposed, for the marks Vietnamese uses (from Unicode NFD data).
struct Compose {
    char32_t base, mark, out;
};
constexpr Compose kCompose[] = {
    {0x0041, 0x0300, 0x00C0},
    {0x0041, 0x0301, 0x00C1},
    {0x0041, 0x0302, 0x00C2},
    {0x0041, 0x0303, 0x00C3},
    {0x0041, 0x0306, 0x0102},
    {0x0041, 0x0309, 0x1EA2},
    {0x0041, 0x0323, 0x1EA0},
    {0x0042, 0x0323, 0x1E04},
    {0x0043, 0x0301, 0x0106},
    {0x0043, 0x0302, 0x0108},
    {0x0044, 0x0323, 0x1E0C},
    {0x0045, 0x0300, 0x00C8},
    {0x0045, 0x0301, 0x00C9},
    {0x0045, 0x0302, 0x00CA},
    {0x0045, 0x0303, 0x1EBC},
    {0x0045, 0x0306, 0x0114},
    {0x0045, 0x0309, 0x1EBA},
    {0x0045, 0x0323, 0x1EB8},
    {0x0047, 0x0301, 0x01F4},
    {0x0047, 0x0302, 0x011C},
    {0x0047, 0x0306, 0x011E},
    {0x0048, 0x0302, 0x0124},
    {0x0048, 0x0323, 0x1E24},
    {0x0049, 0x0300, 0x00CC},
    {0x0049, 0x0301, 0x00CD},
    {0x0049, 0x0302, 0x00CE},
    {0x0049, 0x0303, 0x0128},
    {0x0049, 0x0306, 0x012C},
    {0x0049, 0x0309, 0x1EC8},
    {0x0049, 0x0323, 0x1ECA},
    {0x004A, 0x0302, 0x0134},
    {0x004B, 0x0301, 0x1E30},
    {0x004B, 0x0323, 0x1E32},
    {0x004C, 0x0301, 0x0139},
    {0x004C, 0x0323, 0x1E36},
    {0x004D, 0x0301, 0x1E3E},
    {0x004D, 0x0323, 0x1E42},
    {0x004E, 0x0300, 0x01F8},
    {0x004E, 0x0301, 0x0143},
    {0x004E, 0x0303, 0x00D1},
    {0x004E, 0x0323, 0x1E46},
    {0x004F, 0x0300, 0x00D2},
    {0x004F, 0x0301, 0x00D3},
    {0x004F, 0x0302, 0x00D4},
    {0x004F, 0x0303, 0x00D5},
    {0x004F, 0x0306, 0x014E},
    {0x004F, 0x0309, 0x1ECE},
    {0x004F, 0x031B, 0x01A0},
    {0x004F, 0x0323, 0x1ECC},
    {0x0050, 0x0301, 0x1E54},
    {0x0052, 0x0301, 0x0154},
    {0x0052, 0x0323, 0x1E5A},
    {0x0053, 0x0301, 0x015A},
    {0x0053, 0x0302, 0x015C},
    {0x0053, 0x0323, 0x1E62},
    {0x0054, 0x0323, 0x1E6C},
    {0x0055, 0x0300, 0x00D9},
    {0x0055, 0x0301, 0x00DA},
    {0x0055, 0x0302, 0x00DB},
    {0x0055, 0x0303, 0x0168},
    {0x0055, 0x0306, 0x016C},
    {0x0055, 0x0309, 0x1EE6},
    {0x0055, 0x031B, 0x01AF},
    {0x0055, 0x0323, 0x1EE4},
    {0x0056, 0x0303, 0x1E7C},
    {0x0056, 0x0323, 0x1E7E},
    {0x0057, 0x0300, 0x1E80},
    {0x0057, 0x0301, 0x1E82},
    {0x0057, 0x0302, 0x0174},
    {0x0057, 0x0323, 0x1E88},
    {0x0059, 0x0300, 0x1EF2},
    {0x0059, 0x0301, 0x00DD},
    {0x0059, 0x0302, 0x0176},
    {0x0059, 0x0303, 0x1EF8},
    {0x0059, 0x0309, 0x1EF6},
    {0x0059, 0x0323, 0x1EF4},
    {0x005A, 0x0301, 0x0179},
    {0x005A, 0x0302, 0x1E90},
    {0x005A, 0x0323, 0x1E92},
    {0x0061, 0x0300, 0x00E0},
    {0x0061, 0x0301, 0x00E1},
    {0x0061, 0x0302, 0x00E2},
    {0x0061, 0x0303, 0x00E3},
    {0x0061, 0x0306, 0x0103},
    {0x0061, 0x0309, 0x1EA3},
    {0x0061, 0x0323, 0x1EA1},
    {0x0062, 0x0323, 0x1E05},
    {0x0063, 0x0301, 0x0107},
    {0x0063, 0x0302, 0x0109},
    {0x0064, 0x0323, 0x1E0D},
    {0x0065, 0x0300, 0x00E8},
    {0x0065, 0x0301, 0x00E9},
    {0x0065, 0x0302, 0x00EA},
    {0x0065, 0x0303, 0x1EBD},
    {0x0065, 0x0306, 0x0115},
    {0x0065, 0x0309, 0x1EBB},
    {0x0065, 0x0323, 0x1EB9},
    {0x0067, 0x0301, 0x01F5},
    {0x0067, 0x0302, 0x011D},
    {0x0067, 0x0306, 0x011F},
    {0x0068, 0x0302, 0x0125},
    {0x0068, 0x0323, 0x1E25},
    {0x0069, 0x0300, 0x00EC},
    {0x0069, 0x0301, 0x00ED},
    {0x0069, 0x0302, 0x00EE},
    {0x0069, 0x0303, 0x0129},
    {0x0069, 0x0306, 0x012D},
    {0x0069, 0x0309, 0x1EC9},
    {0x0069, 0x0323, 0x1ECB},
    {0x006A, 0x0302, 0x0135},
    {0x006B, 0x0301, 0x1E31},
    {0x006B, 0x0323, 0x1E33},
    {0x006C, 0x0301, 0x013A},
    {0x006C, 0x0323, 0x1E37},
    {0x006D, 0x0301, 0x1E3F},
    {0x006D, 0x0323, 0x1E43},
    {0x006E, 0x0300, 0x01F9},
    {0x006E, 0x0301, 0x0144},
    {0x006E, 0x0303, 0x00F1},
    {0x006E, 0x0323, 0x1E47},
    {0x006F, 0x0300, 0x00F2},
    {0x006F, 0x0301, 0x00F3},
    {0x006F, 0x0302, 0x00F4},
    {0x006F, 0x0303, 0x00F5},
    {0x006F, 0x0306, 0x014F},
    {0x006F, 0x0309, 0x1ECF},
    {0x006F, 0x031B, 0x01A1},
    {0x006F, 0x0323, 0x1ECD},
    {0x0070, 0x0301, 0x1E55},
    {0x0072, 0x0301, 0x0155},
    {0x0072, 0x0323, 0x1E5B},
    {0x0073, 0x0301, 0x015B},
    {0x0073, 0x0302, 0x015D},
    {0x0073, 0x0323, 0x1E63},
    {0x0074, 0x0323, 0x1E6D},
    {0x0075, 0x0300, 0x00F9},
    {0x0075, 0x0301, 0x00FA},
    {0x0075, 0x0302, 0x00FB},
    {0x0075, 0x0303, 0x0169},
    {0x0075, 0x0306, 0x016D},
    {0x0075, 0x0309, 0x1EE7},
    {0x0075, 0x031B, 0x01B0},
    {0x0075, 0x0323, 0x1EE5},
    {0x0076, 0x0303, 0x1E7D},
    {0x0076, 0x0323, 0x1E7F},
    {0x0077, 0x0300, 0x1E81},
    {0x0077, 0x0301, 0x1E83},
    {0x0077, 0x0302, 0x0175},
    {0x0077, 0x0323, 0x1E89},
    {0x0079, 0x0300, 0x1EF3},
    {0x0079, 0x0301, 0x00FD},
    {0x0079, 0x0302, 0x0177},
    {0x0079, 0x0303, 0x1EF9},
    {0x0079, 0x0309, 0x1EF7},
    {0x0079, 0x0323, 0x1EF5},
    {0x007A, 0x0301, 0x017A},
    {0x007A, 0x0302, 0x1E91},
    {0x007A, 0x0323, 0x1E93},
    {0x00C2, 0x0300, 0x1EA6},
    {0x00C2, 0x0301, 0x1EA4},
    {0x00C2, 0x0303, 0x1EAA},
    {0x00C2, 0x0309, 0x1EA8},
    {0x00C5, 0x0301, 0x01FA},
    {0x00C6, 0x0301, 0x01FC},
    {0x00C7, 0x0301, 0x1E08},
    {0x00CA, 0x0300, 0x1EC0},
    {0x00CA, 0x0301, 0x1EBE},
    {0x00CA, 0x0303, 0x1EC4},
    {0x00CA, 0x0309, 0x1EC2},
    {0x00CF, 0x0301, 0x1E2E},
    {0x00D4, 0x0300, 0x1ED2},
    {0x00D4, 0x0301, 0x1ED0},
    {0x00D4, 0x0303, 0x1ED6},
    {0x00D4, 0x0309, 0x1ED4},
    {0x00D5, 0x0301, 0x1E4C},
    {0x00D8, 0x0301, 0x01FE},
    {0x00DC, 0x0300, 0x01DB},
    {0x00DC, 0x0301, 0x01D7},
    {0x00E2, 0x0300, 0x1EA7},
    {0x00E2, 0x0301, 0x1EA5},
    {0x00E2, 0x0303, 0x1EAB},
    {0x00E2, 0x0309, 0x1EA9},
    {0x00E5, 0x0301, 0x01FB},
    {0x00E6, 0x0301, 0x01FD},
    {0x00E7, 0x0301, 0x1E09},
    {0x00EA, 0x0300, 0x1EC1},
    {0x00EA, 0x0301, 0x1EBF},
    {0x00EA, 0x0303, 0x1EC5},
    {0x00EA, 0x0309, 0x1EC3},
    {0x00EF, 0x0301, 0x1E2F},
    {0x00F4, 0x0300, 0x1ED3},
    {0x00F4, 0x0301, 0x1ED1},
    {0x00F4, 0x0303, 0x1ED7},
    {0x00F4, 0x0309, 0x1ED5},
    {0x00F5, 0x0301, 0x1E4D},
    {0x00F8, 0x0301, 0x01FF},
    {0x00FC, 0x0300, 0x01DC},
    {0x00FC, 0x0301, 0x01D8},
    {0x0102, 0x0300, 0x1EB0},
    {0x0102, 0x0301, 0x1EAE},
    {0x0102, 0x0303, 0x1EB4},
    {0x0102, 0x0309, 0x1EB2},
    {0x0103, 0x0300, 0x1EB1},
    {0x0103, 0x0301, 0x1EAF},
    {0x0103, 0x0303, 0x1EB5},
    {0x0103, 0x0309, 0x1EB3},
    {0x0112, 0x0300, 0x1E14},
    {0x0112, 0x0301, 0x1E16},
    {0x0113, 0x0300, 0x1E15},
    {0x0113, 0x0301, 0x1E17},
    {0x014C, 0x0300, 0x1E50},
    {0x014C, 0x0301, 0x1E52},
    {0x014D, 0x0300, 0x1E51},
    {0x014D, 0x0301, 0x1E53},
    {0x0168, 0x0301, 0x1E78},
    {0x0169, 0x0301, 0x1E79},
    {0x01A0, 0x0300, 0x1EDC},
    {0x01A0, 0x0301, 0x1EDA},
    {0x01A0, 0x0303, 0x1EE0},
    {0x01A0, 0x0309, 0x1EDE},
    {0x01A0, 0x0323, 0x1EE2},
    {0x01A1, 0x0300, 0x1EDD},
    {0x01A1, 0x0301, 0x1EDB},
    {0x01A1, 0x0303, 0x1EE1},
    {0x01A1, 0x0309, 0x1EDF},
    {0x01A1, 0x0323, 0x1EE3},
    {0x01AF, 0x0300, 0x1EEA},
    {0x01AF, 0x0301, 0x1EE8},
    {0x01AF, 0x0303, 0x1EEE},
    {0x01AF, 0x0309, 0x1EEC},
    {0x01AF, 0x0323, 0x1EF0},
    {0x01B0, 0x0300, 0x1EEB},
    {0x01B0, 0x0301, 0x1EE9},
    {0x01B0, 0x0303, 0x1EEF},
    {0x01B0, 0x0309, 0x1EED},
    {0x01B0, 0x0323, 0x1EF1},
    {0x0228, 0x0306, 0x1E1C},
    {0x0229, 0x0306, 0x1E1D},
    {0x0391, 0x0301, 0x0386},
    {0x0395, 0x0301, 0x0388},
    {0x0397, 0x0301, 0x0389},
    {0x0399, 0x0301, 0x038A},
    {0x039F, 0x0301, 0x038C},
    {0x03A5, 0x0301, 0x038E},
    {0x03A9, 0x0301, 0x038F},
    {0x03B1, 0x0301, 0x03AC},
    {0x03B5, 0x0301, 0x03AD},
    {0x03B7, 0x0301, 0x03AE},
    {0x03B9, 0x0301, 0x03AF},
    {0x03BF, 0x0301, 0x03CC},
    {0x03C5, 0x0301, 0x03CD},
    {0x03C9, 0x0301, 0x03CE},
    {0x03CA, 0x0301, 0x0390},
    {0x03CB, 0x0301, 0x03B0},
    {0x03D2, 0x0301, 0x03D3},
    {0x0410, 0x0306, 0x04D0},
    {0x0413, 0x0301, 0x0403},
    {0x0415, 0x0300, 0x0400},
    {0x0415, 0x0306, 0x04D6},
    {0x0416, 0x0306, 0x04C1},
    {0x0418, 0x0300, 0x040D},
    {0x0418, 0x0306, 0x0419},
    {0x041A, 0x0301, 0x040C},
    {0x0423, 0x0306, 0x040E},
    {0x0430, 0x0306, 0x04D1},
    {0x0433, 0x0301, 0x0453},
    {0x0435, 0x0300, 0x0450},
    {0x0435, 0x0306, 0x04D7},
    {0x0436, 0x0306, 0x04C2},
    {0x0438, 0x0300, 0x045D},
    {0x0438, 0x0306, 0x0439},
    {0x043A, 0x0301, 0x045C},
    {0x0443, 0x0306, 0x045E},
    {0x1EA0, 0x0302, 0x1EAC},
    {0x1EA0, 0x0306, 0x1EB6},
    {0x1EA1, 0x0302, 0x1EAD},
    {0x1EA1, 0x0306, 0x1EB7},
    {0x1EB8, 0x0302, 0x1EC6},
    {0x1EB9, 0x0302, 0x1EC7},
    {0x1ECC, 0x0302, 0x1ED8},
    {0x1ECD, 0x0302, 0x1ED9},
};

int markClass(char32_t m) {
    switch (m) {
        case 0x031B: return 216;  // horn
        case 0x0323: return 220;  // dot below
        default: return 230;
    }
}

bool isMark(char32_t c) { return c >= 0x0300 && c <= 0x036F; }

char32_t compose(char32_t base, char32_t mark) {
    for (const auto& c : kCompose)
        if (c.base == base && c.mark == mark) return c.out;
    return 0;
}

}  // namespace

std::u32string decodeUtf8(const std::string& s) {
    std::u32string raw;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        int len = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 6) len = 2, cp = c & 0x1F;
        else if ((c >> 4) == 14) len = 3, cp = c & 0x0F;
        else if ((c >> 3) == 30) len = 4, cp = c & 0x07;
        else len = 1;
        if (len > 1) {
            if (i + len > s.size()) {
                cp = 0xFFFD;
                len = 1;
            } else {
                for (int k = 1; k < len; ++k) {
                    const auto cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc >> 6) != 2) {
                        cp = 0xFFFD;
                        len = k;
                        break;
                    }
                    cp = (cp << 6) | (cc & 0x3F);
                }
            }
        }
        raw.push_back(cp);
        i += len;
    }
    // Compose: a base followed by marks (put in canonical order first).
    std::u32string out;
    for (std::size_t i = 0; i < raw.size();) {
        char32_t base = raw[i++];
        std::u32string marks;
        while (i < raw.size() && isMark(raw[i])) marks.push_back(raw[i++]);
        std::stable_sort(marks.begin(), marks.end(), [](char32_t a, char32_t b) { return markClass(a) < markClass(b); });
        std::u32string left;
        for (char32_t m : marks) {
            if (const char32_t c = compose(base, m)) base = c;
            else left.push_back(m);
        }
        out.push_back(base);
        out += left;
    }
    return out;
}

}  // namespace vstudio::text
