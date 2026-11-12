#include "AudioOutput.h"

#include <QAudioFormat>
#include <QMediaDevices>

#include "vstudio/preview.hpp"

qint64 AudioPull::readData(char* data, qint64 maxlen) {
    const qint64 frameBytes = static_cast<qint64>(sizeof(float)) * channels_;
    const qint64 frames = maxlen / frameBytes;
    if (frames <= 0) return 0;
    engine_->pullAudio(reinterpret_cast<float*>(data), frames);
    return frames * frameBytes;
}

AudioOutput::AudioOutput(vstudio::PreviewEngine* engine, int sampleRate, int channels) {
    QAudioFormat fmt;
    fmt.setSampleRate(sampleRate);
    fmt.setChannelCount(channels);
    fmt.setSampleFormat(QAudioFormat::Float);
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull() || !dev.isFormatSupported(fmt)) return;  // no sound card: preview falls back to the wall clock
    pull_ = std::make_unique<AudioPull>(engine, channels);
    pull_->open(QIODevice::ReadOnly);
    sink_ = std::make_unique<QAudioSink>(dev, fmt);
    sink_->setBufferSize(sampleRate / 20 * channels * static_cast<int>(sizeof(float)));  // ~50 ms device buffer
}

AudioOutput::~AudioOutput() { stop(); }

void AudioOutput::start() {
    if (sink_) sink_->start(pull_.get());
}

void AudioOutput::stop() {
    if (sink_) sink_->stop();
}
