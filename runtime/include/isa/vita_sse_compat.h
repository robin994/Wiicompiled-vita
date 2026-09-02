#pragma once

#if !defined(MKW_TARGET_VITA)
#error "vita_sse_compat.h is only for the PS Vita runtime"
#endif

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Correctness-first subset of the x86 SSE API used by WiiCompiled's PPC
// semantic helpers. This deliberately preserves the existing semantic source
// and generated-code ABI while the Vita backend is brought up. Once the game
// boots, hot operations can be replaced one-by-one with NEON without changing
// the translator-facing helper API.

// AAPCS32 only guarantees an 8-byte-aligned stack at public call boundaries.
// Giving these compatibility aggregates x86's 16-byte type alignment lets GCC
// spill a by-value argument to an 8-byte stack slot and then emit a NEON load
// annotated as 128-bit aligned, which data-aborts on real Vita hardware.  The
// emulated operations below do not require native 16-byte alignment, so keep
// the 16-byte payload but use the ABI-safe 8-byte alignment.
union alignas(8) __m128 {
    float f32[4];
    uint32_t u32[4];
};

union alignas(8) __m128d {
    double f64[2];
    uint64_t u64[2];
};

union alignas(8) __m128i {
    int8_t i8[16];
    uint8_t u8[16];
    int16_t i16[8];
    uint16_t u16[8];
    int32_t i32[4];
    uint32_t u32[4];
    int64_t i64[2];
    uint64_t u64[2];
};

static_assert(sizeof(__m128) == 16);
static_assert(sizeof(__m128d) == 16);
static_assert(sizeof(__m128i) == 16);
static_assert(alignof(__m128) == 8);
static_assert(alignof(__m128d) == 8);
static_assert(alignof(__m128i) == 8);

#ifndef _MM_SHUFFLE
#define _MM_SHUFFLE(z, y, x, w) (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))
#endif

inline __m128 _mm_setzero_ps() noexcept {
    __m128 out{};
    return out;
}

inline __m128i _mm_setzero_si128() noexcept {
    __m128i out{};
    return out;
}

inline __m128 _mm_set1_ps(float value) noexcept {
    __m128 out{};
    for (float& lane : out.f32) lane = value;
    return out;
}

inline __m128 _mm_set_ss(float value) noexcept {
    __m128 out{};
    out.f32[0] = value;
    return out;
}

inline __m128d _mm_set_sd(double value) noexcept {
    __m128d out{};
    out.f64[0] = value;
    return out;
}

inline __m128i _mm_set1_epi32(int value) noexcept {
    __m128i out{};
    for (int32_t& lane : out.i32) lane = value;
    return out;
}

inline __m128i _mm_setr_epi8(
    char e0, char e1, char e2, char e3, char e4, char e5, char e6, char e7,
    char e8, char e9, char e10, char e11, char e12, char e13, char e14, char e15) noexcept {
    __m128i out{};
    const char values[16] = {e0, e1, e2, e3, e4, e5, e6, e7,
                             e8, e9, e10, e11, e12, e13, e14, e15};
    std::memcpy(out.i8, values, sizeof(values));
    return out;
}

inline __m128 _mm_castpd_ps(__m128d value) noexcept {
    __m128 out{};
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline __m128d _mm_castps_pd(__m128 value) noexcept {
    __m128d out{};
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline __m128 _mm_castsi128_ps(__m128i value) noexcept {
    __m128 out{};
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline __m128i _mm_castps_si128(__m128 value) noexcept {
    __m128i out{};
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline __m128i _mm_castpd_si128(__m128d value) noexcept {
    __m128i out{};
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline __m128d _mm_castsi128_pd(__m128i value) noexcept {
    __m128d out{};
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

inline float _mm_cvtss_f32(__m128 value) noexcept {
    return value.f32[0];
}

inline double _mm_cvtsd_f64(__m128d value) noexcept {
    return value.f64[0];
}

inline __m128 _mm_shuffle_ps(__m128 a, __m128 b, int imm) noexcept {
    __m128 out{};
    out.u32[0] = a.u32[(imm >> 0) & 3];
    out.u32[1] = a.u32[(imm >> 2) & 3];
    out.u32[2] = b.u32[(imm >> 4) & 3];
    out.u32[3] = b.u32[(imm >> 6) & 3];
    return out;
}

inline __m128 _mm_unpacklo_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    out.u32[0] = a.u32[0];
    out.u32[1] = b.u32[0];
    out.u32[2] = a.u32[1];
    out.u32[3] = b.u32[1];
    return out;
}

inline __m128 _mm_and_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.u32[i] = a.u32[i] & b.u32[i];
    return out;
}

inline __m128 _mm_andnot_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.u32[i] = ~a.u32[i] & b.u32[i];
    return out;
}

inline __m128 _mm_or_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.u32[i] = a.u32[i] | b.u32[i];
    return out;
}

inline __m128 _mm_xor_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.u32[i] = a.u32[i] ^ b.u32[i];
    return out;
}

inline __m128 _mm_cmpord_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i)
        out.u32[i] = (!std::isnan(a.f32[i]) && !std::isnan(b.f32[i])) ? 0xFFFFFFFFu : 0u;
    return out;
}

inline __m128 _mm_add_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.f32[i] = a.f32[i] + b.f32[i];
    return out;
}

inline __m128 _mm_sub_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.f32[i] = a.f32[i] - b.f32[i];
    return out;
}

inline __m128 _mm_mul_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.f32[i] = a.f32[i] * b.f32[i];
    return out;
}

inline __m128 _mm_div_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.f32[i] = a.f32[i] / b.f32[i];
    return out;
}

inline __m128 _mm_fmadd_ps(__m128 a, __m128 b, __m128 c) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.f32[i] = std::fma(a.f32[i], b.f32[i], c.f32[i]);
    return out;
}

inline __m128 _mm_fmsub_ps(__m128 a, __m128 b, __m128 c) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) out.f32[i] = std::fma(a.f32[i], b.f32[i], -c.f32[i]);
    return out;
}

inline __m128 _mm_min_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(a.f32[i]) || std::isnan(b.f32[i]) || !(a.f32[i] < b.f32[i]))
            out.u32[i] = b.u32[i];
        else
            out.u32[i] = a.u32[i];
    }
    return out;
}

inline __m128 _mm_max_ps(__m128 a, __m128 b) noexcept {
    __m128 out{};
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(a.f32[i]) || std::isnan(b.f32[i]) || !(a.f32[i] > b.f32[i]))
            out.u32[i] = b.u32[i];
        else
            out.u32[i] = a.u32[i];
    }
    return out;
}

inline __m128d _mm_andnot_pd(__m128d a, __m128d b) noexcept {
    __m128d out{};
    for (int i = 0; i < 2; ++i) out.u64[i] = ~a.u64[i] & b.u64[i];
    return out;
}

inline __m128d _mm_cmplt_sd(__m128d a, __m128d b) noexcept {
    __m128d out = a;
    out.u64[0] = (!std::isnan(a.f64[0]) && !std::isnan(b.f64[0]) && a.f64[0] < b.f64[0])
        ? UINT64_MAX : 0u;
    return out;
}

inline __m128i _mm_cvtsi64_si128(long long value) noexcept {
    __m128i out{};
    out.i64[0] = static_cast<int64_t>(value);
    return out;
}

inline long long _mm_cvtsi128_si64(__m128i value) noexcept {
    return static_cast<long long>(value.i64[0]);
}

inline int _mm_cvtsi128_si32(__m128i value) noexcept {
    return static_cast<int>(value.i32[0]);
}

inline __m128i _mm_and_si128(__m128i a, __m128i b) noexcept {
    __m128i out{};
    out.u64[0] = a.u64[0] & b.u64[0];
    out.u64[1] = a.u64[1] & b.u64[1];
    return out;
}

inline __m128i _mm_andnot_si128(__m128i a, __m128i b) noexcept {
    __m128i out{};
    out.u64[0] = ~a.u64[0] & b.u64[0];
    out.u64[1] = ~a.u64[1] & b.u64[1];
    return out;
}

inline __m128i _mm_or_si128(__m128i a, __m128i b) noexcept {
    __m128i out{};
    out.u64[0] = a.u64[0] | b.u64[0];
    out.u64[1] = a.u64[1] | b.u64[1];
    return out;
}

inline __m128i _mm_cmpgt_epi32(__m128i a, __m128i b) noexcept {
    __m128i out{};
    for (int i = 0; i < 4; ++i) out.u32[i] = a.i32[i] > b.i32[i] ? 0xFFFFFFFFu : 0u;
    return out;
}

inline __m128i _mm_cmplt_epi32(__m128i a, __m128i b) noexcept {
    __m128i out{};
    for (int i = 0; i < 4; ++i) out.u32[i] = a.i32[i] < b.i32[i] ? 0xFFFFFFFFu : 0u;
    return out;
}

inline __m128i _mm_shuffle_epi8(__m128i value, __m128i mask) noexcept {
    __m128i out{};
    for (int i = 0; i < 16; ++i) {
        const uint8_t control = mask.u8[i];
        out.u8[i] = (control & 0x80u) ? 0u : value.u8[control & 0x0Fu];
    }
    return out;
}

inline __m128i _mm_loadl_epi64(const __m128i* source) noexcept {
    __m128i out{};
    std::memcpy(&out, source, sizeof(uint64_t));
    return out;
}

inline void _mm_storel_epi64(__m128i* destination, __m128i value) noexcept {
    std::memcpy(destination, &value, sizeof(uint64_t));
}

inline __m128i _mm_cvttps_epi32(__m128 value) noexcept {
    __m128i out{};
    for (int i = 0; i < 4; ++i) {
        const double lane = static_cast<double>(value.f32[i]);
        if (!std::isfinite(lane) || lane < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
            lane > static_cast<double>(std::numeric_limits<int32_t>::max())) {
            out.i32[i] = std::numeric_limits<int32_t>::min();
        } else {
            out.i32[i] = static_cast<int32_t>(std::trunc(lane));
        }
    }
    return out;
}

inline __m128i _mm_packs_epi32(__m128i a, __m128i b) noexcept {
    __m128i out{};
    auto saturate = [](int32_t value) -> int16_t {
        if (value > std::numeric_limits<int16_t>::max()) return std::numeric_limits<int16_t>::max();
        if (value < std::numeric_limits<int16_t>::min()) return std::numeric_limits<int16_t>::min();
        return static_cast<int16_t>(value);
    };
    for (int i = 0; i < 4; ++i) out.i16[i] = saturate(a.i32[i]);
    for (int i = 0; i < 4; ++i) out.i16[i + 4] = saturate(b.i32[i]);
    return out;
}

inline __m128i _mm_packus_epi16(__m128i a, __m128i b) noexcept {
    __m128i out{};
    auto saturate = [](int16_t value) -> uint8_t {
        if (value < 0) return 0;
        if (value > 255) return 255;
        return static_cast<uint8_t>(value);
    };
    for (int i = 0; i < 8; ++i) out.u8[i] = saturate(a.i16[i]);
    for (int i = 0; i < 8; ++i) out.u8[i + 8] = saturate(b.i16[i]);
    return out;
}
