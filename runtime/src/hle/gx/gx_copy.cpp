// gx_copy.cpp - Framebuffer Copy Operations
#include "gx_internal.h"
#include "hle/gx/gx_lock.h"

#include "settings_overlay.h"
#include "gx_copy_async_policy.hpp"

#include <dolphin/gx/GXAurora.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>
#if defined(MKW_TARGET_VITA)
#include "gx/fifo.hpp"
#include <aurora/gfx.h>
#include "gfx/vita_memory_revision.hpp"
#include <array>
#endif

namespace {
#if defined(MKW_TARGET_VITA)
std::atomic_bool g_asyncEfbCopies{false};
#endif
// Copy destinations stay GPU-only until an explicit/lazy readback. Track the
// guest ranges that own those results so a later data-cache flush over a reused
// allocation can retire the stale GPU texture before it is considered by a
// subsequent GXLoadTexObj. There is at most one live range per destination;
// rewriting the destination replaces its previous extent.
GxSync::Mutex g_efbCopyDestinationsMutex;
std::map<uint32_t, uint32_t> g_efbCopyDestinations;
uint32_t g_largestEfbCopyDestination = 0;
#if defined(MKW_TARGET_VITA)
struct DeferredCopy {
    void* host=nullptr;
    uint32_t size=0,address=0;
    std::array<uint64_t,3> tokens{};
    bool materialized=false;
};
std::map<uint32_t,DeferredCopy> g_deferredCopies;
bool MaterializeColorCopy(void* user) {
    auto& copy=*static_cast<DeferredCopy*>(user);
    // All three aliases refer to one GPU result. A later read through another
    // alias must not replay that result over intervening CPU writes.
    if(copy.materialized)return true;
    if(!aurora_flush_efb_copy_to_ram(copy.host))return false;
    copy.materialized=true;
    for(auto token:copy.tokens)Memory::CancelDeferredRead(token);
    copy.tokens={};
    GxGuestWrite::NotifyWrite(copy.address,copy.size);
    aurora::vita::gfx::note_memory_write(copy.host,copy.size);
    return true;
}
void ForgetDeferredCopy(uint32_t canonical) {
    const auto it=g_deferredCopies.find(canonical);
    if(it==g_deferredCopies.end())return;
    for(auto token:it->second.tokens)Memory::CancelDeferredRead(token);
    g_deferredCopies.erase(it);
}
void DeferColorCopy(uint32_t address,uint32_t size) {
    const uint32_t canonical=CanonicalizeGxMainRamAddress(address);
    ForgetDeferredCopy(canonical);
    auto& copy=g_deferredCopies[canonical];
    copy.host=GuestToHostPtr(address,size);copy.size=size;copy.address=canonical;copy.materialized=false;
    const std::array<uint32_t,3> aliases{canonical,canonical+0x80000000u,canonical+0xc0000000u};
    for(size_t i=0;i<aliases.size();++i) {
        if(Memory::Contains(aliases[i],size))
            copy.tokens[i]=Memory::RegisterDeferredRead(aliases[i],size,&MaterializeColorCopy,&copy);
    }
}
#endif

void RememberEfbCopyDestination(uint32_t addr, uint32_t size) {
    if (size == 0) {
        return;
    }
    std::lock_guard<GxSync::Mutex> guard(g_efbCopyDestinationsMutex);
    g_efbCopyDestinations[CanonicalizeGxMainRamAddress(addr)] = size;
    // A conservative monotonic maximum lets invalidation jump directly to
    // the only map interval that could overlap instead of walking every copy
    // destination on every DCStoreRange.
    g_largestEfbCopyDestination = std::max(g_largestEfbCopyDestination, size);
}

} // namespace

#if defined(MKW_TARGET_VITA)
extern "C" void WicSetAsyncEfbCopies(bool enabled) noexcept {
    g_asyncEfbCopies.store(enabled,std::memory_order_release);
}
#endif

void InvalidateEfbCopyDestinationsForRange(uint32_t addr, uint32_t size) {
    if (size == 0) {
        return;
    }

    const uint64_t dirtyStart = CanonicalizeGxMainRamAddress(addr);
    const uint64_t dirtyEnd = dirtyStart + size;
    std::vector<uint32_t> retired;
    {
        std::lock_guard<GxSync::Mutex> guard(g_efbCopyDestinationsMutex);
        const uint64_t earliestCandidate =
            dirtyStart > g_largestEfbCopyDestination ? dirtyStart - g_largestEfbCopyDestination : 0;
        for (auto it = g_efbCopyDestinations.lower_bound(static_cast<uint32_t>(earliestCandidate));
             it != g_efbCopyDestinations.end() && static_cast<uint64_t>(it->first) < dirtyEnd;) {
            const uint64_t copyStart = it->first;
            const uint64_t copyEnd = copyStart + it->second;
            if (dirtyEnd <= copyStart || dirtyStart >= copyEnd) {
                ++it;
                continue;
            }
            retired.push_back(it->first);
            it = g_efbCopyDestinations.erase(it);
        }
    }

    // Preserve FIFO ordering: the destroy command is emitted before any later
    // texture load that can consume the freshly flushed RAM bytes.
    for (const uint32_t copyAddr : retired) {
#if defined(MKW_TARGET_VITA)
        ForgetDeferredCopy(copyAddr);
#endif
        GXDestroyCopyTex(GuestToHostPtr(copyAddr));
    }
}

// ============================================================================
// Display Copy Source/Destination
// ============================================================================

extern "C" void GX__SetDispCopySrc_8016f438(uint32_t l, uint32_t t, uint32_t w, uint32_t h) {
    GXSetDispCopySrc((u16)l, (u16)t, (u16)w, (u16)h);
}
PPC_NATIVE_OVERRIDE_VOID(8016f438, GX__SetDispCopySrc_8016f438, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));

extern "C" void GX__SetDispCopyDst_8016f4b8(uint32_t w, uint32_t h) { GXSetDispCopyDst((u16)w, (u16)h); }
PPC_NATIVE_OVERRIDE_VOID(8016f4b8, GX__SetDispCopyDst_8016f4b8, (uint32_t w, uint32_t h), (w, h));

// ============================================================================
// Texture Copy Source/Destination
// ============================================================================

extern "C" void GX__SetTexCopySrc_8016f478(uint32_t l, uint32_t t, uint32_t w, uint32_t h) {
    GXSetTexCopySrc((u16)l, (u16)t, (u16)w, (u16)h);
    g_texCopyState.srcLeft=(u16)l; g_texCopyState.srcTop=(u16)t;
    g_texCopyState.srcWidth=(u16)w; g_texCopyState.srcHeight=(u16)h;
}
PPC_NATIVE_OVERRIDE_VOID(8016f478, GX__SetTexCopySrc_8016f478, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));

extern "C" void GX__SetTexCopyDst_8016f4dc(uint32_t w, uint32_t h, uint32_t f, uint32_t m) {
    GXSetTexCopyDst((u16)w, (u16)h, (GXTexFmt)f, (GXBool)m);
    g_texCopyState.dstWidth=(u16)w; g_texCopyState.dstHeight=(u16)h;
    g_texCopyState.dstFormat=f; g_texCopyState.dstMipmap=m;
}
PPC_NATIVE_OVERRIDE_VOID(8016f4dc, GX__SetTexCopyDst_8016f4dc, (uint32_t w, uint32_t h, uint32_t f, uint32_t m), (w, h, f, m));

extern "C" void GX__SetCopyFilter_8016fa40(uint32_t aa, uint32_t spa, uint32_t vf, uint32_t vfa) {
    uint8_t sp[12][2]={}, vfb[7]={};
    if(spa) std::memcpy(sp, GuestToHostPtr(spa, 24), 24);
    if(vfa) std::memcpy(vfb, GuestToHostPtr(vfa, 7), 7);
    GXSetCopyFilter((GXBool)aa, sp, (GXBool)vf, vfb);
}
PPC_NATIVE_OVERRIDE_VOID(8016fa40, GX__SetCopyFilter_8016fa40, (uint32_t aa, uint32_t spa, uint32_t vf, uint32_t vfa), (aa, spa, vf, vfa));

extern "C" void GX__SetDispCopyGamma_8016fc24(uint32_t g) { GXSetDispCopyGamma((GXGamma)g); }
PPC_NATIVE_OVERRIDE_VOID(8016fc24, GX__SetDispCopyGamma_8016fc24, (uint32_t g), (g));

// ============================================================================
// Copy Execution
// ============================================================================

extern "C" void GX__CopyDisp_8016fc38(uint32_t da, uint32_t c) {
    EnsureAuroraFrameActive();
    // GX copies are FIFO-ordered on hardware. Drain submitted draws before
    // resolving the EFB so high-level copies see the same contents.
    GXDrawDone();
    GXCopyDisp(GuestToHostPtr(da), (GXBool)c);
    // No second GXDrawDone here: the frame-worker wait below is for the DONE
    // phase, which strictly subsumes the drain this call would perform.
    ++g_gxFrameCount;
    VI_HLE_SetXfbReady(da);
    // Present immediately so post-copy draws don't leak into this frame. Join at the DONE phase
    // (not the cheaper SEALED phase GXDrawDone waits for) because ImGui's draw lists, owned by
    // Aurora's render worker, replay during encode; aurora_end_frame would join here anyway.
    aurora_wait_for_frame_worker();
    settings_overlay::Draw();
    // Seal, pace to the VI retrace boundary (Aurora renders the sealed frame
    // during the wait), and pre-warm the next frame.
    VI_HLE_PresentFrame(/*presentedXfb=*/true, /*paceToRetrace=*/true);
}

PPC_NATIVE_OVERRIDE_VOID(8016fc38, GX__CopyDisp_8016fc38, (uint32_t da, uint32_t c), (da, c));


extern "C" void GX__CopyTex_8016fd74(uint32_t da, uint32_t c) {
    EnsureAuroraFrameActive();
    // Match GX FIFO ordering: texture copies observe all prior draws.
#if defined(MKW_TARGET_VITA)
    // Aurora queues the copy callback after the FIFO. Only keep the producer
    // wait when a draw-done callback is active, or the worker is unavailable.
    if (wiicompiled::gx::copy_tex_must_wait(
            g_asyncEfbCopies.load(std::memory_order_acquire),
            aurora::gx::fifo::worker_running(),GXVitaHasDrawDoneCallback()!=GX_FALSE))
        GXDrawDone();
#else
    GXDrawDone();
#endif
    const uint16_t rawSrcLeft = g_texCopyState.srcLeft;
    const uint16_t rawSrcTop = g_texCopyState.srcTop;
    const uint16_t rawSrcWidth = g_texCopyState.srcWidth;
    const uint16_t rawSrcHeight = g_texCopyState.srcHeight;

    // Keep the source in guest EFB coordinates. Aurora maps it to the scaled
    // EFB exactly once, matching Dolphin's ConvertEFBRectangle path.
    GXSetTexCopySrc(rawSrcLeft, rawSrcTop, rawSrcWidth, rawSrcHeight);
    // EFB copies stay GPU-only except probe-sized ones (e.g. the 4x4 lens-flare depth probe),
    // which Aurora reads back asynchronously via efb_ram::schedule and land in guest RAM a frame
    // later. RISK: copies above the probe threshold, or on the offscreen list, are not
    // auto-downloaded, so guest reads see stale RAM; call aurora_flush_efb_copies_to_ram if a
    // copy needs reading back.
    GXCopyTex(GuestToHostPtr(da), (GXBool)c);
    RememberEfbCopyDestination(
        da, GXGetTexBufferSize(g_texCopyState.dstWidth, g_texCopyState.dstHeight,
                               g_texCopyState.dstFormat, GX_FALSE, 0));
#if defined(MKW_TARGET_VITA)
    // GX color formats have no depth bit. Intercept all RAM aliases lazily so
    // CPU reads see the tiled copy without forcing readback for GPU consumers.
    if((g_texCopyState.dstFormat&0x10u)==0)
        DeferColorCopy(da,GXGetTexBufferSize(g_texCopyState.dstWidth,g_texCopyState.dstHeight,
                                           g_texCopyState.dstFormat,GX_FALSE,0));
    else {
        static bool reported=false;
        if(!reported) { std::fprintf(stderr,"[gx] depth EFB copies require native implementation\n");reported=true; }
    }
#endif
    GXSetTexCopySrc(rawSrcLeft, rawSrcTop, rawSrcWidth, rawSrcHeight);
}
PPC_NATIVE_OVERRIDE_VOID(8016fd74, GX__CopyTex_8016fd74, (uint32_t da, uint32_t c), (da, c));
