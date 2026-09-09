#include "audio_backend.h"
#include "runtime_log.h"
#include "wiicompiled_vita/host_thread.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <thread>
#include <vector>

#ifndef MKW_VITA_NATIVE_AUDIOOUT
#define MKW_VITA_NATIVE_AUDIOOUT 0
#endif

#ifndef MKW_VITA_AUDIO_PACING
#define MKW_VITA_AUDIO_PACING 0
#endif

#if MKW_VITA_NATIVE_AUDIOOUT
#include <psp2/audioout.h>
#endif

namespace {

#if MKW_VITA_NATIVE_AUDIOOUT

constexpr uint32_t kNativeOutputRate = 48000;
constexpr size_t kNativeChannels = 2;
constexpr size_t kNativeChunkFrames = 256; // SceAudioOut requires a multiple of 64.
constexpr size_t kNativeChunkSamples = kNativeChunkFrames * kNativeChannels;
constexpr size_t kNativeMaxQueuedChunks = 8; // ~43 ms at 48 kHz.
constexpr size_t kNativeDeclickFrames = 64;

struct VitaAudioSinkState {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::array<int16_t, kNativeChunkSamples>> ready;
    std::vector<int16_t> staging;
    size_t stagingOffset = 0;
    std::thread worker;
    int port = -1;
    bool running = false;
    bool stop = false;
    bool loggedFirstOutput = false;
    bool loggedOutputError = false;
    bool loggedDrop = false;
    bool loggedUnderrun = false;
    bool discontinuityPending = false;
    uint64_t droppedChunks = 0;
    uint64_t underrunChunks = 0;
    uint64_t realOutputChunks = 0;
    uint64_t silenceOutputChunks = 0;
    size_t queueHighWater = 0;
};

// The process owns the audio port for the app lifetime. Deliberately avoid a
// static std::thread destructor: Vita terminates the process directly on app
// exit, while explicit AudioBackend::Shutdown still performs a clean join.
VitaAudioSinkState& NativeSink() {
    static VitaAudioSinkState* state = new VitaAudioSinkState();
    return *state;
}

void NativeAudioWorker(int port) {
    WiiCompiledVita::ConfigureCurrentThread(WiiCompiledVita::HostThreadRole::Audio);
    auto& state = NativeSink();
    bool playbackStarted = false;
    bool previousWasSilence = false;
    int16_t lastLeft = 0;
    int16_t lastRight = 0;
    for (;;) {
        std::array<int16_t, kNativeChunkSamples> chunk{};
        bool realPcm = false;
        bool discontinuity = false;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
#if MKW_VITA_AUDIO_PACING
            if (!playbackStarted) {
                state.wake.wait(lock, [&] { return state.stop || !state.ready.empty(); });
            }
            if (state.stop) {
                break;
            }
            if (!state.ready.empty()) {
                chunk = state.ready.front();
                state.ready.pop_front();
                realPcm = true;
                playbackStarted = true;
                discontinuity = state.discontinuityPending || previousWasSilence;
                state.discontinuityPending = false;
            } else if (playbackStarted) {
                ++state.underrunChunks;
                if (!state.loggedUnderrun) {
                    state.loggedUnderrun = true;
                    RT_LOGF(RT_TAG_AUDIO,
                            "vita_audioout underrun inserting_silence frames=%u\n",
                            static_cast<unsigned>(kNativeChunkFrames));
                }
            } else {
                continue;
            }
#else
            state.wake.wait(lock, [&] { return state.stop || !state.ready.empty(); });
            if (state.stop && state.ready.empty()) {
                break;
            }
            chunk = state.ready.front();
            state.ready.pop_front();
            realPcm = true;
#endif
        }

#if MKW_VITA_AUDIO_PACING
        if (realPcm && discontinuity) {
            for (size_t frame = 0; frame < kNativeDeclickFrames; ++frame) {
                const int32_t weight = static_cast<int32_t>(frame + 1);
                const int32_t inv = static_cast<int32_t>(kNativeDeclickFrames - frame - 1);
                const size_t base = frame * kNativeChannels;
                chunk[base] = static_cast<int16_t>(
                    (static_cast<int32_t>(chunk[base]) * weight +
                     static_cast<int32_t>(lastLeft) * inv) /
                    static_cast<int32_t>(kNativeDeclickFrames));
                chunk[base + 1] = static_cast<int16_t>(
                    (static_cast<int32_t>(chunk[base + 1]) * weight +
                     static_cast<int32_t>(lastRight) * inv) /
                    static_cast<int32_t>(kNativeDeclickFrames));
            }
        } else if (!realPcm && !previousWasSilence) {
            for (size_t frame = 0; frame < kNativeDeclickFrames; ++frame) {
                const int32_t inv = static_cast<int32_t>(kNativeDeclickFrames - frame - 1);
                const size_t base = frame * kNativeChannels;
                chunk[base] = static_cast<int16_t>(
                    static_cast<int32_t>(lastLeft) * inv /
                    static_cast<int32_t>(kNativeDeclickFrames));
                chunk[base + 1] = static_cast<int16_t>(
                    static_cast<int32_t>(lastRight) * inv /
                    static_cast<int32_t>(kNativeDeclickFrames));
            }
        }
#endif

        const int rc = sceAudioOutOutput(port, chunk.data());
        if (rc < 0) {
            if (!state.loggedOutputError) {
                state.loggedOutputError = true;
                RT_LOGF(RT_TAG_AUDIO, "vita_audioout output_failed rc=0x%08X\n",
                        static_cast<unsigned>(rc));
            }
            continue;
        }
        if (!state.loggedFirstOutput) {
            state.loggedFirstOutput = true;
            RT_LOGF(RT_TAG_AUDIO,
                    "vita_audioout first_output port=%d frames=%u rate=%u\n",
                    port, static_cast<unsigned>(kNativeChunkFrames),
                    static_cast<unsigned>(kNativeOutputRate));
        }
#if MKW_VITA_AUDIO_PACING
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (realPcm) {
                ++state.realOutputChunks;
            } else {
                ++state.silenceOutputChunks;
            }
            const uint64_t total = state.realOutputChunks + state.silenceOutputChunks;
            if ((total & 511u) == 0u) {
                RT_LOGF(RT_TAG_AUDIO,
                        "vita_audioout stats real=%llu silence=%llu underrun=%llu drop_oldest=%llu high_water=%u queued=%u\n",
                        static_cast<unsigned long long>(state.realOutputChunks),
                        static_cast<unsigned long long>(state.silenceOutputChunks),
                        static_cast<unsigned long long>(state.underrunChunks),
                        static_cast<unsigned long long>(state.droppedChunks),
                        static_cast<unsigned>(state.queueHighWater),
                        static_cast<unsigned>(state.ready.size()));
            }
        }
        if (realPcm) {
            lastLeft = chunk[kNativeChunkSamples - 2];
            lastRight = chunk[kNativeChunkSamples - 1];
            previousWasSilence = false;
        } else {
            lastLeft = 0;
            lastRight = 0;
            previousWasSilence = true;
        }
#endif
    }
}

bool StartNativeAudioSink() {
    auto& state = NativeSink();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.running) {
        return true;
    }

    const int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                                         static_cast<int>(kNativeChunkFrames),
                                         static_cast<int>(kNativeOutputRate),
                                         SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        RT_LOGF(RT_TAG_AUDIO, "vita_audioout open_failed rc=0x%08X\n",
                static_cast<unsigned>(port));
        return false;
    }

    state.port = port;
    state.stop = false;
    state.running = true;
    state.loggedFirstOutput = false;
    state.loggedOutputError = false;
    state.loggedDrop = false;
    state.loggedUnderrun = false;
    state.discontinuityPending = false;
    state.droppedChunks = 0;
    state.underrunChunks = 0;
    state.realOutputChunks = 0;
    state.silenceOutputChunks = 0;
    state.queueHighWater = 0;
    state.ready.clear();
    state.staging.clear();
    state.stagingOffset = 0;
    state.staging.reserve(kNativeChunkSamples * 2);
    try {
        state.worker = std::thread(NativeAudioWorker, port);
    } catch (...) {
        state.running = false;
        state.port = -1;
        sceAudioOutReleasePort(port);
        RT_LOGF(RT_TAG_AUDIO, "vita_audioout worker_start_failed\n");
        return false;
    }

    RT_LOGF(RT_TAG_AUDIO,
            "vita_audioout opened port=%d type=main frames=%u rate=%u queue_chunks=%u\n",
            port, static_cast<unsigned>(kNativeChunkFrames),
            static_cast<unsigned>(kNativeOutputRate),
            static_cast<unsigned>(kNativeMaxQueuedChunks));
    return true;
}

void StopNativeAudioSink() {
    auto& state = NativeSink();
    int port = -1;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.running) {
            return;
        }
        state.stop = true;
        state.ready.clear();
        state.staging.clear();
        state.stagingOffset = 0;
        port = state.port;
    }
    state.wake.notify_all();
    if (state.worker.joinable()) {
        state.worker.join();
    }
    if (port >= 0) {
        sceAudioOutReleasePort(port);
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.running = false;
        state.stop = false;
        state.port = -1;
    }
}

void SetNativeAudioVolume(float gain) {
    auto& state = NativeSink();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.running || state.port < 0) {
        return;
    }
    const int value = static_cast<int>(std::clamp(gain, 0.0f, 1.0f) *
                                       static_cast<float>(SCE_AUDIO_OUT_MAX_VOL));
    int volume[2] = {value, value};
    const auto channels = static_cast<SceAudioOutChannelFlag>(
        SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH);
    const int rc = sceAudioOutSetVolume(state.port, channels, volume);
    if (rc < 0) {
        RT_LOGF(RT_TAG_AUDIO, "vita_audioout volume_failed rc=0x%08X\n",
                static_cast<unsigned>(rc));
    }
}

bool QueueNativePcm48(const int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) {
        return false;
    }
    auto& state = NativeSink();
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.running || state.stop) {
            return false;
        }
        state.staging.insert(state.staging.end(), samples, samples + sampleCount);
        while (state.staging.size() - state.stagingOffset >= kNativeChunkSamples) {
            if (state.ready.size() < kNativeMaxQueuedChunks) {
                std::array<int16_t, kNativeChunkSamples> chunk{};
                std::copy_n(state.staging.data() + state.stagingOffset,
                            kNativeChunkSamples, chunk.data());
                state.ready.push_back(std::move(chunk));
                state.queueHighWater = std::max(state.queueHighWater, state.ready.size());
                queued = true;
            } else {
#if MKW_VITA_AUDIO_PACING
                state.ready.pop_front();
                std::array<int16_t, kNativeChunkSamples> chunk{};
                std::copy_n(state.staging.data() + state.stagingOffset,
                            kNativeChunkSamples, chunk.data());
                state.ready.push_back(std::move(chunk));
                state.discontinuityPending = true;
                queued = true;
#endif
                ++state.droppedChunks;
                if (!state.loggedDrop) {
                    state.loggedDrop = true;
#if MKW_VITA_AUDIO_PACING
                    RT_LOGF(RT_TAG_AUDIO,
                            "vita_audioout queue_full dropping_oldest_pcm chunks=%u\n",
                            static_cast<unsigned>(kNativeMaxQueuedChunks));
#else
                    RT_LOGF(RT_TAG_AUDIO,
                            "vita_audioout queue_full dropping_new_pcm chunks=%u\n",
                            static_cast<unsigned>(kNativeMaxQueuedChunks));
#endif
                }
            }
            state.stagingOffset += kNativeChunkSamples;
        }
        if (state.stagingOffset == state.staging.size()) {
            state.staging.clear();
            state.stagingOffset = 0;
        } else if (state.stagingOffset >= kNativeChunkSamples * 4) {
            state.staging.erase(state.staging.begin(),
                                state.staging.begin() + static_cast<std::ptrdiff_t>(state.stagingOffset));
            state.stagingOffset = 0;
        }
    }
    if (queued) {
        state.wake.notify_one();
    }
    // A full native FIFO is not a guest-visible DMA failure. Like the desktop
    // backend, drop new PCM while preserving AI/AX callback progress.
    return true;
}

bool QueueNativeStereoAtRate(const int16_t* samples, size_t sampleCount,
                             uint32_t sampleRate) {
    if (!samples || sampleCount < kNativeChannels || sampleRate == 0) {
        return false;
    }
    const size_t inputFrames = sampleCount / kNativeChannels;
    if (sampleRate == kNativeOutputRate) {
        return QueueNativePcm48(samples, inputFrames * kNativeChannels);
    }

    // Wii AI normally runs at 32 kHz or 48 kHz. A block-local linear resampler
    // keeps the native port fixed at the MAIN-port mandated 48 kHz without ever
    // blocking USER_0 in sceAudioOutOutput. The observed 96-frame/32-kHz DMA
    // maps exactly to 144 frames, so no fractional phase is lost at that path.
    const size_t outputFrames = std::max<size_t>(
        1, (inputFrames * static_cast<uint64_t>(kNativeOutputRate) + sampleRate / 2u) /
               sampleRate);
    static thread_local std::vector<int16_t> resampled;
    resampled.resize(outputFrames * kNativeChannels);
    for (size_t out = 0; out < outputFrames; ++out) {
        const uint64_t position = static_cast<uint64_t>(out) * sampleRate;
        size_t first = static_cast<size_t>(position / kNativeOutputRate);
        uint32_t frac = static_cast<uint32_t>(position % kNativeOutputRate);
        if (first >= inputFrames) {
            first = inputFrames - 1;
            frac = 0;
        }
        const size_t second = std::min(first + 1, inputFrames - 1);
        for (size_t ch = 0; ch < kNativeChannels; ++ch) {
            const int32_t a = samples[first * kNativeChannels + ch];
            const int32_t b = samples[second * kNativeChannels + ch];
            const int64_t mixed = static_cast<int64_t>(a) * (kNativeOutputRate - frac) +
                                  static_cast<int64_t>(b) * frac;
            resampled[out * kNativeChannels + ch] =
                static_cast<int16_t>(mixed / kNativeOutputRate);
        }
    }
    return QueueNativePcm48(resampled.data(), resampled.size());
}

#endif

} // namespace

AudioBackend& AudioBackend::Instance() {
    static AudioBackend instance;
    return instance;
}

float AudioBackend::EffectiveGainLocked() const {
    return m_muted ? 0.0f : m_masterVolume;
}

void AudioBackend::ApplyGainLocked() {
#if MKW_VITA_NATIVE_AUDIOOUT
    SetNativeAudioVolume(EffectiveGainLocked());
#endif
}

void AudioBackend::SetMasterVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
}

void AudioBackend::SetMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
}

bool AudioBackend::EnsureInitializedLocked(uint32_t sampleRate, uint32_t channels) {
    if (sampleRate == 0 || channels == 0) {
        return false;
    }
#if MKW_VITA_NATIVE_AUDIOOUT
    if (channels != kNativeChannels) {
        RT_LOGF(RT_TAG_AUDIO, "vita_audioout unsupported_channels=%u\n",
                static_cast<unsigned>(channels));
        return false;
    }
    if (!m_initialized && !StartNativeAudioSink()) {
        return false;
    }
#endif
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_initialized = true;
#if MKW_VITA_NATIVE_AUDIOOUT
    ApplyGainLocked();
#endif
    return true;
}

bool AudioBackend::Init(uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return EnsureInitializedLocked(sampleRate, channels);
}

void AudioBackend::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
#if MKW_VITA_NATIVE_AUDIOOUT
    StopNativeAudioSink();
#endif
    m_stream = nullptr;
    m_initialized = false;
    m_sampleRate = 0;
    m_channels = 0;
    m_convertBuffer.clear();
}

uint32_t AudioBackend::QueueLimitBytesLocked() const {
#if MKW_VITA_NATIVE_AUDIOOUT
    return static_cast<uint32_t>(kNativeMaxQueuedChunks * kNativeChunkSamples *
                                 sizeof(int16_t));
#else
    return 0;
#endif
}

bool AudioBackend::QueueHasCapacityLocked(int incomingBytes) {
#if MKW_VITA_NATIVE_AUDIOOUT
    auto& state = NativeSink();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.running || state.stop) {
        return false;
    }
    const uint64_t queuedBytes =
        static_cast<uint64_t>(state.ready.size()) * kNativeChunkSamples * sizeof(int16_t);
    return queuedBytes + static_cast<uint64_t>(std::max(incomingBytes, 0)) <=
           QueueLimitBytesLocked();
#else
    (void)incomingBytes;
    return m_initialized;
#endif
}

bool AudioBackend::PushWiiAiSamplesBE16(const uint8_t* data, size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || data == nullptr || bytes < 4) {
        return false;
    }
#if MKW_VITA_NATIVE_AUDIOOUT
    const size_t frameCount = bytes / 4;
    m_convertBuffer.resize(frameCount * 2);
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const size_t rightOffset = frame * 4;
        const size_t leftOffset = rightOffset + 2;
        const uint16_t right = static_cast<uint16_t>(data[rightOffset]) << 8 |
                               static_cast<uint16_t>(data[rightOffset + 1]);
        const uint16_t left = static_cast<uint16_t>(data[leftOffset]) << 8 |
                              static_cast<uint16_t>(data[leftOffset + 1]);
        m_convertBuffer[frame * 2] = static_cast<int16_t>(left);
        m_convertBuffer[frame * 2 + 1] = static_cast<int16_t>(right);
    }
    return QueueNativeStereoAtRate(m_convertBuffer.data(), m_convertBuffer.size(), m_sampleRate);
#else
    return true;
#endif
}

bool AudioBackend::PushSamplesLE16(const int16_t* samples, size_t sampleCount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || samples == nullptr || sampleCount < 2) {
        return false;
    }
#if MKW_VITA_NATIVE_AUDIOOUT
    return QueueNativeStereoAtRate(samples, sampleCount, m_sampleRate);
#else
    return true;
#endif
}
