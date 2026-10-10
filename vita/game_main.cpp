#include "abi_bridge.h"
#include "fiber_manager.h"
#include "host_context.h"
#include "native_call_profile.h"
#include "gx/command_processor.hpp"
#include "gx/fifo.hpp"
#include "gx_guest_write.h"
#include "hle_stubs.h"
#include "system_bridge.h"
#include "audio_backend.h"
#include "guest_stall_probe.h"
#include "console_identity.h"
#include "runtime_config.h"
#include "game_time_trial.hpp"
#include "backend.hpp"
#if WIC_PRECOMPILED_ASSETS
#include "native_assets.hpp"
#include "native_audio.hpp"
#endif
#include "boot_disc.hpp"
#include "runtime_checks.hpp"
#if defined(WIC_CPU_DIAGNOSTICS)
#include "game_cpu_diagnostics.hpp"
#endif
#include "aurora_events.h"
#include "aurora_vita_backend.hpp"
#include <dolphin/gx.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <cstdio>
#include <exception>
#include <system_error>

extern "C" { unsigned int _newlib_heap_size_user=144*1024*1024; bool g_dynamicAspectRatioEnabled=false; }
int main() {
    WicGuestStallProbe::hostGameThread.store(sceKernelGetThreadId(),std::memory_order_relaxed);
    sceIoMkdir("ux0:data/wiicompiled-vita",0777);
    std::freopen("ux0:data/wiicompiled-vita/runtime.log","a",stdout);
    std::freopen("ux0:data/wiicompiled-vita/runtime.log","a",stderr);
    std::setvbuf(stdout,nullptr,_IOLBF,0);std::setvbuf(stderr,nullptr,_IOLBF,0);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    scePowerSetArmClockFrequency(444);scePowerSetGpuClockFrequency(222);
    const char* buildMarker=WIC_RESIDENT_TEXTURES?
        (WIC_GX_ASYNC_BUILD?"psarc-resident-assets-async-native-render-20261009":"psarc-resident-assets-native-render-20261009"):
        (WIC_GX_ASYNC_BUILD?"psarc-precompiled-assets-async-native-render-20261009":"psarc-precompiled-assets-native-render-20261009");
    std::fprintf(stderr,"[boot] build=%s upstream=279ce832 aurora=%s fast_math=%d fast_ram_ranges=%d input=gamecube attribute2=12 native_call_profile=%d native_thp=%d hardware_avc=%d precompiled_assets=%d gx_async=%d\n",buildMarker,WIC_AURORA_REV,WIC_FAST_PPC_MATH,WIC_FAST_RAM_RANGES,WIC_NATIVE_CALL_PROFILE,WIC_NATIVE_THP,WIC_HARDWARE_AVC,WIC_PRECOMPILED_ASSETS,WIC_GX_ASYNC_BUILD);
    std::fprintf(stderr,"[boot] MEM1=%u MEM2=%u heap=%u bytes\n",static_cast<unsigned>(Memory::kMem1Size),static_cast<unsigned>(Memory::kMem2Size),_newlib_heap_size_user);
    try {
        wiicompiled::vita::CheckRuntimeContracts();
        if(WIC_DISTINCT_CPU_CORES || RuntimeConfigFile::Get().vitaRender.distinctCpuDispatch) {
            const int affinityResult=sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),SCE_KERNEL_CPU_MASK_USER_0);
            std::fprintf(stderr,"[boot] distinct_cpu_cores=1 game_affinity_result=%d actual_affinity=0x%x\n",
                affinityResult,sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId()));
        }
        const bool diagnostics=RuntimeConfigFile::Get().diagnosticsEnabled.value_or(true);
        const auto captureFrame=RuntimeConfigFile::Get().diagnosticsCaptureFrame;
        std::fprintf(stderr,"[diagnostics-config] enabled=%u custom_capture=%u capture_frame=%u\n",
            unsigned(diagnostics),unsigned(captureFrame.has_value()),captureFrame.value_or(0));
        bool packedDisc=false;
        const auto psarcPath=RuntimeConfigFile::ResolvedDvdPsarc();
        if(!psarcPath.empty() && wiicompiled::vita::psarc::Mount(RuntimeConfigFile::PathToUtf8(psarcPath).c_str())) {
            try {
                wiicompiled::verifyBootPsarc();
                packedDisc=true;
#if WIC_PRECOMPILED_ASSETS
                wiicompiled::vita::assets::Initialize(size_t(RuntimeConfigFile::Get().vitaRender.nativeAssetCacheMb)*1024*1024);
                wiicompiled::vita::assets::InitializeAudio();
#endif
                std::fprintf(stderr,"[boot] PSARC=%s boot hashes verified; auto-starting guest\n",
                    RuntimeConfigFile::PathToUtf8(psarcPath).c_str());
            } catch(const std::exception& error) {
                std::fprintf(stderr,"[boot] PSARC rejected: %s\n",error.what());
                wiicompiled::vita::psarc::Shutdown();
                throw;
            }
        } else if(!psarcPath.empty()) {
            throw std::runtime_error(std::string("Configured PSARC unavailable: ")+wiicompiled::vita::psarc::LastError());
        }
        if(!packedDisc) {
            const auto isoPath=RuntimeConfigFile::ResolvedDvdIso();
            wiicompiled::Disc disc;
            disc.open(RuntimeConfigFile::PathToUtf8(isoPath));
            wiicompiled::verifyBootDisc(disc);
            std::fprintf(stderr,"[boot] ISO=%s size=%llu boot hashes verified; auto-starting guest\n",
                RuntimeConfigFile::PathToUtf8(isoPath).c_str(),static_cast<unsigned long long>(disc.isoSize()));
        }
        // The archive/catalogues must exist before GXM prewarm calls the native
        // asset reader. Previously initialization guaranteed an archive miss.
        if(!wiicompiled::backend::initialize(diagnostics,RuntimeConfigFile::Get().vitaRender))
            throw std::runtime_error("Native GXM initialization failed");
        std::fprintf(stderr,"[gx-worker] requested=%d running=%d\n",WIC_GX_ASYNC_BUILD,
            aurora::gx::fifo::worker_running()?1:0);
        SystemBridge::Initialize();InitializePersistentCpuContext();
        TranslatedFunctionRegistry::Finalize();
        std::fprintf(stderr,"[boot] preloading NAND console identity\n");
        (void)RuntimeConsoleIdentity::Current();
        std::fprintf(stderr,"[boot] NAND console identity ready\n");
        settings_overlay::InitializeRuntimeSettings();
        wiicompiled::vita::time_trial::initialize(RuntimeConfigFile::Get().testTimeTrial.value_or(false));
        ConfigureMkwDynamicAspect(RuntimeConfigFile::WidescreenEnabled(true),RuntimeConfigFile::ForceAspect169Enabled(false),960,544);
        auto& cpu=GetPersistentCpuContext();cpu.gpr[1]=0x81700000u;
        GxGuestWrite::InstallAuroraHooks();Fiber::GuestFiberManager::Initialize();
        HostContext::SetNativeCallProfiling(diagnostics && WIC_NATIVE_CALL_PROFILE != 0);
#if defined(WIC_CPU_DIAGNOSTICS)
        if(diagnostics)WicEnableCpuDiagnostics();
#endif
        aurora::gx::fifo::set_synchronous_dispatch(HostContext::RunOnNativeStack);
        std::fprintf(stderr,"[boot] native-stack-dispatch=1\n");
        SceKernelThreadInfo threadInfo{};
        threadInfo.size=sizeof(threadInfo);
        if(sceKernelGetThreadInfo(sceKernelGetThreadId(),&threadInfo)>=0)
            std::fprintf(stderr,"[boot] native-stack=%p size=%u\n",threadInfo.stack,static_cast<unsigned>(threadInfo.stackSize));
        CpuContextScope scope(&cpu);
        std::fprintf(stderr,"[guest] entry=%08x\n",kDefaultEntryAddress);
        InvokeIndirectCpu(kDefaultEntryAddress,&cpu);
        aurora::gx::fifo::set_synchronous_dispatch(nullptr);
        Fiber::GuestFiberManager::Shutdown();AudioBackend::Instance().Shutdown();
        wiicompiled::vita::psarc::Shutdown();aurora::vita::shutdown();return 0;
    } catch(const std::system_error& error) {
        char details[320]{};
        std::snprintf(details,sizeof(details),"system_error code=%d category=%s what=%s",
            error.code().value(),error.code().category().name(),error.what());
        std::fprintf(stderr,"[fatal] %s\n",details);
        RuntimeCrash::WriteCrashArtifacts("boot_system_error",details);AudioBackend::Instance().Shutdown();
        wiicompiled::vita::psarc::Shutdown();return 1;
    } catch(const std::exception& error) {
        RuntimeCrash::WriteCrashArtifacts("boot",error.what());AudioBackend::Instance().Shutdown();
        wiicompiled::vita::psarc::Shutdown();return 1;
    }
}
