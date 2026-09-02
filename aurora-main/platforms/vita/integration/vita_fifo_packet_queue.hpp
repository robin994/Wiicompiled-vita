#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aurora::vita::integration {

class FifoPacketQueue {
public:
  bool initialize(size_t slots, size_t maxPacketBytes) noexcept;
  void shutdown() noexcept;
  void clear() noexcept;
  bool push(const void* data, size_t bytes) noexcept;
  bool peek(const uint8_t*& data, size_t& bytes) const noexcept;
  bool pop() noexcept;
  size_t size() const noexcept { return count_; }
  size_t capacity() const noexcept { return slots_.size(); }
  size_t max_packet_bytes() const noexcept { return maxPacketBytes_; }
  uint64_t pushes() const noexcept { return pushes_; }
  uint64_t pops() const noexcept { return pops_; }
  uint64_t overflows() const noexcept { return overflows_; }
  uint64_t oversize_rejects() const noexcept { return oversizeRejects_; }
  size_t high_water() const noexcept { return highWater_; }

private:
  struct Slot {
    std::vector<uint8_t> data;
    size_t bytes = 0;
  };
  std::vector<Slot> slots_{};
  size_t maxPacketBytes_ = 0;
  size_t read_ = 0;
  size_t write_ = 0;
  size_t count_ = 0;
  size_t highWater_ = 0;
  uint64_t pushes_ = 0;
  uint64_t pops_ = 0;
  uint64_t overflows_ = 0;
  uint64_t oversizeRejects_ = 0;
};

} // namespace aurora::vita::integration
