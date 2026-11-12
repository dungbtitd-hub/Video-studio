// Video Studio Core - Job Scheduler.
// All heavy media work (probe, thumbnails, waveforms, proxies, export) runs
// here, never on the UI thread. Jobs have a priority and a cancellation token;
// interactive work (what the user is looking at) always goes first.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace vstudio {

enum class JobPriority : int { Background = 0, Normal = 1, Interactive = 2 };

class CancelToken {
public:
    void cancel() { flag_->store(true); }
    [[nodiscard]] bool cancelled() const { return flag_->load(); }
    [[nodiscard]] const std::atomic<bool>* flag() const { return flag_.get(); }

private:
    std::shared_ptr<std::atomic<bool>> flag_ = std::make_shared<std::atomic<bool>>(false);
};

using JobId = std::uint64_t;

class JobScheduler {
public:
    // workers = 0 picks hardware_concurrency - 1 (at least 1), leaving a core for the UI.
    explicit JobScheduler(unsigned workers = 0);
    ~JobScheduler();
    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    // `fn` receives the job's cancel token and should poll it in long loops.
    JobId submit(std::string name, JobPriority prio, std::function<void(const CancelToken&)> fn,
                 CancelToken token = {});
    // Cancels a queued or running job (cooperative).
    void cancel(JobId id);
    void cancelAll();
    // Blocks until the queue is empty and no job is running (tests/shutdown).
    void waitIdle();
    [[nodiscard]] unsigned workerCount() const { return static_cast<unsigned>(threads_.size()); }
    [[nodiscard]] std::size_t pending() const;

private:
    struct Job {
        JobId id;
        JobPriority prio;
        std::uint64_t seq;
        std::string name;
        std::function<void(const CancelToken&)> fn;
        CancelToken token;
    };
    struct Order {
        bool operator()(const Job& a, const Job& b) const {
            if (a.prio != b.prio) return static_cast<int>(a.prio) < static_cast<int>(b.prio);
            return a.seq > b.seq;  // FIFO within a priority
        }
    };
    void run();

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::priority_queue<Job, std::vector<Job>, Order> queue_;
    std::vector<std::pair<JobId, CancelToken>> running_;
    std::vector<std::thread> threads_;
    std::uint64_t nextId_ = 1;
    std::uint64_t seq_ = 0;
    bool stop_ = false;
};

}  // namespace vstudio
