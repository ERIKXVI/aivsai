#pragma once
// A minimal fork-join thread pool tailored for the per-frame cellular automaton: the main
// thread submits a batch of independent jobs and blocks until all of them complete. The
// calling thread participates in the work so a pool of N workers uses N+1 cores.

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace pm {

class ThreadPool {
public:
    /// `workerCount == 0` picks hardware_concurrency() - 1 (at least one worker).
    explicit ThreadPool(unsigned workerCount = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Runs `job(i)` for every i in [0, count) across the pool and waits for completion.
    void parallelFor(int count, const std::function<void(int)>& job);

    unsigned workerCount() const { return static_cast<unsigned>(workers_.size()); }

private:
    void workerLoop();
    void drain();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;

    const std::function<void(int)>* job_ = nullptr;
    std::atomic<int> next_{0};
    std::atomic<int> remaining_{0};
    int count_ = 0;
    int active_ = 0; // workers currently inside drain(); guarded by mutex_
    uint64_t generation_ = 0;
    bool stopping_ = false;
};

} // namespace pm
