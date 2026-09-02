#pragma once
#include "vita_gfx_types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace aurora::vita::gfx {
struct CompiledPipeline {
  uint64_t key=0;
  uint64_t lastUsed=0;
  PipelineDesc desc{};
  unsigned program=0;
  int uMvp=-1,uKColor=-1,uTevReg=-1,uFogColor=-1,uFogParams=-1,uFogRangeK=-1,uRenderViewportWidth=-1,uIndMtx=-1,uTexcoordScale=-1,uTextureSizeBias=-1;
  std::array<int,MaxTextures> uTex{};
  mutable DrawUniforms cachedUniforms{};
  mutable uint16_t uniformValidMask=0;
};

class PipelineCache {
public:
  explicit PipelineCache(size_t maxEntries=512) noexcept : maxEntries_(maxEntries) {}
  ~PipelineCache();
  const CompiledPipeline* get_or_create(const PipelineDesc& desc,FrameStats* stats=nullptr) noexcept;
  const CompiledPipeline* find(uint64_t key) noexcept;
  void bind(const CompiledPipeline& p,const DrawUniforms& u,FrameStats* stats=nullptr) noexcept;
  void clear() noexcept;
  void invalidate_bound() noexcept{bound_=0;}
  void set_max_entries(size_t maxEntries) noexcept;
  void pin(uint64_t key) noexcept { if(key) pinned_.insert(key); }
  void clear_pins() noexcept { pinned_.clear(); }
  void trim_to_budget() noexcept;
  size_t pinned_entries() const noexcept { return pinned_.size(); }
  size_t max_entries() const noexcept{return maxEntries_;}
  size_t size()const noexcept{return map_.size();}
  size_t high_water_entries() const noexcept{return highWaterEntries_;}
  uint64_t compile_failures() const noexcept{return compileFailures_;}
  uint64_t evictions() const noexcept{return evictions_;}
private:
  bool evict_one() noexcept;
  void destroy_pipeline(CompiledPipeline& pipeline) noexcept;
  std::unordered_map<uint64_t,CompiledPipeline> map_;
  std::unordered_set<uint64_t> pinned_{};
  uint64_t bound_=0;
  uint64_t useSequence_=0;
  size_t maxEntries_=512;
  size_t highWaterEntries_=0;
  uint64_t compileFailures_=0;
  uint64_t evictions_=0;
};
} // namespace aurora::vita::gfx
