#include "memory_access.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace MemoryInline {
PageEntry g_pageTable[kPageCount]{};
uint8_t g_deferredReadCoveredPages[kPageCount]{};
}
namespace RecompMod {
std::atomic<bool> g_executableWriteGuardEnabled{true};
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
std::atomic<uint8_t> g_executableWriteGuardCoarsePages[kExecutableWriteGuardCoarsePageCount]{};
std::atomic<uint8_t> g_executableWriteGuardMidPages[kExecutableWriteGuardMidPageCount]{};
}
namespace {
using namespace MemoryInline;
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
void Guard(uint32_t address, bool on) {
    RecompMod::g_executableWriteGuardPages[address >> 12] = on;
    RecompMod::g_executableWriteGuardMidPages[address >> 16] = on;
    RecompMod::g_executableWriteGuardCoarsePages[address >> 20] = on;
}
}
int main() {
    using namespace MemoryInline;
    static std::array<uint8_t, 2 * kPageSize> ram{};
    static std::array<uint8_t, kPageSize> overlay{};
    for (uint32_t base : {0u, 0x80000000u, 0xc0000000u, 0x90000000u}) {
        const auto page = base >> kPageShift;
        g_pageTable[page] = {ram.data(), kPageSize};
        g_pageTable[page + 1] = {ram.data() + kPageSize, kPageSize};
        Require(ResolveRangeHost(base + 256, -128, 216, false, true) == ram.data() + 128,
            "physical/cached/uncached MEM1 and MEM2 ranges must resolve");
        Require(ResolveRangeHost(base + kPageSize - 7, 0, 16, false, true) == ram.data() + kPageSize - 7,
            "contiguous coarse boundary must resolve");
    }
    constexpr uint32_t base = 0x80000000u, page = base >> kPageShift;
    Require(!ResolveRangeHost(base, 0, 0, false, true), "empty range rejected");
    Require(!ResolveRangeHost(base, 0, kPageSize + 1, false, true), "oversized range rejected");
    Require(!ResolveRangeHost(0xfffffffeu, 0, 4, false, true), "32-bit wrap rejected");
    Require(!ResolveRangeHost(0x40000000u, 0, 8, false, true), "unmapped range rejected");
    g_pageTable[page + 1] = {overlay.data(), kPageSize};
    Require(!ResolveRangeHost(base + kPageSize - 7, 0, 16, false, true), "noncontiguous boundary rejected");
    g_pageTable[page] = {overlay.data(), 0x4000};
    Require(ResolveRangeHost(base + 8, 0, 8, false, true) == overlay.data() + 8,
        "authoritative owned overlay wins over RAM mirrors");
    Require(ResolveRangeHost(base + 0x3ff8, 0, 8, false, true), "last mapped bytes resolve");
    Require(!ResolveRangeHost(base + 0x3ff9, 0, 8, false, true), "partial mapping end rejected");
    g_pageTable[page] = {ram.data(), kPageSize};
    g_pageTable[page + 1] = {ram.data() + kPageSize, 8};
    Require(ResolveRangeHost(base + kPageSize - 4, 0, 12, false, true), "partial successor fits exactly");
    Require(!ResolveRangeHost(base + kPageSize - 4, 0, 13, false, true), "partial successor overflow rejected");
    g_pageTable[page + 1].limit = kPageSize;
    Guard(base + 0x2000, true);
    Require(!ResolveRangeHost(base + 0x1ffc, 0, 8, false, true), "executable boundary write rejected");
    Require(!ResolveRangeHost(base + 0x1000, 0, 0x4000, false, true), "interior executable page rejected");
    Require(!ResolveRangeHost(base + 0x2000, 0, 16, true, false), "reads retain the checked path");
    Require(ResolveRangeHost(base + 0x8000, 0, 16, false, true), "plain data in mixed code page allowed");
    Guard(base + 0x2000, false);
    Require(ResolveRangeHost(base + 0x2000, 0, 16, false, true), "guard removal is observed");
    for (uint32_t deferredPage : {page, page + 1}) {
        g_deferredReadCoveredPages[deferredPage] = 1;
        Require(!ResolveRangeHost(base + kPageSize - 4, 0, 8, true, false), "deferred boundary reads rejected");
        Require(!ResolveRangeHost(base + kPageSize - 4, 0, 8, false, true), "deferred writes must materialize through scalar policy");
        g_deferredReadCoveredPages[deferredPage] = 0;
    }
    for (uint32_t mmio : {0xcc000000u, 0xcc008000u, 0xcdfffff8u}) {
        g_pageTable[mmio >> kPageShift] = {ram.data(), kPageSize};
        Require(!ResolveRangeHost(mmio, 0, 8, false, true), "MMIO/FIFO writes retain slow policy");
        Require(!ResolveRangeHost(mmio, 0, 8, true, false), "MMIO reads retain slow policy");
    }
    // Exercise the actual bulk codecs at an unaligned address. A successful
    // pair is a complete access, with no scalar fallback needed by generated code.
    Require(!ResolveRangeHost(base + 5, 0, 64, true, true),
        "read/write ranges cannot cache a pointer across a new deferred EFB copy");
    auto* host = ResolveRangeHost(base + 5, 0, 64, false, true);
    Require(WriteResolvedPair32(host, 3, 0x0123456789abcdefULL), "bulk store must be available");
    Require(ram[8] == 0x01 && ram[15] == 0xef, "big-endian bytes preserved");
    const auto pair = ReadResolvedPair32(host, 3);
    Require(pair.valid && pair.first == 0x01234567u && pair.second == 0x89abcdefu,
        "unaligned register pair must round-trip");
    Require(!WriteResolvedPair32(nullptr, 0, 0) && !ReadResolvedPair32(nullptr, 0).valid,
        "rejected ranges retain scalar fallback");
    std::puts("Vita checked RAM range contract passed");
}
