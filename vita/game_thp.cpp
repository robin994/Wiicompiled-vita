#include "thp_native.hpp"
#include "avc_video.hpp"
#include "isa/ppc_isa_context.h"
#include "memory.h"
#include "recomp_mod_loader.h"
#include "native_stack_call.h"
#include "gfx/vita_cpu_workers.hpp"
#include <psp2/kernel/processmgr.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <cstring>

extern "C" void __real_func_801B3BAC(CpuContext*);
extern "C" void GxNotifyGuestRamDmaWrite(uint32_t address,uint32_t bytes);
extern "C" void WicInvalidateVideoTextures();
namespace {
struct Tiles {
    const wic::thp::Image* image;
    std::array<uint8_t*, 3> output;
    std::array<size_t, 4> offsets;
    std::array<uint64_t, 4> laneTiles{};
};
bool Tile(void* argument, size_t begin, size_t end, uint32_t lane) noexcept {
    auto& job = *static_cast<Tiles*>(argument);
    for (unsigned plane=0; plane<3; ++plane) {
        const auto first = std::max(begin, job.offsets[plane]), last = std::min(end, job.offsets[plane+1]);
        if (first < last) wic::thp::TileRange(job.image->planes[plane].data(), job.output[plane],
            plane ? job.image->width/2 : job.image->width, first-job.offsets[plane], last-job.offsets[plane]);
    }
    // One execution lane owns each counter, no shared increment in the jobs.
    if (lane < job.laneTiles.size()) job.laneTiles[lane] += end-begin;
    return true;
}

bool Reject(CpuContext* ctx, const char* reason) {
    struct Site { uint32_t caller=0; const char* reason=nullptr; };
    static std::array<Site,32> sites{};
    for (auto& site:sites) {
        if(site.caller==ctx->lr && site.reason==reason) break;
        if(!site.reason) {
            site={ctx->lr,reason};
            std::fprintf(stderr,"[video-native] rejected caller=0x%08x source=0x%08x reason=%s\n",ctx->lr,ctx->gpr[3],reason);
            break;
        }
    }
    return false;
}

bool TryDecode(CpuContext* ctx, bool& nativeFormat) {
    uint32_t bytes=0;
    const auto source=ctx->gpr[3];
    const uint8_t* input=nullptr;
    if(Memory::Contains(source,4)) {
        const auto* magic=Memory::GetPointer(source,4);
        nativeFormat=magic && std::memcmp(magic,"WAVC",4)==0;
    }
    if(nativeFormat) {
#if WIC_HARDWARE_AVC
        if(!Memory::Contains(source,wic::avc::HeaderBytes)) return Reject(ctx,"truncated WAVC header");
        const auto es=Memory::Read32(source+16);
        if(es>wic::avc::MaxComponentBytes-wic::avc::HeaderBytes) return Reject(ctx,"WAVC length exceeds limit");
        bytes=(uint32_t(wic::avc::HeaderBytes)+es+3)&~3u;
        if(!Memory::Contains(source,bytes)) return Reject(ctx,"WAVC source outside mapped memory");
        input=Memory::GetPointer(source,bytes);
#else
        return Reject(ctx,"WAVC requires WIC_HARDWARE_AVC");
#endif
    } else {
    // Only this RMCP01 caller supplies a verified component table and length.
    // Every other SDK-shaped call retains its translated implementation.
    if (ctx->lr != 0x80552c2cu || !ctx->gpr[7] || !ctx->gpr[23] || !ctx->gpr[30]) return Reject(ctx,"unverified original THP caller");
    const auto control = ctx->gpr[30], recordInfo = ctx->gpr[23];
    if (!Memory::Contains(control, 136) || !Memory::Contains(recordInfo, 4)) return false;
    const auto record = Memory::Read32(recordInfo), count = Memory::Read32(control+108), component = ctx->gpr[26];
    if (!count || count > 16 || component >= count || !Memory::Contains(record, 8+count*4) ||
        uint64_t(record)+8+component*4 != ctx->gpr[25]) return false;
    uint64_t start = uint64_t(record)+8+count*4;
    for (unsigned i=0; i<component; ++i) start += Memory::Read32(record+8+i*4);
    bytes = Memory::Read32(record+8+component*4);
    if (start != source || bytes < 4 || bytes > 1024*1024 || !Memory::Contains(source, bytes)) return false;
    input = Memory::GetPointer(source, bytes);
#if !WIC_NATIVE_THP
    return false;
#endif
    }
    if (!input) return Reject(ctx,"unmapped video source");
    thread_local bool busy = false;
    if (busy) return false;
    struct Restore { bool& value; ~Restore() { value=false; } } restore{busy};
    busy = true;
    thread_local wic::thp::Image image;
    std::string error;
    const auto started = sceKernelGetProcessTimeWide();
    bool okay=false,pendingPicture=false;
#if WIC_HARDWARE_AVC
    if(nativeFormat) {
        const auto result=wic::avc::DecodeHardware({input,bytes},image,error);
        if(result==wic::avc::DecodeResult::Pending) {
            // The SDK player queues this destination on success. Initialize
            // every rotating buffer even when no decoded picture is ready;
            // zero chroma is green, neutral chroma is 128. Never reuse another
            // movie's old Image just because its geometry happens to match.
            image.width=Memory::Read32(source+8);image.height=Memory::Read32(source+12);
            for(unsigned p=0;p<3;++p)
                image.planes[p].assign(size_t(image.width)*image.height/(p?4:1),p?128:0);
            pendingPicture=true;
        }
        okay=result==wic::avc::DecodeResult::Picture || pendingPicture;
    }
#endif
#if WIC_NATIVE_THP
    if(!nativeFormat) okay=wic::thp::Decode({input,bytes},image,error);
#endif
    if (!okay) {
        static unsigned failures = 0;
        if (failures++ < 8) std::fprintf(stderr, "[video-native] %s source=0x%08x bytes=%u reason=%s\n", nativeFormat?"avc-error":"thp-fallback", source, bytes, error.c_str());
        return false;
    }
    const auto decoded = sceKernelGetProcessTimeWide();
    // PlayerSetBuffer sized the destinations from this video-info record.
    // Mapped RAM alone does not prove a JPEG of different dimensions fits.
    if (!nativeFormat && (image.width != Memory::Read32(ctx->gpr[30]+128) || image.height != Memory::Read32(ctx->gpr[30]+132)))
        return Reject(ctx,"original THP metadata dimensions differ");
    Tiles job{&image, {}, {}};
    for (unsigned plane=0; plane<3; ++plane) {
        const auto address = ctx->gpr[4+plane];
        const auto size = image.planes[plane].size();
        if (!address || uint64_t(address)+size > UINT32_MAX || !Memory::Contains(address, size) ||
            MemoryInline::IsMmioAddress(address) || MemoryInline::IsMmioAddress(address+uint32_t(size)-1) ||
            RecompMod::ExecutableWriteGuardMayHit(address, uint32_t(size))) return false;
        job.output[plane] = Memory::GetPointer(address, size);
        if (!job.output[plane]) return false;
        job.offsets[plane+1] = job.offsets[plane]+size/32;
        // No guest destination is written until all three have been proved
        // writable, non-overlapping, and disjoint from the compressed source.
        const auto first = uintptr_t(job.output[plane]), last = first+size;
        if (first < uintptr_t(input)+bytes && uintptr_t(input) < last) return false;
        for (unsigned other=0; other<plane; ++other)
            if (first < uintptr_t(job.output[other])+image.planes[other].size() && uintptr_t(job.output[other]) < last) return false;
    }
    if (!aurora::vita::gfx::cpu_parallel_for_min_lanes(job.offsets[3], 128, 3, Tile, &job)) {
        // Range work is idempotent. A pool failure cannot expose half a frame.
        Tile(&job, 0, job.offsets[3], 0);
    }
    // The translated THP path uses locked-cache DMA stores. Native direct
    // writes must publish the same invalidation after every worker has joined,
    // otherwise GX/Aurora can keep the previous video frame at this address.
    for(unsigned plane=0;plane<3;++plane)
        GxNotifyGuestRamDmaWrite(ctx->gpr[4+plane],uint32_t(image.planes[plane].size()));
    // DMA publication marks guest objects dirty; the GX frontend also owns
    // texture bindings and converted sources. Invalidate after all planes are
    // complete, in FIFO order, before a draw can consume a recycled buffer.
    WicInvalidateVideoTextures();
    const auto finished = sceKernelGetProcessTimeWide();
    static uint64_t frames = 0;
    if (++frames <= 8 || frames%60 == 0 || pendingPicture)
        std::fprintf(stderr, "[%s] frame=%llu caller=0x%08x source=0x%08x bytes=%u size=%ux%u "
            "decode_us=%llu tile_us=%llu tiles=%u output=gx-i8 picture=%u helpers=%u "
            "y=0x%08x u=0x%08x v=0x%08x y0=%u u0=%u v0=%u "
            "lane0_tiles=%llu lane1_tiles=%llu lane2_tiles=%llu\n",
            nativeFormat?"avc-native":"thp-native",static_cast<unsigned long long>(frames), ctx->lr,source, bytes, image.width, image.height,
            static_cast<unsigned long long>(decoded-started), static_cast<unsigned long long>(finished-decoded),
            unsigned(job.offsets[3]),unsigned(!pendingPicture),aurora::vita::gfx::cpu_worker_threads(),
            ctx->gpr[4],ctx->gpr[5],ctx->gpr[6],unsigned(image.planes[0][0]),unsigned(image.planes[1][0]),unsigned(image.planes[2][0]),
            static_cast<unsigned long long>(job.laneTiles[0]), static_cast<unsigned long long>(job.laneTiles[1]),
            static_cast<unsigned long long>(job.laneTiles[2]));
    ctx->gpr[3] = 0;
    return true;
}
}

extern "C" __attribute__((noinline)) void __wrap_func_801B3BAC(CpuContext* ctx) {
    bool decoded = false, nativeFormat=false;
    HostContext::CallOnNativeStack([&] {
        try { decoded = TryDecode(ctx,nativeFormat); }
        catch (const std::exception& error) {
            static unsigned failures = 0;
            if (failures++ < 8) std::fprintf(stderr, "[thp-native] fallback reason=%s\n", error.what());
        }
    });
    // A converted AVC component is never sent to the PPC JPEG decoder. Its
    // ordinary nonzero SDK result lets the existing player handle the failure.
    if (!decoded && nativeFormat) ctx->gpr[3]=1;
    else if (!decoded) __real_func_801B3BAC(ctx);
}
