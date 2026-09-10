#pragma once

// Per-granule write-generation counters let GX cache consumers skip re-digesting unchanged
// ranges. Assumes GPU visibility only happens via DCStoreRange/DCFlushRange/DCInvalidateRange,
// so hooking those plus DMA writers is a complete notification. Header-only: hot, must not
// link-depend on the GX HLE.

#include "memory.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#ifndef MKW_VITA_GUEST_WRITE_HIERARCHY
#define MKW_VITA_GUEST_WRITE_HIERARCHY 0
#endif

// Canonical MEM1/MEM2 physical address for any of the guest's cached, uncached
// or physical aliases. Lives here rather than in the GX HLE so the tracking
// table, its notifiers and the GX caches all agree on one address space.
inline uint32_t CanonicalizeGxMainRamAddress(uint32_t addr) noexcept {
    if (addr < 0x01800000u) {
        return addr;
    }
    if (addr >= Memory::kMem2PhysicalBase && addr < Memory::kMem2PhysicalEnd) {
        return addr;
    }
    if (addr >= 0x80000000u && addr < 0x81800000u) {
        return addr - 0x80000000u;
    }
    if (addr >= Memory::kMem2CachedBase && addr < Memory::kMem2CachedEnd) {
        return addr - 0x80000000u;
    }
    if (addr >= 0xC0000000u && addr < 0xC1800000u) {
        return addr - 0xC0000000u;
    }
    if (addr >= Memory::kMem2UncachedBase && addr < Memory::kMem2UncachedEnd) {
        return addr - 0xC0000000u;
    }
    return addr;
}

namespace GxGuestWrite {

// Granularity matches the GX resource caches' occupancy maps: 64 KiB, so a
// cacheable display list or texture spans only a handful of counters. A false
// bump only costs one re-digest, which is exactly the untracked behaviour.
inline constexpr uint32_t kGranuleShift = 16; // 64 KiB per granule
inline constexpr uint64_t kTrackedSpan = static_cast<uint64_t>(Memory::kMem2PhysicalEnd);
inline constexpr size_t kGranuleCount = static_cast<size_t>(kTrackedSpan >> kGranuleShift);
#if MKW_VITA_GUEST_WRITE_HIERARCHY
// P6.26: large GX arrays were validating hundreds of 64 KiB counters per raw-mesh
// cache hit. Keep the exact 64 KiB map for small/edge ranges and add a 1 MiB
// summary level for aligned interiors. Any write touching a 1 MiB block bumps its
// summary counter, so consumers retain the same conservative invalidation contract.
inline constexpr uint32_t kSuperGranuleShift = 20; // 1 MiB
inline constexpr size_t kGranulesPerSuper = size_t{1} << (kSuperGranuleShift - kGranuleShift);
inline constexpr size_t kSuperGranuleCount =
    static_cast<size_t>((kTrackedSpan + ((uint64_t{1} << kSuperGranuleShift) - 1u)) >>
                        kSuperGranuleShift);
#endif

// Folded value used for "this range is not covered by the granule map", which
// forces the consumer to recompute its digest on every call.
inline constexpr uint64_t kUntracked = ~0ull;

// Shared (not thread_local) because notifications come from the OS HLE and from
// aurora's readback callbacks while the caches themselves are per guest thread;
// relaxed ordering is enough, every access is a plain load or a lock-xadd on
// x86 and a missed-by-a-hair ordering only delays a re-digest by one call.
inline std::array<std::atomic<uint32_t>, kGranuleCount> g_generations{};
#if MKW_VITA_GUEST_WRITE_HIERARCHY
inline std::array<std::atomic<uint32_t>, kSuperGranuleCount> g_superGenerations{};
#endif

// Monotone fold of every granule counter covering [addr, addr + nbytes).
// Counters only ever increase, so the sum changes whenever any covered granule
// is bumped and can never alias back to a previously observed value.
inline uint64_t GenerationForRange(uint32_t addr, uint32_t nbytes) noexcept {
    if (nbytes == 0) {
        return kUntracked;
    }
    const uint64_t start = static_cast<uint64_t>(CanonicalizeGxMainRamAddress(addr));
    const uint64_t end = start + static_cast<uint64_t>(nbytes);
    if (end <= start || end > kTrackedSpan) {
        return kUntracked;
    }
    const size_t firstGranule = static_cast<size_t>(start >> kGranuleShift);
    const size_t lastGranule = static_cast<size_t>((end - 1) >> kGranuleShift);
    uint64_t folded = 0;
#if MKW_VITA_GUEST_WRITE_HIERARCHY
    size_t granule = firstGranule;
    // Preserve 64 KiB precision until the next 1 MiB boundary.
    while (granule <= lastGranule && (granule % kGranulesPerSuper) != 0u) {
        folded += g_generations[granule].load(std::memory_order_relaxed);
        ++granule;
    }
    // Fold complete 1 MiB spans through one atomic load each.
    while (granule <= lastGranule &&
           kGranulesPerSuper - 1u <= lastGranule - granule) {
        folded += g_superGenerations[granule / kGranulesPerSuper].load(
            std::memory_order_relaxed);
        granule += kGranulesPerSuper;
    }
    // Tail smaller than a complete 1 MiB block keeps 64 KiB precision.
    while (granule <= lastGranule) {
        folded += g_generations[granule].load(std::memory_order_relaxed);
        ++granule;
    }
#else
    for (size_t granule = firstGranule; granule <= lastGranule; ++granule) {
        folded += g_generations[granule].load(std::memory_order_relaxed);
    }
#endif
    // Never collide with the "untracked" sentinel.
    return folded == kUntracked ? folded - 1u : folded;
}

// Bumps every granule covering [addr, addr + size). Deliberately branch-light:
// this runs on every DC range op, including the ones that only ever touch
// memory no GX cache has ever looked at.
inline void NotifyWrite(uint32_t addr, uint32_t size) noexcept {
    if (size == 0) {
        return;
    }
    const uint64_t start = static_cast<uint64_t>(CanonicalizeGxMainRamAddress(addr));
    const uint64_t end = start + static_cast<uint64_t>(size);
    if (end <= start || start >= kTrackedSpan) {
        return;
    }
    const uint64_t clampedEnd = end < kTrackedSpan ? end : kTrackedSpan;
    const size_t firstGranule = static_cast<size_t>(start >> kGranuleShift);
    const size_t lastGranule = static_cast<size_t>((clampedEnd - 1) >> kGranuleShift);
    for (size_t granule = firstGranule; granule <= lastGranule; ++granule) {
        g_generations[granule].fetch_add(1u, std::memory_order_relaxed);
    }
#if MKW_VITA_GUEST_WRITE_HIERARCHY
    const size_t firstSuper = static_cast<size_t>(start >> kSuperGranuleShift);
    const size_t lastSuper = static_cast<size_t>((clampedEnd - 1) >> kSuperGranuleShift);
    for (size_t super = firstSuper; super <= lastSuper; ++super) {
        g_superGenerations[super].fetch_add(1u, std::memory_order_relaxed);
    }
#endif
}

// The skip contract shared by every consumer: a stored digest may be trusted
// only when the range is tracked at all and no notification has landed on it
// since the digest was taken.
inline bool CanSkipDigest(uint64_t storedGeneration, uint64_t currentGeneration) noexcept {
    return currentGeneration != kUntracked && storedGeneration == currentGeneration;
}

// Host-to-guest inversion for one contiguous guest RAM alias. Aurora holds host
// pointers only, so the generation and notification hooks it calls have to map
// them back before they can touch the table.
inline bool HostRangeToGuest(const void* hostBase, uint64_t hostSize, uint32_t guestBase,
                             const void* hostPtr, size_t size, uint32_t& outAddr) noexcept {
    if (hostBase == nullptr || hostPtr == nullptr || size == 0) {
        return false;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(hostBase);
    const uintptr_t ptr = reinterpret_cast<uintptr_t>(hostPtr);
    if (ptr < base) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(ptr - base);
    if (offset >= hostSize || static_cast<uint64_t>(size) > hostSize - offset) {
        return false;
    }
    outAddr = guestBase + static_cast<uint32_t>(offset);
    return true;
}

// Installs the generation/notification hooks into aurora. Called once after
// aurora_initialize; until then (and if guest RAM cannot be resolved) aurora
// digests its source bytes on every validation, which is the old behaviour.
// Defined in gx_guest_write_hooks.cpp.
void InstallAuroraHooks();

} // namespace GxGuestWrite
