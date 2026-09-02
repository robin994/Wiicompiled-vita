#pragma once
#include "../gfx/vita_telemetry.hpp"
#include "vita_feature_coverage.hpp"
#include "vita_fifo_packet_queue.hpp"
#include "vita_gx_capture.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace aurora::vita::integration {

struct GuestSpan {
  const uint8_t* data = nullptr;
  size_t size = 0;
  explicit operator bool() const noexcept { return data != nullptr && size != 0; }
};

// Host callbacks deliberately keep WiiCompiled-specific symbols out of Aurora.
// The static-recomp runtime supplies guest-address translation and Aurora FIFO publication.
struct WiiCompiledHooks {
  void* user = nullptr;
  GuestSpan (*map_guest_read)(void* user, uint32_t guestAddress, size_t bytes) noexcept = nullptr;
  bool (*publish_gx_fifo)(void* user, const uint8_t* data, size_t bytes) noexcept = nullptr;
  void (*begin_aurora_frame)(void* user, uint64_t frame) noexcept = nullptr;
  void (*end_aurora_frame)(void* user, uint64_t frame) noexcept = nullptr;
  void (*invalidate_texture_range)(void* user, uint32_t guestAddress, size_t bytes) noexcept = nullptr;
  void (*log_line)(void* user, const char* text) noexcept = nullptr;
};

struct WiiCompiledAdapterConfig {
  bool strict = false;
  bool logEveryFrame = false;
  uint32_t summaryPeriodFrames = 60;
  // Deferred mode copies FIFO bytes into a bounded queue so the render worker never
  // depends on mutable/reused guest-memory lifetime.
  bool deferFifo = false;
  size_t fifoQueueSlots = 8;
  size_t fifoMaxPacketBytes = 256 * 1024;
  // Optional deterministic GX capture. The path must remain valid for initialize().
  const char* capturePath = nullptr;
  const char* captureUpstreamCommit = nullptr;
};

class WiiCompiledAuroraAdapter {
public:
  bool initialize(const WiiCompiledHooks& hooks, const WiiCompiledAdapterConfig& config = {}) noexcept;
  void shutdown() noexcept;
  void begin_frame(uint64_t frame) noexcept;
  void end_frame(uint64_t frame, uint64_t frameUs = 0) noexcept;
  bool submit_fifo_guest(uint32_t guestAddress, size_t bytes) noexcept;
  bool submit_fifo_host(const void* data, size_t bytes) noexcept;
  size_t drain_fifo(size_t maxPackets = static_cast<size_t>(-1)) noexcept;
  void invalidate_texture_guest(uint32_t guestAddress, size_t bytes) noexcept;
  bool capture_guest_snapshot(uint32_t guestAddress, size_t bytes) noexcept;
  bool capture_marker(const char* text) noexcept;
  void record_fallback(uint64_t key, const char* description) noexcept;
  void record_unsupported(uint64_t key, const char* description) noexcept;

  gfx::Telemetry& telemetry() noexcept { return telemetry_; }
  FeatureCoverage& coverage() noexcept { return coverage_; }
  const gfx::Telemetry& telemetry() const noexcept { return telemetry_; }
  const FeatureCoverage& coverage() const noexcept { return coverage_; }
  bool strict_failed() const noexcept { return strictFailed_; }
  const FifoPacketQueue& fifo_queue() const noexcept { return fifoQueue_; }
  bool capture_enabled() const noexcept { return capture_.is_open(); }

private:
  void log(const std::string& text) noexcept;
  WiiCompiledHooks hooks_{};
  WiiCompiledAdapterConfig config_{};
  gfx::Telemetry telemetry_{};
  FeatureCoverage coverage_{};
  FifoPacketQueue fifoQueue_{};
  GxCaptureWriter capture_{};
  uint64_t currentFrame_ = 0;
  bool strictFailed_ = false;
  bool initialized_ = false;
};

} // namespace aurora::vita::integration
