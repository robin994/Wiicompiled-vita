#include "wiicompiled_aurora_adapter.hpp"
#include <string>

namespace aurora::vita::integration {

bool WiiCompiledAuroraAdapter::initialize(const WiiCompiledHooks& hooks, const WiiCompiledAdapterConfig& config) noexcept {
  if (!hooks.publish_gx_fifo) return false;
  hooks_ = hooks;
  config_ = config;
  telemetry_.reset();
  coverage_.reset();
  strictFailed_ = false;
  if (config_.deferFifo && !fifoQueue_.initialize(config_.fifoQueueSlots, config_.fifoMaxPacketBytes)) return false;
  if (config_.capturePath && *config_.capturePath && !capture_.open(config_.capturePath, config_.captureUpstreamCommit)) {
    fifoQueue_.shutdown();
    return false;
  }
  initialized_ = true;
  return true;
}

void WiiCompiledAuroraAdapter::shutdown() noexcept {
  capture_.flush();
  capture_.close();
  fifoQueue_.shutdown();
  initialized_ = false;
  hooks_ = {};
  strictFailed_ = false;
}

void WiiCompiledAuroraAdapter::begin_frame(uint64_t frame) noexcept {
  if (!initialized_) return;
  currentFrame_ = frame;
  telemetry_.begin_frame(frame);
  if (capture_.is_open()) capture_.frame_begin(frame);
  if (hooks_.begin_aurora_frame) hooks_.begin_aurora_frame(hooks_.user, frame);
}

void WiiCompiledAuroraAdapter::end_frame(uint64_t frame, uint64_t frameUs) noexcept {
  if (!initialized_) return;
  telemetry_.end_frame(frameUs);
  if (capture_.is_open()) { capture_.frame_end(frame, frameUs); capture_.flush(); }
  if (hooks_.end_aurora_frame) hooks_.end_aurora_frame(hooks_.user, frame);
  if (config_.logEveryFrame || (config_.summaryPeriodFrames && (frame % config_.summaryPeriodFrames) == 0)) {
    log(telemetry_.format_frame());
  }
}

bool WiiCompiledAuroraAdapter::submit_fifo_guest(uint32_t guestAddress, size_t bytes) noexcept {
  if (!initialized_ || !hooks_.map_guest_read || bytes == 0) return false;
  const auto span = hooks_.map_guest_read(hooks_.user, guestAddress, bytes);
  if (!span || span.size < bytes) {
    record_unsupported((static_cast<uint64_t>(guestAddress) << 32) ^ bytes, "guest FIFO mapping failed");
    return false;
  }
  return submit_fifo_host(span.data, bytes);
}

bool WiiCompiledAuroraAdapter::submit_fifo_host(const void* data, size_t bytes) noexcept {
  if (!initialized_ || !data || bytes == 0) return false;
  if (capture_.is_open() && !capture_.fifo(currentFrame_, data, bytes)) record_fallback(bytes, "GX capture FIFO write failed");
  if (config_.deferFifo) {
    if (!fifoQueue_.push(data, bytes)) {
      record_unsupported(bytes, bytes > fifoQueue_.max_packet_bytes() ? "deferred FIFO packet too large" : "deferred FIFO queue full");
      return false;
    }
    return true;
  }
  const bool ok = hooks_.publish_gx_fifo(hooks_.user, static_cast<const uint8_t*>(data), bytes);
  if (!ok) record_unsupported(bytes, "Aurora FIFO publish failed");
  return ok;
}

size_t WiiCompiledAuroraAdapter::drain_fifo(size_t maxPackets) noexcept {
  if (!initialized_ || !config_.deferFifo) return 0;
  size_t drained = 0;
  while (drained < maxPackets) {
    const uint8_t* data = nullptr; size_t bytes = 0;
    if (!fifoQueue_.peek(data, bytes)) break;
    if (!hooks_.publish_gx_fifo(hooks_.user, data, bytes)) {
      record_unsupported(bytes, "Aurora FIFO publish failed while draining");
      break;
    }
    fifoQueue_.pop();
    ++drained;
  }
  return drained;
}

void WiiCompiledAuroraAdapter::invalidate_texture_guest(uint32_t guestAddress, size_t bytes) noexcept {
  if (!initialized_ || bytes == 0) return;
  if (capture_.is_open()) capture_.invalidate(currentFrame_, guestAddress, bytes);
  if (hooks_.invalidate_texture_range) hooks_.invalidate_texture_range(hooks_.user, guestAddress, bytes);
}

bool WiiCompiledAuroraAdapter::capture_guest_snapshot(uint32_t guestAddress, size_t bytes) noexcept {
  if (!initialized_ || !capture_.is_open() || !hooks_.map_guest_read || bytes == 0) return false;
  const auto span = hooks_.map_guest_read(hooks_.user, guestAddress, bytes);
  if (!span || span.size < bytes) return false;
  return capture_.guest_snapshot(currentFrame_, guestAddress, span.data, bytes);
}

bool WiiCompiledAuroraAdapter::capture_marker(const char* text) noexcept {
  return initialized_ && capture_.is_open() && capture_.marker(currentFrame_, text);
}

void WiiCompiledAuroraAdapter::record_fallback(uint64_t key, const char* description) noexcept {
  coverage_.fallback(key, description ? description : "");
  telemetry_.unsupported();
  if (description) log(std::string("[AURORA-VITA][FALLBACK] ") + description);
}

void WiiCompiledAuroraAdapter::record_unsupported(uint64_t key, const char* description) noexcept {
  coverage_.unsupported(key, description ? description : "");
  telemetry_.unsupported();
  if (config_.strict) strictFailed_ = true;
  if (description) log(std::string("[AURORA-VITA][UNSUPPORTED] ") + description);
}

void WiiCompiledAuroraAdapter::log(const std::string& text) noexcept {
  if (hooks_.log_line) hooks_.log_line(hooks_.user, text.c_str());
}

} // namespace aurora::vita::integration
