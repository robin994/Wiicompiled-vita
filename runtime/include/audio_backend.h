#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <SDL3/SDL_audio.h>

struct AudioBackendMetrics {
    uint64_t realOutputChunks = 0;
    uint64_t silenceOutputChunks = 0;
    uint64_t underrunChunks = 0;
    uint64_t droppedChunks = 0;
    uint64_t queueHighWaterChunks = 0;
    uint64_t queuedChunks = 0;
    uint64_t queuedBytes = 0;
    uint64_t stagingSamples = 0;
};

class AudioBackend {
public:
    static AudioBackend& Instance();

    bool Init(uint32_t sampleRate, uint32_t channels);
    void Shutdown();

    // Wii AI DMA frames are big-endian and ordered right, left. SDL expects
    // native-endian interleaved left, right samples.
    bool PushWiiAiSamplesBE16(const uint8_t* data, size_t bytes);
    bool PushSamplesLE16(const int16_t* samples, size_t sampleCount);

    // Applied to the final host output, covering both AX and direct AI DMA.
    void SetMasterVolume(float volume);
    void SetMuted(bool muted);

    // Snapshot-only diagnostics. No queue mutation and no guest-visible side effects.
    AudioBackendMetrics GetMetrics() const;

private:
    AudioBackend() = default;
    ~AudioBackend() = default;
    AudioBackend(const AudioBackend&) = delete;
    AudioBackend& operator=(const AudioBackend&) = delete;

    bool EnsureInitializedLocked(uint32_t sampleRate, uint32_t channels);
    bool QueueHasCapacityLocked(int incomingBytes);
    uint32_t QueueLimitBytesLocked() const;
    float EffectiveGainLocked() const;
    void ApplyGainLocked();

    mutable std::mutex m_mutex;
    SDL_AudioStream* m_stream = nullptr;
    SDL_AudioSpec m_spec{};
    uint32_t m_sampleRate = 0;
    uint32_t m_channels = 0;
    bool m_initialized = false;
    float m_masterVolume = 1.0f;
    bool m_muted = false;
    bool m_reportedDroppedBlock = false;
    std::vector<int16_t> m_convertBuffer;
};
