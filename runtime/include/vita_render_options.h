#pragma once
#include <cstdint>
#include <optional>

// Port policy, independent of renderer internals. Existing cache/GPU-lighting
// choices are retained when optional overrides are absent.
struct VitaRenderOptions {
    bool shaderBinaryPreload = true;
    uint32_t shaderPreloadLimit = 1024;
    uint32_t pipelinePrewarmLimit = 192;
    uint32_t nativeAssetCacheMb = 2;
    uint32_t workerThreads = 2;
    uint32_t rendererLanes = 0;
    uint32_t geometryMinVertices = 48;
    bool preparedDisplayLists = false;
    bool drawBatching = false;
    // One sampled frame out of 32: native GXM/FIFO/EFB timing without
    // enabling continuous draw tracing or guest-call diagnostics.
    bool renderBudgetProbe = false;
    bool immediateDrawView = false;
    bool fragmentPrepareCache = false;
    bool fixedUniformPool = false;
    bool geometryPreflight = false;
    bool distinctCpuDispatch = false;
    bool nativeMaterials = true;
    std::optional<bool> geometryCache;
    std::optional<bool> gpuLighting;
};
