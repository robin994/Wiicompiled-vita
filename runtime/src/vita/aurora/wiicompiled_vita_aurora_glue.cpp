#include "wiicompiled_vita_aurora_glue.hpp"

namespace wiicompiled::vita::aurora_bridge {
namespace { aurora::vita::integration::WiiCompiledAuroraAdapter g_adapter; }

bool initialize(const aurora::vita::integration::WiiCompiledHooks& hooks, bool strict,
                uint32_t summaryPeriodFrames) noexcept {
  aurora::vita::integration::WiiCompiledAdapterConfig cfg{};
  cfg.strict = strict;
  cfg.summaryPeriodFrames = summaryPeriodFrames;
  return g_adapter.initialize(hooks, cfg);
}
bool initialize_config(const aurora::vita::integration::WiiCompiledHooks& hooks,
                       const aurora::vita::integration::WiiCompiledAdapterConfig& config) noexcept {
  return g_adapter.initialize(hooks, config);
}
void shutdown() noexcept { g_adapter.shutdown(); }
void begin_frame(uint64_t frame) noexcept { g_adapter.begin_frame(frame); }
void end_frame(uint64_t frame,uint64_t frameUs) noexcept { g_adapter.end_frame(frame,frameUs); }
bool submit_fifo_guest(uint32_t guestAddress,size_t bytes) noexcept { return g_adapter.submit_fifo_guest(guestAddress,bytes); }
bool submit_fifo_host(const void* data,size_t bytes) noexcept { return g_adapter.submit_fifo_host(data,bytes); }
size_t drain_fifo(size_t maxPackets) noexcept { return g_adapter.drain_fifo(maxPackets); }
void invalidate_texture(uint32_t guestAddress,size_t bytes) noexcept { g_adapter.invalidate_texture_guest(guestAddress,bytes); }
bool capture_guest_snapshot(uint32_t guestAddress,size_t bytes) noexcept { return g_adapter.capture_guest_snapshot(guestAddress,bytes); }
bool capture_marker(const char* text) noexcept { return g_adapter.capture_marker(text); }
aurora::vita::integration::WiiCompiledAuroraAdapter& adapter() noexcept { return g_adapter; }
} // namespace wiicompiled::vita::aurora_bridge
