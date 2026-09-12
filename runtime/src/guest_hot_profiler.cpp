#include "guest_hot_profiler.h"

// Keep the token symbol linkable even in sampler-off performance builds. Some
// translated cold shards are shared across configurations and may have been built
// with MarkGuestPc enabled; hot P6.38 shards compile the call away entirely.
namespace GuestHotProfiler {
std::atomic<uint32_t> g_currentToken{kUnknown};
}

#if defined(MKW_TARGET_VITA) && MKW_VITA_GUEST_PC_SAMPLER

#include "runtime_log.h"
#include "wiicompiled_vita/host_thread.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef MKW_VITA_GUEST_PC_SAMPLE_US
#define MKW_VITA_GUEST_PC_SAMPLE_US 2000
#endif

namespace GuestHotProfiler {

namespace {

std::atomic_bool g_stop{false};
WiiCompiledVita::HostThread g_samplerThread;
SceUID g_observedGuestThread = -1;

uint64_t GuestRunClocks() noexcept {
#if MKW_VITA_GUEST_ACTIVE_CPU_PROFILE
    if (g_observedGuestThread < 0) return 0;
    SceKernelThreadInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetThreadInfo(g_observedGuestThread, &info) >= 0) {
        return static_cast<uint64_t>(info.runClocks);
    }
#endif
    return 0;
}

struct PhaseCounters {
    uint64_t unknown = 0;
    uint64_t vi = 0;
    uint64_t alarm = 0;
    uint64_t audio = 0;
    uint64_t hostOther = 0;
    uint64_t preScheduler = 0;
    uint64_t preAnimation = 0;
    uint64_t preSceneMatrix = 0;
    uint64_t preMaterialVertex = 0;
    uint64_t guest = 0;
};

void EmitWindow(uint64_t window, uint64_t samples, const PhaseCounters& phases,
                const std::unordered_map<uint32_t, uint64_t>& guestCounts,
                uint64_t wallUs, uint64_t runClockDelta) {
    std::vector<std::pair<uint32_t, uint64_t>> hot;
    hot.reserve(guestCounts.size());
    for (const auto& entry : guestCounts) hot.push_back(entry);
    const size_t topCount = std::min<size_t>(8, hot.size());
    std::partial_sort(hot.begin(), hot.begin() + topCount, hot.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });

    char top[384]{};
    size_t used = 0;
    for (size_t i = 0; i < topCount && used < sizeof(top); ++i) {
        const int written = std::snprintf(top + used, sizeof(top) - used,
                                          "%s%08X:%llu",
                                          i == 0 ? "" : ",",
                                          hot[i].first,
                                          static_cast<unsigned long long>(hot[i].second));
        if (written <= 0) break;
        used += std::min<size_t>(static_cast<size_t>(written), sizeof(top) - used - 1);
    }

    const int armMHzRaw = scePowerGetArmClockFrequency();
    const uint64_t activeCpuUs = MKW_VITA_GUEST_ACTIVE_CPU_PROFILE ? runClockDelta : 0;
    const uint32_t activePermille = wallUs != 0
        ? static_cast<uint32_t>(std::min<uint64_t>(1000u, activeCpuUs * 1000u / wallUs)) : 0u;
    RT_LOGF(RT_TAG_OS,
            "guest_hot_pc window=%llu samples=%llu guest=%llu unknown=%llu wait_vi=%llu wait_alarm=%llu wait_audio=%llu host_other=%llu prebegin=scheduler:%llu,animation:%llu,scene:%llu,material_vtx:%llu active_cpu_us=%llu wall_us=%llu active_permille=%u arm_mhz=%d top=%s\n",
            static_cast<unsigned long long>(window),
            static_cast<unsigned long long>(samples),
            static_cast<unsigned long long>(phases.guest),
            static_cast<unsigned long long>(phases.unknown),
            static_cast<unsigned long long>(phases.vi),
            static_cast<unsigned long long>(phases.alarm),
            static_cast<unsigned long long>(phases.audio),
            static_cast<unsigned long long>(phases.hostOther),
            static_cast<unsigned long long>(phases.preScheduler),
            static_cast<unsigned long long>(phases.preAnimation),
            static_cast<unsigned long long>(phases.preSceneMatrix),
            static_cast<unsigned long long>(phases.preMaterialVertex),
            static_cast<unsigned long long>(activeCpuUs),
            static_cast<unsigned long long>(wallUs), activePermille, armMHzRaw, top);
}

void SamplerMain() {
    constexpr uint64_t kReportIntervalUs = 5'000'000u;
    std::unordered_map<uint32_t, uint64_t> guestCounts;
    guestCounts.reserve(256);
    PhaseCounters phases{};
    uint64_t samples = 0;
    uint64_t window = 0;
    uint64_t windowBegin = static_cast<uint64_t>(sceKernelGetProcessTimeWide());
    uint64_t runClockBegin = GuestRunClocks();

    while (!g_stop.load(std::memory_order_acquire)) {
        const uint32_t token = g_currentToken.load(std::memory_order_relaxed);
        ++samples;
        switch (token) {
        case kUnknown: ++phases.unknown; break;
        case kPhaseWaitVi: ++phases.vi; break;
        case kPhaseWaitAlarm: ++phases.alarm; break;
        case kPhaseWaitAudio: ++phases.audio; break;
        case kPhaseHostOther: ++phases.hostOther; break;
        case kPhasePrebeginScheduler: ++phases.preScheduler; break;
        case kPhasePrebeginAnimation: ++phases.preAnimation; break;
        case kPhasePrebeginSceneMatrix: ++phases.preSceneMatrix; break;
        case kPhasePrebeginMaterialVertex: ++phases.preMaterialVertex; break;
        default:
            ++phases.guest;
            ++guestCounts[token];
            break;
        }

        const uint64_t now = static_cast<uint64_t>(sceKernelGetProcessTimeWide());
        if (now - windowBegin >= kReportIntervalUs) {
            const uint64_t runClockEnd = GuestRunClocks();
            const uint64_t runDelta = runClockEnd >= runClockBegin ? runClockEnd - runClockBegin : 0;
            EmitWindow(++window, samples, phases, guestCounts, now - windowBegin, runDelta);
            samples = 0;
            phases = {};
            guestCounts.clear();
            windowBegin = now;
            runClockBegin = runClockEnd;
        }
        sceKernelDelayThread(std::max<uint32_t>(1000u, MKW_VITA_GUEST_PC_SAMPLE_US));
    }

    if (samples != 0) {
        const uint64_t now = static_cast<uint64_t>(sceKernelGetProcessTimeWide());
        const uint64_t runClockEnd = GuestRunClocks();
        EmitWindow(++window, samples, phases, guestCounts, now - windowBegin,
                   runClockEnd >= runClockBegin ? runClockEnd - runClockBegin : 0);
    }
}

} // namespace

bool Start() noexcept {
    if (g_samplerThread.joinable()) return true;
    g_stop.store(false, std::memory_order_release);
    g_currentToken.store(kUnknown, std::memory_order_relaxed);
    g_observedGuestThread = sceKernelGetThreadId();
    const bool started = g_samplerThread.start(
        WiiCompiledVita::HostThreadRole::Background, 64 * 1024, SamplerMain);
    RT_LOGF(RT_TAG_OS, "guest_hot_pc sampler=%s interval_us=%u affinity=helper\n",
            started ? "started" : "failed", static_cast<unsigned>(MKW_VITA_GUEST_PC_SAMPLE_US));
    return started;
}

void Stop() noexcept {
    if (!g_samplerThread.joinable()) return;
    g_stop.store(true, std::memory_order_release);
    g_samplerThread.join();
}

} // namespace GuestHotProfiler

#endif

