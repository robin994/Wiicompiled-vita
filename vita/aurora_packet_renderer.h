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
    AuroraPacketTexture texture{};
};

struct AuroraPacketSubmitResult {
    bool submitted = false;
    bool textureDrawn = false;
    bool textureHit = false;
    bool textureMiss = false;
    bool textureUploaded = false;
    bool textureUploadFailed = false;
    bool textureUnsupported = false;
    std::uint64_t textureBytesUploaded = 0;
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
};

bool AuroraPacketRendererInitialize() noexcept;
void AuroraPacketRendererShutdown() noexcept;
bool AuroraPacketRendererBeginFrame(std::uint64_t serial,
                                    float viewportX, float viewportY,
                                    float viewportWidth, float viewportHeight,
                                    std::int32_t scissorX, std::int32_t scissorY,
                                    std::int32_t scissorWidth, std::int32_t scissorHeight) noexcept;
AuroraPacketSubmitResult AuroraPacketRendererSubmit(const AuroraPacketDraw& draw) noexcept;
AuroraPacketFrameStats AuroraPacketRendererEndFrame() noexcept;

} // namespace WiiCompiledVita
