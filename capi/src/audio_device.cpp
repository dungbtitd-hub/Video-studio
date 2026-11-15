#include "audio_device.hpp"

#include <cstring>

#if defined(__ANDROID__)
#include <aaudio/AAudio.h>
#include <android/log.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>
#endif

namespace vstudio::capi {

#if defined(__ANDROID__)

namespace {

class AAudioDevice final : public AudioDevice {
public:
    explicit AAudioDevice(Pull pull, int channels) : pull_(std::move(pull)), channels_(channels) {}
    ~AAudioDevice() override {
        if (stream_) {
            AAudioStream_requestStop(stream_);
            AAudioStream_close(stream_);  // waits for the callback to return
        }
    }
    bool open(int sampleRate) {
        AAudioStreamBuilder* b = nullptr;
        if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
        AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setChannelCount(b, channels_);
        AAudioStreamBuilder_setSampleRate(b, sampleRate);  // the system resamples if it must
        AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setDataCallback(b, &AAudioDevice::onData, this);
        const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &stream_);
        AAudioStreamBuilder_delete(b);
        if (r != AAUDIO_OK || !stream_) {
            __android_log_print(ANDROID_LOG_WARN, "vstudio", "audio output unavailable: %s", AAudio_convertResultToText(r));
            stream_ = nullptr;
            return false;
        }
        if (AAudioStream_getFormat(stream_) != AAUDIO_FORMAT_PCM_FLOAT ||
            AAudioStream_getChannelCount(stream_) != channels_ || AAudioStream_getSampleRate(stream_) != sampleRate) {
            __android_log_print(ANDROID_LOG_WARN, "vstudio", "audio output: format %d / %d ch / %d Hz not as asked",
                                AAudioStream_getFormat(stream_), AAudioStream_getChannelCount(stream_),
                                AAudioStream_getSampleRate(stream_));
            AAudioStream_close(stream_);
            stream_ = nullptr;
            return false;
        }
        if (AAudioStream_requestStart(stream_) != AAUDIO_OK) return false;
        __android_log_print(ANDROID_LOG_INFO, "vstudio", "audio output: %d Hz, %d ch", sampleRate, channels_);
        return true;
    }

private:
    static aaudio_data_callback_result_t onData(AAudioStream*, void* user, void* data, int32_t frames) {
        auto* self = static_cast<AAudioDevice*>(user);
        self->pull_(static_cast<float*>(data), frames);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    Pull pull_;
    int channels_;
    AAudioStream* stream_ = nullptr;
};

}  // namespace

std::unique_ptr<AudioDevice> AudioDevice::open(int sampleRate, int channels, Pull pull) {
    auto d = std::make_unique<AAudioDevice>(std::move(pull), channels);
    if (!d->open(sampleRate)) return nullptr;
    return d;
}

#elif defined(__APPLE__)

namespace {

class AudioUnitDevice final : public AudioDevice {
public:
    explicit AudioUnitDevice(Pull pull, int channels) : pull_(std::move(pull)), channels_(channels) {}
    ~AudioUnitDevice() override {
        if (unit_) {
            AudioOutputUnitStop(unit_);
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
        }
    }
    bool open(int sampleRate) {
        AudioComponentDescription desc{};
        desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
        desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
        if (!comp || AudioComponentInstanceNew(comp, &unit_) != noErr) {
            unit_ = nullptr;
            return false;
        }
        // Interleaved 32-bit float; the unit converts to the hardware rate.
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = sampleRate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        fmt.mChannelsPerFrame = static_cast<UInt32>(channels_);
        fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = fmt.mBytesPerPacket = 4 * static_cast<UInt32>(channels_);
        fmt.mFramesPerPacket = 1;
        AURenderCallbackStruct cb{&AudioUnitDevice::onRender, this};
        if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt) != noErr ||
            AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr ||
            AudioUnitInitialize(unit_) != noErr || AudioOutputUnitStart(unit_) != noErr)
            return false;
        return true;
    }

private:
    static OSStatus onRender(void* user, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                             AudioBufferList* io) {
        auto* self = static_cast<AudioUnitDevice*>(user);
        if (io && io->mNumberBuffers > 0 && io->mBuffers[0].mData) self->pull_(static_cast<float*>(io->mBuffers[0].mData), frames);
        return noErr;
    }
    Pull pull_;
    int channels_;
    AudioComponentInstance unit_ = nullptr;
};

}  // namespace

std::unique_ptr<AudioDevice> AudioDevice::open(int sampleRate, int channels, Pull pull) {
    auto d = std::make_unique<AudioUnitDevice>(std::move(pull), channels);
    if (!d->open(sampleRate)) return nullptr;
    return d;
}

#else

std::unique_ptr<AudioDevice> AudioDevice::open(int, int, Pull) { return nullptr; }

#endif

}  // namespace vstudio::capi
