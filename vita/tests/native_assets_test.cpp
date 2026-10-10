#include "native_assets.hpp"
#include "native_audio.hpp"
#include "psarc_backend.hpp"
#include "hle/audio/ax_internal.h"
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace AxDspHle {
std::atomic<uint8_t*> g_mixMem1{nullptr},g_mixMem2{nullptr};
std::atomic<uint32_t> g_aramWindowGeneration{1};
thread_local bool t_onMixWorker=true;
void ReportMixAddressOutOfRange(uint32_t,size_t) {std::abort();}
bool ResolveAramWindow(uint32_t addr,AramWindow& w) {
    if(addr>=Memory::kMem1Size)return false;
    w.begin=addr&~(kAramWindowSize-1);w.end=w.begin+kAramWindowSize;w.generation=1;
    w.host=g_mixMem1.load()+w.begin;return true;
}
uint8_t ReadAramByteSlow(uint32_t addr) {return g_mixMem1.load()[addr];}
}
namespace GuestFlat {wiicompiled::RamMirrors g_vitaRamMirrors{};}
namespace MemoryInline {
uint8_t g_deferredReadCoveredPages[kPageCount]{};
uintptr_t g_fullReadablePageBias[kPageCount]{};
uint8_t Read8Slow(uint32_t addr) {return AxDspHle::g_mixMem1.load()[addr&0x1fffffff];}
}
std::vector<uint8_t> file(const std::string& path) {
    std::ifstream in(path,std::ios::binary);return {(std::istreambuf_iterator<char>(in)),{}};
}
static uint16_t little16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static uint32_t little32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
// Decode a real request using the unchanged production AX fallback. This is an
// independent consumer oracle for the host compiler, not a copy of its formula.
static int decodeReference(const char* requestPath,const char* outputPath) {
    const auto request=file(requestPath);CHECK(request.size()>=52);
    const size_t bytes=little32(request.data()+8),samples=little32(request.data()+12);
    CHECK(bytes<=Memory::kMem1Size-0x1000&&request.size()==52+bytes);
    std::vector<uint8_t> ram(Memory::kMem1Size);
    constexpr uint32_t base=0x1000;
    std::memcpy(ram.data()+base,request.data()+52,bytes);AxDspHle::g_mixMem1=ram.data();
    AxDspHle::AXPBWii pb{};pb.running=1;pb.audio_addr.sample_format=0;
    const uint32_t current=base*2+2,end=base*2+bytes*2-1;
    pb.audio_addr.cur_addr_lo=current;pb.audio_addr.cur_addr_hi=current>>16;
    pb.audio_addr.end_addr_lo=end;pb.audio_addr.end_addr_hi=end>>16;
    pb.adpcm.yn1=static_cast<int16_t>(little16(request.data()+48));
    pb.adpcm.yn2=static_cast<int16_t>(little16(request.data()+50));
    pb.adpcm.pred_scale=request[52]&0x7f;
    for(unsigned i=0;i<16;++i)pb.adpcm.coefs[i]=static_cast<int16_t>(little16(request.data()+16+i*2));
    AxDspHle::Accelerator a;
    std::ofstream out(outputPath,std::ios::binary);
    CHECK(out);
    for(size_t i=0;i<samples;++i) {
        if(i%96==0)a.Setup(&pb);
        const uint16_t sample=a.ReadSample();
        const char pair[]{static_cast<char>(sample),static_cast<char>(sample>>8)};
        out.write(pair,2);
    }
    CHECK(out);return 0;
}
int main(int argc,char** argv) {
    if(argc==4&&std::string(argv[1])=="--decode-reference")return decodeReference(argv[2],argv[3]);
    CHECK(argc==5);
    CHECK(wiicompiled::vita::psarc::Mount(argv[1]));
    CHECK(wiicompiled::vita::assets::Initialize());
    std::vector<uint8_t> graphics;
    CHECK(wiicompiled::vita::assets::Read("native/v1/textures/000000000000002a.avnr",graphics,nullptr));
    CHECK(graphics==file(argv[2]));
    const auto reads=wiicompiled::vita::psarc::Statistics().reads;
    CHECK(wiicompiled::vita::assets::Read("native/v1/textures/000000000000002a.avnr",graphics,nullptr));
    CHECK(!wiicompiled::vita::assets::Read("native/v1/textures/000000000000002b.avnr",graphics,nullptr));
    CHECK(!wiicompiled::vita::assets::Read("../disc/sys/main.dol",graphics,nullptr));
    CHECK(wiicompiled::vita::psarc::Statistics().reads==reads);
    CHECK(wiicompiled::vita::assets::Read("native/v1/shaders/gxm-cg-gxp-v1/f-000000000000002a.gxp",graphics,nullptr));
    CHECK(graphics==file(argv[2]));
    const auto shaderReads=wiicompiled::vita::psarc::Statistics().reads;
    CHECK(wiicompiled::vita::assets::Read("native/v1/shaders/gxm-cg-gxp-v1/f-000000000000002a.gxp",graphics,nullptr));
    for(const auto* invalid:{"native/v1/shaders/gxm-cg-gxp-v1/../f-000000000000002a.gxp",
                            "native/v1/shaders/gxm-cg-gxp-v1/f-000000000000002A.gxp",
                            "native/v1/shaders/gxm-cg-gxp-v1/x-000000000000002a.gxp"})
        CHECK(!wiicompiled::vita::assets::Read(invalid,graphics,nullptr));
    CHECK(wiicompiled::vita::psarc::Statistics().reads==shaderReads);
    auto request=file(argv[3]);CHECK(request.size()>52);
    const size_t bytes=little32(request.data()+8),samples=little32(request.data()+12);
    std::vector<uint8_t> ram(Memory::kMem1Size);const uint32_t base=0x1000;
    std::memcpy(ram.data()+base,request.data()+52,bytes);AxDspHle::g_mixMem1=ram.data();
    AxDspHle::AXPBWii initial{};initial.running=1;initial.audio_addr.sample_format=0;
    initial.audio_addr.loop_addr_lo=base*2+2;
    initial.audio_addr.cur_addr_lo=base*2+2;
    initial.audio_addr.end_addr_lo=base*2+bytes*2-1;
    initial.adpcm.yn1=static_cast<int16_t>(little16(request.data()+48));
    initial.adpcm.yn2=static_cast<int16_t>(little16(request.data()+50));
    initial.adpcm.pred_scale=request[52]&0x7f;
    for(unsigned i=0;i<16;++i)initial.adpcm.coefs[i]=static_cast<int16_t>(little16(request.data()+16+i*2));
    for(unsigned loop:{0u,1u})for(unsigned mutate:{0u,1u}) {
        auto reference=initial;reference.audio_addr.looping=loop;
        reference.adpcm_loop_info.pred_scale=reference.adpcm.pred_scale;
        reference.adpcm_loop_info.yn1=reference.adpcm.yn1;reference.adpcm_loop_info.yn2=reference.adpcm.yn2;
        auto candidate=reference;std::vector<int16_t> expected;
        wiicompiled::vita::assets::ShutdownAudio();
        std::memcpy(ram.data()+base,request.data()+52,bytes);
        const size_t count=loop?samples*2:samples;
        AxDspHle::Accelerator a;
        for(size_t i=0;i<count;++i) {
            if(i%96==0)a.Setup(&reference);
            if(mutate&&i==4)ram[base+4]^=0x21;
            expected.push_back(a.ReadSample());
        }
        std::memcpy(ram.data()+base,request.data()+52,bytes);
        CHECK(wiicompiled::vita::assets::InitializeAudio());
        unsigned hitProof=0;
        int16_t pcm[14];uint8_t nibbles[14];
        hitProof=WicNativeAudioFrame(ram.data()+base,candidate.adpcm.coefs,candidate.adpcm.yn1,candidate.adpcm.yn2,0,candidate.adpcm.pred_scale,pcm,nibbles);
        CHECK(hitProof==14);
        AxDspHle::Accelerator b;
        for(size_t i=0;i<count;++i) {
            if(i%96==0)b.Setup(&candidate);
            if(mutate&&i==4)ram[base+4]^=0x21;
            CHECK(b.ReadSample()==expected[i]);
        }
        CHECK(std::memcmp(&reference,&candidate,sizeof candidate)==0);
    }
    wiicompiled::vita::assets::ShutdownAudio();wiicompiled::vita::assets::Shutdown();
    wiicompiled::vita::psarc::Shutdown();
    CHECK(wiicompiled::vita::psarc::Mount(argv[4]));
    CHECK(!wiicompiled::vita::assets::Initialize());
    CHECK(!wiicompiled::vita::assets::InitializeAudio());
    CHECK(!wiicompiled::vita::assets::Read("native/v1/textures/000000000000002a.avnr",graphics,nullptr));
    wiicompiled::vita::psarc::Shutdown();
    std::puts("native PSARC index/cache, compressed PCM, AX task boundaries, loops, mutation and missing-catalogue fallback passed");
}
