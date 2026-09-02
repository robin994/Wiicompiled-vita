#pragma once
#include "platforms/vita/integration/wiicompiled_aurora_adapter.hpp"
#include <cstddef>
#include <cstdint>

namespace wiicompiled::vita::aurora_bridge {

// Runtime-owned singleton wrapper. WiiCompiled supplies the callbacks; Aurora never directly
// depends on the static-recomp guest memory implementation.
bool initialize(const aurora::vita::integration::WiiCompiledHooks& hooks,
                bool strict = false, uint32_t summaryPeriodFrames = 60) noexcept;
bool initialize_config(const aurora::vita::integration::WiiCompiledHooks& hooks,
                       const aurora::vita::integration::WiiCompiledAdapterConfig& config) noexcept;
void shutdown() noexcept;
void begin_frame(uint64_t frame) noexcept;
void end_frame(uint64_t frame, uint64_t frameUs) noexcept;
bool submit_fifo_guest(uint32_t guestAddress, size_t bytes) noexcept;
bool submit_fifo_host(const void* data, size_t bytes) noexcept;
size_t drain_fifo(size_t maxPackets = static_cast<size_t>(-1)) noexcept;
void invalidate_texture(uint32_t guestAddress, size_t bytes) noexcept;
bool capture_guest_snapshot(uint32_t guestAddress, size_t bytes) noexcept;
bool capture_marker(const char* text) noexcept;
aurora::vita::integration::WiiCompiledAuroraAdapter& adapter() noexcept;

} // namespace wiicompiled::vita::aurora_bridge
