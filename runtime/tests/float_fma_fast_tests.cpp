#include "float_fma_fast.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
unsigned fallbacks = 0;
float Reference(float a, float b, float c) { ++fallbacks; return std::fma(a, b, c); }
void Check(float a, float b, float c) {
    const float expected = std::fma(a, b, c);
    const float actual = FloatFmaFast::Evaluate(a, b, c, true, Reference);
    if (std::bit_cast<uint32_t>(actual) != std::bit_cast<uint32_t>(expected)) {
        std::fprintf(stderr, "FMA mismatch: %a * %a + %a: %a / %a\n", a, b, c, actual, expected);
        std::abort();
    }
}
uint32_t randomState = 0x94fe317bu;
uint32_t Next() { randomState ^= randomState << 13; randomState ^= randomState >> 17;
    randomState ^= randomState << 5; return randomState; }
}
int main() {
    const float cases[] = {0.f, -0.f, 1.f, -1.f, 0x1.000002p0f, 0x1.fffffep0f,
        0x1p-24f, -0x1p-24f, 0x1p-100f, -0x1p-100f, 0x1p100f, -0x1p100f,
        std::numeric_limits<float>::min(), std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()};
    for (float a : cases) for (float b : cases) for (float c : cases) Check(a, b, c);
    // A true float32 midpoint in the double sum must use libm, including a
    // contribution far below double precision which moves an exact tie.
    const auto before = fallbacks;
    Check(0x1.000002p0f, 0x1.8p0f, 0x1p-100f);
    if (fallbacks != before + 1) std::abort();
    if (FloatFmaFast::Evaluate(1.f, 2.f, 3.f, false, Reference) != 5.f) std::abort();
    unsigned fastOrdinary = 0;
    for (unsigned i = 0; i < 2000000; ++i) {
        Check(std::bit_cast<float>(Next()), std::bit_cast<float>(Next()), std::bit_cast<float>(Next()));
        // Matrix/vector-like inputs, including cancellation and broad scales.
        const float a = static_cast<int32_t>(Next()) * 0x1p-25f;
        const float b = static_cast<int32_t>(Next()) * 0x1p-25f;
        const float c = static_cast<int32_t>(Next()) * 0x1p-25f;
        const auto old = fallbacks; Check(a, b, c); fastOrdinary += fallbacks == old;
        if ((i % 16) == 0) Check(a, b, -static_cast<float>(static_cast<double>(a) * b));
    }
    if (fastOrdinary < 1900000) std::abort();
    std::printf("Float32 FMA: over 4 million bit comparisons, including cancellation, passed; ordinary fast cases=%u/2000000\n", fastOrdinary);
}
