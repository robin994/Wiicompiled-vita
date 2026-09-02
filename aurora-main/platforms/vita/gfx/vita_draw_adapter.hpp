#pragma once
#include "vita_command_stream.hpp"
#include "vita_renderer.hpp"
#include "vita_streaming_arena.hpp"
#include "vita_telemetry.hpp"
#include "vita_vertex_decode.hpp"
#include "vita_vertex_pipeline.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aurora::vita::gfx {

enum class SourcePrimitive : uint8_t { Quads, Triangles, TriangleStrip, TriangleFan, Lines, LineStrip, Points };

enum class PrepareDrawError : uint8_t { None, InvalidInput, VertexDecodeFailed, VertexTransformFailed, TooManyVertices, UnsupportedLineExpansion, StreamingOverflow, PipelineFailed };

// Runtime state used by Aurora's WGSL line/point expansion, expressed in render-target pixels.
struct PrimitiveExpansionState {
  float viewportWidth = 960.f;
  float viewportHeight = 544.f;
  float lineWidthPixels = 1.f;
  float pointSizePixels = 1.f;
  float lineTexOffset = 0.f;
  float pointTexOffset = 0.f;
  uint8_t lineTexcoordMask = 0;
  uint8_t pointTexcoordMask = 0;
};

struct PreparedDraw {
  PrepareDrawError error = PrepareDrawError::None;
  std::vector<CanonicalVertex> vertices{};
  std::vector<uint16_t> indices{};
  Primitive primitive = Primitive::Triangles;
  bool positionIsClipSpace = false;
  bool ok() const noexcept { return error == PrepareDrawError::None; }
};

PreparedDraw prepare_draw(const uint8_t* rawVertices,size_t rawBytes,uint32_t vertexCount,SourcePrimitive source,
                          const VertexDecodeLayout& layout,const PipelineDesc& pipeline,
                          const VertexTransformState& state,DrawUniforms* uniforms=nullptr,
                          const PrimitiveExpansionState& expansion={},Telemetry* telemetry=nullptr) noexcept;

// Resolves the effective post-conversion pipeline once. Callers submitting a
// consecutive run with unchanged GX state may reuse the returned key.
uint64_t resolve_draw_pipeline(Renderer& renderer,const PreparedDraw& prepared,
                               const PipelineDesc& pipeline,Telemetry* telemetry=nullptr) noexcept;

// Uploads a prepared draw into the multi-buffered stream arena and appends one DrawPacket.
bool enqueue_draw(Renderer& renderer,StreamingArena& arena,CommandStream& stream,const PreparedDraw& prepared,
                  const PipelineDesc& pipeline,const DrawUniforms& uniforms,const Viewport& viewport,
                  const Scissor& scissor,const std::array<TextureBinding,MaxTextures>& textures={},
                  PrepareDrawError* error=nullptr,Telemetry* telemetry=nullptr,
                  uint64_t resolvedPipelineKey=0) noexcept;

} // namespace aurora::vita::gfx
