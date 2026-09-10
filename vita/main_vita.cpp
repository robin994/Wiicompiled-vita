#include "abi_bridge.h"
#include "fiber_manager.h"
#include "generated/RuntimeConfig.h"
#include "guest_flat_memory.h"
#include "guest_hot_profiler.h"
#include "gx_guest_write.h"
#include "hle_stubs.h"
#include "audio_wait_profile.h"
#include "system_bridge.h"
#include "wiicompiled_vita/gx_backend.h"
#include "wiicompiled_vita/host_thread.h"

#include <aurora/aurora.h>

#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string_view>

namespace {

constexpr const char* kDataDirectory = "ux0:data/wiicompiled-vita";
constexpr const char* kRuntimeLogPath = "ux0:data/wiicompiled-vita/runtime.log";
constexpr uint32_t kGuestStackTop = 0x81700000u;

std::atomic<int> g_runtimeExitCode{EXIT_FAILURE};
std::atomic_bool g_fatalErrorReported{false};

#ifndef MKW_VITA_BUFFERED_LOGGING
#define MKW_VITA_BUFFERED_LOGGING 0
#endif
#ifndef MKW_VITA_LOG_FLUSH_INTERVAL_US
#define MKW_VITA_LOG_FLUSH_INTERVAL_US 250000
#endif
#ifndef MKW_VITA_AUDIO_WAIT_MIN_INTERVAL_US
#define MKW_VITA_AUDIO_WAIT_MIN_INTERVAL_US 0
#endif

#if MKW_VITA_BUFFERED_LOGGING
std::array<char, 64 * 1024> g_stdoutBuffer{};
std::array<char, 64 * 1024> g_stderrBuffer{};
std::atomic_bool g_logFlushStop{false};
WiiCompiledVita::HostThread g_logFlushThread;
#endif

void SetupLogging() noexcept {
    sceIoMkdir(kDataDirectory, 0777);
    if (std::freopen(kRuntimeLogPath, "a", stdout) != nullptr) {
#if MKW_VITA_BUFFERED_LOGGING
        std::setvbuf(stdout, g_stdoutBuffer.data(), _IOFBF, g_stdoutBuffer.size());
#else
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
    }
    if (std::freopen(kRuntimeLogPath, "a", stderr) != nullptr) {
#if MKW_VITA_BUFFERED_LOGGING
        std::setvbuf(stderr, g_stderrBuffer.data(), _IOFBF, g_stderrBuffer.size());
#else
        std::setvbuf(stderr, nullptr, _IOLBF, 0);
#endif
    }
}

#if MKW_VITA_BUFFERED_LOGGING
void LogFlushWorker() {
    while (!g_logFlushStop.load(std::memory_order_acquire)) {
        sceKernelDelayThread(std::max<uint32_t>(10000u, MKW_VITA_LOG_FLUSH_INTERVAL_US));
        std::fflush(stdout);
        std::fflush(stderr);
    }
    std::fflush(stdout);
    std::fflush(stderr);
}

void StartLogFlushWorker() noexcept {
    if (g_logFlushThread.joinable()) return;
    g_logFlushStop.store(false, std::memory_order_release);
    if (!g_logFlushThread.start(WiiCompiledVita::HostThreadRole::Background,
                                64 * 1024, LogFlushWorker)) {
        std::fprintf(stderr,
                     "[PERF] buffered log flush worker failed; fatal paths still flush explicitly\n");
        std::fflush(stderr);
    }
}

void StopLogFlushWorker() noexcept {
    if (!g_logFlushThread.joinable()) return;
    g_logFlushStop.store(true, std::memory_order_release);
    g_logFlushThread.join();
}
#else
void StartLogFlushWorker() noexcept {}
void StopLogFlushWorker() noexcept {}
#endif

void BootLog(const char* phase, const char* message) noexcept {
    std::fprintf(stderr, "[%s] %s\n", phase, message);
    std::fflush(stderr);
}

void ConfigurePerformanceClocks() noexcept {
    const int beforeArm = scePowerGetArmClockFrequency();
    const int beforeBus = scePowerGetBusClockFrequency();
    const int beforeGpu = scePowerGetGpuClockFrequency();
    const int beforeXbar = scePowerGetGpuXbarClockFrequency();

    // Request the 500 MHz hardware-test target first. Overclock-enabled systems
    // accept it; stock firmware may reject it, in which case retain the established
    // 444 MHz fallback rather than leaving USER_0 at the 333 MHz boot clock.
    int armResult = 0;
    int armFallbackResult = 0;
    if (beforeArm < 500) {
        armResult = scePowerSetArmClockFrequency(500);
        if (armResult < 0 || scePowerGetArmClockFrequency() < 500) {
            armFallbackResult = scePowerSetArmClockFrequency(444);
        }
    }
    const int busResult = beforeBus < 222 ? scePowerSetBusClockFrequency(222) : 0;
    const int gpuResult = beforeGpu < 222 ? scePowerSetGpuClockFrequency(222) : 0;
    const int xbarResult = beforeXbar < 166 ? scePowerSetGpuXbarClockFrequency(166) : 0;

    std::fprintf(stderr,
                 "[PERF] clocks before=%d/%d/%d/%d arm500_rc=%d arm444_rc=%d other_rc=%d/%d/%d after=%d/%d/%d/%d MHz target_arm=500 preserve_high=%d\n",
                 beforeArm, beforeBus, beforeGpu, beforeXbar,
                 armResult, armFallbackResult, busResult, gpuResult, xbarResult,
                 scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
                 scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency(),
                 beforeArm >= 500 ? 1 : 0);
    std::fflush(stderr);
}

void LogFreeMemory(const char* phase) noexcept {
    SceKernelFreeMemorySizeInfo info{};
    info.size = sizeof(info);
    const int result = sceKernelGetFreeMemorySize(&info);
    if (result < 0) {
        std::fprintf(stderr, "[MEM] %s query failed: 0x%08X\n", phase,
                     static_cast<unsigned int>(result));
    } else {
        std::fprintf(stderr,
                     "[MEM] %s user=%d cdram=%d phycont=%d\n",
                     phase, info.size_user, info.size_cdram, info.size_phycont);
    }
    std::fflush(stderr);
}

void ServiceGuestTimingDuringAuroraFrameWait() {
    // The Vita entrypoint bypasses runtime/src/main.cpp, so install the same
    // bounded guest timing service explicitly on USER_0 while USER_1 drains.
#if MKW_VITA_WAIT_SERVICE_PROFILE
    const uint64_t beginUs = sceKernelGetProcessTimeWide();
#endif
    {
        GuestHotProfiler::HostPhaseScope phase(GuestHotProfiler::kPhaseWaitVi);
        VI_HLE_ProcessRetracesDeferred(8);
    }
#if MKW_VITA_WAIT_SERVICE_PROFILE
    const uint64_t viEndUs = sceKernelGetProcessTimeWide();
#endif
    {
        GuestHotProfiler::HostPhaseScope phase(GuestHotProfiler::kPhaseWaitAlarm);
        OS_HLE_ProcessAlarmsDeferred(8);
    }
#if MKW_VITA_WAIT_SERVICE_PROFILE
    const uint64_t alarmEndUs = sceKernelGetProcessTimeWide();
#endif
    bool serviceAudio = true;
#if MKW_VITA_AUDIO_WAIT_MIN_INTERVAL_US > 0
    static uint64_t lastAudioServiceUs = 0;
    const uint64_t audioServiceNowUs = sceKernelGetProcessTimeWide();
    serviceAudio = lastAudioServiceUs == 0 ||
                   audioServiceNowUs - lastAudioServiceUs >= MKW_VITA_AUDIO_WAIT_MIN_INTERVAL_US;
    if (serviceAudio) lastAudioServiceUs = audioServiceNowUs;
#endif
    if (serviceAudio) {
        GuestHotProfiler::HostPhaseScope phase(GuestHotProfiler::kPhaseWaitAudio);
#if MKW_VITA_AUDIO_WAIT_PROFILE
        Audio_HLE_PollDeferredForRenderWait();
#else
        Audio_HLE_PollDeferred();
#endif
    }
#if MKW_VITA_WAIT_SERVICE_PROFILE
    const uint64_t audioEndUs = sceKernelGetProcessTimeWide();
    WiiCompiledVita::GxBackend::RecordWaitServiceParts(
        viEndUs - beginUs, alarmEndUs - viEndUs, audioEndUs - alarmEndUs);
#endif
}

void ShutdownRuntime(bool fibersReady, bool gxReady) noexcept {
    GuestHotProfiler::Stop();
    if (fibersReady && Fiber::GuestFiberManager::IsInitialized()) {
        Fiber::GuestFiberManager::Shutdown();
    }
    if (gxReady) {
        WiiCompiledVita::GxBackend::Shutdown();
    }
    StopLogFlushWorker();
}

} // namespace

void WriteFatalLog(std::string_view reason) {
    std::fprintf(stderr, "[FAIL] %.*s\n", static_cast<int>(reason.size()), reason.data());
    std::fflush(stderr);
}

void SetRuntimeExitCode(int code) {
    g_runtimeExitCode.store(code, std::memory_order_release);
}

void MarkFatalErrorReported() {
    g_fatalErrorReported.store(true, std::memory_order_release);
}

void ShowRuntimeFatalPopup(std::string_view category, std::string_view details) noexcept {
    // A native Vita dialog can be added once boot reaches interactive state.
    // Logging is authoritative during bring-up and remains safe in crash paths.
    std::fprintf(stderr, "[FAIL] %.*s: %.*s\n",
                 static_cast<int>(category.size()), category.data(),
                 static_cast<int>(details.size()), details.data());
    std::fflush(stderr);
}

extern "C" void DumpHostStackTraceForRuntimeHelper() {
    BootLog("FAIL", "native stack trace unavailable on Vita ARM32");
}

namespace RuntimeCrash {

void WriteCrashArtifacts(std::string_view reason, std::string_view extraDetails,
                         const uint32_t* missingGuestTarget) noexcept {
    std::fprintf(stderr, "[FAIL] crash=%.*s target=0x%08X details=%.*s\n",
                 static_cast<int>(reason.size()), reason.data(),
                 missingGuestTarget != nullptr ? *missingGuestTarget : 0u,
                 static_cast<int>(extraDetails.size()), extraDetails.data());
    std::fflush(stderr);
}

[[noreturn]] void FatalMissingGuestTarget(uint32_t target, CpuContext*) noexcept {
    WriteCrashArtifacts("missing_guest_target", {}, &target);
    ShowRuntimeFatalPopup("Missing translated function",
                          "The guest jumped to an address that is absent from the translated registry.");
    MarkFatalErrorReported();
    SetRuntimeExitCode(EXIT_FAILURE);
    sceKernelExitProcess(EXIT_FAILURE);
    std::abort();
}

} // namespace RuntimeCrash

int main() {
    SetupLogging();
    BootLog("BOOT", "WiiCompiled Vita runtime start");
    ConfigurePerformanceClocks();
    LogFreeMemory("process-start");

    bool gxReady = false;
    bool fibersReady = false;
    try {
        if (sizeof(uintptr_t) != 4u) {
            throw std::runtime_error("Vita runtime was not compiled for ARM32");
        }
        if (!WiiCompiledVita::ConfigureCurrentThread(WiiCompiledVita::HostThreadRole::Guest)) {
            throw std::runtime_error("failed to bind guest runtime to USER_0");
        }
        if (sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId()) != SCE_KERNEL_CPU_MASK_USER_0) {
            throw std::runtime_error("guest runtime affinity is not USER_0");
        }
        BootLog("BOOT", "guest thread configured on USER_0");
        StartLogFlushWorker();

        if (!GuestHotProfiler::Start()) {
            BootLog("PERF", "guest hot-PC sampler failed to start; continuing without samples");
        }

        // SystemBridge owns the canonical Wii memory layout and data-section
        // initialization; initializing GuestFlat independently here would risk
        // requesting a different one-shot layout.
        SystemBridge::Initialize();
        if (!GuestFlat::IsActive()) {
            throw std::runtime_error("guest memory backend is inactive after SystemBridge initialization");
        }
        BootLog("BOOT", "memory and SystemBridge ready");
        LogFreeMemory("system-bridge-ready");

        TranslatedFunctionRegistry::Finalize();
        if (!TranslatedFunctionRegistry::IsLookupPublished()) {
            throw std::runtime_error("translated registry did not publish");
        }
        BootLog("BOOT", "translated registry ready");

        if (!WiiCompiledVita::GxBackend::Initialize()) {
            throw std::runtime_error("Vita GX backend initialization failed");
        }
        gxReady = true;
        BootLog("BOOT", "Vita GX backend ready on USER_1");
        LogFreeMemory("gx-ready");

        // The desktop runtime installs these hooks after aurora_initialize(),
        // but Vita first-boot bypasses runtime/src/main.cpp and initializes the
        // GX backend directly above. Without this call every guest texture is
        // classified as untracked, so GXInvalidateTexAll/GXTexObj churn forces
        // repeated decode + glTexImage2D uploads and catastrophic cache thrash.
        GxGuestWrite::InstallAuroraHooks();
        BootLog("BOOT", "GX guest write tracking installed");

        Fiber::GuestFiberManager::Initialize();
        fibersReady = Fiber::GuestFiberManager::IsInitialized();
        if (!fibersReady) {
            throw std::runtime_error("guest fiber manager initialization failed");
        }
        BootLog("BOOT", "guest fiber manager ready");

        const auto* entry = TranslatedFunctionRegistry::FindByAddressPtr(kDefaultEntryAddress);
        if (entry == nullptr || entry->rawCpuInvoker == nullptr) {
            throw std::runtime_error("Mario Kart entry 0x800060A4 is missing from the translated registry");
        }

        InitializePersistentCpuContext();
        CpuContext& cpu = GetPersistentCpuContext();
        cpu.gpr[1] = kGuestStackTop;
        cpu.gpr[2] = RuntimeConfig::SDA2_BASE;
        cpu.gpr[13] = RuntimeConfig::SDA1_BASE;
        cpu.pc = kDefaultEntryAddress;

#if MKW_VITA_WAIT_TIMING_SERVICE
        aurora_set_frame_worker_wait_callback(ServiceGuestTimingDuringAuroraFrameWait);
        BootLog("BOOT", "Aurora wait timing service installed");
#else
        aurora_set_frame_worker_wait_callback(nullptr);
        BootLog("BOOT", "Aurora wait timing service disabled");
#endif

        std::fprintf(stderr,
                     "[BOOT] entry 0x%08X (%s), r1=0x%08X r2=0x%08X r13=0x%08X\n",
                     entry->address, entry->name, cpu.gpr[1], cpu.gpr[2], cpu.gpr[13]);
        std::fflush(stderr);

        {
            CpuContextScope cpuScope(&cpu);
            InvokeIndirectCpu(entry->address, &cpu);
        }

        std::fprintf(stderr, "[BOOT] guest entry returned r3=0x%08X\n", cpu.gpr[3]);
        BootLog("BOOT", "runtime shutdown");
        ShutdownRuntime(fibersReady, gxReady);
        SetRuntimeExitCode(EXIT_SUCCESS);
    } catch (const std::exception& error) {
        ShowRuntimeFatalPopup("Vita runtime initialization or execution failed", error.what());
        WriteFatalLog("runtime_exception");
        MarkFatalErrorReported();
        ShutdownRuntime(fibersReady, gxReady);
        SetRuntimeExitCode(EXIT_FAILURE);
    } catch (...) {
        ShowRuntimeFatalPopup("Vita runtime initialization or execution failed",
                              "unknown non-standard exception");
        WriteFatalLog("unknown_runtime_exception");
        MarkFatalErrorReported();
        ShutdownRuntime(fibersReady, gxReady);
        SetRuntimeExitCode(EXIT_FAILURE);
    }

    const int result = g_runtimeExitCode.load(std::memory_order_acquire);
    if (result != EXIT_SUCCESS && !g_fatalErrorReported.load(std::memory_order_acquire)) {
        BootLog("FAIL", "runtime exited without a categorized error");
    }
    std::fflush(nullptr);
    sceKernelExitProcess(result);
    return result;
}
