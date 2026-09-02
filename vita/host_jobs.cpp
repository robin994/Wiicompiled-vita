#include "wiicompiled_vita/host_jobs.h"

#include <chrono>

namespace WiiCompiledVita {

void HostJobFence::wait() noexcept {
    if (complete()) {
        return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] { return complete(); });
}

bool HostJobFence::waitFor(uint32_t timeoutMicroseconds) noexcept {
    if (complete()) {
        return true;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, std::chrono::microseconds(timeoutMicroseconds),
                               [this] { return complete(); });
}

void HostJobFence::signal() noexcept {
    if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        condition_.notify_all();
    }
}

HostJobSystem::~HostJobSystem() {
    stop();
}

bool HostJobSystem::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load(std::memory_order_relaxed)) {
        return true;
    }

    stopping_ = false;
    readIndex_ = 0;
    writeIndex_ = 0;
    queued_ = 0;

    if (!worker_.start(HostThreadRole::Background, kWorkerStackSize, [this] { workerMain(); })) {
        return false;
    }
    running_.store(true, std::memory_order_release);
    return true;
}

void HostJobSystem::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_relaxed)) {
            return;
        }
        stopping_ = true;
    }
    wake_.notify_all();
    worker_.join();
    running_.store(false, std::memory_order_release);
}

bool HostJobSystem::submit(HostJobFunction function, void* context, HostJobFence* fence) noexcept {
    if (function == nullptr) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_relaxed) || stopping_ || queued_ == kQueueCapacity) {
            return false;
        }

        if (fence != nullptr) {
            fence->add();
        }
        queue_[writeIndex_] = Job{function, context, fence};
        writeIndex_ = (writeIndex_ + 1) % kQueueCapacity;
        ++queued_;
    }
    wake_.notify_one();
    return true;
}

void HostJobSystem::workerMain() noexcept {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || queued_ != 0; });
            if (queued_ == 0) {
                if (stopping_) {
                    return;
                }
                continue;
            }

            job = queue_[readIndex_];
            readIndex_ = (readIndex_ + 1) % kQueueCapacity;
            --queued_;
        }

        job.function(job.context);
        if (job.fence != nullptr) {
            job.fence->signal();
        }
    }
}

HostJobSystem& BackgroundJobs() noexcept {
    static HostJobSystem jobs;
    return jobs;
}

} // namespace WiiCompiledVita
