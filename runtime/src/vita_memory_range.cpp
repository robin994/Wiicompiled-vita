#include "memory_access.h"

#if defined(MKW_TARGET_VITA) && defined(WIC_FAST_RAM_RANGES) && WIC_FAST_RAM_RANGES
namespace MemoryInline {
// Vita cannot reserve a flat 4 GiB view. Prove the entire range against the
// authoritative mappings and policy tables once, then use the existing bulk
// stores. The translator ends range lifetimes at calls and other barriers.
// Read ranges deliberately retain the checked path: an intervening FIFO store
// can create a deferred EFB read while a resolved host pointer is still live.
uint8_t* ResolveCheckedVitaRange(
    uint32_t start, uint32_t length, bool needsRead, bool needsWrite) {
    if (needsRead) return nullptr;
    const uint32_t end = start + length - 1;
    if (IsMmioAddress(start) || IsMmioAddress(end)) return nullptr;
    const uint32_t firstPage = start >> kPageShift, lastPage = end >> kPageShift;
    // A CPU write must observe the earlier GPU copy first. Returning a direct
    // bulk pointer here would allow a later lazy read to overwrite new bytes.
    if(needsWrite && (g_deferredReadCoveredPages[firstPage] || g_deferredReadCoveredPages[lastPage])) return nullptr;
    const auto& first = g_pageTable[firstPage];
    const uint32_t offset = start & kPageMask;
    if (!first.base) return nullptr;
    if (firstPage == lastPage) {
        if (offset + length > first.limit) return nullptr;
    } else {
        // length <= 1 MiB: at most two coarse pages, both fully covered here.
        const auto& last = g_pageTable[lastPage];
        if (first.limit != kPageSize || !last.base ||
            reinterpret_cast<uintptr_t>(last.base) !=
                reinterpret_cast<uintptr_t>(first.base) + kPageSize ||
            (end & kPageMask) >= last.limit) return nullptr;
    }
    if (needsWrite && RecompMod::ExecutableWriteGuardMayHit(start, length)) return nullptr;
    return first.base + offset;
}
} // namespace MemoryInline
#endif
