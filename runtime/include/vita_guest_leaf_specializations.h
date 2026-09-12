#pragma once

#include "isa/ppc_isa_quantized.h"
#include "memory_access.h"
#include "ppc_runtime.h"

#include <atomic>
#include <cmath>
#include <cstdint>

#ifndef MKW_VITA_ROTRIG_GENERATED_FAST
#define MKW_VITA_ROTRIG_GENERATED_FAST 0
#endif

#ifndef MKW_VITA_ROTRIG_STATS
#define MKW_VITA_ROTRIG_STATS 0
#endif
#ifndef MKW_VITA_PSQ_REGION_LOWERING
#define MKW_VITA_PSQ_REGION_LOWERING 0
#endif
#ifndef MKW_VITA_PANE_GETVTXPOS_FAST
#define MKW_VITA_PANE_GETVTXPOS_FAST 0
#endif
#ifndef MKW_VITA_CONVERT_COLOR_FAST
#define MKW_VITA_CONVERT_COLOR_FAST 0
#endif

namespace VitaGuestLeafSpecializations {

struct RotTrigStats {
    uint64_t calls = 0;
    uint64_t fast = 0;
    uint64_t fallbackAxis = 0;
    uint64_t fallbackGqr = 0;
    uint64_t fallbackRange = 0;
};

#if defined(MKW_TARGET_VITA) && MKW_VITA_ROTRIG_STATS
inline std::atomic<uint64_t> g_rotTrigCalls{0};
inline std::atomic<uint64_t> g_rotTrigFast{0};
inline std::atomic<uint64_t> g_rotTrigFallbackAxis{0};
inline std::atomic<uint64_t> g_rotTrigFallbackGqr{0};
inline std::atomic<uint64_t> g_rotTrigFallbackRange{0};

inline RotTrigStats TakeRotTrigStats() noexcept {
    return {
        g_rotTrigCalls.exchange(0, std::memory_order_relaxed),
        g_rotTrigFast.exchange(0, std::memory_order_relaxed),
        g_rotTrigFallbackAxis.exchange(0, std::memory_order_relaxed),
        g_rotTrigFallbackGqr.exchange(0, std::memory_order_relaxed),
        g_rotTrigFallbackRange.exchange(0, std::memory_order_relaxed),
    };
}
#else
inline RotTrigStats TakeRotTrigStats() noexcept { return {}; }
#endif

inline float ForceSingleForKernel(double value, uint32_t fpscr) noexcept {
#if defined(MKW_TARGET_VITA) && MKW_VITA_PSQ_REGION_LOWERING
    const double threshold = (fpscr & 0x4u) != 0 ? 0x1p-126 : 0.0;
    const uint64_t bits = PpcBitCastToU64Inline(value);
    if (std::fabs(value) < threshold) [[unlikely]]
        return (bits >> 63) != 0 ? -0.0f : 0.0f;
    return static_cast<float>(value);
#else
    (void)fpscr;
    return PpcForceSingleValueInline(value);
#endif
}

// P6.66: exact fast path for PSMTXRotTrig @ 0x8019A204. The translated
// function remains the registry/dispatch winner; this helper only replaces its
// body when all memory/GQR/axis preconditions are proven.
inline bool TryPSMTXRotTrig(CpuContext* ctx) noexcept {
#if defined(MKW_TARGET_VITA) && MKW_VITA_ROTRIG_GENERATED_FAST
#if MKW_VITA_ROTRIG_STATS
    g_rotTrigCalls.fetch_add(1, std::memory_order_relaxed);
#endif
    const uint32_t r3 = ctx->gpr[3];
    const uint32_t r4 = ctx->gpr[4];
    const uint32_t axis = r4 | 32u;
    if (axis != static_cast<uint32_t>('x') &&
        axis != static_cast<uint32_t>('y') &&
        axis != static_cast<uint32_t>('z')) {
#if MKW_VITA_ROTRIG_STATS
        g_rotTrigFallbackAxis.fetch_add(1, std::memory_order_relaxed);
#endif
        return false;
    }

    // Paired stores in the reference require unscaled float store encoding.
    // Scalar W=1 stores would tolerate scale, but the complete routine cannot.
    if ((ctx->gqr[0] & 0xFFFFu) != 0u) {
#if MKW_VITA_ROTRIG_STATS
        g_rotTrigFallbackGqr.fetch_add(1, std::memory_order_relaxed);
#endif
        return false;
    }

    uint8_t* output = nullptr;
    if (!MemoryInline::TryGetWritableRangeFast(r3, 48u, output)) {
#if MKW_VITA_ROTRIG_STATS
        g_rotTrigFallbackRange.fetch_add(1, std::memory_order_relaxed);
#endif
        return false;
    }

    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    PPC_FPR f2 = ctx->fpr[2];
    PPC_FPR f3 = ctx->fpr[3];
    PPC_FPR f4 = ctx->fpr[4];
    PPC_FPR f5 = ctx->fpr[5];
    uint32_t cr = ctx->cr;
    const uint32_t xer = ctx->xer;

    f5.d = static_cast<double>(ForceSingleForKernel(f1.d, ctx->fpscr));
    const uint32_t r0 = axis;
    f4.d = static_cast<double>(ForceSingleForKernel(f2.d, ctx->fpscr));
    SetCRResident(cr, xer, 0, r0, static_cast<uint32_t>('x'));

    // Read every guest input before publishing the first output store, matching
    // the translated routine even if the destination aliases the SDA constants.
    f0.d = MemoryInline::FlatReadFloat32(ctx->gpr[2] - 26412u);
    PpcSetPairedFprInline(f2, PPC_PsNegInline(PPC_PsFromScalarInline(f5.d)));
    f1.d = MemoryInline::FlatReadFloat32(ctx->gpr[2] - 26416u);

    const auto storePair = [output, r3](uint32_t offset, double value) {
        PpcStorePairPsqFloatResolvedInline(output, offset, r3 + offset, value);
    };
    const auto storeSingle = [output, r3](uint32_t offset, double value) {
        PpcStoreSinglePsqFloatResolvedInline(output, offset, r3 + offset, value);
    };

    if (axis == static_cast<uint32_t>('x')) {
        PpcSetPairedFprInline(f3,
            PPC_PsMerge00Inline(PPC_PsFromScalarInline(f5.d), PPC_PsFromScalarInline(f4.d)));
        storeSingle(0u, PPC_PsFromScalarInline(f1.d));
        PpcSetPairedFprInline(f1, PPC_PsMerge00Inline(PPC_PsFromScalarInline(f4.d), f2.d));
        storePair(4u, PPC_PsFromScalarInline(f0.d));
        storePair(12u, PPC_PsFromScalarInline(f0.d));
        storePair(28u, PPC_PsFromScalarInline(f0.d));
        storeSingle(44u, PPC_PsFromScalarInline(f0.d));
        storePair(36u, f3.d);
        storePair(20u, f1.d);
    } else if (axis == static_cast<uint32_t>('y')) {
        SetCRResident(cr, xer, 0, r0, static_cast<uint32_t>('y'));
        PpcSetPairedFprInline(f3,
            PPC_PsMerge00Inline(PPC_PsFromScalarInline(f4.d), PPC_PsFromScalarInline(f0.d)));
        storePair(24u, PPC_PsFromScalarInline(f0.d));
        PpcSetPairedFprInline(f1,
            PPC_PsMerge00Inline(PPC_PsFromScalarInline(f0.d), PPC_PsFromScalarInline(f1.d)));
        PpcSetPairedFprInline(f2, PPC_PsMerge00Inline(f2.d, PPC_PsFromScalarInline(f0.d)));
        PpcSetPairedFprInline(f0,
            PPC_PsMerge00Inline(PPC_PsFromScalarInline(f5.d), PPC_PsFromScalarInline(f0.d)));
        storePair(0u, f3.d);
        storePair(40u, f3.d);
        storePair(16u, f1.d);
        storePair(8u, f0.d);
        storePair(32u, f2.d);
    } else {
        SetCRResident(cr, xer, 0, r0, static_cast<uint32_t>('y'));
        SetCRResident(cr, xer, 0, r0, static_cast<uint32_t>('z'));
        PpcSetPairedFprInline(f3,
            PPC_PsMerge00Inline(PPC_PsFromScalarInline(f5.d), PPC_PsFromScalarInline(f4.d)));
        storePair(8u, PPC_PsFromScalarInline(f0.d));
        PpcSetPairedFprInline(f2, PPC_PsMerge00Inline(PPC_PsFromScalarInline(f4.d), f2.d));
        PpcSetPairedFprInline(f1,
            PPC_PsMerge00Inline(PPC_PsFromScalarInline(f1.d), PPC_PsFromScalarInline(f0.d)));
        storePair(24u, PPC_PsFromScalarInline(f0.d));
        storePair(32u, PPC_PsFromScalarInline(f0.d));
        storePair(16u, f3.d);
        storePair(0u, f2.d);
        storePair(40u, f1.d);
    }

    ctx->gpr[0] = r0;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->fpr[2] = f2;
    ctx->fpr[3] = f3;
    ctx->fpr[4] = f4;
    ctx->fpr[5] = f5;
    ctx->cr = cr;
#if MKW_VITA_ROTRIG_STATS
    g_rotTrigFast.fetch_add(1, std::memory_order_relaxed);
#endif
    return true;
#else
    (void)ctx;
    return false;
#endif
}


// P6.69: exact whole-kernel specialization for nw4r::lyt::Pane::GetVtxPos.
inline bool TryPaneGetVtxPos(CpuContext* ctx) noexcept {
#if defined(MKW_TARGET_VITA) && MKW_VITA_PANE_GETVTXPOS_FAST
    const uint32_t pane = ctx->gpr[3];
    const uint32_t index = MemoryInline::FlatRead8(pane + 186u);
    const uint32_t magic = 1431655766u;
    const auto quotient3 = [magic](uint32_t value) -> uint32_t {
        const int32_t hi = static_cast<int32_t>(
            (static_cast<int64_t>(static_cast<int32_t>(magic)) *
             static_cast<int64_t>(static_cast<int32_t>(value))) >> 32);
        const uint32_t rotated = PpcRotl32Inline(static_cast<uint32_t>(hi), 1u);
        return static_cast<uint32_t>(hi) + (rotated & 1u);
    };

    const double zero = MemoryInline::FlatReadFloat32(ctx->gpr[2] - 29192u);
    const uint32_t q0 = quotient3(index);
    const uint32_t rem = index - q0 * 3u;

    PPC_FPR f0 = ctx->fpr[0];
    PPC_FPR f1 = ctx->fpr[1];
    double x = zero;
    if (rem == 1u) {
        f1.d = -MemoryInline::FlatReadFloat32(pane + 76u);
        f0.d = MemoryInline::FlatReadFloat32(ctx->gpr[2] - 29168u);
        f0.d = PpcFmulsInline(f1.d, f0.d);
        x = f0.d;
    } else if (rem == 2u) {
        f0.d = -MemoryInline::FlatReadFloat32(pane + 76u);
        x = f0.d;
    } else {
        f0.d = zero;
        x = f0.d;
    }

    const uint32_t q = quotient3(index);
    uint32_t cr = ctx->cr;
    SetCRResident(cr, ctx->xer, 0, static_cast<int32_t>(q), static_cast<int32_t>(1));
    double y = zero;
    if (q == 1u) {
        f1.d = MemoryInline::FlatReadFloat32(pane + 80u);
        f0.d = MemoryInline::FlatReadFloat32(ctx->gpr[2] - 29168u);
        f0.d = PpcFmulsInline(f1.d, f0.d);
        y = f0.d;
    } else {
        SetCRResident(cr, ctx->xer, 0, static_cast<int32_t>(q), static_cast<int32_t>(2));
        if (q == 2u) {
            f0.d = MemoryInline::FlatReadFloat32(pane + 80u);
            y = f0.d;
        } else {
            f0.d = zero;
            y = f0.d;
        }
    }

    ctx->gpr[0] = q;
    ctx->gpr[3] = MemoryInline::ConvertPpcDoubleToSingleBits(x);
    ctx->gpr[4] = MemoryInline::ConvertPpcDoubleToSingleBits(y);
    ctx->gpr[5] = index;
    ctx->fpr[0] = f0;
    ctx->fpr[1] = f1;
    ctx->cr = cr;
    return true;
#else
    (void)ctx;
    return false;
#endif
}

// P6.69: exact whole-kernel specialization for ConvertColorS10ToUT.
inline bool TryConvertColorS10ToUT(CpuContext* ctx) noexcept {
#if defined(MKW_TARGET_VITA) && MKW_VITA_CONVERT_COLOR_FAST
    const uint32_t dst = ctx->gpr[3];
    const uint32_t src = ctx->gpr[4];
    const auto readSigned = [src](uint32_t offset) -> int32_t {
        return static_cast<int16_t>(MemoryInline::FlatRead16(src + offset));
    };
    const int32_t c0 = readSigned(0u);
    const int32_t c1 = readSigned(2u);
    const int32_t c2 = readSigned(4u);
    const int32_t c3 = readSigned(6u);
    const auto clamp8 = [](int32_t value) -> uint32_t {
        if (value < 0) return 0u;
        if (value > 255) return 255u;
        return static_cast<uint32_t>(value);
    };
    const uint32_t packed = (clamp8(c0) << 24) | (clamp8(c1) << 16) |
                            (clamp8(c2) << 8) | clamp8(c3);

    uint32_t cr = ctx->cr;
    SetCRResident(cr, ctx->xer, 0, c3, 0);
    if (c3 >= 0) SetCRResident(cr, ctx->xer, 0, c3, 255);
    MemoryInline::FlatWrite32(dst, packed);

    ctx->gpr[0] = packed;
    ctx->gpr[4] = static_cast<uint32_t>(c3);
    ctx->gpr[5] = static_cast<uint32_t>(c2);
    ctx->cr = cr;
    return true;
#else
    (void)ctx;
    return false;
#endif
}

} // namespace VitaGuestLeafSpecializations
