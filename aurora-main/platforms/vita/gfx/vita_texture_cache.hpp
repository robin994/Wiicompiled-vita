#pragma once
#include "vita_gfx_types.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
namespace aurora::vita::gfx {
class TextureCache {
public:
  explicit TextureCache(size_t budget=24*1024*1024):budget_(budget){}~TextureCache();
  Handle get_or_upload(const TextureDesc& desc,uint64_t frame,FrameStats* stats=nullptr) noexcept;
  void bind(Handle h,unsigned unit,const SamplerDesc& sampler) noexcept;
  void erase(Handle h) noexcept;void clear() noexcept;void trim(uint64_t frame) noexcept;
  size_t invalidate_source_range(uint64_t start,size_t bytes) noexcept;
  size_t bytes()const noexcept{return bytes_;}size_t entries()const noexcept{return byKey_.size();}
  size_t budget() const noexcept{return budget_;}
  size_t high_water_bytes() const noexcept{return highWaterBytes_;}
  uint64_t evictions() const noexcept{return evictions_;}
  // M12.1 OOM-hardening telemetry.
  uint64_t alloc_fail_total() const noexcept{return allocFailTotal_;}
  uint64_t pre_evictions() const noexcept{return preEvictions_;}
  uint64_t pre_evicted_bytes() const noexcept{return preEvictedBytes_;}
  uint64_t last_requested_bytes() const noexcept{return lastRequestedBytes_;}
  uint64_t evict_blocked() const noexcept{return evictBlocked_;}
  uint64_t protected_bytes() const noexcept{return protectedBytesLast_;}
  uint64_t protected_bytes_high_water() const noexcept{return protectedBytesHighWater_;}
  bool last_allocation_blocked_by_protection() const noexcept{return lastBlockedByProtection_;}
  // Called only after a real GPU drain (glFinish / equivalent). Entries used at
  // or before this frame are no longer referenced by in-flight command buffers.
  void mark_gpu_idle(uint64_t frame) noexcept;
private:
  struct Entry{Handle handle=InvalidHandle;unsigned gl=0;uint64_t key=0,lastUse=0,useEpoch=0;size_t bytes=0;bool hasMipmaps=false;uint64_t sourceId=0,paletteSourceId=0;size_t sourceBytes=0,paletteBytes=0;};
  // Evict LRU entries (never the entry keyed protectKey) until bytes_+requiredBytes fits
  // under budget_ with headroom. Runs BEFORE any vitaGL allocation.
  void pre_evict(size_t requiredBytes,uint64_t frame,uint64_t protectKey) noexcept;
  std::unordered_map<uint64_t,Entry> byKey_;std::unordered_map<Handle,uint64_t> byHandle_;Handle next_=1;size_t budget_=0,bytes_=0,highWaterBytes_=0;uint64_t evictions_=0;
  uint64_t allocFailTotal_=0,preEvictions_=0,preEvictedBytes_=0,lastRequestedBytes_=0;
  uint64_t evictBlocked_=0,protectedBytesLast_=0,protectedBytesHighWater_=0;
  uint64_t useEpoch_=0,completedUseEpoch_=0;
  bool completedUseEpochValid_=false,lastBlockedByProtection_=false;
};
} // namespace aurora::vita::gfx
