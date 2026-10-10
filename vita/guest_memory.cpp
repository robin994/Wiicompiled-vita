#include "guest_flat_memory.h"
#include "wiicompiled/guest_ram.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <stdexcept>

namespace GuestFlat {
wiicompiled::RamMirrors g_vitaRamMirrors;
namespace {
struct Owned { uint32_t base; size_t size; uint8_t* bytes; };
wiicompiled::RamMirrors mirrors;
std::vector<Owned> owned;
bool active=false;
uint8_t* allocate(size_t size) {
    auto* bytes=static_cast<uint8_t*>(memalign(4096,(size+4095)&~size_t(4095)));
    if (!bytes) throw std::bad_alloc();
    std::memset(bytes,0,size);return bytes;
}
}
bool IsActive() { return active; }
void Initialize(const std::vector<RegionRequest>& regions) {
    if(active) throw std::runtime_error("Guest RAM already initialized");
    size_t mem1=0,mem2=0;
    for(const auto& r:regions) {
        if(r.backing==Backing::Mem1) mem1=std::max(mem1,size_t(r.base&0x01ffffffu)+size_t(r.size));
        else if(r.backing==Backing::Mem2) mem2=std::max(mem2,size_t(r.base&0x0fffffffu)+size_t(r.size));
    }
    try {
        if(mem1) mirrors.mem1=allocate(mem1);
        if(mem2) mirrors.mem2=allocate(mem2);
        mirrors.mem1Size=mem1;mirrors.mem2Size=mem2;
        for(const auto& r:regions) if(r.backing==Backing::Owned && r.size) {
            if(r.size>SIZE_MAX || uint64_t(r.base)+r.size>kGuestSpaceSize)
                throw std::runtime_error("Invalid guest region");
            owned.push_back({r.base,size_t(r.size),allocate(size_t(r.size))});
        }
        g_vitaRamMirrors=mirrors;active=true;
    } catch(...) {
        free(mirrors.mem1);free(mirrors.mem2);mirrors={};
        for(auto& r:owned) free(r.bytes);
        owned.clear();throw;
    }
}
uint8_t* HostPointer(uint32_t address) {
    // Owned overlays take precedence over the underlying MEM1 mirror.
    for(const auto& r:owned) if(address>=r.base && uint64_t(address)-r.base<r.size)
        return r.bytes+(address-r.base);
    return mirrors.pointer(address,1);
}
// The checked runtime owns deferred/executable policy on ARM32; no unsupported
// 4 GiB virtual reservation or Vita kernel address aliases are assumed here.
void ProtectDeferredRange(uint32_t,size_t) {}
void UnprotectDeferredRange(uint32_t,size_t) {}
void RegisterExecutableRange(uint32_t,uint32_t) {}
FaultCounters Counters() { return {}; }
void LogFaultSummary() noexcept {}
bool HandleAccessViolation(void*,bool) noexcept { return false; }
}
