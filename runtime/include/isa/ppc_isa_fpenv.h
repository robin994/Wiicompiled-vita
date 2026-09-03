#pragma once
// FPSCR[NI] (non-IEEE flush-to-zero) modeled on the host FP environment, plus
// the thread-local mirror of that state the hot paths read instead of MXCSR/FPCR.

#include "ppc_isa_config.h"

#include <cstdint>

// Mirror guest FPSCR[NI] into the host FP control register instead of software-flushing
// denormals per operation. x86 uses MXCSR FTZ+DAZ, AArch64 uses FPCR.FZ, and Vita
// ARM32 uses VFP FPSCR.FZ.
#if defined(MKW_TARGET_VITA)
inline constexpr uint32_t kMkwHostFlushToZeroBits = 1u << 24; // ARM FPSCR.FZ
#elif defined(__x86_64__)
inline constexpr uint32_t kMkwHostFlushToZeroBits = (1u << 15) | (1u << 6); // MXCSR FTZ | DAZ
#elif defined(__aarch64__)
inline constexpr uint32_t kMkwHostFlushToZeroBits = 1u << 24; // FPCR.FZ
#else
#error "ppc_isa_fpenv.h has no host FP control register mapping for this architecture"
#endif

inline thread_local bool g_mkwHostNiActive = false;
inline constexpr double kMkwNiFlushThreshold = 0x1p-126;
inline thread_local double g_mkwNiFlushThreshold = 0.0;

inline uint32_t MkwReadHostFpControl() noexcept
{
#if defined(MKW_TARGET_VITA)
    uint32_t value = 0;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(value));
    return value;
#elif defined(__x86_64__)
    return _mm_getcsr();
#elif defined(__aarch64__)
    uint64_t fpcr = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    return static_cast<uint32_t>(fpcr);
#endif
}

inline void MkwWriteHostFpControl(uint32_t value) noexcept
{
#if defined(MKW_TARGET_VITA)
    __asm__ volatile("vmsr fpscr, %0" : : "r"(value));
#elif defined(__x86_64__)
    _mm_setcsr(value);
#elif defined(__aarch64__)
    uint64_t fpcr = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr = (fpcr & ~static_cast<uint64_t>(0xFFFFFFFFu)) | value;
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
#endif
}

inline void MkwApplyHostNiMode(uint32_t fpscr) noexcept
{
    const uint32_t control = MkwReadHostFpControl();
    const bool wantNi = (fpscr & 0x4u) != 0;
    const uint32_t want = wantNi
        ? (control | kMkwHostFlushToZeroBits)
        : (control & ~kMkwHostFlushToZeroBits);
    if (want != control)
        MkwWriteHostFpControl(want);
    g_mkwHostNiActive = wantNi;
    g_mkwNiFlushThreshold = wantNi ? kMkwNiFlushThreshold : 0.0;
}

inline void MkwRestoreHostFpControl(uint32_t control) noexcept
{
    MkwWriteHostFpControl(control);
    const bool niActive = (control & kMkwHostFlushToZeroBits) != 0;
    g_mkwHostNiActive = niActive;
    g_mkwNiFlushThreshold = niActive ? kMkwNiFlushThreshold : 0.0;
}
