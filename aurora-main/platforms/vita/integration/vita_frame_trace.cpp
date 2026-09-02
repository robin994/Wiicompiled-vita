#include "vita_frame_trace.hpp"
#include <cstdio>
#include <sstream>

namespace aurora::vita::integration {

uint64_t trace_hash_bytes(const void* data, size_t bytes) noexcept {
  const auto* p = static_cast<const uint8_t*>(data);
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}

void FrameTrace::reset() { records_.clear(); frame_ = draw_ = dropped_ = 0; }

void FrameTrace::record(uint64_t pipelineKey, uint64_t vertexHash, uint32_t rawBytes, uint32_t vertexCount,
                        uint32_t indexCount, uint8_t primitive, uint8_t vertexFormat,
                        uint8_t fallbackTextureMask, uint8_t warningMask) {
  DrawTraceRecord rec{frame_, draw_++, pipelineKey, vertexHash, rawBytes, vertexCount, indexCount,
                      primitive, vertexFormat, fallbackTextureMask, warningMask};
  if (capacity_ == 0) { ++dropped_; return; }
  if (records_.size() < capacity_) records_.push_back(rec);
  else {
    const size_t slot = static_cast<size_t>((rec.draw + rec.frame * 1315423911ull) % capacity_);
    records_[slot] = rec;
    ++dropped_;
  }
}

std::string FrameTrace::report(size_t maxRecords) const {
  std::ostringstream out;
  out << "[AURORA-VITA][TRACE] records=" << records_.size() << " dropped=" << dropped_ << '\n';
  size_t start = 0;
  if (maxRecords && records_.size() > maxRecords) start = records_.size() - maxRecords;
  for (size_t i = start; i < records_.size(); ++i) {
    const auto& r = records_[i];
    out << "[AURORA-VITA][DRAW] frame=" << r.frame << " draw=" << r.draw
        << " pipe=0x" << std::hex << r.pipelineKey << " vhash=0x" << r.vertexHash << std::dec
        << " bytes=" << r.rawBytes << " vtx=" << r.vertexCount << " idx=" << r.indexCount
        << " prim=" << static_cast<unsigned>(r.primitive) << " fmt=" << static_cast<unsigned>(r.vertexFormat)
        << " fallback=0x" << std::hex << static_cast<unsigned>(r.fallbackTextureMask)
        << " warn=0x" << static_cast<unsigned>(r.warningMask) << std::dec << '\n';
  }
  return out.str();
}

bool FrameTrace::write_report(const char* path, size_t maxRecords) const noexcept {
  if (!path || !*path) return false;
  FILE* fp = std::fopen(path, "wb"); if (!fp) return false;
  const auto text = report(maxRecords); const bool ok = std::fwrite(text.data(),1,text.size(),fp)==text.size();
  std::fclose(fp); return ok;
}

} // namespace aurora::vita::integration
