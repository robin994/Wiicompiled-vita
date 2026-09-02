#include "abi_bridge.h"
#include "fiber_manager.h"
#include "generated/RuntimeConfig.h"
#include "guest_flat_memory.h"
#include "gx_guest_write.h"
#include "system_bridge.h"
#include "wiicompiled_vita/gx_backend.h"
#include "wiicompiled_vita/host_thread.h"

#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <atomic>
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

void SetupLogging() noexcept {
    sceIoMkdir(kDataDirectory, 0777);
    if (std::freopen(kRuntimeLogPath, "a", stdout) != nullptr) {
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
    }
    if (std::freopen(kRuntimeLogPath, "a", stderr) != nullptr) {
        std::setvbuf(stderr, nullptr, _IOLBF, 0);
    }
}

void BootLog(const char* phase, const char* message) noexcept {
    std::fprintf(stderr, "[%s] %s\n", phase, message);
    std::fflush(stderr);
}

void ConfigurePerformanceClocks() noexcept {
    const int beforeArm = scePowerGetArmClockFrequency();
    const int beforeBus = scePowerGetBusClockFrequency();
    const int beforeGpu = scePowerGetGpuClockFrequency();
    const int beforeXbar = scePowerGetGpuXbarClockFrequency();

    // Highest clocks exposed by the stock Vita power API. These do not rely on
    // an overclock plugin and keep the test reproducible on unmodified hardware.
    const int armResult = scePowerSetArmClockFrequency(444);
    const int busResult = scePowerSetBusClockFrequency(222);
    const int gpuResult = scePowerSetGpuClockFrequency(222);
    const int xbarResult = scePowerSetGpuXbarClockFrequency(166);

    std::fprintf(stderr,
                 "[PERF] clocks before=%d/%d/%d/%d set_rc=%d/%d/%d/%d after=%d/%d/%d/%d MHz\n",
                 beforeArm, beforeBus, beforeGpu, beforeXbar,
                 armResult, busResult, gpuResult, xbarResult,
                 scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
                 scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
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

void ShutdownRuntime(bool fibersReady, bool gxReady) noexcept {
    if (fibersReady && Fiber::GuestFiberManager::IsInitialized()) {
        Fiber::GuestFiberManager::Shutdown();
    }
    if (gxReady) {
        WiiCompiledVita::GxBackend::Shutdown();
    }
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
