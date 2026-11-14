#include "VoiceRecorder.h"

#include <QAudioDevice>
#include <QAudioInput>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QUrl>

VoiceRecorder::VoiceRecorder(QObject* parent) : QObject(parent) {}

VoiceRecorder::~VoiceRecorder() {
    if (recorder_) recorder_->stop();
}

bool VoiceRecorder::hasMicrophone() { return !QMediaDevices::audioInputs().isEmpty(); }

bool VoiceRecorder::start(const QString& folder) {
    error_.clear();
    if (recording()) return true;
    if (!hasMicrophone()) {
        error_ = tr("Không tìm thấy micro");
        return false;
    }
    QDir().mkpath(folder);
    file_ = QDir(folder).filePath(QStringLiteral("thu-am-%1.m4a").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")));
    if (!session_) {
        session_ = new QMediaCaptureSession(this);
        input_ = new QAudioInput(QMediaDevices::defaultAudioInput(), this);
        recorder_ = new QMediaRecorder(this);
        session_->setAudioInput(input_);
        session_->setRecorder(recorder_);
        connect(recorder_, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState st) {
            if (st != QMediaRecorder::StoppedState || !stopping_) return;
            stopping_ = false;
            const QString out = recorder_->actualLocation().toLocalFile();
            emit finished(QFileInfo(out).size() > 0 ? out : QString());
        });
        connect(recorder_, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString& msg) {
            error_ = msg;
            if (stopping_ || recorder_->recorderState() == QMediaRecorder::StoppedState) {
                stopping_ = false;
                emit finished({});
            }
        });
    }
    QMediaFormat fmt(QMediaFormat::MPEG4);
    fmt.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    recorder_->setMediaFormat(fmt);
    recorder_->setQuality(QMediaRecorder::HighQuality);
    recorder_->setAudioSampleRate(48000);
    recorder_->setOutputLocation(QUrl::fromLocalFile(file_));
    recorder_->record();
    return error_.isEmpty();
}

void VoiceRecorder::stop() {
    if (!recording()) return;
    stopping_ = true;
    recorder_->stop();
}

bool VoiceRecorder::recording() const { return recorder_ && recorder_->recorderState() == QMediaRecorder::RecordingState; }
