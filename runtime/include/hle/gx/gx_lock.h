#pragma once

#include <atomic>
#include <mutex>

namespace GxSync {

#if defined(MKW_TARGET_VITA)
// Vita's libstdc++ mutex path can surface std::system_error from cache-maintenance
// callbacks. These sections are short bookkeeping updates, so use a non-throwing
// lock on Vita and keep std::mutex everywhere else.
class Mutex {
public:
    Mutex() noexcept = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // Keep the loop observable to the compiler without introducing a
            // scheduler/syscall dependency into cache-maintenance hot paths.
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
    }

    void unlock() noexcept {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
#else
using Mutex = std::mutex;
#endif

} // namespace GxSync
