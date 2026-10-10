#include "game_heap_watch.hpp"
#include "isa/ppc_isa_context.h"
#include "memory.h"
#include "native_stack_call.h"
#include <array>
#include <cstdio>

extern "C" void __real_func_80199B58(CpuContext*);
extern "C" void __real_func_80082AF0(CpuContext*);
namespace {
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
struct Heap {
    uint32_t address=0,start=0,end=0;
    const uint8_t* ram=nullptr;
    bool reported=false;
    struct Allocation { uint32_t caller=0,bytes=0,alignment=0,result=0,tail=0; };
    std::array<Allocation,32> history{};
    unsigned calls=0;
    uint32_t Word(uint32_t at) const { return Be32(ram+at-address); }
    bool Contains(uint32_t at,uint32_t n) const { return at>=start && uint64_t(at)+n<=end; }
};
thread_local bool enabled=false;
thread_local std::array<Heap,8> heaps{};
struct Allocator { uint32_t address=0;const uint8_t* ram=nullptr; };
thread_local std::array<Allocator,16> allocators{};

const uint8_t* TrackAllocator(uint32_t address) {
    if(!enabled) return nullptr;
    for(auto& allocator:allocators) if(allocator.address==address) return allocator.ram;
    const uint8_t* result=nullptr;
    HostContext::CallOnNativeStack([&] {
        if(!address || !Memory::Contains(address,12)) return;
        const auto* ram=Memory::GetPointer(address,12);
        if(!ram) return;
        for(auto& allocator:allocators) if(!allocator.address) {
            allocator={address,ram};result=ram;return;
        }
    });
    return result;
}

Heap* Track(uint32_t address) {
    if(!enabled || address<0x90000000 || address>=0x94000000) return nullptr;
    for(auto& heap:heaps) if(heap.address==address) return &heap;
    // Resolve the complete contiguous mapping once. No native-stack switch,
    // allocation, or guest write is added to subsequent allocation calls.
    Heap* result=nullptr;
    HostContext::CallOnNativeStack([&] {
        if(!Memory::Contains(address,80)) return;
        const auto* head=Memory::GetPointer(address,80);
        if(!head || Be32(head)!=0x45585048) return;
        const auto start=Be32(head+24),end=Be32(head+28);
        if(start<address+80 || end<=start || end>0x94000000 || end-start<1024*1024) return;
        const auto* ram=Memory::GetPointer(address,end-address);
        if(!ram) return;
        for(auto& heap:heaps) if(!heap.address) {
            heap.address=address;heap.start=start;heap.end=end;heap.ram=ram;result=&heap;
            std::fprintf(stderr,"[heap-watch] enabled heap=0x%08x start=0x%08x end=0x%08x repairs=0 history=32\n",address,start,end);
            return;
        }
    });
    return result;
}
void Inspect(Heap& heap,const char* phase,uint32_t caller,uint32_t object=0,uint32_t resource=0,uint32_t count=0) {
    if(heap.reported) return;
    const uint32_t tail=heap.Word(heap.address+64),first=heap.Word(heap.address+60);
    bool valid=(!tail && !first);
    uint32_t tag=0,bytes=0,prev=0,next=0;
    if(tail && first && heap.Contains(tail,16)) {
        tag=heap.Word(tail);bytes=heap.Word(tail+4);prev=heap.Word(tail+8);next=heap.Word(tail+12);
        valid=(tag>>16)==0x4652 && uint64_t(tail)+16+bytes<=heap.end && !next &&
              ((!prev && first==tail) || (prev && heap.Contains(prev,16) && heap.Word(prev+12)==tail));
    }
    if(valid) return;
    heap.reported=true;
    // Log the first observed corruption, before layout's null-root assertion.
    // Before/after markers distinguish damage inside a call from prior damage;
    // the call history alone does not identify an arbitrary external writer.
    HostContext::CallOnNativeStack([&] {
        std::fprintf(stderr,"[heap-watch-corruption] phase=%s caller=0x%08x heap=0x%08x first=0x%08x tail=0x%08x tag=0x%08x bytes=%u prev=0x%08x next=0x%08x calls=%u object=0x%08x resource=0x%08x link_count=%u repairs=0\n",
            phase,caller,heap.address,first,tail,tag,bytes,prev,next,heap.calls,object,resource,count);
        const auto begin=heap.calls>heap.history.size()?heap.calls-heap.history.size():0;
        for(unsigned i=begin;i<heap.calls;++i) {
            const auto& a=heap.history[i%heap.history.size()];
            std::fprintf(stderr,"[heap-watch-alloc] sequence=%u caller=0x%08x bytes=%u alignment=%d result=0x%08x tail=0x%08x\n",i,a.caller,a.bytes,int32_t(a.alignment),a.result,a.tail);
        }
        if(object && Memory::Contains(object,32)) {
            const auto* data=Memory::GetPointer(object,32);
            if(data) std::fprintf(stderr,"[heap-watch-animation] object=0x%08x table=0x%08x resource=0x%08x textures=0x%08x links=0x%08x counts=0x%08x\n",
                object,Be32(data),Be32(data+12),Be32(data+20),Be32(data+24),Be32(data+28));
        }
    });
}
}
void WicEnableHeapWatch() { enabled=true; }
extern "C" void WicObserveDeferredRead(uint32_t request,size_t requestBytes,uint32_t copy,size_t copyBytes,bool after) {
    if(!enabled)return;
    const uint64_t start=copy&0x3fffffffu,end=start+copyBytes;
    for(auto& heap:heaps) {
        // An earlier expansion heap can have been destroyed and recycled.
        if(!heap.address || heap.reported || heap.Word(heap.address)!=0x45585048)continue;
        const uint64_t heapStart=heap.address&0x3fffffffu,heapEnd=heap.end&0x3fffffffu;
        if(start>=heapEnd || end<=heapStart)continue;
        const auto tail=heap.Word(heap.address+64);
        if(!after && tail && heap.Contains(tail,16) && (tail&0x3fffffffu)<end && uint64_t(tail&0x3fffffffu)+16>start) {
            HostContext::CallOnNativeStack([&] {
                std::fprintf(stderr,"[heap-watch-deferred] request=0x%08x request_bytes=%zu copy=0x%08x copy_bytes=%zu heap=0x%08x tail=0x%08x tag=0x%08x phase=before-readback\n",
                             request,requestBytes,copy,copyBytes,heap.address,tail,heap.Word(tail));
            });
        }
        if(after)Inspect(heap,"deferred-after",request);
    }
}
extern "C" __attribute__((noinline)) void __wrap_func_80199B58(CpuContext* ctx) {
    const auto bytes=ctx->gpr[4],caller=ctx->lr;
    const auto* allocator=TrackAllocator(ctx->gpr[3]);
    const auto address=allocator?Be32(allocator+4):0,alignment=allocator?Be32(allocator+8):0;
    auto* heap=Track(address);
    if(heap) Inspect(*heap,"alloc-before",caller);
    __real_func_80199B58(ctx);
    if(heap) {
        heap->history[heap->calls++%heap->history.size()]={caller,bytes,alignment,ctx->gpr[3],heap->Word(address+64)};
        Inspect(*heap,"alloc-after",caller);
    }
}
extern "C" __attribute__((noinline)) void __wrap_func_80082AF0(CpuContext* ctx) {
    const auto caller=ctx->lr,object=ctx->gpr[3],resource=ctx->gpr[4],count=ctx->gpr[6];
    if(enabled) for(auto& heap:heaps) if(heap.address) Inspect(heap,"animation-before",caller,object,resource,count);
    __real_func_80082AF0(ctx);
    if(enabled) for(auto& heap:heaps) if(heap.address) Inspect(heap,"animation-after",caller,object,resource,count);
}
