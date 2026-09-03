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
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

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

struct EfbCopyTexture {
    gfx::Handle handle = gfx::InvalidHandle;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t revision = 0;
};

std::unordered_map<std::uint64_t, EfbCopyTexture> g_efbCopies;
std::uint64_t g_efbSourceMissTraceCount = 0;
std::vector<std::uint8_t> g_efbReadback;
std::uint64_t g_efbCopyTraceCount = 0;
std::vector<std::uint8_t> g_thpRgba;
std::uint64_t g_thpConversionKey = 0;
std::uint64_t g_thpConversionCount = 0;

bool FlushQueuedDraws() noexcept {
    if (!g_frameActive || !g_renderer || !g_arena) return false;
    if (g_stream.commands().empty()) return true;
    if (!g_arena->flush()) return false;
    g_renderer->execute(g_stream);
    g_stream.reset();
    g_stream.reserve(193);
    // capture/clear invalidates renderer bindings; force the next submit to
    // resolve its cached pipeline binding again while retaining the pipeline object.
    g_lastPipelineHash = 0;
    g_lastPipelineKey = 0;
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
        const std::size_t rgbaBytes = static_cast<std::size_t>(input.width) * input.height * 4u;
        g_thpRgba.resize(rgbaBytes);
        const auto* yPlane = static_cast<const std::uint8_t*>(input.data);
        const auto* uPlane = static_cast<const std::uint8_t*>(input.thpUData);
        const auto* vPlane = static_cast<const std::uint8_t*>(input.thpVData);
        for (std::uint32_t y = 0; y < input.height; ++y) {
            for (std::uint32_t x = 0; x < input.width; ++x) {
                const int yy = static_cast<int>(ReadTiledI8(yPlane, input.width, x, y)) - 16;
                const int uu = static_cast<int>(ReadTiledI8(
                    uPlane, input.thpChromaWidth, x >> 1u, y >> 1u)) - 128;
                const int vv = static_cast<int>(ReadTiledI8(
                    vPlane, input.thpChromaWidth, x >> 1u, y >> 1u)) - 128;
                const int c = std::max(0, yy);
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * input.width + x) * 4u;
                g_thpRgba[offset + 0u] = ClampThpColor((298 * c + 409 * vv + 128) >> 8);
                g_thpRgba[offset + 1u] = ClampThpColor((298 * c - 100 * uu - 208 * vv + 128) >> 8);
                g_thpRgba[offset + 2u] = ClampThpColor((298 * c + 516 * uu + 128) >> 8);
                g_thpRgba[offset + 3u] = 255u;
            }
        }
        g_thpConversionKey = key;
        ++g_thpConversionCount;
        if (g_thpConversionCount <= 24u) {
            RT_LOGF(RT_TAG_GX,
                    "thp_yuv420 n=%llu size=%ux%u y=0x%llX u=0x%llX v=0x%llX gen=%llu/%llu/%llu\n",
                    static_cast<unsigned long long>(g_thpConversionCount),
                    static_cast<unsigned>(input.width), static_cast<unsigned>(input.height),
                    static_cast<unsigned long long>(input.sourceId),
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(input.thpUData)),
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(input.thpVData)),
                    static_cast<unsigned long long>(input.sourceGeneration),
                    static_cast<unsigned long long>(input.thpUGeneration),
                    static_cast<unsigned long long>(input.thpVGeneration));
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
    rendererConfig.textureBudget = 12u * 1024u * 1024u;
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
    arenaConfig.vertexBytes = 512u * 1024u;
    arenaConfig.indexBytes = 64u * 1024u;
    arenaConfig.slots = 3;
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
    g_stream.reset();
    g_stream.reserve(193);
    g_viewport = {viewportX, viewportY, viewportWidth, viewportHeight, 0.0f, 1.0f};
    g_scissor = {scissorX, scissorY, scissorWidth, scissorHeight};
    g_lastPipelineHash = 0;
    g_lastPipelineKey = 0;
    g_renderer->clear_current({0.015f, 0.02f, 0.035f, 1.0f}, 1.0f, true, true, true);
    g_frameActive = true;
    return true;
}

AuroraPacketSubmitResult AuroraPacketRendererSubmit(const AuroraPacketDraw& draw) noexcept {
    AuroraPacketSubmitResult result{};
    if (!g_frameActive || !g_renderer || !g_arena || !draw.vertices || draw.vertexCount == 0) return result;

    gfx::PreparedDraw& prepared = g_prepared;
    prepared.error = gfx::PrepareDrawError::None;
    prepared.vertices.clear();
    prepared.indices.clear();
    prepared.primitive = gfx::Primitive::Triangles;
    prepared.positionIsClipSpace = true;
    if (!BuildIndices(prepared.indices, draw.primitive, draw.vertexCount, prepared.primitive)) return result;
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

    gfx::PipelineDesc pipeline{};
    pipeline.primitive = prepared.primitive;
    pipeline.positionIsClipSpace = true;
    pipeline.layout = gfx::canonical_vertex_layout();
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

    std::array<gfx::TextureBinding, gfx::MaxTextures> bindings{};
    bool textured = false;
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
                RT_LOGF(RT_TAG_GX,
                        "efb_source_miss n=%llu source=0x%llX known=%u sample=0x%llX size=%ux%u\n",
                        static_cast<unsigned long long>(g_efbSourceMissTraceCount),
                        static_cast<unsigned long long>(draw.texture.sourceId),
                        static_cast<unsigned>(g_efbCopies.size()),
                        static_cast<unsigned long long>(sample.first),
                        static_cast<unsigned>(sample.second.width),
                        static_cast<unsigned>(sample.second.height));
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
                bindings[0].texture = g_renderer->create_texture(texture);
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
    ConfigureTev(pipeline, draw.texture.tevMode, textured);

    gfx::DrawUniforms uniforms{};
    uniforms.renderViewportWidth = g_viewport.width;
    if (textured) {
        uniforms.texcoordScale[0] = {static_cast<float>(draw.texture.width),
                                     static_cast<float>(draw.texture.height), 0.0f, 0.0f};
        uniforms.textureSizeBias[0] = {static_cast<float>(draw.texture.width),
                                       static_cast<float>(draw.texture.height), 0.0f, 0.0f};
    }

    const std::uint64_t pipelineHash = gfx::pipeline_key(pipeline);
    if (pipelineHash != g_lastPipelineHash || g_lastPipelineKey == 0) {
        g_lastPipelineKey = gfx::resolve_draw_pipeline(*g_renderer, prepared, pipeline);
        g_lastPipelineHash = pipelineHash;
    }
    gfx::PrepareDrawError error = gfx::PrepareDrawError::None;
    result.submitted = g_lastPipelineKey != 0 &&
        gfx::enqueue_draw(*g_renderer, *g_arena, g_stream, prepared, pipeline, uniforms,
                          g_viewport, g_scissor, bindings, &error, nullptr, g_lastPipelineKey);
    return result;
}

bool AuroraPacketRendererCopyEfb(const AuroraPacketEfbCopy& copy) noexcept {
    if (!g_frameActive || !g_renderer || !g_arena || copy.destinationId == 0 ||
        copy.srcWidth <= 0 || copy.srcHeight <= 0 || copy.dstWidth == 0 || copy.dstHeight == 0) {
        return false;
    }
    if (!FlushQueuedDraws()) return false;

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

    // Prefer the GPU-resident framebuffer blit. This avoids one synchronous readback per
    // preview (the main menu can issue 20+ GXCopyTex operations in one logical frame).
    {
        auto gpuIt = g_efbCopies.find(copy.destinationId);
        const gfx::Handle gpuExisting = gpuIt == g_efbCopies.end() ? gfx::InvalidHandle : gpuIt->second.handle;
        const std::uint32_t gpuRevision = gpuIt == g_efbCopies.end() ? 0u : gpuIt->second.revision;
        const gfx::Scissor gpuSrc{x0, y0, static_cast<std::int32_t>(srcW), static_cast<std::int32_t>(srcH)};
        const gfx::Handle gpuCaptured =
            g_renderer->capture_current(gpuExisting, gpuSrc, outW, outH, format);
        if (gpuCaptured != gfx::InvalidHandle) {
            g_efbCopies[copy.destinationId] = EfbCopyTexture{gpuCaptured, outW, outH, gpuRevision + 1u};
            if (copy.clear) {
                g_renderer->clear_current({copy.clearR, copy.clearG, copy.clearB, copy.clearA},
                                          copy.clearDepthValue, copy.clearColor, copy.clearAlpha, copy.clearDepth);
            }
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
            g_lastPipelineHash = 0;
            g_lastPipelineKey = 0;
            return true;
        }
    }

    // Conservative fallback for any vitaGL/GXM combination that cannot blit the requested
    // rectangle. This path is correct but synchronous and should normally stay unused.
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

    // glReadPixels uses a bottom-left origin. copy.srcY is top-left GX/Aurora space.
    const std::int32_t glY = targetH - y1;
    try {
        if (g_efbReadback.size() < srcBytes) g_efbReadback.resize(srcBytes);
    } catch (...) {
        return false;
    }
    glReadPixels(x0, glY, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, g_efbReadback.data());
    if (glGetError() != GL_NO_ERROR) return false;

    // Nearest downscale in-place. Since outW/outH never exceed srcW/srcH, each destination
    // pixel lies at or before the source pixel it reads, so forward compaction cannot destroy
    // data required by a later sample. Copy through a scalar to make overlap semantics explicit.
    if (outW != srcW || outH != srcH) {
        for (std::uint32_t y = 0; y < outH; ++y) {
            const std::uint32_t sy = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(y) * srcH) / outH);
            for (std::uint32_t x = 0; x < outW; ++x) {
                const std::uint32_t sx = static_cast<std::uint32_t>(
                    (static_cast<std::uint64_t>(x) * srcW) / outW);
                const std::size_t srcOffset = (static_cast<std::size_t>(sy) * srcW + sx) * 4u;
                const std::size_t dstOffset = (static_cast<std::size_t>(y) * outW + x) * 4u;
                std::uint32_t pixel = 0;
                std::memcpy(&pixel, g_efbReadback.data() + srcOffset, sizeof(pixel));
                std::memcpy(g_efbReadback.data() + dstOffset, &pixel, sizeof(pixel));
            }
        }
    }

    // The UI copies observed so far are color previews. Preserve the rendered RGBA directly;
    // format-specific Wii RAM packing is unnecessary because this GPU-only result is consumed
    // as a sampled texture and is never exposed to guest CPU reads on this path.
    auto it = g_efbCopies.find(copy.destinationId);
    const gfx::Handle existing = it == g_efbCopies.end() ? gfx::InvalidHandle : it->second.handle;
    const std::uint32_t revision = it == g_efbCopies.end() ? 0u : it->second.revision;
    const gfx::Handle captured =
        g_renderer->upload_efb_rgba(existing, outW, outH, g_efbReadback.data());
    if (captured == gfx::InvalidHandle) {
        if (it != g_efbCopies.end()) {
            if (it->second.handle != gfx::InvalidHandle) g_renderer->efb().destroy(it->second.handle);
            g_efbCopies.erase(it);
        }
        return false;
    }
    g_efbCopies[copy.destinationId] = EfbCopyTexture{captured, outW, outH, revision + 1u};

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
}

AuroraPacketFrameStats AuroraPacketRendererEndFrame() noexcept {
    AuroraPacketFrameStats result{};
    if (!g_frameActive || !g_renderer || !g_arena) return result;
    if (g_arena->flush()) g_renderer->execute(g_stream);
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
    g_frameActive = false;
    return result;
}

} // namespace WiiCompiledVita
