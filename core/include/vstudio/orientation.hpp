// Picture orientation: which way a source picture is turned before it is composited.
//
//   - The file's own rotation (phones store portrait video as landscape pixels plus a
//     "rotate 90°" flag; JPEG photos carry an EXIF orientation). Probed once on import and
//     kept in asset.extra["rotation"] (clockwise degrees).
//   - The user's choice per clip ("Xoay / lật"): clip.extra["orient"] =
//     {"rotate": 0|90|180|270, "flip_h": bool, "flip_v": bool}, applied after the file's
//     rotation, flips last.
//
// Orientation is applied to decoded frames (preview, export, thumbnails), so the layout code
// only ever sees upright pictures; decoders, proxies and caches stay in file orientation.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "vstudio/media.hpp"
#include "vstudio/model.hpp"

namespace vstudio {

struct Orientation {
    int rotate = 0;  // clockwise degrees: 0, 90, 180, 270
    bool flipH = false;
    bool flipV = false;
    [[nodiscard]] bool identity() const { return rotate == 0 && !flipH && !flipV; }
    [[nodiscard]] bool swapsAxes() const { return rotate == 90 || rotate == 270; }
    // Short id for cache keys: "" when identity, else e.g. "r90h".
    [[nodiscard]] std::string key() const;
    bool operator==(const Orientation&) const = default;
};

// Any angle to 0/90/180/270 (nearest quarter turn, negative angles allowed).
int normalizeQuarterTurn(int degrees);

// The file's rotation, from asset.extra["rotation"].
int assetRotation(const MediaAsset& a);
// The size the asset shows at once its own rotation is applied.
std::pair<int, int> displaySize(const MediaAsset& a);
// The user's choice on a clip (identity when none).
Orientation clipOrientation(const Clip& c);
Json toJson(const Orientation& o);
Orientation orientationFromJson(const Json& j);
// File rotation, then the clip's rotation, then its flips.
Orientation combinedOrientation(const MediaAsset& a, const Clip* c);

// Size of the asset's pictures once `o` is applied (o = combinedOrientation(asset, clip)).
std::pair<int, int> orientedSize(const MediaAsset& a, const Orientation& o);

// A decoder whose frames come out turned by `o` (setMaxOutputSize takes the turned box).
// Returns `inner` itself when `o` is the identity.
std::unique_ptr<IMediaDecoder> orientedDecoder(std::unique_ptr<IMediaDecoder> inner, const Orientation& o);

// Turns `f` in place (RGBA8 and RGBA16F).
void orientFrame(VideoFrame& f, const Orientation& o);
// A copy that fits inside maxW x maxH (keeps aspect; box filter). Unchanged when it already fits.
VideoFrame fitFrame(const VideoFrame& f, int maxW, int maxH);

// EXIF orientation (1..8) of a JPEG file, read from its APP1 segment, or nullopt.
std::optional<int> jpegExifOrientation(const std::string& path);
// EXIF orientation 1..8 -> what turns the stored pixels upright.
Orientation fromExif(int exifOrientation);

}  // namespace vstudio
