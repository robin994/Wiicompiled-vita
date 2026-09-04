#include "libco.h"
#include "memory_access.h"
#include "isa/ppc_isa_quantized.h"
#include "wiicompiled_vita/gx_backend.h"
#include "wiicompiled_vita/host_jobs.h"

#include <aurora/aurora.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/gx/GXCull.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXPixel.h>
#include <dolphin/gx/GXTev.h>
#include <dolphin/gx/GXTexture.h>
#include <dolphin/gx/GXTransform.h>
#include <dolphin/gx/GXVert.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace aurora::gx::fifo {
bool submit_raw_draw(GXPrimitive primitive, GXVtxFmt fmt, const uint8_t* vertices,
                     uint16_t vtxCount, uint32_t vertexBytes);
}

namespace {

static_assert(sizeof(uintptr_t) == 4, "The Vita port must remain an ARM32 build");

cothread_t g_mainFiber = nullptr;
cothread_t g_testFiber = nullptr;
volatile int g_sequence = 0;

struct JobProbeState {
    std::atomic<int> sum{0};
    std::atomic<int> executed{0};
    std::atomic<int> workerAffinity{0};
    std::atomic<int> workerStackFree{0};
};

struct JobProbeItem {
    JobProbeState* state = nullptr;
    int value = 0;
};

void JobProbe(void* rawContext) noexcept {
    auto* item = static_cast<JobProbeItem*>(rawContext);
    item->state->sum.fetch_add(item->value, std::memory_order_relaxed);
    item->state->executed.fetch_add(1, std::memory_order_release);

    const SceUID threadId = sceKernelGetThreadId();
    item->state->workerAffinity.store(sceKernelGetThreadCpuAffinityMask(threadId),
                                     std::memory_order_relaxed);
    item->state->workerStackFree.store(sceKernelGetThreadStackFreeSize(threadId),
                                      std::memory_order_relaxed);
}

void Log(const char* message);

template <size_t N>
void AppendBE16(std::array<uint8_t, N>& bytes, size_t& cursor, uint16_t value) {
    bytes[cursor++] = static_cast<uint8_t>(value >> 8);
    bytes[cursor++] = static_cast<uint8_t>(value);
}

template <size_t N>
void AppendBE32(std::array<uint8_t, N>& bytes, size_t& cursor, uint32_t value) {
    bytes[cursor++] = static_cast<uint8_t>(value >> 24);
    bytes[cursor++] = static_cast<uint8_t>(value >> 16);
    bytes[cursor++] = static_cast<uint8_t>(value >> 8);
    bytes[cursor++] = static_cast<uint8_t>(value);
}

template <size_t N>
void BeginXfPacket(std::array<uint8_t, N>& bytes, size_t& cursor,
                   uint16_t address, uint16_t words) {
    bytes[cursor++] = 0x10;
    AppendBE32(bytes, cursor,
               (static_cast<uint32_t>(words - 1u) << 16) | address);
}

template <size_t N>
void AppendBEFloat(std::array<uint8_t, N>& bytes, size_t& cursor, float value) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    bytes[cursor++] = static_cast<uint8_t>(raw >> 24);
    bytes[cursor++] = static_cast<uint8_t>(raw >> 16);
    bytes[cursor++] = static_cast<uint8_t>(raw >> 8);
    bytes[cursor++] = static_cast<uint8_t>(raw);
}

template <size_t N>
void AppendColor(std::array<uint8_t, N>& bytes, size_t& cursor,
                 uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    bytes[cursor++] = r;
    bytes[cursor++] = g;
    bytes[cursor++] = b;
    bytes[cursor++] = a;
}

void StoreBE16(uint8_t* dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>(value >> 8);
    dst[1] = static_cast<uint8_t>(value);
}

uint16_t PackRGB565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t PackRGB5A3(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a >= 224) {
        return static_cast<uint16_t>(0x8000u | ((r >> 3) << 10) |
                                     ((g >> 3) << 5) | (b >> 3));
    }
    return static_cast<uint16_t>(((a >> 5) << 12) | ((r >> 4) << 8) |
                                 ((g >> 4) << 4) | (b >> 4));
}

bool RunMulticoreSmokeCheck() {
    using namespace WiiCompiledVita;

    Log("[JOBS] configuring guest thread on USER_0");
    if (!ConfigureCurrentThread(HostThreadRole::Guest)) {
        Log("[JOBS] guest affinity configuration failed");
        return false;
    }
    if (sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId()) != SCE_KERNEL_CPU_MASK_USER_0) {
        Log("[JOBS] guest affinity verification failed");
        return false;
    }

    Log("[JOBS] starting background worker");
    HostJobSystem jobs;
    if (!jobs.start()) {
        Log("[JOBS] background worker creation failed");
        return false;
    }
    Log("[JOBS] background worker created");

    JobProbeState state;
    std::array<JobProbeItem, 32> items{};
    HostJobFence fence;
    Log("[JOBS] submitting 32 jobs");
    for (size_t i = 0; i < items.size(); ++i) {
        items[i] = JobProbeItem{&state, static_cast<int>(i + 1)};
        if (!jobs.submit(&JobProbe, &items[i], &fence)) {
            Log("[JOBS] submission failed; stopping worker");
            jobs.stop();
            return false;
        }
    }
    Log("[JOBS] submissions complete; waiting up to 3 seconds for fence");
    if (!fence.waitFor(3 * 1000 * 1000)) {
        char message[128];
        std::snprintf(message, sizeof(message),
                      "[JOBS] fence timeout: executed=%d sum=%d",
                      state.executed.load(std::memory_order_acquire),
                      state.sum.load(std::memory_order_relaxed));
        Log(message);
        Log("[JOBS] stopping worker after timeout");
        jobs.stop();
        return false;
    }
    Log("[JOBS] fence completed; stopping worker");
    jobs.stop();
    Log("[JOBS] background worker stopped");

    const int sum = state.sum.load(std::memory_order_relaxed);
    const int executed = state.executed.load(std::memory_order_acquire);
    const int workerMask = state.workerAffinity.load(std::memory_order_relaxed);
    const int workerStackFree = state.workerStackFree.load(std::memory_order_relaxed);
    char result[160];
    std::snprintf(result, sizeof(result),
                  "[JOBS] result: executed=%d sum=%d affinity=0x%X stackFree=%d",
                  executed, sum, workerMask, workerStackFree);
    Log(result);

    if (executed != 32 || sum != 528) {
        Log("[JOBS] result validation failed");
        return false;
    }

    const int helperMask = SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2;
    if (workerMask <= 0 || (workerMask & SCE_KERNEL_CPU_MASK_USER_0) != 0 ||
        (workerMask & helperMask) == 0) {
        Log("[JOBS] worker affinity validation failed");
        return false;
    }

    // The wrapper requests a 96 KiB worker stack. Leave generous headroom for
    // the probe itself while still detecting a fallback to Vita's tiny default.
    if (workerStackFree <= 32 * 1024) {
        Log("[JOBS] worker stack validation failed");
        return false;
    }
    return true;
}

bool RunRenderWorkerSmokeCheck() {
    Log("[GX] initializing USER_1 vitaGL render worker");
    if (!WiiCompiledVita::GxBackend::Initialize()) {
        Log("[GX] render worker or vitaGL initialization failed");
        return false;
    }
    Log("[GX] render worker initialized; beginning frame");
    if (!aurora_begin_frame()) {
        Log("[GX] begin frame failed");
        WiiCompiledVita::GxBackend::Shutdown();
        return false;
    }
    {
        char message[96];
        std::snprintf(message, sizeof(message), "[GX] main stack free before draws=%d",
                      sceKernelGetThreadStackFreeSize(sceKernelGetThreadId()));
        Log(message);
    }

    // Preserve the five already-hardware-validated geometry/XF draws while the
    // raster-state milestone is introduced below.
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-0.65f, -0.55f, 0.0f);
    GXColor4u8(255, 64, 64, 255);
    GXPosition3f32(0.65f, -0.55f, 0.0f);
    GXColor4u8(64, 255, 96, 255);
    GXPosition3f32(0.0f, 0.65f, 0.0f);
    GXColor4u8(64, 128, 255, 255);
    GXEnd();

    // Exercise the same packed-direct path used by the HLE FIFO fast path.
    std::array<uint8_t, 64> directBytes{};
    size_t directCursor = 0;
    AppendBEFloat(directBytes, directCursor, -0.90f);
    AppendBEFloat(directBytes, directCursor, 0.35f);
    AppendBEFloat(directBytes, directCursor, 0.0f);
    AppendColor(directBytes, directCursor, 255, 220, 32, 255);
    AppendBEFloat(directBytes, directCursor, -0.35f);
    AppendBEFloat(directBytes, directCursor, 0.35f);
    AppendBEFloat(directBytes, directCursor, 0.0f);
    AppendColor(directBytes, directCursor, 255, 128, 32, 255);
    AppendBEFloat(directBytes, directCursor, -0.625f);
    AppendBEFloat(directBytes, directCursor, 0.90f);
    AppendBEFloat(directBytes, directCursor, 0.0f);
    AppendColor(directBytes, directCursor, 255, 255, 255, 255);
    if (!aurora::gx::fifo::submit_raw_draw(
            GX_TRIANGLES, GX_VTXFMT0, directBytes.data(), 3,
            static_cast<uint32_t>(directCursor))) {
        Log("[GX] packed direct raw draw decode failed");
        WiiCompiledVita::GxBackend::Shutdown();
        return false;
    }

    // Exercise indexed attributes too. These arrays are host-native little
    // endian, while the index stream itself remains Wii/FIFO big endian.
    struct IndexedPosition { float x, y, z; };
    const std::array<IndexedPosition, 3> indexedPositions{{
        {0.35f, 0.35f, 0.0f},
        {0.90f, 0.35f, 0.0f},
        {0.625f, 0.90f, 0.0f},
    }};
    const std::array<std::array<uint8_t, 4>, 3> indexedColors{{
        {{64, 220, 255, 255}},
        {{160, 96, 255, 255}},
        {{255, 255, 255, 255}},
    }};
    GXSetVtxDesc(GX_VA_POS, GX_INDEX16);
    GXSetVtxDesc(GX_VA_CLR0, GX_INDEX8);
    GXSetArray(GX_VA_POS, indexedPositions.data(), sizeof(indexedPositions),
               sizeof(IndexedPosition), true);
    GXSetArray(GX_VA_CLR0, indexedColors.data(), sizeof(indexedColors),
               sizeof(indexedColors[0]), true);

    std::array<uint8_t, 64> indexedBytes{};
    size_t indexedCursor = 0;
    for (uint16_t index = 0; index < 3; ++index) {
        AppendBE16(indexedBytes, indexedCursor, index);
        indexedBytes[indexedCursor++] = static_cast<uint8_t>(index);
    }
    if (!aurora::gx::fifo::submit_raw_draw(
            GX_TRIANGLES, GX_VTXFMT0, indexedBytes.data(), 3,
            static_cast<uint32_t>(indexedCursor))) {
        Log("[GX] indexed raw draw decode failed");
        WiiCompiledVita::GxBackend::Shutdown();
        return false;
    }

    // The first three draws above were captured under identity transforms. Change
    // the live GX matrices now so the same frame also verifies per-draw snapshots.
    // The projection halves X/Y, while each vertex selects a different PN matrix.
    // Correct output is a small magenta/cyan triangle centered near the bottom.
    const float projection[4][4] = {
        {0.5f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.5f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    const float pn0[3][4] = {
        {1.0f, 0.0f, 0.0f, -0.5f},
        {0.0f, 1.0f, 0.0f, -1.4f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
    const float pn1[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, -1.4f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
    const float pn2[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.5f},
        {0.0f, 1.0f, 0.0f, -1.4f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
    GXSetProjection(projection, GX_ORTHOGRAPHIC);
    GXLoadPosMtxImm(pn0, GX_PNMTX0);
    GXLoadPosMtxImm(pn1, GX_PNMTX1);
    GXLoadPosMtxImm(pn2, GX_PNMTX2);
    GXSetCurrentMtx(GX_PNMTX0);

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_PNMTXIDX, GX_DIRECT);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    std::array<uint8_t, 64> matrixBytes{};
    size_t matrixCursor = 0;
    const auto appendMatrixVertex = [&](u8 matrixIndex, float x, float y, float z,
                                        u8 r, u8 g, u8 b) {
        matrixBytes[matrixCursor++] = matrixIndex;
        AppendBEFloat(matrixBytes, matrixCursor, x);
        AppendBEFloat(matrixBytes, matrixCursor, y);
        AppendBEFloat(matrixBytes, matrixCursor, z);
        AppendColor(matrixBytes, matrixCursor, r, g, b, 255);
    };
    appendMatrixVertex(static_cast<u8>(GX_PNMTX0), -0.1f, -0.1f, 0.0f, 255, 64, 224);
    appendMatrixVertex(static_cast<u8>(GX_PNMTX1), 0.0f, 0.3f, 0.0f, 64, 255, 224);
    appendMatrixVertex(static_cast<u8>(GX_PNMTX2), 0.1f, -0.1f, 0.0f, 192, 96, 255);
    if (!aurora::gx::fifo::submit_raw_draw(
            GX_TRIANGLES, GX_VTXFMT0, matrixBytes.data(), 3,
            static_cast<uint32_t>(matrixCursor))) {
        Log("[GX] PN-matrix raw draw decode failed");
        WiiCompiledVita::GxBackend::Shutdown();
        return false;
    }

    // Apply the next transform exclusively through raw XF packets. The real
    // display-list/FIFO HLE routes these same packets to ApplyXfPacket on Vita.
    const float xfPn3[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.3f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    std::array<uint8_t, 64> xfPosition{};
    size_t xfPositionCursor = 0;
    BeginXfPacket(xfPosition, xfPositionCursor,
                  static_cast<uint16_t>(GX_PNMTX3 * 4u), 12);
    for (float value : xfPn3) {
        AppendBEFloat(xfPosition, xfPositionCursor, value);
    }

    std::array<uint8_t, 64> xfProjection{};
    size_t xfProjectionCursor = 0;
    BeginXfPacket(xfProjection, xfProjectionCursor, 0x1020u, 7);
    AppendBEFloat(xfProjection, xfProjectionCursor, 0.5f);
    AppendBEFloat(xfProjection, xfProjectionCursor, 0.0f);
    AppendBEFloat(xfProjection, xfProjectionCursor, 0.5f);
    AppendBEFloat(xfProjection, xfProjectionCursor, 0.0f);
    AppendBEFloat(xfProjection, xfProjectionCursor, 1.0f);
    AppendBEFloat(xfProjection, xfProjectionCursor, 0.0f);
    AppendBE32(xfProjection, xfProjectionCursor,
               static_cast<uint32_t>(GX_ORTHOGRAPHIC));

    std::array<uint8_t, 64> xfMatrixIndex{};
    size_t xfMatrixIndexCursor = 0;
    BeginXfPacket(xfMatrixIndex, xfMatrixIndexCursor, 0x1018u, 1);
    AppendBE32(xfMatrixIndex, xfMatrixIndexCursor,
               static_cast<uint32_t>(GX_PNMTX3));

    std::array<uint8_t, 64> xfViewport{};
    size_t xfViewportCursor = 0;
    BeginXfPacket(xfViewport, xfViewportCursor, 0x101Au, 6);
    AppendBEFloat(xfViewport, xfViewportCursor, 320.0f);
    AppendBEFloat(xfViewport, xfViewportCursor, -240.0f);
    AppendBEFloat(xfViewport, xfViewportCursor, 16777216.0f);
    AppendBEFloat(xfViewport, xfViewportCursor, 660.0f);
    AppendBEFloat(xfViewport, xfViewportCursor, 580.0f);
    AppendBEFloat(xfViewport, xfViewportCursor, 16777216.0f);

    if (!WiiCompiledVita::GxBackend::ApplyXfPacket(
            xfPosition.data(), static_cast<uint32_t>(xfPositionCursor)) ||
        !WiiCompiledVita::GxBackend::ApplyXfPacket(
            xfProjection.data(), static_cast<uint32_t>(xfProjectionCursor)) ||
        !WiiCompiledVita::GxBackend::ApplyXfPacket(
            xfMatrixIndex.data(), static_cast<uint32_t>(xfMatrixIndexCursor)) ||
        !WiiCompiledVita::GxBackend::ApplyXfPacket(
            xfViewport.data(), static_cast<uint32_t>(xfViewportCursor))) {
        Log("[GX] raw XF packet decode failed");
        WiiCompiledVita::GxBackend::Shutdown();
        return false;
    }

    // G3D shape display lists normally load their PN/normal matrices through
    // GX_LOAD_INDX_A/B rather than ordinary LOAD_XF_REG packets. Exercise the
    // Vita-specific indexed-XF bridge directly so the probe catches a replay
    // path that merely binds the source array without updating XF state.
    std::array<uint8_t, 48> indexedXfMatrix{};
    size_t indexedXfCursor = 0;
    for (float value : xfPn3) {
        AppendBEFloat(indexedXfMatrix, indexedXfCursor, value);
    }
    const uint32_t indexedXfValue =
        ((12u - 1u) << 12u) | ((static_cast<uint32_t>(GX_PNMTX4) * 4u) & 0x0FFFu);
    if (!WiiCompiledVita::GxBackend::ApplyIndexedXfPacket(
            indexedXfValue, indexedXfMatrix.data(), static_cast<uint32_t>(indexedXfCursor))) {
        Log("[GX] indexed XF packet decode failed");
        WiiCompiledVita::GxBackend::Shutdown();
        return false;
    }

    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-0.35f, -0.20f, 0.0f);
    GXColor4u8(255, 220, 32, 255);
    GXPosition3f32(0.0f, 0.35f, 0.0f);
    GXColor4u8(255, 255, 255, 255);
    GXPosition3f32(0.35f, -0.20f, 0.0f);
    GXColor4u8(255, 128, 32, 255);
    GXEnd();

    // Reset the transform state after the raw-XF draw and make depth/culling
    // visually discriminant. The left pair is near-green then far-red: with
    // GX_LESS the green triangle must remain visible. The right pair is a CW
    // cyan triangle followed by the same geometry in CCW red winding: with
    // GX_CULL_BACK and GX's CW front-face convention only cyan must remain.
    const float identityProjection[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    const float identityPn[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
    GXSetProjection(identityProjection, GX_ORTHOGRAPHIC);
    GXLoadPosMtxImm(identityPn, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetViewport(0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    const auto drawSolidTriangle = [](float x0, float y0, float x1, float y1,
                                      float x2, float y2, float z,
                                      u8 r, u8 g, u8 b) {
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition3f32(x0, y0, z); GXColor4u8(r, g, b, 255);
        GXPosition3f32(x1, y1, z); GXColor4u8(r, g, b, 255);
        GXPosition3f32(x2, y2, z); GXColor4u8(r, g, b, 255);
        GXEnd();
    };

    // Use raw BP register semantics for the new raster tests. This is the same
    // state path used by display-list and live-FIFO BP commands on Vita.
    GXApplyBPReg(0x00, 0u << 14); // GX_CULL_NONE in hardware genMode.
    GXApplyBPReg(0x40, 0x1u | (static_cast<u32>(GX_LESS) << 1) | 0x10u);
    drawSolidTriangle(-0.85f, -0.75f, -0.55f, -0.25f, -0.25f, -0.75f,
                      -0.5f, 32, 255, 96);
    drawSolidTriangle(-0.85f, -0.75f, -0.55f, -0.25f, -0.25f, -0.75f,
                      0.5f, 255, 32, 32);

    GXApplyBPReg(0x40, static_cast<u32>(GX_ALWAYS) << 1);
    // Hardware front/back encoding is opposite to the public GX convention.
    GXApplyBPReg(0x00, static_cast<u32>(GX_CULL_FRONT) << 14);
    drawSolidTriangle(0.25f, -0.75f, 0.55f, -0.25f, 0.85f, -0.75f,
                      0.0f, 32, 220, 255);
    drawSolidTriangle(0.25f, -0.75f, 0.85f, -0.75f, 0.55f, -0.25f,
                      0.0f, 255, 32, 32);

    GXApplyBPReg(0x00, static_cast<u32>(GX_CULL_ALL) << 14);
    drawSolidTriangle(-0.15f, -0.90f, 0.0f, -0.60f, 0.15f, -0.90f,
                      0.0f, 64, 96, 255);

    // Texture milestone: three 4x4 Wii-native textures are decoded/uploaded on
    // USER_1. Each panel has asymmetric quadrants so channel order and T
    // orientation are visible on hardware, not merely inferred from counters.
    std::array<uint8_t, 32> rgb565Texture{};
    std::array<uint8_t, 32> rgb5a3Texture{};
    std::array<uint8_t, 64> rgba8Texture{};
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            const size_t pixel = y * 4u + x;
            uint8_t r = 255, g = 255, b = 255, a = 255;
            if (y < 2 && x < 2) { r = 255; g = 32; b = 32; }
            else if (y < 2) { r = 32; g = 255; b = 64; }
            else if (x < 2) { r = 32; g = 96; b = 255; }
            StoreBE16(rgb565Texture.data() + pixel * 2u, PackRGB565(r, g, b));

            if (y < 2 && x < 2) { r = 255; g = 224; b = 32; a = 255; }
            else if (y < 2) { r = 32; g = 224; b = 255; a = 255; }
            else if (x < 2) { r = 255; g = 32; b = 224; a = 160; }
            else { r = 255; g = 112; b = 32; a = 96; }
            StoreBE16(rgb5a3Texture.data() + pixel * 2u, PackRGB5A3(r, g, b, a));

            if (y < 2 && x < 2) { r = 255; g = 255; b = 255; a = 255; }
            else if (y < 2) { r = 255; g = 32; b = 32; a = 255; }
            else if (x < 2) { r = 32; g = 255; b = 64; a = 255; }
            else { r = 32; g = 96; b = 255; a = 255; }
            rgba8Texture[pixel * 2u] = a;
            rgba8Texture[pixel * 2u + 1u] = r;
            rgba8Texture[32u + pixel * 2u] = g;
            rgba8Texture[32u + pixel * 2u + 1u] = b;
        }
    }

    GXApplyBPReg(0x00, 0u << 14);
    GXApplyBPReg(0x40, static_cast<u32>(GX_ALWAYS) << 1);
    GXSetNumTexGens(1);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    const auto drawTexturedQuad = [](float x0, float y0, float x1, float y1) {
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(x0, y1, -0.8f); GXTexCoord2f32(0.0f, 0.0f);
        GXPosition3f32(x0, y0, -0.8f); GXTexCoord2f32(0.0f, 1.0f);
        GXPosition3f32(x1, y0, -0.8f); GXTexCoord2f32(1.0f, 1.0f);
        GXPosition3f32(x1, y1, -0.8f); GXTexCoord2f32(1.0f, 0.0f);
        GXEnd();
    };

    GXTexObj rgb565Obj{};
    GXTexObj rgb5a3Obj{};
    GXTexObj rgba8Obj{};
    GXInitTexObj(&rgb565Obj, rgb565Texture.data(), 4, 4, GX_TF_RGB565,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&rgb565Obj, GX_TEXMAP0);
    drawTexturedQuad(-0.66f, -0.12f, -0.26f, 0.20f);

    // Replay half of the RGB565 panel through the packed raw path. It should be
    // visually identical and, on USER_1, hit the texture cache populated above.
    std::array<uint8_t, 64> rawTextureBytes{};
    size_t rawTextureCursor = 0;
    const auto appendRawTextureVertex = [&](float x, float y, float s, float t) {
        AppendBEFloat(rawTextureBytes, rawTextureCursor, x);
        AppendBEFloat(rawTextureBytes, rawTextureCursor, y);
        AppendBEFloat(rawTextureBytes, rawTextureCursor, -0.8f);
        AppendBEFloat(rawTextureBytes, rawTextureCursor, s);
        AppendBEFloat(rawTextureBytes, rawTextureCursor, t);
    };
    appendRawTextureVertex(-0.66f, 0.20f, 0.0f, 0.0f);
    appendRawTextureVertex(-0.66f, -0.12f, 0.0f, 1.0f);
    appendRawTextureVertex(-0.26f, -0.12f, 1.0f, 1.0f);
    if (!aurora::gx::fifo::submit_raw_draw(
            GX_TRIANGLES, GX_VTXFMT0, rawTextureBytes.data(), 3,
            static_cast<uint32_t>(rawTextureCursor))) {
        Log("[GX] textured raw-direct draw decode failed");
        WiiCompiledVita::GxBackend::Shutdown();
        return false;
    }

    GXInitTexObj(&rgb5a3Obj, rgb5a3Texture.data(), 4, 4, GX_TF_RGB5A3,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&rgb5a3Obj, GX_TEXMAP0);
    drawTexturedQuad(-0.20f, -0.12f, 0.20f, 0.20f);

    GXInitTexObj(&rgba8Obj, rgba8Texture.data(), 4, 4, GX_TF_RGBA8,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&rgba8Obj, GX_TEXMAP0);
    drawTexturedQuad(0.26f, -0.12f, 0.66f, 0.20f);

    // Common Wii texture formats used heavily by real game assets. Keep these
    // as level-0 native tiled payloads so the Vita decoder, not the probe, does
    // all layout conversion. Stage-0 TEV order is applied through raw BP state.
    std::array<uint8_t, 32> i4Texture{};
    std::array<uint8_t, 32> i8Texture{};
    std::array<uint8_t, 32> ia4Texture{};
    std::array<uint8_t, 32> ia8Texture{};
    std::array<uint8_t, 32> cmprTexture{};
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            const uint8_t level = static_cast<uint8_t>(
                y < 4 ? (x < 4 ? 0xFu : 0xAu) : (x < 4 ? 0x5u : 0x1u));
            const size_t pixel = y * 8u + x;
            uint8_t& packed = i4Texture[pixel / 2u];
            if ((pixel & 1u) == 0) packed = static_cast<uint8_t>(level << 4u);
            else packed = static_cast<uint8_t>(packed | level);
        }
    }
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            const size_t pixel = y * 8u + x;
            i8Texture[pixel] = static_cast<uint8_t>(
                y < 2 ? (x < 4 ? 240 : 160) : (x < 4 ? 96 : 32));
            const uint8_t intensity = static_cast<uint8_t>(x < 4 ? 0xFu : 0x6u);
            const uint8_t alpha = static_cast<uint8_t>(y < 2 ? 0xFu : 0x6u);
            ia4Texture[pixel] = static_cast<uint8_t>((alpha << 4u) | intensity);
        }
    }
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            const size_t pixel = y * 4u + x;
            ia8Texture[pixel * 2u] = x < 2 ? 255 : 64;
            ia8Texture[pixel * 2u + 1u] = y < 2 ? 255 : 96;
        }
    }
    const std::array<uint16_t, 4> cmprColors{
        PackRGB565(255, 32, 32), PackRGB565(32, 255, 64),
        PackRGB565(32, 96, 255), PackRGB565(255, 224, 32),
    };
    for (size_t block = 0; block < cmprColors.size(); ++block) {
        uint8_t* out = cmprTexture.data() + block * 8u;
        StoreBE16(out, cmprColors[block]);
        StoreBE16(out + 2u, 0);
        out[4] = out[5] = out[6] = out[7] = 0; // selector 0: solid color0
    }

    // BP tref0: stage 0 uses TEXCOORD0/TEXMAP0, texture enabled.
    GXApplyBPReg(0x28, 1u << 6u);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXTexObj i4Obj{}, i8Obj{}, ia4Obj{}, ia8Obj{}, cmprObj{};

    GXInitTexObj(&i4Obj, i4Texture.data(), 8, 8, GX_TF_I4,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&i4Obj, GX_TEXMAP0);
    drawTexturedQuad(-0.90f, -0.97f, -0.58f, -0.80f);

    GXInitTexObj(&i8Obj, i8Texture.data(), 8, 4, GX_TF_I8,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&i8Obj, GX_TEXMAP0);
    drawTexturedQuad(-0.54f, -0.97f, -0.22f, -0.80f);

    // BP cmode0: enable SrcAlpha/InvSrcAlpha blending and color/alpha writes.
    const uint32_t alphaBlendBp = 0x1u | (1u << 3u) | (1u << 4u) |
        (static_cast<uint32_t>(GX_BL_INVSRCALPHA) << 5u) |
        (static_cast<uint32_t>(GX_BL_SRCALPHA) << 8u);
    GXApplyBPReg(0x41, alphaBlendBp);
    GXInitTexObj(&ia4Obj, ia4Texture.data(), 8, 4, GX_TF_IA4,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&ia4Obj, GX_TEXMAP0);
    drawTexturedQuad(-0.18f, -0.97f, 0.14f, -0.80f);

    // Disable blending, then use the common "alpha > 127 AND always" cutout.
    GXApplyBPReg(0x41, (1u << 3u) | (1u << 4u));
    const uint32_t alphaCompareBp = 127u |
        (static_cast<uint32_t>(GX_GREATER) << 16u) |
        (static_cast<uint32_t>(GX_ALWAYS) << 19u) |
        (static_cast<uint32_t>(GX_AOP_AND) << 22u);
    GXApplyBPReg(0xF3, alphaCompareBp);
    GXInitTexObj(&ia8Obj, ia8Texture.data(), 4, 4, GX_TF_IA8,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&ia8Obj, GX_TEXMAP0);
    drawTexturedQuad(0.18f, -0.97f, 0.50f, -0.80f);

    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXInitTexObj(&cmprObj, cmprTexture.data(), 8, 8, GX_TF_CMPR,
                 GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXLoadTexObj(&cmprObj, GX_TEXMAP0);
    drawTexturedQuad(0.54f, -0.97f, 0.86f, -0.80f);

    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetNumTexGens(0);

    aurora_end_frame();
    Log("[GX] frame submitted; waiting up to 3 seconds for presentation");
    if (!aurora_wait_for_frame_worker_for(3 * 1000 * 1000)) {
        Log("[GX] frame presentation timed out");
        WiiCompiledVita::GxBackend::Shutdown();
        return false;
    }

    const auto stats = WiiCompiledVita::GxBackend::SnapshotStats();
    char result[3072];
    std::snprintf(result, sizeof(result),
                  "[GX] result: submitted=%llu completed=%llu presented=%llu gpu=%d fallback=%d affinity=0x%X stackFree=%d queuedDraws=%llu queuedVertices=%llu geometryDraws=%llu geometryVertices=%llu dropped=%llu transformed=%llu pnMatrixVertices=%llu transformFailed=%llu rawDecoded=%llu rawFailed=%llu directAttrs=%llu indexedAttrs=%llu xfPackets=%llu xfWords=%llu xfPos=%llu xfNrm=%llu xfProjection=%llu xfViewport=%llu xfMtxIdx=%llu xfUnsupported=%llu xfIndexed=%llu/%llu depthCompare=%llu depthWrite=%llu cullNone=%llu cullFront=%llu cullBack=%llu cullAllSkipped=%llu blend=%llu blendFallback=%llu alphaTest=%llu alphaFallback=%llu tevSimple=%llu tevFallback=%llu textureDraws=%llu texHits=%llu texMisses=%llu texUploads=%llu texUploadFailed=%llu texUnsupported=%llu texSourceRace=%llu texMipFallback=%llu texBytes=%llu rgb565Uploads=%llu rgb5a3Uploads=%llu rgba8Uploads=%llu rgba8PcUploads=%llu i4Uploads=%llu i8Uploads=%llu ia4Uploads=%llu ia8Uploads=%llu cmprUploads=%llu quads=%llu triangles=%llu vtxFmt0=%llu",
                  static_cast<unsigned long long>(stats.framesSubmitted),
                  static_cast<unsigned long long>(stats.framesCompleted),
                  static_cast<unsigned long long>(stats.framesPresented),
                  stats.gpuInitialized ? 1 : 0,
                  stats.resolutionFallback ? 1 : 0,
                  static_cast<unsigned int>(stats.renderAffinityMask),
                  stats.renderStackFree,
                  static_cast<unsigned long long>(stats.drawCalls),
                  static_cast<unsigned long long>(stats.vertices),
                  static_cast<unsigned long long>(stats.geometryDrawsPresented),
                  static_cast<unsigned long long>(stats.geometryVerticesPresented),
                  static_cast<unsigned long long>(stats.geometryVerticesDropped),
                  static_cast<unsigned long long>(stats.geometryVerticesTransformed),
                  static_cast<unsigned long long>(stats.geometryPnMatrixVertices),
                  static_cast<unsigned long long>(stats.geometryTransformFailures),
                  static_cast<unsigned long long>(stats.rawDrawsDecoded),
                  static_cast<unsigned long long>(stats.rawDrawDecodeFailures),
                  static_cast<unsigned long long>(stats.rawDirectAttributesDecoded),
                  static_cast<unsigned long long>(stats.rawIndexedAttributesDecoded),
                  static_cast<unsigned long long>(stats.xfPacketsApplied),
                  static_cast<unsigned long long>(stats.xfWordsApplied),
                  static_cast<unsigned long long>(stats.xfPositionMatrixWords),
                  static_cast<unsigned long long>(stats.xfNormalMatrixWords),
                  static_cast<unsigned long long>(stats.xfProjectionWrites),
                  static_cast<unsigned long long>(stats.xfViewportWrites),
                  static_cast<unsigned long long>(stats.xfMatrixIndexWrites),
                  static_cast<unsigned long long>(stats.xfUnsupportedWords),
                  static_cast<unsigned long long>(stats.xfIndexedLoads),
                  static_cast<unsigned long long>(stats.xfIndexedWords),
                  static_cast<unsigned long long>(stats.geometryDepthCompareDraws),
                  static_cast<unsigned long long>(stats.geometryDepthWriteDraws),
                  static_cast<unsigned long long>(stats.geometryCullNoneDraws),
                  static_cast<unsigned long long>(stats.geometryCullFrontDraws),
                  static_cast<unsigned long long>(stats.geometryCullBackDraws),
                  static_cast<unsigned long long>(stats.geometryCullAllSkipped),
                  static_cast<unsigned long long>(stats.geometryBlendDraws),
                  static_cast<unsigned long long>(stats.geometryBlendFallbackDraws),
                  static_cast<unsigned long long>(stats.geometryAlphaTestDraws),
                  static_cast<unsigned long long>(stats.geometryAlphaCompareFallbackDraws),
                  static_cast<unsigned long long>(stats.geometryTevSimpleDraws),
                  static_cast<unsigned long long>(stats.geometryTevFallbackDraws),
                  static_cast<unsigned long long>(stats.textureDrawsPresented),
                  static_cast<unsigned long long>(stats.textureCacheHits),
                  static_cast<unsigned long long>(stats.textureCacheMisses),
                  static_cast<unsigned long long>(stats.textureUploads),
                  static_cast<unsigned long long>(stats.textureUploadFailures),
                  static_cast<unsigned long long>(stats.textureUnsupportedDraws),
                  static_cast<unsigned long long>(stats.textureSourceRaceDraws),
                  static_cast<unsigned long long>(stats.textureMipFallbackDraws),
                  static_cast<unsigned long long>(stats.textureBytesUploaded),
                  static_cast<unsigned long long>(stats.textureRgb565Uploads),
                  static_cast<unsigned long long>(stats.textureRgb5a3Uploads),
                  static_cast<unsigned long long>(stats.textureRgba8Uploads),
                  static_cast<unsigned long long>(stats.textureRgba8PcUploads),
                  static_cast<unsigned long long>(stats.textureI4Uploads),
                  static_cast<unsigned long long>(stats.textureI8Uploads),
                  static_cast<unsigned long long>(stats.textureIa4Uploads),
                  static_cast<unsigned long long>(stats.textureIa8Uploads),
                  static_cast<unsigned long long>(stats.textureCmprUploads),
                  static_cast<unsigned long long>(stats.primitiveDraws[0]),
                  static_cast<unsigned long long>(stats.primitiveDraws[1]),
                  static_cast<unsigned long long>(stats.vertexFormatDraws[0]));
    Log(result);
    const bool ok =
        stats.framesSubmitted >= 1 &&
        stats.framesCompleted >= stats.framesSubmitted &&
        stats.framesPresented >= stats.framesSubmitted &&
        stats.gpuInitialized &&
        !stats.resolutionFallback &&
        stats.drawCalls == 19 &&
        stats.vertices == 65 &&
        stats.geometryDrawsPresented == 18 &&
        stats.geometryVerticesPresented == 62 &&
        stats.geometryVerticesDropped == 0 &&
        stats.geometryVerticesTransformed == 62 &&
        stats.geometryPnMatrixVertices == 3 &&
        stats.geometryTransformFailures == 0 &&
        stats.rawDrawsDecoded == 4 &&
        stats.rawDrawDecodeFailures == 0 &&
        stats.rawDirectAttributesDecoded == 21 &&
        stats.rawIndexedAttributesDecoded == 6 &&
        stats.xfPacketsApplied == 5 &&
        stats.xfWordsApplied == 38 &&
        stats.xfPositionMatrixWords == 24 &&
        stats.xfNormalMatrixWords == 0 &&
        stats.xfProjectionWrites == 7 &&
        stats.xfViewportWrites == 6 &&
        stats.xfMatrixIndexWrites == 1 &&
        stats.xfUnsupportedWords == 0 &&
        stats.xfIndexedLoads == 1 &&
        stats.xfIndexedWords == 12 &&
        stats.geometryDepthCompareDraws == 2 &&
        stats.geometryDepthWriteDraws == 2 &&
        stats.geometryCullNoneDraws == 16 &&
        stats.geometryCullFrontDraws == 0 &&
        stats.geometryCullBackDraws == 2 &&
        stats.geometryCullAllSkipped == 1 &&
        stats.geometryBlendDraws == 1 &&
        stats.geometryBlendFallbackDraws == 0 &&
        stats.geometryAlphaTestDraws == 1 &&
        stats.geometryAlphaCompareFallbackDraws == 0 &&
        stats.geometryTevSimpleDraws == 9 &&
        stats.geometryTevFallbackDraws == 0 &&
        stats.textureDrawsPresented == 9 &&
        stats.textureCacheHits == 1 &&
        stats.textureCacheMisses == 8 &&
        stats.textureUploads == 8 &&
        stats.textureUploadFailures == 0 &&
        stats.textureUnsupportedDraws == 0 &&
        stats.textureSourceRaceDraws == 0 &&
        stats.textureMipFallbackDraws == 0 &&
        stats.textureBytesUploaded == 1024 &&
        stats.textureRgb565Uploads == 1 &&
        stats.textureRgb5a3Uploads == 1 &&
        stats.textureRgba8Uploads == 1 &&
        stats.textureRgba8PcUploads == 0 &&
        stats.textureI4Uploads == 1 &&
        stats.textureI8Uploads == 1 &&
        stats.textureIa4Uploads == 1 &&
        stats.textureIa8Uploads == 1 &&
        stats.textureCmprUploads == 1 &&
        stats.primitiveDraws[0] == 8 &&
        stats.primitiveDraws[1] == 11 &&
        stats.vertexFormatDraws[0] == 19 &&
        stats.renderAffinityMask == SCE_KERNEL_CPU_MASK_USER_1 &&
        stats.renderStackFree > 48 * 1024;
    if (ok) {
        Log("[GX] holding 7 validated triangles + 8 textured panels for 1.5 seconds");
        sceKernelDelayThread(1500 * 1000);
    }
    Log("[GX] shutting down render worker");
    WiiCompiledVita::GxBackend::Shutdown();
    Log("[GX] render worker stopped");
    return ok;
}

bool RunGuestMemorySmokeCheck() {
    const std::vector<GuestFlat::RegionRequest> regions = {
        {0x00000000u, 4096u, GuestFlat::Backing::Mem1},
        {0x80000000u, 4096u, GuestFlat::Backing::Mem1},
        {0xC0000000u, 4096u, GuestFlat::Backing::Mem1},
        {0x10000000u, 4096u, GuestFlat::Backing::Mem2},
        {0x90000000u, 4096u, GuestFlat::Backing::Mem2},
    };

    GuestFlat::Initialize(regions);
    uint8_t* mem1Physical = GuestFlat::HostPointer(0x00000020u);
    uint8_t* mem1Cached = GuestFlat::HostPointer(0x80000020u);
    uint8_t* mem1Uncached = GuestFlat::HostPointer(0xC0000020u);
    uint8_t* mem2Physical = GuestFlat::HostPointer(0x10000040u);
    uint8_t* mem2Cached = GuestFlat::HostPointer(0x90000040u);
    if (!mem1Physical || !mem1Cached || !mem1Uncached || !mem2Physical || !mem2Cached)
        return false;

    *mem1Physical = 0x5Au;
    if (*mem1Cached != 0x5Au || *mem1Uncached != 0x5Au)
        return false;

    *mem2Cached = 0xA5u;
    return *mem2Physical == 0xA5u;
}

bool RunIsaSmokeCheck() {
    const double packed = PpcPackPairedInline(1.25f, -2.5f);
    if (PpcGetPs0Inline(packed) != 1.25f || PpcGetPs1Inline(packed) != -2.5f)
        return false;

    const uint64_t signalingNanPair = 0x7F800001FF800001ull;
    const uint64_t quieted = PpcLoadPairPsqFloatBitsPackedInline(signalingNanPair);
    if (quieted != 0x7FC00001FFC00001ull)
        return false;

    const uint32_t originalFpControl = MkwReadHostFpControl();
    MkwApplyHostNiMode(0x4u);
    const bool niEnabled = g_mkwHostNiActive;
    MkwRestoreHostFpControl(originalFpControl);
    return niEnabled;
}

void Log(const char* message) {
    sceIoMkdir("ux0:data/wiicompiled-vita", 0777);
    SceUID fd = sceIoOpen("ux0:data/wiicompiled-vita/porting_probe.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, message, static_cast<SceSize>(std::strlen(message)));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
    std::printf("%s\n", message);
}

void FiberEntry() {
    g_sequence = 1;

    // This syscall is intentional: raw ARM stack swapping was unsafe on Vita.
    // A successful return here proves the SceFiber backend is running on a
    // kernel-supported context before the full Wii scheduler is enabled.
    sceKernelDelayThread(1000);
    g_sequence = 2;
    co_switch(g_mainFiber);

    g_sequence = 3;
    sceKernelDelayThread(1000);
    g_sequence = 4;
    co_switch(g_mainFiber);

    for (;;) {
        co_switch(g_mainFiber);
    }
}

} // namespace

int main() {
    Log("WiiCompiled Vita porting probe: start");

    if (!RunGuestMemorySmokeCheck()) {
        Log("FAIL: Wii guest-memory alias smoke check");
        sceKernelExitProcess(5);
        return 5;
    }
    Log("PASS: Wii guest-memory alias smoke check");

    if (!RunIsaSmokeCheck()) {
        Log("FAIL: portable PPC ISA/FPU smoke check");
        sceKernelExitProcess(4);
        return 4;
    }
    Log("PASS: portable PPC ISA/FPU smoke check");

    if (!RunMulticoreSmokeCheck()) {
        Log("FAIL: Vita multicore host-job smoke check");
        sceKernelExitProcess(6);
        return 6;
    }
    Log("PASS: Vita multicore affinity/stack/job smoke check");

    if (!RunRenderWorkerSmokeCheck()) {
        Log("FAIL: Vita GX render-worker smoke check");
        sceKernelExitProcess(7);
        return 7;
    }
    Log("PASS: Vita GX render worker on USER_1");

    g_mainFiber = co_active();
    g_testFiber = co_create(64 * 1024, FiberEntry);
    if (!g_mainFiber || !g_testFiber) {
        Log("FAIL: unable to initialize SceFiber-backed libco");
        sceKernelExitProcess(1);
        return 1;
    }

    co_switch(g_testFiber);
    if (g_sequence != 2) {
        Log("FAIL: first fiber switch/syscall did not complete");
        sceKernelExitProcess(2);
        return 2;
    }
    Log("PASS: first SceFiber switch and kernel syscall");

    co_switch(g_testFiber);
    if (g_sequence != 4) {
        Log("FAIL: second fiber resume did not complete");
        sceKernelExitProcess(3);
        return 3;
    }
    Log("PASS: resumed SceFiber context");

    co_delete(g_testFiber);
    g_testFiber = nullptr;

    Log("WiiCompiled Vita porting probe: PASS");
    sceKernelExitProcess(0);
    return 0;
}
