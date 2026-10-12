#include "native_assets.hpp"
#include "native_audio.hpp"
#include "psarc_backend.hpp"
#include "hle/audio/ax_internal.h"
#include <fstream>
#include <filesystem>
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
    // Learn a real texture sidecar in one scene, persist a bounded manifest,
    // then reopen the same PSARC and prove that prefetch avoids a second IO.
    using namespace wiicompiled::vita;
    const auto directory=std::filesystem::path(argv[3]).parent_path().string();
    constexpr uint64_t sceneKey=0x001f0208000101ull;
    const char* texturePath="native/v1/textures/000000000000002a.avnr";
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    CHECK(assets::PrewarmEnabled());
    CHECK(!assets::EnterScene(sceneKey).manifestLoaded);
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(assets::Checkpoint());
    CHECK(!assets::Checkpoint()); // No repeated writes without new hot accesses.
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(!assets::Checkpoint()); // Repeated cache hit is not a new manifest.
    assets::Shutdown();
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    const auto warmed=assets::EnterScene(sceneKey);
    CHECK(warmed.manifestLoaded && warmed.selected==1 && warmed.loaded==1);
    CHECK(warmed.bytes==graphics.size());
    const auto warmedIo=psarc::Statistics().reads;
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(psarc::Statistics().reads==warmedIo); // cache hit, no archive IO.
    assets::Shutdown();
    // Scene transitions only schedule CPU-sidecar prewarm. Large bursts of
    // PSARC reads must not happen inside the frame presentation callback.
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    const auto prePlanReads=psarc::Statistics().reads;
    const auto plan=assets::QueueScene(sceneKey);
    CHECK(plan.manifestLoaded && plan.selected==1 && plan.remaining==1);
    CHECK(plan.loaded==0 && plan.bytes==0);
    CHECK(psarc::Statistics().reads==prePlanReads);
    const auto skipped=assets::StepScenePrewarm(1,1);
    CHECK(skipped.skippedLarge==1 && skipped.loaded==0 && skipped.remaining==0);
    CHECK(psarc::Statistics().reads==prePlanReads);
    assets::Shutdown();
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    CHECK(assets::QueueScene(sceneKey).remaining==1);
    const auto prior=psarc::Statistics().reads;
    const auto step=assets::StepScenePrewarm(1024,1);
    CHECK(step.loaded==1 && step.bytes==graphics.size() && step.remaining==0);
    CHECK(psarc::Statistics().reads==prior+1);
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(psarc::Statistics().reads==prior+1); // Prefetched bytes reused.
    CHECK(assets::StepScenePrewarm(1024,1).loaded==0); // Exactly one pass.
    assets::Shutdown();
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    CHECK(assets::QueueScene(sceneKey).remaining==1);
    (void)assets::QueueScene(sceneKey+1); // cancel stale scene.
    CHECK(assets::StepScenePrewarm(1024,1).loaded==0);
    assets::Shutdown();
    // A tiny warm budget must not even start an oversized sidecar prefetch.
    CHECK(assets::Initialize(1024,1,directory.c_str()));
    const auto smallBudget=assets::EnterScene(sceneKey);
    CHECK(smallBudget.manifestLoaded && smallBudget.loaded==0 && smallBudget.selected==0);
    assets::Shutdown();
    // Checksum corruption is not a fatal boot failure or reason to reuse old data.
    char suffix[50];
    std::snprintf(suffix,sizeof(suffix),"/native_hot_%016llx.warm",
                  static_cast<unsigned long long>(sceneKey));
    const auto manifest=directory+suffix;
    {
        std::fstream io(manifest,std::ios::in|std::ios::out|std::ios::binary);
        CHECK(io.good());io.seekp(-1,std::ios::end);io.put('\x7f');
        CHECK(io.good());
    }
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    CHECK(!assets::EnterScene(sceneKey).manifestLoaded);
    const auto beforeRead=psarc::Statistics().reads;
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(psarc::Statistics().reads==beforeRead+1);
    assets::Shutdown();
    // Startup prewarm deliberately runs *after* shader GXP preload. Learning
    // a boot asset in scene zero lets WarmBoot pin it as MRU for first draws.
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(assets::Checkpoint());
    assets::Shutdown();
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    const auto coldBootReads=psarc::Statistics().reads;
    const auto bootWarm=assets::WarmBoot();
    CHECK(bootWarm.manifestLoaded && bootWarm.loaded==1 &&
          psarc::Statistics().reads==coldBootReads+1);
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(psarc::Statistics().reads==coldBootReads+1);
    assets::Shutdown();
    // A scene transition itself saves the previous scene's learned hot set;
    // do not require an explicit checkpoint or a clean application exit.
    constexpr uint64_t anotherScene=0x001f0209000101ull;
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    CHECK(!assets::EnterScene(anotherScene).manifestLoaded);
    CHECK(assets::Read(texturePath,graphics,nullptr));
    (void)assets::EnterScene(sceneKey);
    assets::Shutdown();
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    const auto changedScene=assets::EnterScene(anotherScene);
    CHECK(changedScene.manifestLoaded && changedScene.loaded==1);
    assets::Shutdown();
    // A scene with TWO ranked hot assets must not create an I/O burst on
    // transition: the first 1-item tick loads only one sidecar, then the
    // next tick independently loads the second. Both are reused on demand.
    constexpr uint64_t dualScene=0x001f0208000404ull;
    constexpr const char* otherTexture="native/v1/textures/000000000000002c.avnr";
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    CHECK(!assets::QueueScene(dualScene).manifestLoaded);
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(assets::Read(otherTexture,graphics,nullptr));
    CHECK(assets::Checkpoint());
    assets::Shutdown();
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    const auto initialReads=psarc::Statistics().reads;
    const auto dual=assets::QueueScene(dualScene);
    CHECK(dual.manifestLoaded && dual.selected==2 && dual.remaining==2);
    CHECK(psarc::Statistics().reads==initialReads);
    const auto part1=assets::StepScenePrewarm(128,1);
    CHECK(part1.loaded==1 && part1.remaining==1);
    CHECK(psarc::Statistics().reads==initialReads+1);
    const auto part2=assets::StepScenePrewarm(128,1);
    CHECK(part2.loaded==1 && part2.remaining==0);
    CHECK(psarc::Statistics().reads==initialReads+2);
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(assets::Read(otherTexture,graphics,nullptr));
    CHECK(psarc::Statistics().reads==initialReads+2);
    assets::Shutdown();
    // Both assets fit independently in a 128-byte step, but not together.
    // The second must remain scheduled, never silently be discarded.
    CHECK(assets::Initialize(1024,512,directory.c_str()));
    CHECK(assets::QueueScene(dualScene).remaining==2);
    const auto boundedBefore=psarc::Statistics().reads;
    const auto boundedFirst=assets::StepScenePrewarm(128,2);
    CHECK(boundedFirst.loaded==1 && boundedFirst.remaining==1);
    CHECK(boundedFirst.skippedLarge==0 && boundedFirst.bytes<=128);
    CHECK(psarc::Statistics().reads==boundedBefore+1);
    const auto boundedSecond=assets::StepScenePrewarm(128,2);
    CHECK(boundedSecond.loaded==1 && boundedSecond.remaining==0);
    CHECK(boundedSecond.skippedLarge==0 && boundedSecond.bytes<=128);
    CHECK(psarc::Statistics().reads==boundedBefore+2);
    CHECK(assets::Read(texturePath,graphics,nullptr));
    CHECK(assets::Read(otherTexture,graphics,nullptr));
    CHECK(psarc::Statistics().reads==boundedBefore+2);
    assets::Shutdown();
    wiicompiled::vita::psarc::Shutdown();
    CHECK(wiicompiled::vita::psarc::Mount(argv[4]));
    CHECK(!wiicompiled::vita::assets::Initialize());
    CHECK(!wiicompiled::vita::assets::InitializeAudio());
    CHECK(!wiicompiled::vita::assets::Read("native/v1/textures/000000000000002a.avnr",graphics,nullptr));
    wiicompiled::vita::psarc::Shutdown();
    std::puts("native PSARC index/cache, compressed PCM, AX task boundaries, loops, mutation and missing-catalogue fallback passed");
}
