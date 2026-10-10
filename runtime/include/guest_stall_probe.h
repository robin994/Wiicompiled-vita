#pragma once

#include <atomic>
#include <cstdint>

// WiiCompiled Vita diagnostics only. Published from the guest scheduler thread
// and read by the independent audio output thread when no video frame advances.
// No guest-memory accesses, locks, HLE calls or renderer access in the reader.
namespace WicGuestStallProbe {
inline std::atomic<int32_t> hostGameThread{-1};
inline std::atomic<uint64_t> selectCalls{0}, idlePolls{0}, contextSwitches{0};
inline std::atomic<uint64_t> viPolls{0}, viRetraces{0};
inline std::atomic<uint64_t> sleepCalls{0}, wakeCalls{0}, resumeCalls{0};
inline std::atomic<uint64_t> messageWaits{0}, messageReceives{0}, messageSends{0};
inline std::atomic<uint64_t> threadCreates{0}, thpTaskRuns{0}, thpTaskCallbacks{0}, thpTaskCompleted{0};
inline std::atomic<uint32_t> lastRunning{0}, lastSelected{0}, lastSleeping{0};
inline std::atomic<uint32_t> lastWaitQueue{0}, lastMessageQueue{0}, lastCreatedThread{0};
inline std::atomic<uint32_t> lastCreatedEntry{0}, lastResumedThread{0}, lastTaskCallback{0};
// Movie-manager async prepare: 10=open, 11=open returned, 20=video info,
// 30=set buffer, 40=prepare, 41=prepare returned. Task loop: 1=wait message,
// 2=callback, 3=completion callback, 4=post done, 0=idle.
inline std::atomic<uint32_t> taskPhase{0};

struct Snapshot {
    int32_t hostGameThread;
    uint64_t selectCalls, idlePolls, contextSwitches;
    uint64_t viPolls, viRetraces;
    uint64_t sleepCalls, wakeCalls, resumeCalls;
    uint64_t messageWaits, messageReceives, messageSends;
    uint64_t threadCreates, thpTaskRuns, thpTaskCallbacks, thpTaskCompleted;
    uint32_t lastRunning, lastSelected, lastSleeping;
    uint32_t lastWaitQueue, lastMessageQueue, lastCreatedThread;
    uint32_t lastCreatedEntry, lastResumedThread, lastTaskCallback, taskPhase;
};

inline Snapshot snapshot() noexcept {
    using std::memory_order_relaxed;
    return {
        hostGameThread.load(memory_order_relaxed),
        selectCalls.load(memory_order_relaxed), idlePolls.load(memory_order_relaxed),
        contextSwitches.load(memory_order_relaxed),
        viPolls.load(memory_order_relaxed), viRetraces.load(memory_order_relaxed),
        sleepCalls.load(memory_order_relaxed), wakeCalls.load(memory_order_relaxed),
        resumeCalls.load(memory_order_relaxed),
        messageWaits.load(memory_order_relaxed), messageReceives.load(memory_order_relaxed),
        messageSends.load(memory_order_relaxed),
        threadCreates.load(memory_order_relaxed), thpTaskRuns.load(memory_order_relaxed),
        thpTaskCallbacks.load(memory_order_relaxed),
        thpTaskCompleted.load(memory_order_relaxed),
        lastRunning.load(memory_order_relaxed), lastSelected.load(memory_order_relaxed),
        lastSleeping.load(memory_order_relaxed),
        lastWaitQueue.load(memory_order_relaxed), lastMessageQueue.load(memory_order_relaxed),
        lastCreatedThread.load(memory_order_relaxed),
        lastCreatedEntry.load(memory_order_relaxed), lastResumedThread.load(memory_order_relaxed),
        lastTaskCallback.load(memory_order_relaxed),taskPhase.load(memory_order_relaxed)
    };
}
} // namespace WicGuestStallProbe
