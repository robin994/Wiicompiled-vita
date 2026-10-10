#include <psp2/kernel/processmgr.h>
#include <cstdint>
#include <cstdio>

#define NO_PROFILE __attribute__((no_instrument_function))
namespace {
struct Counter { uintptr_t function=0;uint64_t calls=0,total=0,self=0; };
struct Frame { Counter* counter;uint64_t start,children; };
thread_local Counter counters[4096];
thread_local Frame stack[1024];
thread_local unsigned depth=0;
thread_local uint64_t dropped=0,frames=0;
NO_PROFILE Counter* lookup(uintptr_t fn) {
    unsigned slot=unsigned((fn>>2)^(fn>>14))&4095;
    for(unsigned probes=0;probes<32;++probes,slot=(slot+1)&4095) {
        auto& counter=counters[slot];
        if(!counter.function)counter.function=fn;
        if(counter.function==fn)return &counter;
    }
    ++dropped;return nullptr;
}
}
extern "C" NO_PROFILE void __cyg_profile_func_enter(void* function,void*) {
    const uint64_t now=sceKernelGetProcessTimeWide();
    if(depth<1024)stack[depth]={lookup(reinterpret_cast<uintptr_t>(function)),now,0};
    else ++dropped;
    ++depth;
}
extern "C" NO_PROFILE void __cyg_profile_func_exit(void*,void*) {
    if(!depth)return;
    --depth;if(depth>=1024)return;
    const uint64_t elapsed=sceKernelGetProcessTimeWide()-stack[depth].start;
    if(auto* c=stack[depth].counter) {
        ++c->calls;c->total+=elapsed;c->self+=elapsed>=stack[depth].children?elapsed-stack[depth].children:0;
    }
    if(depth)stack[depth-1].children+=elapsed;
}
extern "C" NO_PROFILE void wic_guest_profile_frame() {
    if(++frames%300)return;
    const Counter* top[12]{};
    for(const auto& c:counters)if(c.calls) {
        for(unsigned i=0;i<12;++i)if(!top[i]||c.self>top[i]->self) {
            for(unsigned j=11;j>i;--j)top[j]=top[j-1];top[i]=&c;break;
        }
    }
    std::fprintf(stderr,"[guest-profile] frames=%llu dropped=%llu compiler_instrumentation=1\n",
        static_cast<unsigned long long>(frames),static_cast<unsigned long long>(dropped));
    for(const auto* c:top)if(c)std::fprintf(stderr,"[guest-profile] arm=%08lx calls=%llu inclusive_us=%llu translated_self_us=%llu\n",
        static_cast<unsigned long>(c->function),static_cast<unsigned long long>(c->calls),
        static_cast<unsigned long long>(c->total),static_cast<unsigned long long>(c->self));
}
