// Feeds the preview engine's audio ring buffer to the sound card.
// The device pulls; readData() only copies from the ring (never decodes).
#pragma once

#include <QAudioSink>
#include <QIODevice>
#include <memory>

namespace vstudio {
class PreviewEngine;
}

class AudioPull : public QIODevice {
public:
    AudioPull(vstudio::PreviewEngine* e, int channels) : engine_(e), channels_(channels) {}
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char*, qint64) override { return -1; }
    qint64 bytesAvailable() const override { return 1 << 16; }
    bool isSequential() const override { return true; }

private:
    vstudio::PreviewEngine* engine_;
    int channels_;
};

class AudioOutput {
public:
    AudioOutput(vstudio::PreviewEngine* engine, int sampleRate, int channels);
    ~AudioOutput();
    void start();
    void stop();

private:
    std::unique_ptr<AudioPull> pull_;
    std::unique_ptr<QAudioSink> sink_;
};
