#include "abi_bridge.h"
#include "fiber_manager.h"
#include "hle_stubs.h"
#include "hle/os/os_internal.h"

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

void OS_HLE_WakeupThreadNoReschedule(CpuContext*, uint32_t);

// Run the real SelectThread and wake/run-queue code. Replace only the host
// fibers, memory storage and interrupt devices; audio wakes a higher-priority
// thread on every idle pass, while VI periodically wakes the frame producer.
namespace {
constexpr uint32_t kSound = 0x80010000u, kFrame = 0x80011000u;
constexpr uint32_t kAudioWait = 0x80012000u, kVideoWait = 0x80012008u;
std::unordered_map<uint32_t, uint8_t> bytes;
CpuContext cpu{};
uint32_t selected = 0;
unsigned audioPolls = 0, viPolls = 0, frames = 0;
bool interrupts = false, wakeFromTimer = false;

void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
void Park(uint32_t thread, uint32_t queue, uint32_t priority) {
    Memory::Write16(thread + 0x1A2u, 0x0002u);
    Memory::Write16(thread + kThreadStateOffset, kThreadStateWaiting);
    Memory::Write32(thread + kThreadPriorityOffset, priority);
    Memory::Write32(thread + kThreadBasePriorityOffset, priority);
    Memory::Write32(thread + kThreadSuspendOffset, 0);
    Memory::Write32(thread + kThreadNextOffset, 0);
    Memory::Write32(thread + kThreadPrevOffset, 0);
    Memory::Write32(thread + kThreadQueueOffset, queue);
    Memory::Write32(queue, thread);
    Memory::Write32(queue + 4, thread);
}
void WakeAudio() {
    cpu.gpr[3] = kAudioWait;
    OSWakeupThread_HLE_801aaaa4(&cpu);
}
void Scenario(bool timerWake) {
    bytes.clear(); cpu = {}; selected = kFrame;
    audioPolls = viPolls = frames = 0; wakeFromTimer = timerWake;
    Park(kSound, kAudioWait, 4); Park(kFrame, kVideoWait, 16);
    for (unsigned n = 0; n < 512; ++n) {
        const uint32_t previous = selected;
        Memory::Write32(kOSRunningContextAddr, previous);
        Memory::Write32(kOSCurrentContextAddr, previous);
        if (previous == kSound) Park(kSound, kAudioWait, 4);
        else Park(kFrame, kVideoWait, 16);
        cpu.gpr[3] = 0;
        SelectThread_801a9c08(&cpu);
        Require(selected == kSound || selected == kFrame, "scheduler chose an unknown thread");
        if (selected == kFrame) ++frames;
    }
    std::printf("idle retrace: timer=%u audio=%u vi=%u frames=%u\n",
                timerWake, audioPolls, viPolls, frames);
    Require(viPolls > 0 && frames >= 100, "audio/timer wakeups starved VI and frame production");
    Require(audioPolls >= 300, "VI recovery stopped servicing audio");
    Require(Memory::Read32(kSchedulerIdleFlagAddr) == 0, "scheduler disable count leaked");
}
}

namespace GuestFlat { wiicompiled::RamMirrors g_vitaRamMirrors{}; }
namespace RecompMod {
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
}
namespace MemoryInline {
PageEntry g_pageTable[kPageCount]{};
uintptr_t g_fullReadablePageBias[kPageCount]{}, g_fullWritablePageBias[kPageCount]{};
const SparseWritablePageTable* g_sparseWritablePageTables[kPageCount]{};
uint8_t g_deferredReadCoveredPages[kPageCount]{};
uint16_t Read16Slow(uint32_t a) { return (uint16_t(bytes[a]) << 8) | bytes[a + 1]; }
uint32_t Read32Slow(uint32_t a) { return (uint32_t(Read16Slow(a)) << 16) | Read16Slow(a + 2); }
void Write16Slow(uint32_t a, uint16_t v) { bytes[a] = v >> 8; bytes[a + 1] = v; }
void Write32Slow(uint32_t a, uint32_t v) { Write16Slow(a, v >> 16); Write16Slow(a + 2, v); }
}
CpuContext& GetPersistentCpuContext() { return cpu; }
void TranslatedFunctionRegistry::Register(TranslatedFunctionInfo) {}
const TranslatedFunctionInfo* TranslatedFunctionRegistry::FindByAddressPtrSlow(uint32_t) { return nullptr; }
const RawDispatchRecord* TranslatedFunctionRegistry::FindRawByAddressPtrSlow(uint32_t) { return nullptr; }
namespace RuntimeCrash {
[[noreturn]] void FatalMissingGuestTarget(uint32_t, CpuContext*) noexcept { std::abort(); }
}
extern "C" uint32_t PPC_Cntlzw(uint32_t value) { return value ? __builtin_clz(value) : 32u; }
extern "C" int32_t OS__DisableInterrupts_801a65ac() { bool was = interrupts; interrupts = false; return was; }
extern "C" int32_t OS__EnableInterrupts_801a65c0() { bool was = interrupts; interrupts = true; return was; }
extern "C" int32_t OS__RestoreInterrupts_801a65d4(int32_t level) { bool was = interrupts; interrupts = level != 0; return was; }
extern "C" void OS__ClearContext_801a2098(uint32_t) {}
extern "C" void OS__SetCurrentContext_801a1e70(uint32_t t) { Memory::Write32(kOSCurrentContextAddr, t); }
extern "C" [[noreturn]] void OS__LoadContext_801a1f58(CpuContext*) { std::abort(); }
extern "C" void OSSleepThread_HLE_801aa9b8(CpuContext*) { std::abort(); }
extern "C" void func_801A1ED8(CpuContext*) {}
namespace OsHleInternal {
bool ProcessSleepTimers(CpuContext*) {
    if (wakeFromTimer && Memory::Read32(kOSRunningContextAddr) == 0) WakeAudio();
    return wakeFromTimer;
}
bool ProcessAlarmQueue(CpuContext*, int) { return false; }
void RemoveThreadFromQueue(uint32_t) { std::abort(); }
int32_t ComputeThreadEffectivePriority(uint32_t) { std::abort(); }
uint32_t SetThreadEffectivePriority(uint32_t, int32_t) { std::abort(); }
}
namespace Fiber {
bool GuestFiberManager::IsInitialized() { return true; }
bool GuestFiberManager::IsTerminated(uint32_t) { return false; }
bool GuestFiberManager::HasFiber(uint32_t) { return true; }
bool GuestFiberManager::RegisterMainThreadAsFiber(uint32_t, CpuContext*) { return true; }
uint32_t GuestFiberManager::GetCurrentGuestThread() { return selected; }
void GuestFiberManager::SwitchToThread(uint32_t thread, CpuContext*) { selected = thread; }
void GuestFiberManager::ResumeGuestThread(uint32_t) {}
void GuestFiberManager::ProcessTimerEvents(CpuContext*) {}
}
void Audio_HLE_Poll(CpuContext*) { ++audioPolls; WakeAudio(); }
bool VI_HLE_IsAdvancingRetrace() { return false; }
void VI_HLE_ProcessRetracesDeferred(int budget) {
    Require(interrupts && budget > 0, "idle VI poll ran with interrupts disabled");
    if (++viPolls % 4 == 0) OS_HLE_WakeupThreadNoReschedule(&cpu, kVideoWait);
}
void VI_HLE_PollRetrace(CpuContext*) { VI_HLE_ProcessRetracesDeferred(8); }
void VI_HLE_WaitForNextRetracePoll() { std::abort(); }

int main() {
    Scenario(false);
    Scenario(true);
}
