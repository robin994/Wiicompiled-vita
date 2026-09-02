#pragma once
#include "vita_gfx_types.hpp"
#include "vita_vertex_decode.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace aurora::vita::gfx {

// Row-vector compatible 4->3 transform, matching Aurora's vec4 * mat3x4 usage.
struct Matrix3x4 {
  std::array<float,12> v{{1,0,0,0, 0,1,0,0, 0,0,1,0}};
};

struct VertexTransformState {
  // Aurora's postex matrix space: [0..9] position matrices, [10..19] texture matrices.
  std::array<Matrix3x4,20> postexMatrices{};
  std::array<Matrix3x4,10> normalMatrices{};
  std::array<Matrix3x4,20> postMatrices{};
  // OpenGL column-major projection consumed by the simple Vita vertex shader.
  std::array<float,16> projection{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
  std::array<std::array<float,4>,4> channelAmbient{};
  std::array<std::array<float,4>,4> channelMaterial{};
  std::array<std::array<uint8_t,MaxLights>,4> lightEnabled{};
  std::array<LightUniform,MaxLights> lights{};
  uint8_t currentPnMatrix = 0;
};

// Correctness-first CPU implementation of the Aurora GX vertex shader semantics.
// It resolves per-vertex position matrices, vertex lighting and texgen into the canonical Vita stream.
bool run_vertex_pipeline(std::vector<CanonicalVertex>& vertices, const PipelineDesc& pipeline,
                         const VertexTransformState& state, DrawUniforms* uniforms=nullptr) noexcept;

} // namespace aurora::vita::gfx
