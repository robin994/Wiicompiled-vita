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
    CHECK(config.gxm_scenes_per_frame==64 && !config.prepared_display_lists && !config.native_lyt_packets && !config.gxm_local_draw_batching &&
          !config.gxm_render_budget_probe);
    CHECK(!config.gxm_precomputed_geometry);
    CHECK(config.gxm_native_primitive_min_vertices==0);
    CHECK(config.gxm_defer_program_cache_writes);
    CHECK(!config.gxm_deferred_efb_targets);
    CHECK(!config.gxm_efb_copy_snap_1to1);
    options.geometryCache=false;options.gpuLighting=false;options.nativeMaterials=false;
    options.workerThreads=0;options.rendererLanes=1;options.pipelinePrewarmLimit=0;
    options.preparedDisplayLists=true;options.nativeLytPackets=true;options.immediateDrawView=true;
    options.drawBatching=true;options.renderBudgetProbe=true;options.geometryMinVertices=3;
    options.fragmentPrepareCache=true;options.fixedUniformPool=true;options.geometryPreflight=true;
    options.precomputedGeometry=true;
    options.nativePrimitiveMinVertices=128;
    options.deferredEfbTargets=true;
    options.efbCopySnap1to1=true;
    options.deferShaderDiskWrites=false;
    wiicompiled::backend::apply_render_options(config,options);
    CHECK(!config.static_geometry_budget && !config.gxm_lit_fixed_vertex_gpu);
    CHECK(config.gxm_disable_mask&aurora::vita::gfx::GxmDisableNativeMaterial);
    CHECK(config.prepared_display_lists && config.native_lyt_packets &&
          config.gxm_local_draw_batching && config.gxm_immediate_draw_view);
    CHECK(config.gxm_render_budget_probe);
    CHECK(config.gxm_fragment_prepare_cache && config.gxm_fixed_uniform_pool && config.gxm_geometry_preflight);
    CHECK(config.gxm_precomputed_geometry);
    CHECK(config.gxm_native_primitive_min_vertices==128);
    CHECK(config.gxm_deferred_efb_targets);
    CHECK(config.gxm_efb_copy_snap_1to1);
    CHECK(!config.gxm_defer_program_cache_writes);
    CHECK(config.cpu_worker_threads==0 && config.cpu_renderer_execution_lanes==1 && config.static_geometry_min_vertices==3);
    CHECK(config.pipeline_prewarm_limit==0 && !config.gxm_seal_shader_cache_after_prewarm);
    CHECK(wiicompiled::backend::sanitize_render_features(config)==7u);
    CHECK(!config.native_lyt_packets&&config.gxm_native_primitive_min_vertices==0&&
          !config.gxm_deferred_efb_targets);
    CHECK(config.gxm_precomputed_geometry&&config.gxm_efb_copy_snap_1to1&&config.gxm_local_draw_batching);
}
