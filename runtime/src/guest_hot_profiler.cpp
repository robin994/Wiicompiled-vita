#include "guest_hot_profiler.h"

#if defined(MKW_TARGET_VITA) && MKW_VITA_GUEST_PC_SAMPLER

#include "runtime_log.h"
#include "wiicompiled_vita/host_thread.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

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

std::atomic<uint32_t> g_currentToken{kUnknown};

namespace {

std::atomic_bool g_stop{false};
WiiCompiledVita::HostThread g_samplerThread;

struct PhaseCounters {
    uint64_t unknown = 0;
    uint64_t vi = 0;
    uint64_t alarm = 0;
    uint64_t audio = 0;
    uint64_t hostOther = 0;
    uint64_t guest = 0;
};

void EmitWindow(uint64_t window, uint64_t samples, const PhaseCounters& phases,
                const std::unordered_map<uint32_t, uint64_t>& guestCounts) {
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

    RT_LOGF(RT_TAG_OS,
            "guest_hot_pc window=%llu samples=%llu guest=%llu unknown=%llu wait_vi=%llu wait_alarm=%llu wait_audio=%llu host_other=%llu top=%s\n",
            static_cast<unsigned long long>(window),
            static_cast<unsigned long long>(samples),
            static_cast<unsigned long long>(phases.guest),
            static_cast<unsigned long long>(phases.unknown),
            static_cast<unsigned long long>(phases.vi),
            static_cast<unsigned long long>(phases.alarm),
            static_cast<unsigned long long>(phases.audio),
            static_cast<unsigned long long>(phases.hostOther), top);
}

void SamplerMain() {
    constexpr uint64_t kReportIntervalUs = 5'000'000u;
    std::unordered_map<uint32_t, uint64_t> guestCounts;
    guestCounts.reserve(256);
    PhaseCounters phases{};
    uint64_t samples = 0;
    uint64_t window = 0;
    uint64_t windowBegin = static_cast<uint64_t>(sceKernelGetProcessTimeWide());

    while (!g_stop.load(std::memory_order_acquire)) {
        const uint32_t token = g_currentToken.load(std::memory_order_relaxed);
        ++samples;
        switch (token) {
        case kUnknown: ++phases.unknown; break;
        case kPhaseWaitVi: ++phases.vi; break;
        case kPhaseWaitAlarm: ++phases.alarm; break;
        case kPhaseWaitAudio: ++phases.audio; break;
        case kPhaseHostOther: ++phases.hostOther; break;
        default:
            ++phases.guest;
            ++guestCounts[token];
            break;
        }

        const uint64_t now = static_cast<uint64_t>(sceKernelGetProcessTimeWide());
        if (now - windowBegin >= kReportIntervalUs) {
            EmitWindow(++window, samples, phases, guestCounts);
            samples = 0;
            phases = {};
            guestCounts.clear();
            windowBegin = now;
        }
        sceKernelDelayThread(std::max<uint32_t>(1000u, MKW_VITA_GUEST_PC_SAMPLE_US));
    }

    if (samples != 0) EmitWindow(++window, samples, phases, guestCounts);
}

} // namespace

bool Start() noexcept {
    if (g_samplerThread.joinable()) return true;
    g_stop.store(false, std::memory_order_release);
    g_currentToken.store(kUnknown, std::memory_order_relaxed);
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
