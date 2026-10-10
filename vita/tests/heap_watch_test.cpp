#include "game_heap_watch.hpp"
#include "isa/ppc_isa_context.h"
#include "memory.h"
#include "host_context.h"
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>

extern "C" void __wrap_func_80199B58(CpuContext*);
extern "C" void __wrap_func_80082AF0(CpuContext*);
namespace {
constexpr uint32_t Base=0x91000000,Tail=Base+128;
std::array<uint8_t,2*1024*1024> ram{};
bool native=false,corrupt=false;
unsigned allocations=0,animations=0;
CpuContext expected{};
void Check(bool b) { if(!b)std::abort(); }
void Put(uint32_t at,uint32_t n) { for(unsigned i=0;i<4;++i)ram[at-Base+i]=uint8_t(n>>(24-i*8)); }
}
void HostContext::RunOnNativeStack(Entry fn,void* arg) { Check(!native);native=true;fn(arg);native=false; }
bool Memory::Contains(uint32_t at,size_t n) { Check(native);return at>=Base && uint64_t(at)+n<=uint64_t(Base)+ram.size(); }
uint8_t* Memory::GetPointer(uint32_t at,size_t n) { return Contains(at,n)?ram.data()+at-Base:nullptr; }
extern "C" void __real_func_80199B58(CpuContext* cpu) {
    Check(!native && std::memcmp(cpu,&expected,sizeof(expected))==0);++allocations;cpu->gpr[3]=Base+96;
}
extern "C" void __real_func_80082AF0(CpuContext* cpu) {
    Check(!native && std::memcmp(cpu,&expected,sizeof(expected))==0);++animations;
    if(corrupt) std::memset(ram.data()+Tail-Base,0,16);
    cpu->gpr[3]=Base+512;
}
int main() {
    Put(Base,0x45585048);Put(Base+24,Base+80);Put(Base+28,Base+ram.size());
    Put(Base+60,Tail);Put(Base+64,Tail);Put(Tail,0x46520000);Put(Tail+4,ram.size()-144);
    Put(Base+516,Base);Put(Base+520,4); // Allocator fields: heap, alignment.
    for(unsigned i=0;i<32;++i) { expected.gpr[i]=i+17;expected.fpr[i].raw=i+95; }
    expected.gpr[3]=Base+512;expected.gpr[4]=96;expected.gpr[5]=4;
    auto cpu=expected;__wrap_func_80199B58(&cpu);
    auto returned=expected;returned.gpr[3]=Base+96;
    Check(std::memcmp(&cpu,&returned,sizeof(cpu))==0);
    WicEnableHeapWatch();const auto saved=ram;
    cpu=expected;__wrap_func_80199B58(&cpu);
    Check(ram==saved && std::memcmp(&cpu,&returned,sizeof(cpu))==0);
    for(unsigned i=0;i<35;++i) { cpu=expected;__wrap_func_80199B58(&cpu); }
    expected.gpr[3]=Base+512;expected.gpr[4]=Base+1024;expected.gpr[6]=6;
    cpu=expected;__wrap_func_80082AF0(&cpu);returned=expected;returned.gpr[3]=Base+512;
    Check(ram==saved && std::memcmp(&cpu,&returned,sizeof(cpu))==0);
    corrupt=true;cpu=expected;__wrap_func_80082AF0(&cpu);
    const auto bad=ram;
    Check(std::memcmp(&cpu,&returned,sizeof(cpu))==0);
    corrupt=false;cpu=expected;__wrap_func_80082AF0(&cpu);
    Check(ram==bad && std::memcmp(&cpu,&returned,sizeof(cpu))==0);
    Check(allocations==37 && animations==3 && !native);
    std::puts("Heap watch ABI/read-only/history/first-corruption contracts PASS");
}
