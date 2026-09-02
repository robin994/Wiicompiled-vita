#pragma once
#include "vita_pipeline_cache.hpp"
#include "vita_efb.hpp"
#include "vita_streaming_arena.hpp"
#include "vita_texture_cache.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace aurora::vita::gfx {

struct MemoryBudgetSnapshot {
  size_t vertexCapacity = 0;
  size_t vertexUsed = 0;
  size_t vertexHighWater = 0;
  size_t indexCapacity = 0;
  size_t indexUsed = 0;
  size_t indexHighWater = 0;
  size_t textureBudget = 0;
  size_t textureBytes = 0;
  size_t textureHighWater = 0;
  size_t textureEntries = 0;
  size_t pipelineBudget = 0;
  size_t pipelineEntries = 0;
  size_t pipelineHighWater = 0;
  size_t efbBytes = 0;
  size_t efbHighWater = 0;
  size_t efbEntries = 0;
  uint64_t vertexOverflows = 0;
  uint64_t indexOverflows = 0;
  uint64_t textureEvictions = 0;
  uint64_t pipelineCompileFailures = 0;
  uint64_t pipelineEvictions = 0;

  bool within_stream_budget() const noexcept { return vertexUsed <= vertexCapacity && indexUsed <= indexCapacity; }
  bool within_texture_budget() const noexcept { return textureBytes <= textureBudget; }
  std::string format() const;
};

MemoryBudgetSnapshot capture_memory_budget(const StreamingArena& arena, const TextureCache& textures,
                                           const PipelineCache& pipelines, const EfbManager* efb=nullptr) noexcept;

} // namespace aurora::vita::gfx
