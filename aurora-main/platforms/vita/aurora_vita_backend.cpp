#include "aurora_vita_backend.hpp"
#include "gfx/vita_renderer.hpp"
#include "gx/aurora_vita_draw_sink.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#if defined(__vita__)
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <vitaGL.h>
#else
#include <chrono>
#endif

namespace aurora::vita {
namespace {
BackendConfig g_config{};
bool g_initialized=false;
uint64_t g_frame=0,g_start=0,g_last=0;
std::unique_ptr<gfx::Renderer> g_renderer;
std::unique_ptr<gxbridge::DrawSink> g_drawSink;
gfx::Telemetry g_telemetry;
integration::FeatureCoverage g_coverage;
std::unique_ptr<integration::FrameTrace> g_trace;
InitFailure g_initFailure=InitFailure::None;
char g_initFailureDetail[384]{};

uint64_t now_us() noexcept {
#if defined(__vita__)
  return sceKernelGetProcessTimeWide();
#else
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

#if defined(__vita__)
bool path_exists(const char* path) noexcept {
  SceIoStat st{};
  return path && sceIoGetstat(path,&st)>=0 && st.st_size>0;
}

void ensure_parent_dir(const char* path) noexcept {
  if (!path) return;
  // Probe/runtime paths use ux0:data/aurora-vita/*.log. Creating the known parent is harmless if it exists.
  sceIoMkdir("ux0:data/aurora-vita", 0777);
}
#else
void ensure_parent_dir(const char*) noexcept {}
#endif

void emit_periodic_diagnostics() noexcept {
  if (!g_config.diagnostics || !g_drawSink) return;
  const uint32_t period = g_config.diagnostics_period_frames;
  if (period == 0 || (g_frame % period) != 0) return;
  const auto frameLine = g_telemetry.format_frame();
  const auto memLine = g_drawSink->memory_budget().format();
  std::printf("%s\n%s\n", frameLine.c_str(), memLine.c_str());
  if (g_config.telemetry_log_path) {
    ensure_parent_dir(g_config.telemetry_log_path);
    g_telemetry.append_frame_log(g_config.telemetry_log_path);
    FILE* fp = std::fopen(g_config.telemetry_log_path, "ab");
    if (fp) { std::fwrite(memLine.data(),1,memLine.size(),fp); std::fwrite("\n",1,1,fp); std::fclose(fp); }
  }
}
}

bool initialize(const BackendConfig& c) noexcept {
  if (g_initialized) return true;
  g_config=c;
  g_initFailure=InitFailure::None;
  g_initFailureDetail[0]='\0';
#if defined(__vita__)
  // The vitaGL archive shipped by the currently supported VitaSDK probes
  // ur0:data/external/libshacccg.suprx, while vitaShaRK's canonical default
  // remains ur0:/data/libshacccg.suprx. Accept both layouts. If only the
  // canonical path exists, pre-initialize vitaShaRK; shark_init() is
  // deliberately idempotent, so vitaGL's later init observes it as online.
  constexpr const char* ShaccPrimary="ur0:/data/libshacccg.suprx";
  constexpr const char* ShaccExternal="ur0:/data/external/libshacccg.suprx";
  const bool hasPrimary=path_exists(ShaccPrimary) || path_exists("ur0:data/libshacccg.suprx");
  const bool hasExternal=path_exists(ShaccExternal) || path_exists("ur0:data/external/libshacccg.suprx");
  if(!hasPrimary && !hasExternal){
    g_initFailure=InitFailure::ShaderCompilerMissing;
    std::snprintf(g_initFailureDetail,sizeof(g_initFailureDetail),
                  "libshacccg.suprx missing; checked %s and %s",ShaccPrimary,ShaccExternal);
    return false;
  }
  if(!hasExternal && hasPrimary){
    const int sharkRc=shark_init(ShaccPrimary);
    if(sharkRc<0){
      g_initFailure=InitFailure::ShaderCompilerLoadFailed;
      std::snprintf(g_initFailureDetail,sizeof(g_initFailureDetail),
                    "shark_init(%s) failed rc=0x%08x",ShaccPrimary,static_cast<unsigned>(sharkRc));
      return false;
    }
  }
  // vitaGL's current API does not return a success flag here. The value is
  // `res_fallback`: GL_TRUE means the requested framebuffer was clamped to
  // the maximum supported resolution, while the normal successful path for
  // 960x544 returns GL_FALSE after setting vgl_inited=GL_TRUE.
  const GLboolean resolutionFallback =
      vglInitExtended(static_cast<int>(g_config.vgl_legacy_pool_size),
                      static_cast<int>(g_config.width),
                      static_cast<int>(g_config.height),
                      static_cast<int>(g_config.vgl_ram_threshold),
                      SCE_GXM_MULTISAMPLE_NONE);
  if(resolutionFallback){
    std::printf("[aurora-vita] vitaGL framebuffer resolution fallback requested=%ux%u\n",
                g_config.width,g_config.height);
  }
  vglWaitVblankStart(g_config.wait_vblank?GL_TRUE:GL_FALSE);
  glViewport(0,0,g_config.width,g_config.height);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GEQUAL); glClearDepth(0.0f);
#endif
  gfx::RendererConfig rc{}; rc.width=c.width; rc.height=c.height; rc.textureBudget=c.texture_cache_budget;
  g_renderer=std::make_unique<gfx::Renderer>(rc);
  if(!g_renderer->initialize()) {
    g_initFailure=InitFailure::RendererInitFailed;
    std::snprintf(g_initFailureDetail,sizeof(g_initFailureDetail),"Renderer::initialize failed");
    g_renderer.reset();
    return false;
  }
  g_telemetry.reset(); g_coverage.reset(); g_trace=std::make_unique<integration::FrameTrace>(c.trace_capacity);
  gxbridge::DrawSinkConfig dc{};
  dc.streaming.vertexBytes=c.stream_vertex_bytes; dc.streaming.indexBytes=c.stream_index_bytes; dc.streaming.slots=c.stream_slots;
  dc.telemetry=c.diagnostics ? &g_telemetry : nullptr; dc.coverage=c.diagnostics ? &g_coverage : nullptr; dc.trace=c.diagnostics ? g_trace.get() : nullptr; dc.strictUnsupported=c.strict_unsupported;
  g_drawSink=std::make_unique<gxbridge::DrawSink>();
  if(!g_drawSink->initialize(*g_renderer,dc)){
    g_initFailure=InitFailure::DrawSinkInitFailed;
    std::snprintf(g_initFailureDetail,sizeof(g_initFailureDetail),"DrawSink::initialize failed");
    g_renderer->shutdown(); g_renderer.reset(); g_drawSink.reset();
    return false;
  }
  if (c.telemetry_log_path) ensure_parent_dir(c.telemetry_log_path);
  if (c.coverage_log_path) ensure_parent_dir(c.coverage_log_path);
  if (c.trace_log_path) ensure_parent_dir(c.trace_log_path);
  g_initialized=true; g_frame=0; g_last=0;
  return true;
}

InitFailure last_init_failure() noexcept{return g_initFailure;}
const char* last_init_failure_detail() noexcept{return g_initFailureDetail;}

bool begin_frame() noexcept {
  if(!g_initialized) return false;
  g_start=now_us();
  if (g_config.diagnostics) g_telemetry.begin_frame(g_frame);
  g_renderer->begin_frame(); g_drawSink->begin_frame(g_frame);
  return true;
}

void end_frame() noexcept {
  if(!g_initialized) return;
  g_drawSink->flush(); g_renderer->end_frame();
  const uint64_t presentStart = now_us();
#if defined(__vita__)
  vglSwapBuffers(GL_FALSE);
#endif
  const uint64_t end = now_us();
  g_last=end-g_start;
  if (g_config.diagnostics) {
    g_telemetry.add_time(gfx::TelemetryPhase::Present,end-presentStart);
    g_telemetry.end_frame(g_last);
  }
  ++g_frame;
  emit_periodic_diagnostics();
}

void shutdown() noexcept {
  if(!g_initialized) return;
  if (g_config.diagnostics && g_config.coverage_log_path) g_coverage.write_report(g_config.coverage_log_path);
  if (g_config.diagnostics && g_config.trace_log_path && g_trace) g_trace->write_report(g_config.trace_log_path, 2048);
  if(g_drawSink){g_drawSink->shutdown();g_drawSink.reset();}
  if(g_renderer){g_renderer->shutdown();g_renderer.reset();}
  g_trace.reset();
  g_initialized=false;
}

uint64_t frame_index() noexcept{return g_frame;}
uint64_t last_frame_time_us() noexcept{return g_last;}
uint32_t width() noexcept{return g_config.width;}
uint32_t height() noexcept{return g_config.height;}
gfx::Renderer& renderer() noexcept{return *g_renderer;}
gxbridge::DrawSink& draw_sink() noexcept{return *g_drawSink;}
gfx::Telemetry& telemetry() noexcept{return g_telemetry;}
integration::FeatureCoverage& feature_coverage() noexcept{return g_coverage;}
integration::FrameTrace& frame_trace() noexcept{return *g_trace;}
gfx::MemoryBudgetSnapshot memory_budget() noexcept{return g_drawSink ? g_drawSink->memory_budget() : gfx::MemoryBudgetSnapshot{};}
size_t invalidate_texture_source_range(uint64_t start,size_t bytes) noexcept{return g_renderer ? g_renderer->invalidate_texture_source_range(start,bytes) : 0;}

} // namespace aurora::vita
