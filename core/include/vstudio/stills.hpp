// Still-image decoders for formats a video backend does not read: camera RAW photos
// (CR2/CR3, NEF, ARW, RAF, ORF, RW2, DNG, ...).
//
// A still decoder is a module of kind "media.still" (LibRaw on every OS, Apple's own
// RAW engine on macOS/iOS). The core never links one: withStillDecoders() wraps the
// session's media backend and hands a RAW file to the highest-priority still module
// that reads it, loading that module only when the first RAW file is opened. Video,
// audio and ordinary images keep going to the backend.
//
// RAW pictures are developed to display-referred Rec.709 (camera white balance,
// camera colour matrix) and come out as RGBA16F when deep output is asked, so the
// pro grade works on the full 12-14 bits of the sensor. Preview asks for its own size
// and decoders develop at reduced resolution for it (much faster); export gets full size.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vstudio/media.hpp"
#include "vstudio/modules.hpp"

namespace vstudio {

class IStillDecoder {
public:
    static constexpr const char* kModuleKind = "media.still";
    virtual ~IStillDecoder() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    // Header only (fast): size, bit depth. isStillImage is true.
    virtual std::optional<ProbeResult> probe(const std::string& path, std::string* error) = 0;
    // Develops the picture to fit inside maxW x maxH (0 = full size), keeping its aspect.
    // deep: RGBA16F output, else RGBA8. Must be safe to call from several threads at once.
    virtual std::optional<VideoFrame> decode(const std::string& path, int maxW, int maxH, bool deep,
                                             std::string* error) = 0;
};

// Lower-case extensions (with the dot) of camera RAW files.
const std::vector<std::string>& cameraRawExtensions();
bool isCameraRawPath(const std::string& path);

// `primary` with camera RAW files routed to "media.still" modules of `modules` (tried in
// priority order; a file the first cannot read goes to the next). `modules` must outlive
// the returned backend. Returns `primary` itself when no still module is registered.
std::shared_ptr<IMediaBackend> withStillDecoders(std::shared_ptr<IMediaBackend> primary, ModuleRegistry& modules);

}  // namespace vstudio
