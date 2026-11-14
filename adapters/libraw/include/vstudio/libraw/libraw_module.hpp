// Camera RAW photos with LibRaw (all platforms): module "libraw" of kind "media.still".
// The core reaches it through withStillDecoders() (vstudio/stills.hpp) only when a RAW
// file is opened.
#pragma once

#include <memory>

#include "vstudio/stills.hpp"

namespace vstudio::libraw {

std::shared_ptr<IStillDecoder> createDecoder();
// Registers module id "libraw" (capability "decode.raw"). On Apple platforms the native
// RAW engine registers with a higher priority and LibRaw reads what it cannot.
void registerModule(ModuleRegistry& r, int priority = 0);

}  // namespace vstudio::libraw
