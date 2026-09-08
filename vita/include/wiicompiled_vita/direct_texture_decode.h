#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace WiiCompiledVita {
inline std::uint8_t ReadTiledI8(const std::uint8_t* data, std::uint32_t width,
                         std::uint32_t x, std::uint32_t y) noexcept {
    const std::uint32_t blocksPerRow = (width + 7u) / 8u;
    const std::size_t block = static_cast<std::size_t>(y / 4u) * blocksPerRow + x / 8u;
    return data[block * 32u + static_cast<std::size_t>(y & 3u) * 8u + (x & 7u)];
}

inline std::uint8_t ClampThpColor(int value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

inline bool DecodeDirectThp(const void* yData, const void* uData, const void* vData,
                            uint32_t width, uint32_t height, uint32_t chromaWidth,
                            uint32_t chromaHeight, uint8_t* rgba, size_t capacity) {
    if (!yData || !uData || !vData || !rgba || !width || !height || width > 1024 || height > 1024 ||
        chromaWidth != (width + 1) / 2 || chromaHeight != (height + 1) / 2 ||
        size_t(width) * height * 4 > capacity) return false;
        const auto* yPlane = static_cast<const std::uint8_t*>(yData);
        const auto* uPlane = static_cast<const std::uint8_t*>(uData);
        const auto* vPlane = static_cast<const std::uint8_t*>(vData);
        for (std::uint32_t y = 0; y < height; y += 2u) {
            const std::uint32_t chromaY = y >> 1u;
            for (std::uint32_t x = 0; x < width; x += 2u) {
                const std::uint32_t chromaX = x >> 1u;
                const int uu = static_cast<int>(ReadTiledI8(
                    uPlane, chromaWidth, chromaX, chromaY)) - 128;
                const int vv = static_cast<int>(ReadTiledI8(
                    vPlane, chromaWidth, chromaX, chromaY)) - 128;
                const int rChroma = 409 * vv;
                const int gChroma = -100 * uu - 208 * vv;
                const int bChroma = 516 * uu;
                for (std::uint32_t dy = 0; dy < 2u && y + dy < height; ++dy) {
                    for (std::uint32_t dx = 0; dx < 2u && x + dx < width; ++dx) {
                        const std::uint32_t px = x + dx;
                        const std::uint32_t py = y + dy;
                        const int yy = static_cast<int>(ReadTiledI8(
                            yPlane, width, px, py)) - 16;
                        const int c = std::max(0, yy);
                        const std::size_t offset =
                            (static_cast<std::size_t>(py) * width + px) * 4u;
                        rgba[offset + 0u] = ClampThpColor((298 * c + rChroma + 128) >> 8);
                        rgba[offset + 1u] = ClampThpColor((298 * c + gChroma + 128) >> 8);
                        rgba[offset + 2u] = ClampThpColor((298 * c + bChroma + 128) >> 8);
                        rgba[offset + 3u] = 255u;
                    }
                }
            }
        }
        return true;

}
} // namespace WiiCompiledVita
