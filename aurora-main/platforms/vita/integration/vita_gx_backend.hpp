#pragma once
#include "aurora_gx_backend_api.hpp"
#include "vita_feature_coverage.hpp"
#include "vita_frame_trace.hpp"
#include "../gfx/vita_memory_budget.hpp"
#include "../gfx/vita_renderer.hpp"
#include "../gfx/vita_telemetry.hpp"
#include "../gx/aurora_vita_draw_sink.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace aurora::vita::integration {

struct VitaGxBackendConfig {
  gfx::RendererConfig renderer{};
  gxbridge::DrawSinkConfig sink{};
  bool strictUnsupported = false;
  // Optional files written by the actual renderer backend on hardware.
  const char* telemetryLogPath = nullptr;
  const char* coverageReportPath = nullptr;
  const char* traceReportPath = nullptr;
  const char* memoryLogPath = nullptr;
  uint32_t diagnosticDumpEveryFrames = 60;
  size_t traceReportRecords = 512;
};

class VitaGxBackend final : public GxBackendApi {
public:
  bool initialize(const VitaGxBackendConfig& config = {}) noexcept;
  void shutdown() noexcept;
  bool begin_frame(uint64_t frame) noexcept override;
  GxBackendSubmitStatus submit_raw_draw(const GxRawDraw& draw) noexcept override;
  bool copy_tex(const void* destination, bool clear) noexcept override;
  void evict_copy_tex(const void* destination) noexcept override;
  void clear_copy_textures() noexcept override;
  void invalidate_texture_range(uint64_t sourceAddress, size_t bytes) noexcept override;
  bool flush() noexcept override;
  bool end_frame(uint64_t frame) noexcept override;
  bool strict_failed() const noexcept override;

  gfx::Renderer& renderer() noexcept { return *renderer_; }
  gxbridge::DrawSink& sink() noexcept { return sink_; }
  gfx::Telemetry& telemetry() noexcept { return telemetry_; }
  FeatureCoverage& coverage() noexcept { return coverage_; }
  FrameTrace& trace() noexcept { return trace_; }
  gfx::MemoryBudgetSnapshot memory_budget() const noexcept { return sink_.memory_budget(); }
  bool initialized() const noexcept { return initialized_; }
private:
  std::unique_ptr<gfx::Renderer> renderer_{};
  gxbridge::DrawSink sink_{};
  gfx::Telemetry telemetry_{};
  FeatureCoverage coverage_{};
  FrameTrace trace_{4096};
  uint64_t frame_ = 0;
  uint64_t frameStartUs_ = 0;
  bool strictUnsupported_ = false;
  const char* telemetryLogPath_ = nullptr;
  const char* coverageReportPath_ = nullptr;
  const char* traceReportPath_ = nullptr;
  const char* memoryLogPath_ = nullptr;
  uint32_t diagnosticDumpEveryFrames_ = 60;
  size_t traceReportRecords_ = 512;
  bool initialized_ = false;
};

} // namespace aurora::vita::integration
