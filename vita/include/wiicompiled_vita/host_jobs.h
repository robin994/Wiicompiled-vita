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

    bool start();
    void stop() noexcept;

    bool submit(HostJobFunction function, void* context, HostJobFence* fence = nullptr) noexcept;
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

    static constexpr size_t kQueueCapacity = 64;
    static constexpr size_t kWorkerStackSize = 96 * 1024;

private:
    struct Job {
        HostJobFunction function = nullptr;
        void* context = nullptr;
        HostJobFence* fence = nullptr;
    };

    void workerMain() noexcept;

    HostThread worker_;
    std::array<Job, kQueueCapacity> queue_{};
    size_t readIndex_ = 0;
    size_t writeIndex_ = 0;
    size_t queued_ = 0;
    bool stopping_ = false;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
};

HostJobSystem& BackgroundJobs() noexcept;

} // namespace WiiCompiledVita
