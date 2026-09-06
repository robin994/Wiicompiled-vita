#pragma once

#include <cstddef>
#include <cstdint>

namespace WiiCompiledVita {

struct AuroraPacketVertex {
    float x;
    float y;
    float z;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
    float s;
    float t;
#if MKW_VITA_CLIP_W
    float clipW = 1.0f;
#endif
};

struct AuroraPacketTexture {
    const void* data = nullptr;
    std::size_t dataBytes = 0;
    std::uint64_t sourceId = 0;
    std::uint64_t sourceGeneration = 0;
    std::uint32_t revision = 0;
    std::uint32_t globalEpoch = 0;
    std::uint32_t format = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t wrapS = 0;
    std::uint8_t wrapT = 0;
    std::uint8_t minFilter = 0;
    std::uint8_t magFilter = 0;
    std::uint8_t tevMode = 0;
    bool enabled = false;
    const void* thpUData = nullptr;
    const void* thpVData = nullptr;
    std::size_t thpUBytes = 0;
    std::size_t thpVBytes = 0;
    std::uint64_t thpUGeneration = 0;
    std::uint64_t thpVGeneration = 0;
    std::uint32_t thpURevision = 0;
    std::uint32_t thpVRevision = 0;
    std::uint16_t thpChromaWidth = 0;
    std::uint16_t thpChromaHeight = 0;
    bool thpYuv420 = false;
};

struct AuroraPacketDraw {
    const AuroraPacketVertex* vertices = nullptr;
    std::uint32_t vertexCount = 0;
    std::uint32_t primitive = 0;
    std::uint32_t depthFunc = 0;
    std::uint32_t cullMode = 0;
    std::uint32_t blendMode = 0;
    std::uint32_t blendSrc = 0;
    std::uint32_t blendDst = 0;
    std::uint32_t logicOp = 0;
    std::uint32_t alphaComp0 = 0;
    std::uint32_t alphaComp1 = 0;
    std::uint32_t alphaOp = 0;
    std::uint8_t alphaRef0 = 0;
    std::uint8_t alphaRef1 = 0;
    bool depthCompare = true;
    bool depthUpdate = true;
    bool colorUpdate = true;
    bool alphaUpdate = true;
    bool perspective = false;
    // Dense producer-assigned ID for one adjacent render-state run. Zero keeps
    // compatibility with callers that do not provide compact frame-state IDs.
    std::uint16_t renderStateId = 0;
    float viewportX = 0.0f;
    float viewportY = 0.0f;
    float viewportWidth = 960.0f;
    float viewportHeight = 544.0f;
    float viewportNear = 0.0f;
    float viewportFar = 1.0f;
    std::int32_t scissorX = 0;
    std::int32_t scissorY = 0;
    std::int32_t scissorWidth = 960;
    std::int32_t scissorHeight = 544;
    AuroraPacketTexture texture{};
};

struct AuroraPacketSubmitResult {
    bool submitted = false;
    bool textureDrawn = false;
    bool textureEfb = false;
    bool textureHit = false;
    bool textureMiss = false;
    bool textureUploaded = false;
    bool textureUploadFailed = false;
    bool textureUnsupported = false;
    std::uint64_t textureBytesUploaded = 0;
    // gfx::PrepareDrawError when submitted == false and the draw reached enqueue:
    // 0 None, 1 InvalidInput, 2 VertexDecodeFailed, 3 VertexTransformFailed,
    // 4 TooManyVertices, 5 UnsupportedLineExpansion, 6 StreamingOverflow, 7 PipelineFailed.
    // 255 = never reached enqueue (frame inactive / no vertices / bad index build).
    std::uint8_t prepareError = 0;
};

struct AuroraPacketEfbCopy {
    std::uint64_t destinationId = 0;
    std::int32_t srcX = 0;
    std::int32_t srcY = 0;
    std::int32_t srcWidth = 0;
    std::int32_t srcHeight = 0;
    std::uint32_t dstWidth = 0;
    std::uint32_t dstHeight = 0;
    std::uint32_t format = 0;
    float clearR = 0.0f;
    float clearG = 0.0f;
    float clearB = 0.0f;
    float clearA = 1.0f;
    float clearDepthValue = 1.0f;
    bool clear = false;
    bool clearColor = true;
    bool clearAlpha = true;
    bool clearDepth = true;
};

struct AuroraPacketFrameStats {
    std::uint32_t physicalDrawCalls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t pipelineHits = 0;
    std::uint32_t pipelineMisses = 0;
    std::uint32_t stateChanges = 0;
    std::uint64_t vertexBytes = 0;
    std::uint64_t indexBytes = 0;
    std::uint64_t vertexOverflows = 0;
    std::uint64_t indexOverflows = 0;
    std::uint64_t textureBytes = 0;
    std::uint64_t textureHighWaterBytes = 0;
    std::uint64_t textureBudgetBytes = 0;
    std::uint64_t textureEvictions = 0;
    std::uint32_t textureEntries = 0;
    // M12.1 texture OOM hardening
    std::uint64_t textureAllocFailTotal = 0;
    std::uint64_t texturePreEvictions = 0;
    std::uint64_t texturePreEvictedBytes = 0;
    std::uint64_t textureRequestedBytes = 0;
    std::uint64_t textureEvictBlocked = 0;
    std::uint64_t textureProtectedBytes = 0;
    std::uint64_t textureProtectedHighWaterBytes = 0;
    std::uint32_t textureAllocRetry = 0;
    std::uint32_t textureAllocRetrySuccess = 0;
    std::uint32_t textureAllocFailAfterEvict = 0;
    std::uint64_t textureAllocRetryWaitUs = 0;
    // M12.4 EFB budget telemetry. Avoid vitaGL free-space queries here: the
    // M12.3 hardware core crashed inside sceClibMspaceMallocStats.
    std::uint64_t efbBytes = 0;
    std::uint64_t efbHighWaterBytes = 0;
    std::uint32_t efbEntries = 0;
    std::uint64_t efbAllocationBlocked = 0;
    std::uint64_t efbAllocationBlockedBytes = 0;
    std::uint64_t efbBudgetBytes = 0;
    std::uint32_t efbGpuCopies = 0;
    std::uint32_t efbReadbackCopies = 0;
    std::uint32_t efbTransferReadbacks = 0;
    std::uint32_t efbResidentScaled = 0;
    std::uint64_t efbResidentUs = 0;
    // P5.1 resident-copy classification. GpuResize stays zero until an exact
    // nearest persistent GPU scaler is implemented; native-res avoidance is
    // reported separately rather than mislabeled as resize.
    std::uint32_t efbGpuSameSize = 0;
    std::uint32_t efbGpuResize = 0;
    std::uint32_t efbCpuCopy = 0;
    std::uint32_t efbCpuResize = 0;
    std::uint32_t efbResidentFailures = 0;
    std::uint32_t efbNativeResCopies = 0;
    std::uint32_t efbNativeBudgetFallbacks = 0;
    std::uint32_t efbFallbackInvalidSource = 0;
    std::uint32_t efbFallbackUnsupportedSurface = 0;
    std::uint32_t efbFallbackExistingSize = 0;
    std::uint32_t efbFallbackAllocation = 0;
    std::uint32_t efbFallbackBacking = 0;
    std::uint32_t efbFallbackTransfer = 0;
    std::uint32_t efbFallbackResizeUnavailable = 0;
    std::uint32_t efbFallbackCpu = 0;
    std::uint64_t streamReuseWaitUs = 0;
    // P0/P1 low-overhead renderer breakdown. Times are accumulated over all
    // logical submissions in the frame and emitted only by the outer summary.
    std::uint32_t logicalSubmits = 0;
    std::uint32_t compactDraws = 0;
    std::uint32_t compactFallbacks = 0;
    std::uint32_t batchMerges = 0;
    std::uint32_t compactRunStarts = 0;
    std::uint32_t compactRunExtends = 0;
    std::uint32_t compactStateHits = 0;
    std::uint32_t compactStateMisses = 0;
    std::uint64_t indexBuildUs = 0;
    std::uint64_t vertexPackUs = 0;
    std::uint64_t textureResolveUs = 0;
    std::uint64_t pipelineResolveUs = 0;
    std::uint64_t streamWriteUs = 0;
    std::uint64_t flushExecuteUs = 0;
    std::uint64_t efbSyncUs = 0;
    std::uint64_t efbReadbackUs = 0;
    std::uint64_t efbScaleUs = 0;
    std::uint64_t efbUploadUs = 0;
};

bool AuroraPacketRendererInitialize() noexcept;
void AuroraPacketRendererShutdown() noexcept;
bool AuroraPacketRendererBeginFrame(std::uint64_t serial,
                                    float viewportX, float viewportY,
                                    float viewportWidth, float viewportHeight,
                                    std::int32_t scissorX, std::int32_t scissorY,
                                    std::int32_t scissorWidth, std::int32_t scissorHeight) noexcept;
AuroraPacketSubmitResult AuroraPacketRendererSubmit(const AuroraPacketDraw& draw) noexcept;
bool AuroraPacketRendererCopyEfb(const AuroraPacketEfbCopy& copy) noexcept;
void AuroraPacketRendererDestroyEfbCopy(std::uint64_t destinationId) noexcept;
AuroraPacketFrameStats AuroraPacketRendererEndFrame() noexcept;

} // namespace WiiCompiledVita
