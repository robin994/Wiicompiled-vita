#include "native_prewarm_plan.hpp"
#include "native_scene_prewarm.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>
using namespace wiicompiled::vita::assets::prewarm;
int main() {
    constexpr uint64_t scene=scene_key(0x1f,2,8,0,1);
    static_assert(scene==scene_key(0x1f,2,8,0,1));
    static_assert(scene!=scene_key(0x1f,2,9,0,1));
    static_assert(scene!=scene_key(0x1f,2,8,1,1));
    static_assert(scene!=scene_key(0x1f,2,8,0,2));
    static_assert(scene!=scene_key(0x1f,3,8,0,1));
    static_assert(scene!=scene_key(0x20,2,8,0,1));
    std::array<uint8_t,5> catalogue{1,2,3,4,5};
    const uint64_t catalogueId=fingerprint(catalogue);
    const auto ranked=rank({
        {1,0xA,256,7},{2,0xB,512,4},{1,0xC,64,7},
        {1,0xD,1000,0},{2,0xE,24,2},{1,0xF,512,8},
    });
    assert(ranked.size()==4 && ranked[0].key==0xF && ranked[1].key==0xC &&
           ranked[2].key==0xA && ranked[3].key==0xB);
    auto chosen=select(ranked,800);
    assert(chosen.size()==2 && chosen[0].key==0xF && chosen[1].key==0xC);
    assert(select(ranked,0).empty());
    assert(select(ranked,1).empty());
    auto encoded=encode(scene,catalogueId,ranked);
    assert(encoded.size()==HeaderBytes+ranked.size()*RecordBytes+8);
    std::vector<Record> decoded;
    assert(decode(encoded,scene,catalogueId,decoded) && decoded==ranked);
    assert(!decode(encoded,scene+1,catalogueId,decoded));
    assert(decoded.empty());
    assert(!decode(encoded,scene,catalogueId+1,decoded));
    assert(!decode(std::span<const uint8_t>{encoded}.first(encoded.size()-1),scene,catalogueId,decoded));
    auto modified=encoded;
    modified[HeaderBytes+5]^=0x10;
    assert(!decode(modified,scene,catalogueId,decoded));
    modified=encoded;modified[8]=2;
    assert(!decode(modified,scene,catalogueId,decoded));
    modified=encoded;modified[34]=255;
    assert(!decode(modified,scene,catalogueId,decoded));
    std::vector<Record> duplicates{{1,33,64,4},{1,33,64,3}};
    assert(!decode(encode(scene,catalogueId,duplicates),scene,catalogueId,decoded));
    assert(encode(scene,catalogueId,{{1,3,16,1}}).empty());
    std::vector<Record> tooMany(MaxRecords+1,{1,1,64,1});
    assert(encode(scene,catalogueId,tooMany).empty());
    std::puts("scene identity, bounded ranking, budget, manifest integrity, stale catalogue and duplicates passed");
}
