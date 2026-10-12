#include "psarc_backend.hpp"
#include "psarc_stall_watch.hpp"
#include "port/asset_archive.h"
#include "psarc_read_cache.hpp"
#include <atomic>
#include <climits>
#include <cstdio>
#include <limits>
#include <mutex>
#include <chrono>
#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#endif

namespace wiicompiled::vita::psarc {
namespace {
PortAssetArchive* archive=nullptr;
std::atomic<bool> mounted{false};
std::string mountedPath;
char lastError[192]="not mounted";
Stats counters{};
ReadAheadCache readCache;
uint64_t micros() {
#if defined(__vita__)
    return sceKernelGetProcessTimeWide(); // Only called by the native worker.
#else
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
struct Request {
    enum Op { Mount, Close, Size, Read, Snapshot } op;
    const char* path=nullptr;
    uint64_t offset=0,value=0;
    void* destination=nullptr;
    size_t bytes=0;
    bool okay=false;
};
// Always runs on a native stack on Vita. Stored reads use one positional I/O.
void execute(Request& r) {
    if(r.op==Request::Snapshot) { *static_cast<Stats*>(r.destination)=counters;r.okay=true;return; }
    if(r.op==Request::Mount) {
        if(archive) {
            r.okay=mountedPath==r.path;
            if(!r.okay)std::snprintf(lastError,sizeof(lastError),"another PSARC is already mounted");
            return;
        }
        archive=port_asset_archive_open(r.path,lastError,sizeof(lastError));
        if(archive) { mountedPath=r.path;r.okay=true;counters={};readCache.reset();mounted.store(true,std::memory_order_release);std::snprintf(lastError,sizeof(lastError),"ok"); }
        return;
    }
    if(r.op==Request::Close) {
        mounted.store(false,std::memory_order_release);
        port_asset_archive_close(archive);archive=nullptr;mountedPath.clear();r.okay=true;
        readCache.reset();
        std::snprintf(lastError,sizeof(lastError),"not mounted");return;
    }
    if(!archive)return;
    const int entry=port_asset_archive_find(archive,r.path);
    if(entry<0) { std::snprintf(lastError,sizeof(lastError),"file absent from PSARC: %.140s",r.path);return; }
    const uint64_t size=port_asset_archive_size(archive,static_cast<unsigned>(entry));
    if(r.op==Request::Size) { r.value=size;r.okay=true;return; }
    ++counters.reads;
    if(r.offset>size || r.bytes>size-r.offset || (!r.destination && r.bytes) || r.bytes>LONG_MAX) {
        ++counters.failures;std::snprintf(lastError,sizeof(lastError),"PSARC read outside file");return;
    }
    r.okay=readCache.read(static_cast<unsigned>(entry),size,r.offset,r.destination,r.bytes,
        [](unsigned file,uint64_t at,void* destination,size_t count) noexcept {
            const uint64_t started=micros();
            const bool okay=port_asset_archive_read(
                archive,file,destination,count,at)==static_cast<long>(count);
            counters.ioMicros+=micros()-started; // actual PSARC calls only
            return okay;
        });
    const auto physical=readCache.counters();
    counters.physicalReads=physical.physicalReads;
    counters.physicalBytes=physical.physicalBytes;
    counters.smallReads=physical.smallReads;
    counters.cacheHits=physical.cacheHits;
    counters.cacheFills=physical.cacheFills;
    counters.physicalFailures=physical.ioFailures;
    if(r.okay)counters.bytes+=r.bytes;else ++counters.failures;
    if(!r.okay)std::snprintf(lastError,sizeof(lastError),"short PSARC read");
}
#if defined(__vita__)
std::atomic_flag requestLock=ATOMIC_FLAG_INIT;
std::atomic<bool> workerBroken{false};
std::atomic<unsigned> state{0}; // 0 idle, 1 published, 2 completed.
std::atomic<uint64_t> requestSubmitted{0}, requestCompleted{0};
std::atomic<uint32_t> activeOp{0};
Request request{};
SceUID worker=-1;
SceUID readySema=-1,doneSema=-1;
void relax() { __asm__ volatile("yield" ::: "memory"); }
int threadMain(SceSize,void*) {
    for(;;) {
        // Blocking wait replaces the old 100 us polling loop on the IO core.
        // The published request and completion retain the same order.
        if(sceKernelWaitSema(readySema,1,nullptr)<0) {
            sceKernelDelayThread(1000);continue;
        }
        if(state.load(std::memory_order_acquire)!=1)continue;
        try { execute(request); }
        catch(...) { request.okay=false;std::snprintf(lastError,sizeof(lastError),"PSARC worker failed"); }
        state.store(2,std::memory_order_release);
        sceKernelSignalSema(doneSema,1);
    }
}
bool startWorker() {
    if(worker>=0)return true;
    readySema=sceKernelCreateSema("wic_io_ready",0,0,1,nullptr);
    if(readySema<0) {
        std::snprintf(lastError,sizeof(lastError),"cannot create PSARC ready semaphore");
        return false;
    }
    doneSema=sceKernelCreateSema("wic_io_done",0,0,1,nullptr);
    if(doneSema<0) {
        sceKernelDeleteSema(readySema);readySema=-1;
        std::snprintf(lastError,sizeof(lastError),"cannot create PSARC completion semaphore");
        return false;
    }
    worker=sceKernelCreateThread("wic_psarc_io",threadMain,0x10000100,64*1024,0,0,nullptr);
    if(worker<0) {
        sceKernelDeleteSema(readySema);sceKernelDeleteSema(doneSema);
        readySema=doneSema=-1;
        std::snprintf(lastError,sizeof(lastError),"cannot create PSARC worker");return false;
    }
    if(sceKernelStartThread(worker,0,nullptr)<0) {
        sceKernelDeleteThread(worker);worker=-1;
        sceKernelDeleteSema(readySema);sceKernelDeleteSema(doneSema);
        readySema=doneSema=-1;
        std::snprintf(lastError,sizeof(lastError),"cannot start PSARC worker");return false;
    }
    return true;
}
void submit(Request& r) {
    requestSubmitted.fetch_add(1,std::memory_order_relaxed);
    unsigned contentionSpins=0;
    while(requestLock.test_and_set(std::memory_order_acquire)) {
        if(++contentionSpins<32)relax();
        else {
            // Other callers may hold this lock for an entire SD-card read.
            // Never burn a second CPU core spinning through flash latency.
            sceKernelDelayThread(100); // strictly positive Vita delay
            contentionSpins=0;
        }
    }
    activeOp.store(static_cast<uint32_t>(r.op)+1u,std::memory_order_relaxed);
    if(worker>=0 && !workerBroken.load(std::memory_order_acquire)) {
        request=r;state.store(1,std::memory_order_release);
        if(sceKernelSignalSema(readySema,1)<0 ||
           sceKernelWaitSema(doneSema,1,nullptr)<0 ||
           state.load(std::memory_order_acquire)!=2) {
            // Do not submit a second request to an uncertain in-flight
            // operation. Fail future accesses instead of spinning forever.
            workerBroken.store(true,std::memory_order_release);
            r.okay=false;std::snprintf(lastError,sizeof(lastError),"PSARC worker semaphore error");
        } else {
            r=request;state.store(0,std::memory_order_release);
        }
    } else if(worker>=0) {
        r.okay=false; // worker failed previously; preserve original error
    }
    activeOp.store(0,std::memory_order_relaxed);
    requestCompleted.fetch_add(1,std::memory_order_relaxed);
    requestLock.clear(std::memory_order_release);
}
#else
std::mutex requestLock;
void submit(Request& r) { std::lock_guard<std::mutex> lock(requestLock);execute(r); }
#endif
}
bool Mount(const char* path) noexcept {
    if(!path || !*path)return false;
#if defined(__vita__)
    // Mount before guest entry. Later DVD calls only use the existing worker.
    if(!startWorker())return false;
#endif
    try { Request r{Request::Mount,path};submit(r);return r.okay; }
    catch(...) { return false; }
}
void Shutdown() noexcept { Request r{Request::Close};submit(r); }
bool IsMounted() noexcept { return mounted.load(std::memory_order_acquire); }
const char* LastError() noexcept { return lastError; }
Stats Statistics() noexcept {
    Stats stats{};Request r{Request::Snapshot};r.destination=&stats;submit(r);return stats;
}
bool Size(const std::string& path,uint64_t& bytes) noexcept {
    bytes=0;Request r{Request::Size,path.c_str()};submit(r);bytes=r.value;return r.okay;
}
bool Read(const std::string& path,uint64_t offset,void* out,size_t bytes) noexcept {
    Request r{Request::Read,path.c_str(),offset,0,out,bytes};submit(r);return r.okay;
}
bool ReadWhole(const std::string& path,std::vector<uint8_t>& out,size_t maxBytes) noexcept {
    uint64_t bytes=0;
    if(!Size(path,bytes) || bytes>maxBytes || bytes>std::numeric_limits<size_t>::max())return false;
    try {
        std::vector<uint8_t> staged(static_cast<size_t>(bytes));
        if(!Read(path,0,staged.data(),staged.size()))return false;
        out=std::move(staged);return true;
    } catch(...) { return false; }
}

#if defined(__vita__)
extern "C" void WicReadPsarcProgress(WicPsarcProgress* out) noexcept {
    if(!out)return;
    out->submitted=requestSubmitted.load(std::memory_order_relaxed);
    out->completed=requestCompleted.load(std::memory_order_relaxed);
    out->state=state.load(std::memory_order_relaxed);
    out->activeOp=activeOp.load(std::memory_order_relaxed);
}
#endif
}
