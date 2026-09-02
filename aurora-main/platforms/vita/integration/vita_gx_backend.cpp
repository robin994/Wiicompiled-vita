#include "vita_gx_backend.hpp"
#include <cstdio>
#include <string>

namespace aurora::vita::integration {

bool VitaGxBackend::initialize(const VitaGxBackendConfig& config) noexcept {
  if (initialized_) return true;
  strictUnsupported_ = config.strictUnsupported;
  telemetryLogPath_ = config.telemetryLogPath;
  coverageReportPath_ = config.coverageReportPath;
  traceReportPath_ = config.traceReportPath;
  memoryLogPath_ = config.memoryLogPath;
  diagnosticDumpEveryFrames_ = config.diagnosticDumpEveryFrames;
  traceReportRecords_ = config.traceReportRecords;
  renderer_ = std::make_unique<gfx::Renderer>(config.renderer);
  if (!renderer_->initialize()) { renderer_.reset(); return false; }
  auto sinkCfg = config.sink;
  sinkCfg.telemetry = &telemetry_;
  sinkCfg.coverage = &coverage_;
  sinkCfg.trace = &trace_;
  sinkCfg.strictUnsupported = strictUnsupported_;
  if (!sink_.initialize(*renderer_, sinkCfg)) {
    renderer_->shutdown();
    renderer_.reset();
    return false;
  }
  telemetry_.reset(); coverage_.reset(); trace_.reset();
  initialized_ = true;
  return true;
}

void VitaGxBackend::shutdown() noexcept {
  if (!initialized_) return;
  sink_.shutdown();
  if (renderer_) renderer_->shutdown();
  renderer_.reset();
  telemetryLogPath_ = coverageReportPath_ = traceReportPath_ = memoryLogPath_ = nullptr;
  initialized_ = false;
}

bool VitaGxBackend::begin_frame(uint64_t frame) noexcept {
  if (!initialized_) return false;
  frame_ = frame;
  frameStartUs_ = gfx::telemetry_now_us();
  telemetry_.begin_frame(frame);
  renderer_->begin_frame();
  sink_.begin_frame(frame);
  return true;
}

GxBackendSubmitStatus VitaGxBackend::submit_raw_draw(const GxRawDraw& draw) noexcept {
  GxBackendSubmitStatus out{};
#if defined(AURORA_VITA_UPSTREAM)
  const auto result = sink_.submit(draw.primitive, draw.vertexFormat, draw.vertexData, draw.vertexBytes, draw.vertexCount,
                                   draw.indexData, draw.indexCount);
  out.ok = result.ok;
  out.error = static_cast<uint32_t>(result.drawError);
  out.warningMask = static_cast<uint32_t>(result.warnings);
#else
  (void)draw;
  out.error = 1;
#endif
  return out;
}

bool VitaGxBackend::copy_tex(const void* destination, bool clear) noexcept {
#if defined(AURORA_VITA_UPSTREAM)
  return initialized_ && sink_.copy_tex(destination, clear);
#else
  (void)destination; (void)clear; return false;
#endif
}

void VitaGxBackend::evict_copy_tex(const void* destination) noexcept {
#if defined(AURORA_VITA_UPSTREAM)
  if (initialized_) sink_.evict_copy_tex(destination);
#else
  (void)destination;
#endif
}

void VitaGxBackend::clear_copy_textures() noexcept {
#if defined(AURORA_VITA_UPSTREAM)
  if (initialized_) sink_.clear_copy_textures();
#endif
}

void VitaGxBackend::invalidate_texture_range(uint64_t sourceAddress, size_t bytes) noexcept {
  if (!initialized_ || bytes == 0) return;
  renderer_->invalidate_texture_source_range(sourceAddress, bytes);
}

bool VitaGxBackend::flush() noexcept {
  if (!initialized_) return false;
  sink_.flush();
  return true;
}

bool VitaGxBackend::end_frame(uint64_t frame) noexcept {
  if (!initialized_ || frame != frame_) return false;
  sink_.flush();
  renderer_->end_frame();
  const uint64_t elapsed = gfx::telemetry_now_us() - frameStartUs_;
  telemetry_.end_frame(elapsed);
  if (telemetryLogPath_) telemetry_.append_frame_log(telemetryLogPath_);
  const bool dump = diagnosticDumpEveryFrames_ != 0 && (frame % diagnosticDumpEveryFrames_) == 0;
  if (dump) {
    if (coverageReportPath_) coverage_.write_report(coverageReportPath_);
    if (traceReportPath_) trace_.write_report(traceReportPath_, traceReportRecords_);
    if (memoryLogPath_) {
      if (FILE* fp = std::fopen(memoryLogPath_, "ab")) {
        const std::string line = memory_budget().format();
        std::fwrite(line.data(), 1, line.size(), fp); std::fwrite("\n", 1, 1, fp); std::fclose(fp);
      }
    }
  }
  return !strict_failed();
}

bool VitaGxBackend::strict_failed() const noexcept {
  return !initialized_ || (strictUnsupported_ && sink_.strict_failed());
}

} // namespace aurora::vita::integration
