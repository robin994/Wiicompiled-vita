#include "audio_backend.h"

#include <algorithm>

// First-boot Vita audio sink. The Wii AX side may already run on USER_2, but
// the desktop AudioBackend is SDL3-specific. Keep guest-visible audio calls
// successful and non-blocking until the native SceAudioOut sink is introduced;
// this prevents host audio plumbing from masking earlier CPU/HLE/DVD faults.

AudioBackend& AudioBackend::Instance() {
    static AudioBackend instance;
    return instance;
}

float AudioBackend::EffectiveGainLocked() const {
    return m_muted ? 0.0f : m_masterVolume;
}

void AudioBackend::ApplyGainLocked() {
    // No host device in the first-boot sink.
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
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_initialized = true;
    return true;
}

bool AudioBackend::Init(uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return EnsureInitializedLocked(sampleRate, channels);
}

void AudioBackend::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stream = nullptr;
    m_initialized = false;
    m_sampleRate = 0;
    m_channels = 0;
    m_convertBuffer.clear();
}

uint32_t AudioBackend::QueueLimitBytesLocked() const {
    return 0;
}

bool AudioBackend::QueueHasCapacityLocked(int) {
    return m_initialized;
}

bool AudioBackend::PushWiiAiSamplesBE16(const uint8_t* data, size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_initialized && data != nullptr && bytes != 0;
}

bool AudioBackend::PushSamplesLE16(const int16_t* samples, size_t sampleCount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_initialized && samples != nullptr && sampleCount != 0;
}
