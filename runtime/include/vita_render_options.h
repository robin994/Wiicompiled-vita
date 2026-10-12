#pragma once
#include <cstdint>
#include <optional>

// Port policy, independent of renderer internals. Existing cache/GPU-lighting
// choices are retained when optional overrides are absent.
struct VitaRenderOptions {
    bool shaderBinaryPreload = true;
    // GXP binaries stay live in GXM immediately; only on-disk persistence
    // waits until a clean renderer shutdown. Avoids random writes mid-race.
    // WiiCompiled opt-in; Aurora's other titles keep their original policy.
    bool deferShaderDiskWrites = true;
    uint32_t shaderPreloadLimit = 1024;
    uint32_t pipelinePrewarmLimit = 192;
    uint32_t nativeAssetCacheMb = 2;
    // Bounded CPU-side prefetch from hot native PSARC sidecars. No GPU upload
    // or scene stall when disabled; clamped to nativeAssetCacheMb at runtime.
    uint32_t nativeAssetPrewarmMb = 0;
    uint32_t workerThreads = 2;
    uint32_t rendererLanes = 0;
    uint32_t geometryMinVertices = 48;
    bool preparedDisplayLists = false;
    bool nativeLytPackets = false;
    bool drawBatching = false;
    // Zero: existing triangle-list batching; >=3 enables native GXM strips/fans
    // at or above this vertex count. Experimental, never enabled by default.
    uint32_t nativePrimitiveMinVertices = 0;
    // One sampled frame out of 32: native GXM/FIFO/EFB timing without
    // enabling continuous draw tracing or guest-call diagnostics.
    bool renderBudgetProbe = false;
    bool immediateDrawView = false;
    bool fragmentPrepareCache = false;
    bool precomputedGeometry = false;
    // Experiments are separate: copy scheduling does not imply GPU retirement.
    bool asyncEfbCopies = false;
    bool deferredEfbTargets = false;
    // Opt-in: snap only 1:1 GXCopyTex raster-rounding edges by <=1 pixel.
    // Its sampling differs from the original scaled shader copy.
    bool efbCopySnap1to1 = false;
    bool fixedUniformPool = false;
    bool geometryPreflight = false;
    bool distinctCpuDispatch = false;
    bool nativeMaterials = true;
    std::optional<bool> geometryCache;
    std::optional<bool> gpuLighting;
};
