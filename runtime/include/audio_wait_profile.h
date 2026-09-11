#pragma once

#if defined(MKW_TARGET_VITA) && MKW_VITA_AUDIO_WAIT_PROFILE
#include <array>
#include <cstdint>

// USER_0 only. Counts only Audio_HLE_Tick work nested in the render-wait
// wrapper, not scheduler/VI audio polls. Reset at every producer submission.
struct AudioWaitProfile {
    uint64_t polls = 0, ticks = 0, reentries = 0, blocks = 0, capped = 0;
    uint32_t blockBudget = 0;
    std::array<uint64_t, 4> calls{}, totalUs{}, maxUs{}; // join, sink, AI, AX
    uint64_t backlogMaxUs = 0, backlogLastUs = 0;
    uint32_t callback = 0, length = 0, sampleRate = 0;
#if MKW_VITA_AUDIO_AI_PROFILE
    std::array<uint64_t, 2> aiCalls{}, aiTotalUs{}, aiMaxUs{}; // cache, DSP mail
    uint32_t thpChain = 0, thpMode = 0, thpOpen = 0, thpFlags = 0;
    uint64_t thpSnapshots = 0, thpReadFailures = 0;
#endif
};
#if MKW_VITA_AUDIO_AI_PROFILE
// Inclusive native child timing, active only inside a profiled AI callback.
// Thread-local gating excludes mix-worker activity and unrelated GX flushes.
class AudioAiSubtimer {
public:
    explicit AudioAiSubtimer(unsigned stage) noexcept;
    ~AudioAiSubtimer();
    AudioAiSubtimer(const AudioAiSubtimer&) = delete;
    AudioAiSubtimer& operator=(const AudioAiSubtimer&) = delete;
private:
    unsigned stage_;
    bool active_;
    uint64_t beginUs_;
};
#endif
void Audio_HLE_PollDeferredForRenderWait();
AudioWaitProfile Audio_HLE_TakeWaitProfile() noexcept;
#endif
#if defined(MKW_TARGET_VITA) && MKW_VITA_LOADING_AUDIO_PROFILE
struct AudioRuntimeMetrics {
    uint64_t backlogClampCount = 0;
    uint64_t backlogDroppedUs = 0;
    uint64_t backlogUs = 0;
    uint32_t callback = 0;
    uint32_t length = 0;
    uint32_t sampleRate = 0;
    uint8_t enabled = 0;
    uint8_t tickActive = 0;
};
AudioRuntimeMetrics Audio_HLE_GetRuntimeMetrics() noexcept;
#endif
