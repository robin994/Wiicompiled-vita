#pragma once
// Adaptive, bounded cache for immutable *logical* PSARC file ranges.
//
// Two small reads from the same 16 KiB window are required before a
// read-ahead fill. Sparse/random one-off requests still read their exact
// byte count. This keeps SD-card overfetch under control while eliminating
// duplicate sceIoPread calls from fragmented DVD requests.
//
// Owned by the serialized PSARC worker; no other thread may touch it.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace wiicompiled::vita::psarc {

class ReadAheadCache {
public:
    static constexpr size_t WindowBytes=16*1024;
    static constexpr size_t CachedWindows=4;
    static constexpr size_t ProbeWindows=8;
    static constexpr size_t MaxSmallRead=4*1024;

    struct Counters {
        uint64_t smallReads=0, cacheHits=0, cacheFills=0;
        uint64_t physicalReads=0, physicalBytes=0, ioFailures=0;
    };

    void reset() noexcept {
        for(auto& slot:cache_)slot.valid=false;
        for(auto& hint:recent_)hint.valid=false;
        nextHint_=0;clock_=0;stats_={};
    }
    Counters counters() const noexcept { return stats_; }

    template<class PhysicalRead>
    bool read(unsigned entry,uint64_t fileBytes,uint64_t offset,
              void* dst,size_t bytes,PhysicalRead&& physicalRead) noexcept {
        if(offset>fileBytes || bytes>fileBytes-offset ||
           (!dst && bytes) || bytes>std::numeric_limits<long>::max())return false;
        if(!bytes)return true;
        const auto fetch=[&](uint64_t at,void* destination,size_t count) noexcept {
            ++stats_.physicalReads;
            const bool success=physicalRead(entry,at,destination,count);
            if(success)stats_.physicalBytes+=count;
            else ++stats_.ioFailures;
            return success;
        };
        const auto within=static_cast<size_t>(offset%WindowBytes);
        if(bytes>MaxSmallRead || within>WindowBytes-bytes)
            return fetch(offset,dst,bytes); // large or cross-window: original I/O
        ++stats_.smallReads;
        const uint64_t window=offset-within;
        for(auto& slot:cache_) {
            if(slot.valid && slot.entry==entry && slot.start==window &&
               within<=slot.length && bytes<=slot.length-within) {
                ++stats_.cacheHits;
                slot.used=++clock_;
                std::memcpy(dst,slot.data.data()+within,bytes);
                return true;
            }
        }
        // Only a repeated access to the same logical window justifies
        // reading ahead. There is deliberately no speculation on first touch.
        bool repeated=false;
        for(auto& hint:recent_) {
            if(hint.valid && hint.entry==entry && hint.start==window) {
                repeated=true;
                hint.valid=false;
                break;
            }
        }
        if(repeated) {
            auto* victim=&cache_.front();
            for(auto& slot:cache_)
                if(!slot.valid || slot.used<victim->used)victim=&slot;
            victim->valid=false;
            const auto available=static_cast<size_t>(
                std::min<uint64_t>(WindowBytes,fileBytes-window));
            if(fetch(window,victim->data.data(),available)) {
                victim->entry=entry;victim->start=window;
                victim->length=available;victim->used=++clock_;
                victim->valid=true;
                ++stats_.cacheFills;
                std::memcpy(dst,victim->data.data()+within,bytes);
                return true;
            }
            // Optional lookahead failing must not break a valid short read.
            // Try the caller's exact bytes using the original reader.
        } else {
            auto& hint=recent_[nextHint_++%recent_.size()];
            hint.entry=entry;hint.start=window;hint.valid=true;
        }
        return fetch(offset,dst,bytes);
    }

private:
    struct CacheWindow {
        std::array<uint8_t,WindowBytes> data{};
        uint64_t start=0,used=0;
        size_t length=0;
        unsigned entry=0;
        bool valid=false;
    };
    struct CandidateWindow {
        uint64_t start=0;
        unsigned entry=0;
        bool valid=false;
    };
    std::array<CacheWindow,CachedWindows> cache_{};
    std::array<CandidateWindow,ProbeWindows> recent_{};
    Counters stats_{};
    size_t nextHint_=0;
    uint64_t clock_=0;
};

} // namespace wiicompiled::vita::psarc
