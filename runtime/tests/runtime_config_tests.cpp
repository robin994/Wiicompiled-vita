#include "runtime_config.h"
#include "gx_copy_async_policy.hpp"

#include <iostream>
#include <sstream>

namespace {
bool Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}
} // namespace

int main() {
    for (const auto& [text, expected] : std::array{
             std::pair{"[vita]\nshader_cache_deferred_writes = true\n", true},
             std::pair{"[vita]\nshader_cache_deferred_writes = false\n", false},
             std::pair{"[vita]\nshader_cache_deferred_writes = 1\n", true},
             std::pair{"[video]\nshader_cache_deferred_writes = false\n", true}}) {
        std::istringstream input(text);
        if (!Require(RuntimeConfigFile::ParseConfig(input).vitaRender.deferShaderDiskWrites == expected,
                     "shader cache write deferral must be a typed Vita boolean")) return 1;
    }
    static_assert(wiicompiled::gx::copy_tex_must_wait(false,true,false));
    static_assert(wiicompiled::gx::copy_tex_must_wait(true,false,false));
    static_assert(wiicompiled::gx::copy_tex_must_wait(true,true,true));
    static_assert(!wiicompiled::gx::copy_tex_must_wait(true,true,false));
    for (const auto& [text, expected] : std::array{
             std::pair{"[vita]\nnative_lyt_packets = true\n", true},
             std::pair{"[vita]\nnative_lyt_packets = false\n", false},
             std::pair{"[vita]\nnative_lyt_packets = 1\n", false},
             std::pair{"[video]\nnative_lyt_packets = true\n", false}}) {
        std::istringstream input(text);
        if(!Require(RuntimeConfigFile::ParseConfig(input).vitaRender.nativeLytPackets==expected,
                    "native NW4r UI packet setting must be an opt-in Vita boolean"))return 1;
    }
    for (const auto& [text, expected] : std::array{
             std::pair{"[vita]\nasync_efb_copies = true\n", true},
             std::pair{"[vita]\nasync_efb_copies = false\n", false},
             std::pair{"[vita]\nasync_efb_copies = \"true\"\n", false},
             std::pair{"[video]\nasync_efb_copies = true\n", false}}) {
        std::istringstream input(text);
        if(!Require(RuntimeConfigFile::ParseConfig(input).vitaRender.asyncEfbCopies==expected,
                    "async EFB copies must be an opt-in Vita boolean"))return 1;
    }
    for (const auto& [text, expected] : std::array{
             std::pair{"[vita]\ndeferred_efb_targets = true\n", true},
             std::pair{"[vita]\ndeferred_efb_targets = false\n", false},
             std::pair{"[vita]\ndeferred_efb_targets = 1\n", false},
             std::pair{"[video]\ndeferred_efb_targets = true\n", false}}) {
        std::istringstream input(text);
        if(!Require(RuntimeConfigFile::ParseConfig(input).vitaRender.deferredEfbTargets==expected,
                    "deferred EFB targets must be an opt-in Vita boolean"))return 1;
    }
    for (const auto& [text, expected] : std::array{
             std::pair{"[vita]\nefb_copy_snap_1to1 = true\n", true},
             std::pair{"[vita]\nefb_copy_snap_1to1 = false\n", false},
             std::pair{"[vita]\nefb_copy_snap_1to1 = 1\n", false},
             std::pair{"[video]\nefb_copy_snap_1to1 = true\n", false}}) {
        std::istringstream input(text);
        if(!Require(RuntimeConfigFile::ParseConfig(input).vitaRender.efbCopySnap1to1==expected,
                    "EFB snap must be an opt-in Vita boolean"))return 1;
    }
    for (const auto& [text, expected] : std::array{
             std::pair{"[vita]\nprecomputed_geometry = true\n", true},
             std::pair{"[vita]\nprecomputed_geometry = false\n", false},
             std::pair{"[vita]\nprecomputed_geometry = 1\n", false},
             std::pair{"[vita]\nprecomputed_geometry = \"true\"\n", false},
             std::pair{"[video]\nprecomputed_geometry = true\n", false}}) {
        std::istringstream input(text);
        if (!Require(RuntimeConfigFile::ParseConfig(input).vitaRender.precomputedGeometry == expected,
                     "precomputed geometry must be an opt-in Vita boolean")) return 1;
    }
    for (const auto& [text, expected] : std::array{
             std::pair{"[vita]\nnative_primitive_min_vertices = 0\n", uint32_t{0}},
             std::pair{"[vita]\nnative_primitive_min_vertices = 3\n", uint32_t{3}},
             std::pair{"[vita]\nnative_primitive_min_vertices = 128\n", uint32_t{128}},
             std::pair{"[vita]\nnative_primitive_min_vertices = 64000\n", uint32_t{64000}},
             std::pair{"[vita]\nnative_primitive_min_vertices = 1\n", uint32_t{0}},
             std::pair{"[vita]\nnative_primitive_min_vertices = 64001\n", uint32_t{0}},
             std::pair{"[video]\nnative_primitive_min_vertices = 128\n", uint32_t{0}},
             std::pair{"[vita]\nnative_primitive_min_vertices = \"128\"\n", uint32_t{0}}}) {
        std::istringstream input(text);
        if (!Require(RuntimeConfigFile::ParseConfig(input).vitaRender.nativePrimitiveMinVertices == expected,
                     "native primitive threshold must be opt-in and within GXM U16 limits")) return 1;
    }
    for (const auto& [text, expected] : std::array{
             std::pair{"[vita]\nnative_asset_prewarm_mb = 0\n", uint32_t{0}},
             std::pair{"[vita]\nnative_asset_prewarm_mb = 1\n", uint32_t{1}},
             std::pair{"[vita]\nnative_asset_prewarm_mb = 8\n", uint32_t{8}},
             std::pair{"[vita]\nnative_asset_prewarm_mb = 9\n", uint32_t{0}},
             std::pair{"[vita]\nnative_asset_prewarm_mb = -1\n", uint32_t{0}},
             std::pair{"[vita]\nnative_asset_prewarm_mb = \"8\"\n", uint32_t{0}},
             std::pair{"[video]\nnative_asset_prewarm_mb = 8\n", uint32_t{0}}}) {
        std::istringstream input(text);
        if(!Require(RuntimeConfigFile::ParseConfig(input).vitaRender.nativeAssetPrewarmMb==expected,
                    "native asset prewarm must be bounded, opt-in and Vita-only"))return 1;
    }
    {
        std::istringstream input("[vita]\nshader_binary_preload = false\npipeline_prewarm_limit = 512\nworker_threads = 0\nrenderer_lanes = 1\ngeometry_min_vertices = 3\nprepared_display_lists = true\ndraw_batching = true\nrender_budget_probe = true\nimmediate_draw_view = true\ngpu_lighting = false\ngeometry_cache = false\nnative_materials = false\nnative_asset_cache_mb = 8\n");
        const auto render=RuntimeConfigFile::ParseConfig(input).vitaRender;
        if(!Require(!render.shaderBinaryPreload && render.pipelinePrewarmLimit==512 &&
                    render.workerThreads==0 && render.rendererLanes==1 && render.geometryMinVertices==3 &&
                    render.preparedDisplayLists && render.drawBatching && render.renderBudgetProbe && render.immediateDrawView &&
                    render.gpuLighting==std::optional<bool>{false} && render.geometryCache==std::optional<bool>{false} &&
                    !render.nativeMaterials && render.nativeAssetCacheMb==8,
                    "Vita renderer/cache overrides were not loaded"))return 1;
    }
    {
        std::istringstream input("[vita]\nshader_binary_preload = \"yes\"\npipeline_prewarm_limit = 513\nworker_threads = 3\nrenderer_lanes = -1\ngeometry_min_vertices = 2\nprepared_display_lists = 1\nrender_budget_probe = 1\ngeometry_cache = \"true\"\nshader_preload_limit = 1025\nnative_asset_cache_mb = 33\n");
        const auto render=RuntimeConfigFile::ParseConfig(input).vitaRender;
        if(!Require(render.shaderBinaryPreload && render.pipelinePrewarmLimit==192 &&
                    render.workerThreads==2 && render.rendererLanes==0 && render.geometryMinVertices==48 &&
                    !render.preparedDisplayLists && !render.renderBudgetProbe && !render.geometryCache &&
                    render.shaderPreloadLimit==1024 && render.nativeAssetCacheMb==2,
                    "invalid Vita renderer options must retain bounded defaults"))return 1;
    }
    {
        std::istringstream input("[diagnostics]\nenabled = false\ncapture_frame = 300\n");
        const auto config=RuntimeConfigFile::ParseConfig(input);
        if(!Require(config.diagnosticsEnabled==std::optional<bool>{false} &&
                    config.diagnosticsCaptureFrame==std::optional<uint32_t>{300},
                    "diagnostic comparison settings were not loaded"))return 1;
    }
    for(const auto* text : {"[diagnostics]\nenabled = \"false\"\ncapture_frame = -1\n",
                           "[video]\nenabled = false\ncapture_frame = 300\n"}) {
        std::istringstream input(text);
        const auto config=RuntimeConfigFile::ParseConfig(input);
        if(!Require(!config.diagnosticsEnabled && !config.diagnosticsCaptureFrame,
                    "invalid diagnostic settings must retain the existing defaults"))return 1;
    }
    {
        std::istringstream input(
            "[diagnostics]\nenabled = true\ncapture_frame = 300\n"
            "draw_suppress_first = 694\ndraw_suppress_last = 728\n");
        const auto config=RuntimeConfigFile::ParseConfig(input);
        if(!Require(config.diagnosticsDrawSuppressFirst==std::optional<uint32_t>{694} &&
                    config.diagnosticsDrawSuppressLast==std::optional<uint32_t>{728},
                    "diagnostic GXM draw suppression range was not loaded"))return 1;
    }
    for (const auto& [text, expected] : std::array{
             std::pair{"[test]\ntime_trial = true\n", std::optional<bool>{true}},
             std::pair{"[test]\ntime_trial = false\n", std::optional<bool>{false}},
             std::pair{"[test]\ntime_trial = \"yes\"\n", std::optional<bool>{}},
             std::pair{"[video]\ntime_trial = true\n", std::optional<bool>{}},
             std::pair{"[paths]\ndvd_iso = \"mariokart.iso\"\n", std::optional<bool>{}}}) {
        std::istringstream input(text);
        if (!Require(RuntimeConfigFile::ParseConfig(input).testTimeTrial == expected,
                     "Time Trial config flag must be an opt-in boolean in [test]")) return 1;
    }
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = true\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "enabled.toml");
        if (!Require(config.metalFxSpatialUpscaling == std::optional<bool>{true},
                     "valid MetalFX setting was not loaded")) return 1;
    }
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = false\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "disabled.toml");
        if (!Require(config.metalFxSpatialUpscaling == std::optional<bool>{false},
                     "false MetalFX setting was not loaded")) return 1;
    }
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = \"yes\"\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "invalid.toml");
        if (!Require(!config.metalFxSpatialUpscaling, "invalid MetalFX setting was accepted")) return 1;
    }
    {
        std::istringstream input("[audio]\nlegacy_stop_dma_on_queue_full = true\n");
        const auto config=RuntimeConfigFile::ParseConfig(input,"legacy-audio.toml");
        if (!Require(config.audioLegacyStopDmaOnQueueFull == std::optional<bool>{true},
                     "legacy audio queue-full policy was not loaded")) return 1;
    }
    {
        std::istringstream input("[audio]\nlegacy_stop_dma_on_queue_full = false\n");
        const auto config=RuntimeConfigFile::ParseConfig(input,"current-audio.toml");
        if (!Require(config.audioLegacyStopDmaOnQueueFull == std::optional<bool>{false},
                     "current audio queue-full policy was not loaded")) return 1;
    }
    {
        std::istringstream input("[audio]\nlegacy_stop_dma_on_queue_full = \"yes\"\n");
        const auto config=RuntimeConfigFile::ParseConfig(input,"invalid-audio.toml");
        if (!Require(!config.audioLegacyStopDmaOnQueueFull,
                     "invalid audio queue-full policy was accepted")) return 1;
    }
    return 0;
}
