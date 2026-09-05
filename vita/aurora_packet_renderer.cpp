#include "aurora_packet_renderer.h"
#include "runtime_log.h"

#include "vita_draw_adapter.hpp"
#include "vita_pipeline_key.hpp"
#include "vita_renderer.hpp"
#include "vita_streaming_arena.hpp"
#include "vita_texture_decode.hpp"

#include <dolphin/gx/GXEnum.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#ifndef MKW_VITA_EFB_GPU_BLIT
#define MKW_VITA_EFB_GPU_BLIT 0
#endif
#ifndef MKW_VITA_PERF_LOG
#define MKW_VITA_PERF_LOG 1
#endif
#ifndef MKW_VITA_COMPACT_VERTEX
#define MKW_VITA_COMPACT_VERTEX 0
#endif
#ifndef MKW_VITA_FRAME_BATCHER
#define MKW_VITA_FRAME_BATCHER 0
#endif
#ifndef MKW_VITA_DIRECT_STREAM_WRITE
#define MKW_VITA_DIRECT_STREAM_WRITE 0
#endif
#ifndef MKW_VITA_PERF_INJECT_CLIP_TRIANGLE
#define MKW_VITA_PERF_INJECT_CLIP_TRIANGLE 0
#endif
#ifndef MKW_VITA_PERF_INJECT_WII_TRIANGLE
#define MKW_VITA_PERF_INJECT_WII_TRIANGLE 0
#endif
#ifndef MKW_VITA_EFB_READBACK_FLIP_Y
#define MKW_VITA_EFB_READBACK_FLIP_Y 1
#endif
#ifndef MKW_VITA_EFB_TRANSFER_READBACK
#define MKW_VITA_EFB_TRANSFER_READBACK 0
#endif
#ifndef MKW_VITA_EFB_NATIVE_RES_COPY
#define MKW_VITA_EFB_NATIVE_RES_COPY 0
#endif
#ifndef MKW_VITA_TEXTURE_SAFE_RETRY
#define MKW_VITA_TEXTURE_SAFE_RETRY 0
#endif

static_assert(sizeof(WiiCompiledVita::AuroraPacketVertex) == 24,
              "compact packet vertex must stay 24 bytes");

namespace WiiCompiledVita {
namespace {

namespace gfx = aurora::vita::gfx;

std::unique_ptr<gfx::Renderer> g_renderer;
std::unique_ptr<gfx::StreamingArena> g_arena;
gfx::CommandStream g_stream;
gfx::PreparedDraw g_prepared;
gfx::Viewport g_viewport{};
gfx::Scissor g_scissor{};
std::uint64_t g_lastPipelineHash = 0;
std::uint64_t g_lastPipelineKey = 0;
bool g_frameActive = false;

struct PacketPerfFrame {
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
    std::uint32_t textureSafeRetryAttempts = 0;
    std::uint32_t textureSafeRetrySuccesses = 0;
    std::uint32_t textureSafeRetryFailAfter = 0;
    std::uint64_t textureSafeRetryWaitUs = 0;
    std::uint64_t pipelineResolveUs = 0;
    std::uint64_t streamWriteUs = 0;
    std::uint64_t flushExecuteUs = 0;
    std::uint64_t efbSyncUs = 0;
    std::uint64_t efbReadbackUs = 0;
    std::uint64_t efbScaleUs = 0;
    std::uint64_t efbUploadUs = 0;
    std::uint32_t efbResidentScaled = 0;
    std::uint64_t efbResidentUs = 0;
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
};
PacketPerfFrame g_packetPerf{};

struct WiiTriangleProbeFrame {
    std::uint64_t serial = 0;
    std::uint32_t perspectiveDraws = 0;
    std::uint32_t perspectiveVertices = 0;
    std::uint32_t finiteVertices = 0;
    std::uint32_t triangles = 0;
    std::uint32_t finiteTriangles = 0;
    std::uint32_t nonDegenerate = 0;
    std::uint32_t fullyInside = 0;
    std::uint32_t intersectsViewport = 0;
    float minX = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    float minZ = std::numeric_limits<float>::infinity();
    float maxZ = -std::numeric_limits<float>::infinity();
    float maxAnyArea2 = 0.0f;
    float maxVisibleArea2 = 0.0f;
    float maxInsideArea2 = 0.0f;
    bool anyValid = false;
    bool visibleValid = false;
    bool insideValid = false;
    std::array<AuroraPacketVertex, 3> bestAny{};
    std::array<AuroraPacketVertex, 3> bestVisible{};
    std::array<AuroraPacketVertex, 3> bestInside{};
};
WiiTriangleProbeFrame g_wiiTriangleProbe{};

struct CompactResolvedState {
    bool valid = false;
    bool textured = false;
    bool textureEfb = false;
    std::uint64_t key = 0;
    std::uint64_t pipelineKey = 0;
    std::uint64_t pipelineHash = 0;
    std::array<gfx::TextureBinding, gfx::MaxTextures> bindings{};
    gfx::DrawUniforms uniforms{};
};
CompactResolvedState g_compactState{};

struct EfbCopyTexture {
    gfx::Handle handle = gfx::InvalidHandle;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t revision = 0;
};

std::unordered_map<std::uint64_t, EfbCopyTexture> g_efbCopies;
std::uint64_t g_efbSourceMissTraceCount = 0;
std::vector<std::uint8_t> g_efbReadback;
std::vector<std::uint8_t> g_efbUpload;
std::vector<std::uint16_t> g_efbXMap;
std::vector<std::uint16_t> g_efbYMap;
void* g_efbMappedReadback = nullptr;
std::size_t g_efbMappedReadbackBytes = 0;
std::uint32_t g_efbXMapSrc = 0;
std::uint32_t g_efbXMapDst = 0;
std::uint32_t g_efbYMapSrc = 0;
std::uint32_t g_efbYMapDst = 0;
std::uint64_t g_efbCopyTraceCount = 0;
std::uint32_t g_frameEfbGpuCopies = 0;
std::uint32_t g_frameEfbReadbackCopies = 0;
std::uint32_t g_frameEfbTransferReadbacks = 0;
// M12.5 keeps the M12.4 sampled-EFB budget based on our own accounting. M12.3
// proved that vglMemFree(VGL_MEM_VRAM) is itself unsafe under this workload:
// its sceClibMspaceMallocStats query crashed before an allocation was attempted.
constexpr std::size_t kEfbBudgetBytes = 4u * 1024u * 1024u;
std::uint64_t g_efbAllocationBlocked = 0;
std::uint64_t g_efbAllocationBlockedBytes = 0;
std::vector<std::uint8_t> g_thpRgba;
std::uint64_t g_thpConversionKey = 0;
std::uint64_t g_thpConversionCount = 0;

bool PrepareNearestMap(std::vector<std::uint16_t>& map, std::uint32_t src, std::uint32_t dst,
                       std::uint32_t& cachedSrc, std::uint32_t& cachedDst) noexcept {
    if (src == cachedSrc && dst == cachedDst && map.size() == dst) return true;
    try {
        map.resize(dst);
    } catch (...) {
        return false;
    }
    for (std::uint32_t i = 0; i < dst; ++i) {
        // EFB dimensions are capped at 2048, so the 32-bit product cannot overflow.
        map[i] = static_cast<std::uint16_t>((i * src) / dst);
    }
    cachedSrc = src;
    cachedDst = dst;
    return true;
}

bool FlushQueuedDraws() noexcept {
    if (!g_frameActive || !g_renderer || !g_arena) return false;
    if (g_stream.commands().empty()) return true;
    const uint64_t beginUs = sceKernelGetProcessTimeWide();
    if (!g_arena->flush()) return false;
    g_renderer->execute(g_stream);
    g_packetPerf.flushExecuteUs += sceKernelGetProcessTimeWide() - beginUs;
    g_stream.reset();
    g_stream.reserve(193);
    // capture/clear invalidates renderer bindings; force the next submit to
    // resolve its cached pipeline binding again while retaining the pipeline object.
    g_lastPipelineHash = 0;
    g_lastPipelineKey = 0;
    g_compactState.valid = false;
    return true;
}

gfx::Compare MapCompare(std::uint32_t value) noexcept {
    switch (static_cast<GXCompare>(value)) {
    case GX_NEVER: return gfx::Compare::Never;
    case GX_LESS: return gfx::Compare::Less;
    case GX_EQUAL: return gfx::Compare::Equal;
    case GX_LEQUAL: return gfx::Compare::LessEqual;
    case GX_GREATER: return gfx::Compare::Greater;
    case GX_NEQUAL: return gfx::Compare::NotEqual;
    case GX_GEQUAL: return gfx::Compare::GreaterEqual;
    case GX_ALWAYS: return gfx::Compare::Always;
    }
    return gfx::Compare::Always;
}

gfx::CullMode MapCull(std::uint32_t value) noexcept {
    switch (static_cast<GXCullMode>(value)) {
    case GX_CULL_FRONT: return gfx::CullMode::Front;
    case GX_CULL_BACK: return gfx::CullMode::Back;
    case GX_CULL_ALL: return gfx::CullMode::All;
    case GX_CULL_NONE:
    default: return gfx::CullMode::None;
    }
}

gfx::BlendMode MapBlend(std::uint32_t value) noexcept {
    switch (static_cast<GXBlendMode>(value)) {
    case GX_BM_BLEND: return gfx::BlendMode::Blend;
    case GX_BM_SUBTRACT: return gfx::BlendMode::Subtract;
    case GX_BM_LOGIC: return gfx::BlendMode::Logic;
    case GX_BM_NONE:
    default: return gfx::BlendMode::None;
    }
}

gfx::BlendFactor MapBlendFactor(std::uint32_t value, bool destination) noexcept {
    switch (static_cast<GXBlendFactor>(value)) {
    case GX_BL_ZERO: return gfx::BlendFactor::Zero;
    case GX_BL_ONE: return gfx::BlendFactor::One;
    case GX_BL_SRCCLR:
        return destination ? gfx::BlendFactor::SrcColor : gfx::BlendFactor::DstColor;
    case GX_BL_INVSRCCLR:
        return destination ? gfx::BlendFactor::OneMinusSrcColor
                           : gfx::BlendFactor::OneMinusDstColor;
    case GX_BL_SRCALPHA: return gfx::BlendFactor::SrcAlpha;
    case GX_BL_INVSRCALPHA: return gfx::BlendFactor::OneMinusSrcAlpha;
    case GX_BL_DSTALPHA: return gfx::BlendFactor::DstAlpha;
    case GX_BL_INVDSTALPHA: return gfx::BlendFactor::OneMinusDstAlpha;
    }
    return gfx::BlendFactor::One;
}

gfx::LogicOp MapLogic(std::uint32_t value) noexcept {
    const auto bounded = std::min<std::uint32_t>(value, static_cast<std::uint32_t>(GX_LO_SET));
    return static_cast<gfx::LogicOp>(bounded);
}

gfx::WrapMode MapWrap(std::uint8_t value) noexcept {
    switch (static_cast<GXTexWrapMode>(value)) {
    case GX_REPEAT: return gfx::WrapMode::Repeat;
    case GX_MIRROR: return gfx::WrapMode::Mirror;
    case GX_CLAMP:
    default: return gfx::WrapMode::Clamp;
    }
}

gfx::Filter MapFilter(std::uint8_t value) noexcept {
    switch (static_cast<GXTexFilter>(value)) {
    case GX_LINEAR: return gfx::Filter::Linear;
    case GX_NEAR_MIP_NEAR: return gfx::Filter::NearestMipmapNearest;
    case GX_LIN_MIP_NEAR: return gfx::Filter::LinearMipmapNearest;
    case GX_NEAR_MIP_LIN: return gfx::Filter::NearestMipmapLinear;
    case GX_LIN_MIP_LIN: return gfx::Filter::LinearMipmapLinear;
    case GX_NEAR:
    default: return gfx::Filter::Nearest;
    }
}

bool MapTextureFormat(std::uint32_t value, gfx::TextureFormat& output) noexcept {
    switch (value) {
    case GX_TF_I4: output = gfx::TextureFormat::I4; return true;
    case GX_TF_I8: output = gfx::TextureFormat::I8; return true;
    case GX_TF_IA4: output = gfx::TextureFormat::IA4; return true;
    case GX_TF_IA8: output = gfx::TextureFormat::IA8; return true;
    case GX_TF_RGB565: output = gfx::TextureFormat::RGB565; return true;
    case GX_TF_RGB5A3: output = gfx::TextureFormat::RGB5A3; return true;
    case GX_TF_RGBA8: output = gfx::TextureFormat::RGBA8; return true;
    case GX_TF_CMPR: output = gfx::TextureFormat::CMPR; return true;
#if defined(TARGET_PC)
    case GX_TF_RGBA8_PC: output = gfx::TextureFormat::RGBA8888; return true;
#endif
    default: return false;
    }
}

bool BuildIndices(std::vector<std::uint16_t>& output, std::uint32_t primitive,
                  std::uint32_t count, gfx::Primitive& converted) {
    const auto pushTriangle = [&output](std::uint32_t a, std::uint32_t b,
                                        std::uint32_t c) {
        if (a > std::numeric_limits<std::uint16_t>::max() ||
            b > std::numeric_limits<std::uint16_t>::max() ||
            c > std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
        output.push_back(static_cast<std::uint16_t>(a));
        output.push_back(static_cast<std::uint16_t>(b));
        output.push_back(static_cast<std::uint16_t>(c));
        return true;
    };

    converted = gfx::Primitive::Triangles;
    switch (static_cast<GXPrimitive>(primitive)) {
    case GX_QUADS:
        if ((count & 3u) != 0) return false;
        output.reserve((count / 4u) * 6u);
        for (std::uint32_t i = 0; i < count; i += 4) {
            if (!pushTriangle(i, i + 1, i + 2) || !pushTriangle(i + 2, i + 3, i)) return false;
        }
        return true;
    case GX_TRIANGLES:
        if ((count % 3u) != 0) return false;
        output.reserve(count);
        for (std::uint32_t i = 0; i < count; i += 3) {
            if (!pushTriangle(i, i + 1, i + 2)) return false;
        }
        return true;
    case GX_TRIANGLESTRIP:
        output.reserve(count > 2 ? (count - 2u) * 3u : 0u);
        for (std::uint32_t i = 2; i < count; ++i) {
            if ((i & 1u) != 0) {
                if (!pushTriangle(i - 1, i - 2, i)) return false;
            } else if (!pushTriangle(i - 2, i - 1, i)) {
                return false;
            }
        }
        return true;
    case GX_TRIANGLEFAN:
        output.reserve(count > 2 ? (count - 2u) * 3u : 0u);
        for (std::uint32_t i = 2; i < count; ++i) {
            if (!pushTriangle(0, i - 1, i)) return false;
        }
        return true;
    case GX_LINES: converted = gfx::Primitive::Lines; return true;
    case GX_LINESTRIP: converted = gfx::Primitive::LineStrip; return true;
    case GX_POINTS: converted = gfx::Primitive::Points; return true;
    }
    return false;
}

void AnalyzeWiiPerspectiveTriangles(const AuroraPacketDraw& draw) noexcept {
#if MKW_VITA_PERF_INJECT_WII_TRIANGLE
    if (!draw.perspective || !draw.vertices || draw.vertexCount < 3u) return;
    ++g_wiiTriangleProbe.perspectiveDraws;
    g_wiiTriangleProbe.perspectiveVertices += draw.vertexCount;
    for (std::uint32_t i = 0; i < draw.vertexCount; ++i) {
        const AuroraPacketVertex& vertex = draw.vertices[i];
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)) continue;
        ++g_wiiTriangleProbe.finiteVertices;
        g_wiiTriangleProbe.minX = std::min(g_wiiTriangleProbe.minX, vertex.x);
        g_wiiTriangleProbe.maxX = std::max(g_wiiTriangleProbe.maxX, vertex.x);
        g_wiiTriangleProbe.minY = std::min(g_wiiTriangleProbe.minY, vertex.y);
        g_wiiTriangleProbe.maxY = std::max(g_wiiTriangleProbe.maxY, vertex.y);
        g_wiiTriangleProbe.minZ = std::min(g_wiiTriangleProbe.minZ, vertex.z);
        g_wiiTriangleProbe.maxZ = std::max(g_wiiTriangleProbe.maxZ, vertex.z);
    }
    const auto visit = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
        if (ia >= draw.vertexCount || ib >= draw.vertexCount || ic >= draw.vertexCount) return;
        const AuroraPacketVertex& a = draw.vertices[ia];
        const AuroraPacketVertex& b = draw.vertices[ib];
        const AuroraPacketVertex& c = draw.vertices[ic];
        ++g_wiiTriangleProbe.triangles;
        if (!std::isfinite(a.x) || !std::isfinite(a.y) ||
            !std::isfinite(b.x) || !std::isfinite(b.y) ||
            !std::isfinite(c.x) || !std::isfinite(c.y)) return;
        ++g_wiiTriangleProbe.finiteTriangles;
        const float area2 = std::fabs((b.x - a.x) * (c.y - a.y) -
                                      (b.y - a.y) * (c.x - a.x));
        if (!std::isfinite(area2) || area2 <= 1.0e-7f) return;
        ++g_wiiTriangleProbe.nonDegenerate;
        if (area2 > g_wiiTriangleProbe.maxAnyArea2) {
            g_wiiTriangleProbe.maxAnyArea2 = area2;
            g_wiiTriangleProbe.anyValid = true;
            g_wiiTriangleProbe.bestAny = {a, b, c};
        }
        const auto inside = [](const AuroraPacketVertex& v) {
            return v.x >= -1.0f && v.x <= 1.0f && v.y >= -1.0f && v.y <= 1.0f;
        };
        const float triMinX = std::min({a.x, b.x, c.x});
        const float triMaxX = std::max({a.x, b.x, c.x});
        const float triMinY = std::min({a.y, b.y, c.y});
        const float triMaxY = std::max({a.y, b.y, c.y});
        const bool intersects = triMaxX >= -1.0f && triMinX <= 1.0f &&
                                triMaxY >= -1.0f && triMinY <= 1.0f;
        if (intersects) {
            ++g_wiiTriangleProbe.intersectsViewport;
            if (area2 > g_wiiTriangleProbe.maxVisibleArea2) {
                g_wiiTriangleProbe.maxVisibleArea2 = area2;
                g_wiiTriangleProbe.visibleValid = true;
                g_wiiTriangleProbe.bestVisible = {a, b, c};
            }
        }
        if (inside(a) && inside(b) && inside(c)) {
            ++g_wiiTriangleProbe.fullyInside;
            if (area2 > g_wiiTriangleProbe.maxInsideArea2) {
                g_wiiTriangleProbe.maxInsideArea2 = area2;
                g_wiiTriangleProbe.insideValid = true;
                g_wiiTriangleProbe.bestInside = {a, b, c};
            }
        }
    };

    switch (static_cast<GXPrimitive>(draw.primitive)) {
    case GX_QUADS:
        for (std::uint32_t i = 0; i + 3u < draw.vertexCount; i += 4u) {
            visit(i, i + 1u, i + 2u);
            visit(i + 2u, i + 3u, i);
        }
        break;
    case GX_TRIANGLES:
        for (std::uint32_t i = 0; i + 2u < draw.vertexCount; i += 3u) visit(i, i + 1u, i + 2u);
        break;
    case GX_TRIANGLESTRIP:
        for (std::uint32_t i = 2; i < draw.vertexCount; ++i) {
            if ((i & 1u) != 0u) visit(i - 1u, i - 2u, i);
            else visit(i - 2u, i - 1u, i);
        }
        break;
    case GX_TRIANGLEFAN:
        for (std::uint32_t i = 2; i < draw.vertexCount; ++i) visit(0u, i - 1u, i);
        break;
    default:
        break;
    }
#else
    (void)draw;
#endif
}

gfx::VertexLayout CompactVertexLayout() noexcept {
    gfx::VertexLayout layout{};
    layout.count = 3;
    layout.attributes[0] = {0, 3, gfx::VertexScalar::F32, false,
                            sizeof(AuroraPacketVertex), offsetof(AuroraPacketVertex, x)};
    layout.attributes[1] = {1, 4, gfx::VertexScalar::U8, true,
                            sizeof(AuroraPacketVertex), offsetof(AuroraPacketVertex, r)};
    layout.attributes[2] = {3, 2, gfx::VertexScalar::F32, false,
                            sizeof(AuroraPacketVertex), offsetof(AuroraPacketVertex, s)};
    return layout;
}

bool CompactIndexInfo(std::uint32_t primitive, std::uint32_t count,
                      gfx::Primitive& converted, std::uint32_t& indexCount) noexcept {
    converted = gfx::Primitive::Triangles;
    switch (static_cast<GXPrimitive>(primitive)) {
    case GX_QUADS:
        if ((count & 3u) != 0) return false;
        indexCount = (count / 4u) * 6u;
        return true;
    case GX_TRIANGLES:
        if ((count % 3u) != 0) return false;
        indexCount = count;
        return true;
    case GX_TRIANGLESTRIP:
    case GX_TRIANGLEFAN:
        indexCount = count > 2u ? (count - 2u) * 3u : 0u;
        return true;
    default:
        return false;
    }
}

std::uint64_t MixCompactState(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    return hash;
}

std::uint64_t CompactStateKey(const AuroraPacketDraw& draw) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    const auto mix = [&hash](std::uint64_t value) { hash = MixCompactState(hash, value); };
    mix(draw.primitive); mix(draw.depthFunc); mix(draw.cullMode); mix(draw.blendMode);
    mix(draw.blendSrc); mix(draw.blendDst); mix(draw.logicOp);
    mix(draw.alphaComp0); mix(draw.alphaComp1); mix(draw.alphaOp);
    mix(draw.alphaRef0); mix(draw.alphaRef1);
    mix(draw.depthCompare); mix(draw.depthUpdate); mix(draw.colorUpdate); mix(draw.alphaUpdate);
    std::uint32_t viewportWidthBits = 0;
    std::memcpy(&viewportWidthBits, &draw.viewportWidth, sizeof(viewportWidthBits));
    mix(viewportWidthBits);
    mix(draw.texture.enabled); mix(draw.texture.sourceId); mix(draw.texture.sourceGeneration);
    mix(draw.texture.revision); mix(draw.texture.globalEpoch); mix(draw.texture.format);
    mix(draw.texture.width); mix(draw.texture.height); mix(draw.texture.wrapS); mix(draw.texture.wrapT);
    mix(draw.texture.minFilter); mix(draw.texture.magFilter); mix(draw.texture.tevMode);
    mix(reinterpret_cast<std::uintptr_t>(draw.texture.data));
    mix(reinterpret_cast<std::uintptr_t>(draw.texture.thpUData));
    mix(reinterpret_cast<std::uintptr_t>(draw.texture.thpVData));
    mix(draw.texture.thpURevision); mix(draw.texture.thpVRevision);
    mix(draw.texture.thpUGeneration); mix(draw.texture.thpVGeneration);
    mix(draw.texture.thpChromaWidth); mix(draw.texture.thpChromaHeight); mix(draw.texture.thpYuv420);
    return hash;
}

bool FillCompactIndices(std::uint16_t* output, std::uint32_t primitive, std::uint32_t count,
                        std::uint32_t base) noexcept {
    if (!output && count > 2u) return false;
    std::uint32_t out = 0;
    auto push = [&](std::uint32_t value) {
        const std::uint32_t absolute = base + value;
        if (absolute > std::numeric_limits<std::uint16_t>::max()) return false;
        output[out++] = static_cast<std::uint16_t>(absolute);
        return true;
    };
    auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        return push(a) && push(b) && push(c);
    };
    switch (static_cast<GXPrimitive>(primitive)) {
    case GX_QUADS:
        for (std::uint32_t i = 0; i < count; i += 4u)
            if (!tri(i, i + 1u, i + 2u) || !tri(i + 2u, i + 3u, i)) return false;
        return true;
    case GX_TRIANGLES:
        for (std::uint32_t i = 0; i < count; ++i) if (!push(i)) return false;
        return true;
    case GX_TRIANGLESTRIP:
        for (std::uint32_t i = 2; i < count; ++i) {
            if ((i & 1u) != 0) { if (!tri(i - 1u, i - 2u, i)) return false; }
            else if (!tri(i - 2u, i - 1u, i)) return false;
        }
        return true;
    case GX_TRIANGLEFAN:
        for (std::uint32_t i = 2; i < count; ++i) if (!tri(0u, i - 1u, i)) return false;
        return true;
    default:
        return false;
    }
}

bool SameSampler(const gfx::SamplerDesc& a, const gfx::SamplerDesc& b) noexcept {
    return a.wrapS == b.wrapS && a.wrapT == b.wrapT && a.minFilter == b.minFilter &&
           a.magFilter == b.magFilter && a.lodBias == b.lodBias &&
           a.minLod == b.minLod && a.maxLod == b.maxLod;
}

bool SameTexture(const gfx::TextureBinding& a, const gfx::TextureBinding& b) noexcept {
    return a.texture == b.texture && a.source == b.source && SameSampler(a.sampler, b.sampler);
}

bool CompactBatchCompatible(const gfx::DrawPacket& a, const gfx::DrawPacket& b) noexcept {
#if MKW_VITA_FRAME_BATCHER
    if (!a.absoluteVertexIndices || !b.absoluteVertexIndices || a.pipelineKey != b.pipelineKey ||
        a.vertices.buffer != b.vertices.buffer || a.indices.buffer != b.indices.buffer ||
        a.vertices.offset + a.vertices.size != b.vertices.offset ||
        a.indices.offset + a.indices.size != b.indices.offset ||
        a.instanceCount != 1 || b.instanceCount != 1) return false;
    if (std::memcmp(&a.viewport, &b.viewport, sizeof(gfx::Viewport)) != 0 ||
        std::memcmp(&a.scissor, &b.scissor, sizeof(gfx::Scissor)) != 0 ||
        std::memcmp(&a.uniforms, &b.uniforms, sizeof(gfx::DrawUniforms)) != 0) return false;
    for (unsigned i = 0; i < gfx::MaxTextures; ++i) if (!SameTexture(a.textures[i], b.textures[i])) return false;
    return true;
#else
    (void)a; (void)b;
    return false;
#endif
}

void ConfigureTev(gfx::PipelineDesc& pipeline, std::uint8_t mode, bool textured) noexcept {
    auto& stage = pipeline.tev.stages[0];
    pipeline.tev.stageCount = 1;
    pipeline.tev.texCoordCount = textured ? 1 : 0;
    pipeline.tev.rasterColorCount = 1;
    stage.texture = textured ? 0 : 0xff;
    stage.texCoord = textured ? 0 : 0xff;
    stage.rasterSource = gfx::RasterSource::Color0;

    if (!textured || static_cast<GXTevMode>(mode) == GX_PASSCLR) {
        stage.color.d = gfx::TevColorArg::RasColor;
        stage.alpha.d = gfx::TevAlphaArg::RasAlpha;
        return;
    }

    switch (static_cast<GXTevMode>(mode)) {
    case GX_DECAL:
        stage.color = {gfx::TevColorArg::RasColor, gfx::TevColorArg::TexColor,
                       gfx::TevColorArg::TexAlpha, gfx::TevColorArg::Zero};
        stage.alpha.d = gfx::TevAlphaArg::RasAlpha;
        break;
    case GX_BLEND:
        stage.color = {gfx::TevColorArg::RasColor, gfx::TevColorArg::One,
                       gfx::TevColorArg::TexColor, gfx::TevColorArg::Zero};
        stage.alpha = {gfx::TevAlphaArg::Zero, gfx::TevAlphaArg::TexAlpha,
                       gfx::TevAlphaArg::RasAlpha, gfx::TevAlphaArg::Zero};
        break;
    case GX_REPLACE:
        stage.color.d = gfx::TevColorArg::TexColor;
        stage.alpha.d = gfx::TevAlphaArg::TexAlpha;
        break;
    case GX_MODULATE:
    default:
        stage.color = {gfx::TevColorArg::Zero, gfx::TevColorArg::TexColor,
                       gfx::TevColorArg::RasColor, gfx::TevColorArg::Zero};
        stage.alpha = {gfx::TevAlphaArg::Zero, gfx::TevAlphaArg::TexAlpha,
                       gfx::TevAlphaArg::RasAlpha, gfx::TevAlphaArg::Zero};
        break;
    }
}

std::uint32_t FoldTextureRevision(const AuroraPacketTexture& texture) noexcept {
    const std::uint64_t generation = texture.sourceGeneration;
    if (generation != std::numeric_limits<std::uint64_t>::max()) {
        // For tracked guest RAM the range generation is the content revision.
        // GXInvalidateTexAll only invalidates Wii TMEM/cache state, and GXTexObj
        // objects are routinely rebuilt without changing the source bytes.
        // Folding object/global revisions here would therefore force needless
        // decode + glTexImage2D uploads every frame.
        return static_cast<std::uint32_t>(generation) ^
               static_cast<std::uint32_t>(generation >> 32u);
    }
    // Untracked sources retain the conservative object/global invalidation path.
    return texture.revision ^ (texture.globalEpoch * 0x9e3779b9u);
}

std::uint64_t MixThpKey(std::uint64_t key, std::uint64_t value) noexcept {
    key ^= value + 0x9e3779b97f4a7c15ull + (key << 6u) + (key >> 2u);
    return key;
}

std::uint64_t ThpPlaneRevision(std::uint64_t generation, std::uint32_t revision,
                               std::uint32_t epoch) noexcept {
    if (generation != std::numeric_limits<std::uint64_t>::max()) {
        return generation;
    }
    return static_cast<std::uint64_t>(revision) ^
           (static_cast<std::uint64_t>(epoch) << 32u);
}

std::uint8_t ReadTiledI8(const std::uint8_t* data, std::uint32_t width,
                         std::uint32_t x, std::uint32_t y) noexcept {
    const std::uint32_t blocksPerRow = (width + 7u) / 8u;
    const std::size_t block = static_cast<std::size_t>(y / 4u) * blocksPerRow + x / 8u;
    return data[block * 32u + static_cast<std::size_t>(y & 3u) * 8u + (x & 7u)];
}

std::uint8_t ClampThpColor(int value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

bool PrepareThpYuv420(const AuroraPacketTexture& input, gfx::TextureDesc& output) noexcept {
    if (!input.thpYuv420 || !input.data || !input.thpUData || !input.thpVData ||
        input.width == 0u || input.height == 0u || input.thpChromaWidth == 0u ||
        input.thpChromaHeight == 0u) {
        return false;
    }
    const std::size_t yNeed = static_cast<std::size_t>((input.width + 7u) / 8u) *
                              ((input.height + 3u) / 4u) * 32u;
    const std::size_t cNeed = static_cast<std::size_t>((input.thpChromaWidth + 7u) / 8u) *
                              ((input.thpChromaHeight + 3u) / 4u) * 32u;
    if (input.dataBytes < yNeed || input.thpUBytes < cNeed || input.thpVBytes < cNeed) {
        return false;
    }

    std::uint64_t key = 0xcbf29ce484222325ull;
    key = MixThpKey(key, input.sourceId);
    key = MixThpKey(key, reinterpret_cast<std::uintptr_t>(input.thpUData));
    key = MixThpKey(key, reinterpret_cast<std::uintptr_t>(input.thpVData));
    key = MixThpKey(key, ThpPlaneRevision(input.sourceGeneration, input.revision,
                                          input.globalEpoch));
    key = MixThpKey(key, ThpPlaneRevision(input.thpUGeneration, input.thpURevision,
                                          input.globalEpoch));
    key = MixThpKey(key, ThpPlaneRevision(input.thpVGeneration, input.thpVRevision,
                                          input.globalEpoch));
    key = MixThpKey(key, (static_cast<std::uint64_t>(input.width) << 48u) |
                         (static_cast<std::uint64_t>(input.height) << 32u) |
                         (static_cast<std::uint64_t>(input.thpChromaWidth) << 16u) |
                         input.thpChromaHeight);

    if (key != g_thpConversionKey) {
        const std::uint64_t conversionBeginUs = sceKernelGetProcessTimeWide();
        const std::size_t rgbaBytes = static_cast<std::size_t>(input.width) * input.height * 4u;
        g_thpRgba.resize(rgbaBytes);
        const auto* yPlane = static_cast<const std::uint8_t*>(input.data);
        const auto* uPlane = static_cast<const std::uint8_t*>(input.thpUData);
        const auto* vPlane = static_cast<const std::uint8_t*>(input.thpVData);
        // 4:2:0 shares one chroma sample across a 2x2 luma block. The old loop
        // re-read U/V and recomputed all chroma coefficients for every pixel.
        // Reuse them here: this removes 75% of chroma tiled-addressing and the
        // corresponding integer multiplies from the render thread.
        for (std::uint32_t y = 0; y < input.height; y += 2u) {
            const std::uint32_t chromaY = y >> 1u;
            for (std::uint32_t x = 0; x < input.width; x += 2u) {
                const std::uint32_t chromaX = x >> 1u;
                const int uu = static_cast<int>(ReadTiledI8(
                    uPlane, input.thpChromaWidth, chromaX, chromaY)) - 128;
                const int vv = static_cast<int>(ReadTiledI8(
                    vPlane, input.thpChromaWidth, chromaX, chromaY)) - 128;
                const int rChroma = 409 * vv;
                const int gChroma = -100 * uu - 208 * vv;
                const int bChroma = 516 * uu;
                for (std::uint32_t dy = 0; dy < 2u && y + dy < input.height; ++dy) {
                    for (std::uint32_t dx = 0; dx < 2u && x + dx < input.width; ++dx) {
                        const std::uint32_t px = x + dx;
                        const std::uint32_t py = y + dy;
                        const int yy = static_cast<int>(ReadTiledI8(
                            yPlane, input.width, px, py)) - 16;
                        const int c = std::max(0, yy);
                        const std::size_t offset =
                            (static_cast<std::size_t>(py) * input.width + px) * 4u;
                        g_thpRgba[offset + 0u] = ClampThpColor((298 * c + rChroma + 128) >> 8);
                        g_thpRgba[offset + 1u] = ClampThpColor((298 * c + gChroma + 128) >> 8);
                        g_thpRgba[offset + 2u] = ClampThpColor((298 * c + bChroma + 128) >> 8);
                        g_thpRgba[offset + 3u] = 255u;
                    }
                }
            }
        }
        g_thpConversionKey = key;
        ++g_thpConversionCount;
        if (g_thpConversionCount <= 24u) {
            const std::uint64_t conversionEndUs = sceKernelGetProcessTimeWide();
            RT_LOGF(RT_TAG_GX,
                    "thp_yuv420 n=%llu size=%ux%u y=0x%llX u=0x%llX v=0x%llX gen=%llu/%llu/%llu elapsed_us=%llu\n",
                    static_cast<unsigned long long>(g_thpConversionCount),
                    static_cast<unsigned>(input.width), static_cast<unsigned>(input.height),
                    static_cast<unsigned long long>(input.sourceId),
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(input.thpUData)),
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(input.thpVData)),
                    static_cast<unsigned long long>(input.sourceGeneration),
                    static_cast<unsigned long long>(input.thpUGeneration),
                    static_cast<unsigned long long>(input.thpVGeneration),
                    static_cast<unsigned long long>(conversionEndUs - conversionBeginUs));
        }
    }

    output.width = input.width;
    output.height = input.height;
    output.format = gfx::TextureFormat::RGBA8888;
    output.data = g_thpRgba.data();
    output.dataSize = g_thpRgba.size();
    output.sourceId = input.sourceId;
    output.revision = static_cast<std::uint32_t>(key) ^ static_cast<std::uint32_t>(key >> 32u);
    output.mipCount = 1;
    return true;
}

} // namespace

bool AuroraPacketRendererInitialize() noexcept {
    const uint64_t initBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=aurora_packet phase=begin t_us=%llu\n",
            static_cast<unsigned long long>(initBeginUs));

    gfx::RendererConfig rendererConfig{};
    rendererConfig.width = 960;
    rendererConfig.height = 544;
    // M12.1: conservative — the M12 arena bump raised total GPU pressure and the
    // speedhack vitaGL alloc path crashes on OOM instead of failing cleanly. The
    // cache's pre-eviction keeps bytes_ under this; graceful OOM, not more memory.
    rendererConfig.textureBudget = 10u * 1024u * 1024u;
    rendererConfig.pipelineBudget = 256;
    auto renderer = std::make_unique<gfx::Renderer>(rendererConfig);
    const uint64_t rendererBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=aurora_renderer phase=begin t_us=%llu texture_budget=%u pipeline_budget=%u\n",
            static_cast<unsigned long long>(rendererBeginUs),
            static_cast<unsigned>(rendererConfig.textureBudget), rendererConfig.pipelineBudget);
    if (!renderer->initialize()) {
        RT_LOGF(RT_TAG_GX,
                "init_marker=aurora_renderer phase=failed t_us=%llu\n",
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide()));
        return false;
    }
    const uint64_t rendererEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=aurora_renderer phase=end t_us=%llu elapsed_us=%llu\n",
            static_cast<unsigned long long>(rendererEndUs),
            static_cast<unsigned long long>(rendererEndUs - rendererBeginUs));

    gfx::StreamingArenaConfig arenaConfig{};
    // M12.7 accepts up to 49152 packet vertices. CanonicalVertex is 168 bytes, so
    // an 8 MiB slot can retain the entire bounded frame without mid-frame reuse.
    // M12.6's 4 MiB slot was validated only through Select Class, not the 6154-draw
    // first race frame. Two slots add 8 MiB over M12.6.
    arenaConfig.vertexBytes = 8u * 1024u * 1024u;
    arenaConfig.indexBytes = 512u * 1024u;
    arenaConfig.slots = 2;
    arenaConfig.alignment = 16;
    auto arena = std::make_unique<gfx::StreamingArena>(renderer->buffers(), arenaConfig);
    const uint64_t arenaBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=aurora_streaming_arena phase=begin t_us=%llu slots=%u vbo_bytes=%u ibo_bytes=%u\n",
            static_cast<unsigned long long>(arenaBeginUs), arenaConfig.slots,
            static_cast<unsigned>(arenaConfig.vertexBytes),
            static_cast<unsigned>(arenaConfig.indexBytes));
    if (!arena->initialize()) {
        RT_LOGF(RT_TAG_GX,
                "init_marker=aurora_streaming_arena phase=failed t_us=%llu\n",
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide()));
        renderer->shutdown();
        return false;
    }
    const uint64_t arenaEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=aurora_streaming_arena phase=end t_us=%llu elapsed_us=%llu\n",
            static_cast<unsigned long long>(arenaEndUs),
            static_cast<unsigned long long>(arenaEndUs - arenaBeginUs));

    g_renderer = std::move(renderer);
    g_arena = std::move(arena);
    const uint64_t preparedBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=aurora_prepared_reserve phase=begin t_us=%llu vertex_target=%u index_target=%u\n",
            static_cast<unsigned long long>(preparedBeginUs), 1024u, 1536u);
    g_prepared.vertices.reserve(1024);
    g_prepared.indices.reserve(1536);
    const uint64_t preparedEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=aurora_prepared_reserve phase=end t_us=%llu elapsed_us=%llu vertex_capacity=%u index_capacity=%u\n",
            static_cast<unsigned long long>(preparedEndUs),
            static_cast<unsigned long long>(preparedEndUs - preparedBeginUs),
            static_cast<unsigned>(g_prepared.vertices.capacity()),
            static_cast<unsigned>(g_prepared.indices.capacity()));
    RT_LOGF(RT_TAG_GX,
            "init_marker=aurora_packet phase=end t_us=%llu elapsed_us=%llu ready=1\n",
            static_cast<unsigned long long>(preparedEndUs),
            static_cast<unsigned long long>(preparedEndUs - initBeginUs));
    return true;
}

void AuroraPacketRendererShutdown() noexcept {
    g_frameActive = false;
    g_stream.reset();
    g_prepared = {};
    g_efbCopies.clear();
    g_efbReadback.clear();
    g_efbUpload.clear();
    g_efbXMap.clear();
    g_efbYMap.clear();
    if (g_efbMappedReadback) {
        vglFree(g_efbMappedReadback);
        g_efbMappedReadback = nullptr;
        g_efbMappedReadbackBytes = 0;
    }
    g_efbAllocationBlocked = 0;
    g_efbAllocationBlockedBytes = 0;
    if (g_arena) g_arena->shutdown();
    if (g_renderer) g_renderer->shutdown();
    g_arena.reset();
    g_renderer.reset();
}

bool AuroraPacketRendererBeginFrame(std::uint64_t serial,
                                    float viewportX, float viewportY,
                                    float viewportWidth, float viewportHeight,
                                    std::int32_t scissorX, std::int32_t scissorY,
                                    std::int32_t scissorWidth, std::int32_t scissorHeight) noexcept {
    if (!g_renderer || !g_arena || g_frameActive) return false;
    g_renderer->begin_frame();
    g_arena->begin_frame(serial);
#if MKW_VITA_TEXTURE_SAFE_RETRY
    // P6 safe slot reuse performs a real glFinish when a two-frame-old stream
    // slot is still in flight. Reuse that completion information for texture
    // lifetime protection; no extra fence is introduced here.
    if (g_arena->reuse_wait_us() != 0u) g_renderer->mark_texture_gpu_idle();
#endif
    g_stream.reset();
    g_stream.reserve(193);
    g_viewport = {viewportX, viewportY, viewportWidth, viewportHeight, 0.0f, 1.0f};
    g_scissor = {scissorX, scissorY, scissorWidth, scissorHeight};
    g_lastPipelineHash = 0;
    g_lastPipelineKey = 0;
    g_frameEfbGpuCopies = 0;
    g_frameEfbReadbackCopies = 0;
    g_frameEfbTransferReadbacks = 0;
    g_packetPerf = {};
    g_wiiTriangleProbe = {};
    g_wiiTriangleProbe.serial = serial;
    g_compactState = {};
    g_renderer->clear_current({0.015f, 0.02f, 0.035f, 1.0f}, 1.0f, true, true, true);
    g_frameActive = true;
    return true;
}

AuroraPacketSubmitResult AuroraPacketRendererSubmit(const AuroraPacketDraw& draw) noexcept {
    AuroraPacketSubmitResult result{};
    result.prepareError = 255u;
    if (!g_frameActive || !g_renderer || !g_arena || !draw.vertices || draw.vertexCount == 0) return result;
    ++g_packetPerf.logicalSubmits;
    AnalyzeWiiPerspectiveTriangles(draw);

    gfx::Primitive compactPrimitive = gfx::Primitive::Triangles;
    std::uint32_t compactIndexCount = 0;
    const bool compactEligible = MKW_VITA_COMPACT_VERTEX &&
        CompactIndexInfo(draw.primitive, draw.vertexCount, compactPrimitive, compactIndexCount) &&
        compactIndexCount != 0;
    const std::uint64_t compactStateKey = compactEligible
        ? (draw.renderStateId != 0u
               ? (0x8000000000000000ull |
                  (static_cast<std::uint64_t>(draw.renderStateId) << 1u) |
                  static_cast<std::uint64_t>(draw.texture.enabled))
               : CompactStateKey(draw))
        : 0u;

#if MKW_VITA_DIRECT_STREAM_WRITE
    // Fast continuation of an adjacent producer state run. The first draw in
    // the run resolves pipeline/texture/uniform state; subsequent draws only
    // append compact vertices and absolute U16 indices to the existing command.
    // This removes the ~840 B PipelineDesc + ~1.5 KiB DrawUniforms + ~1.8 KiB
    // DrawPacket construction/hash/memcmp from the dominant merged-draw path.
    if (compactEligible && draw.renderStateId != 0u && g_compactState.valid &&
        g_compactState.key == compactStateKey) {
        ++g_packetPerf.compactStateHits;
        const std::uint64_t streamBeginUs = sceKernelGetProcessTimeWide();
        const std::size_t vertexBytes =
            static_cast<std::size_t>(draw.vertexCount) * sizeof(AuroraPacketVertex);
        void* vertexWrite = nullptr;
        gfx::BufferSlice vertexSlice =
            g_arena->reserve_vertices(vertexBytes, alignof(AuroraPacketVertex), &vertexWrite);
        if (!vertexSlice.buffer || !vertexWrite ||
            (vertexSlice.offset % sizeof(AuroraPacketVertex)) != 0u) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::StreamingOverflow);
            return result;
        }
        std::memcpy(vertexWrite, draw.vertices, vertexBytes);
        const std::uint32_t vertexBase = vertexSlice.offset / sizeof(AuroraPacketVertex);
        void* indexWrite = nullptr;
        gfx::BufferSlice indexSlice = g_arena->reserve_indices(
            static_cast<std::size_t>(compactIndexCount) * sizeof(std::uint16_t),
            alignof(std::uint16_t), &indexWrite);
        if (!indexSlice.buffer || !indexWrite) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::StreamingOverflow);
            return result;
        }
        const std::uint64_t indexBeginUs = sceKernelGetProcessTimeWide();
        if (!FillCompactIndices(static_cast<std::uint16_t*>(indexWrite), draw.primitive,
                                draw.vertexCount, vertexBase)) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::TooManyVertices);
            return result;
        }
        g_packetPerf.indexBuildUs += sceKernelGetProcessTimeWide() - indexBeginUs;

        gfx::DrawPacket* tail = g_stream.tail_draw();
        const bool contiguous = tail && tail->absoluteVertexIndices &&
            tail->pipelineKey == g_compactState.pipelineKey &&
            tail->vertices.buffer == vertexSlice.buffer &&
            tail->indices.buffer == indexSlice.buffer &&
            tail->vertices.offset + tail->vertices.size == vertexSlice.offset &&
            tail->indices.offset + tail->indices.size == indexSlice.offset &&
            tail->instanceCount == 1u;
        if (contiguous) {
            tail->vertices.size += vertexSlice.size;
            tail->indices.size += indexSlice.size;
            tail->vertexCount += draw.vertexCount;
            tail->indexCount += compactIndexCount;
            ++g_packetPerf.batchMerges;
            ++g_packetPerf.compactRunExtends;
        } else {
            gfx::DrawPacket packet{};
            packet.pipelineKey = g_compactState.pipelineKey;
            packet.vertices = vertexSlice;
            packet.indices = indexSlice;
            packet.vertexCount = draw.vertexCount;
            packet.indexCount = compactIndexCount;
            packet.absoluteVertexIndices = true;
            packet.textures = g_compactState.bindings;
            packet.uniforms = g_compactState.uniforms;
            packet.viewport = {draw.viewportX, draw.viewportY, draw.viewportWidth, draw.viewportHeight,
                               draw.viewportNear, draw.viewportFar};
            packet.scissor = {draw.scissorX, draw.scissorY, draw.scissorWidth, draw.scissorHeight};
            g_stream.draw(packet);
        }
        ++g_packetPerf.compactDraws;
        g_packetPerf.streamWriteUs += sceKernelGetProcessTimeWide() - streamBeginUs;
        result.submitted = true;
        result.textureDrawn = g_compactState.textured;
        result.textureEfb = g_compactState.textureEfb;
        result.prepareError = 0u;
        return result;
    }
#endif

    gfx::PreparedDraw& prepared = g_prepared;
    prepared.error = gfx::PrepareDrawError::None;
    prepared.vertices.clear();
    prepared.indices.clear();
    prepared.primitive = gfx::Primitive::Triangles;
    prepared.positionIsClipSpace = true;

    if (!compactEligible) {
        if (MKW_VITA_COMPACT_VERTEX) ++g_packetPerf.compactFallbacks;
        const uint64_t indexBeginUs = sceKernelGetProcessTimeWide();
        if (!BuildIndices(prepared.indices, draw.primitive, draw.vertexCount, prepared.primitive)) return result;
        g_packetPerf.indexBuildUs += sceKernelGetProcessTimeWide() - indexBeginUs;
        const uint64_t packBeginUs = sceKernelGetProcessTimeWide();
        prepared.vertices.resize(draw.vertexCount);
        for (std::uint32_t i = 0; i < draw.vertexCount; ++i) {
            const AuroraPacketVertex& source = draw.vertices[i];
            gfx::CanonicalVertex& destination = prepared.vertices[i];
            destination.position[0] = source.x;
            destination.position[1] = source.y;
            destination.position[2] = source.z;
            destination.position[3] = 1.0f;
            destination.color0[0] = source.r;
            destination.color0[1] = source.g;
            destination.color0[2] = source.b;
            destination.color0[3] = source.a;
            std::memcpy(destination.color1, destination.color0, sizeof(destination.color0));
            destination.texcoord[0][0] = source.s;
            destination.texcoord[0][1] = source.t;
            destination.texcoord[0][2] = 1.0f;
        }
        g_packetPerf.vertexPackUs += sceKernelGetProcessTimeWide() - packBeginUs;
    } else {
        prepared.primitive = compactPrimitive;
    }

    gfx::PipelineDesc pipeline{};
    pipeline.primitive = compactEligible ? compactPrimitive : prepared.primitive;
    pipeline.positionIsClipSpace = true;
    pipeline.layout = compactEligible ? CompactVertexLayout() : gfx::canonical_vertex_layout();
    pipeline.depthFunc = MapCompare(draw.depthFunc);
    pipeline.cull = MapCull(draw.cullMode);
    pipeline.blendMode = MapBlend(draw.blendMode);
    pipeline.srcFactor = MapBlendFactor(draw.blendSrc, false);
    pipeline.dstFactor = MapBlendFactor(draw.blendDst, true);
    pipeline.logicOp = MapLogic(draw.logicOp);
    pipeline.depthTest = draw.depthCompare;
    pipeline.depthWrite = draw.depthUpdate;
    pipeline.colorWrite = draw.colorUpdate;
    pipeline.alphaWrite = draw.alphaUpdate;
    pipeline.reversedZ = false;
    pipeline.tev.alphaCompare.comp0 = MapCompare(draw.alphaComp0);
    pipeline.tev.alphaCompare.comp1 = MapCompare(draw.alphaComp1);
    pipeline.tev.alphaCompare.ref0 = draw.alphaRef0;
    pipeline.tev.alphaCompare.ref1 = draw.alphaRef1;
    pipeline.tev.alphaCompare.op = static_cast<std::uint8_t>(draw.alphaOp & 3u);

    const bool reuseCompactState = compactEligible && g_compactState.valid &&
                                   g_compactState.key == compactStateKey;
    std::array<gfx::TextureBinding, gfx::MaxTextures> bindings{};
    bool textured = false;
    gfx::DrawUniforms uniforms{};
    const gfx::Viewport drawViewport{draw.viewportX, draw.viewportY,
                                     draw.viewportWidth, draw.viewportHeight,
                                     draw.viewportNear, draw.viewportFar};
    const gfx::Scissor drawScissor{draw.scissorX, draw.scissorY,
                                   draw.scissorWidth, draw.scissorHeight};
    if (reuseCompactState) {
        ++g_packetPerf.compactStateHits;
        bindings = g_compactState.bindings;
        uniforms = g_compactState.uniforms;
        textured = g_compactState.textured;
        result.textureDrawn = textured;
        result.textureEfb = g_compactState.textureEfb;
        g_lastPipelineKey = g_compactState.pipelineKey;
        g_lastPipelineHash = g_compactState.pipelineHash;
    } else {
        if (compactEligible) {
            ++g_packetPerf.compactStateMisses;
            ++g_packetPerf.compactRunStarts;
        }
    const uint64_t textureBeginUs = sceKernelGetProcessTimeWide();
    if (draw.texture.enabled && draw.texture.data && draw.texture.width && draw.texture.height) {
        const auto efbIt = g_efbCopies.find(draw.texture.sourceId);
        if (efbIt != g_efbCopies.end() && efbIt->second.handle != gfx::InvalidHandle) {
            bindings[0].texture = efbIt->second.handle;
            bindings[0].source = gfx::TextureSource::Efb;
            bindings[0].sampler.wrapS = MapWrap(draw.texture.wrapS);
            bindings[0].sampler.wrapT = MapWrap(draw.texture.wrapT);
            bindings[0].sampler.minFilter = MapFilter(draw.texture.minFilter);
            bindings[0].sampler.magFilter = MapFilter(draw.texture.magFilter);
            textured = true;
            result.textureDrawn = true;
            result.textureEfb = true;
        } else {
            if (!g_efbCopies.empty() && g_efbSourceMissTraceCount < 24u) {
                const auto& sample = *g_efbCopies.begin();
                ++g_efbSourceMissTraceCount;
#if MKW_VITA_PERF_LOG
                RT_LOGF(RT_TAG_GX,
                        "efb_source_miss n=%llu source=0x%llX known=%u sample=0x%llX size=%ux%u\n",
                        static_cast<unsigned long long>(g_efbSourceMissTraceCount),
                        static_cast<unsigned long long>(draw.texture.sourceId),
                        static_cast<unsigned>(g_efbCopies.size()),
                        static_cast<unsigned long long>(sample.first),
                        static_cast<unsigned>(sample.second.width),
                        static_cast<unsigned>(sample.second.height));
#endif
            }
            gfx::TextureFormat textureFormat{};
            gfx::TextureDesc texture{};
            const bool thpPrepared = PrepareThpYuv420(draw.texture, texture);
            if (!thpPrepared && !MapTextureFormat(draw.texture.format, textureFormat)) {
                result.textureUnsupported = true;
            } else {
                if (!thpPrepared) {
                    texture.width = draw.texture.width;
                    texture.height = draw.texture.height;
                    texture.format = textureFormat;
                    texture.data = draw.texture.data;
                    texture.dataSize = draw.texture.dataBytes;
                    texture.sourceId = draw.texture.sourceId;
                    texture.revision = FoldTextureRevision(draw.texture);
                    texture.mipCount = 1;
                }
                const gfx::FrameStats before = g_renderer->stats();
                const std::uint64_t allocFailBefore = g_renderer->textures().alloc_fail_total();
                bindings[0].texture = g_renderer->create_texture(texture);
#if MKW_VITA_TEXTURE_SAFE_RETRY
                constexpr std::uint32_t kMaxSafeTextureRetriesPerFrame = 4u;
                const bool pressureFailure = bindings[0].texture == gfx::InvalidHandle &&
                    g_renderer->textures().alloc_fail_total() > allocFailBefore &&
                    g_renderer->textures().last_allocation_blocked_by_protection();
                if (pressureFailure &&
                    g_packetPerf.textureSafeRetryAttempts < kMaxSafeTextureRetriesPerFrame) {
                    ++g_packetPerf.textureSafeRetryAttempts;
                    const std::uint64_t retryBeginUs = sceKernelGetProcessTimeWide();
                    if (FlushQueuedDraws()) {
                        // The flush submits every command that can reference the
                        // protected entries; the explicit finish is the resource
                        // lifetime fence that makes same-frame LRU eviction safe.
                        glFinish();
                        g_renderer->mark_texture_gpu_idle();
                        g_renderer->invalidate_draw_state();
                        bindings[0].texture = g_renderer->create_texture(texture);
                    }
                    g_packetPerf.textureSafeRetryWaitUs +=
                        sceKernelGetProcessTimeWide() - retryBeginUs;
                    if (bindings[0].texture != gfx::InvalidHandle) {
                        ++g_packetPerf.textureSafeRetrySuccesses;
                    } else {
                        ++g_packetPerf.textureSafeRetryFailAfter;
                    }
                }
#endif
                const gfx::FrameStats after = g_renderer->stats();
                result.textureHit = after.textureHits > before.textureHits;
                result.textureMiss = after.textureMisses > before.textureMisses;
                result.textureUploaded = after.textureUploads > before.textureUploads;
                result.textureUploadFailed = bindings[0].texture == gfx::InvalidHandle;
                if (result.textureUploaded) {
                    result.textureBytesUploaded = static_cast<std::uint64_t>(draw.texture.width) *
                                                  draw.texture.height * 4u;
                }
                if (bindings[0].texture != gfx::InvalidHandle) {
                    bindings[0].sampler.wrapS = MapWrap(draw.texture.wrapS);
                    bindings[0].sampler.wrapT = MapWrap(draw.texture.wrapT);
                    bindings[0].sampler.minFilter = MapFilter(draw.texture.minFilter);
                    bindings[0].sampler.magFilter = MapFilter(draw.texture.magFilter);
                    textured = true;
                    result.textureDrawn = true;
                }
            }
        }
    }
    g_packetPerf.textureResolveUs += sceKernelGetProcessTimeWide() - textureBeginUs;
    ConfigureTev(pipeline, draw.texture.tevMode, textured);

    uniforms.renderViewportWidth = drawViewport.width;
    if (textured) {
        uniforms.texcoordScale[0] = {static_cast<float>(draw.texture.width),
                                     static_cast<float>(draw.texture.height), 0.0f, 0.0f};
        uniforms.textureSizeBias[0] = {static_cast<float>(draw.texture.width),
                                       static_cast<float>(draw.texture.height), 0.0f, 0.0f};
    }

    const uint64_t pipelineBeginUs = sceKernelGetProcessTimeWide();
    const std::uint64_t pipelineHash = gfx::pipeline_key(pipeline);
    if (pipelineHash != g_lastPipelineHash || g_lastPipelineKey == 0) {
        g_lastPipelineKey = compactEligible
            ? g_renderer->create_pipeline(pipeline)
            : gfx::resolve_draw_pipeline(*g_renderer, prepared, pipeline);
        g_lastPipelineHash = pipelineHash;
    }
    g_packetPerf.pipelineResolveUs += sceKernelGetProcessTimeWide() - pipelineBeginUs;
    if (g_lastPipelineKey == 0) {
        result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::PipelineFailed);
        return result;
    }

        if (compactEligible) {
            // A failed dynamic upload is retryable; do not freeze that failure for
            // the rest of an otherwise identical run.
            g_compactState.valid = !result.textureUploadFailed;
            g_compactState.key = compactStateKey;
            g_compactState.pipelineKey = g_lastPipelineKey;
            g_compactState.pipelineHash = pipelineHash;
            g_compactState.bindings = bindings;
            g_compactState.uniforms = uniforms;
            g_compactState.textured = textured;
            g_compactState.textureEfb = result.textureEfb;
        }
    }

    if (compactEligible) {
        const uint64_t streamBeginUs = sceKernelGetProcessTimeWide();
        const std::size_t vertexBytes = static_cast<std::size_t>(draw.vertexCount) * sizeof(AuroraPacketVertex);
        gfx::BufferSlice vertexSlice{};
        gfx::BufferSlice indexSlice{};
#if MKW_VITA_DIRECT_STREAM_WRITE
        void* vertexWrite = nullptr;
        vertexSlice = g_arena->reserve_vertices(vertexBytes, alignof(AuroraPacketVertex), &vertexWrite);
        if (!vertexSlice.buffer || !vertexWrite) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::StreamingOverflow);
            return result;
        }
        std::memcpy(vertexWrite, draw.vertices, vertexBytes);
        if ((vertexSlice.offset % sizeof(AuroraPacketVertex)) != 0u) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::StreamingOverflow);
            return result;
        }
        const std::uint32_t vertexBase = vertexSlice.offset / sizeof(AuroraPacketVertex);
        void* indexWrite = nullptr;
        indexSlice = g_arena->reserve_indices(
            static_cast<std::size_t>(compactIndexCount) * sizeof(std::uint16_t),
            alignof(std::uint16_t), &indexWrite);
        if (!indexSlice.buffer || !indexWrite) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::StreamingOverflow);
            return result;
        }
        const uint64_t indexBeginUs = sceKernelGetProcessTimeWide();
        if (!FillCompactIndices(static_cast<std::uint16_t*>(indexWrite), draw.primitive,
                                draw.vertexCount, vertexBase)) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::TooManyVertices);
            return result;
        }
        g_packetPerf.indexBuildUs += sceKernelGetProcessTimeWide() - indexBeginUs;
#else
        vertexSlice = g_arena->upload_vertices(draw.vertices, vertexBytes, alignof(AuroraPacketVertex));
        if (!vertexSlice.buffer || (vertexSlice.offset % sizeof(AuroraPacketVertex)) != 0u) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::StreamingOverflow);
            return result;
        }
        const std::uint32_t vertexBase = vertexSlice.offset / sizeof(AuroraPacketVertex);
        const uint64_t indexBeginUs = sceKernelGetProcessTimeWide();
        gfx::Primitive rebuiltPrimitive{};
        if (!BuildIndices(prepared.indices, draw.primitive, draw.vertexCount, rebuiltPrimitive)) return result;
        g_packetPerf.indexBuildUs += sceKernelGetProcessTimeWide() - indexBeginUs;
        indexSlice = g_arena->upload_rebased_indices(prepared.indices.data(), prepared.indices.size(), vertexBase);
        if (!indexSlice.buffer) {
            result.prepareError = static_cast<std::uint8_t>(gfx::PrepareDrawError::StreamingOverflow);
            return result;
        }
#endif
        gfx::DrawPacket packet{};
        packet.pipelineKey = g_lastPipelineKey;
        packet.vertices = vertexSlice;
        packet.indices = indexSlice;
        packet.vertexCount = draw.vertexCount;
        packet.indexCount = compactIndexCount;
        packet.absoluteVertexIndices = true;
        packet.textures = bindings;
        packet.uniforms = uniforms;
        packet.viewport = drawViewport;
        packet.scissor = drawScissor;
        if (auto* tail = g_stream.tail_draw(); tail && CompactBatchCompatible(*tail, packet)) {
            tail->vertices.size = (packet.vertices.offset + packet.vertices.size) - tail->vertices.offset;
            tail->indices.size += packet.indices.size;
            tail->vertexCount += packet.vertexCount;
            tail->indexCount += packet.indexCount;
            ++g_packetPerf.batchMerges;
        } else {
            g_stream.draw(packet);
        }
        ++g_packetPerf.compactDraws;
        g_packetPerf.streamWriteUs += sceKernelGetProcessTimeWide() - streamBeginUs;
        result.submitted = true;
        result.prepareError = 0u;
        return result;
    }

    gfx::PrepareDrawError error = gfx::PrepareDrawError::None;
    const uint64_t streamBeginUs = sceKernelGetProcessTimeWide();
    result.submitted = gfx::enqueue_draw(*g_renderer, *g_arena, g_stream, prepared, pipeline,
                                         uniforms, drawViewport, drawScissor, bindings, &error,
                                         nullptr, g_lastPipelineKey);
    g_packetPerf.streamWriteUs += sceKernelGetProcessTimeWide() - streamBeginUs;
    result.prepareError = result.submitted ? 0u : static_cast<std::uint8_t>(error);
    return result;
}

bool AuroraPacketRendererCopyEfb(const AuroraPacketEfbCopy& copy) noexcept {
    if (!g_frameActive || !g_renderer || !g_arena || copy.destinationId == 0 ||
        copy.srcWidth <= 0 || copy.srcHeight <= 0 || copy.dstWidth == 0 || copy.dstHeight == 0) {
        return false;
    }
    const std::uint64_t syncBeginUs = sceKernelGetProcessTimeWide();
    const bool flushed = FlushQueuedDraws();
    g_packetPerf.efbSyncUs += sceKernelGetProcessTimeWide() - syncBeginUs;
    if (!flushed) return false;

    const gfx::EfbCopyFormat format = gfx::efb_copy_format_from_gx_raw(copy.format);
    if (!gfx::is_supported_color_copy_format(format)) return false;

    const std::int32_t targetW = static_cast<std::int32_t>(g_renderer->target_width());
    const std::int32_t targetH = static_cast<std::int32_t>(g_renderer->target_height());
    if (targetW <= 0 || targetH <= 0) return false;

    const std::int64_t requestedX1 = static_cast<std::int64_t>(copy.srcX) + copy.srcWidth;
    const std::int64_t requestedY1 = static_cast<std::int64_t>(copy.srcY) + copy.srcHeight;
    const std::int32_t x0 = std::clamp(copy.srcX, 0, targetW);
    const std::int32_t y0 = std::clamp(copy.srcY, 0, targetH);
    const std::int32_t x1 = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(requestedX1, 0, targetW));
    const std::int32_t y1 = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(requestedY1, 0, targetH));
    if (x1 <= x0 || y1 <= y0) return false;

    const std::uint32_t srcW = static_cast<std::uint32_t>(x1 - x0);
    const std::uint32_t srcH = static_cast<std::uint32_t>(y1 - y0);
    // Dynamic GXCopyTex results are sampled with normalized UVs in the menu path. Never upscale
    // the physical copy beyond the source: it wastes the Vita's very limited mapped memory and
    // provides no visual benefit. Common 960x544 rendering therefore downsamples to Wii-logical size.
    const std::uint32_t outW = std::max<std::uint32_t>(1u, std::min(copy.dstWidth, srcW));
    const std::uint32_t outH = std::max<std::uint32_t>(1u, std::min(copy.dstHeight, srcH));
    if (srcW > 2048u || srcH > 2048u || outW > 2048u || outH > 2048u) return false;
    const std::size_t srcPixels = static_cast<std::size_t>(srcW) * srcH;
    if (srcPixels > SIZE_MAX / 4u) return false;
    const std::size_t srcBytes = srcPixels * 4u;
    const std::size_t outPixels = static_cast<std::size_t>(outW) * outH;
    if (outPixels > SIZE_MAX / 4u) return false;
    const std::size_t outBytes = outPixels * 4u;

    // M12.6 hardware crashed in SceGxm while the GPU-resident path switched FBOs
    // and rebuilt vitaGL's scissor mask. Keep that path as an explicit A/B build;
    // M12.7 defaults to the synchronous readback/upload path for correctness.
    {
        auto gpuIt = g_efbCopies.find(copy.destinationId);
        const gfx::Handle gpuExisting = gpuIt == g_efbCopies.end() ? gfx::InvalidHandle : gpuIt->second.handle;
        std::uint32_t residentW = outW;
        std::uint32_t residentH = outH;
        bool nativeResolutionResident = false;
#if MKW_VITA_EFB_NATIVE_RES_COPY
        if (srcW != outW || srcH != outH) {
            std::uint32_t existingW = 0, existingH = 0;
            const bool existingKnown = gpuExisting != gfx::InvalidHandle &&
                g_renderer->efb().dimensions(gpuExisting, existingW, existingH);
            if (existingKnown && existingW == srcW && existingH == srcH) {
                // Keep an already-promoted backing at native capture resolution.
                residentW = srcW;
                residentH = srcH;
                nativeResolutionResident = true;
            } else if (!existingKnown) {
                const std::size_t nativeAlignedW =
                    (static_cast<std::size_t>(srcW) + 7u) & ~std::size_t{7u};
                const std::size_t nativeRequired = nativeAlignedW * static_cast<std::size_t>(srcH) * 4u;
                const std::size_t currentBytes = g_renderer->efb().bytes();
                if (nativeRequired <= kEfbBudgetBytes && currentBytes <= kEfbBudgetBytes - nativeRequired) {
                    residentW = srcW;
                    residentH = srcH;
                    nativeResolutionResident = true;
                } else {
                    ++g_packetPerf.efbNativeBudgetFallbacks;
                }
            }
        }
#endif
        bool needsBackingAllocation = gpuExisting == gfx::InvalidHandle;
        if (!needsBackingAllocation) {
            std::uint32_t existingW = 0, existingH = 0;
            needsBackingAllocation =
                !g_renderer->efb().dimensions(gpuExisting, existingW, existingH) ||
                existingW != residentW || existingH != residentH;
        }
        if (needsBackingAllocation) {
            const std::size_t alignedW = (static_cast<std::size_t>(residentW) + 7u) & ~std::size_t{7u};
            const std::size_t requiredBytes = alignedW * static_cast<std::size_t>(residentH) * 4u;
            const std::size_t currentBytes = g_renderer->efb().bytes();
            if (requiredBytes > kEfbBudgetBytes ||
                currentBytes > kEfbBudgetBytes - requiredBytes) {
                ++g_efbAllocationBlocked;
                g_efbAllocationBlockedBytes += requiredBytes;
                if (g_efbAllocationBlocked == 1u ||
                    (g_efbAllocationBlocked & (g_efbAllocationBlocked - 1u)) == 0u) {
                    RT_LOGF(RT_TAG_GX,
                            "m12_5_efb_budget blocked=%llu dest=0x%llX dst=%ux%u required=%llu "
                            "efb_bytes=%llu budget=%llu entries=%u\n",
                            static_cast<unsigned long long>(g_efbAllocationBlocked),
                            static_cast<unsigned long long>(copy.destinationId), residentW, residentH,
                            static_cast<unsigned long long>(requiredBytes),
                            static_cast<unsigned long long>(currentBytes),
                            static_cast<unsigned long long>(kEfbBudgetBytes),
                            static_cast<unsigned>(g_renderer->efb().entries()));
                }
                if (copy.clear) {
                    g_renderer->clear_current({copy.clearR, copy.clearG, copy.clearB, copy.clearA},
                                              copy.clearDepthValue, copy.clearColor,
                                              copy.clearAlpha, copy.clearDepth);
                }
                return false;
            }
        }
#if MKW_VITA_EFB_RESIDENT_COPY
        gfx::EfbManager::ResidentCopyStats resident{};
        const gfx::Handle residentHandle = g_renderer->efb().capture_resident(
            gpuExisting, x0, y0, srcW, srcH, residentW, residentH,
            MKW_VITA_EFB_READBACK_FLIP_Y != 0, resident);
        g_packetPerf.efbSyncUs += resident.syncUs;
        g_packetPerf.efbResidentUs += resident.copyUs;
        switch (resident.path) {
        case gfx::EfbManager::ResidentPath::GpuSameSize: ++g_packetPerf.efbGpuSameSize; break;
        case gfx::EfbManager::ResidentPath::GpuResize: ++g_packetPerf.efbGpuResize; break;
        case gfx::EfbManager::ResidentPath::CpuCopy: ++g_packetPerf.efbCpuCopy; break;
        case gfx::EfbManager::ResidentPath::CpuResize: ++g_packetPerf.efbCpuResize; break;
        case gfx::EfbManager::ResidentPath::None: break;
        }
        switch (resident.fallbackReason) {
        case gfx::EfbManager::ResidentFallbackReason::InvalidSource: ++g_packetPerf.efbFallbackInvalidSource; break;
        case gfx::EfbManager::ResidentFallbackReason::UnsupportedSurface: ++g_packetPerf.efbFallbackUnsupportedSurface; break;
        case gfx::EfbManager::ResidentFallbackReason::ExistingSizeMismatch: ++g_packetPerf.efbFallbackExistingSize; break;
        case gfx::EfbManager::ResidentFallbackReason::AllocationFailed: ++g_packetPerf.efbFallbackAllocation; break;
        case gfx::EfbManager::ResidentFallbackReason::TextureBackingInvalid: ++g_packetPerf.efbFallbackBacking; break;
        case gfx::EfbManager::ResidentFallbackReason::GpuTransferFailed: ++g_packetPerf.efbFallbackTransfer; break;
        case gfx::EfbManager::ResidentFallbackReason::GpuResizeUnavailable: ++g_packetPerf.efbFallbackResizeUnavailable; break;
        case gfx::EfbManager::ResidentFallbackReason::CpuCopyFailed: ++g_packetPerf.efbFallbackCpu; break;
        case gfx::EfbManager::ResidentFallbackReason::None: break;
        }
#if MKW_VITA_PERF_LOG
        const std::uint64_t residentTrace = ++g_efbCopyTraceCount;
        if (residentTrace <= 32u) {
            RT_LOGF(RT_TAG_GX,
                    "efb_copy_exec n=%llu path=resident result=%u native=%u src=%d,%d %ux%u logical=%ux%u backing=%ux%u rpath=%u reason=%u copy_us=%llu sync_us=%llu\n",
                    static_cast<unsigned long long>(residentTrace),
                    static_cast<unsigned>(residentHandle != gfx::InvalidHandle),
                    static_cast<unsigned>(nativeResolutionResident), x0, y0,
                    static_cast<unsigned>(srcW), static_cast<unsigned>(srcH),
                    static_cast<unsigned>(outW), static_cast<unsigned>(outH),
                    static_cast<unsigned>(residentW), static_cast<unsigned>(residentH),
                    static_cast<unsigned>(resident.path),
                    static_cast<unsigned>(resident.fallbackReason),
                    static_cast<unsigned long long>(resident.copyUs),
                    static_cast<unsigned long long>(resident.syncUs));
        }
#endif
        // Allocation/binding changes invalidate the renderer's texture mirror,
        // even when resident capture falls back to the reference path.
        g_renderer->invalidate_draw_state();
        g_compactState.valid = false;
        g_lastPipelineHash = g_lastPipelineKey = 0;
        if (residentHandle != gfx::InvalidHandle) {
            const std::uint32_t revision = gpuIt == g_efbCopies.end() ? 0u : gpuIt->second.revision;
            g_efbCopies[copy.destinationId] = EfbCopyTexture{residentHandle, residentW, residentH, revision + 1u};
            if (resident.gpu) ++g_frameEfbGpuCopies;
            else ++g_packetPerf.efbResidentScaled;
            if (nativeResolutionResident) ++g_packetPerf.efbNativeResCopies;
            if (copy.clear) {
                g_renderer->clear_current({copy.clearR, copy.clearG, copy.clearB, copy.clearA},
                    copy.clearDepthValue, copy.clearColor, copy.clearAlpha, copy.clearDepth);
            }
            return true;
        }
        ++g_packetPerf.efbResidentFailures;
#endif
#if MKW_VITA_EFB_GPU_BLIT
        const std::uint32_t gpuRevision =
            gpuIt == g_efbCopies.end() ? 0u : gpuIt->second.revision;
        const gfx::Scissor gpuSrc{x0, y0, static_cast<std::int32_t>(srcW), static_cast<std::int32_t>(srcH)};
        const gfx::Handle gpuCaptured =
            g_renderer->capture_current(gpuExisting, gpuSrc, outW, outH, format);
        if (gpuCaptured != gfx::InvalidHandle) {
            ++g_frameEfbGpuCopies;
            g_efbCopies[copy.destinationId] = EfbCopyTexture{gpuCaptured, outW, outH, gpuRevision + 1u};
            if (copy.clear) {
                g_renderer->clear_current({copy.clearR, copy.clearG, copy.clearB, copy.clearA},
                                          copy.clearDepthValue, copy.clearColor, copy.clearAlpha, copy.clearDepth);
            }
#if MKW_VITA_PERF_LOG
            const std::uint64_t trace = ++g_efbCopyTraceCount;
            if (trace <= 32u) {
                RT_LOGF(RT_TAG_GX,
                        "efb_copy_exec n=%llu path=gpu dest=0x%llX src=%d,%d %ux%u dst=%ux%u fmt=0x%X clear=%u\n",
                        static_cast<unsigned long long>(trace),
                        static_cast<unsigned long long>(copy.destinationId), x0, y0,
                        static_cast<unsigned>(srcW), static_cast<unsigned>(srcH),
                        static_cast<unsigned>(outW), static_cast<unsigned>(outH),
                        static_cast<unsigned>(copy.format), static_cast<unsigned>(copy.clear));
            }
#endif
            g_lastPipelineHash = 0;
            g_lastPipelineKey = 0;
            return true;
        }
#endif
    }

    // Conservative synchronous path. It is the M12.7 default and also the fallback
    // for GPU-blit A/B builds when vitaGL cannot capture the requested rectangle.
#if MKW_VITA_PERF_LOG
    const std::uint64_t trace = ++g_efbCopyTraceCount;
    if (trace <= 32u) {
        RT_LOGF(RT_TAG_GX,
                "efb_copy_exec n=%llu path=readback dest=0x%llX src=%d,%d %ux%u dst=%ux%u fmt=0x%X clear=%u\n",
                static_cast<unsigned long long>(trace),
                static_cast<unsigned long long>(copy.destinationId), x0, y0,
                static_cast<unsigned>(srcW), static_cast<unsigned>(srcH),
                static_cast<unsigned>(outW), static_cast<unsigned>(outH),
                static_cast<unsigned>(copy.format), static_cast<unsigned>(copy.clear));
    }
#endif

    // glReadPixels uses a bottom-left origin. copy.srcY is top-left GX/Aurora space.
    const std::int32_t glY = targetH - y1;
    const std::uint64_t readbackBeginUs = sceKernelGetProcessTimeWide();
    const std::uint8_t* readbackPixels = nullptr;
    GLenum readbackError = GL_NO_ERROR;
#if MKW_VITA_EFB_TRANSFER_READBACK
    if (g_efbMappedReadbackBytes < srcBytes) {
        if (g_efbMappedReadback) vglFree(g_efbMappedReadback);
        g_efbMappedReadback = vglMemalign(64u, static_cast<std::uint32_t>(srcBytes));
        g_efbMappedReadbackBytes = g_efbMappedReadback ? srcBytes : 0u;
    }
    if (g_efbMappedReadback) {
        // End/submit the current GL scene but deliberately avoid sceGxmFinish;
        // vglReadPixels performs a mapped sceGxmTransfer and waits for that transfer.
        // This is an explicit A/B speedhack because upstream warns about stale/glitched reads.
        while (glGetError() != GL_NO_ERROR) {}
        glFlush();
        vglReadPixels(x0, glY, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, g_efbMappedReadback);
        readbackError = glGetError();
        if (readbackError == GL_NO_ERROR) {
            readbackPixels = static_cast<const std::uint8_t*>(g_efbMappedReadback);
            ++g_frameEfbTransferReadbacks;
        }
    }
#endif
    if (!readbackPixels) {
        try {
            if (g_efbReadback.size() < srcBytes) g_efbReadback.resize(srcBytes);
        } catch (...) {
            g_packetPerf.efbReadbackUs += sceKernelGetProcessTimeWide() - readbackBeginUs;
            return false;
        }
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(x0, glY, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, g_efbReadback.data());
        readbackError = glGetError();
        if (readbackError == GL_NO_ERROR) readbackPixels = g_efbReadback.data();
    }
    g_packetPerf.efbReadbackUs += sceKernelGetProcessTimeWide() - readbackBeginUs;
    if (readbackError != GL_NO_ERROR || !readbackPixels) return false;

    const std::uint8_t* uploadPixels = readbackPixels;
    const bool needsScale = outW != srcW || outH != srcH;
    const bool needsFlip = MKW_VITA_EFB_READBACK_FLIP_Y != 0;
    if (needsScale || needsFlip) {
        const std::uint64_t scaleBeginUs = sceKernelGetProcessTimeWide();
        try {
            if (g_efbUpload.size() < outBytes) g_efbUpload.resize(outBytes);
        } catch (...) {
            g_packetPerf.efbScaleUs += sceKernelGetProcessTimeWide() - scaleBeginUs;
            return false;
        }
        if (!PrepareNearestMap(g_efbYMap, srcH, outH, g_efbYMapSrc, g_efbYMapDst) ||
            (outW != srcW &&
             !PrepareNearestMap(g_efbXMap, srcW, outW, g_efbXMapSrc, g_efbXMapDst))) {
            g_packetPerf.efbScaleUs += sceKernelGetProcessTimeWide() - scaleBeginUs;
            return false;
        }
        for (std::uint32_t y = 0; y < outH; ++y) {
            const std::uint32_t mappedY = g_efbYMap[y];
            const std::uint32_t sy = needsFlip ? (srcH - 1u - mappedY) : mappedY;
            const std::uint8_t* srcRow =
                readbackPixels + static_cast<std::size_t>(sy) * srcW * 4u;
            std::uint8_t* dstRow =
                g_efbUpload.data() + static_cast<std::size_t>(y) * outW * 4u;
            if (outW == srcW) {
                std::memcpy(dstRow, srcRow, static_cast<std::size_t>(outW) * 4u);
                continue;
            }
            for (std::uint32_t x = 0; x < outW; ++x) {
                std::uint32_t pixel = 0;
                std::memcpy(&pixel, srcRow + static_cast<std::size_t>(g_efbXMap[x]) * 4u,
                            sizeof(pixel));
                std::memcpy(dstRow + static_cast<std::size_t>(x) * 4u, &pixel, sizeof(pixel));
            }
        }
        uploadPixels = g_efbUpload.data();
        g_packetPerf.efbScaleUs += sceKernelGetProcessTimeWide() - scaleBeginUs;
    }

    // The UI copies observed so far are color previews. Preserve the rendered RGBA directly;
    // format-specific Wii RAM packing is unnecessary because this GPU-only result is consumed
    // as a sampled texture and is never exposed to guest CPU reads on this path.
    auto it = g_efbCopies.find(copy.destinationId);
    const gfx::Handle existing = it == g_efbCopies.end() ? gfx::InvalidHandle : it->second.handle;
    const std::uint32_t revision = it == g_efbCopies.end() ? 0u : it->second.revision;
    const std::uint64_t uploadBeginUs = sceKernelGetProcessTimeWide();
    const gfx::Handle captured =
        g_renderer->upload_efb_rgba(existing, outW, outH, uploadPixels);
    g_packetPerf.efbUploadUs += sceKernelGetProcessTimeWide() - uploadBeginUs;
    if (captured == gfx::InvalidHandle) {
        if (it != g_efbCopies.end()) {
            if (it->second.handle != gfx::InvalidHandle) g_renderer->efb().destroy(it->second.handle);
            g_efbCopies.erase(it);
        }
        return false;
    }
    g_efbCopies[copy.destinationId] = EfbCopyTexture{captured, outW, outH, revision + 1u};
    ++g_frameEfbReadbackCopies;

    if (copy.clear) {
        g_renderer->clear_current({copy.clearR, copy.clearG, copy.clearB, copy.clearA},
                                  copy.clearDepthValue, copy.clearColor, copy.clearAlpha, copy.clearDepth);
    }
    g_lastPipelineHash = 0;
    g_lastPipelineKey = 0;
    return true;
}

void AuroraPacketRendererDestroyEfbCopy(std::uint64_t destinationId) noexcept {
    if (destinationId == 0) return;
    auto it = g_efbCopies.find(destinationId);
    if (it == g_efbCopies.end()) return;
    // Draws queued before the guest invalidation may still reference this handle.
    // Execute them before retiring it so FIFO ordering remains intact.
    if (g_frameActive && !FlushQueuedDraws()) return;
    if (g_renderer && it->second.handle != gfx::InvalidHandle) {
        g_renderer->efb().destroy(it->second.handle);
    }
    g_efbCopies.erase(it);
    g_lastPipelineHash = 0;
    g_lastPipelineKey = 0;
    g_compactState.valid = false;
}

AuroraPacketFrameStats AuroraPacketRendererEndFrame() noexcept {
    AuroraPacketFrameStats result{};
    if (!g_frameActive || !g_renderer || !g_arena) return result;
#if MKW_VITA_PERF_INJECT_CLIP_TRIANGLE
    // M13.6: submit one known clip-space primitive through the exact same
    // compact VBO/index/pipeline path as Wii geometry. Put it last so no guest
    // UI draw can cover it. If this is not visible, the fault is downstream of
    // guest transforms/state capture (stream/layout/shader/draw-elements).
    static const std::array<AuroraPacketVertex, 3> kClipTriangle{{
        {-0.78f, -0.72f, 0.0f, 255u, 0u, 255u, 255u, 0.0f, 0.0f},
        { 0.78f, -0.72f, 0.0f, 255u, 0u, 255u, 255u, 1.0f, 0.0f},
        { 0.00f,  0.78f, 0.0f, 255u, 0u, 255u, 255u, 0.5f, 1.0f},
    }};
    AuroraPacketDraw probe{};
    probe.vertices = kClipTriangle.data();
    probe.vertexCount = static_cast<std::uint32_t>(kClipTriangle.size());
    probe.primitive = static_cast<std::uint32_t>(GX_TRIANGLES);
    probe.depthFunc = static_cast<std::uint32_t>(GX_ALWAYS);
    probe.cullMode = static_cast<std::uint32_t>(GX_CULL_NONE);
    probe.blendMode = static_cast<std::uint32_t>(GX_BM_NONE);
    probe.blendSrc = static_cast<std::uint32_t>(GX_BL_ONE);
    probe.blendDst = static_cast<std::uint32_t>(GX_BL_ZERO);
    probe.logicOp = static_cast<std::uint32_t>(GX_LO_COPY);
    probe.alphaComp0 = static_cast<std::uint32_t>(GX_ALWAYS);
    probe.alphaComp1 = static_cast<std::uint32_t>(GX_ALWAYS);
    probe.alphaOp = static_cast<std::uint32_t>(GX_AOP_AND);
    probe.depthCompare = false;
    probe.depthUpdate = false;
    probe.colorUpdate = true;
    probe.alphaUpdate = true;
    probe.viewportX = 0.0f;
    probe.viewportY = 0.0f;
    probe.viewportWidth = 960.0f;
    probe.viewportHeight = 544.0f;
    probe.viewportNear = 0.0f;
    probe.viewportFar = 1.0f;
    probe.scissorX = 0;
    probe.scissorY = 0;
    probe.scissorWidth = 960;
    probe.scissorHeight = 544;
    const AuroraPacketSubmitResult probeResult = AuroraPacketRendererSubmit(probe);
    static bool s_probeLogged = false;
    if (!s_probeLogged) {
        RT_LOGF(RT_TAG_GX, "perf_probe clip_triangle submit=%u err=%u compact=%u\n",
                static_cast<unsigned>(probeResult.submitted),
                static_cast<unsigned>(probeResult.prepareError),
                static_cast<unsigned>(MKW_VITA_COMPACT_VERTEX));
        s_probeLogged = true;
    }
#endif
#if MKW_VITA_PERF_INJECT_WII_TRIANGLE
    static std::uint32_t s_wiiPerspectiveStatsLogs = 0;
    const bool logPerspectiveStats = g_wiiTriangleProbe.perspectiveDraws != 0u &&
        (s_wiiPerspectiveStatsLogs < 32u ||
         (s_wiiPerspectiveStatsLogs & (s_wiiPerspectiveStatsLogs - 1u)) == 0u);
    const bool logNoPerspectiveHeartbeat = g_packetPerf.logicalSubmits != 0u &&
        g_wiiTriangleProbe.perspectiveDraws == 0u && (g_wiiTriangleProbe.serial % 120u) == 0u;
    if (logPerspectiveStats || logNoPerspectiveHeartbeat) {
        const float minX = g_wiiTriangleProbe.finiteVertices ? g_wiiTriangleProbe.minX : 0.0f;
        const float maxX = g_wiiTriangleProbe.finiteVertices ? g_wiiTriangleProbe.maxX : 0.0f;
        const float minY = g_wiiTriangleProbe.finiteVertices ? g_wiiTriangleProbe.minY : 0.0f;
        const float maxY = g_wiiTriangleProbe.finiteVertices ? g_wiiTriangleProbe.maxY : 0.0f;
        const float minZ = g_wiiTriangleProbe.finiteVertices ? g_wiiTriangleProbe.minZ : 0.0f;
        const float maxZ = g_wiiTriangleProbe.finiteVertices ? g_wiiTriangleProbe.maxZ : 0.0f;
        RT_LOGF(RT_TAG_GX,
                "perf_probe wii_tri_stats serial=%llu submits=%u persp=%u verts=%u finite_v=%u "
                "tris=%u finite_t=%u nondeg=%u intersect=%u inside=%u "
                "xy=(%.3f..%.3f,%.3f..%.3f) z=(%.3f..%.3f) area_any=%.8f area_intersect=%.8f area_inside=%.8f\n",
                static_cast<unsigned long long>(g_wiiTriangleProbe.serial),
                g_packetPerf.logicalSubmits, g_wiiTriangleProbe.perspectiveDraws,
                g_wiiTriangleProbe.perspectiveVertices, g_wiiTriangleProbe.finiteVertices,
                g_wiiTriangleProbe.triangles, g_wiiTriangleProbe.finiteTriangles,
                g_wiiTriangleProbe.nonDegenerate, g_wiiTriangleProbe.intersectsViewport,
                g_wiiTriangleProbe.fullyInside, minX, maxX, minY, maxY, minZ, maxZ,
                g_wiiTriangleProbe.maxAnyArea2, g_wiiTriangleProbe.maxVisibleArea2,
                g_wiiTriangleProbe.maxInsideArea2);
        if (g_wiiTriangleProbe.perspectiveDraws != 0u) ++s_wiiPerspectiveStatsLogs;
    }

    std::array<AuroraPacketVertex, 3> visual{};
    const char* visualMode = "none";
    bool injectVisual = false;
    if (g_wiiTriangleProbe.insideValid) {
        visual = g_wiiTriangleProbe.bestInside;
        visualMode = "inside";
        injectVisual = true;
    } else if (g_wiiTriangleProbe.visibleValid) {
        visual = g_wiiTriangleProbe.bestVisible;
        visualMode = "intersect";
        injectVisual = true;
    } else if (g_wiiTriangleProbe.anyValid) {
        visual = g_wiiTriangleProbe.bestAny;
        const float minX = std::min({visual[0].x, visual[1].x, visual[2].x});
        const float maxX = std::max({visual[0].x, visual[1].x, visual[2].x});
        const float minY = std::min({visual[0].y, visual[1].y, visual[2].y});
        const float maxY = std::max({visual[0].y, visual[1].y, visual[2].y});
        const float span = std::max(maxX - minX, maxY - minY);
        if (std::isfinite(span) && span > 1.0e-7f) {
            const float cx = (minX + maxX) * 0.5f;
            const float cy = (minY + maxY) * 0.5f;
            const float scale = 1.45f / span;
            for (AuroraPacketVertex& vertex : visual) {
                vertex.x = (vertex.x - cx) * scale;
                vertex.y = (vertex.y - cy) * scale;
            }
            visualMode = "fitted";
            injectVisual = true;
        }
    }

    if (injectVisual) {
        for (AuroraPacketVertex& vertex : visual) {
            vertex.z = 0.0f;
            vertex.r = 0u;
            vertex.g = 255u;
            vertex.b = 255u;
            vertex.a = 255u;
        }
        AuroraPacketDraw probe{};
        probe.vertices = visual.data();
        probe.vertexCount = 3u;
        probe.primitive = static_cast<std::uint32_t>(GX_TRIANGLES);
        probe.depthFunc = static_cast<std::uint32_t>(GX_ALWAYS);
        probe.cullMode = static_cast<std::uint32_t>(GX_CULL_NONE);
        probe.blendMode = static_cast<std::uint32_t>(GX_BM_NONE);
        probe.blendSrc = static_cast<std::uint32_t>(GX_BL_ONE);
        probe.blendDst = static_cast<std::uint32_t>(GX_BL_ZERO);
        probe.logicOp = static_cast<std::uint32_t>(GX_LO_COPY);
        probe.alphaComp0 = static_cast<std::uint32_t>(GX_ALWAYS);
        probe.alphaComp1 = static_cast<std::uint32_t>(GX_ALWAYS);
        probe.alphaOp = static_cast<std::uint32_t>(GX_AOP_AND);
        probe.depthCompare = false;
        probe.depthUpdate = false;
        probe.colorUpdate = true;
        probe.alphaUpdate = true;
        probe.viewportX = 0.0f;
        probe.viewportY = 0.0f;
        probe.viewportWidth = 960.0f;
        probe.viewportHeight = 544.0f;
        probe.viewportNear = 0.0f;
        probe.viewportFar = 1.0f;
        probe.scissorX = 0;
        probe.scissorY = 0;
        probe.scissorWidth = 960;
        probe.scissorHeight = 544;
        const AuroraPacketSubmitResult probeResult = AuroraPacketRendererSubmit(probe);
        static std::uint32_t s_wiiProbeLogs = 0;
        if (s_wiiProbeLogs < 16u || (s_wiiProbeLogs & (s_wiiProbeLogs - 1u)) == 0u) {
            RT_LOGF(RT_TAG_GX,
                    "perf_probe wii_triangle serial=%llu mode=%s submit=%u err=%u tris=%u nondeg=%u "
                    "intersect=%u inside=%u max_area2=%.8f "
                    "a=(%.3f,%.3f) b=(%.3f,%.3f) c=(%.3f,%.3f)\n",
                    static_cast<unsigned long long>(g_wiiTriangleProbe.serial), visualMode,
                    static_cast<unsigned>(probeResult.submitted),
                    static_cast<unsigned>(probeResult.prepareError),
                    g_wiiTriangleProbe.triangles, g_wiiTriangleProbe.nonDegenerate,
                    g_wiiTriangleProbe.intersectsViewport, g_wiiTriangleProbe.fullyInside,
                    g_wiiTriangleProbe.maxAnyArea2,
                    visual[0].x, visual[0].y, visual[1].x, visual[1].y,
                    visual[2].x, visual[2].y);
        }
        ++s_wiiProbeLogs;
    }
#endif
    const uint64_t flushBeginUs = sceKernelGetProcessTimeWide();
    if (g_arena->flush()) g_renderer->execute(g_stream);
    g_packetPerf.flushExecuteUs += sceKernelGetProcessTimeWide() - flushBeginUs;
    g_renderer->end_frame();
    const gfx::FrameStats& stats = g_renderer->stats();
    result.physicalDrawCalls = stats.drawCalls;
    result.triangles = stats.triangles;
    result.pipelineHits = stats.pipelineHits;
    result.pipelineMisses = stats.pipelineMisses;
    result.stateChanges = stats.stateChanges;
    result.vertexBytes = g_arena->vertex_used();
    result.indexBytes = g_arena->index_used();
    result.vertexOverflows = g_arena->vertex_overflows();
    result.indexOverflows = g_arena->index_overflows();
    const auto& textureCache = g_renderer->textures();
    result.textureBytes = textureCache.bytes();
    result.textureHighWaterBytes = textureCache.high_water_bytes();
    result.textureBudgetBytes = textureCache.budget();
    result.textureEvictions = textureCache.evictions();
    result.textureEntries = static_cast<std::uint32_t>(textureCache.entries());
    result.textureAllocFailTotal = textureCache.alloc_fail_total();
    result.texturePreEvictions = textureCache.pre_evictions();
    result.texturePreEvictedBytes = textureCache.pre_evicted_bytes();
    result.textureRequestedBytes = textureCache.last_requested_bytes();
    result.textureEvictBlocked = textureCache.evict_blocked();
    result.textureProtectedBytes = textureCache.protected_bytes();
    result.textureProtectedHighWaterBytes = textureCache.protected_bytes_high_water();
    result.textureAllocRetry = g_packetPerf.textureSafeRetryAttempts;
    result.textureAllocRetrySuccess = g_packetPerf.textureSafeRetrySuccesses;
    result.textureAllocFailAfterEvict = g_packetPerf.textureSafeRetryFailAfter;
    result.textureAllocRetryWaitUs = g_packetPerf.textureSafeRetryWaitUs;
    const auto& efb = g_renderer->efb();
    result.efbBytes = efb.bytes();
    result.efbHighWaterBytes = efb.high_water_bytes();
    result.efbEntries = static_cast<std::uint32_t>(efb.entries());
    result.efbAllocationBlocked = g_efbAllocationBlocked;
    result.efbAllocationBlockedBytes = g_efbAllocationBlockedBytes;
    result.efbBudgetBytes = kEfbBudgetBytes;
    result.efbGpuCopies = g_frameEfbGpuCopies;
    result.efbReadbackCopies = g_frameEfbReadbackCopies;
    result.efbTransferReadbacks = g_frameEfbTransferReadbacks;
    result.efbResidentScaled = g_packetPerf.efbResidentScaled;
    result.efbResidentUs = g_packetPerf.efbResidentUs;
    result.efbGpuSameSize = g_packetPerf.efbGpuSameSize;
    result.efbGpuResize = g_packetPerf.efbGpuResize;
    result.efbCpuCopy = g_packetPerf.efbCpuCopy;
    result.efbCpuResize = g_packetPerf.efbCpuResize;
    result.efbResidentFailures = g_packetPerf.efbResidentFailures;
    result.efbNativeResCopies = g_packetPerf.efbNativeResCopies;
    result.efbNativeBudgetFallbacks = g_packetPerf.efbNativeBudgetFallbacks;
    result.efbFallbackInvalidSource = g_packetPerf.efbFallbackInvalidSource;
    result.efbFallbackUnsupportedSurface = g_packetPerf.efbFallbackUnsupportedSurface;
    result.efbFallbackExistingSize = g_packetPerf.efbFallbackExistingSize;
    result.efbFallbackAllocation = g_packetPerf.efbFallbackAllocation;
    result.efbFallbackBacking = g_packetPerf.efbFallbackBacking;
    result.efbFallbackTransfer = g_packetPerf.efbFallbackTransfer;
    result.efbFallbackResizeUnavailable = g_packetPerf.efbFallbackResizeUnavailable;
    result.efbFallbackCpu = g_packetPerf.efbFallbackCpu;
    result.streamReuseWaitUs = g_arena->reuse_wait_us();
    result.logicalSubmits = g_packetPerf.logicalSubmits;
    result.compactDraws = g_packetPerf.compactDraws;
    result.compactFallbacks = g_packetPerf.compactFallbacks;
    result.batchMerges = g_packetPerf.batchMerges;
    result.compactRunStarts = g_packetPerf.compactRunStarts;
    result.compactRunExtends = g_packetPerf.compactRunExtends;
    result.compactStateHits = g_packetPerf.compactStateHits;
    result.compactStateMisses = g_packetPerf.compactStateMisses;
    result.indexBuildUs = g_packetPerf.indexBuildUs;
    result.vertexPackUs = g_packetPerf.vertexPackUs;
    result.textureResolveUs = g_packetPerf.textureResolveUs;
    result.pipelineResolveUs = g_packetPerf.pipelineResolveUs;
    result.streamWriteUs = g_packetPerf.streamWriteUs;
    result.flushExecuteUs = g_packetPerf.flushExecuteUs;
    result.efbSyncUs = g_packetPerf.efbSyncUs;
    result.efbReadbackUs = g_packetPerf.efbReadbackUs;
    result.efbScaleUs = g_packetPerf.efbScaleUs;
    result.efbUploadUs = g_packetPerf.efbUploadUs;
    g_frameActive = false;
    return result;
}

} // namespace WiiCompiledVita
