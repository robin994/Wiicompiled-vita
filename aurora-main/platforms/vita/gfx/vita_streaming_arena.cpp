#include "vita_streaming_arena.hpp"
#include <cstdint>
#include <cstring>
#include <limits>
#if defined(__vita__)
#include <cstdio>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#endif

#ifndef MKW_VITA_DIRECT_STREAM_WRITE
#define MKW_VITA_DIRECT_STREAM_WRITE 0
#endif

namespace aurora::vita::gfx {

StreamingArena::StreamingArena(BufferPool& pool, StreamingArenaConfig cfg) noexcept : pool_(pool), cfg_(cfg) {}
StreamingArena::~StreamingArena() { shutdown(); }

size_t StreamingArena::align_up(size_t value, size_t alignment) noexcept {
  if (alignment <= 1) return value;
  const size_t remainder = value % alignment;
  return remainder ? value + (alignment - remainder) : value;
}

bool StreamingArena::initialize() noexcept {
  if (initialized_) return true;
  if (cfg_.slots == 0 || cfg_.vertexBytes == 0 || cfg_.indexBytes == 0) return false;

#if defined(__vita__)
  const uint64_t slotsResizeBeginUs = sceKernelGetProcessTimeWide();
  std::fprintf(stderr,
               "[gx] init_marker=aurora_stream_slots phase=resize_begin t_us=%llu slots=%u\n",
               static_cast<unsigned long long>(slotsResizeBeginUs), cfg_.slots);
  std::fflush(stderr);
#endif
  slots_.resize(cfg_.slots);
#if defined(__vita__)
  const uint64_t slotsResizeEndUs = sceKernelGetProcessTimeWide();
  std::fprintf(stderr,
               "[gx] init_marker=aurora_stream_slots phase=resize_end t_us=%llu elapsed_us=%llu capacity=%u\n",
               static_cast<unsigned long long>(slotsResizeEndUs),
               static_cast<unsigned long long>(slotsResizeEndUs - slotsResizeBeginUs),
               static_cast<unsigned>(slots_.capacity()));
  std::fflush(stderr);
#endif
  for (size_t slotIndex = 0; slotIndex < slots_.size(); ++slotIndex) {
    auto& slot = slots_[slotIndex];
#if defined(__vita__)
    const uint64_t vboBeginUs = sceKernelGetProcessTimeWide();
    std::fprintf(stderr,
                 "[gx] init_marker=aurora_stream_slot phase=vbo_begin t_us=%llu slot=%u bytes=%u\n",
                 static_cast<unsigned long long>(vboBeginUs),
                 static_cast<unsigned>(slotIndex), static_cast<unsigned>(cfg_.vertexBytes));
    std::fflush(stderr);
#endif
    slot.vertex = pool_.create_vertex(nullptr, cfg_.vertexBytes, true);
#if defined(__vita__)
    const uint64_t vboEndUs = sceKernelGetProcessTimeWide();
    std::fprintf(stderr,
                 "[gx] init_marker=aurora_stream_slot phase=vbo_end t_us=%llu elapsed_us=%llu slot=%u handle=%u\n",
                 static_cast<unsigned long long>(vboEndUs),
                 static_cast<unsigned long long>(vboEndUs - vboBeginUs),
                 static_cast<unsigned>(slotIndex), static_cast<unsigned>(slot.vertex));
    std::fflush(stderr);
    const uint64_t iboBeginUs = sceKernelGetProcessTimeWide();
    std::fprintf(stderr,
                 "[gx] init_marker=aurora_stream_slot phase=ibo_begin t_us=%llu slot=%u bytes=%u\n",
                 static_cast<unsigned long long>(iboBeginUs),
                 static_cast<unsigned>(slotIndex), static_cast<unsigned>(cfg_.indexBytes));
    std::fflush(stderr);
#endif
    slot.index = pool_.create_index(nullptr, cfg_.indexBytes, true);
#if defined(__vita__)
    const uint64_t iboEndUs = sceKernelGetProcessTimeWide();
    std::fprintf(stderr,
                 "[gx] init_marker=aurora_stream_slot phase=ibo_end t_us=%llu elapsed_us=%llu slot=%u handle=%u\n",
                 static_cast<unsigned long long>(iboEndUs),
                 static_cast<unsigned long long>(iboEndUs - iboBeginUs),
                 static_cast<unsigned>(slotIndex), static_cast<unsigned>(slot.index));
    std::fflush(stderr);
#endif
    if (!slot.vertex || !slot.index) {
#if defined(__vita__)
      std::fprintf(stderr,
                   "[gx] init_marker=aurora_stream_slot phase=failed slot=%u vbo=%u ibo=%u\n",
                   static_cast<unsigned>(slotIndex), static_cast<unsigned>(slot.vertex),
                   static_cast<unsigned>(slot.index));
      std::fflush(stderr);
#endif
      shutdown();
      return false;
    }
  }
#if !MKW_VITA_DIRECT_STREAM_WRITE
#if defined(__vita__)
  const uint64_t stagingReserveBeginUs = sceKernelGetProcessTimeWide();
  std::fprintf(stderr,
               "[gx] init_marker=aurora_staging_vectors phase=reserve_begin t_us=%llu vertex_bytes=%u index_bytes=%u\n",
               static_cast<unsigned long long>(stagingReserveBeginUs),
               static_cast<unsigned>(cfg_.vertexBytes), static_cast<unsigned>(cfg_.indexBytes));
  std::fflush(stderr);
#endif
  vertexStage_.reserve(cfg_.vertexBytes);
  indexStage_.reserve(cfg_.indexBytes);
#if defined(__vita__)
  const uint64_t stagingReserveEndUs = sceKernelGetProcessTimeWide();
  std::fprintf(stderr,
               "[gx] init_marker=aurora_staging_vectors phase=reserve_end t_us=%llu elapsed_us=%llu vertex_capacity=%u index_capacity=%u\n",
               static_cast<unsigned long long>(stagingReserveEndUs),
               static_cast<unsigned long long>(stagingReserveEndUs - stagingReserveBeginUs),
               static_cast<unsigned>(vertexStage_.capacity()),
               static_cast<unsigned>(indexStage_.capacity()));
  std::fflush(stderr);
  const uint64_t stagingResizeBeginUs = sceKernelGetProcessTimeWide();
  std::fprintf(stderr,
               "[gx] init_marker=aurora_staging_vectors phase=resize_begin t_us=%llu\n",
               static_cast<unsigned long long>(stagingResizeBeginUs));
  std::fflush(stderr);
#endif
  vertexStage_.resize(cfg_.vertexBytes);
  indexStage_.resize(cfg_.indexBytes);
#if defined(__vita__)
  const uint64_t stagingResizeEndUs = sceKernelGetProcessTimeWide();
  std::fprintf(stderr,
               "[gx] init_marker=aurora_staging_vectors phase=resize_end t_us=%llu elapsed_us=%llu vertex_size=%u index_size=%u\n",
               static_cast<unsigned long long>(stagingResizeEndUs),
               static_cast<unsigned long long>(stagingResizeEndUs - stagingResizeBeginUs),
               static_cast<unsigned>(vertexStage_.size()),
               static_cast<unsigned>(indexStage_.size()));
  std::fflush(stderr);
#endif
#else
  vertexStage_.clear();
  indexStage_.clear();
#endif
  initialized_ = true;
  return true;
}

void StreamingArena::shutdown() noexcept {
#if defined(__vita__) && MKW_VITA_STREAM_SAFE_REUSE
  for (const auto& slot : slots_) if (slot.inFlight) { glFinish(); break; }
#endif
  for (auto& slot : slots_) {
#if defined(__vita__) && MKW_VITA_DIRECT_STREAM_WRITE
    if (slot.vmapped && slot.vertex) { glBindBuffer(GL_ARRAY_BUFFER, pool_.gl_id(slot.vertex)); (void)glUnmapBuffer(GL_ARRAY_BUFFER); slot.vmapped = nullptr; }
    if (slot.imapped && slot.index) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pool_.gl_id(slot.index)); (void)glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER); slot.imapped = nullptr; }
#endif
    if (slot.vertex) pool_.destroy(slot.vertex);
    if (slot.index) pool_.destroy(slot.index);
  }
  slots_.clear();
  vertexStage_.clear();
  indexStage_.clear();
  initialized_ = false;
  vertexHighWater_ = indexHighWater_ = 0;
  vertexOverflows_ = indexOverflows_ = 0;
  current_ = 0;
}

void StreamingArena::begin_frame(uint64_t frame) noexcept {
  if (!initialized_ || slots_.empty()) return;
  current_ = static_cast<uint32_t>(frame % slots_.size());
  auto& slot = slots_[current_];
  reuseWaitUs_ = 0;
#if defined(__vita__) && MKW_VITA_STREAM_SAFE_REUSE
  // vitaGL's MapBufferRange ignores last_frame. Rotation alone is no fence:
  // drain before overwriting a slot that may still be read by the GPU. One
  // drain retires every slot, rather than synchronizing every logical draw.
  if (slot.inFlight) {
    const uint64_t begin=sceKernelGetProcessTimeWide();
    glFinish();
    reuseWaitUs_=sceKernelGetProcessTimeWide()-begin;
    for (auto& submitted : slots_) submitted.inFlight=false;
  }
#endif
#if defined(__vita__) && MKW_VITA_DIRECT_STREAM_WRITE
  if (slot.vmapped && slot.vertex) { glBindBuffer(GL_ARRAY_BUFFER, pool_.gl_id(slot.vertex)); (void)glUnmapBuffer(GL_ARRAY_BUFFER); slot.vmapped = nullptr; }
  if (slot.imapped && slot.index) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pool_.gl_id(slot.index)); (void)glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER); slot.imapped = nullptr; }
#endif
  slot.voff = 0;
  slot.ioff = 0;
  slot.vflushed = 0;
  slot.iflushed = 0;
}

BufferSlice StreamingArena::reserve(bool vertex, size_t bytes, size_t alignment, void** writable) noexcept {
  if (writable) *writable = nullptr;
  if (!initialized_ || !bytes || !writable) return {};
  auto& slot = slots_[current_];
  const size_t requestedAlignment = alignment ? alignment : cfg_.alignment;
  size_t& offset = vertex ? slot.voff : slot.ioff;
  const size_t capacity = vertex ? cfg_.vertexBytes : cfg_.indexBytes;
  const size_t aligned = align_up(offset, requestedAlignment);
  if (aligned > capacity || bytes > capacity - aligned) {
    if (vertex) ++vertexOverflows_; else ++indexOverflows_;
    return {};
  }

  const Handle handle = vertex ? slot.vertex : slot.index;
#if defined(__vita__) && MKW_VITA_DIRECT_STREAM_WRITE
  void*& mapped = vertex ? slot.vmapped : slot.imapped;
  if (!mapped) {
    const GLenum target = vertex ? GL_ARRAY_BUFFER : GL_ELEMENT_ARRAY_BUFFER;
    const GLuint id = pool_.gl_id(handle);
    if (!id) return {};
    glBindBuffer(target, id);
    mapped = glMapBufferRange(target, 0, static_cast<GLsizeiptr>(capacity), GL_MAP_WRITE_BIT);
    if (!mapped) return {};
  }
  *writable = static_cast<uint8_t*>(mapped) + aligned;
#else
  auto& stage = vertex ? vertexStage_ : indexStage_;
  *writable = stage.data() + aligned;
#endif
  offset = aligned + bytes;
  slot.inFlight = true;
  if (vertex) { if (offset > vertexHighWater_) vertexHighWater_ = offset; }
  else { if (offset > indexHighWater_) indexHighWater_ = offset; }
  return {handle, static_cast<uint32_t>(aligned), static_cast<uint32_t>(bytes)};
}

BufferSlice StreamingArena::upload(bool vertex, const void* data, size_t bytes, size_t alignment) noexcept {
  if (!data) return {};
  void* writable = nullptr;
  const BufferSlice slice = reserve(vertex, bytes, alignment, &writable);
  if (!slice.buffer || !writable) return {};
  std::memcpy(writable, data, bytes);
  return slice;
}

BufferSlice StreamingArena::upload_vertices(const void* data, size_t bytes, size_t alignment) noexcept {
  return upload(true, data, bytes, alignment);
}
BufferSlice StreamingArena::upload_indices(const void* data, size_t bytes, size_t alignment) noexcept {
  return upload(false, data, bytes, alignment);
}
BufferSlice StreamingArena::reserve_vertices(size_t bytes, size_t alignment, void** writable) noexcept {
  return reserve(true, bytes, alignment, writable);
}
BufferSlice StreamingArena::reserve_indices(size_t bytes, size_t alignment, void** writable) noexcept {
  return reserve(false, bytes, alignment, writable);
}
BufferSlice StreamingArena::upload_rebased_indices(const uint16_t* data, size_t count, uint32_t vertexBase) noexcept {
  if (!initialized_ || !data || !count || count > std::numeric_limits<size_t>::max() / sizeof(uint16_t)) return {};
  const size_t bytes = count * sizeof(uint16_t);
  void* writable = nullptr;
  const BufferSlice slice = reserve_indices(bytes, alignof(uint16_t), &writable);
  if (!slice.buffer || !writable) return {};
  auto* destination = static_cast<uint16_t*>(writable);
  for (size_t i = 0; i < count; ++i) {
    const uint32_t absolute = vertexBase + data[i];
    if (absolute > std::numeric_limits<uint16_t>::max()) return {};
    destination[i] = static_cast<uint16_t>(absolute);
  }
  return slice;
}
bool StreamingArena::flush() noexcept {
  if (!initialized_ || slots_.empty()) return false;
  auto& slot = slots_[current_];
#if defined(__vita__) && MKW_VITA_DIRECT_STREAM_WRITE
  bool ok = true;
  if (slot.vmapped) {
    glBindBuffer(GL_ARRAY_BUFFER, pool_.gl_id(slot.vertex));
    ok = glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE && ok;
    slot.vmapped = nullptr;
    slot.vflushed = slot.voff;
  }
  if (slot.imapped) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pool_.gl_id(slot.index));
    ok = glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER) == GL_TRUE && ok;
    slot.imapped = nullptr;
    slot.iflushed = slot.ioff;
  }
  return ok;
#else
  if (slot.voff > slot.vflushed) {
    const size_t bytes = slot.voff - slot.vflushed;
    if (!pool_.update(slot.vertex, vertexStage_.data() + slot.vflushed, bytes, slot.vflushed)) return false;
    slot.vflushed = slot.voff;
  }
  if (slot.ioff > slot.iflushed) {
    const size_t bytes = slot.ioff - slot.iflushed;
    if (!pool_.update(slot.index, indexStage_.data() + slot.iflushed, bytes, slot.iflushed)) return false;
    slot.iflushed = slot.ioff;
  }
  return true;
#endif
}
size_t StreamingArena::vertex_used() const noexcept { return initialized_ ? slots_[current_].voff : 0; }
size_t StreamingArena::index_used() const noexcept { return initialized_ ? slots_[current_].ioff : 0; }

} // namespace aurora::vita::gfx
