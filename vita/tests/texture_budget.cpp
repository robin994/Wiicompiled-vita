#include "aurora-main/platforms/vita/gfx/vita_texture_cache.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

#ifndef MKW_VITA_TEXTURE_SAFE_RETRY
#define MKW_VITA_TEXTURE_SAFE_RETRY 0
#endif

using namespace aurora::vita::gfx;
int main() {
    constexpr size_t budget=1024*1024;
    TextureCache cache(budget);
    std::array<uint8_t,16*16*4> pixels{};
    TextureDesc desc{};
    desc.width=desc.height=16;
    desc.data=pixels.data(); desc.dataSize=pixels.size();
    desc.sourceId=1;
    const Handle first=cache.get_or_upload(desc,1);
    assert(first);
    unsigned uploaded=1;
    for(unsigned i=2;i<=30;++i) {
        desc.sourceId=i;
        if(cache.get_or_upload(desc,1)) ++uploaded;
        assert(cache.bytes()<=budget);
    }
    desc.sourceId=1;
    assert(cache.get_or_upload(desc,1)==first); // current-frame handles stay pinned
#if MKW_VITA_TEXTURE_SHARED_HEADROOM
    assert(uploaded==30);
    assert(cache.alloc_fail_total()==0);
#else
    assert(uploaded<30); // old per-entry scratch accounting exhausts this budget
#endif
    // Advance lifetime, force LRU pre-eviction, then invalidate a guest source.
    for(unsigned i=31;i<1000;++i) {
        desc.sourceId=i;
        assert(cache.get_or_upload(desc,i));
        assert(cache.bytes()<=budget);
    }
    assert(cache.pre_evictions()>0);
    assert(cache.invalidate_source_range(999,1)>0);
    cache.clear(); assert(cache.bytes()==0 && cache.entries()==0);
#if MKW_VITA_TEXTURE_SAFE_RETRY
    // Queue-depth-safe pressure test. Fill one frame with unique textures until
    // every reclaimable byte is protected. Allocation must stop before budget,
    // and an explicit GPU-idle retirement must make LRU pre-eviction possible.
    TextureCache pressure(budget);
    std::vector<uint8_t> mediumPixels(128u*128u*4u);
    TextureDesc medium{};
    medium.width=medium.height=128;
    medium.format=TextureFormat::RGBA8888;
    medium.data=mediumPixels.data();
    medium.dataSize=mediumPixels.size();
    uint64_t blockedSource=0;
    for(uint64_t source=10000;source<10032;++source){
        medium.sourceId=source;
        if(!pressure.get_or_upload(medium,200)){
            blockedSource=source;
            break;
        }
        assert(pressure.bytes()<=budget);
    }
    assert(blockedSource!=0);
    assert(pressure.last_allocation_blocked_by_protection());
    assert(pressure.evict_blocked()>0);
    assert(pressure.protected_bytes()>0);
    const uint64_t evictionsBefore=pressure.pre_evictions();
    pressure.mark_gpu_idle(200);
    medium.sourceId=blockedSource;
    assert(pressure.get_or_upload(medium,200));
    assert(pressure.pre_evictions()>evictionsBefore);
    assert(pressure.bytes()<=budget);
#endif
    std::printf("PASS: texture budget; %u/30 resident small textures, LRU, frame pins, invalidation\n",uploaded);
}
