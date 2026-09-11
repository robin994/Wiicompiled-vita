#pragma once

#if !defined(MKW_TARGET_VITA)
#error "wiicompiled_vita/host_jobs.h is only for the PS Vita target"
#endif

#include "wiicompiled_vita/host_thread.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace WiiCompiledVita {

using HostJobFunction = void (*)(void* context) noexcept;

struct HostJobStats {
    uint64_t submitted = 0;
    uint64_t executed = 0;
    uint64_t queueWaitUs = 0;
    uint64_t queueWaitMaxUs = 0;
    uint64_t executeUs = 0;
    uint64_t executeMaxUs = 0;
    uint64_t submitFailures = 0;
    size_t queueHighWater = 0;
};

class HostJobFence {
public:
    HostJobFence() = default;
    HostJobFence(const HostJobFence&) = delete;
    HostJobFence& operator=(const HostJobFence&) = delete;

    void wait() noexcept;
    bool waitFor(uint32_t timeoutMicroseconds) noexcept;
    bool complete() const noexcept { return pending_.load(std::memory_order_acquire) == 0; }

private:
    friend class HostJobSystem;

    void add() noexcept { pending_.fetch_add(1, std::memory_order_relaxed); }
    void signal() noexcept;

    std::atomic<uint32_t> pending_{0};
    std::mutex mutex_;
    std::condition_variable condition_;
};

class HostJobSystem {
public:
    HostJobSystem() = default;
    HostJobSystem(const HostJobSystem&) = delete;
    HostJobSystem& operator=(const HostJobSystem&) = delete;
    ~HostJobSystem();

    bool start(HostThreadRole role = HostThreadRole::Background);
    void stop() noexcept;

    bool submit(HostJobFunction function, void* context, HostJobFence* fence = nullptr) noexcept;
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    void setProfiling(bool enabled) noexcept { profiling_.store(enabled, std::memory_order_release); }
    HostJobStats takeStats() noexcept;

    static constexpr size_t kQueueCapacity = 64;
    static constexpr size_t kWorkerStackSize = 96 * 1024;

private:
    struct Job {
        HostJobFunction function = nullptr;
        void* context = nullptr;
        HostJobFence* fence = nullptr;
        uint64_t queuedAtUs = 0;
    };

    void workerMain() noexcept;

    HostThread worker_;
    std::array<Job, kQueueCapacity> queue_{};
    size_t readIndex_ = 0;
    size_t writeIndex_ = 0;
    size_t queued_ = 0;
    bool stopping_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> profiling_{false};
    std::atomic<uint64_t> submitted_{0};
    std::atomic<uint64_t> executed_{0};
    std::atomic<uint64_t> queueWaitUs_{0};
    std::atomic<uint64_t> queueWaitMaxUs_{0};
    std::atomic<uint64_t> executeUs_{0};
    std::atomic<uint64_t> executeMaxUs_{0};
    std::atomic<uint64_t> submitFailures_{0};
    std::atomic<size_t> queueHighWater_{0};
    std::mutex mutex_;
    std::condition_variable wake_;
};

HostJobSystem& BackgroundJobs() noexcept;
// Dedicated low-priority/storage-prefetch lane so large speculative reads cannot
// head-of-line block real asynchronous Wii DVD requests.
HostJobSystem& PrefetchJobs() noexcept;

} // namespace WiiCompiledVita
