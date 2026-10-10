#pragma once
#include <cstddef>
#include <cstdint>

namespace wiicompiled {
// Physical, cached and uncached Wii RAM addresses share one host allocation.
// This maps RAM only: MMIO, FIFO, overlays and deferred policy stay in the HLE.
struct RamMirrors {
    uint8_t* mem1 = nullptr;
    uint8_t* mem2 = nullptr;
    size_t mem1Size = 0, mem2Size = 0;

    uint8_t* pointer(uint32_t address, size_t length) const noexcept {
        const uint32_t view = address & 0xc0000000u;
        if (view != 0 && view != 0x80000000u && view != 0xc0000000u) return nullptr;
        const uint32_t physical = address & 0x3fffffffu;
        if (physical < 0x02000000u) {
            if (mem1 && physical <= mem1Size && length <= mem1Size - physical) return mem1 + physical;
        } else if (physical >= 0x10000000u && physical < 0x14000000u) {
            const uint32_t offset=physical-0x10000000u;
            if (mem2 && offset <= mem2Size && length <= mem2Size-offset) return mem2+offset;
        }
        return nullptr;
    }
};
} // namespace wiicompiled
