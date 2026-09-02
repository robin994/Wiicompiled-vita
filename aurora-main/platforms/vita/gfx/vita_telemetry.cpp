#include "vita_telemetry.hpp"
#include <chrono>
#include <sstream>
#if defined(__vita__)
#include <psp2/kernel/processmgr.h>
#endif

namespace aurora::vita::gfx {

uint64_t telemetry_now_us() noexcept {
#if defined(__vita__)
  return sceKernelGetProcessTimeWide();
#else
  using namespace std::chrono;
  return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

const char* telemetry_phase_name(TelemetryPhase phase) noexcept {
  switch (phase) {
    case TelemetryPhase::VertexDecode: return "vertex_decode";
    case TelemetryPhase::VertexTransform: return "vertex_transform";
    case TelemetryPhase::TextureResolve: return "texture_resolve";
    case TelemetryPhase::PipelineResolve: return "pipeline_resolve";
    case TelemetryPhase::CommandBuild: return "command_build";
    case TelemetryPhase::Submit: return "submit";
    case TelemetryPhase::EfbCopy: return "efb_copy";
    case TelemetryPhase::Present: return "present";
    case TelemetryPhase::Count: break;
  }
  return "unknown";
}

void Telemetry::reset() noexcept { frame_ = {}; lifetime_ = {}; }
void Telemetry::begin_frame(uint64_t frame) noexcept { frame_ = {}; frame_.frame = frame; }
void Telemetry::end_frame(uint64_t totalUs) noexcept { frame_.totalUs = totalUs; ++frame_.counters.frames; ++lifetime_.frames; }
void Telemetry::add_time(TelemetryPhase phase, uint64_t us) noexcept {
  const auto idx = static_cast<size_t>(phase);
  if (idx < frame_.phaseUs.size()) frame_.phaseUs[idx] += us;
}
void Telemetry::add_draw(uint32_t vertices, uint32_t indices, uint32_t triangles) noexcept {
  ++frame_.counters.draws; ++lifetime_.draws;
  frame_.counters.vertices += vertices; lifetime_.vertices += vertices;
  frame_.counters.indices += indices; lifetime_.indices += indices;
  frame_.counters.triangles += triangles; lifetime_.triangles += triangles;
}
void Telemetry::pipeline(bool hit) noexcept {
  if (hit) { ++frame_.counters.pipelineHits; ++lifetime_.pipelineHits; }
  else { ++frame_.counters.pipelineMisses; ++lifetime_.pipelineMisses; }
}
void Telemetry::texture(bool hit, bool uploaded, uint64_t uploadBytes) noexcept {
  if (hit) { ++frame_.counters.textureHits; ++lifetime_.textureHits; }
  else { ++frame_.counters.textureMisses; ++lifetime_.textureMisses; }
  if (uploaded) {
    ++frame_.counters.textureUploads; ++lifetime_.textureUploads;
    frame_.counters.textureUploadBytes += uploadBytes; lifetime_.textureUploadBytes += uploadBytes;
  }
}
void Telemetry::fallback_texture(uint32_t count) noexcept { frame_.counters.fallbackTextures += count; lifetime_.fallbackTextures += count; }
void Telemetry::efb_copy() noexcept { ++frame_.counters.efbCopies; ++lifetime_.efbCopies; }
void Telemetry::arena_overflow() noexcept { ++frame_.counters.arenaOverflows; ++lifetime_.arenaOverflows; }
void Telemetry::unsupported() noexcept { ++frame_.counters.unsupportedFeatures; ++lifetime_.unsupportedFeatures; }

std::string Telemetry::format_frame() const {
  std::ostringstream out;
  out << "[AURORA-VITA][FRAME] frame=" << frame_.frame
      << " total_us=" << frame_.totalUs
      << " draws=" << frame_.counters.draws
      << " vertices=" << frame_.counters.vertices
      << " indices=" << frame_.counters.indices
      << " triangles=" << frame_.counters.triangles
      << " pipeline_hit=" << frame_.counters.pipelineHits
      << " pipeline_miss=" << frame_.counters.pipelineMisses
      << " texture_hit=" << frame_.counters.textureHits
      << " texture_miss=" << frame_.counters.textureMisses
      << " texture_uploads=" << frame_.counters.textureUploads
      << " upload_bytes=" << frame_.counters.textureUploadBytes
      << " fallback_tex=" << frame_.counters.fallbackTextures
      << " efb_copy=" << frame_.counters.efbCopies
      << " arena_overflow=" << frame_.counters.arenaOverflows
      << " unsupported=" << frame_.counters.unsupportedFeatures;
  for (size_t i = 0; i < frame_.phaseUs.size(); ++i) {
    out << ' ' << telemetry_phase_name(static_cast<TelemetryPhase>(i)) << "_us=" << frame_.phaseUs[i];
  }
  return out.str();
}

std::string Telemetry::format_lifetime() const {
  std::ostringstream out;
  out << "[AURORA-VITA][LIFETIME] frames=" << lifetime_.frames
      << " draws=" << lifetime_.draws << " vertices=" << lifetime_.vertices
      << " indices=" << lifetime_.indices << " triangles=" << lifetime_.triangles
      << " pipeline_hit=" << lifetime_.pipelineHits << " pipeline_miss=" << lifetime_.pipelineMisses
      << " texture_hit=" << lifetime_.textureHits << " texture_miss=" << lifetime_.textureMisses
      << " texture_uploads=" << lifetime_.textureUploads << " upload_bytes=" << lifetime_.textureUploadBytes
      << " fallback_tex=" << lifetime_.fallbackTextures << " efb_copy=" << lifetime_.efbCopies
      << " arena_overflow=" << lifetime_.arenaOverflows << " unsupported=" << lifetime_.unsupportedFeatures;
  return out.str();
}

bool Telemetry::append_frame_log(const char* path) const noexcept {
  if (!path || !*path) return false;
  FILE* fp = std::fopen(path, "ab");
  if (!fp) return false;
  const auto line = format_frame();
  const bool ok = std::fwrite(line.data(), 1, line.size(), fp) == line.size() && std::fwrite("\n", 1, 1, fp) == 1;
  std::fclose(fp);
  return ok;
}

ScopedTelemetryPhase::ScopedTelemetryPhase(Telemetry* telemetry, TelemetryPhase phase) noexcept
: telemetry_(telemetry), phase_(phase), startUs_(telemetry ? telemetry_now_us() : 0) {}
ScopedTelemetryPhase::~ScopedTelemetryPhase() {
  if (telemetry_) telemetry_->add_time(phase_, telemetry_now_us() - startUs_);
}

} // namespace aurora::vita::gfx
