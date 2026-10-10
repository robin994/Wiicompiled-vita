#pragma once
#include <cstdint>
namespace wiicompiled::vita::assets {
bool InitializeAudio() noexcept;
void ShutdownAudio() noexcept;
}
// Called by the native AX mixer. No guest state is changed by this lookup.
extern "C" unsigned WicNativeAudioFrame(const uint8_t* source,const int16_t* coefficients,
    int16_t yn1,int16_t yn2,unsigned nibbleOffset,unsigned predictorScale,
    int16_t* pcm,uint8_t* nibbles) noexcept;
