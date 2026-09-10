#include "hle_stubs.h"

#include "memory.h"
#include "runtime_log.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define func_80124894 func_80124894_reference
#include "../../../generated/functions/func_80124894.cpp"
#undef func_80124894

namespace {

struct GuestTail {
    uint32_t base = 0;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

uint32_t ContiguousRamBytesFrom(uint32_t address) noexcept {
    const auto remaining = [address](uint32_t begin, uint32_t end) -> uint32_t {
        return address >= begin && address < end ? end - address : 0u;
    };
    if (const uint32_t bytes = remaining(Memory::kMem1PhysicalBase,
            Memory::kMem1PhysicalBase + static_cast<uint32_t>(Memory::kMem1Size)); bytes) return bytes;
    if (const uint32_t bytes = remaining(Memory::kMem1CachedBase,
            Memory::kMem1CachedBase + static_cast<uint32_t>(Memory::kMem1Size)); bytes) return bytes;
    if (const uint32_t bytes = remaining(Memory::kMem1UncachedBase,
            Memory::kMem1UncachedBase + static_cast<uint32_t>(Memory::kMem1Size)); bytes) return bytes;
    if (const uint32_t bytes = remaining(Memory::kMem2PhysicalBase, Memory::kMem2PhysicalEnd); bytes) return bytes;
    if (const uint32_t bytes = remaining(Memory::kMem2CachedBase, Memory::kMem2CachedEnd); bytes) return bytes;
    return remaining(Memory::kMem2UncachedBase, Memory::kMem2UncachedEnd);
}

GuestTail MapGuestTail(uint32_t address) noexcept {
    GuestTail out{address, nullptr, ContiguousRamBytesFrom(address)};
    if (out.size == 0) return out;
    try { out.data = Memory::GetPointer(address, out.size); } catch (...) { out.data = nullptr; }
    return out;
}

uint8_t ReadGuestByte(const GuestTail& tail, uint32_t address) {
    if (address >= tail.base) {
        const uint32_t offset = address - tail.base;
        if (tail.data != nullptr && offset < tail.size) [[likely]] return tail.data[offset];
    }
    return MemoryInline::FlatRead8(address);
}

struct GuestCaseFold {
    GuestTail table;
    uint32_t tableAddress = 0;
};

GuestCaseFold ResolveGuestCaseFold() {
    constexpr uint32_t kCtypeLocaleSlot = 0x80271180u;
    const uint32_t locale = MemoryInline::FlatRead32(kCtypeLocaleSlot);
    const uint32_t tableAddress = MemoryInline::FlatRead32(locale + 16u);
    return {MapGuestTail(tableAddress), tableAddress};
}

int32_t FoldGuestSignedByte(uint8_t raw, const GuestCaseFold& fold) {
    const int32_t value = static_cast<int32_t>(static_cast<int8_t>(raw));
    if (value < 0) return value;
    const uint8_t mapped = ReadGuestByte(fold.table, fold.tableAddress + static_cast<uint32_t>(value));
    return static_cast<int32_t>(static_cast<int8_t>(mapped));
}

constexpr size_t kArcCacheSlots = 256u;
constexpr size_t kArcCacheMaxPath = 255u;

struct ArcCacheEntry {
    bool valid = false;
    uint32_t handle = 0;
    uint32_t fstBase = 0;
    uint32_t stringBase = 0;
    uint32_t current = 0;
    uint32_t pathAddress = 0;
    uint16_t pathLength = 0;
    uint64_t pathHash = 0;
    std::array<uint8_t, kArcCacheMaxPath> path{};
    uint32_t r0 = 0;
    std::array<uint32_t, 10> r3ToR12{};
    uint32_t cr = 0;
};

std::array<ArcCacheEntry, kArcCacheSlots> g_arcCache{};
uint64_t g_arcCacheCalls = 0;
uint64_t g_arcCacheHits = 0;
uint64_t g_arcCacheMisses = 0;
uint64_t g_arcCacheBypass = 0;

uint64_t HashByte(uint64_t hash, uint8_t value) noexcept {
    return (hash ^ value) * 1099511628211ull;
}

uint64_t HashU32(uint64_t hash, uint32_t value) noexcept {
    hash = HashByte(hash, static_cast<uint8_t>(value >> 24));
    hash = HashByte(hash, static_cast<uint8_t>(value >> 16));
    hash = HashByte(hash, static_cast<uint8_t>(value >> 8));
    return HashByte(hash, static_cast<uint8_t>(value));
}

bool CaptureGuestPath(uint32_t pathAddress, const GuestTail& tail,
                      std::array<uint8_t, kArcCacheMaxPath>& bytes,
                      uint16_t& length, uint64_t& hash) {
    hash = 1469598103934665603ull;
    for (size_t i = 0; i < kArcCacheMaxPath; ++i) {
        const uint8_t ch = ReadGuestByte(tail, pathAddress + static_cast<uint32_t>(i));
        if (ch == 0) {
            length = static_cast<uint16_t>(i);
            return true;
        }
        bytes[i] = ch;
        hash = HashByte(hash, ch);
    }
    if (ReadGuestByte(tail, pathAddress + static_cast<uint32_t>(kArcCacheMaxPath)) == 0) {
        length = static_cast<uint16_t>(kArcCacheMaxPath);
        return true;
    }
    return false;
}

void SaveArcEffects(ArcCacheEntry& entry, const CpuContext* ctx) noexcept {
    entry.r0 = ctx->gpr[0];
    for (uint32_t reg = 3; reg <= 12; ++reg) entry.r3ToR12[reg - 3] = ctx->gpr[reg];
    entry.cr = ctx->cr;
}

void RestoreArcEffects(const ArcCacheEntry& entry, CpuContext* ctx) noexcept {
    ctx->gpr[0] = entry.r0;
    for (uint32_t reg = 3; reg <= 12; ++reg) ctx->gpr[reg] = entry.r3ToR12[reg - 3];
    ctx->cr = entry.cr;
}

void MaybeLogArcCache() noexcept {
#if defined(MKW_TARGET_VITA)
    if (g_arcCacheCalls >= 64u && (g_arcCacheCalls & (g_arcCacheCalls - 1u)) == 0u) {
        RT_LOGF(RT_TAG_HLE,
                "arc_native_cache calls=%llu hits=%llu misses=%llu bypass=%llu hit_pct=%llu\n",
                static_cast<unsigned long long>(g_arcCacheCalls),
                static_cast<unsigned long long>(g_arcCacheHits),
                static_cast<unsigned long long>(g_arcCacheMisses),
                static_cast<unsigned long long>(g_arcCacheBypass),
                static_cast<unsigned long long>(g_arcCacheCalls == 0 ? 0 :
                    (g_arcCacheHits * 100u) / g_arcCacheCalls));
    }
#endif
}

void LogArcNativeOnce() noexcept {
#if defined(MKW_TARGET_VITA)
    static bool logged = false;
    if (!logged) {
        logged = true;
        RT_LOGF(RT_TAG_HLE, "arc_native_fast active=1 mode=oracle-cache stricmp=0x8001BBF8 convert_path=0x80124894\n");
    }
#endif
}

} // namespace

extern "C" int32_t RevolutionStricmpNative_8001bbf8(uint32_t lhsAddress, uint32_t rhsAddress) {
    LogArcNativeOnce();
    const GuestTail lhsTail = MapGuestTail(lhsAddress);
    const GuestTail rhsTail = MapGuestTail(rhsAddress);
    const GuestCaseFold fold = ResolveGuestCaseFold();
    uint32_t lhs = lhsAddress;
    uint32_t rhs = rhsAddress;
    for (;;) {
        const int32_t a = FoldGuestSignedByte(ReadGuestByte(lhsTail, lhs++), fold);
        const int32_t b = FoldGuestSignedByte(ReadGuestByte(rhsTail, rhs++), fold);
        if (a < b) return -1;
        if (a > b) return 1;
        if (a == 0) return 0;
    }
}
PPC_NATIVE_OVERRIDE(8001BBF8, RevolutionStricmpNative_8001bbf8, int32_t,
                    (uint32_t lhsAddress, uint32_t rhsAddress), (lhsAddress, rhsAddress));

extern "C" void ARCConvertPathToEntrynumNative_80124894(CpuContext* ctx) {
    LogArcNativeOnce();
    ++g_arcCacheCalls;

    const uint32_t handle = ctx->gpr[3];
    const uint32_t pathAddress = ctx->gpr[4];
    uint32_t fstBase = 0;
    uint32_t stringBase = 0;
    uint32_t current = 0;
    std::array<uint8_t, kArcCacheMaxPath> pathBytes{};
    uint16_t pathLength = 0;
    uint64_t pathHash = 0;
    bool cacheable = false;

    try {
        fstBase = MemoryInline::FlatRead32(handle + 4u);
        stringBase = MemoryInline::FlatRead32(handle + 16u);
        current = MemoryInline::FlatRead32(handle + 24u);
        cacheable = CaptureGuestPath(pathAddress, MapGuestTail(pathAddress),
                                     pathBytes, pathLength, pathHash);
    } catch (...) {
        cacheable = false;
    }

    uint64_t keyHash = pathHash;
    keyHash = HashU32(keyHash, handle);
    keyHash = HashU32(keyHash, fstBase);
    keyHash = HashU32(keyHash, stringBase);
    keyHash = HashU32(keyHash, current);
    keyHash = HashU32(keyHash, pathAddress);

    if (cacheable) {
        ArcCacheEntry& entry = g_arcCache[static_cast<size_t>(keyHash) & (kArcCacheSlots - 1u)];
        if (entry.valid && entry.handle == handle && entry.fstBase == fstBase &&
            entry.stringBase == stringBase && entry.current == current &&
            entry.pathAddress == pathAddress && entry.pathLength == pathLength &&
            entry.pathHash == pathHash &&
            std::memcmp(entry.path.data(), pathBytes.data(), pathLength) == 0) {
            ++g_arcCacheHits;
            RestoreArcEffects(entry, ctx);
            MaybeLogArcCache();
            return;
        }

        ++g_arcCacheMisses;
        func_80124894_reference(ctx);
        entry.valid = true;
        entry.handle = handle;
        entry.fstBase = fstBase;
        entry.stringBase = stringBase;
        entry.current = current;
        entry.pathAddress = pathAddress;
        entry.pathLength = pathLength;
        entry.pathHash = pathHash;
        if (pathLength != 0) std::memcpy(entry.path.data(), pathBytes.data(), pathLength);
        SaveArcEffects(entry, ctx);
        MaybeLogArcCache();
        return;
    }

    ++g_arcCacheBypass;
    func_80124894_reference(ctx);
    MaybeLogArcCache();
}
PPC_NATIVE_OVERRIDE_VOID(80124894, ARCConvertPathToEntrynumNative_80124894,
                         (CpuContext* ctx), (ctx));
