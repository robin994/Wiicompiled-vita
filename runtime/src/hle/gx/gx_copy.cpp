// gx_copy.cpp - Framebuffer Copy Operations
#include "gx_internal.h"

#include "settings_overlay.h"

#include <dolphin/gx/GXAurora.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

namespace {
// Copy destinations stay GPU-only until an explicit/lazy readback. Track the
// guest ranges that own those results so a later data-cache flush over a reused
// allocation can retire the stale GPU texture before it is considered by a
// subsequent GXLoadTexObj. There is at most one live range per destination;
// rewriting the destination replaces its previous extent.
std::mutex g_efbCopyDestinationsMutex;
std::map<uint32_t, uint32_t> g_efbCopyDestinations;
uint32_t g_largestEfbCopyDestination = 0;

#if defined(MKW_TARGET_VITA)
void LogGxBeginHot(const GxCpuPerfSnapshot& snapshot, int frame) {
    bool hasLongGap = false;
    for (uint32_t i = 0; i < snapshot.gxBeginCallerCount; ++i) {
        hasLongGap |= snapshot.gxBeginCallers[i].maxGapUs >= 100000u;
    }
    // High-draw menu frames used to emit 6-8 unbuffered stderr lines every frame,
    // which perturbed the very producer timing we are trying to measure on Vita.
    // Sample routine frames, but always retain transition stalls and very large frames.
    if (frame > 8 && (frame % 30) != 0 && snapshot.gxBeginCalls < 1000 && !hasLongGap) {
        return;
    }
    RT_LOGF(RT_TAG_GX,
            "gx_phase frame=%d prebegin_us=%llu tail_us=%llu begins=%u\n",
            frame, static_cast<unsigned long long>(snapshot.preFirstBeginUs),
            static_cast<unsigned long long>(snapshot.tailAfterLastBeginUs), snapshot.gxBeginCalls);

    std::array<uint8_t, 8> topIndices{};
    std::array<bool, GxCpuPerfSnapshot::kGxBeginCallerCapacity> selected{};
    uint32_t topCount = 0;
    for (uint32_t rank = 0; rank < 8; ++rank) {
        uint32_t best = GxCpuPerfSnapshot::kGxBeginCallerCapacity;
        uint32_t bestCount = 0;
        for (uint32_t i = 0; i < snapshot.gxBeginCallerCount; ++i) {
            if (selected[i] || snapshot.gxBeginCallers[i].count <= bestCount) {
                continue;
            }
            best = i;
            bestCount = snapshot.gxBeginCallers[i].count;
        }
        if (best == GxCpuPerfSnapshot::kGxBeginCallerCapacity) {
            break;
        }
        selected[best] = true;
        topIndices[topCount++] = static_cast<uint8_t>(best);
    }

    for (uint32_t rank = 0; rank < topCount; ++rank) {
        const auto& caller = snapshot.gxBeginCallers[topIndices[rank]];
        RT_LOGF(RT_TAG_GX,
                "gx_begin_hot frame=%d rank=%u lr=%08x count=%u total=%u gap_us=%llu max_gap_us=%llu\n",
                frame, rank, caller.lr, caller.count, snapshot.gxBeginCalls,
                static_cast<unsigned long long>(caller.gapUs),
                static_cast<unsigned long long>(caller.maxGapUs));
    }

    // Count tells us which draw path is busiest; accumulated inter-begin time tells us
    // which path actually owns producer CPU time. A low-count THP/RFL call can otherwise
    // disappear behind hundreds of cheap glyph draws.
    selected.fill(false);
    topCount = 0;
    for (uint32_t rank = 0; rank < 6; ++rank) {
        uint32_t best = GxCpuPerfSnapshot::kGxBeginCallerCapacity;
        uint64_t bestGap = 0;
        for (uint32_t i = 0; i < snapshot.gxBeginCallerCount; ++i) {
            if (selected[i] || snapshot.gxBeginCallers[i].gapUs <= bestGap) {
                continue;
            }
            best = i;
            bestGap = snapshot.gxBeginCallers[i].gapUs;
        }
        if (best == GxCpuPerfSnapshot::kGxBeginCallerCapacity) {
            break;
        }
        selected[best] = true;
        topIndices[topCount++] = static_cast<uint8_t>(best);
    }
    for (uint32_t rank = 0; rank < topCount; ++rank) {
        const auto& caller = snapshot.gxBeginCallers[topIndices[rank]];
        RT_LOGF(RT_TAG_GX,
                "gx_gap_hot frame=%d rank=%u lr=%08x gap_us=%llu max_gap_us=%llu count=%u\n",
                frame, rank, caller.lr,
                static_cast<unsigned long long>(caller.gapUs),
                static_cast<unsigned long long>(caller.maxGapUs), caller.count);
    }
}
#endif

void RememberEfbCopyDestination(uint32_t addr, uint32_t size) {
    if (size == 0) {
        return;
    }
    std::lock_guard<std::mutex> guard(g_efbCopyDestinationsMutex);
    g_efbCopyDestinations[CanonicalizeGxMainRamAddress(addr)] = size;
    // A conservative monotonic maximum lets invalidation jump directly to
    // the only map interval that could overlap instead of walking every copy
    // destination on every DCStoreRange.
    g_largestEfbCopyDestination = std::max(g_largestEfbCopyDestination, size);
}

} // namespace

void InvalidateEfbCopyDestinationsForRange(uint32_t addr, uint32_t size) {
    if (size == 0) {
        return;
    }

    const uint64_t dirtyStart = CanonicalizeGxMainRamAddress(addr);
    const uint64_t dirtyEnd = dirtyStart + size;
    std::vector<uint32_t> retired;
    {
        std::lock_guard<std::mutex> guard(g_efbCopyDestinationsMutex);
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
    GX_HLE_RecordCopyDispStart();
    const auto copyBegin = std::chrono::steady_clock::now();
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
#if defined(MKW_TARGET_VITA)
    const auto copyUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - copyBegin).count();
    const GxCpuPerfSnapshot cpuPerf = GX_HLE_TakeCpuPerfSnapshot();
    if (g_gxFrameCount <= 8 || (g_gxFrameCount % 30) == 0) {
        RT_LOGF(RT_TAG_GX,
                "gx_cpu_perf frame=%d lyt_calls=%llu lyt_us=%llu lyt_direct=%llu lyt_packet=%llu lyt_faithful=%llu "
                "dl_calls=%llu dl_us=%llu dl_bytes=%llu dl_cache=%llu/%llu dl_fallback=%llu "
                "dl_raw=%llu/%llu verts=%llu fail=%llu "
                "glyph_fast=%llu setup=%llu texload=%llu glyph_raw=%llu glyph_fallback=%llu prebegin_us=%llu tail_us=%llu copydisp_us=%llu\n",
                g_gxFrameCount,
                static_cast<unsigned long long>(cpuPerf.lytCalls),
                static_cast<unsigned long long>(cpuPerf.lytUs),
                static_cast<unsigned long long>(cpuPerf.lytDirect),
                static_cast<unsigned long long>(cpuPerf.lytPacket),
                static_cast<unsigned long long>(cpuPerf.lytFaithful),
                static_cast<unsigned long long>(cpuPerf.dlCalls),
                static_cast<unsigned long long>(cpuPerf.dlUs),
                static_cast<unsigned long long>(cpuPerf.dlBytes),
                static_cast<unsigned long long>(cpuPerf.dlCacheHits),
                static_cast<unsigned long long>(cpuPerf.dlCacheMisses),
                static_cast<unsigned long long>(cpuPerf.dlFallbacks),
                static_cast<unsigned long long>(cpuPerf.dlRawFastDraws),
                static_cast<unsigned long long>(cpuPerf.dlRawIndexedDraws),
                static_cast<unsigned long long>(cpuPerf.dlRawFastVertices),
                static_cast<unsigned long long>(cpuPerf.dlRawFastFailures),
                static_cast<unsigned long long>(cpuPerf.glyphFastCalls),
                static_cast<unsigned long long>(cpuPerf.glyphSetupCalls),
                static_cast<unsigned long long>(cpuPerf.glyphTextureLoads),
                static_cast<unsigned long long>(cpuPerf.glyphRawDirectCalls),
                static_cast<unsigned long long>(cpuPerf.glyphRawFallbacks),
                static_cast<unsigned long long>(cpuPerf.preFirstBeginUs),
                static_cast<unsigned long long>(cpuPerf.tailAfterLastBeginUs),
                static_cast<unsigned long long>(copyUs > 0 ? copyUs : 0));
    }
    LogGxBeginHot(cpuPerf, g_gxFrameCount);
#endif
}

PPC_NATIVE_OVERRIDE_VOID(8016fc38, GX__CopyDisp_8016fc38, (uint32_t da, uint32_t c), (da, c));


extern "C" void GX__CopyTex_8016fd74(uint32_t da, uint32_t c) {
#if defined(MKW_VITA_PERF_SKIP_EFB) && MKW_VITA_PERF_SKIP_EFB
    static uint64_t s_perfSkippedEfbCopies = 0;
    const uint64_t n = ++s_perfSkippedEfbCopies;
    if (n <= 16u || (n & (n - 1u)) == 0u) {
        RT_LOGF(RT_TAG_GX, "perf_probe skip_efb_copy n=%llu dest=0x%08X\n",
                static_cast<unsigned long long>(n), static_cast<unsigned>(da));
    }
    (void)c;
    return;
#endif
    EnsureAuroraFrameActive();
    // Match GX FIFO ordering: texture copies observe all prior draws.
    GXDrawDone();
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
    GXSetTexCopySrc(rawSrcLeft, rawSrcTop, rawSrcWidth, rawSrcHeight);
}
PPC_NATIVE_OVERRIDE_VOID(8016fd74, GX__CopyTex_8016fd74, (uint32_t da, uint32_t c), (da, c));
