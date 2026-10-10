#include "platform/host_platform.h"
#include "system_bridge.h"
#include "abi_bridge.h"
#include "settings_overlay.h"
#include "discord_presence.h"
#include "audio_backend.h"
#include "music_attenuation.h"
#include "runtime_config.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/rng.h>
#include <psp2/net/net.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform_time.h>
#include <mbedtls/ssl.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cerrno>
#include <sys/socket.h>

namespace RuntimePlatform {
std::optional<std::filesystem::path> ExecutableDirectory() noexcept { return "app0:"; }
std::filesystem::path ApplicationDataDirectory(std::string_view) { return "ux0:data/wiicompiled-vita"; }
std::filesystem::path LogDirectory(std::string_view) { return "ux0:data/wiicompiled-vita"; }
uint64_t CurrentProcessId() noexcept { return sceKernelGetProcessId(); }
}
extern "C" int mbedtls_hardware_poll(void*,unsigned char* output,size_t length,size_t* written) {
    *written=0;
    if(sceKernelGetRandomNumber(output,static_cast<SceSize>(length))<0)return -1;
    *written=length;return 0;
}
extern "C" mbedtls_ms_time_t mbedtls_ms_time() { return sceKernelGetProcessTimeWide()/1000; }
extern "C" int mbedtls_net_recv(void* ctx,unsigned char* out,size_t length) {
    const int result=recv(static_cast<mbedtls_net_context*>(ctx)->fd,out,length,0);
    if(result>=0)return result;
    if(errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)return MBEDTLS_ERR_SSL_WANT_READ;
    if(errno==ECONNRESET)return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}
namespace settings_overlay {
void InitializeRuntimeSettings() noexcept {
    AudioBackend::Instance().SetMasterVolume(RuntimeConfigFile::AudioVolume(1));
    AudioBackend::Instance().SetMuted(RuntimeConfigFile::AudioMuted(false));
    MusicAttenuation::SetMusicVolume(RuntimeConfigFile::MusicVolume(1));
    MusicAttenuation::SetSoundEffectsVolume(RuntimeConfigFile::SoundEffectsVolume(1));
    MusicAttenuation::SetUiVolume(RuntimeConfigFile::UiVolume(1));
    MusicAttenuation::SetVoicesVolume(RuntimeConfigFile::VoicesVolume(1));
}
void HandleEvents(const AuroraEvent*) noexcept {}
void Draw() noexcept {}
bool StartupScreenVisible() noexcept { return false; }
void NotifyStrapInputAccepted() noexcept {}
void AdvancePresentedFrame() noexcept {}
void ReleaseControllers() noexcept {}
}
namespace DiscordPresence {
void Initialize(const std::string&,const std::string&) {}
void SetClient(const std::string&) {}
void SetActivity(Activity) {}
void Reset() {}
void Shutdown() {}
}
void WriteFatalLog(std::string_view reason) { RuntimeCrash::WriteCrashArtifacts(reason); }
void SetRuntimeExitCode(int) {}
void MarkFatalErrorReported() {}
void ShowRuntimeFatalPopup(std::string_view category,std::string_view details) noexcept {
    std::fprintf(stderr,"[fatal] %.*s: %.*s\n",int(category.size()),category.data(),int(details.size()),details.data());
}
extern "C" void DumpHostStackTraceForRuntimeHelper() { SystemBridge::DumpCpuState(TryGetCpuContext()); }
namespace RuntimeCrash {
void WriteCrashArtifacts(std::string_view reason,std::string_view details,const uint32_t* target) noexcept {
    try {
        std::ofstream out("ux0:data/wiicompiled-vita/crash.txt");
        const CpuContext* cpu=TryGetCpuContext();
        // Exception unwinding restores the outer scope before main catches it.
        // The persistent guest context still holds the failing register state.
        if(!cpu)cpu=&GetPersistentCpuContext();
        out << reason << '\n' << details << '\n';SystemBridge::DumpCpuState(out,cpu);
        SystemBridge::DumpCrashHeuristics(out,cpu,target);
        out.flush();
    } catch(...) {}
    std::fflush(stdout);std::fflush(stderr);
}
[[noreturn]] void FatalMissingGuestTarget(uint32_t target,CpuContext* cpu) noexcept {
    std::fprintf(stderr,"[fatal] missing PPC target %08x\n",target);
    SystemBridge::DumpCpuState(cpu);WriteCrashArtifacts("missing_target",{},&target);std::abort();
}
}
