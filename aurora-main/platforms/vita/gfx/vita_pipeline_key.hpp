#pragma once
#include "vita_gfx_types.hpp"
#include <cstdint>
namespace aurora::vita::gfx {
uint64_t pipeline_key(const PipelineDesc& desc) noexcept;
uint64_t texture_key(const TextureDesc& desc) noexcept;
} // namespace aurora::vita::gfx
