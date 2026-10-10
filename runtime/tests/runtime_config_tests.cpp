#include "runtime_config.h"

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
