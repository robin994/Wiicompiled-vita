#include "psarc_read_cache.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using wiicompiled::vita::psarc::ReadAheadCache;

int main() {
    ReadAheadCache cache{};
    const std::vector<uint8_t> a(0x20000,0x5a);
    std::vector<uint8_t> b(0x20000);
    for(size_t i=0;i<b.size();++i)b[i]=static_cast<uint8_t>((i*47+23)&255);
    uint64_t calls=0;
    auto backend=[&](unsigned entry,uint64_t offset,void* dest,size_t bytes) noexcept {
        const auto& blob=entry==7?a:b;
        ++calls;
        if(offset>blob.size() || bytes>blob.size()-offset)return false;
        std::memcpy(dest,blob.data()+offset,bytes);
        return true;
    };
    std::array<uint8_t,32*1024> output{};
    // Do not prefetch 16 KiB from a one-off 256-byte random read.
    assert(cache.read(7,a.size(),0,output.data(),256,backend));
    assert(calls==1 && cache.counters().physicalBytes==256);
    // Second read in the same window promotes it to the 16 KiB cache.
    assert(cache.read(7,a.size(),256,output.data(),256,backend));
    assert(calls==2 && cache.counters().cacheFills==1);
    assert(cache.counters().physicalBytes==256+16*1024);
    assert(cache.read(7,a.size(),512,output.data(),4096,backend));
    assert(calls==2 && cache.counters().cacheHits==1);
    assert(std::equal(output.begin(),output.begin()+4096,a.begin()+512));
    // Different file with the same offset must never reuse the cached bytes.
    assert(cache.read(8,b.size(),0,output.data(),64,backend));
    assert(calls==3 && output[0]==b[0]);
    assert(cache.read(8,b.size(),64,output.data(),64,backend));
    assert(calls==4 && cache.counters().cacheFills==2);
    assert(cache.read(8,b.size(),128,output.data(),128,backend));
    assert(calls==4 && std::equal(output.begin(),output.begin()+128,b.begin()+128));
    assert(cache.counters().cacheHits==2);
    // 64 KiB or boundary-crossing requests must keep exact read semantics.
    assert(cache.read(8,b.size(),8000,output.data(),25000,backend));
    assert(calls==5 && std::equal(output.begin(),output.begin()+25000,b.begin()+8000));
    assert(cache.read(8,b.size(),ReadAheadCache::WindowBytes-10,output.data(),20,backend));
    assert(calls==6 && std::equal(output.begin(),output.begin()+20,b.begin()+ReadAheadCache::WindowBytes-10));
    // Near EOF, the cache fill is bounded by the logical entry extent.
    cache.reset();calls=0;
    constexpr size_t end=0x20000;
    assert(cache.read(8,b.size(),end-100,output.data(),30,backend));
    assert(cache.read(8,b.size(),end-90,output.data(),90,backend));
    assert(cache.read(8,b.size(),end-30,output.data(),30,backend));
    assert(calls==2 && cache.counters().cacheFills==1);
    assert(std::equal(output.begin(),output.begin()+30,b.end()-30));
    cache.reset();calls=0;
    constexpr size_t logicalEnd=end-37; // partial final PSARC window
    assert(cache.read(8,logicalEnd,logicalEnd-90,output.data(),30,backend));
    assert(cache.read(8,logicalEnd,logicalEnd-80,output.data(),30,backend));
    assert(cache.read(8,logicalEnd,logicalEnd-1,output.data(),1,backend));
    assert(calls==2 && cache.counters().cacheFills==1);
    assert(cache.counters().physicalBytes==30+ReadAheadCache::WindowBytes-37);
    // Simulated read-ahead error falls back to exact read; no false cache hit.
    cache.reset();calls=0;
    unsigned simulatedFailures=0;
    auto failing=[&](unsigned file,uint64_t at,void* dst,size_t bytes) noexcept {
        if(bytes==ReadAheadCache::WindowBytes) {
            ++simulatedFailures;return false;
        }
        return backend(file,at,dst,bytes);
    };
    assert(cache.read(7,a.size(),400,output.data(),32,failing));
    assert(cache.read(7,a.size(),512,output.data(),32,failing));
    assert(calls==2 && simulatedFailures==1);
    assert(cache.counters().physicalReads==3);
    assert(cache.counters().ioFailures==1);
    assert(cache.counters().cacheFills==0);
    // Sparse accesses never trigger speculative 16 KiB reads. In the worst
    // case random seeks, preserve total bytes transferred (besides metadata).
    cache.reset();calls=0;
    constexpr size_t oneOff=127;
    for(unsigned window=0;window<6;++window) {
        const auto at=window*ReadAheadCache::WindowBytes+64;
        assert(cache.read(8,b.size(),at,output.data(),oneOff,backend));
        assert(std::equal(output.begin(),output.begin()+oneOff,b.begin()+at));
    }
    assert(cache.counters().cacheFills==0);
    assert(cache.counters().cacheHits==0);
    assert(cache.counters().physicalReads==6);
    assert(cache.counters().physicalBytes==6*oneOff);
    // Invalid ranges and null buffers must not issue disk I/O.
    const auto count=calls;
    assert(!cache.read(7,a.size(),a.size()-4,output.data(),5,backend));
    assert(!cache.read(7,a.size(),1,nullptr,1,backend));
    assert(!cache.read(7,a.size(),UINT64_MAX,output.data(),1,backend));
    assert(cache.read(7,a.size(),a.size(),nullptr,0,backend));
    assert(count==calls);
    // Cache slots are bounded, and reset is required on archive remount.
    cache.reset();calls=0;
    for(unsigned window=0;window<ReadAheadCache::CachedWindows+2;++window) {
        const auto position=window*ReadAheadCache::WindowBytes;
        assert(cache.read(7,a.size(),position,output.data(),200,backend));
        assert(cache.read(7,a.size(),position+100,output.data(),200,backend));
    }
    assert(cache.counters().cacheFills==ReadAheadCache::CachedWindows+2);
    const auto before=calls;
    assert(cache.read(7,a.size(),100,output.data(),100,backend));
    assert(calls==before+1); // earliest LRU window was evicted
    cache.reset();
    assert(cache.counters().physicalBytes==0);
    assert(cache.counters().cacheHits==0);
    assert(cache.counters().cacheFills==0);
    std::puts("Adaptive bounded PSARC I/O cache: hits, isolation, EOF, errors, eviction and reset PASS");
    return 0;
}
