// Fonts for text clips: the OFL fonts bundled in the core (core/assets/fonts) plus any the
// app registers (a company font). Glyph outlines come out as vector paths, so text is
// drawn by the same anti-aliased renderer on every OS and looks identical everywhere.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vstudio/vector.hpp"

namespace vstudio::text {

struct FontInfo {
    std::string id;      // stored in projects, e.g. "be-vietnam-pro-bold"
    std::string name;    // shown in the UI
    std::string style;   // "Sans", "Script", "Hand", "Display"
    bool bundled = true;
};

// Every font that can be used, bundled first.
std::vector<FontInfo> fontCatalog();
// The default font id (Be Vietnam Pro Bold: full Vietnamese coverage).
const char* defaultFontId();
// Adds a font from TrueType/OpenType (glyf) bytes. False if the bytes are not a usable font
// or the id is taken by a bundled font. Registering the same id again replaces it.
bool registerFont(const std::string& id, const std::string& name, std::vector<std::uint8_t> bytes,
                  const std::string& style = "Sans");

class Font {
public:
    // The font `id`, else the default one. Never null.
    static std::shared_ptr<const Font> get(const std::string& id);

    [[nodiscard]] const std::string& id() const { return id_; }
    // Metrics in em units (1 = font size), y down: ascent < 0 < descent.
    [[nodiscard]] double ascent() const { return ascent_; }
    [[nodiscard]] double descent() const { return descent_; }
    [[nodiscard]] double lineGap() const { return lineGap_; }
    [[nodiscard]] int glyph(char32_t cp) const;  // 0 = missing
    [[nodiscard]] double advance(int glyph) const;
    [[nodiscard]] double kerning(int left, int right) const;
    // Outline in em units, origin on the baseline, y down. Cached.
    [[nodiscard]] const vec::Path& outline(int glyph) const;

    struct Impl;
    explicit Font(std::shared_ptr<Impl> impl, std::string id);
    ~Font();

private:
    std::shared_ptr<Impl> impl_;
    std::string id_;
    double ascent_ = -0.8, descent_ = 0.2, lineGap_ = 0;
};

// UTF-8 to code points, composing decomposed Vietnamese (base letter + combining marks,
// as some keyboards type it) into the precomposed letters fonts have glyphs for.
std::u32string decodeUtf8(const std::string& s);

}  // namespace vstudio::text
