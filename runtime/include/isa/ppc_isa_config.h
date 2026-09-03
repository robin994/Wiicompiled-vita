#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#define MKW_RESTRICT __restrict
#if defined(MKW_TARGET_VITA)
#include "vita_sse_compat.h"
#include <arm_neon.h>
#elif defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#else
#error "ppc_isa_config.h has no SIMD intrinsics header for this architecture"
#endif

static constexpr bool MkwStateFreeAbiEnabled(uint32_t) noexcept
{
#if defined(MKW_TARGET_VITA)
    // ARM32 hardware validation is opt-in. The translator already limits this
    // ABI to small closed regions (<=4 input values, <=2 outputs), but keep the
    // conservative CpuContext path as the default until real Vita tests prove
    // the AAPCS variant correct and worthwhile. State-free builds use a
    // separate translated-object directory so they can be A/B tested safely.
#if defined(MKW_VITA_STATE_FREE_ABI) && MKW_VITA_STATE_FREE_ABI
    return true;
#else
    return false;
#endif
#else
    return true;
#endif
}

#if defined(_WIN32)
#define MKW_PPC_FORCE_INLINE __forceinline
#define MKW_PPC_NO_INLINE __declspec(noinline)
#define MKW_PPC_INTERNAL_CALL __regcall
#else
// __forceinline/__declspec are MS-extension keywords Clang only recognizes when targeting
// Windows (MSVC or mingw); native Linux Clang needs the GNU-attribute spellings instead.
// __regcall has no portable non-Windows equivalent worth chasing here - the extra register
// args it saves matter for the hot PPC interpreter loop on Windows, but plain calls are fine
// elsewhere.
#define MKW_PPC_FORCE_INLINE __attribute__((always_inline)) inline
#define MKW_PPC_NO_INLINE __attribute__((noinline))
#define MKW_PPC_INTERNAL_CALL
#endif
#define MKW_PPC_ALWAYS_INLINE_BODY __attribute__((always_inline))
#define MKW_PPC_COLD __attribute__((cold))


#if defined(MKW_TARGET_VITA)
struct alignas(16) MkwStateFreeResult2 {
    uint64_t lanes[2];

    uint64_t& operator[](size_t index) noexcept { return lanes[index]; }
    const uint64_t& operator[](size_t index) const noexcept { return lanes[index]; }
};
#else
using MkwStateFreeResult2 = uint64_t __attribute__((ext_vector_type(2)));
#endif
