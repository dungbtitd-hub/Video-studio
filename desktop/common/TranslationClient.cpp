#include "TranslationClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
#include <QUrl>

using vstudio::Json;
namespace sub = vstudio::subtitles;

namespace {
constexpr int kMaxAttempts = 3;
constexpr int kTimeoutMs = 180'000;

// "python3 /path/to/translate.py --model x" -> program + arguments (quotes group words).
QStringList splitCommand(const QString& cmd) { return QProcess::splitCommand(cmd); }
}  // namespace

TranslationClient::TranslationClient(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

TranslationClient::~TranslationClient() { cancel(); }

Json TranslationClient::jobConfig(const Json& config) {
    Json c = config;
    if (c.value("provider", std::string{}) == "command") {
        c["provider"] = "custom";
        c["url"] = "stdin:";
    }
    return c;
}

void TranslationClient::start(std::unique_ptr<sub::TranslationJob> job, const Json& config, int parallel) {
    cancel();
    job_ = std::move(job);
    config_ = config;
    parallel_ = std::clamp(parallel, 1, 8);
    attempts_.assign(job_->batchCount(), 0);
    next_ = 0;
    retry_.clear();
    inFlight_ = failed_ = 0;
    finished_ = cancelled_ = false;
    lastError_.clear();
    emit progress(0, static_cast<int>(job_->batchCount()));
    pump();
}

void TranslationClient::cancel() {
    const bool wasRunning = running();
    cancelled_ = true;
    const auto pending = pending_;
    pending_.clear();
    for (QObject* o : pending) {
        if (auto* r = qobject_cast<QNetworkReply*>(o)) {
            r->disconnect(this);
            r->abort();
            r->deleteLater();
        } else if (auto* p = qobject_cast<QProcess*>(o)) {
            p->disconnect(this);
            p->kill();
            p->deleteLater();
        }
    }
    inFlight_ = 0;
    if (wasRunning) {
        finished_ = true;
        emit finished(failed_, lastError_, true);
    }
}

void TranslationClient::pump() {
    if (!job_ || finished_) return;
    while (inFlight_ < parallel_) {
        if (!retry_.empty()) {
            const std::size_t b = retry_.front();
            retry_.erase(retry_.begin());
            send(b);
        } else if (next_ < job_->batchCount()) {
            send(next_++);
        } else {
            break;
        }
    }
    if (inFlight_ == 0 && retry_.empty() && next_ >= job_->batchCount()) {
        finished_ = true;
        emit finished(failed_, lastError_, false);
    }
}

void TranslationClient::send(std::size_t batch) {
    ++inFlight_;
    ++attempts_[batch];
    const std::string provider = config_.value("provider", std::string{});
    if (provider == "command") {
        // The open protocol over stdin/stdout (the job was made with provider "custom").
        std::string body;
        try {
            body = job_->request(batch).body;
        } catch (const std::exception& e) {
            QTimer::singleShot(0, this, [this, batch, msg = QString::fromStdString(e.what())] { done(batch, false, msg); });
            return;
        }
        QStringList args = splitCommand(QString::fromStdString(config_.value("command", std::string{})));
        if (args.isEmpty()) {
            QTimer::singleShot(0, this, [this, batch] { done(batch, false, tr("Chưa nhập lệnh chạy chương trình dịch.")); });
            return;
        }
        auto* p = new QProcess(this);
        pending_.insert(p);
        const QString program = args.takeFirst();
        connect(p, &QProcess::finished, this, [this, p, batch](int code, QProcess::ExitStatus st) {
            if (!pending_.remove(p)) return;
            p->deleteLater();
            const QByteArray out = p->readAllStandardOutput();
            if (st != QProcess::NormalExit || code != 0) {
                done(batch, false, tr("Chương trình dịch thoát với mã %1: %2")
                                       .arg(code)
                                       .arg(QString::fromUtf8(p->readAllStandardError()).left(300)));
                return;
            }
            std::string err;
            const bool ok = job_->accept(batch, out.toStdString(), &err);
            done(batch, ok, QString::fromStdString(err));
        });
        connect(p, &QProcess::errorOccurred, this, [this, p, batch](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart || !pending_.remove(p)) return;
            p->deleteLater();
            done(batch, false, tr("Không chạy được chương trình dịch."));
        });
        p->start(program, args);
        p->write(QByteArray::fromStdString(body));
        p->closeWriteChannel();
        QTimer::singleShot(kTimeoutMs, p, [p] { p->kill(); });
        return;
    }
    sub::HttpRequest r;
    try {
        r = job_->request(batch);
    } catch (const std::exception& e) {
        QTimer::singleShot(0, this, [this, batch, msg = QString::fromStdString(e.what())] { done(batch, false, msg); });
        return;
    }
    QNetworkRequest req(QUrl(QString::fromStdString(r.url)));
    for (const auto& [k, v] : r.headers) req.setRawHeader(QByteArray::fromStdString(k), QByteArray::fromStdString(v));
    req.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = net_->post(req, QByteArray::fromStdString(r.body));
    pending_.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, batch] {
        if (!pending_.remove(reply)) return;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        std::string err;
        // The core reads error bodies too (they say what went wrong: a bad key, a wrong model).
        const bool ok = !body.isEmpty() && job_->accept(batch, body.toStdString(), &err);
        if (!ok && err.empty()) err = reply->errorString().toStdString();
        if (!ok && status) err = "HTTP " + std::to_string(status) + ": " + err;
        done(batch, ok, QString::fromStdString(err));
    });
}

void TranslationClient::done(std::size_t batch, bool ok, const QString& error) {
    --inFlight_;
    if (!ok) {
        lastError_ = error;
        // A bad key or address fails every time: no point in retrying those.
        const bool hopeless = error.startsWith("HTTP 401") || error.startsWith("HTTP 403") || error.startsWith("HTTP 404");
        if (attempts_[batch] < kMaxAttempts && !hopeless) {
            ++inFlight_;  // the slot stays taken during the back-off
            QTimer::singleShot(1500 * attempts_[batch], this, [this, batch] {
                --inFlight_;
                if (finished_) return;
                retry_.push_back(batch);
                pump();
            });
            return;
        }
        ++failed_;
    }
    emit progress(static_cast<int>(job_->doneCount()), static_cast<int>(job_->batchCount()));
    pump();
}
