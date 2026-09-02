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
private:
  struct Entry{Handle handle=InvalidHandle;unsigned gl=0;uint64_t key=0,lastUse=0;size_t bytes=0;bool hasMipmaps=false;uint64_t sourceId=0,paletteSourceId=0;size_t sourceBytes=0,paletteBytes=0;};
  std::unordered_map<uint64_t,Entry> byKey_;std::unordered_map<Handle,uint64_t> byHandle_;Handle next_=1;size_t budget_=0,bytes_=0,highWaterBytes_=0;uint64_t evictions_=0;
};
} // namespace aurora::vita::gfx
