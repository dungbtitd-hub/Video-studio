// FFmpeg media module (desktop default on Windows/Linux, fallback elsewhere).
// Registers itself as a "media.backend" module; the core only sees IMediaBackend.
#pragma once

#include <memory>

#include "vstudio/modules.hpp"

namespace vstudio::ffmpeg {

std::shared_ptr<IMediaBackend> createBackend();
// Registers module id "ffmpeg" (capabilities: probe, decode, encode.h264, encode.aac).
void registerModule(ModuleRegistry& r, int priority = 10);

}  // namespace vstudio::ffmpeg
