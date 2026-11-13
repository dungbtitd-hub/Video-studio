// Android media module: AMediaExtractor + AMediaCodec (hardware decode and
// encode) + AMediaMuxer, through the NDK C API (libmediandk, API 26+).
// Registers as a "media.backend" module; the core only sees IMediaBackend.
//
// Paths may be plain file paths or "fd:<number>" for files the Flutter side
// opened through the Storage Access Framework / Photo Picker.
//
// The host process needs binder threads (an app process always has them): the
// extractor service reads files back through binder calls into this process.
// A bare executable must start them first (see tests/test_mediacodec.cpp).
#pragma once

#include <memory>

#include "vstudio/modules.hpp"

namespace vstudio::android {

std::shared_ptr<IMediaBackend> createBackend();
// Registers module id "android.mediacodec" (capabilities: probe, decode, encode.h264, encode.aac).
void registerModule(ModuleRegistry& r, int priority = 20);

}  // namespace vstudio::android
