#pragma once

#include <bit>
#include <cstdint>

namespace FloatFmaFast {

#if defined(__GNUC__)
__attribute__((always_inline))
#endif
inline bool NormalOrZero(float value) {
    const uint32_t magnitude = std::bit_cast<uint32_t>(value) & 0x7fffffffu;
    return magnitude == 0 || (magnitude >= 0x00800000u && magnitude < 0x7f800000u);
}

// Two float32 significands have at most 48 product bits, so their product is
// exact in float64. Adding C in float64 followed by narrowing is correctly
// rounded to float32 except when the float64 sum lands on a float32 midpoint.
// Those cases, special values, subnormals and non-nearest rounding retain the
// original libm operation. This is not a non-fused float32 multiply/add.
template <class Fallback>
inline float Evaluate(float a, float b, float c, bool roundToNearest, Fallback fallback) {
    if (!roundToNearest || !NormalOrZero(a) || !NormalOrZero(b) || !NormalOrZero(c))
        return fallback(a, b, c);
    const double product = static_cast<double>(a) * static_cast<double>(b);
    const double sum = product + static_cast<double>(c);
    const uint64_t magnitude = std::bit_cast<uint64_t>(sum) & 0x7fffffffffffffffull;
    // Restrict narrowing to finite normal float32 results within FLT_MAX.
    // Zero/sign, underflow and overflow exception handling stays in libm.
    constexpr uint64_t minNormal = 0x3810000000000000ull;
    constexpr uint64_t maxNormal = 0x47efffffe0000000ull;
    constexpr uint64_t discarded = 0x1fffffffull;
    constexpr uint64_t midpoint = 0x10000000ull;
    if (magnitude < minNormal || magnitude > maxNormal ||
        (magnitude & discarded) == midpoint)
        return fallback(a, b, c);
    return static_cast<float>(sum);
}

} // namespace FloatFmaFast
