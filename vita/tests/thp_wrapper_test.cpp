#include "thp_native.hpp"
#include "avc_video.hpp"
#include "isa/ppc_isa_context.h"
#include "host_context.h"
#include "memory.h"
#include "recomp_mod_loader.h"
#include "gfx/vita_cpu_workers.hpp"
#include <cstring>
#include <thread>

void Require(bool);
std::vector<uint8_t> Encode(unsigned, unsigned);
std::vector<uint8_t> Unstuff(const std::vector<uint8_t>&, unsigned);
extern "C" void __wrap_func_801B3BAC(CpuContext*);
namespace {
bool native = false;
unsigned realCalls = 0, poolCalls = 0, publications=0;
unsigned publishWidth=608,publishHeight=456,hardwareCalls=0;
unsigned invalidations=0;
bool pendingHardware=false;
CpuContext expected{};
std::vector<uint8_t> memory(2*1024*1024);
constexpr uint32_t base=0x90000000, record=base+0x300;
void Put(uint32_t address, uint32_t value) {
    for (unsigned i=0;i<4;++i) memory[address-base+i]=uint8_t(value>>(24-i*8));
}
}
namespace RecompMod {
std::atomic<bool> g_executableWriteGuardEnabled{false};
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
std::atomic<uint8_t> g_executableWriteGuardCoarsePages[kExecutableWriteGuardCoarsePageCount]{};
std::atomic<uint8_t> g_executableWriteGuardMidPages[kExecutableWriteGuardMidPageCount]{};
}
extern "C" uint64_t sceKernelGetProcessTimeWide() { Require(native); static uint64_t clock=0; return ++clock; }
void HostContext::RunOnNativeStack(Entry entry, void* argument) {
    Require(!native); native=true; entry(argument); native=false;
}
bool Memory::Contains(uint32_t address, size_t size) {
    Require(native); return address>=base && uint64_t(address)+size<=uint64_t(base)+memory.size();
}
uint8_t* Memory::GetPointer(uint32_t address, size_t size) {
    return Contains(address,size) ? memory.data()+address-base : nullptr;
}
namespace MemoryInline {
uintptr_t g_fullReadablePageBias[kPageCount]{};
uint32_t Read32Slow(uint32_t address) {
    Require(Memory::Contains(address,4)); uint32_t value=0;
    for (unsigned i=0;i<4;++i) value=(value<<8)|memory[address-base+i];
    return value;
}
}
extern "C" void __real_func_801B3BAC(CpuContext* ctx) {
    Require(!native && std::memcmp(ctx,&expected,sizeof(expected))==0);
    ++realCalls; ctx->gpr[3]=77;
}
extern "C" void GxNotifyGuestRamDmaWrite(uint32_t address,uint32_t bytes) {
    Require(native && poolCalls==1 && publications<3);
    Require(address==expected.gpr[4+publications]);
    Require(bytes==(publications?publishWidth*publishHeight/4:publishWidth*publishHeight));
    ++publications;
}
extern "C" void WicInvalidateVideoTextures() {
    Require(native && publications==3 && poolCalls==1);++invalidations;
}
namespace wic::avc {
DecodeResult DecodeHardware(std::span<const uint8_t> bytes,wic::thp::Image& image,std::string& error) {
    Require(native); ++hardwareCalls; Component c;
    if(!Parse(bytes,c,error)) return DecodeResult::Error;
    if(pendingHardware) return DecodeResult::Pending;
    image.width=c.width;image.height=c.height;
    for(unsigned i=0;i<3;++i) image.planes[i].assign(size_t(c.width)*c.height/(i?4:1),uint8_t(7+i*10));
    return DecodeResult::Picture;
}
}
namespace aurora::vita::gfx {
uint32_t cpu_worker_threads() noexcept { return 2; }
bool cpu_parallel_for_min_lanes(size_t count,size_t minItems,uint32_t lanes,CpuRangeTask task,void* context) noexcept {
    Require(native && minItems==128 && lanes==3); ++poolCalls;
    bool a=false,b=false;
    std::thread x([&] { a=task(context,0,count/3,1); });
    std::thread y([&] { b=task(context,count/3,2*count/3,2); });
    bool c=task(context,2*count/3,count,0); x.join();y.join(); return a&&b&&c;
}
}
void CheckWrapper() {
    const auto bytes=Unstuff(Encode(608,456),2);
    std::fill(memory.begin(),memory.end(),0xa5);
    std::copy(bytes.begin(),bytes.end(),memory.begin()+record-base+12);
    Put(base+108,1); Put(base+128,608); Put(base+132,456);
    Put(base+0x200,record); Put(record+8,bytes.size());
    expected.gpr[3]=record+12; expected.gpr[4]=base+0x50000;
    expected.gpr[5]=base+0xa0000; expected.gpr[6]=base+0xc0000;
    expected.gpr[7]=base+0xe0000; expected.gpr[23]=base+0x200;
    expected.gpr[25]=record+8; expected.gpr[26]=0; expected.gpr[30]=base; expected.lr=0x80552c2c;
    auto cpu=expected;
    __wrap_func_801B3BAC(&cpu);
    auto success=expected; success.gpr[3]=0;
    Require(realCalls==0 && poolCalls==1 && publications==3 && std::memcmp(&cpu,&success,sizeof(cpu))==0);
    wic::thp::Image image; std::string error; Require(wic::thp::Decode(bytes,image,error));
    for (unsigned p=0;p<3;++p) {
        std::vector<uint8_t> tiled(image.planes[p].size());
        wic::thp::TileRange(image.planes[p].data(),tiled.data(),p?304:608,0,tiled.size()/32);
        const auto index=expected.gpr[4+p]-base;
        Require(std::memcmp(tiled.data(),memory.data()+index,tiled.size())==0);
        Require(memory[index-1]==0xa5 && memory[index+tiled.size()]==0xa5);
    }
    const auto snapshot=memory;
    const auto checkFallback=[&] {
        cpu=expected; __wrap_func_801B3BAC(&cpu);
        auto result=expected; result.gpr[3]=77;
        Require(std::memcmp(&cpu,&result,sizeof(cpu))==0 && memory==snapshot && poolCalls==1);
    };
    expected.lr=0; checkFallback(); expected.lr=0x80552c2c;
    expected.gpr[6]=expected.gpr[5]; checkFallback(); expected.gpr[6]=base+0xc0000;
    expected.gpr[4]=expected.gpr[3]; checkFallback(); expected.gpr[4]=base+0x50000;
    expected.gpr[26]=16; checkFallback(); expected.gpr[26]=0;
    // A translated-code destination must retain the authoritative fallback.
    RecompMod::g_executableWriteGuardEnabled=true;
    const auto addr=expected.gpr[4];
    RecompMod::g_executableWriteGuardCoarsePages[addr>>20]=1;
    RecompMod::g_executableWriteGuardMidPages[addr>>16]=1;
    RecompMod::g_executableWriteGuardPages[addr>>12]=1;
    checkFallback(); RecompMod::g_executableWriteGuardEnabled=false;
    Require(realCalls==5);
    Put(base+128,512); // Buffers were sized from different video metadata.
    const auto mismatched=memory;
    cpu=expected; __wrap_func_801B3BAC(&cpu);
    auto fallback=expected; fallback.gpr[3]=77;
    Require(realCalls==6 && poolCalls==1 && memory==mismatched &&
        std::memcmp(&cpu,&fallback,sizeof(cpu))==0);

    // Converted components use the ordinary decoder ABI even through other
    // translated callers. Never reinterpret AVC as PPC JPEG on an error.
    const std::vector<uint8_t> es={0,0,0,1,9,16,0,0,1,7,66,0,32,0,0,1,8,128,0,0,1,5,128};
    const auto source=record+12;
    std::memcpy(memory.data()+source-base,"WAVC",4);
    Put(source+4,1);Put(source+8,512);Put(source+12,192);Put(source+16,es.size());Put(source+20,1);
    std::copy(es.begin(),es.end(),memory.begin()+source-base+24);memory[source-base+24+es.size()]=0;
    expected.lr=0x80550000; expected.gpr[3]=source;
    expected.gpr[23]=expected.gpr[25]=expected.gpr[26]=expected.gpr[30]=0;
    poolCalls=publications=0;publishWidth=512;publishHeight=192;
    const auto beforePending=memory;
    pendingHardware=true;cpu=expected;__wrap_func_801B3BAC(&cpu);success=expected;success.gpr[3]=0;
    Require(realCalls==6 && hardwareCalls==1 && poolCalls==1 && publications==3 && invalidations==2 && std::memcmp(&cpu,&success,sizeof(cpu))==0);
    for(unsigned p=0;p<3;++p) {
        const auto at=expected.gpr[4+p]-base,n=publishWidth*publishHeight/(p?4:1);
        for(unsigned i=0;i<n;++i)Require(memory[at+i]==(p?128:0));
        Require(memory[at-1]==beforePending[at-1] && memory[at+n]==beforePending[at+n]);
    }
    poolCalls=publications=0;
    pendingHardware=false;cpu=expected;__wrap_func_801B3BAC(&cpu);
    Require(realCalls==6 && hardwareCalls==2 && poolCalls==1 && publications==3 && invalidations==3 && std::memcmp(&cpu,&success,sizeof(cpu))==0);
    for(unsigned i=0;i<3;++i) {
        const auto at=expected.gpr[4+i]-base,bytes=publishWidth*publishHeight/(i?4:1);
        for(unsigned j=0;j<bytes;++j) Require(memory[at+j]==7+i*10);
    }
    Put(source+16,UINT32_MAX);
    const auto beforeAvcError=memory;
    cpu=expected;__wrap_func_801B3BAC(&cpu);fallback=expected;fallback.gpr[3]=1;
    Require(realCalls==6 && hardwareCalls==2 && poolCalls==1 && publications==3 && memory==beforeAvcError && std::memcmp(&cpu,&fallback,sizeof(cpu))==0);
    Put(source+16,es.size()); Put(source+4,2); // Recognized format, unsupported version.
    const auto beforeVersionError=memory;
    cpu=expected;__wrap_func_801B3BAC(&cpu);
    Require(realCalls==6 && hardwareCalls==3 && poolCalls==1 && publications==3 && memory==beforeVersionError && std::memcmp(&cpu,&fallback,sizeof(cpu))==0);
}
