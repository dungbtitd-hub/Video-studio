// Camera RAW photos with Apple's own RAW engine (Core Image CIRAWFilter, macOS 12+ /
// iOS 15+): module "apple.raw" of kind "media.still". It has the newest camera
// profiles on Apple devices and develops on the GPU. Registered with a higher priority
// than LibRaw, which still reads any file this engine cannot (or older systems).
#pragma once

#include <memory>

#include "vstudio/stills.hpp"

namespace vstudio::apple {

// nullptr before macOS 12 / iOS 15.
std::shared_ptr<IStillDecoder> createRawDecoder();
void registerRawModule(ModuleRegistry& r, int priority = 10);

}  // namespace vstudio::apple
