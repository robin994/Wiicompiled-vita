#pragma once
// Bounded, versioned, portable manifest for the precompiled native PSARC sidecars.
// This is *CPU-side* resource prefetch: never use it to manufacture GXM handles
// or to reuse dynamically generated GX textures across scene/source mutations.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wiicompiled::vita::assets::prewarm {

struct Record {
    uint32_t kind = 0;  // 1: texture, 2: geometry
    uint64_t key = 0;
    uint32_t size = 0;
    uint32_t hits = 0;
    bool operator==(const Record&) const = default;
};
constexpr size_t MaxRecords = 128;
constexpr size_t HeaderBytes = 36;
constexpr size_t RecordBytes = 20;
constexpr uint64_t FingerprintOffset = 14695981039346656037ull;
constexpr uint64_t FingerprintPrime = 1099511628211ull;

inline uint64_t fingerprint(std::span<const uint8_t> bytes) noexcept {
    uint64_t h = FingerprintOffset;
    for(uint8_t v : bytes) { h ^= v; h *= FingerprintPrime; }
    return h;
}
inline uint32_t read32(const uint8_t* p) noexcept {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint64_t read64(const uint8_t* p) noexcept {
    return uint64_t(read32(p)) | (uint64_t(read32(p+4)) << 32);
}
inline void append32(std::vector<uint8_t>& bytes, uint32_t value) {
    for(unsigned i=0;i<4;++i) bytes.push_back(uint8_t(value >> (8u*i)));
}
inline void append64(std::vector<uint8_t>& bytes, uint64_t value) {
    append32(bytes,uint32_t(value));
    append32(bytes,uint32_t(value >> 32));
}
inline bool valid(const Record& record) noexcept {
    return (record.kind==1 || record.kind==2) && record.size>=28 &&
           record.size<=32u*1024u*1024u && record.hits>0;
}
// Preferred hot records are used first. Do not replace this with "all assets":
// a catalogue can contain thousands of textures and exceed Vita CDRAM.
inline std::vector<Record> rank(std::vector<Record> records, size_t maxRecords=MaxRecords) {
    records.erase(std::remove_if(records.begin(),records.end(),
                                 [](const Record& r){return !valid(r);}), records.end());
    std::sort(records.begin(),records.end(),[](const Record& a,const Record& b){
        if(a.hits!=b.hits) return a.hits>b.hits;
        if(a.kind!=b.kind) return a.kind<b.kind; // texture before geometry
        if(a.size!=b.size) return a.size<b.size;
        return a.key<b.key;
    });
    if(records.size()>maxRecords) records.resize(maxRecords);
    return records;
}
inline std::vector<Record> select(std::span<const Record> records,size_t budget) {
    std::vector<Record> chosen;
    chosen.reserve(std::min(records.size(),MaxRecords));
    size_t bytes=0;
    for(const Record& r:records) {
        if(chosen.size()>=MaxRecords)break;
        if(!valid(r) || r.size>budget-bytes)continue;
        chosen.push_back(r);
        bytes+=r.size;
    }
    return chosen;
}
// Layout: magic[8], version:u32, reserved:u32, scene:u64,
// catalogue fingerprint:u64, count:u32, records[count]*20,
// trailing checksum:u64 over all preceding bytes.
inline std::vector<uint8_t> encode(uint64_t scene,uint64_t catalogue,
                                   std::span<const Record> records) {
    if(records.size()>MaxRecords)return {};
    std::vector<uint8_t> bytes;
    bytes.reserve(HeaderBytes+records.size()*RecordBytes+8);
    constexpr char magic[]="WICWARM1";
    for(unsigned i=0;i<8;++i)bytes.push_back(uint8_t(magic[i]));
    append32(bytes,1);append32(bytes,0);
    append64(bytes,scene);append64(bytes,catalogue);
    append32(bytes,uint32_t(records.size()));
    for(const Record& r:records) {
        if(!valid(r))return {};
        append32(bytes,r.kind);append64(bytes,r.key);
        append32(bytes,r.size);append32(bytes,r.hits);
    }
    append64(bytes,fingerprint(bytes));
    return bytes;
}
inline bool decode(std::span<const uint8_t> bytes,uint64_t scene,
                   uint64_t catalogue,std::vector<Record>& out) {
    out.clear();
    constexpr uint8_t magic[8]={'W','I','C','W','A','R','M','1'};
    if(bytes.size()<HeaderBytes+8 || !std::equal(magic,magic+8,bytes.begin()) ||
       read32(bytes.data()+8)!=1 || read32(bytes.data()+12)!=0 ||
       read64(bytes.data()+16)!=scene || read64(bytes.data()+24)!=catalogue)return false;
    const uint32_t count=read32(bytes.data()+32);
    if(count>MaxRecords || bytes.size()!=36+size_t(count)*RecordBytes+8 ||
       read64(bytes.data()+bytes.size()-8)!=
           fingerprint(bytes.first(bytes.size()-8)))return false;
    std::vector<Record> staged;
    staged.reserve(count);
    for(uint32_t i=0;i<count;++i) {
        const uint8_t* p=bytes.data()+36+size_t(i)*RecordBytes;
        Record r{read32(p),read64(p+4),read32(p+12),read32(p+16)};
        if(!valid(r))return false;
        if(std::any_of(staged.begin(),staged.end(),[&](const Record& other){
               return other.kind==r.kind && other.key==r.key;
           }))return false;
        staged.push_back(r);
    }
    out=std::move(staged);
    return true;
}
} // namespace wiicompiled::vita::assets::prewarm
