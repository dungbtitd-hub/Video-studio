// Apple media module, shared by macOS and iOS: AVFoundation (AVAssetReader /
// AVAssetWriter), which uses VideoToolbox hardware codecs underneath.
// Registers as a "media.backend" module; the core only sees IMediaBackend.
#pragma once

#include <memory>

#include "vstudio/modules.hpp"

namespace vstudio::apple {

std::shared_ptr<IMediaBackend> createBackend();
// Registers module id "apple.avfoundation" (capabilities: probe, decode, encode.h264, encode.aac)
// for platforms "macos" and "ios".
void registerModule(ModuleRegistry& r, int priority = 20);

}  // namespace vstudio::apple
