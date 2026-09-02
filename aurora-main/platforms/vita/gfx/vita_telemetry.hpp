#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace aurora::vita::gfx {

enum class TelemetryPhase : uint8_t {
  VertexDecode,
  VertexTransform,
  TextureResolve,
  PipelineResolve,
  CommandBuild,
  Submit,
  EfbCopy,
  Present,
  Count,
};

struct TelemetryCounters {
  uint64_t frames = 0;
  uint64_t draws = 0;
  uint64_t vertices = 0;
  uint64_t indices = 0;
  uint64_t triangles = 0;
  uint64_t pipelineHits = 0;
  uint64_t pipelineMisses = 0;
  uint64_t textureHits = 0;
  uint64_t textureMisses = 0;
  uint64_t textureUploads = 0;
  uint64_t textureUploadBytes = 0;
  uint64_t fallbackTextures = 0;
  uint64_t efbCopies = 0;
  uint64_t arenaOverflows = 0;
  uint64_t unsupportedFeatures = 0;
};

struct FrameTelemetry {
  uint64_t frame = 0;
  uint64_t totalUs = 0;
  std::array<uint64_t, static_cast<size_t>(TelemetryPhase::Count)> phaseUs{};
  TelemetryCounters counters{};
};

class Telemetry {
public:
  void reset() noexcept;
  void begin_frame(uint64_t frame) noexcept;
  void end_frame(uint64_t totalUs) noexcept;
  void add_time(TelemetryPhase phase, uint64_t us) noexcept;
  void add_draw(uint32_t vertices, uint32_t indices, uint32_t triangles) noexcept;
  void pipeline(bool hit) noexcept;
  void texture(bool hit, bool uploaded, uint64_t uploadBytes = 0) noexcept;
  void fallback_texture(uint32_t count = 1) noexcept;
  void efb_copy() noexcept;
  void arena_overflow() noexcept;
  void unsupported() noexcept;

  const FrameTelemetry& frame() const noexcept { return frame_; }
  const TelemetryCounters& lifetime() const noexcept { return lifetime_; }
  std::string format_frame() const;
  std::string format_lifetime() const;
  bool append_frame_log(const char* path) const noexcept;

private:
  FrameTelemetry frame_{};
  TelemetryCounters lifetime_{};
};

class ScopedTelemetryPhase {
public:
  ScopedTelemetryPhase(Telemetry* telemetry, TelemetryPhase phase) noexcept;
  ~ScopedTelemetryPhase();
  ScopedTelemetryPhase(const ScopedTelemetryPhase&) = delete;
  ScopedTelemetryPhase& operator=(const ScopedTelemetryPhase&) = delete;
private:
  Telemetry* telemetry_ = nullptr;
  TelemetryPhase phase_ = TelemetryPhase::Submit;
  uint64_t startUs_ = 0;
};

uint64_t telemetry_now_us() noexcept;
const char* telemetry_phase_name(TelemetryPhase phase) noexcept;

} // namespace aurora::vita::gfx
