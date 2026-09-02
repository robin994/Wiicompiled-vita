#pragma once
#include "vita_gx_capture.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aurora::vita::integration {

struct ReplaySpan {
  const uint8_t* data = nullptr;
  size_t size = 0;
  explicit operator bool() const noexcept { return data != nullptr && size != 0; }
};

class GuestSnapshotMirror {
public:
  void reset() noexcept { snapshots_.clear(); }
  void apply(uint32_t guestAddress, const void* data, size_t bytes, uint64_t sequence);
  ReplaySpan map(uint32_t guestAddress, size_t bytes) const noexcept;
  size_t snapshot_count() const noexcept { return snapshots_.size(); }
  size_t bytes_stored() const noexcept;
private:
  struct Snapshot {
    uint32_t address = 0;
    uint64_t sequence = 0;
    std::vector<uint8_t> data{};
  };
  std::vector<Snapshot> snapshots_{};
};

struct GxReplayCallbacks {
  void* user = nullptr;
  bool (*frame_begin)(void* user, uint64_t frame) noexcept = nullptr;
  bool (*frame_end)(void* user, uint64_t frame, uint64_t frameUs) noexcept = nullptr;
  bool (*fifo)(void* user, uint64_t frame, const uint8_t* data, size_t bytes, const GuestSnapshotMirror& mirror) noexcept = nullptr;
  bool (*invalidate)(void* user, uint64_t frame, uint32_t guestAddress, size_t bytes) noexcept = nullptr;
  bool (*marker)(void* user, uint64_t frame, const char* text) noexcept = nullptr;
};

struct GxReplayStats {
  uint64_t records = 0;
  uint64_t frames = 0;
  uint64_t fifoPackets = 0;
  uint64_t fifoBytes = 0;
  uint64_t snapshotsApplied = 0;
  uint64_t snapshotBytes = 0;
  uint64_t invalidations = 0;
  uint64_t markers = 0;
  uint64_t callbackFailures = 0;
};

class GxCaptureReplayer {
public:
  bool replay(const char* path, const GxReplayCallbacks& callbacks, GxReplayStats* stats = nullptr) noexcept;
  const GuestSnapshotMirror& mirror() const noexcept { return mirror_; }
  const std::string& error() const noexcept { return error_; }
private:
  GuestSnapshotMirror mirror_{};
  std::string error_{};
};

std::string gx_replay_format_stats(const GxReplayStats& stats);

} // namespace aurora::vita::integration
