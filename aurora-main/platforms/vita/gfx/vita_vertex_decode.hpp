#pragma once
#include "vita_gfx_types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aurora::vita::gfx {

enum class VertexSemantic : uint8_t {
  PnMatrixIndex,
  TexMatrixIndex0, TexMatrixIndex1, TexMatrixIndex2, TexMatrixIndex3,
  TexMatrixIndex4, TexMatrixIndex5, TexMatrixIndex6, TexMatrixIndex7,
  Position, Normal, Binormal, Tangent, Color0, Color1,
  Tex0, Tex1, Tex2, Tex3, Tex4, Tex5, Tex6, Tex7
};
enum class VertexSource : uint8_t { None, Direct, Index8, Index16 };
enum class VertexComponent : uint8_t { U8, S8, U16, S16, F32, RGB565, RGB8, RGBX8, RGBA4, RGBA6, RGBA8 };

struct VertexArrayView {
  const uint8_t* data = nullptr;
  size_t size = 0;
  uint16_t stride = 0;
  bool littleEndian = false;
};

struct VertexDecodeAttribute {
  VertexSemantic semantic = VertexSemantic::Position;
  VertexSource source = VertexSource::None;
  VertexComponent component = VertexComponent::F32;
  uint8_t components = 0;
  uint8_t frac = 0;
  uint16_t streamOffset = 0;
  uint16_t valueOffset = 0; // offset after resolving direct/indexed source; used for NBT slices
  VertexArrayView array{};
};

struct VertexDecodeLayout {
  std::array<VertexDecodeAttribute, 24> attributes{};
  uint8_t count = 0;
  uint16_t streamStride = 0;
  bool streamLittleEndian = false;
};

struct CanonicalVertex {
  float position[4]{0.f,0.f,0.f,1.f};
  float normal[3]{0.f,0.f,1.f};
  float binormal[3]{0.f,1.f,0.f};
  float tangent[3]{1.f,0.f,0.f};
  uint8_t color0[4]{255,255,255,255};
  uint8_t color1[4]{255,255,255,255};
  float texcoord[8][3]{};
  uint8_t pnMatrixIndex = 0xff; // decoded GX index / 3; 0xff => current matrix
  uint8_t texMatrixIndex[8]{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}; // raw GX matrix index
};

struct VertexDecodeResult {
  bool ok = false;
  size_t badVertex = 0;
  std::vector<CanonicalVertex> vertices;
};

VertexLayout canonical_vertex_layout() noexcept;
VertexDecodeResult decode_vertices(const uint8_t* stream, size_t streamSize, uint32_t vertexCount,
                                   const VertexDecodeLayout& layout) noexcept;

} // namespace aurora::vita::gfx
