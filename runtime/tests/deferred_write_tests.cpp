// Exercise actual checked-memory and scalar/bulk policy using shared RAM
// aliases. A GPU-copy callback writes its old result only when materialized.
#include "memory.h"
#include "isa/ppc_isa_context.h"
#include "system_bridge.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t Cached=0x90000000,Physical=0x10000000,Uncached=0xd0000000;
constexpr size_t Bytes=2*1024*1024;
std::array<uint8_t,Bytes> ram{};
void Check(bool b,const char* why) { if(!b){std::fprintf(stderr,"deferred write: %s\n",why);std::abort();} }
struct Copy { uint32_t offset=0,length=32;unsigned calls=0;std::array<uint64_t,3> tokens{}; };
bool Materialize(void* context) {
    auto& copy=*static_cast<Copy*>(context);++copy.calls;
    std::fill_n(ram.data()+copy.offset,copy.length,uint8_t(0x5a));
    for(auto token:copy.tokens)Memory::CancelDeferredRead(token);
    copy.tokens={};return true;
}
void Defer(Copy& copy,uint32_t offset,uint32_t length=32) {
    copy={offset,length};
    const std::array<uint32_t,3> aliases{Cached,Physical,Uncached};
    for(unsigned i=0;i<3;++i)copy.tokens[i]=Memory::RegisterDeferredRead(aliases[i]+offset,length,Materialize,&copy);
}
}
namespace GuestFlat {
wiicompiled::RamMirrors g_vitaRamMirrors{};
void Initialize(const std::vector<RegionRequest>&) {g_vitaRamMirrors.mem2=ram.data();g_vitaRamMirrors.mem2Size=ram.size();}
uint8_t* HostPointer(uint32_t at) {return g_vitaRamMirrors.pointer(at,1);}
void ProtectDeferredRange(uint32_t,size_t) {}
void UnprotectDeferredRange(uint32_t,size_t) {}
}
namespace RecompMod {
std::atomic<bool> g_executableWriteGuardEnabled{false};
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
std::atomic<uint8_t> g_executableWriteGuardCoarsePages[kExecutableWriteGuardCoarsePageCount]{};
std::atomic<uint8_t> g_executableWriteGuardMidPages[kExecutableWriteGuardMidPageCount]{};
bool HandleExecutableWrite(uint32_t,size_t,uint64_t){return false;}
uint32_t CurrentTranslatedExecutionAddress() noexcept{return 0;}
}
void SystemBridge::DumpCpuState(const CpuContext*) {}
extern "C" {
void GX_HLE_FIFO_WriteFloat(float){std::abort();}
void GX_HLE_FIFO_Write32(uint32_t){std::abort();}
void GX_HLE_FIFO_Write16(uint16_t){std::abort();}
void GX_HLE_FIFO_Write8(uint8_t){std::abort();}
}
int main() {
    Memory::Config config;
    for(uint32_t base:{Cached,Physical,Uncached})config.regions.push_back({"shared test MEM2",base,Bytes});
    Memory::Init(config);Copy copy;
    for(uint32_t alias:{Cached,Physical,Uncached}) {
        Defer(copy,128);Memory::Write32(alias+132,0x46520000);
        Check(copy.calls==1,"CPU store must first materialize prior GPU result");
        for(uint32_t view:{Cached,Physical,Uncached})Check(Memory::Read32(view+132)==0x46520000,"alias read must preserve new heap tag");
        Check(copy.calls==1 && ram[128]==0x5a && ram[159]==0x5a,"copy once, preserving untouched bytes");
    }
    Defer(copy,256);MemoryInline::FlatWriteRam32(Cached+260,0x55440006);
    Check(copy.calls==1 && Memory::Read32(Physical+260)==0x55440006,"proven RAM write obeys deferred policy");
    Defer(copy,384);MemoryInline::FlatWriteRam8(Cached+385,0xab);
    Check(copy.calls==1 && Memory::Read8(Uncached+385)==0xab,"byte RAM write obeys deferred policy");
    Defer(copy,512);MemoryInline::FlatWriteRam16(Uncached+514,0x4652);
    Check(copy.calls==1 && Memory::Read16(Cached+514)==0x4652,"halfword RAM write obeys deferred policy");
    Defer(copy,640);MemoryInline::FlatWriteRamFloat32(Physical+644,1.0);
    Check(copy.calls==1 && Memory::ReadFloat32(Cached+644)==1.f,"single RAM write obeys deferred policy");
    Defer(copy,768);MemoryInline::FlatWriteRamFloat64(Cached+776,2.0);
    Check(copy.calls==1 && Memory::ReadFloat64(Physical+776)==2.0,"double RAM write obeys deferred policy");
    // Cross the coarse-page boundary; neither fast scalar nor bulk resolution
    // may hide the pending copy on the second page.
    Defer(copy,MemoryInline::kPageSize,32);
    Check(!MemoryInline::ResolveRangeHost(Cached+MemoryInline::kPageSize-4,0,8,false,true),"deferred bulk range must retain scalar policy");
    Memory::Write64(Cached+MemoryInline::kPageSize-4,0x0123456789abcdefULL);
    Check(copy.calls==1 && Memory::Read64(Uncached+MemoryInline::kPageSize-4)==0x0123456789abcdefULL,"cross-page new data must survive");
    auto* writable=MemoryInline::ResolveRangeHost(Cached+MemoryInline::kPageSize-4,0,8,false,true);
    Check(writable && MemoryInline::WriteResolvedPair32(writable,0,0xfedcba9876543210ULL),"completed copy restores bulk path");
    Check(Memory::Read64(Physical+MemoryInline::kPageSize-4)==0xfedcba9876543210ULL,"bulk bytes visible through aliases");
    Defer(copy,1024);Check(Memory::Read32(Uncached+1024)==0x5a5a5a5a && copy.calls==1,"ordinary lazy read still materializes");
    Memory::Write32(Cached+1024,0x12345678);
    Check(Memory::Read32(Physical+1024)==0x12345678 && copy.calls==1,"resolved aliases cannot replay stale copy");
    Memory::Reset();std::puts("Deferred CPU stores, RAM stores, bulk boundary and alias-order contracts PASS (host RAM)");
}
