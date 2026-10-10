// Replay the actual generated SDK allocator and memset with the Vita checked
// memory/ABI helpers. The host supplies RAM and slow-access policy only.
#include "abi_bridge.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <vector>

MKW_TRANSLATED_TRAIT(80005F84, func_80005F84, 0);
MKW_TRANSLATED_TRAIT(80006038, func_80006038, 0);
MKW_TRANSLATED_TRAIT(80198798, func_80198798, 0);
MKW_TRANSLATED_TRAIT(8019899C, func_8019899C, 0);
MKW_TRANSLATED_TRAIT(80198D88, func_80198D88, 0);
MKW_TRANSLATED_TRAIT(80199B58, func_80199B58, 0);
extern "C" void UnsupportedAllocatorBranch(CpuContext*);
MKW_TRANSLATED_TRAIT(80198A78, UnsupportedAllocatorBranch, 0);
MKW_TRANSLATED_TRAIT(801A7EE4, UnsupportedAllocatorBranch, 0);
MKW_TRANSLATED_TRAIT(801A7FC0, UnsupportedAllocatorBranch, 0);
extern "C" void ReplayAllocation(CpuContext*);
MKW_TRANSLATED_TRAIT(80199B98, ReplayAllocation, 0);
#include "../../generated/functions/func_80005F84.cpp"
#include "../../generated/functions/func_80006038.cpp"
#include "../../generated/functions/func_80198798.cpp"
#include "../../generated/functions/func_8019899C.cpp"
#include "../../generated/functions/func_80198D88.cpp"
#include "../../generated/functions/func_80199B58.cpp"
#include "../../generated/functions/func_80082AF0.cpp"

namespace {
constexpr uint32_t Base=0x90000000, Heap=0x91a7fe54;
constexpr uint32_t OldTail=0x924b2dc0, PreviousFree=0x923e337c, PreviousUsed=0x924b2d90, End=0x937d3250;
std::vector<uint8_t> ram(64*1024*1024);
void Check(bool value,const char* reason) {
    if(!value) { std::fprintf(stderr,"guest heap replay: %s\n",reason); std::abort(); }
}
uint8_t* Pointer(uint32_t address,size_t bytes) {
    Check(address>=Base && uint64_t(address)+bytes<=uint64_t(Base)+ram.size(),"out of test RAM");
    return ram.data()+address-Base;
}
template<class T> T Read(uint32_t address) {
    const auto* p=Pointer(address,sizeof(T));T value=0;
    for(unsigned i=0;i<sizeof(T);++i) value=(value<<8)|p[i];return value;
}
template<class T> void Write(uint32_t address,T value) {
    auto* p=Pointer(address,sizeof(T));
    for(unsigned i=0;i<sizeof(T);++i)p[i]=uint8_t(value>>(8*(sizeof(T)-i-1)));
}
void Node(uint32_t at,uint16_t tag,uint32_t size,uint32_t prev,uint32_t next) {
    Write<uint16_t>(at,tag);Write<uint16_t>(at+2,0);Write(at+4,size);Write(at+8,prev);Write(at+12,next);
}
void Prepare(uint32_t size,uint32_t gap) {
    const uint32_t payload=OldTail+16+gap,newTail=payload+size;
    std::fill(Pointer(OldTail-64,newTail-OldTail+128),Pointer(newTail+64,0),0xa5);
    Write(Heap+56,1u);Write(Heap+60,PreviousFree);Write(Heap+64,OldTail);
    Write(Heap+68,PreviousUsed);Write(Heap+72,PreviousUsed);Write(Heap+76,0x00060000u);
    Node(PreviousFree,0x4652,4,0,OldTail);
    Node(OldTail,0x4652,End-OldTail-16,PreviousFree,0);
    Node(PreviousUsed,0x5544,32,0,0);
}
void Replay(uint32_t size,uint32_t gap) {
    const uint32_t payload=OldTail+16+gap,newTail=payload+size;
    Prepare(size,gap);
    CpuContext cpu{};
    cpu.gpr[1]=Base+0x10000;cpu.gpr[3]=Heap+60;cpu.gpr[4]=OldTail;
    cpu.gpr[5]=payload;cpu.gpr[6]=size;cpu.gpr[7]=0;
    for(unsigned i=26;i<32;++i)cpu.gpr[i]=0x12340000+i;
    cpu.lr=0x801989f8;
    const auto before=cpu;CpuContextScope scope(&cpu);func_80198798(&cpu);
    Check(cpu.gpr[3]==payload && cpu.gpr[1]==before.gpr[1] && cpu.lr==before.lr,"allocator return/stack ABI");
    for(unsigned i=26;i<32;++i) Check(cpu.gpr[i]==before.gpr[i],"nonvolatile ABI");
    Check(Read<uint16_t>(newTail)==0x4652 && Read<uint32_t>(newTail+4)==End-newTail-16,"free tail survives zero fill");
    Check(Read<uint32_t>(newTail+12)==0 && Read<uint32_t>(Heap+64)==newTail,"last free links");
    Check(Read<uint16_t>(payload-16)==0x5544 && Read<uint32_t>(payload-12)==size,"used tag/size");
    Check(Read<uint32_t>(Heap+72)==payload-16 && Read<uint32_t>(PreviousUsed+12)==payload-16,"used list links");
    Check(std::all_of(Pointer(payload,size),Pointer(newTail,0),[](uint8_t b){return b==0;}),"payload fully initialized");
    Check(Pointer(newTail+16,1)[0]==0xa5,"no write past free header");
}
void ReplayHead(uint32_t size,uint32_t alignment) {
    Prepare(size,alignment);
    constexpr uint32_t allocator=Base+0x3000;
    Write(allocator+4,Heap);Write(allocator+8,alignment);
    CpuContext cpu{};
    cpu.gpr[1]=Base+0x10000;cpu.gpr[3]=allocator;cpu.gpr[4]=size;
    for(unsigned i=26;i<32;++i)cpu.gpr[i]=0x56780000+i;
    cpu.lr=0x8007a4e4;
    const auto before=cpu;CpuContextScope scope(&cpu);func_80199B58(&cpu);
    const uint32_t payload=(OldTail+16+alignment-1)&~(alignment-1),newTail=payload+size;
    if(cpu.gpr[3]!=payload || cpu.gpr[1]!=before.gpr[1] || cpu.lr!=before.lr)
        std::fprintf(stderr,"head size=%u alignment=%u expected=%08x actual=%08x sp=%08x lr=%08x tail=%08x\n",
            size,alignment,payload,cpu.gpr[3],cpu.gpr[1],cpu.lr,Read<uint32_t>(Heap+64));
    Check(cpu.gpr[3]==payload && cpu.gpr[1]==before.gpr[1] && cpu.lr==before.lr,"complete head allocator return/stack ABI");
    for(unsigned i=26;i<32;++i)Check(cpu.gpr[i]==before.gpr[i],"complete head allocator nonvolatile ABI");
    Check(Read<uint16_t>(newTail)==0x4652 && Read<uint32_t>(newTail+4)==End-newTail-16,"complete head allocator free tail");
    Check(Read<uint32_t>(Heap+64)==newTail && Read<uint32_t>(newTail+12)==0,"complete head allocator links");
    Check(std::all_of(Pointer(payload,size),Pointer(newTail,0),[](uint8_t b){return b==0;}),"complete head allocator zero fill");
    Check(Pointer(newTail+16,1)[0]==0xa5,"complete head allocator end boundary");
}
}
namespace GuestFlat { wiicompiled::RamMirrors g_vitaRamMirrors{}; }
namespace RecompMod {
std::atomic<bool> g_executableWriteGuardEnabled{false};
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
std::atomic<uint8_t> g_executableWriteGuardCoarsePages[kExecutableWriteGuardCoarsePageCount]{};
std::atomic<uint8_t> g_executableWriteGuardMidPages[kExecutableWriteGuardMidPageCount]{};
}
namespace MemoryInline {
PageEntry g_pageTable[kPageCount]{};
uintptr_t g_fullPageBias[kPageCount]{},g_fullReadablePageBias[kPageCount]{},g_fullWritablePageBias[kPageCount]{};
const SparseWritablePageTable* g_sparseWritablePageTables[kPageCount]{};
uint8_t g_deferredReadCoveredPages[kPageCount]{};
uint8_t Read8Slow(uint32_t at) { return Read<uint8_t>(at); }
uint16_t Read16Slow(uint32_t at) { return Read<uint16_t>(at); }
uint32_t Read32Slow(uint32_t at) { return Read<uint32_t>(at); }
void Write8Slow(uint32_t at,uint8_t value) { Write(at,value); }
void Write16Slow(uint32_t at,uint16_t value) { Write(at,value); }
void Write32Slow(uint32_t at,uint32_t value) { Write(at,value); }
template<class T> T ReadResolvedFallback(uint32_t at) { return Read<T>(at); }
template<class T> void WriteResolvedFallback(uint32_t at,T value) { Write(at,value); }
}
CpuContext& GetPersistentCpuContext() { static CpuContext cpu;return cpu; }
const TranslatedFunctionInfo* TranslatedFunctionRegistry::FindByAddressPtrSlow(uint32_t) { return nullptr; }
const RawDispatchRecord* TranslatedFunctionRegistry::FindRawByAddressPtrSlow(uint32_t) { return nullptr; }
[[noreturn]] void RuntimeCrash::FatalMissingGuestTarget(uint32_t, CpuContext*) noexcept { std::abort(); }
extern "C" void UnsupportedAllocatorBranch(CpuContext*) {
    Check(false,"unexpected locked or tail allocation in head-only replay");
}
extern "C" void ReplayAllocation(CpuContext* cpu) {
    const auto size=cpu->gpr[4];
    cpu->gpr[3]=Heap+60;cpu->gpr[4]=OldTail;cpu->gpr[5]=OldTail+16;cpu->gpr[6]=size;cpu->gpr[7]=0;
    func_80198798(cpu);
}
int main() {
    using namespace MemoryInline;
    for(unsigned i=0;i<ram.size()/kPageSize;++i) {
        const auto address=Base+i*kPageSize,index=address>>kPageShift;
        g_pageTable[index]={ram.data()+i*kPageSize,kPageSize};
        if(i+1<ram.size()/kPageSize)
            g_fullPageBias[index]=g_fullReadablePageBias[index]=g_fullWritablePageBias[index]=uintptr_t(ram.data()+i*kPageSize)-address+1;
    }
    // The last successful allocation in the hardware dump was exactly 96
    // bytes, ending at the zeroed free header 0x924b2e30.
    Replay(96,0);
    for(uint32_t size=4;size<=4096;size+=4) for(uint32_t gap:{0u,4u,16u,32u,64u}) Replay(size,gap);
    // Exercise the real callback -> MEMAllocFromExpHeapEx -> AllocFromHead_
    // -> split/zero-fill chain, rather than substituting the splitting step.
    // The hardware heap has flags=1: positive alignment and no mutex branch.
    // Requests start at 8 bytes so the preceding four-byte hole is skipped.
    for(uint32_t size=8;size<=4096;size+=4) for(uint32_t alignment:{4u,8u,16u,32u,64u}) ReplayHead(size,alignment);
    for(uint32_t offset=0;offset<32;++offset) for(uint32_t size=0;size<=256;++size) for(uint32_t fill:{0u,0xa5u,0xffu}) {
        const uint32_t at=Base+kPageSize-64+offset;
        std::fill(Pointer(at-32,size+64),Pointer(at+size+32,0),0x71);
        CpuContext cpu{};cpu.gpr[1]=Base+0x10000;cpu.gpr[3]=at;cpu.gpr[4]=fill;cpu.gpr[5]=size;cpu.lr=0x12345678;
        CpuContextScope scope(&cpu);func_80006038(&cpu);
        Check(cpu.gpr[3]==at && cpu.gpr[1]==Base+0x10000 && cpu.lr==0x12345678,"memset return/stack ABI");
        Check(Pointer(at-1,1)[0]==0x71 && Pointer(at+size,1)[0]==0x71,"memset boundary overwrite");
        Check(std::all_of(Pointer(at,size),Pointer(at+size,0),[=](uint8_t b){return b==fill;}),"memset contents");
    }
    for(uint32_t count=1;count<=1024;++count) {
        Prepare(16*count,0);
        constexpr uint32_t resource=Base+0x20000,smallData=Base+0x40000;
        Write<uint16_t>(resource+12,0); // No texture lookup; link initialization only.
        Write<uint32_t>(smallData-27280,Base+0x3000);
        CpuContext cpu{};cpu.gpr[1]=Base+0x10000;cpu.gpr[3]=PreviousUsed+16;
        cpu.gpr[4]=resource;cpu.gpr[6]=count;cpu.gpr[13]=smallData;cpu.lr=0x80082adc;
        CpuContextScope scope(&cpu);func_80082AF0(&cpu);
        const auto newTail=OldTail+16+16*count;
        Check(Read<uint16_t>(newTail)==0x4652 && Read<uint32_t>(newTail+4)==End-newTail-16,"animation initialization corrupted free tail");
        Check(Read<uint32_t>(PreviousUsed+16+24)==OldTail+16 && Read<uint16_t>(PreviousUsed+16+28)==count,"animation buffer/count");
    }
    std::puts("Actual translated allocator: 5121 splits, 5115 complete head allocations; memset: 24672 boundary cases; animation links: 1024 counts PASS (host RAM, not ARM/device proof)");
}
