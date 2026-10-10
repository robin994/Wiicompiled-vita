#pragma once
#include "vita_render_options.h"
#include "aurora_vita_backend.hpp"

namespace wiicompiled::backend {
inline void apply_render_options(aurora::vita::BackendConfig& config,
                                 const VitaRenderOptions& options) {
    config.gxm_preload_program_cache=options.shaderBinaryPreload;
    config.gxm_program_cache_preload_limit=options.shaderPreloadLimit;
    config.pipeline_prewarm_limit=options.pipelinePrewarmLimit;
    config.prepared_display_lists=options.preparedDisplayLists;
    config.gxm_local_draw_batching=options.drawBatching;
    config.gxm_immediate_draw_view=options.immediateDrawView;
    config.gxm_fragment_prepare_cache=options.fragmentPrepareCache;
    config.gxm_fixed_uniform_pool=options.fixedUniformPool;
    config.gxm_geometry_preflight=options.geometryPreflight;
    config.static_geometry_min_vertices=options.geometryMinVertices;
    config.cpu_worker_threads=options.workerThreads;
    config.cpu_renderer_execution_lanes=options.rendererLanes;
    config.cpu_distinct_core_dispatch=options.distinctCpuDispatch;
    if(options.geometryCache) {
        if(!*options.geometryCache) config.static_geometry_budget=0;
        else if(!config.static_geometry_budget) config.static_geometry_budget=8*1024*1024;
    }
    if(options.gpuLighting) config.gxm_lit_fixed_vertex_gpu=*options.gpuLighting;
    config.gxm_disable_mask&=~uint32_t(aurora::vita::gfx::GxmDisableNativeMaterial);
    if(!options.nativeMaterials) config.gxm_disable_mask|=aurora::vita::gfx::GxmDisableNativeMaterial;
    // Unknown pipelines keep the runtime compiler/fallback. Sealing a warm
    // cache before every race/UI variant was trained would hide valid draws.
    config.gxm_seal_shader_cache_after_prewarm=false;
}
}
