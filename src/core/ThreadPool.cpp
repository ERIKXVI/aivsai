#include "core/ThreadPool.hpp"

#include <algorithm>

namespace pm {

ThreadPool::ThreadPool(unsigned workerCount) {
    if (workerCount == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        workerCount = hw > 1 ? hw - 1 : 1;
    }
    workerCount = std::min(workerCount, 15u);
    workers_.reserve(workerCount);
    for (unsigned i = 0; i < workerCount; ++i) workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_) t.join();
}

void ThreadPool::drain() {
    // Claim job indices until the batch is exhausted. `job_` and `count_` were published
    // under the mutex before any participant could reach this point.
    for (;;) {
        int i = next_.fetch_add(1, std::memory_order_relaxed);
        if (i >= count_) break;
        (*job_)(i);
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard lock(mutex_);
            done_.notify_all();
        }
    }
}

void ThreadPool::parallelFor(int count, const std::function<void(int)>& job) {
    if (count <= 0) return;
    if (count == 1 || workers_.empty()) {
        for (int i = 0; i < count; ++i) job(i);
        return;
    }
    {
        std::lock_guard lock(mutex_);
        job_ = &job;
        count_ = count;
        next_.store(0, std::memory_order_relaxed);
        remaining_.store(count, std::memory_order_relaxed);
        ++generation_;
    }
    wake_.notify_all();
    drain();

    // Wait for the work *and* for every worker to leave drain(), so no worker can observe
    // the next batch's counters while still holding this batch's job pointer.
    std::unique_lock lock(mutex_);
    done_.wait(lock, [this] { return remaining_.load(std::memory_order_acquire) == 0 && active_ == 0; });
    job_ = nullptr;
}

void ThreadPool::workerLoop() {
    uint64_t seenGeneration = 0;
    std::unique_lock lock(mutex_);
    for (;;) {
        wake_.wait(lock, [&] { return stopping_ || generation_ != seenGeneration; });
        if (stopping_) return;
        seenGeneration = generation_;
        if (job_ == nullptr) continue;
        ++active_;
        lock.unlock();
        drain();
        lock.lock();
        if (--active_ == 0) done_.notify_all();
    }
}

} // namespace pm
