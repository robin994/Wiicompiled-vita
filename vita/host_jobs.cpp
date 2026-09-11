#include "wiicompiled_vita/host_jobs.h"

#include <chrono>
#include <psp2/kernel/processmgr.h>

namespace {
void AtomicMax(std::atomic<uint64_t>& dst, uint64_t value) noexcept {
    uint64_t current = dst.load(std::memory_order_relaxed);
    while (current < value && !dst.compare_exchange_weak(current, value,
            std::memory_order_relaxed, std::memory_order_relaxed)) {}
}
void AtomicMaxSize(std::atomic<size_t>& dst, size_t value) noexcept {
    size_t current = dst.load(std::memory_order_relaxed);
    while (current < value && !dst.compare_exchange_weak(current, value,
            std::memory_order_relaxed, std::memory_order_relaxed)) {}
}
}

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

bool HostJobSystem::start(HostThreadRole role) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load(std::memory_order_relaxed)) {
        return true;
    }

    stopping_ = false;
    readIndex_ = 0;
    writeIndex_ = 0;
    queued_ = 0;

    if (!worker_.start(role, kWorkerStackSize, [this] { workerMain(); })) {
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
        submitFailures_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const bool profile = profiling_.load(std::memory_order_relaxed);
    const uint64_t queuedAtUs = profile ? static_cast<uint64_t>(sceKernelGetProcessTimeWide()) : 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_relaxed) || stopping_ || queued_ == kQueueCapacity) {
            submitFailures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (fence != nullptr) {
            fence->add();
        }
        queue_[writeIndex_] = Job{function, context, fence, queuedAtUs};
        writeIndex_ = (writeIndex_ + 1) % kQueueCapacity;
        ++queued_;
        submitted_.fetch_add(1, std::memory_order_relaxed);
        if (profile) AtomicMaxSize(queueHighWater_, queued_);
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

        const bool profile = profiling_.load(std::memory_order_relaxed);
        const uint64_t beginUs = profile ? static_cast<uint64_t>(sceKernelGetProcessTimeWide()) : 0;
        if (profile && job.queuedAtUs != 0) {
            const uint64_t waitUs = beginUs >= job.queuedAtUs ? beginUs - job.queuedAtUs : 0;
            queueWaitUs_.fetch_add(waitUs, std::memory_order_relaxed);
            AtomicMax(queueWaitMaxUs_, waitUs);
        }
        job.function(job.context);
        if (profile) {
            const uint64_t executeUs = static_cast<uint64_t>(sceKernelGetProcessTimeWide()) - beginUs;
            executeUs_.fetch_add(executeUs, std::memory_order_relaxed);
            AtomicMax(executeMaxUs_, executeUs);
        }
        executed_.fetch_add(1, std::memory_order_relaxed);
        if (job.fence != nullptr) {
            job.fence->signal();
        }
    }
}

HostJobStats HostJobSystem::takeStats() noexcept {
    HostJobStats out{};
    out.submitted = submitted_.exchange(0, std::memory_order_acq_rel);
    out.executed = executed_.exchange(0, std::memory_order_acq_rel);
    out.queueWaitUs = queueWaitUs_.exchange(0, std::memory_order_acq_rel);
    out.queueWaitMaxUs = queueWaitMaxUs_.exchange(0, std::memory_order_acq_rel);
    out.executeUs = executeUs_.exchange(0, std::memory_order_acq_rel);
    out.executeMaxUs = executeMaxUs_.exchange(0, std::memory_order_acq_rel);
    out.submitFailures = submitFailures_.exchange(0, std::memory_order_acq_rel);
    out.queueHighWater = queueHighWater_.exchange(0, std::memory_order_acq_rel);
    return out;
}

HostJobSystem& BackgroundJobs() noexcept {
    static HostJobSystem jobs;
    return jobs;
}

HostJobSystem& PrefetchJobs() noexcept {
    static HostJobSystem jobs;
    return jobs;
}

} // namespace WiiCompiledVita
