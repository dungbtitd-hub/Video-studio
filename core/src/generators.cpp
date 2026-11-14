#include "vstudio/generators.hpp"

#include "vstudio/keyframes.hpp"
#include "vstudio/modules.hpp"
#include "vstudio/overlays.hpp"
#include "vstudio/stickers.hpp"
#include "vstudio/text.hpp"

namespace vstudio {

namespace {

class TextGenerator final : public IClipGenerator {
public:
    void draw(const Json& c, double local, double length, const vec::Affine& place, VideoFrame& out) const override {
        text::drawText(c, local, length, place, out);
    }
    bool still(const Json& c, double local, double length) const override { return text::textStill(c, local, length); }
    std::array<double, 4> bounds(const Json& c) const override { return text::textBounds(c); }
};

class StickerGenerator final : public IClipGenerator {
public:
    void draw(const Json& c, double local, double length, const vec::Affine& place, VideoFrame& out) const override {
        stickers::drawSticker(c, local, length, place, out);
    }
    bool still(const Json& c, double local, double length) const override {
        return stickers::stickerStill(c, local, length);
    }
    std::array<double, 4> bounds(const Json& c) const override { return stickers::stickerBounds(c); }
};

class OverlayGenerator final : public IClipGenerator {
public:
    void draw(const Json& c, double local, double length, const vec::Affine& place, VideoFrame& out) const override {
        overlays::drawOverlay(c, local, length, place, out);
    }
    bool still(const Json& c, double local, double length) const override {
        return overlays::overlayStill(c, local, length);
    }
    std::array<double, 4> bounds(const Json& c) const override { return overlays::overlayBounds(c); }
};

std::shared_ptr<IClipGenerator> builtin(const std::string& kind) {
    static const auto textGen = std::make_shared<TextGenerator>();
    static const auto stickerGen = std::make_shared<StickerGenerator>();
    if (kind == "text") return textGen;
    static const auto overlayGen = std::make_shared<OverlayGenerator>();
    if (kind == "sticker") return stickerGen;
    if (kind == "overlay") return overlayGen;
    return nullptr;
}

}  // namespace

std::shared_ptr<IClipGenerator> generatorFor(ModuleRegistry* modules, const std::string& kind) {
    if (modules)
        if (auto g = modules->best<IClipGenerator>("generate." + kind)) return g;
    return builtin(kind);
}

void registerGeneratorModules(ModuleRegistry& r) {
    r.add<IClipGenerator>({.id = "builtin.text", .displayName = "Text", .capabilities = {"generate.text"}},
                          [] { return builtin("text"); });
    r.add<IClipGenerator>({.id = "builtin.sticker", .displayName = "Stickers", .capabilities = {"generate.sticker"}},
                          [] { return builtin("sticker"); });
    r.add<IClipGenerator>({.id = "builtin.overlay", .displayName = "Overlay effects", .capabilities = {"generate.overlay"}},
                          [] { return builtin("overlay"); });
}

std::array<double, 4> generatedRectOnCanvas(const Project& p, const Clip& c, Time sourceTime) {
    const auto g = generatorFor(nullptr, c.content.value("kind", std::string{}));
    const std::array<double, 4> b = g ? g->bounds(c.content) : std::array<double, 4>{-50, -50, 50, 50};
    const Transform tf = animatedTransform(c, sourceTime);
    const double cx = p.canvas.width / 2.0 + tf.x, cy = p.canvas.height / 2.0 + tf.y;
    return {cx + b[0] * tf.scale, cy + b[1] * tf.scale, (b[2] - b[0]) * tf.scale, (b[3] - b[1]) * tf.scale};
}

}  // namespace vstudio
