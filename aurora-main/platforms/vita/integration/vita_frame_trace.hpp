#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aurora::vita::integration {

struct DrawTraceRecord {
  uint64_t frame = 0;
  uint64_t draw = 0;
  uint64_t pipelineKey = 0;
  uint64_t vertexHash = 0;
  uint32_t rawBytes = 0;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  uint8_t primitive = 0;
  uint8_t vertexFormat = 0;
  uint8_t fallbackTextureMask = 0;
  uint8_t warningMask = 0;
};

class FrameTrace {
public:
  explicit FrameTrace(size_t capacity = 4096) : capacity_(capacity) { records_.reserve(capacity); }
  void reset();
  void begin_frame(uint64_t frame) noexcept { frame_ = frame; draw_ = 0; }
  void record(uint64_t pipelineKey, uint64_t vertexHash, uint32_t rawBytes, uint32_t vertexCount,
              uint32_t indexCount, uint8_t primitive, uint8_t vertexFormat,
              uint8_t fallbackTextureMask, uint8_t warningMask);
  size_t size() const noexcept { return records_.size(); }
  uint64_t dropped() const noexcept { return dropped_; }
  const std::vector<DrawTraceRecord>& records() const noexcept { return records_; }
  std::string report(size_t maxRecords = 0) const;
  bool write_report(const char* path, size_t maxRecords = 0) const noexcept;
private:
  size_t capacity_ = 0;
  std::vector<DrawTraceRecord> records_{};
  uint64_t frame_ = 0;
  uint64_t draw_ = 0;
  uint64_t dropped_ = 0;
};

uint64_t trace_hash_bytes(const void* data, size_t bytes) noexcept;

} // namespace aurora::vita::integration
