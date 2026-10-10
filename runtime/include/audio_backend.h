#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <SDL3/SDL_audio.h>

// A full output queue is a transient drop, not a failed guest DMA read.
enum class AudioPushResult : uint8_t {
    Accepted,
    QueueFull,
    InvalidBuffer,
    BackendUnavailable,
};

class AudioBackend {
public:
    static AudioBackend& Instance();

    bool Init(uint32_t sampleRate, uint32_t channels);
    void Shutdown();

    // Wii AI DMA frames are big-endian and ordered right, left. SDL expects
    // native-endian interleaved left, right samples.
    AudioPushResult PushWiiAiSamplesBE16(const uint8_t* data, size_t bytes);
    AudioPushResult PushSamplesLE16(const int16_t* samples, size_t sampleCount);

    // Applied to the final host output, covering both AX and direct AI DMA.
    void SetMasterVolume(float volume);
    void SetMuted(bool muted);

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
