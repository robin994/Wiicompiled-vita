#include "guest_flat_memory.h"

#if defined(MKW_TARGET_VITA)

#include "runtime_log.h"

#include <psp2/kernel/sysmem.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace GuestFlat {
namespace {

constexpr size_t kAllocationAlignment = 4096;

struct Store {
    Backing backing = Backing::Owned;
    uint32_t ownedBase = 0;
    size_t size = 0;
    uint8_t* data = nullptr;
    SceUID memblock = -1;
};

struct Mapping {
    uint32_t guestBase = 0;
    uint64_t guestSize = 0;
    uint64_t storeOffset = 0;
    Store* store = nullptr;
};

std::mutex& StateMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<RegionRequest>& ActiveRegions() {
    static std::vector<RegionRequest> regions;
    return regions;
}

std::vector<Store>& Stores() {
    static std::vector<Store> stores;
    return stores;
}

std::vector<Mapping>& Mappings() {
    static std::vector<Mapping> mappings;
    return mappings;
}

bool& Active() {
    static bool active = false;
    return active;
}

uint64_t StoreOffsetFor(const RegionRequest& region) {
    switch (region.backing) {
    case Backing::Mem1:
        return region.base & 0x01FFFFFFu;
    case Backing::Mem2:
        return region.base & 0x0FFFFFFFu;
    case Backing::Owned:
    default:
        return 0;
    }
}

uint32_t OwnedBaseFor(const RegionRequest& region) {
    return region.backing == Backing::Owned ? region.base : 0;
}

bool SameStore(const Store& store, const RegionRequest& region) {
    return store.backing == region.backing && store.ownedBase == OwnedBaseFor(region);
}

bool SameLayout(const std::vector<RegionRequest>& lhs, const std::vector<RegionRequest>& rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].base != rhs[i].base || lhs[i].size != rhs[i].size ||
            lhs[i].backing != rhs[i].backing) {
            return false;
        }
    }
    return true;
}

size_t AlignUp(size_t value) {
    return (value + kAllocationAlignment - 1u) & ~(kAllocationAlignment - 1u);
}

void ZeroStores() {
    for (auto& store : Stores()) {
        if (store.data && store.size) std::memset(store.data, 0, store.size);
    }
}

void ReleaseStores() {
    for (auto& store : Stores()) {
        if (store.memblock >= 0) {
            sceKernelFreeMemBlock(store.memblock);
        } else {
            free(store.data);
        }
    }
    Stores().clear();
    Mappings().clear();
}

const char* BackingName(Backing backing) {
    switch (backing) {
    case Backing::Mem1:
        return "MEM1";
    case Backing::Mem2:
        return "MEM2";
    case Backing::Owned:
    default:
        return "owned";
    }
}

void LogFreeMemory(const char* phase) {
    SceKernelFreeMemorySizeInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) >= 0) {
        RT_LOGF(RT_TAG_MEMORY,
                "%s user=%d cdram=%d phycont=%d\n",
                phase, info.size_user, info.size_cdram, info.size_phycont);
    }
}

bool AllocateStore(Store& store) {
    if (store.backing != Backing::Mem2) {
        store.data = static_cast<uint8_t*>(memalign(kAllocationAlignment, store.size));
        return store.data != nullptr;
    }

    // MEM2 is guest CPU memory, but keeping its 64 MiB backing in USER RAM
    // leaves too little addressable memory after the translated ARM image is
    // loaded. CDRAM is CPU-readable and has enough headroom for retail MEM2;
    // vitaGL retains the remaining 48 MiB for render allocations.
    store.memblock = sceKernelAllocMemBlock(
        "WiiCompiledGuestMem2", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
        static_cast<SceSize>(store.size), nullptr);
    if (store.memblock < 0) {
        RT_LOGF(RT_TAG_MEMORY, "sceKernelAllocMemBlock(CDRAM) failed: 0x%08X\n",
                static_cast<unsigned>(store.memblock));
        return false;
    }

    void* base = nullptr;
    const int baseResult = sceKernelGetMemBlockBase(store.memblock, &base);
    if (baseResult < 0 || base == nullptr) {
        RT_LOGF(RT_TAG_MEMORY, "sceKernelGetMemBlockBase(CDRAM) failed: 0x%08X\n",
                static_cast<unsigned>(baseResult));
        sceKernelFreeMemBlock(store.memblock);
        store.memblock = -1;
        return false;
    }
    store.data = static_cast<uint8_t*>(base);
    return true;
}

} // namespace

bool IsActive() {
    return Active();
}

void Initialize(const std::vector<RegionRequest>& regions) {
    std::lock_guard<std::mutex> lock(StateMutex());

    if (Active()) {
        if (!SameLayout(ActiveRegions(), regions)) {
            throw std::runtime_error(
                "Vita guest memory is initialized once per process; a different region layout was requested");
        }
        ZeroStores();
        return;
    }

    auto& stores = Stores();
    stores.reserve(regions.size());

    for (const auto& region : regions) {
        if (region.size == 0) continue;

        auto it = std::find_if(stores.begin(), stores.end(),
                               [&](const Store& store) { return SameStore(store, region); });
        if (it == stores.end()) {
            stores.push_back(Store{region.backing, OwnedBaseFor(region), 0, nullptr, -1});
            it = stores.end() - 1;
        }

        const uint64_t required = StoreOffsetFor(region) + region.size;
        if (required > static_cast<uint64_t>(SIZE_MAX)) {
            ReleaseStores();
            throw std::runtime_error("Vita guest-memory backing exceeds the 32-bit address space");
        }
        it->size = std::max(it->size, AlignUp(static_cast<size_t>(required)));
    }

    LogFreeMemory("before guest backing");
    for (auto& store : stores) {
        RT_LOGF(RT_TAG_MEMORY, "allocating %s backing: %u bytes via %s\n",
                BackingName(store.backing), static_cast<unsigned>(store.size),
                store.backing == Backing::Mem2 ? "CDRAM" : "USER");
        if (!AllocateStore(store)) {
            RT_LOGF(RT_TAG_MEMORY, "allocation failed for %s backing (%u bytes)\n",
                    BackingName(store.backing), static_cast<unsigned>(store.size));
            ReleaseStores();
            throw std::runtime_error("Vita guest-memory backing allocation failed");
        }
        std::memset(store.data, 0, store.size);
        LogFreeMemory("after guest backing allocation");
    }

    auto& mappings = Mappings();
    mappings.reserve(regions.size());
    for (const auto& region : regions) {
        if (region.size == 0) continue;
        auto it = std::find_if(stores.begin(), stores.end(),
                               [&](const Store& store) { return SameStore(store, region); });
        if (it == stores.end()) {
            ReleaseStores();
            throw std::runtime_error("Vita guest-memory backing lookup failed");
        }
        mappings.push_back(Mapping{region.base, region.size, StoreOffsetFor(region), &*it});
    }

    ActiveRegions() = regions;
    Active() = true;
}

uint8_t* HostPointer(uint32_t guestAddress) {
    if (!Active()) return nullptr;
    for (const auto& mapping : Mappings()) {
        if (guestAddress < mapping.guestBase) continue;
        const uint64_t offset = static_cast<uint64_t>(guestAddress) - mapping.guestBase;
        if (offset >= mapping.guestSize) continue;
        return mapping.store->data + mapping.storeOffset + offset;
    }
    return nullptr;
}

void ProtectDeferredRange(uint32_t address, size_t length) {
    (void)address;
    (void)length;
    // The Vita backend invalidates readable page-table biases in memory.cpp;
    // there is no virtual-memory protection layer to update here.
}

void UnprotectDeferredRange(uint32_t address, size_t length) {
    (void)address;
    (void)length;
}

void RegisterExecutableRange(uint32_t start, uint32_t end) {
    (void)start;
    (void)end;
    // RecompMod's 4 KiB executable guard tables remain authoritative on Vita.
}

FaultCounters Counters() {
    return {};
}

void LogFaultSummary() noexcept {}

bool HandleAccessViolation(void* faultAddress, bool isWrite) noexcept {
    (void)faultAddress;
    (void)isWrite;
    return false;
}

} // namespace GuestFlat

#endif // MKW_TARGET_VITA
