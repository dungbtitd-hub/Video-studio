// Microphone recording for voice-over (Qt Multimedia: QMediaCaptureSession + QMediaRecorder).
// Writes a file in the background; `finished` brings back its path (empty on failure).
#pragma once

#include <QObject>
#include <QString>

class QAudioInput;
class QMediaCaptureSession;
class QMediaRecorder;

class VoiceRecorder : public QObject {
    Q_OBJECT
public:
    explicit VoiceRecorder(QObject* parent = nullptr);
    ~VoiceRecorder() override;
    // Starts recording into `folder` (created if needed). False (and error()) when no microphone.
    bool start(const QString& folder);
    void stop();
    [[nodiscard]] bool recording() const;
    [[nodiscard]] QString error() const { return error_; }
    static bool hasMicrophone();

signals:
    void finished(const QString& file);

private:
    QMediaCaptureSession* session_ = nullptr;
    QAudioInput* input_ = nullptr;
    QMediaRecorder* recorder_ = nullptr;
    QString error_;
    QString file_;
    bool stopping_ = false;
};
