#pragma once
#include "vita_gfx_types.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aurora::vita::gfx {
struct DecodeResult {
  std::vector<uint8_t> rgba;
  uint32_t width = 0;
  uint32_t height = 0;
  bool ok = false;
};
size_t encoded_texture_size(uint32_t width, uint32_t height, TextureFormat format) noexcept;
size_t encoded_mip_chain_size(uint32_t width, uint32_t height, TextureFormat format, uint8_t mipCount) noexcept;
DecodeResult decode_texture_rgba8(const TextureDesc& desc) noexcept;
} // namespace aurora::vita::gfx
