#include "game_diagnostics.hpp"
#include "game_time_trial.hpp"
#include "native_call_profile.h"
#include "memory.h"
#include "aurora_vita_backend.hpp"
#if WIC_PRECOMPILED_ASSETS
#include "gfx/vita_native_assets.hpp"
extern "C" void WicReportNativeAudio(uint64_t frame) noexcept;
#endif
#if defined(WIC_CPU_DIAGNOSTICS)
#include "game_cpu_diagnostics.hpp"
#endif
#include <psp2/kernel/processmgr.h>
#include <cstdio>

namespace aurora::gx::fifo { uint64_t process_time_total_us(); }

namespace {
void LogRflState(uint64_t frame) {
    // PAL RMCP01: the same manager pointer/working field used by RFLiIsWorking
    // HLE. Flags/reason offsets come from translated RFLGetAsyncStatus.
    constexpr uint32_t managerPtr = 0x80386298u;
    try {
        const uint32_t manager = Memory::Read32(managerPtr);
        if (!manager || !Memory::Contains(manager, 0x1b44)) {
            std::fprintf(stderr, "[rfl-health] frame=%llu manager=0x%08x readable=0\n",
                static_cast<unsigned long long>(frame), manager);
            return;
        }
        const auto working = Memory::Read32(manager + 0x1b34);
        const auto flags = Memory::Read8(manager + 0x1b3c);
        const auto reason = Memory::Read32(manager + 0x1b40);
        std::fprintf(stderr, "[rfl-health] frame=%llu manager=0x%08x readable=1 working=%u flags=0x%02x reason=%u\n",
            static_cast<unsigned long long>(frame), manager, working, unsigned(flags), reason);
    } catch (const Memory::AccessViolation&) {
        std::fprintf(stderr, "[rfl-health] frame=%llu snapshot_failed=1\n",
            static_cast<unsigned long long>(frame));
    }
}
}

void WicLogFrameDiagnostics(uint64_t frame, uint64_t draws, bool capture) {
    const bool profiling=HostContext::NativeCallProfilingEnabled();
    static uint64_t start = 0, frames = 0, drawnFrames = 0, captures = 0, previousFifo = 0;
    static HostContext::NativeCallProfile previous{};
    const uint64_t now = sceKernelGetProcessTimeWide();
    if (!start) {
        start = now;
        previous = HostContext::SnapshotNativeCallProfile();
        previousFifo = aurora::gx::fifo::process_time_total_us();
        return; // Count only frames completed after this window's boundary.
    }
    ++frames;
    if (draws) ++drawnFrames;
    if (capture) ++captures;
    const uint64_t elapsed = now - start;
    // Slow menus need a report within ten seconds, rather than waiting five
    // minutes for 300 frames. Faster runs report at most once per two seconds.
    if (elapsed < 10000000 && (frames < 60 || elapsed < 2000000)) return;
    wiicompiled::vita::time_trial::log_progress(frame);
    if(!profiling) {
        std::fprintf(stderr,"[runtime-cadence] frame=%llu frames=%llu wall_us=%llu present_hz=%.3f attribution=0\n",
            static_cast<unsigned long long>(frame),static_cast<unsigned long long>(frames),
            static_cast<unsigned long long>(elapsed),double(frames)*1000000./double(elapsed));
        start=now;frames=drawnFrames=captures=0;
        return;
    }
    const auto current = HostContext::SnapshotNativeCallProfile();
    const uint64_t request = current.requestUs - previous.requestUs;
    const uint64_t callback = current.callbackUs - previous.callbackUs;
    const auto fifo = aurora::gx::fifo::process_time_total_us();
    std::fprintf(stderr,
        "[runtime-profile] frame=%llu frames=%llu drawn_frames=%llu wall_us=%llu "
        "present_hz=%.3f drawn_hz=%.3f captures=%llu native_requests=%llu stack_dispatches=%llu "
        "native_request_us=%llu native_callback_us=%llu native_transition_us=%llu "
        "fifo_us=%llu waits_vi=%llu/%llu waits_gx=%llu/%llu waits_gki=%llu/%llu "
        "waits_panic=%llu/%llu nested_times=1 diagnostic=1\n",
        static_cast<unsigned long long>(frame), static_cast<unsigned long long>(frames),
        static_cast<unsigned long long>(drawnFrames), static_cast<unsigned long long>(elapsed),
        double(frames) * 1000000. / double(elapsed), double(drawnFrames) * 1000000. / double(elapsed),
        static_cast<unsigned long long>(captures),
        static_cast<unsigned long long>(current.requests - previous.requests),
        static_cast<unsigned long long>(current.stackDispatches - previous.stackDispatches),
        static_cast<unsigned long long>(request), static_cast<unsigned long long>(callback),
        static_cast<unsigned long long>(request > callback ? request - callback : 0),
        static_cast<unsigned long long>(fifo - previousFifo),
        static_cast<unsigned long long>(current.waitCalls[0] - previous.waitCalls[0]),
        static_cast<unsigned long long>(current.waitUs[0] - previous.waitUs[0]),
        static_cast<unsigned long long>(current.waitCalls[1] - previous.waitCalls[1]),
        static_cast<unsigned long long>(current.waitUs[1] - previous.waitUs[1]),
        static_cast<unsigned long long>(current.waitCalls[2] - previous.waitCalls[2]),
        static_cast<unsigned long long>(current.waitUs[2] - previous.waitUs[2]),
        static_cast<unsigned long long>(current.waitCalls[3] - previous.waitCalls[3]),
        static_cast<unsigned long long>(current.waitUs[3] - previous.waitUs[3]));
    LogRflState(frame);
    const auto completed=aurora::vita::completed_performance_snapshot();
    const bool fresh=completed.completedFrame && completed.frameIndex==frame;
    const auto& resident=completed.textureSnapshots;
    std::fprintf(stderr,"[resident-assets] frame=%llu completed=%u snapshot_frame=%llu texture_hits_total=%llu texture_misses_total=%llu texture_uploads_total=%llu texture_evictions_total=%llu texture_entries=%u texture_gpu_bytes=%u texture_source_bytes=%u texture_budget=%u geometry_hits_total=%llu geometry_misses_total=%llu geometry_entries=%u geometry_bytes=%u\n",
        static_cast<unsigned long long>(frame),unsigned(fresh),
        static_cast<unsigned long long>(completed.frameIndex),
        static_cast<unsigned long long>(resident.hits),static_cast<unsigned long long>(resident.misses),
        static_cast<unsigned long long>(resident.uploads),static_cast<unsigned long long>(resident.evictions),
        unsigned(resident.entries),unsigned(resident.gpuBytes),unsigned(resident.sourceBytes),unsigned(resident.budget),
        static_cast<unsigned long long>(completed.staticGeometryHits),
        static_cast<unsigned long long>(completed.staticGeometryMisses),
        unsigned(completed.staticGeometryEntries),unsigned(completed.staticGeometryBytes));
#if WIC_PRECOMPILED_ASSETS
    const auto assets=aurora::vita::gfx::native_asset_stats();
    std::fprintf(stderr,"[native-assets] frame=%llu texture_hits_total=%llu geometry_hits_total=%llu misses_total=%llu rejected_total=%llu oom_fallback_total=%llu\n",
        static_cast<unsigned long long>(frame),static_cast<unsigned long long>(assets.textureHits),
        static_cast<unsigned long long>(assets.geometryHits),static_cast<unsigned long long>(assets.misses),
        static_cast<unsigned long long>(assets.rejected),static_cast<unsigned long long>(assets.allocationFailures));
    WicReportNativeAudio(frame);
#endif
#if defined(WIC_CPU_DIAGNOSTICS)
    WicLogCpuDiagnostics(frame);
#endif
    // Include this bounded logging cost in the following diagnostic window.
    start = now; frames = drawnFrames = captures = 0;
    previous = current; previousFifo = fifo;
}
