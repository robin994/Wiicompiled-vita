#pragma once
#include "../gfx/vita_gfx_types.hpp"
#include "../gfx/vita_vertex_decode.hpp"
#include "../gfx/vita_vertex_pipeline.hpp"
#include "../gfx/vita_draw_adapter.hpp"
#include <cstdint>
#include <cstddef>

namespace aurora::gx { struct PipelineConfig; struct ShaderConfig; }

namespace aurora::vita::gxbridge {

struct TextureTranslation {
  gfx::TextureDesc texture{};
  gfx::SamplerDesc sampler{};
  bool valid = false;
  bool dynamicCopy = false; // GXCopyTex/EFB-backed texture; resolved by the EFB bridge.
};

struct Capabilities {
  bool hasFog = false;
  bool hasLighting = false;
  bool hasIndirect = false;
  bool hasIndirectOrigLod = false;
  bool hasGeneratedTexcoords = false;
  bool hasEmbossTexgen = false;
  bool hasLineExpansion = false;
  bool exactTev = true;
  bool exactFixedState = true;
};

#if defined(AURORA_VITA_UPSTREAM)
gfx::PipelineDesc translate_pipeline(const aurora::gx::PipelineConfig& config) noexcept;
gfx::VertexDecodeLayout translate_vertex_layout(const aurora::gx::ShaderConfig& config) noexcept;
// Dawn-free path: derive the same shader/pipeline inputs directly from Aurora GX state.
gfx::PipelineDesc translate_current_pipeline(uint8_t primitive, uint8_t fmt) noexcept;
gfx::VertexDecodeLayout translate_current_vertex_layout(uint8_t fmt) noexcept;
gfx::SourcePrimitive translate_source_primitive(uint8_t primitive) noexcept;
uint8_t translate_line_mode(uint8_t primitive) noexcept;
void translate_vertex_state(gfx::VertexTransformState& state, gfx::DrawUniforms& uniforms) noexcept;
TextureTranslation translate_texture(unsigned slot) noexcept;
gfx::PrimitiveExpansionState translate_primitive_expansion(uint8_t lineMode) noexcept;
gfx::Viewport translate_viewport() noexcept;
gfx::Scissor translate_scissor() noexcept;
Capabilities inspect(const aurora::gx::ShaderConfig& config) noexcept;
Capabilities inspect_current(uint8_t primitive, uint8_t fmt) noexcept;
#endif
} // namespace aurora::vita::gxbridge
