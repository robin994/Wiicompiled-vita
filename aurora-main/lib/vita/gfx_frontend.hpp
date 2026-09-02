#pragma once

// Vita bring-up seam for Aurora's guest-facing GX implementation. The Dolphin
// GX frontend mostly manipulates console state, but its shared headers also
// expose Dawn/WebGPU resource types. Keep those resource-shaped types local to
// this seam until the real Vita renderer replaces the no-op operations below.

#include <aurora/aurora.h>
#include <aurora/math.hpp>
#include <dolphin/gx.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace wgpu {
struct BindGroup {};
struct RenderPipeline {};
struct ShaderModule {};
struct VertexBufferLayout {};
struct SamplerDescriptor {};
struct Extent3D {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depthOrArrayLayers = 1;
};
struct Color {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  double a = 0.0;
};
} // namespace wgpu

using WGPUBindGroup = void*;

namespace aurora {
using HashType = uint32_t;

inline HashType xxh3_hash_s(const void* input, size_t len, HashType seed = 0) noexcept {
  // Cache identity only; this is not guest-visible state. FNV-1a keeps the Vita
  // bring-up independent of xxHash while preserving stable content hashing.
  const auto* bytes = static_cast<const uint8_t*>(input);
  uint32_t hash = 2166136261u ^ seed;
  for (size_t i = 0; i < len; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

template <typename T>
inline HashType xxh3_hash(const T& input, HashType seed = 0) noexcept {
  return xxh3_hash_s(&input, sizeof(T), seed);
}
} // namespace aurora

namespace aurora::webgpu {
struct Viewport {
  float left = 0.f;
  float top = 0.f;
  float width = 0.f;
  float height = 0.f;
  float znear = 0.f;
  float zfar = 1.f;

  bool operator==(const Viewport& rhs) const {
    return left == rhs.left && top == rhs.top && width == rhs.width && height == rhs.height && znear == rhs.znear &&
           zfar == rhs.zfar;
  }
  bool operator!=(const Viewport& rhs) const { return !(*this == rhs); }
};
} // namespace aurora::webgpu

namespace aurora::gfx {
using BindGroupRef = HashType;
using PipelineRef = HashType;
using SamplerRef = HashType;
using ShaderRef = HashType;

struct Range {
  uint32_t offset = 0;
  uint32_t size = 0;

  bool operator==(const Range& rhs) const { return offset == rhs.offset && size == rhs.size; }
  bool operator!=(const Range& rhs) const { return !(*this == rhs); }
};

struct ClipRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;

  bool operator==(const ClipRect& rhs) const {
    return x == rhs.x && y == rhs.y && width == rhs.width && height == rhs.height;
  }
  bool operator!=(const ClipRect& rhs) const { return !(*this == rhs); }
};

using Viewport = webgpu::Viewport;

inline constexpr u32 InvalidTextureFormat = static_cast<u32>(-1);

constexpr uint32_t max_texture_mip_count(uint32_t width, uint32_t height) noexcept {
  uint32_t dimension = std::max(width, height);
  uint32_t count = 1;
  while (dimension > 1) {
    dimension >>= 1;
    ++count;
  }
  return count;
}

struct TextureRef {
  wgpu::Extent3D size{};
  uint32_t mipCount = 1;
  u32 gxFormat = GX_TF_RGBA8;
};
using TextureHandle = std::shared_ptr<TextureRef>;
} // namespace aurora::gfx

struct GXTexObj_ {
  u32 mode0 = 0;
  u32 mode1 = 0;
  u32 image0 = UINT32_MAX;
  u32 image3 = 0;
  const void* userData = nullptr;
  const void* data = nullptr;
  u32 mWidth = 0;
  u32 mHeight = 0;
  u32 mFormat = aurora::gfx::InvalidTextureFormat;
  GXTlut tlut = GX_TLUT0;
  u32 texObjId = 0;
  u32 texDataVersion = 0;
  u8 flags = 0;

  static constexpr u32 get_bits(u32 reg, u32 size, u32 shift) noexcept { return (reg >> shift) & ((1u << size) - 1); }
  u32 width() const noexcept { return mWidth != 0 ? mWidth : ((get_bits(image0, 10, 0) + 1) & 0x3FF); }
  u32 height() const noexcept { return mHeight != 0 ? mHeight : ((get_bits(image0, 10, 10) + 1) & 0x3FF); }
  u32 raw_format() const noexcept { return get_bits(image0, 4, 20); }
  u32 format() const noexcept { return mFormat != aurora::gfx::InvalidTextureFormat ? mFormat : raw_format(); }
  GXTexWrapMode wrap_s() const noexcept { return static_cast<GXTexWrapMode>(get_bits(mode0, 2, 0)); }
  GXTexWrapMode wrap_t() const noexcept { return static_cast<GXTexWrapMode>(get_bits(mode0, 2, 2)); }
  GXTexFilter min_filter() const noexcept {
    constexpr GXTexFilter kHwToGxFilter[8] = {
        GX_NEAR, GX_NEAR_MIP_NEAR, GX_NEAR_MIP_LIN, GX_NEAR, GX_LINEAR, GX_LIN_MIP_NEAR, GX_LIN_MIP_LIN, GX_NEAR,
    };
    return kHwToGxFilter[get_bits(mode0, 3, 5)];
  }
  GXTexFilter mag_filter() const noexcept { return get_bits(mode0, 1, 4) != 0 ? GX_LINEAR : GX_NEAR; }
  GXBool has_mips() const noexcept { return (flags & 1u) != 0 ? GX_TRUE : GX_FALSE; }
  u32 mip_count() const noexcept {
    if (!has_mips()) return 1;
    const u32 requested = std::max<u32>(static_cast<u32>(max_lod()) + 1, 1u);
    return std::min(requested, aurora::gfx::max_texture_mip_count(width(), height()));
  }
  GXBool do_edge_lod() const noexcept { return get_bits(mode0, 1, 8) == 0 ? GX_TRUE : GX_FALSE; }
  float lod_bias() const noexcept { return static_cast<float>(static_cast<int8_t>(get_bits(mode0, 8, 9))) / 32.0f; }
  GXAnisotropy max_aniso() const noexcept { return static_cast<GXAnisotropy>(get_bits(mode0, 2, 19)); }
  GXBool bias_clamp() const noexcept { return get_bits(mode0, 1, 21) != 0 ? GX_TRUE : GX_FALSE; }
  float min_lod() const noexcept { return static_cast<float>(get_bits(mode1, 8, 0)) / 16.0f; }
  float max_lod() const noexcept { return static_cast<float>(get_bits(mode1, 8, 8)) / 16.0f; }
  bool no_cache() const noexcept { return (flags & 0x80) != 0; }
  void set_no_cache(bool value) noexcept { flags = value ? flags | 0x80 : flags & ~0x80; }
};
static_assert(sizeof(GXTexObj_) <= sizeof(GXTexObj), "GXTexObj too small!");

struct GXTlutObj_ {
  u32 tlut = 0;
  u32 loadTlut0 = 0;
  u16 numEntries = 0;
  const void* data = nullptr;
  GXTlutFmt format = GX_TL_IA8;
  u32 tlutObjId = 0;
  u32 tlutDataVersion = 0;
  u8 flags = 0;

  bool no_cache() const noexcept { return (flags & 0x80) != 0; }
  void set_no_cache(bool value) noexcept { flags = value ? flags | 0x80 : flags & ~0x80; }
};
static_assert(sizeof(GXTlutObj_) <= sizeof(GXTlutObj), "GXTlutObj too small!");

namespace aurora::gfx {
struct TextureBind {
  TextureHandle ref;
  GXTexObj_ texObj;

  TextureBind() noexcept = default;
  TextureBind(const GXTexObj_& obj, TextureHandle handle) noexcept : ref(std::move(handle)), texObj(obj) {}
  void reset() noexcept { ref.reset(); }
  [[nodiscard]] wgpu::SamplerDescriptor get_descriptor() const noexcept { return {}; }
  operator bool() const noexcept { return ref.operator bool(); }
};

inline Vec2<uint32_t> get_render_target_size() noexcept { return {960, 544}; }
inline Vec2<uint32_t> get_frame_buffer_size() noexcept { return {960, 544}; }
inline uint32_t current_frame() noexcept { return 0; }
inline uint32_t get_sample_count() noexcept { return 1; }
inline void begin_offscreen(uint32_t, uint32_t) {}
inline void end_offscreen() {}

inline TextureHandle new_render_texture(uint32_t width, uint32_t height, u32 gxFormat, const char*) noexcept {
  auto texture = std::make_shared<TextureRef>();
  texture->size = {width, height, 1};
  texture->gxFormat = gxFormat;
  return texture;
}
inline TextureHandle new_conv_texture(uint32_t width, uint32_t height, u32 gxFormat, const char* label) noexcept {
  return new_render_texture(width, height, gxFormat, label);
}
inline TextureHandle new_dynamic_texture_2d(uint32_t width, uint32_t height, uint32_t mips, u32 gxFormat,
                                            const char* label) noexcept {
  auto texture = new_render_texture(width, height, gxFormat, label);
  texture->mipCount = mips;
  return texture;
}

template <typename PipelineConfig>
inline PipelineRef pipeline_ref(const PipelineConfig& config) {
  return xxh3_hash(config);
}

template <typename DrawData>
inline void push_draw_command(DrawData) {}

inline void resolve_pass(TextureHandle, ClipRect, bool, bool, bool, Vec4<float>, float, GXTexFmt = GX_TF_RGBA8,
                         const Vec4<float>* = nullptr, bool = false, const std::array<u32, 3>* = nullptr,
                         bool = false, float = 1.0f, bool = false, bool = false, bool = false) {}

namespace clear {
struct DrawData {
  PipelineRef pipeline = 0;
  Range uniformRange{};
  wgpu::Color color{};
  float depth = 0.f;
  bool useScissor = false;
  ClipRect scissor{};
};
struct PipelineConfig {
  uint32_t version = 3;
  uint32_t msaaSamples = 1;
  bool clearColor = true;
  bool clearAlpha = true;
  bool clearDepth = true;
  uint8_t _pad = 0;
};
} // namespace clear

namespace tex_copy_conv {
inline bool needs_conversion(GXTexFmt) { return false; }
} // namespace tex_copy_conv

namespace efb_ram {
inline void schedule(void*, uint32_t, uint32_t, GXTexFmt, TextureHandle) noexcept {}
} // namespace efb_ram

namespace texture_replacement {
inline void register_tlut(const GXTlutObj*, const void*, GXTlutFmt, uint16_t) noexcept {}
inline void load_tlut(const GXTlutObj*, uint32_t) noexcept {}
} // namespace texture_replacement

namespace depth_peek {
inline void poll() noexcept {}
inline bool read_latest(uint16_t, uint16_t, uint32_t&) noexcept { return false; }
inline void request_snapshot() noexcept {}
} // namespace depth_peek
} // namespace aurora::gfx

namespace aurora::window {
inline void set_frame_buffer_aspect_fit(bool) {}
inline void set_present_surface_fill(bool) {}
inline AuroraWindowSize get_window_size() {
  AuroraWindowSize size{};
  size.width = 960;
  size.height = 544;
  size.fb_width = 960;
  size.fb_height = 544;
  size.native_fb_width = 960;
  size.native_fb_height = 544;
  size.scale = 1.f;
  return size;
}
} // namespace aurora::window
