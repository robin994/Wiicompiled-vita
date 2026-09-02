#include "vita_memory_budget.hpp"
#include <sstream>

namespace aurora::vita::gfx {

MemoryBudgetSnapshot capture_memory_budget(const StreamingArena& arena, const TextureCache& textures,
                                           const PipelineCache& pipelines, const EfbManager* efb) noexcept {
  MemoryBudgetSnapshot s{};
  s.vertexCapacity = arena.vertex_capacity(); s.vertexUsed = arena.vertex_used(); s.vertexHighWater = arena.vertex_high_water();
  s.indexCapacity = arena.index_capacity(); s.indexUsed = arena.index_used(); s.indexHighWater = arena.index_high_water();
  s.textureBudget = textures.budget(); s.textureBytes = textures.bytes(); s.textureHighWater = textures.high_water_bytes(); s.textureEntries = textures.entries();
  s.pipelineBudget = pipelines.max_entries(); s.pipelineEntries = pipelines.size(); s.pipelineHighWater = pipelines.high_water_entries();
  if (efb) { s.efbBytes = efb->bytes(); s.efbHighWater = efb->high_water_bytes(); s.efbEntries = efb->entries(); }
  s.vertexOverflows = arena.vertex_overflows(); s.indexOverflows = arena.index_overflows(); s.textureEvictions = textures.evictions(); s.pipelineCompileFailures = pipelines.compile_failures(); s.pipelineEvictions = pipelines.evictions();
  return s;
}

std::string MemoryBudgetSnapshot::format() const {
  std::ostringstream out;
  out << "[AURORA-VITA][MEM] vertex=" << vertexUsed << '/' << vertexCapacity << " high=" << vertexHighWater
      << " index=" << indexUsed << '/' << indexCapacity << " high=" << indexHighWater
      << " texture=" << textureBytes << '/' << textureBudget << " high=" << textureHighWater << " entries=" << textureEntries
      << " pipelines=" << pipelineEntries << '/' << pipelineBudget << " high=" << pipelineHighWater
      << " efb=" << efbBytes << " high=" << efbHighWater << " entries=" << efbEntries
      << " pipe_evict=" << pipelineEvictions
      << " v_overflow=" << vertexOverflows << " i_overflow=" << indexOverflows
      << " tex_evict=" << textureEvictions << " shader_fail=" << pipelineCompileFailures;
  return out.str();
}

} // namespace aurora::vita::gfx
