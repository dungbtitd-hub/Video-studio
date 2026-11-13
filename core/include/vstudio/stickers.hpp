// Stickers ("builtin.sticker"): a library of animated vector stickers drawn in-house
// (faces, hearts, flowers, stars, weather, celebrations, signs, food, animals). Each one
// has its own gentle idle motion (a heart beats, a sun's rays turn...) and takes the same
// in/out/loop animations as text (motion.hpp).
//
// Content: {"kind": "sticker", "sticker": "<id>", "size": <canvas px>, "rotation": <degrees>,
//   "flip": false, "alive": true (idle motion), "in": {...}, "out": {...}, "loop": {...}}
#pragma once

#include <array>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"
#include "vstudio/vector.hpp"

namespace vstudio::stickers {

struct StickerInfo {
    std::string id;
    std::string name;      // Vietnamese
    std::string category;  // Vietnamese, in display order (see stickerCategories)
};

const std::vector<StickerInfo>& stickerCatalog();
std::vector<std::string> stickerCategories();
[[nodiscard]] const StickerInfo* findSticker(const std::string& id);

Json defaultStickerContent(const std::string& id);
void drawSticker(const Json& content, double local, double length, const vec::Affine& place, VideoFrame& out);
std::array<double, 4> stickerBounds(const Json& content);
bool stickerStill(const Json& content, double local, double length);
// Draws sticker `id` filling `out` (a thumbnail), its idle motion at `seconds`.
void drawStickerIcon(const std::string& id, double seconds, VideoFrame& out);

}  // namespace vstudio::stickers
