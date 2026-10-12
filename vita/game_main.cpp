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
#include "gx_copy_async_policy.hpp"
#include "game_time_trial.hpp"
#include "boot_identity.hpp"
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
#include <aurora_vita_view_draw_capture.h>
#include <dolphin/gx.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <cstdio>
#include <array>
#include <exception>
#include <system_error>

extern "C" { unsigned int _newlib_heap_size_user=144*1024*1024; bool g_dynamicAspectRatioEnabled=false; }
int main() {
    WicGuestStallProbe::hostGameThread.store(sceKernelGetThreadId(),std::memory_order_relaxed);
    sceIoMkdir("ux0:data/wiicompiled-vita",0777);
    bool immediateLogs=true;
    try {
        immediateLogs=RuntimeConfigFile::Get().diagnosticsEnabled.value_or(true);
    } catch(...) {
        // Keep immediate logs for an invalid configuration. The normal boot
        // path below still validates it and reports the original exception.
    }
    std::freopen("ux0:data/wiicompiled-vita/runtime.log","a",stdout);
    std::freopen("ux0:data/wiicompiled-vita/runtime.log","a",stderr);
    // Vita flash I/O is slow: flushing a physical write per newline can stall
    // the guest/GX thread even when logging is not requested. Quiet mode
    // buffers at most 32 KiB per stream; diagnostic mode stays line-buffered
    // for immediate crash/trace inspection. Full buffers flush on process exit
    // or the existing fatal path, not once per draw.
    static std::array<char,32*1024> stdoutBuffer{},stderrBuffer{};
    if(immediateLogs) {
        std::setvbuf(stdout,nullptr,_IOLBF,0);
        std::setvbuf(stderr,nullptr,_IOLBF,0);
    } else {
        std::setvbuf(stdout,stdoutBuffer.data(),_IOFBF,stdoutBuffer.size());
        std::setvbuf(stderr,stderrBuffer.data(),_IOFBF,stderrBuffer.size());
    }
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    scePowerSetArmClockFrequency(444);scePowerSetGpuClockFrequency(222);
    const char* buildMarker=WIC_RESIDENT_TEXTURES?
        (WIC_GX_ASYNC_BUILD?"psarc-resident-assets-async-native-render-20261009":"psarc-resident-assets-native-render-20261009"):
        (WIC_GX_ASYNC_BUILD?"psarc-precompiled-assets-async-native-render-20261009":"psarc-precompiled-assets-native-render-20261009");
    std::fprintf(stderr,"[boot] build=%s upstream=279ce832 aurora=%s fast_math=%d fast_ram_ranges=%d input=gamecube attribute2=12 native_call_profile=%d native_thp=%d hardware_avc=%d precompiled_assets=%d gx_async=%d\n",buildMarker,WIC_AURORA_REV,WIC_FAST_PPC_MATH,WIC_FAST_RAM_RANGES,WIC_NATIVE_CALL_PROFILE,WIC_NATIVE_THP,WIC_HARDWARE_AVC,WIC_PRECOMPILED_ASSETS,WIC_GX_ASYNC_BUILD);
    const auto selfIdentity=wiicompiled::vita::ReadBootFileIdentity("app0:eboot.bin");
    const auto& configSource=RuntimeConfigFile::LoadedSource();
    const auto configIdentity=wiicompiled::vita::HashBootBytes(configSource.bytes);
    std::fprintf(stderr,"[boot-identity] self_sha256=%s self_bytes=%llu config_sha256=%s config_bytes=%llu config_loaded=%u cpu_mhz=%d gpu_mhz=%d\n",
        selfIdentity.available?selfIdentity.sha256.data():"unavailable",
        static_cast<unsigned long long>(selfIdentity.bytes),
        configSource.available&&configIdentity.available?configIdentity.sha256.data():"unavailable",
        static_cast<unsigned long long>(configIdentity.bytes),unsigned(configSource.available),
        scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency());
    std::fprintf(stderr,"[cpu-dispatch-config] immutable_fastpath=%u\n",unsigned(WIC_IMMUTABLE_DISPATCH_FASTPATH));
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
                const auto& options=RuntimeConfigFile::Get().vitaRender;
                wiicompiled::vita::assets::Initialize(size_t(options.nativeAssetCacheMb)*1024*1024,
                    size_t(options.nativeAssetPrewarmMb)*1024*1024,"ux0:data/wiicompiled-vita");
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
        const auto& renderOptions=RuntimeConfigFile::Get().vitaRender;
        // The ISO-check executable shares the renderer backend, not the game
        // GX HLE runtime. Configure GX-only copy scheduling on the game path.
        WicSetAsyncEfbCopies(renderOptions.asyncEfbCopies);
        std::fprintf(stderr,"[wic-efb-async] copy=%u retire=%u\n",
            unsigned(renderOptions.asyncEfbCopies),unsigned(renderOptions.deferredEfbTargets));
        if(!wiicompiled::backend::initialize(diagnostics,renderOptions))
            throw std::runtime_error("Native GXM initialization failed");
        // Hash the *encoded* GX source at upload time, before game code can
        // release or mutate it. Disabled completely on normal runs, and
        // independent from the expensive one-frame draw capture.
        aurora_vita_view_draw_set_texture_probe(
            diagnostics && captureFrame && *captureFrame!=0 ? 1:0);
#if WIC_PRECOMPILED_ASSETS
        if(wiicompiled::vita::assets::PrewarmEnabled()) {
            const auto started=sceKernelGetProcessTimeWide();
            const auto warm=wiicompiled::vita::assets::WarmBoot();
            std::fprintf(stderr,"[asset-prewarm] phase=boot manifest=%u selected=%u loaded=%u existing=%u bytes=%u elapsed_us=%llu\n",
                unsigned(warm.manifestLoaded),warm.selected,warm.loaded,warm.alreadyCached,
                unsigned(warm.bytes),
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-started));
        }
#endif
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
        aurora::vita::shutdown();
#if WIC_PRECOMPILED_ASSETS
        wiicompiled::vita::assets::Shutdown();
#endif
        wiicompiled::vita::psarc::Shutdown();return 0;
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
