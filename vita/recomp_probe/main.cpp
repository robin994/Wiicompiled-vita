#include "abi_bridge.h"
#include "generated/RuntimeConfig.h"
#include "guest_flat_memory.h"
#include "isa/ppc_isa_config.h"
#include "wiicompiled_vita/host_thread.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kLogDirectory = "ux0:data/wiicompiled-vita";
constexpr const char* kLogPath = "ux0:data/wiicompiled-vita/recomp_probe.log";
constexpr uint32_t kEntryAddress = 0x80001000u;
constexpr uint32_t kCalleeAddress = 0x80001100u;
constexpr uint32_t kExpectedResult = 60u;

void Log(const char* format, ...) {
    char line[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    sceIoMkdir(kLogDirectory, 0777);
    const SceUID fd = sceIoOpen(kLogPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, static_cast<SceSize>(std::strlen(line)));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
    std::printf("%s\n", line);
}

[[noreturn]] void ExitFailure(int code, const char* message) {
    Log("[FAIL] %s", message);
    Log("[RESULT] recomp probe FAIL (exit=%d)", code);
    sceKernelExitProcess(code);
    std::abort();
}

bool InitializeGuestMemory() {
    const std::vector<GuestFlat::RegionRequest> regions = {
        {0x00000000u, 64u * 1024u, GuestFlat::Backing::Mem1},
        {0x80000000u, 64u * 1024u, GuestFlat::Backing::Mem1},
        {0xC0000000u, 64u * 1024u, GuestFlat::Backing::Mem1},
        {0x10000000u, 64u * 1024u, GuestFlat::Backing::Mem2},
        {0x90000000u, 64u * 1024u, GuestFlat::Backing::Mem2},
    };
    GuestFlat::Initialize(regions);
    return GuestFlat::IsActive() && GuestFlat::HostPointer(0x80000000u) != nullptr &&
           GuestFlat::HostPointer(0x90000000u) != nullptr;
}

} // namespace

// emit-build-shards discovers this HLE hook in the native source tree and puts
// it in the generated base indirect-dispatch table. The synthetic program does
// not call it, but the real registry validator deliberately requires every
// generated table entry to have a matching registration.
extern "C" void PPCMfhid2_HLE_8012e630(CpuContext* ctx) {
    if (ctx != nullptr) {
        ctx->gpr[3] = ctx->hid2 != 0 ? ctx->hid2 : 0x10000000u;
    }
}
REGISTER_TRANSLATED_FUNCTION(0x8012e630, PPCMfhid2_HLE_8012e630);

void ShowRuntimeFatalPopup(std::string_view category, std::string_view details) noexcept {
    Log("[FATAL] %.*s: %.*s",
        static_cast<int>(category.size()), category.data(),
        static_cast<int>(details.size()), details.data());
}

extern "C" void DumpHostStackTraceForRuntimeHelper() {}
void MarkFatalErrorReported() {}
void SetRuntimeExitCode(int) {}

namespace RuntimeCrash {

void WriteCrashArtifacts(std::string_view reason, std::string_view details,
                         const uint32_t* missingGuestTarget) noexcept {
    Log("[CRASH] %.*s target=0x%08X details=%.*s",
        static_cast<int>(reason.size()), reason.data(),
        missingGuestTarget != nullptr ? *missingGuestTarget : 0u,
        static_cast<int>(details.size()), details.data());
}

[[noreturn]] void FatalMissingGuestTarget(uint32_t target, CpuContext*) noexcept {
    Log("[FAIL] missing translated guest target 0x%08X", target);
    sceKernelExitProcess(20);
    std::abort();
}

} // namespace RuntimeCrash

int main() {
    sceIoMkdir(kLogDirectory, 0777);
    const SceUID oldLog = sceIoOpen(kLogPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (oldLog >= 0) {
        sceIoClose(oldLog);
    }

    Log("[BOOT] WiiCompiled Vita synthetic recomp probe");
    Log("[BOOT] ARM pointer width=%u bits", static_cast<unsigned>(sizeof(uintptr_t) * 8u));
    if (sizeof(uintptr_t) != 4u) {
        ExitFailure(1, "binary is not ARM32");
    }

    if (!WiiCompiledVita::ConfigureCurrentThread(WiiCompiledVita::HostThreadRole::Guest)) {
        ExitFailure(2, "unable to bind guest thread to USER_0");
    }
    const int affinity = sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId());
    Log("[BOOT] guest thread affinity=0x%X", affinity);
    if (affinity != SCE_KERNEL_CPU_MASK_USER_0) {
        ExitFailure(3, "guest thread is not isolated on USER_0");
    }

    try {
        if (!InitializeGuestMemory()) {
            ExitFailure(4, "guest memory backend did not become active");
        }
    } catch (const std::exception& error) {
        Log("[FAIL] guest memory initialization exception: %s", error.what());
        ExitFailure(5, "guest memory initialization failed");
    }
    Log("[BOOT] guest memory ready (MEM1/MEM2 aliases mapped)");

    Log("[BOOT] state-free ABI for 0x%08X=%s", kCalleeAddress,
        MkwStateFreeAbiEnabled(kCalleeAddress) ? "enabled" : "disabled");
    if (MkwStateFreeAbiEnabled(kCalleeAddress)) {
        ExitFailure(6, "state-free ABI must remain disabled on Vita");
    }

    const auto* entryBeforeFinalize = TranslatedFunctionRegistry::FindByAddressPtr(kEntryAddress);
    const auto* calleeBeforeFinalize = TranslatedFunctionRegistry::FindByAddressPtr(kCalleeAddress);
    if (entryBeforeFinalize == nullptr || calleeBeforeFinalize == nullptr) {
        ExitFailure(7, "generated bulk registrar did not register both translated functions");
    }
    Log("[BOOT] bulk registration ready: entry=%s callee=%s",
        entryBeforeFinalize->name, calleeBeforeFinalize->name);

    try {
        TranslatedFunctionRegistry::Finalize();
    } catch (const std::exception& error) {
        Log("[FAIL] registry finalization exception: %s", error.what());
        ExitFailure(8, "generated registry/dispatch validation failed");
    }
    if (!TranslatedFunctionRegistry::IsLookupPublished()) {
        ExitFailure(9, "translated registry was not published");
    }
    Log("[BOOT] registry finalized; generated indirect dispatch published");

    InitializePersistentCpuContext();
    CpuContext& cpu = GetPersistentCpuContext();
    cpu.gpr[1] = 0x8000F000u;
    cpu.gpr[2] = RuntimeConfig::SDA2_BASE;
    cpu.gpr[3] = 0u;
    cpu.gpr[13] = RuntimeConfig::SDA1_BASE;
    cpu.pc = kEntryAddress;
    Log("[EXEC] CpuContext seeded: pc=0x%08X r1=0x%08X r2=0x%08X r3=%u r13=0x%08X",
        cpu.pc, cpu.gpr[1], cpu.gpr[2], cpu.gpr[3], cpu.gpr[13]);

    const RawDispatchRecord* rawEntry = TranslatedFunctionRegistry::FindRawByAddressPtr(kEntryAddress);
    if (rawEntry == nullptr || rawEntry->entry == nullptr) {
        ExitFailure(10, "generated static indirect-dispatch record is missing");
    }
    Log("[EXEC] dispatch record resolved for 0x%08X", kEntryAddress);

    InvokeIndirectCpu(kEntryAddress, &cpu);
    Log("[EXEC] InvokeIndirectCpu returned: r3=%u", cpu.gpr[3]);
    if (cpu.gpr[3] != kExpectedResult) {
        ExitFailure(11, "translated result mismatch (expected r3=60)");
    }

    Log("[PASS] CpuContext path executed 0x%08X -> 0x%08X and produced r3=%u",
        kEntryAddress, kCalleeAddress, cpu.gpr[3]);
    Log("[PASS] state-free fast ABI remained disabled on Vita ARM32");
    Log("[RESULT] recomp probe PASS");
    sceKernelExitProcess(0);
    return 0;
}
