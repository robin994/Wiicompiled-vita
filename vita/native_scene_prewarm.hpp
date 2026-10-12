#pragma once
// Stable, versioned WiiCompiled scene identity for sidecar CPU prefetch.
// These are guest-state identifiers only; never cache GXM handles or pointers.
#include <cstdint>
namespace wiicompiled::vita::assets::prewarm {
inline constexpr uint64_t scene_key(uint32_t section,uint32_t scene,
                                    uint32_t course=255,uint32_t character=255,
                                    uint32_t kart=255) noexcept {
    return (uint64_t(section & 255u)<<40) | (uint64_t(scene & 255u)<<32) |
           (uint64_t(course & 255u)<<24) | (uint64_t(character & 255u)<<16) |
           (uint64_t(kart & 255u)<<8) | 1u;
}
} // namespace wiicompiled::vita::assets::prewarm
