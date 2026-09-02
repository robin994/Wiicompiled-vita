#pragma once
#include <cstddef>
#include <cstdint>

namespace aurora::vita::integration {

struct GxRawDraw {
  uint8_t primitive = 0;
  uint8_t vertexFormat = 0;
  uint16_t reserved = 0;
  const uint8_t* vertexData = nullptr;
  size_t vertexBytes = 0;
  uint32_t vertexCount = 0;
  // Optional host-endian u16 index stream (used by GX_AURORA_DRAW_INDEXED).
  const uint16_t* indexData = nullptr;
  uint32_t indexCount = 0;
};

struct GxBackendSubmitStatus {
  bool ok = false;
  uint32_t error = 0;
  uint32_t warningMask = 0;
};

// Minimal renderer-neutral seam intended to be called by Aurora's FIFO/BP/CP/XF core.
// The GX core owns state parsing; a backend owns resource creation and draw submission.
class GxBackendApi {
public:
  virtual ~GxBackendApi() = default;
  virtual bool begin_frame(uint64_t frame) noexcept = 0;
  virtual GxBackendSubmitStatus submit_raw_draw(const GxRawDraw& draw) noexcept = 0;
  virtual bool copy_tex(const void* destination, bool clear) noexcept = 0;
  virtual void evict_copy_tex(const void* destination) noexcept = 0;
  virtual void clear_copy_textures() noexcept = 0;
  virtual void invalidate_texture_range(uint64_t sourceAddress, size_t bytes) noexcept = 0;
  virtual bool flush() noexcept = 0;
  virtual bool end_frame(uint64_t frame) noexcept = 0;
  virtual bool strict_failed() const noexcept = 0;
};

} // namespace aurora::vita::integration
