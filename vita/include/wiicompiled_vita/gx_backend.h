#pragma once

#include <array>
#include <cstdint>

namespace WiiCompiledVita::GxBackend {

struct Stats {
    uint64_t framesSubmitted = 0;
    uint64_t framesCompleted = 0;
    uint64_t framesPresented = 0;
    uint64_t drawCalls = 0;
    uint64_t vertices = 0;
    uint64_t displayListBytes = 0;
    uint64_t displayListsReplayed = 0;
    uint64_t rawDrawBytes = 0;
    uint64_t geometryDrawsPresented = 0;
    uint64_t geometryVerticesPresented = 0;
    uint64_t geometryVerticesDropped = 0;
    uint64_t geometryVerticesTransformed = 0;
    uint64_t geometryPnMatrixVertices = 0;
    uint64_t geometryTransformFailures = 0;
    uint64_t rawDrawsDecoded = 0;
    uint64_t rawDrawDecodeFailures = 0;
    uint64_t rawDirectAttributesDecoded = 0;
    uint64_t rawIndexedAttributesDecoded = 0;
    uint64_t xfPacketsApplied = 0;
    uint64_t xfWordsApplied = 0;
    uint64_t xfPositionMatrixWords = 0;
    uint64_t xfNormalMatrixWords = 0;
    uint64_t xfProjectionWrites = 0;
    uint64_t xfViewportWrites = 0;
    uint64_t xfMatrixIndexWrites = 0;
    uint64_t xfUnsupportedWords = 0;
    uint64_t xfIndexedLoads = 0;
    uint64_t xfIndexedWords = 0;
    uint64_t geometryDepthCompareDraws = 0;
    uint64_t geometryDepthWriteDraws = 0;
    uint64_t geometryCullNoneDraws = 0;
    uint64_t geometryCullFrontDraws = 0;
    uint64_t geometryCullBackDraws = 0;
    uint64_t geometryCullAllSkipped = 0;
    uint64_t geometryBlendDraws = 0;
    uint64_t geometryBlendFallbackDraws = 0;
    uint64_t geometryAlphaTestDraws = 0;
    uint64_t geometryAlphaCompareFallbackDraws = 0;
    uint64_t geometryTevSimpleDraws = 0;
    uint64_t geometryTevFallbackDraws = 0;
    uint64_t textureDrawsPresented = 0;
    uint64_t textureCacheHits = 0;
    uint64_t textureCacheMisses = 0;
    uint64_t textureUploads = 0;
    uint64_t textureUploadFailures = 0;
    uint64_t textureUnsupportedDraws = 0;
    uint64_t textureSourceRaceDraws = 0;
    uint64_t textureMipFallbackDraws = 0;
    uint64_t textureBytesUploaded = 0;
    uint64_t textureRgb565Uploads = 0;
    uint64_t textureRgb5a3Uploads = 0;
    uint64_t textureRgba8Uploads = 0;
    uint64_t textureRgba8PcUploads = 0;
    uint64_t textureI4Uploads = 0;
    uint64_t textureI8Uploads = 0;
    uint64_t textureIa4Uploads = 0;
    uint64_t textureIa8Uploads = 0;
    uint64_t textureCmprUploads = 0;
    std::array<uint64_t, 7> primitiveDraws{};
    std::array<uint64_t, 8> vertexFormatDraws{};
    int32_t renderAffinityMask = 0;
    int32_t renderStackFree = 0;
    bool gpuInitialized = false;
    bool resolutionFallback = false;
};

bool Initialize() noexcept;
void Shutdown() noexcept;
Stats SnapshotStats() noexcept;
bool ApplyXfPacket(const uint8_t* packet, uint32_t packetBytes) noexcept;
bool ApplyIndexedXfPacket(uint32_t value, const uint8_t* source,
                          uint32_t sourceBytes) noexcept;

// M12 trace: guest return address of the current GXBegin, threaded into GeometryDraw.
void SetGuestBeginLr(uint32_t lr) noexcept;

} // namespace WiiCompiledVita::GxBackend
