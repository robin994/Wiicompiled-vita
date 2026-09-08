#include "wiicompiled_vita/direct_texture_decode.h"
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    std::array<uint8_t, 128> y{};
    std::array<uint8_t, 32> u{}, v{};
    std::array<uint8_t, 9 * 5 * 4 + 4> out{};
    u.fill(128); v.fill(128); y.fill(16); out.fill(0xcd);
    assert(WiiCompiledVita::DecodeDirectThp(y.data(), u.data(), v.data(), 9, 5, 5, 3,
                                          out.data(), out.size() - 4));
    for (size_t i = 0; i < 45; ++i) {
        assert(out[i * 4] == 0 && out[i * 4 + 1] == 0 && out[i * 4 + 2] == 0);
        assert(out[i * 4 + 3] == 255);
    }
    for (size_t i = 180; i < out.size(); ++i) assert(out[i] == 0xcd);
    // Pixel (8,4) lives in the fourth 8x4 tile, not the linear row offset.
    y[96] = 235;
    assert(WiiCompiledVita::DecodeDirectThp(y.data(), u.data(), v.data(), 9, 5, 5, 3,
                                          out.data(), 180));
    assert(out[176] == 255 && out[172] == 0);
    y.fill(81); u.fill(90); v.fill(240);
    assert(WiiCompiledVita::DecodeDirectThp(y.data(), u.data(), v.data(), 2, 2, 1, 1,
                                          out.data(), 16));
    for (size_t i = 0; i < 4; ++i) {
        assert(out[i * 4] >= 253 && out[i * 4 + 1] <= 1 && out[i * 4 + 2] <= 1);
    }
    assert(!WiiCompiledVita::DecodeDirectThp(y.data(), u.data(), v.data(), 9, 5, 4, 3, out.data(), 180));
    assert(!WiiCompiledVita::DecodeDirectThp(y.data(), u.data(), v.data(), 9, 5, 5, 3, out.data(), 179));
    assert(!WiiCompiledVita::DecodeDirectThp(nullptr, u.data(), v.data(), 2, 2, 1, 1, out.data(), 16));
    std::puts("direct_texture_decode: PASS (color, tiled edges, odd dimensions, bounds)");
}
