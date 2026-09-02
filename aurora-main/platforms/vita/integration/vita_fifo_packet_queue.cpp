#include "vita_fifo_packet_queue.hpp"
#include <cstring>

namespace aurora::vita::integration {

bool FifoPacketQueue::initialize(size_t slots, size_t maxPacketBytes) noexcept {
  shutdown();
  if (slots == 0 || maxPacketBytes == 0) return false;
  slots_.resize(slots);
  for (auto& slot : slots_) slot.data.resize(maxPacketBytes);
  maxPacketBytes_ = maxPacketBytes;
  return true;
}

void FifoPacketQueue::shutdown() noexcept {
  slots_.clear();
  slots_.shrink_to_fit();
  maxPacketBytes_ = 0;
  read_ = write_ = count_ = highWater_ = 0;
  pushes_ = pops_ = overflows_ = oversizeRejects_ = 0;
}

void FifoPacketQueue::clear() noexcept {
  for (auto& slot : slots_) slot.bytes = 0;
  read_ = write_ = count_ = highWater_ = 0;
}

bool FifoPacketQueue::push(const void* data, size_t bytes) noexcept {
  if (!data || bytes == 0 || slots_.empty()) return false;
  if (bytes > maxPacketBytes_) {
    ++oversizeRejects_;
    return false;
  }
  if (count_ == slots_.size()) {
    ++overflows_;
    return false;
  }
  auto& slot = slots_[write_];
  std::memcpy(slot.data.data(), data, bytes);
  slot.bytes = bytes;
  write_ = (write_ + 1) % slots_.size();
  ++count_;
  ++pushes_;
  if (count_ > highWater_) highWater_ = count_;
  return true;
}

bool FifoPacketQueue::peek(const uint8_t*& data, size_t& bytes) const noexcept {
  if (count_ == 0 || slots_.empty()) {
    data = nullptr;
    bytes = 0;
    return false;
  }
  const auto& slot = slots_[read_];
  data = slot.data.data();
  bytes = slot.bytes;
  return true;
}

bool FifoPacketQueue::pop() noexcept {
  if (count_ == 0 || slots_.empty()) return false;
  slots_[read_].bytes = 0;
  read_ = (read_ + 1) % slots_.size();
  --count_;
  ++pops_;
  return true;
}

} // namespace aurora::vita::integration
