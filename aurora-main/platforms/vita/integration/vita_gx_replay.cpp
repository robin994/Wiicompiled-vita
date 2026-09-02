#include "vita_gx_replay.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>

namespace aurora::vita::integration {

void GuestSnapshotMirror::apply(uint32_t guestAddress, const void* data, size_t bytes, uint64_t sequence) {
  if (!data || bytes == 0) return;
  Snapshot s{}; s.address = guestAddress; s.sequence = sequence;
  const auto* p = static_cast<const uint8_t*>(data); s.data.assign(p, p + bytes);
  snapshots_.push_back(std::move(s));
}

ReplaySpan GuestSnapshotMirror::map(uint32_t guestAddress, size_t bytes) const noexcept {
  if (bytes == 0) return {};
  const uint64_t end = static_cast<uint64_t>(guestAddress) + bytes;
  if (end > (uint64_t{1} << 32)) return {};
  for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it) {
    const uint64_t start = it->address;
    const uint64_t stop = start + it->data.size();
    if (guestAddress >= start && end <= stop) {
      return {it->data.data() + (guestAddress - it->address), bytes};
    }
  }
  return {};
}

size_t GuestSnapshotMirror::bytes_stored() const noexcept {
  size_t total = 0;
  for (const auto& s : snapshots_) {
    if (s.data.size() > std::numeric_limits<size_t>::max() - total) return std::numeric_limits<size_t>::max();
    total += s.data.size();
  }
  return total;
}

bool GxCaptureReplayer::replay(const char* path, const GxReplayCallbacks& cb, GxReplayStats* outStats) noexcept {
  mirror_.reset(); error_.clear(); GxReplayStats stats{};
  GxCaptureReader reader; bool callbackOk = true;
  const bool readOk = reader.read(path, [&](const GxCaptureRecord& r) {
    ++stats.records;
    bool ok = true;
    switch (r.type) {
      case GxCaptureRecordType::FrameBegin:
        ++stats.frames;
        if (cb.frame_begin) ok = cb.frame_begin(cb.user, r.frame);
        break;
      case GxCaptureRecordType::FrameEnd:
        if (cb.frame_end) ok = cb.frame_end(cb.user, r.frame, r.frameUs);
        break;
      case GxCaptureRecordType::Fifo:
        ++stats.fifoPackets;
        stats.fifoBytes += r.bytes;
        if (cb.fifo) ok = cb.fifo(cb.user, r.frame, r.data, r.bytes, mirror_);
        break;
      case GxCaptureRecordType::Invalidate:
        ++stats.invalidations;
        if (cb.invalidate) ok = cb.invalidate(cb.user, r.frame, r.guestAddress, r.bytes);
        break;
      case GxCaptureRecordType::GuestSnapshot:
        mirror_.apply(r.guestAddress, r.data, r.bytes, r.sequence);
        ++stats.snapshotsApplied;
        stats.snapshotBytes += r.bytes;
        break;
      case GxCaptureRecordType::Marker:
        ++stats.markers;
        if (cb.marker) ok = cb.marker(cb.user, r.frame, r.text.c_str());
        break;
    }
    if (!ok) { ++stats.callbackFailures; callbackOk = false; }
    return ok;
  });
  if (!readOk) { error_ = reader.error(); if(outStats)*outStats=stats; return false; }
  if (!callbackOk) { error_ = "replay callback failed"; if(outStats)*outStats=stats; return false; }
  if (outStats) *outStats = stats;
  return true;
}

std::string gx_replay_format_stats(const GxReplayStats& s) {
  std::ostringstream out;
  out << "[AURORA-VITA][REPLAY] records=" << s.records << " frames=" << s.frames
      << " fifo_packets=" << s.fifoPackets << " fifo_bytes=" << s.fifoBytes
      << " snapshots=" << s.snapshotsApplied << " snapshot_bytes=" << s.snapshotBytes
      << " invalidations=" << s.invalidations << " markers=" << s.markers
      << " callback_failures=" << s.callbackFailures;
  return out.str();
}

} // namespace aurora::vita::integration
