// Sends the batches of a subtitle translation job (vstudio/subtitles.hpp) to an AI service and
// hands the replies back to the job. The core builds every request and reads every reply; this
// class only moves bytes, so a new service needs no change here.
//
// Two transports:
//   * HTTP (Claude, OpenAI-compatible, Gemini, the open "custom" protocol): Qt Network, async on
//     the UI thread, a few batches in flight at once, each retried twice before giving up.
//   * "command": a program on this computer (a script calling any AI). It gets the open protocol's
//     JSON request on stdin and writes the JSON reply to stdout (docs/subtitles.md).
#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <memory>
#include <vector>

#include "vstudio/subtitles.hpp"

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

class TranslationClient : public QObject {
    Q_OBJECT
public:
    explicit TranslationClient(QObject* parent = nullptr);
    ~TranslationClient() override;

    // Starts sending `job`'s batches. `config` is the user's connector; for "command" the job must
    // be made with provider "custom" (jobConfig() does that).
    void start(std::unique_ptr<vstudio::subtitles::TranslationJob> job, const vstudio::Json& config, int parallel = 3);
    void cancel();
    static vstudio::Json jobConfig(const vstudio::Json& config);
    [[nodiscard]] bool running() const { return job_ && !finished_; }
    [[nodiscard]] vstudio::subtitles::TranslationJob* job() const { return job_.get(); }
    // Takes the finished job (for applying its results).
    std::unique_ptr<vstudio::subtitles::TranslationJob> take() { return std::move(job_); }

signals:
    void progress(int done, int total);
    // All batches answered or given up; `failed` batches keep their source text.
    void finished(int failed, const QString& lastError, bool cancelled);

private:
    void pump();
    void send(std::size_t batch);
    void done(std::size_t batch, bool ok, const QString& error);

    QNetworkAccessManager* net_;
    std::unique_ptr<vstudio::subtitles::TranslationJob> job_;
    vstudio::Json config_;
    int parallel_ = 3;
    std::vector<int> attempts_;
    std::size_t next_ = 0;
    std::vector<std::size_t> retry_;
    int inFlight_ = 0;
    int failed_ = 0;
    bool finished_ = false;
    bool cancelled_ = false;
    QString lastError_;
    QSet<QObject*> pending_;  // replies / processes in flight
};
