#include "vstudio/scheduler.hpp"

#include <algorithm>

namespace vstudio {

JobScheduler::JobScheduler(unsigned workers) {
    if (workers == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        workers = hw > 1 ? hw - 1 : 1;
    }
    for (unsigned i = 0; i < workers; ++i) threads_.emplace_back([this] { run(); });
}

JobScheduler::~JobScheduler() {
    {
        std::lock_guard lk(mu_);
        stop_ = true;
        for (auto& [id, tok] : running_) tok.cancel();
        while (!queue_.empty()) queue_.pop();
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

JobId JobScheduler::submit(std::string name, JobPriority prio, std::function<void(const CancelToken&)> fn,
                           CancelToken token) {
    JobId id;
    {
        std::lock_guard lk(mu_);
        id = nextId_++;
        queue_.push(Job{id, prio, seq_++, std::move(name), std::move(fn), std::move(token)});
    }
    cv_.notify_one();
    return id;
}

void JobScheduler::cancel(JobId id) {
    std::lock_guard lk(mu_);
    for (auto& [rid, tok] : running_)
        if (rid == id) tok.cancel();
    // Queued jobs: mark cancelled; they are skipped when dequeued.
    std::vector<Job> keep;
    while (!queue_.empty()) {
        Job j = queue_.top();
        queue_.pop();
        if (j.id == id) j.token.cancel();
        keep.push_back(std::move(j));
    }
    for (auto& j : keep) queue_.push(std::move(j));
}

void JobScheduler::cancelAll() {
    std::lock_guard lk(mu_);
    for (auto& [rid, tok] : running_) tok.cancel();
    while (!queue_.empty()) {
        Job j = queue_.top();
        queue_.pop();
        j.token.cancel();
    }
    idleCv_.notify_all();
}

void JobScheduler::waitIdle() {
    std::unique_lock lk(mu_);
    idleCv_.wait(lk, [this] { return queue_.empty() && running_.empty(); });
}

std::size_t JobScheduler::pending() const {
    std::lock_guard lk(mu_);
    return queue_.size();
}

void JobScheduler::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lk(mu_);
            cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = queue_.top();
            queue_.pop();
            if (job.token.cancelled()) {
                if (queue_.empty() && running_.empty()) idleCv_.notify_all();
                continue;
            }
            running_.emplace_back(job.id, job.token);
        }
        try {
            job.fn(job.token);
        } catch (...) {
            // Jobs report their own errors; a throwing job must not kill the worker.
        }
        {
            std::lock_guard lk(mu_);
            std::erase_if(running_, [&](auto& r) { return r.first == job.id; });
            if (queue_.empty() && running_.empty()) idleCv_.notify_all();
        }
    }
}

}  // namespace vstudio
