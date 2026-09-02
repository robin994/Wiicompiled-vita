#pragma once
#include <cstddef>
#include <cstdint>
#include "gfx/vita_telemetry.hpp"
#include "integration/vita_feature_coverage.hpp"
#include "integration/vita_frame_trace.hpp"
#include "gfx/vita_memory_budget.hpp"
namespace aurora::vita::gfx { class Renderer; }
namespace aurora::vita::gxbridge { class DrawSink; }
namespace aurora::vita {
enum class InitFailure : uint8_t {
  None = 0,
  ShaderCompilerMissing,
  ShaderCompilerLoadFailed,
  VitaGlInitFailed,
  RendererInitFailed,
  DrawSinkInitFailed,
};
struct BackendConfig {
  uint32_t width=960,height=544;
  // Aurora never uses vitaGL immediate mode, so reserving a large legacy pool
  // only steals memory from textures, EFBs and the Wii guest runtime.
  uint32_t vgl_legacy_pool_size=0;
  uint32_t vgl_ram_threshold=16*1024*1024;
  size_t texture_cache_budget=24*1024*1024;
  bool wait_vblank=true;
  size_t stream_vertex_bytes=4*1024*1024;
  size_t stream_index_bytes=1024*1024;
  uint32_t stream_slots=3;
  bool diagnostics=true;
  bool strict_unsupported=false;
  uint32_t diagnostics_period_frames=300;
  const char* telemetry_log_path=nullptr;
  const char* coverage_log_path=nullptr;
  const char* trace_log_path=nullptr;
  size_t trace_capacity=4096;
};
bool initialize(const BackendConfig& config={}) noexcept;
InitFailure last_init_failure() noexcept;
const char* last_init_failure_detail() noexcept;
bool begin_frame() noexcept;void end_frame() noexcept;void shutdown() noexcept;
uint64_t frame_index() noexcept;uint64_t last_frame_time_us() noexcept;uint32_t width() noexcept;uint32_t height() noexcept;
gfx::Renderer& renderer() noexcept;
gxbridge::DrawSink& draw_sink() noexcept;
gfx::Telemetry& telemetry() noexcept;
integration::FeatureCoverage& feature_coverage() noexcept;
integration::FrameTrace& frame_trace() noexcept;
gfx::MemoryBudgetSnapshot memory_budget() noexcept;
size_t invalidate_texture_source_range(uint64_t start,size_t bytes) noexcept;
} // namespace aurora::vita
