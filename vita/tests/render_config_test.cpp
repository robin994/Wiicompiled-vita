#include "render_config.hpp"
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
    aurora::vita::BackendConfig config;
    config.static_geometry_budget=8*1024*1024;
    config.gxm_lit_fixed_vertex_gpu=true;
    config.gxm_scenes_per_frame=64;
    config.efb_width=1024;config.efb_height=640;
    config.gxm_seal_shader_cache_after_prewarm=true;
    VitaRenderOptions options;
    wiicompiled::backend::apply_render_options(config,options);
    CHECK(config.gxm_preload_program_cache && config.pipeline_prewarm_limit==192);
    CHECK(config.static_geometry_budget==8*1024*1024 && config.gxm_lit_fixed_vertex_gpu);
    CHECK(!config.gxm_seal_shader_cache_after_prewarm && config.efb_width==1024 && config.efb_height==640);
    CHECK(config.gxm_scenes_per_frame==64 && !config.prepared_display_lists && !config.gxm_local_draw_batching);
    options.geometryCache=false;options.gpuLighting=false;options.nativeMaterials=false;
    options.workerThreads=0;options.rendererLanes=1;options.pipelinePrewarmLimit=0;
    options.preparedDisplayLists=true;options.immediateDrawView=true;
    options.drawBatching=true;options.geometryMinVertices=3;
    options.fragmentPrepareCache=true;options.fixedUniformPool=true;options.geometryPreflight=true;
    wiicompiled::backend::apply_render_options(config,options);
    CHECK(!config.static_geometry_budget && !config.gxm_lit_fixed_vertex_gpu);
    CHECK(config.gxm_disable_mask&aurora::vita::gfx::GxmDisableNativeMaterial);
    CHECK(config.prepared_display_lists && config.gxm_local_draw_batching && config.gxm_immediate_draw_view);
    CHECK(config.gxm_fragment_prepare_cache && config.gxm_fixed_uniform_pool && config.gxm_geometry_preflight);
    CHECK(config.cpu_worker_threads==0 && config.cpu_renderer_execution_lanes==1 && config.static_geometry_min_vertices==3);
    CHECK(config.pipeline_prewarm_limit==0 && !config.gxm_seal_shader_cache_after_prewarm);
}
