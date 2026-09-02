#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace aurora::vita::integration {

enum class GxCaptureRecordType : uint32_t {
  FrameBegin = 1,
  FrameEnd = 2,
  Fifo = 3,
  Invalidate = 4,
  GuestSnapshot = 5,
  Marker = 6,
};

struct GxCaptureSummary {
  uint64_t records = 0;
  uint64_t frames = 0;
  uint64_t fifoPackets = 0;
  uint64_t fifoBytes = 0;
  uint64_t invalidations = 0;
  uint64_t snapshots = 0;
  uint64_t snapshotBytes = 0;
  uint64_t markers = 0;
  uint64_t crcErrors = 0;
  uint64_t malformedRecords = 0;
};

struct GxCaptureRecord {
  GxCaptureRecordType type = GxCaptureRecordType::Marker;
  uint64_t frame = 0;
  uint64_t sequence = 0;
  uint32_t guestAddress = 0;
  uint64_t frameUs = 0;
  const uint8_t* data = nullptr;
  size_t bytes = 0;
  std::string text{};
};

uint32_t gx_capture_crc32(const void* data, size_t bytes) noexcept;

class GxCaptureWriter {
public:
  GxCaptureWriter() = default;
  ~GxCaptureWriter();
  bool open(const char* path, const char* upstreamCommit = nullptr) noexcept;
  void close() noexcept;
  bool is_open() const noexcept { return fp_ != nullptr; }
  bool frame_begin(uint64_t frame) noexcept;
  bool frame_end(uint64_t frame, uint64_t frameUs) noexcept;
  bool fifo(uint64_t frame, const void* data, size_t bytes) noexcept;
  bool invalidate(uint64_t frame, uint32_t guestAddress, size_t bytes) noexcept;
  bool guest_snapshot(uint64_t frame, uint32_t guestAddress, const void* data, size_t bytes) noexcept;
  bool marker(uint64_t frame, const char* text) noexcept;
  bool flush() noexcept;
  uint64_t records_written() const noexcept { return sequence_; }
  uint64_t bytes_written() const noexcept { return bytesWritten_; }
private:
  bool record(GxCaptureRecordType type, uint64_t frame, const void* data, size_t bytes) noexcept;
  FILE* fp_ = nullptr;
  uint64_t sequence_ = 0;
  uint64_t bytesWritten_ = 0;
};

class GxCaptureReader {
public:
  using Visitor = std::function<bool(const GxCaptureRecord&)>;
  bool read(const char* path, const Visitor& visitor, GxCaptureSummary* summary = nullptr) noexcept;
  const std::string& error() const noexcept { return error_; }
private:
  std::string error_{};
};

std::string gx_capture_format_summary(const GxCaptureSummary& summary);

} // namespace aurora::vita::integration
