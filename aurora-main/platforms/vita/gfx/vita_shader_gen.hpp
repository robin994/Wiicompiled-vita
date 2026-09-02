#pragma once
#include "vita_gfx_types.hpp"
#include <string>

namespace aurora::vita::gfx {
struct ShaderSources { std::string vertex; std::string fragment; uint64_t key=0; };
ShaderSources build_tev_glsl(const PipelineDesc& desc) noexcept;
} // namespace aurora::vita::gfx
