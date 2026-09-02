#pragma once

#include "aurora_gx_bridge.hpp"
#include "../gfx/vita_command_stream.hpp"
#include "../gfx/vita_streaming_arena.hpp"
#include "../gfx/vita_telemetry.hpp"
#include "../gfx/vita_memory_budget.hpp"
#include "../integration/vita_feature_coverage.hpp"
#include "../integration/vita_frame_trace.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace aurora::vita::gxbridge {

enum class SubmitWarning : uint8_t {
  None = 0,
  MissingTextureFallback = 1 << 0,
  DynamicCopyFallback = 1 << 1,
  LogicOpFallback = 1 << 2,
  OrigLodApproximation = 1 << 3,
  ZCompLocApproximation = 1 << 4,
};
inline SubmitWarning operator|(SubmitWarning a, SubmitWarning b) noexcept {
  return static_cast<SubmitWarning>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline SubmitWarning& operator|=(SubmitWarning& a, SubmitWarning b) noexcept { a = a | b; return a; }
inline bool has_warning(SubmitWarning set, SubmitWarning bit) noexcept {
  return (static_cast<uint8_t>(set) & static_cast<uint8_t>(bit)) != 0;
}

struct SubmitResult {
  bool ok = false;
  gfx::PrepareDrawError drawError = gfx::PrepareDrawError::None;
  SubmitWarning warnings = SubmitWarning::None;
  uint8_t fallbackTextureMask = 0;
};

struct DrawSinkConfig {
  gfx::StreamingArenaConfig streaming{};
  size_t commandReserve = 2048;
  gfx::Telemetry* telemetry = nullptr;
  integration::FeatureCoverage* coverage = nullptr;
  integration::FrameTrace* trace = nullptr;
  bool strictUnsupported = false;
};

class DrawSink {
public:
  DrawSink() = default;
  ~DrawSink();
  DrawSink(const DrawSink&) = delete;
  DrawSink& operator=(const DrawSink&) = delete;

  bool initialize(gfx::Renderer& renderer, const DrawSinkConfig& config = {}) noexcept;
  void shutdown() noexcept;
  void begin_frame(uint64_t frame) noexcept;
  void flush() noexcept;
  void reset_commands() noexcept { stream_.reset(); reset_pipeline_run_cache(); }

#if defined(AURORA_VITA_UPSTREAM)
  SubmitResult submit(uint8_t primitive, uint8_t fmt, const uint8_t* rawVertices,
                      size_t rawBytes, uint32_t vertexCount,
                      const uint16_t* rawIndices = nullptr, uint32_t indexCount = 0) noexcept;
  // GXCopyTex integration. Call after the source EFB has been rendered and before
  // a texture object backed by dest is sampled. The copy stays on the GPU.
  bool copy_tex(const void* dest, bool clear) noexcept;
  void evict_copy_tex(const void* dest) noexcept;
  void clear_copy_textures() noexcept;
#endif

  gfx::CommandStream& stream() noexcept { return stream_; }
  const gfx::CommandStream& stream() const noexcept { return stream_; }
  uint64_t submitted_draws() const noexcept { return submittedDraws_; }
  bool strict_failed() const noexcept { return strictFailed_; }
  gfx::MemoryBudgetSnapshot memory_budget() const noexcept;

private:
  gfx::Handle white_texture() noexcept;
  void reset_pipeline_run_cache() noexcept {
#if defined(AURORA_VITA_UPSTREAM)
    queuedPipelineValid_ = false;
#endif
  }
  gfx::Renderer* renderer_ = nullptr;
  std::unique_ptr<gfx::StreamingArena> arena_{};
  gfx::CommandStream stream_{};
  gfx::Handle whiteTexture_ = gfx::InvalidHandle;
#if defined(AURORA_VITA_UPSTREAM)
  uint64_t queuedTranslatedPipelineKey_ = 0;
  uint64_t queuedResolvedPipelineKey_ = 0;
  gfx::Primitive queuedPrimitive_ = gfx::Primitive::Triangles;
  bool queuedPositionIsClipSpace_ = false;
  bool queuedPipelineValid_ = false;
#endif
  uint64_t submittedDraws_ = 0;
  gfx::Telemetry* telemetry_ = nullptr;
  integration::FeatureCoverage* coverage_ = nullptr;
  integration::FrameTrace* trace_ = nullptr;
  bool strictUnsupported_ = false;
  bool strictFailed_ = false;
  struct CopyTextureEntry { gfx::Handle handle=gfx::InvalidHandle; uint32_t width=0,height=0; uint32_t revision=0; };
  std::unordered_map<uintptr_t,CopyTextureEntry> copyTextures_{};
  bool initialized_ = false;
};

} // namespace aurora::vita::gxbridge
