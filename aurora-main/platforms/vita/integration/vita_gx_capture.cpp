#include "vita_gx_capture.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

namespace aurora::vita::integration {
namespace {
constexpr std::array<char,8> Magic{{'A','V','G','X','C','A','P','1'}};
constexpr uint32_t Version = 1;
constexpr size_t MaxRecordBytes = 64u * 1024u * 1024u;
#pragma pack(push,1)
struct FileHeader {
  char magic[8];
  uint32_t version;
  uint32_t headerBytes;
  char upstreamCommit[48];
  uint64_t reserved[4];
};
struct RecordHeader {
  uint32_t type;
  uint32_t payloadBytes;
  uint64_t frame;
  uint64_t sequence;
  uint32_t crc32;
  uint32_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(FileHeader) == 96);
static_assert(sizeof(RecordHeader) == 32);

bool known_type(uint32_t type) noexcept {
  return type >= static_cast<uint32_t>(GxCaptureRecordType::FrameBegin) &&
         type <= static_cast<uint32_t>(GxCaptureRecordType::Marker);
}

template <typename T>
bool append_scalar(std::vector<uint8_t>& out, const T& value) {
  const auto* p = reinterpret_cast<const uint8_t*>(&value);
  out.insert(out.end(), p, p + sizeof(T));
  return true;
}

template <typename T>
bool read_scalar(const uint8_t*& p, size_t& remaining, T& value) noexcept {
  if (remaining < sizeof(T)) return false;
  std::memcpy(&value, p, sizeof(T));
  p += sizeof(T);
  remaining -= sizeof(T);
  return true;
}
} // namespace

uint32_t gx_capture_crc32(const void* data, size_t bytes) noexcept {
  const auto* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < bytes; ++i) {
    crc ^= p[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

GxCaptureWriter::~GxCaptureWriter() { close(); }

bool GxCaptureWriter::open(const char* path, const char* upstreamCommit) noexcept {
  close();
  if (!path || !*path) return false;
  fp_ = std::fopen(path, "wb");
  if (!fp_) return false;
  FileHeader h{};
  std::memcpy(h.magic, Magic.data(), Magic.size());
  h.version = Version;
  h.headerBytes = sizeof(h);
  if (upstreamCommit) std::snprintf(h.upstreamCommit, sizeof(h.upstreamCommit), "%s", upstreamCommit);
  if (std::fwrite(&h, 1, sizeof(h), fp_) != sizeof(h)) { close(); return false; }
  bytesWritten_ = sizeof(h);
  return true;
}

void GxCaptureWriter::close() noexcept {
  if (fp_) std::fclose(fp_);
  fp_ = nullptr;
  sequence_ = 0;
  bytesWritten_ = 0;
}

bool GxCaptureWriter::record(GxCaptureRecordType type, uint64_t frame, const void* data, size_t bytes) noexcept {
  if (!fp_ || bytes > MaxRecordBytes || (bytes != 0 && !data) || bytes > std::numeric_limits<uint32_t>::max()) return false;
  RecordHeader h{};
  h.type = static_cast<uint32_t>(type);
  h.payloadBytes = static_cast<uint32_t>(bytes);
  h.frame = frame;
  h.sequence = sequence_;
  h.crc32 = gx_capture_crc32(data, bytes);
  if (std::fwrite(&h, 1, sizeof(h), fp_) != sizeof(h)) return false;
  if (bytes && std::fwrite(data, 1, bytes, fp_) != bytes) return false;
  ++sequence_;
  bytesWritten_ += sizeof(h) + bytes;
  return true;
}

bool GxCaptureWriter::frame_begin(uint64_t frame) noexcept { return record(GxCaptureRecordType::FrameBegin, frame, nullptr, 0); }
bool GxCaptureWriter::frame_end(uint64_t frame, uint64_t frameUs) noexcept { return record(GxCaptureRecordType::FrameEnd, frame, &frameUs, sizeof(frameUs)); }
bool GxCaptureWriter::fifo(uint64_t frame, const void* data, size_t bytes) noexcept { return record(GxCaptureRecordType::Fifo, frame, data, bytes); }

bool GxCaptureWriter::invalidate(uint64_t frame, uint32_t guestAddress, size_t bytes) noexcept {
  if (bytes > std::numeric_limits<uint32_t>::max()) return false;
  std::array<uint32_t,2> payload{{guestAddress, static_cast<uint32_t>(bytes)}};
  return record(GxCaptureRecordType::Invalidate, frame, payload.data(), sizeof(payload));
}

bool GxCaptureWriter::guest_snapshot(uint64_t frame, uint32_t guestAddress, const void* data, size_t bytes) noexcept {
  if (!data || bytes == 0 || bytes > MaxRecordBytes - sizeof(uint32_t)) return false;
  std::vector<uint8_t> payload;
  payload.reserve(sizeof(uint32_t) + bytes);
  append_scalar(payload, guestAddress);
  const auto* p = static_cast<const uint8_t*>(data);
  payload.insert(payload.end(), p, p + bytes);
  return record(GxCaptureRecordType::GuestSnapshot, frame, payload.data(), payload.size());
}

bool GxCaptureWriter::marker(uint64_t frame, const char* text) noexcept {
  if (!text) text = "";
  return record(GxCaptureRecordType::Marker, frame, text, std::strlen(text));
}

bool GxCaptureWriter::flush() noexcept { return fp_ && std::fflush(fp_) == 0; }

bool GxCaptureReader::read(const char* path, const Visitor& visitor, GxCaptureSummary* outSummary) noexcept {
  error_.clear();
  GxCaptureSummary summary{};
  if (!path || !*path) { error_ = "empty path"; return false; }
  FILE* fp = std::fopen(path, "rb");
  if (!fp) { error_ = "open failed"; return false; }
  FileHeader file{};
  if (std::fread(&file, 1, sizeof(file), fp) != sizeof(file)) { error_ = "truncated header"; std::fclose(fp); return false; }
  if (std::memcmp(file.magic, Magic.data(), Magic.size()) != 0 || file.version != Version || file.headerBytes != sizeof(file)) {
    error_ = "invalid header"; std::fclose(fp); return false;
  }
  uint64_t expectedSequence = 0;
  std::vector<uint8_t> payload;
  while (true) {
    RecordHeader h{};
    const size_t got = std::fread(&h, 1, sizeof(h), fp);
    if (got == 0 && std::feof(fp)) break;
    if (got != sizeof(h)) { error_ = "truncated record header"; ++summary.malformedRecords; std::fclose(fp); if(outSummary)*outSummary=summary; return false; }
    if (!known_type(h.type) || h.payloadBytes > MaxRecordBytes || h.sequence != expectedSequence) {
      error_ = "malformed record header"; ++summary.malformedRecords; std::fclose(fp); if(outSummary)*outSummary=summary; return false;
    }
    payload.resize(h.payloadBytes);
    if (h.payloadBytes && std::fread(payload.data(), 1, h.payloadBytes, fp) != h.payloadBytes) {
      error_ = "truncated record payload"; ++summary.malformedRecords; std::fclose(fp); if(outSummary)*outSummary=summary; return false;
    }
    if (gx_capture_crc32(payload.data(), payload.size()) != h.crc32) {
      error_ = "crc mismatch"; ++summary.crcErrors; std::fclose(fp); if(outSummary)*outSummary=summary; return false;
    }
    ++expectedSequence;
    ++summary.records;
    GxCaptureRecord rec{};
    rec.type = static_cast<GxCaptureRecordType>(h.type);
    rec.frame = h.frame;
    rec.sequence = h.sequence;
    rec.data = payload.data();
    rec.bytes = payload.size();
    const uint8_t* p = payload.data();
    size_t remaining = payload.size();
    switch (rec.type) {
      case GxCaptureRecordType::FrameBegin:
        if (remaining != 0) { error_ = "invalid frame begin"; ++summary.malformedRecords; std::fclose(fp); return false; }
        ++summary.frames;
        break;
      case GxCaptureRecordType::FrameEnd:
        if (!read_scalar(p, remaining, rec.frameUs) || remaining != 0) { error_ = "invalid frame end"; ++summary.malformedRecords; std::fclose(fp); return false; }
        break;
      case GxCaptureRecordType::Fifo:
        ++summary.fifoPackets; summary.fifoBytes += rec.bytes;
        break;
      case GxCaptureRecordType::Invalidate: {
        uint32_t bytes32 = 0;
        if (!read_scalar(p, remaining, rec.guestAddress) || !read_scalar(p, remaining, bytes32) || remaining != 0) { error_ = "invalid invalidate"; ++summary.malformedRecords; std::fclose(fp); return false; }
        rec.bytes = bytes32; rec.data = nullptr; ++summary.invalidations; break;
      }
      case GxCaptureRecordType::GuestSnapshot:
        if (!read_scalar(p, remaining, rec.guestAddress) || remaining == 0) { error_ = "invalid guest snapshot"; ++summary.malformedRecords; std::fclose(fp); return false; }
        rec.data = p; rec.bytes = remaining; ++summary.snapshots; summary.snapshotBytes += remaining; break;
      case GxCaptureRecordType::Marker:
        rec.text.assign(reinterpret_cast<const char*>(payload.data()), payload.size()); ++summary.markers; break;
    }
    if (visitor && !visitor(rec)) break;
  }
  std::fclose(fp);
  if (outSummary) *outSummary = summary;
  return true;
}

std::string gx_capture_format_summary(const GxCaptureSummary& s) {
  std::ostringstream out;
  out << "[AURORA-VITA][CAPTURE] records=" << s.records << " frames=" << s.frames
      << " fifo_packets=" << s.fifoPackets << " fifo_bytes=" << s.fifoBytes
      << " invalidations=" << s.invalidations << " snapshots=" << s.snapshots
      << " snapshot_bytes=" << s.snapshotBytes << " markers=" << s.markers
      << " crc_errors=" << s.crcErrors << " malformed=" << s.malformedRecords;
  return out.str();
}

} // namespace aurora::vita::integration
