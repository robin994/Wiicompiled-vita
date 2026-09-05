#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace aurora::vita::gfx {
// Same nearest-neighbour grid as the reference GX sampled-copy path. Pitches
// are bytes; source rows are top-down. No allocation/division in the pixel loop.
inline bool resample_efb_rgba(const void* src, uint32_t sw, uint32_t sh, size_t sp,
                              void* dst, uint32_t dw, uint32_t dh, size_t dp,
                              bool topDown = true) noexcept {
  if (!src || !dst || !sw || !sh || !dw || !dh || sw > 2048 || sh > 2048 ||
      dw > 2048 || dh > 2048 || sp < size_t(sw)*4 || dp < size_t(dw)*4) return false;
  std::array<uint32_t, 2048> xs;
  for (uint32_t x=0; x<dw; ++x) xs[x] = (x*sw/dw)*4;
  for (uint32_t y=0; y<dh; ++y) {
    const uint32_t row=y*sh/dh;
    const auto* s=static_cast<const uint8_t*>(src)+(topDown?row:sh-1-row)*sp;
    auto* d=static_cast<uint8_t*>(dst)+y*dp;
    if (sw==dw) { std::memcpy(d,s,size_t(dw)*4); continue; }
    for (uint32_t x=0; x<dw; ++x) std::memcpy(d+x*4,s+xs[x],4);
  }
  return true;
}
} // namespace aurora::vita::gfx
