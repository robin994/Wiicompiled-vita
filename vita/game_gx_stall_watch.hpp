#pragma once

#include <cstdint>

// Lock-free diagnostics. The audio output thread reads this only after the
// video producer has stopped completing frames; it never waits for GX.
struct WicGxProgress {
    uint64_t frame=0, submitted=0, completed=0, activeSerial=0;
    uint32_t activeType=0, activeSegment=0, gxmOperation=0;
};

extern "C" void WicReadGxProgress(WicGxProgress* progress) noexcept;
