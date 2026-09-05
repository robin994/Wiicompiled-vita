#include "guest_stall_watchdog.h"

#include "abi_bridge.h"
#include "fiber_manager.h"
#include "memory.h"
#include "runtime_log.h"

#include <atomic>
#include <chrono>

#ifndef MKW_VITA_PERF_LOG
#define MKW_VITA_PERF_LOG 1
#endif

#if defined(MKW_TARGET_VITA)
#include <psp2/kernel/processmgr.h>
namespace {

uint64_t NowUs() noexcept
{
    return static_cast<uint64_t>(sceKernelGetProcessTimeWide());
}

struct LiveOwnership {
    std::atomic<uint64_t> switchSeq{0};
    std::atomic<uint32_t> switchInProgress{0};
    std::atomic<uint32_t> activeFrom{0};
    std::atomic<uint32_t> activeTo{0};
    std::atomic<uint32_t> activeFromEntry{0};
    std::atomic<uint32_t> activeToEntry{0};
    std::atomic<uint64_t> switchBeginUs{0};
    std::atomic<uint64_t> lastReturnUs{0};
    std::atomic<uint64_t> schedulerSelectTotal{0};
    std::atomic<uint64_t> schedulerIdleTotal{0};
    std::atomic<uint64_t> viPollTotal{0};
    std::atomic<uint64_t> lastSchedulerUs{0};
    std::atomic<uint64_t> lastIdleUs{0};
    std::atomic<uint64_t> lastViPollUs{0};
    std::atomic<uint64_t> idleAudioPendingTotal{0};
    std::atomic<uint64_t> idleViAfterAudioTotal{0};
    std::atomic<uint64_t> idleBreakAfterServiceTotal{0};
};
LiveOwnership g_live;

constexpr uint32_t kOSCurrentContextAddr = 0x800000D4u;
constexpr uint32_t kOSRunningContextAddr = 0x800000E4u;
constexpr uint32_t kSchedulerPendingFlagAddr = 0x80386920u;
constexpr uint32_t kSchedulerIdleFlagAddr = 0x80386918u;
constexpr uint32_t kRflManagerPtrAddr = 0x80386298u;
constexpr uint32_t kRflWorkingOffset = 0x1B34u;
constexpr uint32_t kThreadStateOffset = 0x2C8u;
constexpr uint32_t kThreadSuspendOffset = 0x2CCu;
constexpr uint32_t kThreadPriorityOffset = 0x2D0u;
constexpr uint32_t kThreadWaitQueueOffset = 0x2DCu;
constexpr uint32_t kThreadQueueLinkOffset = 0x2E0u;
constexpr uint32_t kThreadJoinQueueOffset = 0x2E8u;
constexpr uint32_t kThreadListLinkOffset = 0x2FCu;
constexpr uint32_t kThreadListHeadAddr = 0x800000DCu;

struct AtomicSnapshot {
    std::atomic<uint64_t> serial{0};
    std::atomic<uint64_t> progressUs{0};
    std::atomic<uint64_t> intervalUs{0};
    std::atomic<uint32_t> currentGuestThread{0};
    std::atomic<uint32_t> osCurrentContext{0};
    std::atomic<uint32_t> osRunningContext{0};
    std::atomic<uint32_t> threadState{0};
    std::atomic<uint32_t> threadPriority{0};
    std::atomic<uint32_t> schedulerPendingMask{0};
    std::atomic<uint32_t> schedulerIdleFlag{0};
    std::atomic<uint32_t> translatedPc{0};
    std::atomic<uint32_t> translatedSrr0{0};
    std::atomic<uint32_t> translatedLr{0};
    std::atomic<uint32_t> fiberEntryPoint{0};
    std::atomic<uint32_t> taskThread{0};
    std::atomic<uint32_t> taskJob{0};
    std::atomic<uint32_t> taskCallback{0};
    std::atomic<uint32_t> taskArgument{0};
    std::atomic<uint32_t> movieManager{0};
    std::atomic<uint32_t> movieState{0};
    std::atomic<uint32_t> movieResult{0};
    std::atomic<uint32_t> rflManager{0};
    std::atomic<uint32_t> rflWorking{0};
};

AtomicSnapshot g_snapshot;

uint32_t Read32OrZero(uint32_t address) noexcept
{
    uint32_t value = 0;
    (void)Memory::TryRead32(address, value);
    return value;
}

uint32_t Read16OrZero(uint32_t address) noexcept
{
    try {
        return Memory::Contains(address, sizeof(uint16_t)) ? Memory::Read16(address) : 0u;
    } catch (const Memory::AccessViolation&) {
        return 0;
    }
}

#ifndef MKW_VITA_SCHED_TRACE
#define MKW_VITA_SCHED_TRACE 0
#endif

#if MKW_VITA_SCHED_TRACE
struct SchedTraceEntry {
    uint64_t whenUs = 0;
    uint32_t kind = 0;
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
};

constexpr uint32_t kSchedTraceCapacity = 4096u;
SchedTraceEntry g_schedTrace[kSchedTraceCapacity];
std::atomic<uint32_t> g_schedTraceHead{0};

bool IsNoisyAudioLr(uint32_t lr) noexcept
{
    return lr >= 0x800A0000u && lr < 0x800B4000u;
}

const char* SchedKindName(uint32_t kind) noexcept
{
    switch (kind) {
    case 1: return "sleep";
    case 2: return "wake";
    case 3: return "wake_thr";
    case 4: return "resume";
    case 5: return "suspend";
    case 6: return "send";
    case 7: return "recv_blk";
    case 8: return "reschd_idle";
    case 9: return "io_cb";
    default: return "?";
    }
}
#endif

void DumpGuestThreadTable() noexcept
{
    const uint32_t head = Read32OrZero(kThreadListHeadAddr);
    RT_LOGF(RT_TAG_OS, "guest_thread_table head=0x%08X\n", head);
    uint32_t thread = head;
    for (uint32_t i = 0; i < 48u && thread != 0 && Memory::Contains(thread + kThreadListLinkOffset, 4); ++i) {
        const uint32_t state = Read16OrZero(thread + kThreadStateOffset);
        const uint32_t suspend = Read32OrZero(thread + kThreadSuspendOffset);
        const uint32_t priority = Read32OrZero(thread + kThreadPriorityOffset);
        const uint32_t waitQueue = Read32OrZero(thread + kThreadWaitQueueOffset);
        const uint32_t queueLink = Read32OrZero(thread + kThreadQueueLinkOffset);
        const uint32_t joinQueue = Read32OrZero(thread + kThreadJoinQueueOffset);
        const uint32_t wqHead = waitQueue != 0 ? Read32OrZero(waitQueue) : 0u;
        const uint32_t wqTail = waitQueue != 0 ? Read32OrZero(waitQueue + 4u) : 0u;
        uint32_t fiberEntry = 0;
        const auto* fiber = Fiber::GuestFiberManager::GetFiber(thread);
        if (fiber != nullptr) {
            fiberEntry = fiber->entryPoint;
        }
        RT_LOGF(RT_TAG_OS,
                "guest_thread t=0x%08X state=%u suspend=%d prio=%u wait_q=0x%08X wq=[0x%08X,0x%08X] q_link=0x%08X "
                "join_q=0x%08X fiber=%d entry=0x%08X term=%d\n",
                thread, state, static_cast<int32_t>(suspend), priority, waitQueue, wqHead, wqTail, queueLink,
                joinQueue, fiber != nullptr ? 1 : 0, fiberEntry,
                Fiber::GuestFiberManager::IsTerminated(thread) ? 1 : 0);
        thread = Read32OrZero(thread + kThreadListLinkOffset);
    }

#if MKW_VITA_SCHED_TRACE
    const uint32_t h = g_schedTraceHead.load(std::memory_order_relaxed);
    constexpr uint32_t kDumpLimit = 300u;
    const uint32_t available = h < kSchedTraceCapacity ? h : kSchedTraceCapacity;
    const uint32_t toDump = available < kDumpLimit ? available : kDumpLimit;
    RT_LOGF(RT_TAG_OS, "sched_trace total=%u showing=%u\n", h, toDump);
    const uint32_t start = h - toDump;
    for (uint32_t i = start; i < h; ++i) {
        const SchedTraceEntry& e = g_schedTrace[i % kSchedTraceCapacity];
        RT_LOGF(RT_TAG_OS, "sched_trace t_us=%llu %s a=0x%08X b=0x%08X c=0x%08X\n",
                static_cast<unsigned long long>(e.whenUs), SchedKindName(e.kind), e.a, e.b, e.c);
    }
#endif
}

} // namespace
#endif

void VI_HLE_LogDiagnostics() noexcept;

namespace GuestStallWatchdog {

void RecordFrame(uint64_t serial, uint64_t progressUs, uint64_t intervalUs,
                 const CpuContext* cpu) noexcept
{
#if defined(MKW_TARGET_VITA)
    const uint32_t currentGuestThread = Fiber::GuestFiberManager::GetCurrentGuestThread();
    const uint32_t osCurrentContext = Read32OrZero(kOSCurrentContextAddr);
    const uint32_t osRunningContext = Read32OrZero(kOSRunningContextAddr);
    const uint32_t threadState = osRunningContext != 0
        ? Read16OrZero(osRunningContext + kThreadStateOffset) : 0u;
    const uint32_t threadPriority = osRunningContext != 0
        ? Read32OrZero(osRunningContext + kThreadPriorityOffset) : 0u;
    const uint32_t rflManager = Read32OrZero(kRflManagerPtrAddr);

    g_snapshot.serial.store(static_cast<uint64_t>(serial), std::memory_order_relaxed);
    g_snapshot.intervalUs.store(intervalUs, std::memory_order_relaxed);
    g_snapshot.currentGuestThread.store(currentGuestThread, std::memory_order_relaxed);
    g_snapshot.osCurrentContext.store(osCurrentContext, std::memory_order_relaxed);
    g_snapshot.osRunningContext.store(osRunningContext, std::memory_order_relaxed);
    g_snapshot.threadState.store(threadState, std::memory_order_relaxed);
    g_snapshot.threadPriority.store(threadPriority, std::memory_order_relaxed);
    g_snapshot.schedulerPendingMask.store(Read32OrZero(kSchedulerPendingFlagAddr),
                                          std::memory_order_relaxed);
    g_snapshot.schedulerIdleFlag.store(Read32OrZero(kSchedulerIdleFlagAddr),
                                       std::memory_order_relaxed);
    g_snapshot.fiberEntryPoint.store(
        Fiber::GuestFiberManager::GetCurrentGuestThreadEntryPoint(), std::memory_order_relaxed);
    g_snapshot.rflManager.store(rflManager, std::memory_order_relaxed);
    g_snapshot.rflWorking.store(rflManager != 0
        ? Read32OrZero(rflManager + kRflWorkingOffset) : 0u, std::memory_order_relaxed);

    if (cpu != nullptr) {
        g_snapshot.translatedPc.store(cpu->pc, std::memory_order_relaxed);
        g_snapshot.translatedSrr0.store(cpu->srr0, std::memory_order_relaxed);
        g_snapshot.translatedLr.store(cpu->lr, std::memory_order_relaxed);
    }
    g_snapshot.progressUs.store(progressUs, std::memory_order_release);
#else
    (void)serial;
    (void)progressUs;
    (void)intervalUs;
    (void)cpu;
#endif
}

void RecordTaskThread(uint32_t taskThread, uint32_t job, uint32_t callback,
                      uint32_t argument) noexcept
{
#if defined(MKW_TARGET_VITA)
    g_snapshot.taskThread.store(taskThread, std::memory_order_relaxed);
    g_snapshot.taskJob.store(job, std::memory_order_relaxed);
    g_snapshot.taskCallback.store(callback, std::memory_order_relaxed);
    g_snapshot.taskArgument.store(argument, std::memory_order_relaxed);
#else
    (void)taskThread;
    (void)job;
    (void)callback;
    (void)argument;
#endif
}

void TraceSchedEvent(uint32_t kind, uint32_t a, uint32_t b, uint32_t c) noexcept
{
#if defined(MKW_TARGET_VITA) && MKW_VITA_SCHED_TRACE
    if ((kind == 1u || kind == 7u) && IsNoisyAudioLr(c)) {
        return;
    }
    const uint32_t slot = g_schedTraceHead.fetch_add(1, std::memory_order_relaxed);
    SchedTraceEntry& e = g_schedTrace[slot % kSchedTraceCapacity];
    e.whenUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    e.kind = kind;
    e.a = a;
    e.b = b;
    e.c = c;
#else
    (void)kind;
    (void)a;
    (void)b;
    (void)c;
#endif
}

void RecordFiberSwitchBegin(uint32_t fromThread, uint32_t toThread,
                            uint32_t fromEntry, uint32_t toEntry) noexcept
{
#if defined(MKW_TARGET_VITA)
    g_live.activeFrom.store(fromThread, std::memory_order_relaxed);
    g_live.activeTo.store(toThread, std::memory_order_relaxed);
    g_live.activeFromEntry.store(fromEntry, std::memory_order_relaxed);
    g_live.activeToEntry.store(toEntry, std::memory_order_relaxed);
    g_live.switchBeginUs.store(NowUs(), std::memory_order_relaxed);
    g_live.switchSeq.fetch_add(1, std::memory_order_relaxed);
    g_live.switchInProgress.store(1, std::memory_order_release);
#else
    (void)fromThread; (void)toThread; (void)fromEntry; (void)toEntry;
#endif
}

void RecordFiberSwitchEnd() noexcept
{
#if defined(MKW_TARGET_VITA)
    g_live.switchInProgress.store(0, std::memory_order_release);
    g_live.lastReturnUs.store(NowUs(), std::memory_order_relaxed);
#endif
}

void RecordSchedulerTick(uint32_t kind) noexcept
{
#if defined(MKW_TARGET_VITA)
    switch (kind) {
    case 1u:
        g_live.schedulerSelectTotal.fetch_add(1, std::memory_order_relaxed);
        g_live.lastSchedulerUs.store(NowUs(), std::memory_order_relaxed);
        break;
    case 2u:
        g_live.schedulerIdleTotal.fetch_add(1, std::memory_order_relaxed);
        g_live.lastIdleUs.store(NowUs(), std::memory_order_relaxed);
        break;
    case 3u:
        g_live.viPollTotal.fetch_add(1, std::memory_order_relaxed);
        g_live.lastViPollUs.store(NowUs(), std::memory_order_relaxed);
        break;
    case 4u:
        g_live.idleAudioPendingTotal.fetch_add(1, std::memory_order_relaxed);
        break;
    case 5u:
        g_live.idleViAfterAudioTotal.fetch_add(1, std::memory_order_relaxed);
        break;
    case 6u:
        g_live.idleBreakAfterServiceTotal.fetch_add(1, std::memory_order_relaxed);
        break;
    default:
        break;
    }
#else
    (void)kind;
#endif
}

void RecordMovieManager(uint32_t manager, uint32_t state,
                        uint32_t result) noexcept
{
#if defined(MKW_TARGET_VITA)
    g_snapshot.movieManager.store(manager, std::memory_order_relaxed);
    g_snapshot.movieState.store(state, std::memory_order_relaxed);
    g_snapshot.movieResult.store(result, std::memory_order_relaxed);
#else
    (void)manager;
    (void)state;
    (void)result;
#endif
}

void Poll(uint64_t nowUs) noexcept
{
#if defined(MKW_TARGET_VITA)
    const uint64_t progressUs = g_snapshot.progressUs.load(std::memory_order_acquire);
    if (progressUs == 0 || nowUs <= progressUs || nowUs - progressUs < 1000000u) {
        return;
    }

    static uint64_t lastReportUs = 0;
    if (lastReportUs != 0 && nowUs - lastReportUs < 1000000u) {
        return;
    }
#if !MKW_VITA_PERF_LOG
    // A stable guest stall used to write the same large watchdog record once a
    // second. In performance mode retain the first/new location immediately,
    // then sample an unchanged stall only once every ten seconds.
    const uint32_t pc = g_snapshot.translatedPc.load(std::memory_order_relaxed);
    const uint32_t lr = g_snapshot.translatedLr.load(std::memory_order_relaxed);
    const uint32_t task = g_snapshot.taskCallback.load(std::memory_order_relaxed);
    const uint32_t rfl = g_snapshot.rflManager.load(std::memory_order_relaxed);
    const uint64_t signature = (static_cast<uint64_t>(pc) << 32) ^
                               (static_cast<uint64_t>(lr) << 1) ^
                               (static_cast<uint64_t>(task) << 17) ^ rfl;
    static uint64_t lastSignature = 0;
    static uint64_t lastSignatureReportUs = 0;
    if (signature == lastSignature && lastSignatureReportUs != 0 &&
        nowUs - lastSignatureReportUs < 10000000u) {
        return;
    }
    lastSignature = signature;
    lastSignatureReportUs = nowUs;
#endif
    lastReportUs = nowUs;

    RT_LOGF(RT_TAG_OS,
            "guest_watchdog_stall serial=%llu stalled_us=%llu interval_us=%llu "
            "current_guest=0x%08X os_current=0x%08X os_running=0x%08X "
            "thread_state=%u priority=%u pending=0x%08X idle=%u "
            "pc=0x%08X srr0=0x%08X lr=0x%08X fiber_entry=0x%08X "
            "task_thread=0x%08X task_job=0x%08X task_cb=0x%08X task_arg=0x%08X "
            "movie_manager=0x%08X movie_state=0x%08X movie_result=0x%08X "
            "rfl_manager=0x%08X rfl_working=%u\n",
            static_cast<unsigned long long>(g_snapshot.serial.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(nowUs - progressUs),
            static_cast<unsigned long long>(g_snapshot.intervalUs.load(std::memory_order_relaxed)),
            g_snapshot.currentGuestThread.load(std::memory_order_relaxed),
            g_snapshot.osCurrentContext.load(std::memory_order_relaxed),
            g_snapshot.osRunningContext.load(std::memory_order_relaxed),
            g_snapshot.threadState.load(std::memory_order_relaxed),
            g_snapshot.threadPriority.load(std::memory_order_relaxed),
            g_snapshot.schedulerPendingMask.load(std::memory_order_relaxed),
            g_snapshot.schedulerIdleFlag.load(std::memory_order_relaxed),
            g_snapshot.translatedPc.load(std::memory_order_relaxed),
            g_snapshot.translatedSrr0.load(std::memory_order_relaxed),
            g_snapshot.translatedLr.load(std::memory_order_relaxed),
            g_snapshot.fiberEntryPoint.load(std::memory_order_relaxed),
            g_snapshot.taskThread.load(std::memory_order_relaxed),
            g_snapshot.taskJob.load(std::memory_order_relaxed),
            g_snapshot.taskCallback.load(std::memory_order_relaxed),
            g_snapshot.taskArgument.load(std::memory_order_relaxed),
            g_snapshot.movieManager.load(std::memory_order_relaxed),
            g_snapshot.movieState.load(std::memory_order_relaxed),
            g_snapshot.movieResult.load(std::memory_order_relaxed),
            g_snapshot.rflManager.load(std::memory_order_relaxed),
            g_snapshot.rflWorking.load(std::memory_order_relaxed));

    const uint32_t liveRunning = Read32OrZero(kOSRunningContextAddr);
    const uint32_t liveCurrent = Read32OrZero(kOSCurrentContextAddr);
    const uint32_t livePending = Read32OrZero(kSchedulerPendingFlagAddr);
    const uint32_t runState = liveRunning != 0 ? Read16OrZero(liveRunning + kThreadStateOffset) : 0u;
    const uint32_t runWaitQ = liveRunning != 0 ? Read32OrZero(liveRunning + kThreadWaitQueueOffset) : 0u;
    const uint32_t runPrio = liveRunning != 0 ? Read32OrZero(liveRunning + kThreadPriorityOffset) : 0u;
    const uint32_t curState = liveCurrent != 0 ? Read16OrZero(liveCurrent + kThreadStateOffset) : 0u;
    RT_LOGF(RT_TAG_OS,
            "guest_watchdog_live os_running=0x%08X run_state=%u run_prio=%u run_wait_q=0x%08X "
            "os_current=0x%08X cur_state=%u pending=0x%08X\n",
            liveRunning, runState, runPrio, runWaitQ, liveCurrent, curState, livePending);

    const uint64_t swBegin = g_live.switchBeginUs.load(std::memory_order_relaxed);
    RT_LOGF(RT_TAG_OS,
            "guest_live switch_seq=%llu switching=%u from=0x%08X to=0x%08X from_entry=0x%08X to_entry=0x%08X "
            "slice_age_us=%lld last_return_age_us=%lld\n",
            static_cast<unsigned long long>(g_live.switchSeq.load(std::memory_order_relaxed)),
            g_live.switchInProgress.load(std::memory_order_acquire),
            g_live.activeFrom.load(std::memory_order_relaxed),
            g_live.activeTo.load(std::memory_order_relaxed),
            g_live.activeFromEntry.load(std::memory_order_relaxed),
            g_live.activeToEntry.load(std::memory_order_relaxed),
            static_cast<long long>(swBegin != 0 ? static_cast<int64_t>(nowUs - swBegin) : -1),
            static_cast<long long>(g_live.lastReturnUs.load(std::memory_order_relaxed) != 0
                ? static_cast<int64_t>(nowUs - g_live.lastReturnUs.load(std::memory_order_relaxed)) : -1));

    const uint64_t lastSched = g_live.lastSchedulerUs.load(std::memory_order_relaxed);
    const uint64_t lastIdle = g_live.lastIdleUs.load(std::memory_order_relaxed);
    const uint64_t lastPoll = g_live.lastViPollUs.load(std::memory_order_relaxed);
    RT_LOGF(RT_TAG_OS,
            "guest_live sched_select_total=%llu sched_idle_total=%llu vi_poll_total=%llu "
            "last_scheduler_age_us=%lld last_idle_age_us=%lld last_vi_poll_age_us=%lld "
            "idle_audio_pending_total=%llu idle_vi_after_audio_total=%llu idle_break_after_service_total=%llu\n",
            static_cast<unsigned long long>(g_live.schedulerSelectTotal.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_live.schedulerIdleTotal.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_live.viPollTotal.load(std::memory_order_relaxed)),
            static_cast<long long>(lastSched != 0 ? static_cast<int64_t>(nowUs - lastSched) : -1),
            static_cast<long long>(lastIdle != 0 ? static_cast<int64_t>(nowUs - lastIdle) : -1),
            static_cast<long long>(lastPoll != 0 ? static_cast<int64_t>(nowUs - lastPoll) : -1),
            static_cast<unsigned long long>(g_live.idleAudioPendingTotal.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_live.idleViAfterAudioTotal.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_live.idleBreakAfterServiceTotal.load(std::memory_order_relaxed)));

    VI_HLE_LogDiagnostics();

    static bool s_dumpedThreadTable = false;
    if (!s_dumpedThreadTable) {
        s_dumpedThreadTable = true;
        DumpGuestThreadTable();
    }
#else
    (void)nowUs;
#endif
}

} // namespace GuestStallWatchdog
