// The phone's / tablet's speaker for the editor preview (Android AAudio, Apple AudioUnit).
// The device pulls interleaved float samples from the preview engine on its own real-time
// thread; the preview engine's sample clock then drives the playhead.
// Desktop apps open their own device (Qt); elsewhere open() returns nullptr and the preview
// plays silently on the wall clock.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace vstudio::capi {

class AudioDevice {
public:
    using Pull = std::function<void(float* interleaved, std::int64_t frames)>;
    virtual ~AudioDevice() = default;  // stops and closes the device
    // Opens and starts output at `sampleRate` / `channels` (float). nullptr when there is no device.
    static std::unique_ptr<AudioDevice> open(int sampleRate, int channels, Pull pull);
};

}  // namespace vstudio::capi
