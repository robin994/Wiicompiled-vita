#include "audio_backend.h"
#include "game_gx_stall_watch.hpp"
#include "guest_stall_probe.h"
#include "psarc_stall_watch.hpp"
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

namespace {
class AudioSpinMutex {
public:
    void lock() noexcept {
        while(flag.test_and_set(std::memory_order_acquire))
            std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    void unlock() noexcept { flag.clear(std::memory_order_release); }
private:
    std::atomic_flag flag=ATOMIC_FLAG_INIT;
};

std::array<int16_t,32768> ring{};
size_t head=0,count=0;
AudioSpinMutex queueMutex;
std::atomic_bool stopped{true};
int port=-1;
SceUID workerThread=-1;
uint64_t acceptedSamples=0,consumedSamples=0,underruns=0,rejectedBlocks=0;
std::atomic<uint32_t> outputErrors{0};

int drainThread(SceSize,void*) {
    std::array<int16_t,512> block{};
    uint64_t lastFrame=0,lastFrameAdvanceUs=0,lastReportUs=0,nextSampleUs=0;
    uint64_t lastGameCpuClocks=0;
    while(!stopped.load(std::memory_order_acquire)) {
        block.fill(0);
        {
            std::lock_guard<AudioSpinMutex> lock(queueMutex);
            const size_t available=std::min(count,block.size());
            for(size_t i=0;i<available;++i)block[i]=ring[(head+i)%ring.size()];
            head=(head+available)%ring.size();count-=available;
            consumedSamples+=available;
            if(available<block.size())++underruns;
        }
        if(sceAudioOutOutput(port,block.data())<0)outputErrors.fetch_add(1,std::memory_order_relaxed);
        const uint64_t now=sceKernelGetProcessTimeWide();
        if(now>=nextSampleUs) {
            nextSampleUs=now+1000000u;
            WicGxProgress gx{};
            WicReadGxProgress(&gx);
            if(!lastFrameAdvanceUs || gx.frame!=lastFrame) {
                lastFrame=gx.frame;
                lastFrameAdvanceUs=now;
            } else if(gx.frame>=30 && now-lastFrameAdvanceUs>=8000000u &&
                      now-lastReportUs>=10000000u) {
                lastReportUs=now;
                std::fprintf(stderr,
                    "[gx-stall] frame=%llu stalled_us=%llu submitted=%llu completed=%llu active_serial=%llu active_type=%u segment=%u gxm_op=%u audio_alive=1\n",
                    static_cast<unsigned long long>(gx.frame),
                    static_cast<unsigned long long>(now-lastFrameAdvanceUs),
                    static_cast<unsigned long long>(gx.submitted),
                    static_cast<unsigned long long>(gx.completed),
                    static_cast<unsigned long long>(gx.activeSerial),gx.activeType,gx.activeSegment,gx.gxmOperation);
                const auto guest=WicGuestStallProbe::snapshot();
                SceKernelThreadInfo mainInfo{};
                mainInfo.size=sizeof(mainInfo);
                const int infoResult=guest.hostGameThread<0?-1:
                    sceKernelGetThreadInfo(guest.hostGameThread,&mainInfo);
                const uint64_t currentClocks=infoResult>=0?mainInfo.runClocks:0;
                const uint64_t elapsedClocks=currentClocks>=lastGameCpuClocks?
                    currentClocks-lastGameCpuClocks:0;
                lastGameCpuClocks=currentClocks;
                std::fprintf(stderr,
                    "[guest-stall] frame=%llu host_uid=%d info_rc=0x%08x status=0x%08x wait_type=%u wait_id=%d cpu_id=%d run_clocks=%llu run_delta=%llu "
                    "select=%llu idle_polls=%llu vi_polls=%llu vi_retraces=%llu switches=%llu sleeps=%llu wakes=%llu resumes=%llu recv=%llu send=%llu msg_waits=%llu "
                    "creates=%llu task_runs=%llu task_callbacks=%llu task_done=%llu task_phase=%u running=0x%08x selected=0x%08x sleeping=0x%08x wait_queue=0x%08x msg_queue=0x%08x created_thread=0x%08x created_entry=0x%08x resumed_thread=0x%08x task_callback=0x%08x\n",
                    static_cast<unsigned long long>(gx.frame),guest.hostGameThread,static_cast<unsigned>(infoResult),
                    infoResult>=0?mainInfo.status:0u,infoResult>=0?mainInfo.waitType:0u,
                    infoResult>=0?mainInfo.waitId:0,infoResult>=0?mainInfo.currentCpuId:-1,
                    static_cast<unsigned long long>(currentClocks),static_cast<unsigned long long>(elapsedClocks),
                    static_cast<unsigned long long>(guest.selectCalls),static_cast<unsigned long long>(guest.idlePolls),
                    static_cast<unsigned long long>(guest.viPolls),static_cast<unsigned long long>(guest.viRetraces),
                    static_cast<unsigned long long>(guest.contextSwitches),static_cast<unsigned long long>(guest.sleepCalls),
                    static_cast<unsigned long long>(guest.wakeCalls),static_cast<unsigned long long>(guest.resumeCalls),
                    static_cast<unsigned long long>(guest.messageReceives),static_cast<unsigned long long>(guest.messageSends),
                    static_cast<unsigned long long>(guest.messageWaits),static_cast<unsigned long long>(guest.threadCreates),
                    static_cast<unsigned long long>(guest.thpTaskRuns),static_cast<unsigned long long>(guest.thpTaskCallbacks),
                    static_cast<unsigned long long>(guest.thpTaskCompleted),guest.taskPhase,
                    guest.lastRunning,guest.lastSelected,guest.lastSleeping,guest.lastWaitQueue,
                    guest.lastMessageQueue,guest.lastCreatedThread,guest.lastCreatedEntry,
                    guest.lastResumedThread,guest.lastTaskCallback);
                WicPsarcProgress psarc{};
                WicReadPsarcProgress(&psarc);
                std::fprintf(stderr,
                    "[psarc-stall] frame=%llu submitted=%llu completed=%llu state=%u active_op=%u\n",
                    static_cast<unsigned long long>(gx.frame),
                    static_cast<unsigned long long>(psarc.submitted),
                    static_cast<unsigned long long>(psarc.completed),psarc.state,psarc.activeOp);
            }
        }
    }
    return 0;
}
AudioPushResult enqueue(const int16_t* data,size_t samples,float gain) {
    std::lock_guard<AudioSpinMutex> lock(queueMutex);
    if(stopped.load(std::memory_order_acquire)) { ++rejectedBlocks;return AudioPushResult::BackendUnavailable; }
    if(samples>ring.size()-count) { ++rejectedBlocks;return AudioPushResult::QueueFull; }
    for(size_t i=0;i<samples;++i)ring[(head+count+i)%ring.size()]=
        static_cast<int16_t>(std::clamp(data[i]*gain,-32768.f,32767.f));
    count+=samples;acceptedSamples+=samples;return AudioPushResult::Accepted;
}
}
void WicLogAudioHealth() {
    uint64_t accepted=0,consumed=0,underflow=0,rejected=0;
    size_t queued=0;
    {
        std::lock_guard<AudioSpinMutex> lock(queueMutex);
        accepted=acceptedSamples;consumed=consumedSamples;underflow=underruns;
        rejected=rejectedBlocks;queued=count;
    }
    std::fprintf(stderr,"[audio-health] accepted_samples=%llu consumed_samples=%llu queued_samples=%u underruns=%llu rejected_blocks=%llu output_errors=%u\n",
        static_cast<unsigned long long>(accepted),static_cast<unsigned long long>(consumed),unsigned(queued),
        static_cast<unsigned long long>(underflow),static_cast<unsigned long long>(rejected),
        unsigned(outputErrors.load(std::memory_order_relaxed)));
}
AudioBackend& AudioBackend::Instance() { static AudioBackend instance;return instance; }
bool AudioBackend::Init(uint32_t rate,uint32_t channels) {
    std::lock_guard<std::mutex> lock(m_mutex);return EnsureInitializedLocked(rate,channels);
}
bool AudioBackend::EnsureInitializedLocked(uint32_t rate,uint32_t channels) {
    if(m_initialized)return rate==m_sampleRate && channels==m_channels;
    if(channels!=2 || (rate!=32000 && rate!=48000)) {
        std::fprintf(stderr,"[audio-vita] invalid format rate=%u channels=%u\n",rate,channels);
        return false;
    }
    port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,256,rate,SCE_AUDIO_OUT_MODE_STEREO);
    if(port<0) {
        std::fprintf(stderr,"[audio-vita] sceAudioOutOpenPort failed=0x%08x rate=%u\n",
            static_cast<unsigned int>(port),rate);
        return false;
    }
    std::fprintf(stderr,"[audio-vita] port opened id=%d rate=%u\n",port,rate);
    m_sampleRate=rate;m_channels=channels;
    stopped.store(false,std::memory_order_release);
    workerThread=sceKernelCreateThread("wic_audio",drainThread,0x10000100,64*1024,0,0,nullptr);
    if(workerThread<0) {
        std::fprintf(stderr,"[audio-vita] sceKernelCreateThread failed=0x%08x\n",
            static_cast<unsigned int>(workerThread));
        stopped.store(true,std::memory_order_release);sceAudioOutReleasePort(port);port=-1;return false;
    }
    const int startResult=sceKernelStartThread(workerThread,0,nullptr);
    if(startResult<0) {
        std::fprintf(stderr,"[audio-vita] sceKernelStartThread failed=0x%08x\n",
            static_cast<unsigned int>(startResult));
        stopped.store(true,std::memory_order_release);sceKernelDeleteThread(workerThread);workerThread=-1;
        sceAudioOutReleasePort(port);port=-1;return false;
    }
    std::fprintf(stderr,"[audio-vita] worker started uid=%d\n",workerThread);
    m_initialized=true;return true;
}
void AudioBackend::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);stopped.store(true,std::memory_order_release);
    if(workerThread>=0) {
        int status=0;
        sceKernelWaitThreadEnd(workerThread,&status,nullptr);
        sceKernelDeleteThread(workerThread);
        workerThread=-1;
    }
    if(port>=0)sceAudioOutReleasePort(port);
    port=-1;m_initialized=false;
    std::lock_guard<AudioSpinMutex> queueLock(queueMutex);head=count=0;
}
void AudioBackend::SetMasterVolume(float volume) { std::lock_guard<std::mutex> lock(m_mutex);m_masterVolume=std::clamp(volume,0.f,1.f); }
void AudioBackend::SetMuted(bool muted) { std::lock_guard<std::mutex> lock(m_mutex);m_muted=muted; }
AudioPushResult AudioBackend::PushSamplesLE16(const int16_t* samples,size_t length) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(!samples || !length || (length&1))return AudioPushResult::InvalidBuffer;
    if(!EnsureInitializedLocked(m_initialized ? m_sampleRate : 32000,2))return AudioPushResult::BackendUnavailable;
    return enqueue(samples,length,EffectiveGainLocked());
}
AudioPushResult AudioBackend::PushWiiAiSamplesBE16(const uint8_t* data,size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(!data || !bytes || (bytes&3))return AudioPushResult::InvalidBuffer;
    if(!EnsureInitializedLocked(m_initialized ? m_sampleRate : 32000,2))return AudioPushResult::BackendUnavailable;
    if(bytes/2>ring.size())return AudioPushResult::QueueFull;
    m_convertBuffer.resize(bytes/2);
    for(size_t i=0;i<bytes;i+=4) {
        m_convertBuffer[i/2]=static_cast<int16_t>((uint16_t(data[i+2])<<8)|data[i+3]);
        m_convertBuffer[i/2+1]=static_cast<int16_t>((uint16_t(data[i])<<8)|data[i+1]);
    }
    return enqueue(m_convertBuffer.data(),m_convertBuffer.size(),EffectiveGainLocked());
}
float AudioBackend::EffectiveGainLocked() const { return m_muted?0:m_masterVolume; }
void AudioBackend::ApplyGainLocked() {}
uint32_t AudioBackend::QueueLimitBytesLocked() const { return ring.size()*2; }
bool AudioBackend::QueueHasCapacityLocked(int bytes) { std::lock_guard<AudioSpinMutex> lock(queueMutex);return bytes>=0 && size_t(bytes/2)<=ring.size()-count; }
