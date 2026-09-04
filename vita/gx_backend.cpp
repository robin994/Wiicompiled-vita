#include "wiicompiled_vita/gx_backend.h"
#include "wiicompiled_vita/host_thread.h"
#include "guest_stall_watchdog.h"
#include "abi_bridge.h"
#include "runtime_log.h"
#if defined(MKW_VITA_AURORA_RENDERER)
#include "aurora_packet_renderer.h"
#endif

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXBump.h>
#include <dolphin/gx/GXCull.h>
#include <dolphin/gx/GXDispList.h>
#include <dolphin/gx/GXExtra.h>
#include <dolphin/gx/GXFifo.h>
#include <dolphin/gx/GXFrameBuffer.h>
#include <dolphin/gx/GXGeometry.h>
#include <dolphin/gx/GXGet.h>
#include <dolphin/gx/GXLighting.h>
#include <dolphin/gx/GXManage.h>
#include <dolphin/gx/GXPixel.h>
#include <dolphin/gx/GXTev.h>
#include <dolphin/gx/GXTexture.h>
#include <dolphin/gx/GXTransform.h>
#include <dolphin/gx/GXVert.h>

#include <vitaGL.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#ifndef MKW_VITA_EFB_GPU_BLIT
#define MKW_VITA_EFB_GPU_BLIT 0
#endif

#if defined(MKW_VITA_VITAGL_SPEEDHACK)
extern "C" void vglSetupRenderTargetScenesNum(uint8_t displaySize, uint8_t fboSize);
#endif

extern "C" {
struct PsvDebugScreenFont {
    unsigned char* glyphs;
    unsigned char width;
    unsigned char height;
    unsigned char first;
    unsigned char last;
    unsigned char size_w;
    unsigned char size_h;
};
PsvDebugScreenFont* psvDebugScreenGetFont(void);
}

namespace {

using WiiCompiledVita::HostThread;
using WiiCompiledVita::HostThreadRole;

constexpr uint32_t kSurfaceWidth = 960;
constexpr uint32_t kSurfaceHeight = 544;
constexpr size_t kRenderWorkerStack = 128 * 1024;
constexpr int kVitaGlUserRamReserve = 8 * 1024 * 1024;
// M12.6 hardware reached the first race frame with 6154 requested draws, at
// least 36457 requested vertices and exactly 64 recorded EFB commands. Keep a
// bounded full-frame packet with enough headroom to distinguish real renderer
// failures from producer truncation. This raises FramePacket from ~3.78 MiB to
// ~7.18 MiB and all static frame buffers by ~7.16 MiB.
constexpr size_t kMaxFrameVertices = 49152;
constexpr size_t kMaxFrameDraws = 8192;
constexpr size_t kMaxFrameEfbCommands = 128;
constexpr size_t kPnMtxCount = 10;
constexpr u8 kPnMtxExplicitBit = 0x80u;
constexpr size_t kTextureCacheCapacity = 32;
constexpr size_t kMaxTevSignaturesPerFrame = 12;
constexpr size_t kMaxTextureDimension = 1024;
constexpr size_t kTextureScratchBytes = kMaxTextureDimension * kMaxTextureDimension * 4;
constexpr size_t kTextureCacheBudgetBytes = 12 * 1024 * 1024;
constexpr const char* kBootLogPath = "ux0:data/wiicompiled-vita/runtime.log";
constexpr size_t kBootLogTailBytes = 24 * 1024;
constexpr size_t kBootConsoleColumns = 118;
constexpr size_t kBootConsoleRows = 64;
constexpr int kBootFontAtlasSize = 128;
#if defined(MKW_VITA_AURORA_RENDERER)
constexpr const char* kRendererVariant = "aurora";
#else
constexpr const char* kRendererVariant = "legacy";
#endif
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
constexpr const char* kVitaGlVariant = "speedhack-custom-heap";
constexpr u8 kRenderTargetScenes = 8;
#else
constexpr const char* kVitaGlVariant = "stock";
constexpr u8 kRenderTargetScenes = 1;
#endif

struct FifoMeta {
    void* base = nullptr;
    void* read = nullptr;
    void* write = nullptr;
    u32 size = 0;
};
static_assert(sizeof(FifoMeta) <= sizeof(GXFifoObj));

struct TexMeta {
    const void* data = nullptr;
    void* userData = nullptr;
    u16 width = 0;
    u16 height = 0;
    u32 format = GX_TF_I4;
    u32 tlut = 0;
    GXTexWrapMode wrapS = GX_CLAMP;
    GXTexWrapMode wrapT = GX_CLAMP;
    GXTexFilter minFilter = GX_NEAR;
    GXTexFilter magFilter = GX_NEAR;
    f32 minLod = 0.0f;
    f32 maxLod = 0.0f;
    f32 lodBias = 0.0f;
    GXBool mipmap = GX_FALSE;
    GXBool biasClamp = GX_FALSE;
    GXBool edgeLod = GX_FALSE;
    GXAnisotropy maxAniso = GX_ANISO_1;
    u32 dataRevision = 0;
};
static_assert(sizeof(TexMeta) <= sizeof(GXTexObj));

struct TlutMeta {
    const void* data = nullptr;
    GXTlutFmt format = GX_TL_IA8;
    u16 entries = 0;
};
static_assert(sizeof(TlutMeta) <= sizeof(GXTlutObj));

struct LightMeta {
    GXColor color{};
    f32 a0 = 1.0f;
    f32 a1 = 0.0f;
    f32 a2 = 0.0f;
    f32 k0 = 1.0f;
    f32 k1 = 0.0f;
    f32 k2 = 0.0f;
    f32 px = 0.0f;
    f32 py = 0.0f;
    f32 pz = 0.0f;
    f32 nx = 0.0f;
    f32 ny = 0.0f;
    f32 nz = -1.0f;
};
static_assert(sizeof(LightMeta) <= sizeof(GXLightObj));

struct VtxFmtState {
    GXCompCnt cnt = GX_POS_XYZ;
    GXCompType type = GX_F32;
    u8 frac = 0;
};

struct TevSignature {
    u32 color = 0;
    u32 alpha = 0;
    u32 order = 0;
    u32 ksel = 0;
    u16 count = 0;
    u8 stageCount = 0;
    u8 selectedStage = 0;
};

struct ArrayState {
    const void* data = nullptr;
    u32 size = 0;
    u8 stride = 0;
    bool littleEndian = false;
};

struct FrameCounters {
    uint64_t drawCalls = 0;
    uint64_t vertices = 0;
    uint64_t displayListBytes = 0;
    uint64_t displayListsReplayed = 0;
    uint64_t rawDrawBytes = 0;
    uint64_t rawDrawsDecoded = 0;
    uint64_t rawDrawDecodeFailures = 0;
    uint64_t rawDrawCapacityFailures = 0;
    uint64_t immediateDrawCapacityFailures = 0;
    uint64_t textureStateNoTexGen = 0;
    uint64_t textureStateNoTexAttr = 0;
    uint64_t textureStateNoTevStage = 0;
    uint64_t textureStateBadOrder = 0;
    uint64_t textureStateUnbound = 0;
    uint64_t textureStateInvalidObject = 0;
    uint64_t textureStateRecoveredLaterStage = 0;
    uint64_t textureStateUnsupportedTexCoord = 0;
    uint64_t textureStateRecoveredCustomStage = 0;
    uint64_t textureStateCustomPresetModulate = 0;
    uint64_t textureStateCustomPresetDecal = 0;
    uint64_t textureStateCustomPresetBlend = 0;
    uint64_t textureStateCustomPresetReplace = 0;
    uint64_t textureStateCustomPresetPassClr = 0;
    uint64_t textureStateCustomPresetUnknown = 0;
    uint64_t textureStateCustomSignatureOverflow = 0;
    uint64_t textureInvalidateAllCalls = 0;
    uint64_t efbCopyCalls = 0;
    uint64_t efbCopyRecorded = 0;
    uint64_t efbCopyCapacityFailures = 0;
    uint64_t efbDestroyRecorded = 0;
    std::array<TevSignature, kMaxTevSignaturesPerFrame> customTevSignatures{};
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
    std::array<uint32_t, 7> primitiveDraws{};
    std::array<uint32_t, 8> vertexFormatDraws{};
};

struct RenderVertex {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    u8 r = 255;
    u8 g = 255;
    u8 b = 255;
    u8 a = 255;
    f32 s = 0.0f;
    f32 t = 0.0f;
};
static_assert(sizeof(RenderVertex) == 24);
#if defined(MKW_VITA_AURORA_RENDERER)
static_assert(sizeof(RenderVertex) == sizeof(WiiCompiledVita::AuroraPacketVertex));
static_assert(alignof(RenderVertex) == alignof(WiiCompiledVita::AuroraPacketVertex));
#endif

GLuint g_bootFontTexture = 0;
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
GLuint g_bootVertexBuffer = 0;
size_t g_bootVertexBufferCapacity = 0;
uint32_t g_bootConsoleGpuTraceCount = 0;
#endif
std::array<char, kBootLogTailBytes + 1> g_bootLogTail{};
std::vector<RenderVertex> g_bootTextVertices;

bool EnsureBootFontTexture() {
    if (g_bootFontTexture != 0) {
        return true;
    }

    const PsvDebugScreenFont* font = psvDebugScreenGetFont();
    if (!font || !font->glyphs || font->width != 8 || font->height != 8) {
        return false;
    }

    std::unique_ptr<u8[]> pixels(
        new (std::nothrow) u8[kBootFontAtlasSize * kBootFontAtlasSize * 4]);
    if (!pixels) {
        return false;
    }
    std::memset(pixels.get(), 0, kBootFontAtlasSize * kBootFontAtlasSize * 4);

    for (unsigned glyph = font->first; glyph <= font->last; ++glyph) {
        const unsigned glyphIndex = glyph - font->first;
        const unsigned atlasX = (glyph & 15u) * 8u;
        const unsigned atlasY = (glyph >> 4u) * 8u;
        for (unsigned y = 0; y < 8; ++y) {
            const u8 bits = font->glyphs[glyphIndex * 8u + y];
            for (unsigned x = 0; x < 8; ++x) {
                if ((bits & (0x80u >> x)) == 0) {
                    continue;
                }
                const size_t pixel =
                    (static_cast<size_t>(atlasY + y) * kBootFontAtlasSize + atlasX + x) * 4u;
                pixels[pixel + 0] = 190;
                pixels[pixel + 1] = 255;
                pixels[pixel + 2] = 205;
                pixels[pixel + 3] = 255;
            }
        }
    }

    glGenTextures(1, &g_bootFontTexture);
    if (g_bootFontTexture == 0) {
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, g_bootFontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kBootFontAtlasSize, kBootFontAtlasSize,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
    return glGetError() == GL_NO_ERROR;
}

std::vector<std::string> ReadBootConsoleLines() {
    std::vector<std::string> lines;
    const SceUID fd = sceIoOpen(kBootLogPath, SCE_O_RDONLY, 0);
    if (fd < 0) {
        lines.emplace_back("Waiting for runtime.log...");
        return lines;
    }

    const SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
    const SceOff start = end > static_cast<SceOff>(kBootLogTailBytes)
        ? end - static_cast<SceOff>(kBootLogTailBytes)
        : 0;
    sceIoLseek(fd, start, SCE_SEEK_SET);
    const int read = sceIoRead(fd, g_bootLogTail.data(), kBootLogTailBytes);
    sceIoClose(fd);
    if (read <= 0) {
        lines.emplace_back("runtime.log is empty");
        return lines;
    }
    g_bootLogTail[static_cast<size_t>(read)] = '\0';

    size_t offset = 0;
    if (start != 0) {
        while (offset < static_cast<size_t>(read) && g_bootLogTail[offset] != '\n') {
            ++offset;
        }
        if (offset < static_cast<size_t>(read)) {
            ++offset;
        }
    }

    std::string line;
    line.reserve(kBootConsoleColumns);
    const auto pushLine = [&]() {
        lines.push_back(line);
        line.clear();
        if (lines.size() > kBootConsoleRows) {
            lines.erase(lines.begin());
        }
    };
    for (; offset < static_cast<size_t>(read); ++offset) {
        const unsigned char ch = static_cast<unsigned char>(g_bootLogTail[offset]);
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            pushLine();
            continue;
        }
        if (ch == '\t') {
            do {
                line.push_back(' ');
            } while ((line.size() & 3u) != 0 && line.size() < kBootConsoleColumns);
        } else {
            line.push_back(ch >= 32 && ch < 127 ? static_cast<char>(ch) : '?');
        }
        if (line.size() >= kBootConsoleColumns) {
            pushLine();
        }
    }
    if (!line.empty()) {
        pushLine();
    }
    return lines;
}

void AppendBootGlyph(unsigned char ch, float x, float y) {
    const float left = -1.0f + (2.0f * x / static_cast<float>(kSurfaceWidth));
    const float right = -1.0f + (2.0f * (x + 8.0f) / static_cast<float>(kSurfaceWidth));
    const float top = 1.0f - (2.0f * y / static_cast<float>(kSurfaceHeight));
    const float bottom = 1.0f - (2.0f * (y + 8.0f) / static_cast<float>(kSurfaceHeight));
    const float u0 = static_cast<float>((ch & 15u) * 8u) / kBootFontAtlasSize;
    const float v0 = static_cast<float>((ch >> 4u) * 8u) / kBootFontAtlasSize;
    const float u1 = u0 + 8.0f / kBootFontAtlasSize;
    const float v1 = v0 + 8.0f / kBootFontAtlasSize;

    const auto vertex = [](float px, float py, float u, float v) {
        RenderVertex result{};
        result.x = px;
        result.y = py;
        result.z = 0.0f;
        result.r = result.g = result.b = result.a = 255;
        result.s = u;
        result.t = v;
        return result;
    };
    g_bootTextVertices.push_back(vertex(left, top, u0, v0));
    g_bootTextVertices.push_back(vertex(right, top, u1, v0));
    g_bootTextVertices.push_back(vertex(right, bottom, u1, v1));
    g_bootTextVertices.push_back(vertex(left, top, u0, v0));
    g_bootTextVertices.push_back(vertex(right, bottom, u1, v1));
    g_bootTextVertices.push_back(vertex(left, bottom, u0, v1));
}

#if defined(MKW_VITA_VITAGL_SPEEDHACK)
bool UploadBootConsoleVerticesToGpu() {
    if (g_bootTextVertices.empty()) {
        return true;
    }
    const size_t bytes = g_bootTextVertices.size() * sizeof(RenderVertex);
    if (g_bootVertexBuffer == 0) {
        glGenBuffers(1, &g_bootVertexBuffer);
        if (g_bootVertexBuffer == 0) {
            return false;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, g_bootVertexBuffer);
    if (bytes > g_bootVertexBufferCapacity) {
        size_t capacity = g_bootVertexBufferCapacity ? g_bootVertexBufferCapacity : 4096u;
        while (capacity < bytes && capacity <= (std::numeric_limits<size_t>::max() / 2u)) {
            capacity *= 2u;
        }
        if (capacity < bytes) {
            capacity = bytes;
        }
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity), nullptr, GL_DYNAMIC_DRAW);
        if (glGetError() != GL_NO_ERROR) {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            return false;
        }
        g_bootVertexBufferCapacity = capacity;
    }
    void* mapped = glMapBufferRange(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_WRITE_BIT);
    if (!mapped) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return false;
    }
    std::memcpy(mapped, g_bootTextVertices.data(), bytes);
    if (glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return false;
    }
    return true;
}
#endif

void RenderBootConsole() {
    if (!EnsureBootFontTexture()) {
        return;
    }
    const std::vector<std::string> lines = ReadBootConsoleLines();
    g_bootTextVertices.clear();
    g_bootTextVertices.reserve(lines.size() * kBootConsoleColumns * 6u);
    for (size_t row = 0; row < lines.size(); ++row) {
        const std::string& line = lines[row];
        for (size_t col = 0; col < line.size(); ++col) {
            if (line[col] != ' ') {
                AppendBootGlyph(static_cast<unsigned char>(line[col]),
                                8.0f + static_cast<float>(col * 8u),
                                8.0f + static_cast<float>(row * 8u));
            }
        }
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, kSurfaceWidth, kSurfaceHeight);
    glClearColor(0.01f, 0.018f, 0.012f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_bootFontTexture);
    if (!g_bootTextVertices.empty()) {
        // Fixed-function client pointers are unsafe with the tested vitaGL
        // speedhack: its DRAW_SPEEDHACK can feed them directly to GXM. Keep
        // the boot console GPU-backed too, and always neutralize an Aurora IBO.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
        const bool traceBootGpu = g_bootConsoleGpuTraceCount < 4u;
        if (traceBootGpu) {
            RT_LOGF(RT_TAG_GX,
                    "boot_console_gpu phase=upload_begin ordinal=%u vertices=%u bytes=%u renderer=%s vitagl=%s\n",
                    g_bootConsoleGpuTraceCount + 1u,
                    static_cast<unsigned>(g_bootTextVertices.size()),
                    static_cast<unsigned>(g_bootTextVertices.size() * sizeof(RenderVertex)),
                    kRendererVariant, kVitaGlVariant);
        }
        if (!UploadBootConsoleVerticesToGpu()) {
            RT_LOGF(RT_TAG_GX,
                    "boot_console_gpu phase=upload_failed ordinal=%u vertices=%u\n",
                    g_bootConsoleGpuTraceCount + 1u,
                    static_cast<unsigned>(g_bootTextVertices.size()));
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            return;
        }
        if (traceBootGpu) {
            RT_LOGF(RT_TAG_GX,
                    "boot_console_gpu phase=upload_end ordinal=%u vbo=%u capacity=%u\n",
                    g_bootConsoleGpuTraceCount + 1u,
                    static_cast<unsigned>(g_bootVertexBuffer),
                    static_cast<unsigned>(g_bootVertexBufferCapacity));
        }
#else
        glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
        glVertexPointer(3, GL_FLOAT, sizeof(RenderVertex),
                        reinterpret_cast<const GLvoid*>(offsetof(RenderVertex, x)));
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(RenderVertex),
                       reinterpret_cast<const GLvoid*>(offsetof(RenderVertex, r)));
        glTexCoordPointer(2, GL_FLOAT, sizeof(RenderVertex),
                          reinterpret_cast<const GLvoid*>(offsetof(RenderVertex, s)));
        if (traceBootGpu) {
            RT_LOGF(RT_TAG_GX, "boot_console_gpu phase=draw_begin ordinal=%u\n",
                    g_bootConsoleGpuTraceCount + 1u);
        }
#else
        glVertexPointer(3, GL_FLOAT, sizeof(RenderVertex), &g_bootTextVertices[0].x);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(RenderVertex), &g_bootTextVertices[0].r);
        glTexCoordPointer(2, GL_FLOAT, sizeof(RenderVertex), &g_bootTextVertices[0].s);
#endif
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_bootTextVertices.size()));
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
        if (traceBootGpu) {
            RT_LOGF(RT_TAG_GX, "boot_console_gpu phase=draw_returned ordinal=%u\n",
                    g_bootConsoleGpuTraceCount + 1u);
            glFinish();
            RT_LOGF(RT_TAG_GX, "boot_console_gpu phase=finish_end ordinal=%u\n",
                    g_bootConsoleGpuTraceCount + 1u);
        }
        ++g_bootConsoleGpuTraceCount;
#endif
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    vglSwapBuffers(GL_FALSE);
}

struct DrawTransform {
    std::array<f32, 16> projection{};
    std::array<std::array<f32, 12>, kPnMtxCount> posMtx{};
    GXProjectionType projectionType = GX_PERSPECTIVE;
};

struct DrawRasterState {
    GXCompare depthFunc = GX_LEQUAL;
    GXCullMode cullMode = GX_CULL_BACK;
    GXBool depthCompare = GX_TRUE;
    GXBool depthUpdate = GX_TRUE;
    GXBlendMode blendMode = GX_BM_NONE;
    GXBlendFactor blendSrc = GX_BL_ONE;
    GXBlendFactor blendDst = GX_BL_ZERO;
    GXLogicOp logicOp = GX_LO_COPY;
    GXCompare alphaComp0 = GX_ALWAYS;
    GXCompare alphaComp1 = GX_ALWAYS;
    GXAlphaOp alphaOp = GX_AOP_AND;
    u8 alphaRef0 = 0;
    u8 alphaRef1 = 0;
    GXBool colorUpdate = GX_TRUE;
    GXBool alphaUpdate = GX_TRUE;
};

struct TexGenState {
    GXTexGenType type = GX_TG_MTX2x4;
    GXTexGenSrc src = GX_TG_TEX0;
    u32 mtx = GX_IDENTITY;
    GXBool normalize = GX_FALSE;
    u32 postMtx = GX_PTIDENTITY;
};

struct DrawTextureState {
    const void* data = nullptr;
    u32 dataRevision = 0;
    u32 globalEpoch = 0;
    uint64_t sourceGeneration = AURORA_GUEST_WRITE_UNTRACKED;
    u32 format = GX_TF_I4;
    u16 width = 0;
    u16 height = 0;
    u8 wrapS = static_cast<u8>(GX_CLAMP);
    u8 wrapT = static_cast<u8>(GX_CLAMP);
    u8 minFilter = static_cast<u8>(GX_NEAR);
    u8 magFilter = static_cast<u8>(GX_NEAR);
    u8 tevMode = static_cast<u8>(GX_MODULATE);
    u8 tevSimple = 1;
    u8 mipmap = 0;
    u8 enabled = 0;
    u8 texGenMode = 0; // 0=raw/identity, 1=matrix, 2=unsupported fallback
    u8 texGenType = static_cast<u8>(GX_TG_MTX2x4);
    u8 texGenSrc = static_cast<u8>(GX_TG_TEX0);
    u8 texGenNormalize = 0;
    std::array<f32, 12> texGenMtx{};
    std::array<f32, 12> texGenPostMtx{};
    const void* thpUData = nullptr;
    const void* thpVData = nullptr;
    uint64_t thpUGeneration = AURORA_GUEST_WRITE_UNTRACKED;
    uint64_t thpVGeneration = AURORA_GUEST_WRITE_UNTRACKED;
    u32 thpURevision = 0;
    u32 thpVRevision = 0;
    u16 thpChromaWidth = 0;
    u16 thpChromaHeight = 0;
    u8 thpYuv420 = 0;
};

struct GeometryDraw {
    GXPrimitive primitive = GX_TRIANGLES;
    u16 firstVertex = 0;
    u16 vertexCount = 0;
    u32 guestLr = 0;
    u32 pnMtxIndex = 0;
    DrawTransform transform{};
    DrawRasterState raster{};
    DrawTextureState texture{};
};

enum class EfbFrameCommandType : u8 { Copy = 0, Destroy = 1 };

struct EfbFrameCommand {
    EfbFrameCommandType type = EfbFrameCommandType::Copy;
    u16 afterDrawCount = 0;
    uintptr_t destination = 0;
    u16 srcLeft = 0;
    u16 srcTop = 0;
    u16 srcWidth = 0;
    u16 srcHeight = 0;
    u16 dstWidth = 0;
    u16 dstHeight = 0;
    u32 format = GX_TF_RGBA8;
    GXColor clearColor{0, 0, 0, 255};
    u32 clearDepth = 0x00ffffffu;
    u8 clear = 0;
    u8 clearColorEnable = 1;
    u8 clearAlphaEnable = 1;
    u8 clearDepthEnable = 1;
};

struct FrameGeometry {
    std::array<RenderVertex, kMaxFrameVertices> vertices{};
    std::array<u8, kMaxFrameVertices> pnMtxRefs{};
    std::array<GeometryDraw, kMaxFrameDraws> draws{};
    std::array<EfbFrameCommand, kMaxFrameEfbCommands> efbCommands{};
    u16 vertexCount = 0;
    u16 drawCount = 0;
    u8 efbCommandCount = 0;
    uint32_t droppedVertices = 0;
};

struct FramePacket {
    FrameCounters counters{};
    FrameGeometry geometry{};
    std::array<f32, 6> viewport{0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f};
    std::array<u32, 4> scissor{0, 0, 640, 480};
};
static_assert(sizeof(FramePacket) < 8 * 1024 * 1024,
              "Vita GX frame packet must stay bounded below 8 MiB");

struct GxState {
    std::array<GXAttrType, GX_VA_MAX_ATTR> vtxDesc{};
    std::array<GXAttrType, GX_VA_MAX_ATTR> sourceVtxDesc{};
    std::array<std::array<VtxFmtState, GX_VA_MAX_ATTR>, GX_MAX_VTXFMT> vtxFmt{};
    std::array<ArrayState, GX_VA_MAX_ATTR> arrays{};
    std::array<GXTexObj*, GX_MAX_TEXMAP> textures{};
    std::array<GXTlutObj*, 20> tluts{};
    std::array<u32, 256> bpRegs{};
    std::array<f32, 16> projection{};
    std::array<f32, 6> xfProjection{1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
    std::array<f32, 6> xfViewport{320.0f, -240.0f, 16777216.0f,
                                  660.0f, 580.0f, 16777216.0f};
    std::array<std::array<f32, 12>, 10> posMtx{};
    std::array<std::array<f32, 12>, 10> nrmMtx{};
    std::array<std::array<f32, 12>, 20> texMtx{};
    std::array<std::array<f32, 12>, 20> postTexMtx{};
    std::array<TexGenState, GX_MAX_TEXCOORD> texGen{};
    std::array<f32, 6> viewport{0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f};
    std::array<u32, 4> scissor{0, 0, 640, 480};
    std::array<u16, 4> boundingBox{1023, 0, 1023, 0};
    std::array<GXColor, 4> tevColor{};
    std::array<GXColor, GX_MAX_KCOLOR> kColor{};
    std::array<GXColor, 4> chanAmb{};
    std::array<GXColor, 4> chanMat{};
    std::array<GXTexCoordID, GX_MAX_TEVSTAGE> tevTexCoords{};
    std::array<GXTexMapID, GX_MAX_TEVSTAGE> tevTexMaps{};
    std::array<GXTevMode, GX_MAX_TEVSTAGE> tevModes{};
    std::array<u8, GX_MAX_TEVSTAGE> tevPresetValid{};
    GXProjectionType projectionType = GX_PERSPECTIVE;
    GXPrimitive primitive = GX_TRIANGLES;
    GXVtxFmt currentVtxFmt = GX_VTXFMT0;
    u16 declaredVertices = 0;
    int activeDraw = -1;
    u32 currentMtx = 0;
    u8 pendingPnMtxRef = 0;
    GXBlendMode blendMode = GX_BM_NONE;
    GXBlendFactor blendSrc = GX_BL_ONE;
    GXBlendFactor blendDst = GX_BL_ZERO;
    GXLogicOp logicOp = GX_LO_COPY;
    GXCompare depthFunc = GX_LEQUAL;
    GXPixelFmt pixelFmt = GX_PF_RGB8_Z24;
    GXZFmt16 zFmt = GX_ZC_LINEAR;
    GXCullMode cullMode = GX_CULL_BACK;
    GXClipMode clipMode = GX_CLIP_ENABLE;
    GXBool colorUpdate = GX_TRUE;
    GXBool alphaUpdate = GX_TRUE;
    GXBool depthCompare = GX_TRUE;
    GXBool depthUpdate = GX_TRUE;
    GXCompare alphaComp0 = GX_ALWAYS;
    GXCompare alphaComp1 = GX_ALWAYS;
    GXAlphaOp alphaOp = GX_AOP_AND;
    u8 alphaRef0 = 0;
    u8 alphaRef1 = 0;
    GXBool zCompBeforeTex = GX_TRUE;
    GXBool dither = GX_FALSE;
    u32 textureGlobalEpoch = 1;
    u8 dstAlpha = 0;
    GXBool dstAlphaEnable = GX_FALSE;
    u8 numTevStages = 1;
    u8 numTexGens = 0;
    u8 numChans = 0;
    u8 numIndStages = 0;
    u16 dispCopyWidth = 640;
    u16 dispCopyHeight = 480;
    u16 texCopyWidth = 0;
    u16 texCopyHeight = 0;
    u16 dispCopyLeft = 0;
    u16 dispCopyTop = 0;
    u16 dispCopySrcWidth = 640;
    u16 dispCopySrcHeight = 480;
    u16 texCopyLeft = 0;
    u16 texCopyTop = 0;
    u16 texCopySrcWidth = 0;
    u16 texCopySrcHeight = 0;
    GXTexFmt texCopyFmt = GX_TF_RGBA8;
    GXBool texCopyMipmap = GX_FALSE;
    GXColor copyClearColor{0, 0, 0, 255};
    u32 copyClearDepth = 0x00ffffffu;
    GXGamma dispGamma = GX_GM_1_0;
    GXFBClamp copyClamp = GX_CLAMP_NONE;
    u32 frame2Field = 0;
    f32 zScale = 1.0f;
    f32 zOffset = 0.0f;
    s32 scissorOffsetX = 0;
    s32 scissorOffsetY = 0;
    FrameCounters frame{};
    FrameGeometry geometry{};
};

GxState g_gx;
u32 g_textureRevisionSerial = 1;
GXFifoObj g_defaultFifo{};
GXFifoObj* g_cpuFifo = &g_defaultFifo;
GXFifoObj* g_gpFifo = &g_defaultFifo;

// Best-effort guest return address of the most recent GXBegin (immediate-mode
// wrapper path only; G3D/FIFO display-list draws leave it stale). Captured into
// each GeometryDraw for the M12 graphics-correctness trace.
std::atomic<u32> g_lastGuestBeginLr{0};

std::mutex g_renderMutex;
std::condition_variable g_renderWake;
std::condition_variable g_renderIdle;
HostThread g_renderThread;
bool g_renderStarted = false;
bool g_renderStop = false;
bool g_renderPending = false;
bool g_renderBusy = false;
bool g_renderInitDone = false;
bool g_renderInitOk = false;
uint64_t g_submittedSerial = 0;
uint64_t g_completedSerial = 0;
uint64_t g_lastProducerSubmitUs = 0;
uint64_t g_guestWaitRenderCallsSinceSubmit = 0;
uint64_t g_guestWaitRenderUsSinceSubmit = 0;
FramePacket g_pendingFrame{};
WiiCompiledVita::GxBackend::Stats g_stats{};
AuroraFrameWorkerWaitCallback g_waitCallback = nullptr;
AuroraGuestWriteGenerationCallback g_guestWriteGeneration = nullptr;
AuroraGuestWriteNotifyCallback g_guestWriteNotify = nullptr;
AuroraDisplayMode g_displayMode = AURORA_DISPLAY_MODE_BORDERLESS;
AuroraViewportPolicy g_viewportPolicy = AURORA_VIEWPORT_FIT;
std::array<RenderVertex, kMaxFrameVertices> g_renderVertices{};
#if !defined(MKW_VITA_AURORA_RENDERER) && defined(MKW_VITA_VITAGL_SPEEDHACK)
// The speedhack vitaGL fixed-function path may submit client pointers directly
// to GXM. Keep legacy transformed vertices in a GPU-backed VBO so those streams
// are always GXM-visible instead of pointing at ordinary process RAM.
GLuint g_legacyVertexBuffer = 0;
#endif

struct TextureCacheEntry {
    bool valid = false;
    uintptr_t data = 0;
    u32 dataRevision = 0;
    u32 globalEpoch = 0;
    uint64_t sourceGeneration = AURORA_GUEST_WRITE_UNTRACKED;
    u32 format = 0;
    u16 width = 0;
    u16 height = 0;
    GLuint glTexture = 0;
    size_t gpuBytes = 0;
    uint64_t lastUse = 0;
};

struct TextureRenderCounters {
    uint64_t draws = 0;
    uint64_t cacheHits = 0;
    uint64_t cacheMisses = 0;
    uint64_t uploads = 0;
    uint64_t uploadFailures = 0;
    uint64_t unsupportedDraws = 0;
    uint64_t sourceRaceDraws = 0;
    uint64_t mipFallbackDraws = 0;
    uint64_t trackedDraws = 0;
    uint64_t untrackedDraws = 0;
    uint64_t bytesUploaded = 0;
    uint64_t rgb565Uploads = 0;
    uint64_t rgb5a3Uploads = 0;
    uint64_t rgba8Uploads = 0;
    uint64_t rgba8PcUploads = 0;
    uint64_t i4Uploads = 0;
    uint64_t i8Uploads = 0;
    uint64_t ia4Uploads = 0;
    uint64_t ia8Uploads = 0;
    uint64_t cmprUploads = 0;
};

std::array<TextureCacheEntry, kTextureCacheCapacity> g_textureCache{};
std::unique_ptr<u8[]> g_textureScratch;
size_t g_textureCacheBytes = 0;
uint64_t g_textureUseSerial = 0;

TexMeta& Tex(GXTexObj* obj);
const TexMeta& Tex(const GXTexObj* obj);
uint32_t TextureLevelSize(u16 width, u16 height, u32 fmt);

#if defined(MKW_TARGET_VITA)
extern "C" void GX_HLE_ReplayDisplayListVita(const uint8_t*, uint32_t) __attribute__((weak));
thread_local bool g_replayingDisplayList = false;
#endif

GLenum PrimitiveMode(GXPrimitive primitive) {
    switch (primitive) {
    case GX_QUADS:
        return GL_QUADS;
    case GX_TRIANGLES:
        return GL_TRIANGLES;
    case GX_TRIANGLESTRIP:
        return GL_TRIANGLE_STRIP;
    case GX_TRIANGLEFAN:
        return GL_TRIANGLE_FAN;
    case GX_LINES:
        return GL_LINES;
    case GX_LINESTRIP:
        return GL_LINE_STRIP;
    case GX_POINTS:
        return GL_POINTS;
    }
    return GL_TRIANGLES;
}

GLenum DepthCompareMode(GXCompare compare) {
    switch (compare) {
    case GX_NEVER: return GL_NEVER;
    case GX_LESS: return GL_LESS;
    case GX_EQUAL: return GL_EQUAL;
    case GX_LEQUAL: return GL_LEQUAL;
    case GX_GREATER: return GL_GREATER;
    case GX_NEQUAL: return GL_NOTEQUAL;
    case GX_GEQUAL: return GL_GEQUAL;
    case GX_ALWAYS: return GL_ALWAYS;
    }
    return GL_ALWAYS;
}

GLenum BlendFactorMode(GXBlendFactor factor, bool destination) {
    switch (factor) {
    case GX_BL_ZERO: return GL_ZERO;
    case GX_BL_ONE: return GL_ONE;
    case GX_BL_SRCCLR:
        return destination ? GL_SRC_COLOR : GL_DST_COLOR;
    case GX_BL_INVSRCCLR:
        return destination ? GL_ONE_MINUS_SRC_COLOR : GL_ONE_MINUS_DST_COLOR;
    case GX_BL_SRCALPHA: return GL_SRC_ALPHA;
    case GX_BL_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case GX_BL_DSTALPHA: return GL_DST_ALPHA;
    case GX_BL_INVDSTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    }
    return destination ? GL_ZERO : GL_ONE;
}

GXCompare InvertCompare(GXCompare compare) {
    switch (compare) {
    case GX_NEVER: return GX_ALWAYS;
    case GX_LESS: return GX_GEQUAL;
    case GX_EQUAL: return GX_NEQUAL;
    case GX_LEQUAL: return GX_GREATER;
    case GX_GREATER: return GX_LEQUAL;
    case GX_NEQUAL: return GX_EQUAL;
    case GX_GEQUAL: return GX_LESS;
    case GX_ALWAYS: return GX_NEVER;
    }
    return GX_ALWAYS;
}

struct AlphaTestSelection {
    bool enabled = false;
    bool exact = true;
    GXCompare compare = GX_ALWAYS;
    u8 reference = 0;
};

AlphaTestSelection SelectAlphaTest(const DrawRasterState& raster) {
    const auto constant = [](GXCompare compare, bool& value) {
        if (compare == GX_ALWAYS) {
            value = true;
            return true;
        }
        if (compare == GX_NEVER) {
            value = false;
            return true;
        }
        return false;
    };
    const auto single = [](GXCompare compare, u8 reference) {
        AlphaTestSelection result{};
        result.enabled = compare != GX_ALWAYS;
        result.compare = compare;
        result.reference = reference;
        return result;
    };

    bool c0 = false;
    bool c1 = false;
    const bool const0 = constant(raster.alphaComp0, c0);
    const bool const1 = constant(raster.alphaComp1, c1);
    if (const0 && const1) {
        bool pass = true;
        switch (raster.alphaOp) {
        case GX_AOP_AND: pass = c0 && c1; break;
        case GX_AOP_OR: pass = c0 || c1; break;
        case GX_AOP_XOR: pass = c0 != c1; break;
        case GX_AOP_XNOR: pass = c0 == c1; break;
        default: break;
        }
        return single(pass ? GX_ALWAYS : GX_NEVER, 0);
    }

    if (raster.alphaComp0 == raster.alphaComp1 && raster.alphaRef0 == raster.alphaRef1) {
        if (raster.alphaOp == GX_AOP_AND || raster.alphaOp == GX_AOP_OR) {
            return single(raster.alphaComp0, raster.alphaRef0);
        }
        return single(raster.alphaOp == GX_AOP_XNOR ? GX_ALWAYS : GX_NEVER, 0);
    }

    if (const0 || const1) {
        const bool leftConstant = const0;
        const bool constantValue = leftConstant ? c0 : c1;
        GXCompare otherCompare = leftConstant ? raster.alphaComp1 : raster.alphaComp0;
        const u8 otherRef = leftConstant ? raster.alphaRef1 : raster.alphaRef0;
        switch (raster.alphaOp) {
        case GX_AOP_AND:
            return constantValue ? single(otherCompare, otherRef) : single(GX_NEVER, 0);
        case GX_AOP_OR:
            return constantValue ? single(GX_ALWAYS, 0) : single(otherCompare, otherRef);
        case GX_AOP_XOR:
            if (constantValue) otherCompare = InvertCompare(otherCompare);
            return single(otherCompare, otherRef);
        case GX_AOP_XNOR:
            if (!constantValue) otherCompare = InvertCompare(otherCompare);
            return single(otherCompare, otherRef);
        default:
            break;
        }
    }

    AlphaTestSelection fallback{};
    fallback.exact = false;
    return fallback;
}

bool IsTrianglePrimitive(GXPrimitive primitive) {
    return primitive == GX_QUADS || primitive == GX_TRIANGLES ||
           primitive == GX_TRIANGLESTRIP || primitive == GX_TRIANGLEFAN;
}

u8 PnMtxSlot(u32 matrixIndex) {
    return static_cast<u8>(std::min<u32>(matrixIndex / 3u, static_cast<u32>(kPnMtxCount - 1u)));
}

u8 DefaultPnMtxRef() {
    return PnMtxSlot(g_gx.currentMtx);
}

u32 NextTextureRevision() {
    ++g_textureRevisionSerial;
    if (g_textureRevisionSerial == 0) {
        g_textureRevisionSerial = 1;
    }
    return g_textureRevisionSerial;
}

void RebuildProjectionFromXf() {
    g_gx.projection.fill(0.0f);
    g_gx.projection[0] = g_gx.xfProjection[0];
    g_gx.projection[5] = g_gx.xfProjection[2];
    g_gx.projection[10] = g_gx.xfProjection[4];
    g_gx.projection[11] = g_gx.xfProjection[5];
    if (g_gx.projectionType == GX_ORTHOGRAPHIC) {
        g_gx.projection[3] = g_gx.xfProjection[1];
        g_gx.projection[7] = g_gx.xfProjection[3];
        g_gx.projection[15] = 1.0f;
    } else {
        g_gx.projection[2] = g_gx.xfProjection[1];
        g_gx.projection[6] = g_gx.xfProjection[3];
        g_gx.projection[14] = -1.0f;
    }
}

void RebuildViewportFromXf() {
    const f32 sx = g_gx.xfViewport[0];
    const f32 sy = g_gx.xfViewport[1];
    const f32 sz = g_gx.xfViewport[2];
    const f32 ox = g_gx.xfViewport[3];
    const f32 oy = g_gx.xfViewport[4];
    const f32 oz = g_gx.xfViewport[5];
    constexpr f32 z24Scale = 16777216.0f;
    const f32 width = sx * 2.0f;
    const f32 height = -sy * 2.0f;
    g_gx.viewport = {
        ox - 340.0f - width * 0.5f,
        oy - 340.0f - height * 0.5f,
        width,
        height,
        (oz - sz) / z24Scale,
        oz / z24Scale,
    };
}

void CaptureDrawTransform(GeometryDraw& draw) {
    draw.transform.projection = g_gx.projection;
    draw.transform.posMtx = g_gx.posMtx;
    draw.transform.projectionType = g_gx.projectionType;
    draw.guestLr = g_lastGuestBeginLr.load(std::memory_order_relaxed);
    draw.pnMtxIndex = g_gx.currentMtx;
}

void CaptureDrawRasterState(GeometryDraw& draw) {
    draw.raster.depthFunc = g_gx.depthFunc;
    draw.raster.cullMode = g_gx.cullMode;
    draw.raster.depthCompare = g_gx.depthCompare;
    draw.raster.depthUpdate = g_gx.depthUpdate;
    draw.raster.blendMode = g_gx.blendMode;
    draw.raster.blendSrc = g_gx.blendSrc;
    draw.raster.blendDst = g_gx.blendDst;
    draw.raster.logicOp = g_gx.logicOp;
    draw.raster.alphaComp0 = g_gx.alphaComp0;
    draw.raster.alphaComp1 = g_gx.alphaComp1;
    draw.raster.alphaOp = g_gx.alphaOp;
    draw.raster.alphaRef0 = g_gx.alphaRef0;
    draw.raster.alphaRef1 = g_gx.alphaRef1;
    draw.raster.colorUpdate = g_gx.colorUpdate;
    draw.raster.alphaUpdate = g_gx.alphaUpdate;
}

constexpr u32 EncodeTevColorInputs(GXTevColorArg a, GXTevColorArg b,
                                   GXTevColorArg c, GXTevColorArg d) {
    return (static_cast<u32>(a) << 12u) |
           (static_cast<u32>(b) << 8u) |
           (static_cast<u32>(c) << 4u) |
           static_cast<u32>(d);
}

constexpr u32 EncodeTevAlphaInputs(GXTevAlphaArg a, GXTevAlphaArg b,
                                   GXTevAlphaArg c, GXTevAlphaArg d) {
    return (static_cast<u32>(a) << 13u) |
           (static_cast<u32>(b) << 10u) |
           (static_cast<u32>(c) << 7u) |
           (static_cast<u32>(d) << 4u);
}

constexpr u32 EncodeSimpleTevOp() {
    // GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, clamp=true, GX_TEVPREV.
    return (static_cast<u32>(GX_TB_ZERO) << 16u) |
           ((static_cast<u32>(GX_TEV_ADD) & 1u) << 18u) |
           (1u << 19u) |
           (static_cast<u32>(GX_CS_SCALE_1) << 20u) |
           (static_cast<u32>(GX_TEVPREV) << 22u);
}

void SetPackedBits(u32& reg, u32 shift, u32 width, u32 value) {
    const u32 fieldMask = ((1u << width) - 1u) << shift;
    reg = (reg & ~fieldMask) | ((value << shift) & fieldMask);
    reg &= 0x00FFFFFFu;
}

bool TryClassifyCustomTevPreset(size_t stage, GXTevMode& outMode) {
    if (stage >= GX_MAX_TEVSTAGE) {
        return false;
    }

    const size_t colorReg = 0xC0u + stage * 2u;
    const size_t alphaReg = colorReg + 1u;
    const u32 color = g_gx.bpRegs[colorReg] & 0x00FFFFFFu;
    // Alpha low four bits contain raster/texture swap selectors, not combiner
    // inputs. Ignore them while recognising GXSetTevOp-equivalent equations.
    const u32 alpha = g_gx.bpRegs[alphaReg] & 0x00FFFFF0u;
    const u32 op = EncodeSimpleTevOp();
    const GXTevColorArg inputColor = stage == 0 ? GX_CC_RASC : GX_CC_CPREV;
    const GXTevAlphaArg inputAlpha = stage == 0 ? GX_CA_RASA : GX_CA_APREV;

    const auto matches = [&](GXTevColorArg ca, GXTevColorArg cb,
                             GXTevColorArg cc, GXTevColorArg cd,
                             GXTevAlphaArg aa, GXTevAlphaArg ab,
                             GXTevAlphaArg ac, GXTevAlphaArg ad) {
        const u32 expectedColor = (EncodeTevColorInputs(ca, cb, cc, cd) | op) & 0x00FFFFFFu;
        const u32 expectedAlpha = (EncodeTevAlphaInputs(aa, ab, ac, ad) | op) & 0x00FFFFF0u;
        return color == expectedColor && alpha == expectedAlpha;
    };

    if (matches(GX_CC_ZERO, GX_CC_TEXC, inputColor, GX_CC_ZERO,
                GX_CA_ZERO, GX_CA_TEXA, inputAlpha, GX_CA_ZERO)) {
        outMode = GX_MODULATE;
        return true;
    }
    if (matches(inputColor, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO,
                GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, inputAlpha)) {
        outMode = GX_DECAL;
        return true;
    }
    if (matches(inputColor, GX_CC_ONE, GX_CC_TEXC, GX_CC_ZERO,
                GX_CA_ZERO, GX_CA_TEXA, inputAlpha, GX_CA_ZERO)) {
        outMode = GX_BLEND;
        return true;
    }
    if (matches(GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC,
                GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA)) {
        outMode = GX_REPLACE;
        return true;
    }
    if (matches(GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, inputColor,
                GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, inputAlpha)) {
        outMode = GX_PASSCLR;
        return true;
    }
    return false;
}

void CountCustomTevPreset(GXTevMode mode) {
    switch (mode) {
    case GX_MODULATE: ++g_gx.frame.textureStateCustomPresetModulate; break;
    case GX_DECAL: ++g_gx.frame.textureStateCustomPresetDecal; break;
    case GX_BLEND: ++g_gx.frame.textureStateCustomPresetBlend; break;
    case GX_REPLACE: ++g_gx.frame.textureStateCustomPresetReplace; break;
    case GX_PASSCLR: ++g_gx.frame.textureStateCustomPresetPassClr; break;
    default: ++g_gx.frame.textureStateCustomPresetUnknown; break;
    }
}

void RecordCustomTevSignature(size_t selectedStage, size_t stageCount) {
    const size_t colorReg = 0xC0u + selectedStage * 2u;
    const size_t alphaReg = colorReg + 1u;
    const size_t orderReg = 0x28u + selectedStage / 2u;
    const size_t kselReg = 0xF6u + selectedStage / 2u;
    TevSignature signature{};
    signature.color = g_gx.bpRegs[colorReg] & 0x00FFFFFFu;
    signature.alpha = g_gx.bpRegs[alphaReg] & 0x00FFFFFFu;
    signature.order = g_gx.bpRegs[orderReg] & 0x00FFFFFFu;
    signature.ksel = g_gx.bpRegs[kselReg] & 0x00FFFFFFu;
    signature.stageCount = static_cast<u8>(stageCount);
    signature.selectedStage = static_cast<u8>(selectedStage);

    TevSignature* empty = nullptr;
    for (TevSignature& existing : g_gx.frame.customTevSignatures) {
        if (existing.count == 0) {
            if (!empty) {
                empty = &existing;
            }
            continue;
        }
        if (existing.color == signature.color && existing.alpha == signature.alpha &&
            existing.order == signature.order && existing.ksel == signature.ksel &&
            existing.stageCount == signature.stageCount &&
            existing.selectedStage == signature.selectedStage) {
            if (existing.count != std::numeric_limits<u16>::max()) {
                ++existing.count;
            }
            return;
        }
    }
    if (empty) {
        signature.count = 1;
        *empty = signature;
    } else {
        ++g_gx.frame.textureStateCustomSignatureOverflow;
    }
}

void CaptureDrawTextureState(GeometryDraw& draw) {
    draw.texture = {};
    if (g_gx.numTexGens == 0) {
        ++g_gx.frame.textureStateNoTexGen;
        return;
    }
    if (g_gx.vtxDesc[GX_VA_TEX0] == GX_NONE) {
        ++g_gx.frame.textureStateNoTexAttr;
        return;
    }
    if (g_gx.numTevStages == 0) {
        ++g_gx.frame.textureStateNoTevStage;
        return;
    }

    // The Vita renderer currently has one texture unit, but real NW4R/G3D
    // materials frequently put their first sampled texture on TEV stage 1+
    // while stage 0 passes raster colour. Pick the first stage we can represent
    // instead of requiring stage 0 to be TEXCOORD0/TEXMAPn.
    const size_t stageCount = std::min<size_t>(g_gx.numTevStages, g_gx.tevTexMaps.size());
    size_t selectedStage = stageCount;
    GXTexObj* obj = nullptr;
    bool sawUnsupportedTexCoord = false;
    bool sawUnboundTexMap = false;
    for (size_t stage = 0; stage < stageCount; ++stage) {
        const GXTexMapID map = g_gx.tevTexMaps[stage];
        // tevModes[] is authoritative only while the stage still represents a
        // GXSetTevOp preset. NW4R commonly switches a stage to custom color/
        // alpha combiners afterwards; tevPresetValid is then cleared but the
        // old preset enum (often GX_PASSCLR) remains cached. In that case TREF
        // decides whether the stage samples a texture, not the stale preset.
        if ((g_gx.tevPresetValid[stage] != 0 && g_gx.tevModes[stage] == GX_PASSCLR) ||
            map < 0 || map >= GX_MAX_TEXMAP) {
            continue;
        }
        if (g_gx.tevTexCoords[stage] != GX_TEXCOORD0) {
            sawUnsupportedTexCoord = true;
            continue;
        }
        obj = g_gx.textures[static_cast<size_t>(map)];
        if (!obj) {
            sawUnboundTexMap = true;
            continue;
        }
        selectedStage = stage;
        break;
    }

    if (selectedStage == stageCount) {
        if (sawUnsupportedTexCoord) {
            ++g_gx.frame.textureStateUnsupportedTexCoord;
        } else if (sawUnboundTexMap) {
            ++g_gx.frame.textureStateUnbound;
        } else {
            ++g_gx.frame.textureStateBadOrder;
        }
        return;
    }
    if (selectedStage != 0) {
        ++g_gx.frame.textureStateRecoveredLaterStage;
    }

    const bool simplePreset = g_gx.tevPresetValid[selectedStage] != 0;
    GXTevMode selectedMode = simplePreset ? g_gx.tevModes[selectedStage] : GX_MODULATE;
    bool classifiedCustomPreset = false;
    if (!simplePreset) {
        ++g_gx.frame.textureStateRecoveredCustomStage;
        classifiedCustomPreset = TryClassifyCustomTevPreset(selectedStage, selectedMode);
        if (classifiedCustomPreset) {
            CountCustomTevPreset(selectedMode);
        } else {
            ++g_gx.frame.textureStateCustomPresetUnknown;
            RecordCustomTevSignature(selectedStage, stageCount);
        }
    }
    // Exact GXSetTevOp-equivalent custom equations can use vitaGL's fixed
    // texture environment directly. Truly custom combiners still fall back to
    // MODULATE until the pixel combiner path grows a shader implementation.
    draw.texture.tevMode = static_cast<u8>(selectedMode);
    draw.texture.tevSimple =
        (g_gx.numTevStages == 1 && selectedStage == 0 &&
         (simplePreset || classifiedCustomPreset)) ? 1u : 0u;

    const TexGenState& texGen = g_gx.texGen[0];
    draw.texture.texGenType = static_cast<u8>(texGen.type);
    draw.texture.texGenSrc = static_cast<u8>(texGen.src);
    draw.texture.texGenNormalize = texGen.normalize ? 1u : 0u;
    const bool supportedType = texGen.type == GX_TG_MTX2x4 || texGen.type == GX_TG_MTX3x4;
    const bool supportedSource = texGen.src == GX_TG_TEX0 || texGen.src == GX_TG_POS;
    const bool mainIdentity = texGen.mtx == GX_IDENTITY;
    const bool postIdentity = texGen.postMtx == GX_PTIDENTITY;
    if (!supportedType || !supportedSource) {
        draw.texture.texGenMode = 2u;
    } else if (mainIdentity && postIdentity && !texGen.normalize && texGen.src == GX_TG_TEX0) {
        draw.texture.texGenMode = 0u;
    } else {
        draw.texture.texGenMode = 1u;
        if (!mainIdentity) {
            const size_t slot = static_cast<size_t>(texGen.mtx / 3u);
            if (slot < g_gx.texMtx.size()) {
                draw.texture.texGenMtx = g_gx.texMtx[slot];
            } else {
                draw.texture.texGenMode = 2u;
            }
        }
        if (!postIdentity) {
            if (texGen.postMtx >= GX_PTTEXMTX0) {
                const size_t slot = static_cast<size_t>((texGen.postMtx - GX_PTTEXMTX0) / 3u);
                if (slot < g_gx.postTexMtx.size()) {
                    draw.texture.texGenPostMtx = g_gx.postTexMtx[slot];
                } else {
                    draw.texture.texGenMode = 2u;
                }
            } else {
                draw.texture.texGenMode = 2u;
            }
        }
        if (mainIdentity) {
            draw.texture.texGenMtx = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
            };
        }
        if (postIdentity) {
            draw.texture.texGenPostMtx = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
            };
        }
    }

    const TexMeta& texture = Tex(obj);
    draw.texture.data = texture.data;
    draw.texture.dataRevision = texture.dataRevision;
    draw.texture.globalEpoch = g_gx.textureGlobalEpoch;
    draw.texture.format = texture.format;
    draw.texture.width = texture.width;
    draw.texture.height = texture.height;
    draw.texture.wrapS = static_cast<u8>(texture.wrapS);
    draw.texture.wrapT = static_cast<u8>(texture.wrapT);
    draw.texture.minFilter = static_cast<u8>(texture.minFilter);
    draw.texture.magFilter = static_cast<u8>(texture.magFilter);
    draw.texture.mipmap = texture.mipmap ? 1u : 0u;
    draw.texture.enabled = texture.data && texture.width != 0 && texture.height != 0 ? 1u : 0u;
    if (!draw.texture.enabled) {
        ++g_gx.frame.textureStateInvalidObject;
    }
    if (draw.texture.enabled && g_guestWriteGeneration) {
        const uint32_t sourceBytes = TextureLevelSize(texture.width, texture.height, texture.format);
        if (sourceBytes != 0) {
            draw.texture.sourceGeneration = g_guestWriteGeneration(texture.data, sourceBytes);
        }
    }

    // THP's MoviePaneHandler binds three tiled I8 planes to TEXMAP0/1/2 and
    // combines them with an 11-stage YUV TEV program. The Vita packet bridge
    // currently represents one sampled texture, so retain the chroma planes
    // and let the render worker produce the equivalent RGBA texture once per
    // decoded frame.
    if (g_gx.numTevStages >= 3u && g_gx.textures[0] && g_gx.textures[1] &&
        g_gx.textures[2]) {
        const TexMeta& y = Tex(g_gx.textures[0]);
        const TexMeta& u = Tex(g_gx.textures[1]);
        const TexMeta& v = Tex(g_gx.textures[2]);
        const bool dimensionsMatch = y.width != 0u && y.height != 0u &&
            u.width == v.width && u.height == v.height &&
            u.width == static_cast<u16>((y.width + 1u) / 2u) &&
            u.height == static_cast<u16>((y.height + 1u) / 2u);
        if (y.data && u.data && v.data && y.format == GX_TF_I8 &&
            u.format == GX_TF_I8 && v.format == GX_TF_I8 && dimensionsMatch) {
            draw.texture.data = y.data;
            draw.texture.dataRevision = y.dataRevision;
            draw.texture.format = y.format;
            draw.texture.width = y.width;
            draw.texture.height = y.height;
            draw.texture.thpUData = u.data;
            draw.texture.thpVData = v.data;
            draw.texture.thpURevision = u.dataRevision;
            draw.texture.thpVRevision = v.dataRevision;
            draw.texture.thpChromaWidth = u.width;
            draw.texture.thpChromaHeight = u.height;
            draw.texture.thpYuv420 = 1u;
            const uint32_t yBytes = TextureLevelSize(y.width, y.height, y.format);
            const uint32_t uBytes = TextureLevelSize(u.width, u.height, u.format);
            if (g_guestWriteGeneration) {
                draw.texture.sourceGeneration = g_guestWriteGeneration(y.data, yBytes);
                draw.texture.thpUGeneration = g_guestWriteGeneration(u.data, uBytes);
                draw.texture.thpVGeneration = g_guestWriteGeneration(v.data, uBytes);
            }
        }
    }
}

void CaptureDrawState(GeometryDraw& draw) {
    CaptureDrawTransform(draw);
    CaptureDrawRasterState(draw);
    CaptureDrawTextureState(draw);
}

void InitializeTransformDefaults() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    g_gx.projection = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    constexpr std::array<f32, 12> identity3x4{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    g_gx.posMtx.fill(identity3x4);
    g_gx.nrmMtx.fill(identity3x4);
    g_gx.texMtx.fill(identity3x4);
    g_gx.postTexMtx.fill(identity3x4);
    for (size_t i = 0; i < g_gx.texGen.size(); ++i) {
        g_gx.texGen[i].type = GX_TG_MTX2x4;
        g_gx.texGen[i].src = static_cast<GXTexGenSrc>(GX_TG_TEX0 + i);
        g_gx.texGen[i].mtx = GX_IDENTITY;
        g_gx.texGen[i].normalize = GX_FALSE;
        g_gx.texGen[i].postMtx = GX_PTIDENTITY;
    }
    g_gx.tevTexCoords.fill(GX_TEXCOORD_NULL);
    g_gx.tevTexMaps.fill(GX_TEXMAP_NULL);
    g_gx.tevModes.fill(GX_MODULATE);
    g_gx.tevPresetValid.fill(1u);
    g_gx.pendingPnMtxRef = DefaultPnMtxRef();
}

bool TransformVertex(const GeometryDraw& draw, u8 pnMtxRef,
                     const RenderVertex& source, RenderVertex& output) {
    const size_t slot = static_cast<size_t>(pnMtxRef & ~kPnMtxExplicitBit);
    if (slot >= draw.transform.posMtx.size()) {
        return false;
    }

    const auto& m = draw.transform.posMtx[slot];
    const f32 vx = m[0] * source.x + m[1] * source.y + m[2] * source.z + m[3];
    const f32 vy = m[4] * source.x + m[5] * source.y + m[6] * source.z + m[7];
    const f32 vz = m[8] * source.x + m[9] * source.y + m[10] * source.z + m[11];

    const auto& p = draw.transform.projection;
    const f32 clipX = p[0] * vx + p[1] * vy + p[2] * vz + p[3];
    const f32 clipY = p[4] * vx + p[5] * vy + p[6] * vz + p[7];
    const f32 clipZ = p[8] * vx + p[9] * vy + p[10] * vz + p[11];
    const f32 clipW = p[12] * vx + p[13] * vy + p[14] * vz + p[15];
    if (!std::isfinite(clipW) || std::fabs(clipW) < 1.0e-8f) {
        return false;
    }

    const f32 invW = 1.0f / clipW;
    output = source;
    output.x = clipX * invW;
    output.y = clipY * invW;
    output.z = clipZ * invW;
    return std::isfinite(output.x) && std::isfinite(output.y) && std::isfinite(output.z);
}

bool TransformTexCoord(const DrawTextureState& texture,
                       const RenderVertex& source, RenderVertex& output) {
    if (texture.texGenMode == 0u) {
        output.s = source.s;
        output.t = source.t;
        return true;
    }
    if (texture.texGenMode != 1u) {
        output.s = source.s;
        output.t = source.t;
        return false;
    }

    f32 x = source.s;
    f32 y = source.t;
    f32 z = 1.0f;
    if (static_cast<GXTexGenSrc>(texture.texGenSrc) == GX_TG_POS) {
        x = source.x;
        y = source.y;
        z = source.z;
    } else if (static_cast<GXTexGenSrc>(texture.texGenSrc) != GX_TG_TEX0) {
        output.s = source.s;
        output.t = source.t;
        return false;
    }

    const auto& m = texture.texGenMtx;
    f32 u = m[0] * x + m[1] * y + m[2] * z + m[3];
    f32 v = m[4] * x + m[5] * y + m[6] * z + m[7];
    f32 q = m[8] * x + m[9] * y + m[10] * z + m[11];

    if (texture.texGenNormalize) {
        const f32 lenSq = u * u + v * v + q * q;
        if (std::isfinite(lenSq) && lenSq > 1.0e-12f) {
            const f32 invLen = 1.0f / std::sqrt(lenSq);
            u *= invLen;
            v *= invLen;
            q *= invLen;
        }
    }

    const auto& post = texture.texGenPostMtx;
    const f32 pu = post[0] * u + post[1] * v + post[2] * q + post[3];
    const f32 pv = post[4] * u + post[5] * v + post[6] * q + post[7];
    const f32 pq = post[8] * u + post[9] * v + post[10] * q + post[11];

    if (static_cast<GXTexGenType>(texture.texGenType) == GX_TG_MTX3x4) {
        if (!std::isfinite(pq) || std::fabs(pq) < 1.0e-8f) {
            return false;
        }
        output.s = pu / pq;
        output.t = pv / pq;
    } else {
        output.s = pu;
        output.t = pv;
    }
    return std::isfinite(output.s) && std::isfinite(output.t);
}

void AppendPosition(f32 x, f32 y, f32 z) {
    if (g_gx.activeDraw < 0 ||
        g_gx.geometry.vertexCount >= g_gx.geometry.vertices.size()) {
        ++g_gx.geometry.droppedVertices;
        return;
    }

    GeometryDraw& draw = g_gx.geometry.draws[static_cast<size_t>(g_gx.activeDraw)];
    if (draw.vertexCount >= g_gx.declaredVertices) {
        ++g_gx.geometry.droppedVertices;
        return;
    }

    const u16 vertexIndex = g_gx.geometry.vertexCount++;
    RenderVertex& vertex = g_gx.geometry.vertices[vertexIndex];
    vertex = {x, y, z, 255, 255, 255, 255};
    g_gx.geometry.pnMtxRefs[vertexIndex] = g_gx.pendingPnMtxRef;
    g_gx.pendingPnMtxRef = DefaultPnMtxRef();
    ++draw.vertexCount;
}

void SetCurrentVertexColor(u8 r, u8 g, u8 b, u8 a) {
    if (g_gx.activeDraw < 0 || g_gx.geometry.vertexCount == 0) {
        return;
    }
    const GeometryDraw& draw = g_gx.geometry.draws[static_cast<size_t>(g_gx.activeDraw)];
    if (g_gx.geometry.vertexCount <= draw.firstVertex) {
        return;
    }
    RenderVertex& vertex = g_gx.geometry.vertices[g_gx.geometry.vertexCount - 1];
    vertex.r = r;
    vertex.g = g;
    vertex.b = b;
    vertex.a = a;
}

void SetCurrentVertexTexCoord(f32 s, f32 t) {
    if (g_gx.activeDraw < 0 || g_gx.geometry.vertexCount == 0) {
        return;
    }
    const GeometryDraw& draw = g_gx.geometry.draws[static_cast<size_t>(g_gx.activeDraw)];
    if (g_gx.geometry.vertexCount <= draw.firstVertex) {
        return;
    }
    RenderVertex& vertex = g_gx.geometry.vertices[g_gx.geometry.vertexCount - 1];
    vertex.s = s;
    vertex.t = t;
}

f32 DecodeImmediateTexCoordInteger(int32_t value) {
    const VtxFmtState& fmt =
        g_gx.vtxFmt[static_cast<size_t>(g_gx.currentVtxFmt)][static_cast<size_t>(GX_VA_TEX0)];
    if (fmt.frac == 0) {
        return static_cast<f32>(value);
    }
    return static_cast<f32>(value) / static_cast<f32>(1u << fmt.frac);
}

f32 DecodeImmediatePositionInteger(int32_t value) {
    const VtxFmtState& fmt =
        g_gx.vtxFmt[static_cast<size_t>(g_gx.currentVtxFmt)][static_cast<size_t>(GX_VA_POS)];
    if (fmt.frac == 0) {
        return static_cast<f32>(value);
    }
    return static_cast<f32>(value) / static_cast<f32>(1u << fmt.frac);
}

bool IsMatrixIndexAttr(GXAttr attr) {
    return attr >= GX_VA_PNMTXIDX && attr <= GX_VA_TEX7MTXIDX;
}

uint32_t CompByteSize(GXCompType type) {
    switch (type) {
    case GX_U8:
    case GX_S8:
        return 1;
    case GX_U16:
    case GX_S16:
        return 2;
    case GX_F32:
        return 4;
    default:
        return 0;
    }
}

uint32_t AttrCompCount(GXAttr attr, const VtxFmtState& fmt) {
    switch (attr) {
    case GX_VA_POS:
        return fmt.cnt == GX_POS_XY ? 2u : 3u;
    case GX_VA_NRM:
        return (fmt.cnt == GX_NRM_NBT || fmt.cnt == GX_NRM_NBT3) ? 9u : 3u;
    case GX_VA_CLR0:
    case GX_VA_CLR1:
        return fmt.cnt == GX_CLR_RGB ? 3u : 4u;
    case GX_VA_TEX0:
    case GX_VA_TEX1:
    case GX_VA_TEX2:
    case GX_VA_TEX3:
    case GX_VA_TEX4:
    case GX_VA_TEX5:
    case GX_VA_TEX6:
    case GX_VA_TEX7:
        return fmt.cnt == GX_TEX_S ? 1u : 2u;
    default:
        return 0;
    }
}

uint32_t ColorByteSize(GXCompType type, GXCompCnt cnt) {
    switch (type) {
    case GX_RGB565: return 2;
    case GX_RGB8: return 3;
    case GX_RGBX8: return 4;
    case GX_RGBA4: return 2;
    case GX_RGBA6: return 3;
    case GX_RGBA8: return 4;
    default: return cnt == GX_CLR_RGB ? 3u : 4u;
    }
}

uint16_t ReadU16(const uint8_t* src, bool littleEndian) {
    if (littleEndian) {
        return static_cast<uint16_t>(src[0]) |
               static_cast<uint16_t>(src[1] << 8);
    }
    return static_cast<uint16_t>(src[0] << 8) |
           static_cast<uint16_t>(src[1]);
}

uint32_t ReadU32(const uint8_t* src, bool littleEndian) {
    if (littleEndian) {
        return static_cast<uint32_t>(src[0]) |
               (static_cast<uint32_t>(src[1]) << 8) |
               (static_cast<uint32_t>(src[2]) << 16) |
               (static_cast<uint32_t>(src[3]) << 24);
    }
    return (static_cast<uint32_t>(src[0]) << 24) |
           (static_cast<uint32_t>(src[1]) << 16) |
           (static_cast<uint32_t>(src[2]) << 8) |
           static_cast<uint32_t>(src[3]);
}

float ReadF32(const uint8_t* src, bool littleEndian) {
    const uint32_t raw = ReadU32(src, littleEndian);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

bool ApplyXfPacketImpl(const uint8_t* packet, uint32_t packetBytes) {
    if (!packet || packetBytes < 9 || (packet[0] & 0xF8u) != 0x10u) {
        return false;
    }

    const uint32_t header = ReadU32(packet + 1, false);
    const uint32_t wordCount = ((header >> 16) & 0xFFFFu) + 1u;
    const uint32_t xfAddress = header & 0xFFFFu;
    const uint64_t required = 5ull + static_cast<uint64_t>(wordCount) * 4ull;
    if (required != packetBytes) {
        return false;
    }

    bool projectionDirty = false;
    bool viewportDirty = false;
    uint64_t supportedWords = 0;
    for (uint32_t i = 0; i < wordCount; ++i) {
        const uint32_t addr = xfAddress + i;
        const uint8_t* word = packet + 5u + i * 4u;

        if (addr < 0x78u) {
            const size_t slot = addr / 12u;
            const size_t component = addr % 12u;
            if (slot < g_gx.posMtx.size()) {
                g_gx.posMtx[slot][component] = ReadF32(word, false);
                ++g_gx.frame.xfPositionMatrixWords;
                ++supportedWords;
                continue;
            }
        }

        if (addr >= 0x78u && addr < 0xF0u) {
            // GX texture matrices share the post-transform matrix address space
            // immediately after the ten PN matrices. GX_TEXMTX0 (30) starts at
            // XF word 0x78, i.e. combined matrix slot 10.
            const size_t slot = addr / 12u;
            const size_t component = addr % 12u;
            if (slot < g_gx.texMtx.size()) {
                g_gx.texMtx[slot][component] = ReadF32(word, false);
                ++supportedWords;
                continue;
            }
        }

        if (addr >= 0x400u && addr < 0x45Au) {
            const uint32_t rel = addr - 0x400u;
            const size_t slot = rel / 9u;
            const size_t component = rel % 9u;
            if (slot < g_gx.nrmMtx.size()) {
                const size_t row = component / 3u;
                const size_t col = component % 3u;
                g_gx.nrmMtx[slot][row * 4u + col] = ReadF32(word, false);
                ++g_gx.frame.xfNormalMatrixWords;
                ++supportedWords;
                continue;
            }
        }

        if (addr >= 0x500u && addr < 0x5F0u) {
            const uint32_t rel = addr - 0x500u;
            const size_t slot = rel / 12u;
            const size_t component = rel % 12u;
            if (slot < g_gx.postTexMtx.size()) {
                g_gx.postTexMtx[slot][component] = ReadF32(word, false);
                ++supportedWords;
                continue;
            }
        }

        if (addr == 0x1018u) {
            const uint32_t raw = ReadU32(word, false);
            g_gx.currentMtx = raw & 0x3Fu;
            for (size_t tc = 0; tc < 4u && tc < g_gx.texGen.size(); ++tc) {
                g_gx.texGen[tc].mtx = (raw >> (6u + static_cast<uint32_t>(tc) * 6u)) & 0x3Fu;
            }
            g_gx.pendingPnMtxRef = DefaultPnMtxRef();
            ++g_gx.frame.xfMatrixIndexWrites;
            ++supportedWords;
            continue;
        }

        if (addr == 0x1019u) {
            const uint32_t raw = ReadU32(word, false);
            for (size_t tc = 4u; tc < 8u && tc < g_gx.texGen.size(); ++tc) {
                g_gx.texGen[tc].mtx =
                    (raw >> (static_cast<uint32_t>(tc - 4u) * 6u)) & 0x3Fu;
            }
            ++g_gx.frame.xfMatrixIndexWrites;
            ++supportedWords;
            continue;
        }

        if (addr >= 0x101Au && addr <= 0x101Fu) {
            g_gx.xfViewport[addr - 0x101Au] = ReadF32(word, false);
            ++g_gx.frame.xfViewportWrites;
            ++supportedWords;
            viewportDirty = true;
            continue;
        }

        if (addr >= 0x1020u && addr <= 0x1025u) {
            g_gx.xfProjection[addr - 0x1020u] = ReadF32(word, false);
            ++g_gx.frame.xfProjectionWrites;
            ++supportedWords;
            projectionDirty = true;
            continue;
        }

        if (addr == 0x1026u) {
            const uint32_t rawType = ReadU32(word, false);
            g_gx.projectionType = rawType == static_cast<uint32_t>(GX_ORTHOGRAPHIC)
                ? GX_ORTHOGRAPHIC : GX_PERSPECTIVE;
            ++g_gx.frame.xfProjectionWrites;
            ++supportedWords;
            projectionDirty = true;
            continue;
        }

        // XF_NUMTEXGENS. Real GXSetNumTexGens mirrors this count into XF state,
        // and display-list replay reaches this register without necessarily
        // calling the high-level host symbol.
        if (addr == 0x103Fu) {
            g_gx.numTexGens = static_cast<u8>(ReadU32(word, false) & 0xFu);
            ++supportedWords;
            continue;
        }

        if (addr >= 0x1040u && addr < 0x1040u + GX_MAX_TEXCOORD) {
            const size_t tc = static_cast<size_t>(addr - 0x1040u);
            const uint32_t raw = ReadU32(word, false);
            TexGenState& texGen = g_gx.texGen[tc];
            const uint32_t tgType = (raw >> 4u) & 0x7u;
            const uint32_t srcRow = (raw >> 7u) & 0x1Fu;
            if (tgType == 0u) {
                texGen.type = ((raw >> 1u) & 1u) ? GX_TG_MTX3x4 : GX_TG_MTX2x4;
            } else if (tgType == 1u) {
                texGen.type = static_cast<GXTexGenType>(GX_TG_BUMP0 + ((raw >> 15u) & 0x7u));
            } else {
                texGen.type = GX_TG_SRTG;
            }
            static constexpr std::array<GXTexGenSrc, 13> kRowToSrc{
                GX_TG_POS, GX_TG_NRM, GX_TG_COLOR0, GX_TG_BINRM, GX_TG_TANGENT,
                GX_TG_TEX0, GX_TG_TEX1, GX_TG_TEX2, GX_TG_TEX3,
                GX_TG_TEX4, GX_TG_TEX5, GX_TG_TEX6, GX_TG_TEX7,
            };
            if (srcRow < kRowToSrc.size()) {
                texGen.src = kRowToSrc[srcRow];
            }
            ++supportedWords;
            continue;
        }

        if (addr >= 0x1050u && addr < 0x1050u + GX_MAX_TEXCOORD) {
            const size_t tc = static_cast<size_t>(addr - 0x1050u);
            const uint32_t raw = ReadU32(word, false);
            g_gx.texGen[tc].postMtx = (raw & 0x3Fu) + GX_PTTEXMTX0;
            g_gx.texGen[tc].normalize = ((raw >> 8u) & 1u) ? GX_TRUE : GX_FALSE;
            ++supportedWords;
            continue;
        }

        ++g_gx.frame.xfUnsupportedWords;
    }

    if (projectionDirty) {
        RebuildProjectionFromXf();
    }
    if (viewportDirty) {
        RebuildViewportFromXf();
    }
    ++g_gx.frame.xfPacketsApplied;
    g_gx.frame.xfWordsApplied += supportedWords;
    return true;
}

float DecodeComp(const uint8_t* src, GXCompType type, u8 frac, bool littleEndian) {
    uint32_t raw = 0;
    switch (type) {
    case GX_U8:
    case GX_S8:
        raw = src[0];
        break;
    case GX_U16:
    case GX_S16:
        raw = ReadU16(src, littleEndian);
        break;
    case GX_F32:
        raw = ReadU32(src, littleEndian);
        break;
    default:
        return 0.0f;
    }

    if (type == GX_F32) {
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    float value = 0.0f;
    switch (type) {
    case GX_U8: value = static_cast<float>(static_cast<u8>(raw)); break;
    case GX_S8: value = static_cast<float>(static_cast<s8>(raw)); break;
    case GX_U16: value = static_cast<float>(static_cast<u16>(raw)); break;
    case GX_S16: value = static_cast<float>(static_cast<s16>(raw)); break;
    default: break;
    }
    if (frac != 0) {
        value /= static_cast<float>(1u << frac);
    }
    return value;
}

GXColor DecodeColor(const uint8_t* src, GXCompType type, GXCompCnt cnt, bool littleEndian) {
    GXColor out{255, 255, 255, 255};
    switch (type) {
    case GX_RGB565: {
        const uint16_t packed = ReadU16(src, littleEndian);
        out.r = static_cast<u8>(((packed >> 11) & 0x1fu) * 255u / 31u);
        out.g = static_cast<u8>(((packed >> 5) & 0x3fu) * 255u / 63u);
        out.b = static_cast<u8>((packed & 0x1fu) * 255u / 31u);
        break;
    }
    case GX_RGB8:
    case GX_RGBX8:
        out.r = src[0]; out.g = src[1]; out.b = src[2];
        break;
    case GX_RGBA4: {
        const uint16_t packed = ReadU16(src, littleEndian);
        out.r = static_cast<u8>(((packed >> 12) & 0x0fu) * 17u);
        out.g = static_cast<u8>(((packed >> 8) & 0x0fu) * 17u);
        out.b = static_cast<u8>(((packed >> 4) & 0x0fu) * 17u);
        out.a = static_cast<u8>((packed & 0x0fu) * 17u);
        break;
    }
    case GX_RGBA6: {
        uint32_t packed = 0;
        if (littleEndian) {
            packed = static_cast<uint32_t>(src[2]) << 16 |
                     static_cast<uint32_t>(src[1]) << 8 |
                     static_cast<uint32_t>(src[0]);
        } else {
            packed = static_cast<uint32_t>(src[0]) << 16 |
                     static_cast<uint32_t>(src[1]) << 8 |
                     static_cast<uint32_t>(src[2]);
        }
        out.r = static_cast<u8>(((packed >> 18) & 0x3fu) * 255u / 63u);
        out.g = static_cast<u8>(((packed >> 12) & 0x3fu) * 255u / 63u);
        out.b = static_cast<u8>(((packed >> 6) & 0x3fu) * 255u / 63u);
        out.a = static_cast<u8>((packed & 0x3fu) * 255u / 63u);
        break;
    }
    case GX_RGBA8:
        out.r = src[0]; out.g = src[1]; out.b = src[2]; out.a = src[3];
        break;
    default:
        out.r = src[0]; out.g = src[1]; out.b = src[2];
        out.a = cnt == GX_CLR_RGBA ? src[3] : 255;
        break;
    }
    return out;
}

uint32_t DirectAttrByteSize(GXAttr attr, const VtxFmtState& fmt) {
    if (IsMatrixIndexAttr(attr)) {
        return 1;
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        return ColorByteSize(fmt.type, fmt.cnt);
    }
    return AttrCompCount(attr, fmt) * CompByteSize(fmt.type);
}

uint32_t IndexedElementByteSize(GXAttr attr, const VtxFmtState& fmt) {
    if (attr == GX_VA_NRM && fmt.cnt == GX_NRM_NBT3) {
        return 3u * CompByteSize(fmt.type);
    }
    return DirectAttrByteSize(attr, fmt);
}

bool ArrayElement(const ArrayState& array, uint32_t index, uint32_t bytes, const uint8_t*& out) {
    if (!array.data || array.stride == 0 || bytes == 0) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(index) * array.stride;
    if (offset + bytes > array.size) {
        return false;
    }
    out = static_cast<const uint8_t*>(array.data) + static_cast<size_t>(offset);
    return true;
}

bool DecodePosition(const uint8_t* src, const VtxFmtState& fmt, bool littleEndian, RenderVertex& vertex) {
    const uint32_t step = CompByteSize(fmt.type);
    const uint32_t count = AttrCompCount(GX_VA_POS, fmt);
    if (step == 0 || (count != 2 && count != 3)) {
        return false;
    }
    vertex.x = DecodeComp(src, fmt.type, fmt.frac, littleEndian);
    vertex.y = DecodeComp(src + step, fmt.type, fmt.frac, littleEndian);
    vertex.z = count == 3 ? DecodeComp(src + 2u * step, fmt.type, fmt.frac, littleEndian) : 0.0f;
    return true;
}

bool DecodeTexCoord(const uint8_t* src, const VtxFmtState& fmt, bool littleEndian,
                    RenderVertex& vertex) {
    const uint32_t step = CompByteSize(fmt.type);
    const uint32_t count = AttrCompCount(GX_VA_TEX0, fmt);
    if (step == 0 || (count != 1 && count != 2)) {
        return false;
    }
    vertex.s = DecodeComp(src, fmt.type, fmt.frac, littleEndian);
    vertex.t = count == 2 ? DecodeComp(src + step, fmt.type, fmt.frac, littleEndian) : 0.0f;
    return std::isfinite(vertex.s) && std::isfinite(vertex.t);
}

size_t PrimitiveBucket(GXPrimitive primitive) {
    switch (primitive) {
    case GX_QUADS: return 0;
    case GX_TRIANGLES: return 1;
    case GX_TRIANGLESTRIP: return 2;
    case GX_TRIANGLEFAN: return 3;
    case GX_LINES: return 4;
    case GX_LINESTRIP: return 5;
    case GX_POINTS: return 6;
    }
    return 1;
}

enum class RawDecodeFailReason : uint8_t {
    None = 0,
    InvalidInput,
    Capacity,
    DirectStreamBounds,
    DirectPosition,
    DirectTexCoord,
    InvalidIndexedDescriptor,
    IndexedStreamBounds,
    IndexedArrayBounds,
    IndexedPosition,
    IndexedTexCoord,
    CursorMismatch,
};

struct RawDecodeFailure {
    RawDecodeFailReason reason = RawDecodeFailReason::None;
    uint16_t vertex = 0;
    uint8_t attr = 0xff;
    uint8_t desc = 0;
    uint32_t index = 0;
    uint32_t elementBytes = 0;
    uint32_t arraySize = 0;
    uint32_t arrayStride = 0;
    uint32_t cursorOffset = 0;
};

thread_local RawDecodeFailure g_rawDecodeFailure{};

bool RawDecodeFail(RawDecodeFailReason reason, uint16_t vertex = 0, GXAttr attr = GX_VA_NULL,
                   GXAttrType desc = GX_NONE, uint32_t index = 0, uint32_t elementBytes = 0,
                   uint32_t arraySize = 0, uint32_t arrayStride = 0,
                   uint32_t cursorOffset = 0) {
    g_rawDecodeFailure.reason = reason;
    g_rawDecodeFailure.vertex = vertex;
    g_rawDecodeFailure.attr = attr == GX_VA_NULL ? 0xffu : static_cast<uint8_t>(attr);
    g_rawDecodeFailure.desc = static_cast<uint8_t>(desc);
    g_rawDecodeFailure.index = index;
    g_rawDecodeFailure.elementBytes = elementBytes;
    g_rawDecodeFailure.arraySize = arraySize;
    g_rawDecodeFailure.arrayStride = arrayStride;
    g_rawDecodeFailure.cursorOffset = cursorOffset;
    return false;
}

const char* RawDecodeFailName(RawDecodeFailReason reason) {
    switch (reason) {
    case RawDecodeFailReason::None: return "none";
    case RawDecodeFailReason::InvalidInput: return "input";
    case RawDecodeFailReason::Capacity: return "capacity";
    case RawDecodeFailReason::DirectStreamBounds: return "direct_stream";
    case RawDecodeFailReason::DirectPosition: return "direct_pos";
    case RawDecodeFailReason::DirectTexCoord: return "direct_tex";
    case RawDecodeFailReason::InvalidIndexedDescriptor: return "indexed_desc";
    case RawDecodeFailReason::IndexedStreamBounds: return "indexed_stream";
    case RawDecodeFailReason::IndexedArrayBounds: return "indexed_array";
    case RawDecodeFailReason::IndexedPosition: return "indexed_pos";
    case RawDecodeFailReason::IndexedTexCoord: return "indexed_tex";
    case RawDecodeFailReason::CursorMismatch: return "cursor";
    }
    return "unknown";
}

bool DecodeRawDraw(GXPrimitive primitive, GXVtxFmt fmt, const uint8_t* vertices,
                   uint16_t vtxCount, uint32_t vertexBytes) {
    g_rawDecodeFailure = {};
    if (!vertices || vtxCount == 0 || vertexBytes == 0 || fmt < 0 || fmt >= GX_MAX_VTXFMT) {
        return RawDecodeFail(RawDecodeFailReason::InvalidInput);
    }
    if (g_gx.geometry.drawCount >= g_gx.geometry.draws.size() ||
        static_cast<size_t>(g_gx.geometry.vertexCount) + vtxCount > g_gx.geometry.vertices.size()) {
        g_gx.geometry.droppedVertices += vtxCount;
        ++g_gx.frame.rawDrawCapacityFailures;
        return RawDecodeFail(RawDecodeFailReason::Capacity);
    }

    const uint8_t* cursor = vertices;
    const uint8_t* const end = vertices + vertexBytes;
    uint64_t directAttrs = 0;
    uint64_t indexedAttrs = 0;
    const u16 firstVertex = g_gx.geometry.vertexCount;

    for (uint16_t vertexIndex = 0; vertexIndex < vtxCount; ++vertexIndex) {
        RenderVertex decoded{};
        u8 pnMtxRef = DefaultPnMtxRef();
        for (int attrValue = GX_VA_PNMTXIDX; attrValue <= GX_VA_TEX7; ++attrValue) {
            const GXAttr attr = static_cast<GXAttr>(attrValue);
            const GXAttrType desc = g_gx.vtxDesc[static_cast<size_t>(attr)];
            if (desc == GX_NONE) {
                continue;
            }
            const VtxFmtState& attrFmt = g_gx.vtxFmt[static_cast<size_t>(fmt)][static_cast<size_t>(attr)];

            if (desc == GX_DIRECT) {
                const uint32_t bytes = DirectAttrByteSize(attr, attrFmt);
                if (bytes == 0 || static_cast<size_t>(end - cursor) < bytes) {
                    return RawDecodeFail(RawDecodeFailReason::DirectStreamBounds, vertexIndex, attr,
                                         desc, 0, bytes, 0, 0,
                                         static_cast<uint32_t>(cursor - vertices));
                }
                if (attr == GX_VA_PNMTXIDX) {
                    pnMtxRef = static_cast<u8>(PnMtxSlot(cursor[0]) | kPnMtxExplicitBit);
                }
                if (attr == GX_VA_POS && !DecodePosition(cursor, attrFmt, false, decoded)) {
                    return RawDecodeFail(RawDecodeFailReason::DirectPosition, vertexIndex, attr,
                                         desc, 0, bytes, 0, 0,
                                         static_cast<uint32_t>(cursor - vertices));
                }
                if (attr == GX_VA_CLR0) {
                    const GXColor color = DecodeColor(cursor, attrFmt.type, attrFmt.cnt, false);
                    decoded.r = color.r; decoded.g = color.g; decoded.b = color.b; decoded.a = color.a;
                }
                if (attr == GX_VA_TEX0 && !DecodeTexCoord(cursor, attrFmt, false, decoded)) {
                    return RawDecodeFail(RawDecodeFailReason::DirectTexCoord, vertexIndex, attr,
                                         desc, 0, bytes, 0, 0,
                                         static_cast<uint32_t>(cursor - vertices));
                }
                cursor += bytes;
                ++directAttrs;
                continue;
            }

            if (desc != GX_INDEX8 && desc != GX_INDEX16 || IsMatrixIndexAttr(attr)) {
                return RawDecodeFail(RawDecodeFailReason::InvalidIndexedDescriptor, vertexIndex,
                                     attr, desc, 0, 0, 0, 0,
                                     static_cast<uint32_t>(cursor - vertices));
            }

            const uint32_t indexBytes = desc == GX_INDEX8 ? 1u : 2u;
            const uint32_t indexCount =
                (attr == GX_VA_NRM && attrFmt.cnt == GX_NRM_NBT3) ? 3u : 1u;
            if (static_cast<size_t>(end - cursor) < indexBytes * indexCount) {
                return RawDecodeFail(RawDecodeFailReason::IndexedStreamBounds, vertexIndex, attr,
                                     desc, 0, indexBytes * indexCount, 0, 0,
                                     static_cast<uint32_t>(cursor - vertices));
            }

            const ArrayState& array = g_gx.arrays[static_cast<size_t>(attr)];
            const uint32_t elementBytes = IndexedElementByteSize(attr, attrFmt);
            for (uint32_t indexSlot = 0; indexSlot < indexCount; ++indexSlot) {
                const uint32_t index = desc == GX_INDEX8
                    ? cursor[indexSlot]
                    : ReadU16(cursor + indexSlot * 2u, false);
                const uint8_t* element = nullptr;
                if (!ArrayElement(array, index, elementBytes, element)) {
                    return RawDecodeFail(RawDecodeFailReason::IndexedArrayBounds, vertexIndex, attr,
                                         desc, index, elementBytes, array.size, array.stride,
                                         static_cast<uint32_t>(cursor - vertices));
                }
                if (indexSlot == 0 && attr == GX_VA_POS &&
                    !DecodePosition(element, attrFmt, array.littleEndian, decoded)) {
                    return RawDecodeFail(RawDecodeFailReason::IndexedPosition, vertexIndex, attr,
                                         desc, index, elementBytes, array.size, array.stride,
                                         static_cast<uint32_t>(cursor - vertices));
                }
                if (indexSlot == 0 && attr == GX_VA_CLR0) {
                    const GXColor color = DecodeColor(element, attrFmt.type, attrFmt.cnt, array.littleEndian);
                    decoded.r = color.r; decoded.g = color.g; decoded.b = color.b; decoded.a = color.a;
                }
                if (indexSlot == 0 && attr == GX_VA_TEX0 &&
                    !DecodeTexCoord(element, attrFmt, array.littleEndian, decoded)) {
                    return RawDecodeFail(RawDecodeFailReason::IndexedTexCoord, vertexIndex, attr,
                                         desc, index, elementBytes, array.size, array.stride,
                                         static_cast<uint32_t>(cursor - vertices));
                }
            }
            cursor += indexBytes * indexCount;
            ++indexedAttrs;
        }
        const size_t outputVertex = static_cast<size_t>(firstVertex) + vertexIndex;
        g_gx.geometry.vertices[outputVertex] = decoded;
        g_gx.geometry.pnMtxRefs[outputVertex] = pnMtxRef;
    }

    if (cursor != end) {
        return RawDecodeFail(RawDecodeFailReason::CursorMismatch, vtxCount, GX_VA_NULL, GX_NONE,
                             0, vertexBytes, 0, 0,
                             static_cast<uint32_t>(cursor - vertices));
    }

    GeometryDraw& draw = g_gx.geometry.draws[g_gx.geometry.drawCount++];
    draw = {};
    draw.primitive = primitive;
    draw.firstVertex = firstVertex;
    draw.vertexCount = vtxCount;
    CaptureDrawState(draw);
    g_gx.geometry.vertexCount = static_cast<u16>(firstVertex + vtxCount);
    ++g_gx.frame.drawCalls;
    g_gx.frame.vertices += vtxCount;
    g_gx.frame.rawDrawBytes += vertexBytes;
    ++g_gx.frame.rawDrawsDecoded;
    g_gx.frame.rawDirectAttributesDecoded += directAttrs;
    g_gx.frame.rawIndexedAttributesDecoded += indexedAttrs;
    ++g_gx.frame.primitiveDraws[PrimitiveBucket(primitive)];
    ++g_gx.frame.vertexFormatDraws[static_cast<size_t>(fmt)];
    return true;
}

FifoMeta& Fifo(GXFifoObj* fifo) {
    return *reinterpret_cast<FifoMeta*>(fifo);
}

const FifoMeta& Fifo(const GXFifoObj* fifo) {
    return *reinterpret_cast<const FifoMeta*>(fifo);
}

TexMeta& Tex(GXTexObj* obj) {
    return *reinterpret_cast<TexMeta*>(obj);
}

const TexMeta& Tex(const GXTexObj* obj) {
    return *reinterpret_cast<const TexMeta*>(obj);
}

TlutMeta& Tlut(GXTlutObj* obj) {
    return *reinterpret_cast<TlutMeta*>(obj);
}

LightMeta& Light(GXLightObj* obj) {
    return *reinterpret_cast<LightMeta*>(obj);
}

bool SupportedTextureFormat(u32 format) {
    return format == GX_TF_I4 || format == GX_TF_I8 ||
           format == GX_TF_IA4 || format == GX_TF_IA8 ||
           format == GX_TF_RGB565 || format == GX_TF_RGB5A3 ||
           format == GX_TF_RGBA8 || format == GX_TF_CMPR ||
           format == GX_TF_RGBA8_PC;
}

void StoreTexturePixel(u8* rgba, u16 width, u16 x, u16 y,
                       u8 r, u8 g, u8 b, u8 a) {
    const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
    rgba[offset + 0] = r;
    rgba[offset + 1] = g;
    rgba[offset + 2] = b;
    rgba[offset + 3] = a;
}

bool ConvertTextureLevel0(const DrawTextureState& texture, u8* rgba, size_t rgbaBytes) {
    if (!texture.data || !rgba || texture.width == 0 || texture.height == 0 ||
        texture.width > kMaxTextureDimension || texture.height > kMaxTextureDimension) {
        return false;
    }
    const size_t required = static_cast<size_t>(texture.width) * texture.height * 4u;
    if (required > rgbaBytes) {
        return false;
    }

    const auto* src = static_cast<const u8*>(texture.data);
    if (texture.format == GX_TF_RGBA8_PC) {
        std::memcpy(rgba, src, required);
        return true;
    }

    if (texture.format == GX_TF_I4) {
        const u32 tilesX = (static_cast<u32>(texture.width) + 7u) / 8u;
        const u32 tilesY = (static_cast<u32>(texture.height) + 7u) / 8u;
        for (u32 tileY = 0; tileY < tilesY; ++tileY) {
            for (u32 tileX = 0; tileX < tilesX; ++tileX) {
                const size_t tileOffset = (static_cast<size_t>(tileY) * tilesX + tileX) * 32u;
                for (u32 localY = 0; localY < 8; ++localY) {
                    for (u32 localX = 0; localX < 8; ++localX) {
                        const u16 x = static_cast<u16>(tileX * 8u + localX);
                        const u16 y = static_cast<u16>(tileY * 8u + localY);
                        if (x >= texture.width || y >= texture.height) {
                            continue;
                        }
                        const u32 pixel = localY * 8u + localX;
                        const u8 packed = src[tileOffset + pixel / 2u];
                        const u8 nibble = (pixel & 1u) ? (packed & 0x0fu) : (packed >> 4u);
                        const u8 intensity = static_cast<u8>(nibble * 17u);
                        StoreTexturePixel(rgba, texture.width, x, y,
                                          intensity, intensity, intensity, intensity);
                    }
                }
            }
        }
        return true;
    }

    if (texture.format == GX_TF_I8 || texture.format == GX_TF_IA4) {
        const u32 tilesX = (static_cast<u32>(texture.width) + 7u) / 8u;
        const u32 tilesY = (static_cast<u32>(texture.height) + 3u) / 4u;
        for (u32 tileY = 0; tileY < tilesY; ++tileY) {
            for (u32 tileX = 0; tileX < tilesX; ++tileX) {
                const size_t tileOffset = (static_cast<size_t>(tileY) * tilesX + tileX) * 32u;
                for (u32 localY = 0; localY < 4; ++localY) {
                    for (u32 localX = 0; localX < 8; ++localX) {
                        const u16 x = static_cast<u16>(tileX * 8u + localX);
                        const u16 y = static_cast<u16>(tileY * 4u + localY);
                        if (x >= texture.width || y >= texture.height) {
                            continue;
                        }
                        const u8 packed = src[tileOffset + localY * 8u + localX];
                        if (texture.format == GX_TF_I8) {
                            StoreTexturePixel(rgba, texture.width, x, y,
                                              packed, packed, packed, packed);
                        } else {
                            const u8 intensity = static_cast<u8>((packed & 0x0fu) * 17u);
                            const u8 alpha = static_cast<u8>((packed >> 4u) * 17u);
                            StoreTexturePixel(rgba, texture.width, x, y,
                                              intensity, intensity, intensity, alpha);
                        }
                    }
                }
            }
        }
        return true;
    }

    if (texture.format == GX_TF_IA8) {
        const u32 tilesX = (static_cast<u32>(texture.width) + 3u) / 4u;
        const u32 tilesY = (static_cast<u32>(texture.height) + 3u) / 4u;
        for (u32 tileY = 0; tileY < tilesY; ++tileY) {
            for (u32 tileX = 0; tileX < tilesX; ++tileX) {
                const size_t tileOffset = (static_cast<size_t>(tileY) * tilesX + tileX) * 32u;
                for (u32 localY = 0; localY < 4; ++localY) {
                    for (u32 localX = 0; localX < 4; ++localX) {
                        const u16 x = static_cast<u16>(tileX * 4u + localX);
                        const u16 y = static_cast<u16>(tileY * 4u + localY);
                        if (x >= texture.width || y >= texture.height) {
                            continue;
                        }
                        const size_t pixelOffset = tileOffset + (localY * 4u + localX) * 2u;
                        const u8 alpha = src[pixelOffset];
                        const u8 intensity = src[pixelOffset + 1u];
                        StoreTexturePixel(rgba, texture.width, x, y,
                                          intensity, intensity, intensity, alpha);
                    }
                }
            }
        }
        return true;
    }

    if (texture.format == GX_TF_RGB565 || texture.format == GX_TF_RGB5A3) {
        const u32 tilesX = (static_cast<u32>(texture.width) + 3u) / 4u;
        const u32 tilesY = (static_cast<u32>(texture.height) + 3u) / 4u;
        for (u32 tileY = 0; tileY < tilesY; ++tileY) {
            for (u32 tileX = 0; tileX < tilesX; ++tileX) {
                const size_t tileOffset = (static_cast<size_t>(tileY) * tilesX + tileX) * 32u;
                for (u32 localY = 0; localY < 4; ++localY) {
                    for (u32 localX = 0; localX < 4; ++localX) {
                        const u16 x = static_cast<u16>(tileX * 4u + localX);
                        const u16 y = static_cast<u16>(tileY * 4u + localY);
                        if (x >= texture.width || y >= texture.height) {
                            continue;
                        }
                        const size_t pixelOffset = tileOffset + (localY * 4u + localX) * 2u;
                        const u16 packed = ReadU16(src + pixelOffset, false);
                        u8 r = 0, g = 0, b = 0, a = 255;
                        if (texture.format == GX_TF_RGB565) {
                            r = static_cast<u8>(((packed >> 11) & 0x1fu) * 255u / 31u);
                            g = static_cast<u8>(((packed >> 5) & 0x3fu) * 255u / 63u);
                            b = static_cast<u8>((packed & 0x1fu) * 255u / 31u);
                        } else if ((packed & 0x8000u) != 0) {
                            r = static_cast<u8>(((packed >> 10) & 0x1fu) * 255u / 31u);
                            g = static_cast<u8>(((packed >> 5) & 0x1fu) * 255u / 31u);
                            b = static_cast<u8>((packed & 0x1fu) * 255u / 31u);
                        } else {
                            a = static_cast<u8>(((packed >> 12) & 0x7u) * 255u / 7u);
                            r = static_cast<u8>(((packed >> 8) & 0x0fu) * 17u);
                            g = static_cast<u8>(((packed >> 4) & 0x0fu) * 17u);
                            b = static_cast<u8>((packed & 0x0fu) * 17u);
                        }
                        StoreTexturePixel(rgba, texture.width, x, y, r, g, b, a);
                    }
                }
            }
        }
        return true;
    }

    if (texture.format == GX_TF_RGBA8) {
        const u32 tilesX = (static_cast<u32>(texture.width) + 3u) / 4u;
        const u32 tilesY = (static_cast<u32>(texture.height) + 3u) / 4u;
        for (u32 tileY = 0; tileY < tilesY; ++tileY) {
            for (u32 tileX = 0; tileX < tilesX; ++tileX) {
                const size_t tileOffset = (static_cast<size_t>(tileY) * tilesX + tileX) * 64u;
                const u8* ar = src + tileOffset;
                const u8* gb = ar + 32u;
                for (u32 localY = 0; localY < 4; ++localY) {
                    for (u32 localX = 0; localX < 4; ++localX) {
                        const u16 x = static_cast<u16>(tileX * 4u + localX);
                        const u16 y = static_cast<u16>(tileY * 4u + localY);
                        if (x >= texture.width || y >= texture.height) {
                            continue;
                        }
                        const size_t pixel = localY * 4u + localX;
                        StoreTexturePixel(rgba, texture.width, x, y,
                                          ar[pixel * 2u + 1u], gb[pixel * 2u],
                                          gb[pixel * 2u + 1u], ar[pixel * 2u]);
                    }
                }
            }
        }
        return true;
    }

    if (texture.format == GX_TF_CMPR) {
        const u8* block = src;
        for (u32 tileY = 0; tileY < texture.height; tileY += 8u) {
            for (u32 tileX = 0; tileX < texture.width; tileX += 8u) {
                for (u32 subY = 0; subY < 8u; subY += 4u) {
                    for (u32 subX = 0; subX < 8u; subX += 4u) {
                        const u16 color0 = ReadU16(block, false);
                        const u16 color1 = ReadU16(block + 2u, false);
                        std::array<std::array<u8, 4>, 4> colors{};
                        const auto decode565 = [](u16 packed, std::array<u8, 4>& out) {
                            out[0] = static_cast<u8>(((packed >> 11) & 0x1fu) * 255u / 31u);
                            out[1] = static_cast<u8>(((packed >> 5) & 0x3fu) * 255u / 63u);
                            out[2] = static_cast<u8>((packed & 0x1fu) * 255u / 31u);
                            out[3] = 255;
                        };
                        decode565(color0, colors[0]);
                        decode565(color1, colors[1]);
                        if (color0 > color1) {
                            for (size_t c = 0; c < 3; ++c) {
                                colors[2][c] = static_cast<u8>((3u * colors[1][c] + 5u * colors[0][c]) >> 3u);
                                colors[3][c] = static_cast<u8>((3u * colors[0][c] + 5u * colors[1][c]) >> 3u);
                            }
                            colors[2][3] = 255;
                            colors[3][3] = 255;
                        } else {
                            for (size_t c = 0; c < 3; ++c) {
                                colors[2][c] = static_cast<u8>((static_cast<u16>(colors[0][c]) + colors[1][c]) >> 1u);
                                colors[3][c] = colors[2][c];
                            }
                            colors[2][3] = 255;
                            colors[3][3] = 0;
                        }

                        for (u32 localY = 0; localY < 4u; ++localY) {
                            u8 selectors = block[4u + localY];
                            for (u32 localX = 0; localX < 4u; ++localX) {
                                const u32 x = tileX + subX + localX;
                                const u32 y = tileY + subY + localY;
                                const auto& color = colors[(selectors >> 6u) & 0x3u];
                                selectors <<= 2u;
                                if (x < texture.width && y < texture.height) {
                                    StoreTexturePixel(rgba, texture.width,
                                                      static_cast<u16>(x), static_cast<u16>(y),
                                                      color[0], color[1], color[2], color[3]);
                                }
                            }
                        }
                        block += 8u;
                    }
                }
            }
        }
        return true;
    }
    return false;
}

GLint TextureWrapMode(u8 mode) {
    switch (static_cast<GXTexWrapMode>(mode)) {
    case GX_REPEAT: return GL_REPEAT;
    case GX_MIRROR: return GL_MIRRORED_REPEAT;
    case GX_CLAMP:
    default: return GL_CLAMP_TO_EDGE;
    }
}

GLint TextureBaseFilter(u8 filter) {
    switch (static_cast<GXTexFilter>(filter)) {
    case GX_LINEAR:
    case GX_LIN_MIP_NEAR:
    case GX_LIN_MIP_LIN:
        return GL_LINEAR;
    default:
        return GL_NEAREST;
    }
}

bool TextureCacheMatches(const TextureCacheEntry& entry, const DrawTextureState& texture) {
    return entry.valid && entry.data == reinterpret_cast<uintptr_t>(texture.data) &&
           entry.dataRevision == texture.dataRevision && entry.globalEpoch == texture.globalEpoch &&
           entry.sourceGeneration == texture.sourceGeneration &&
           entry.format == texture.format && entry.width == texture.width && entry.height == texture.height;
}

bool TextureSourceStillMatches(const DrawTextureState& texture) {
    if (!g_guestWriteGeneration || texture.sourceGeneration == AURORA_GUEST_WRITE_UNTRACKED) {
        return true;
    }
    const uint32_t sourceBytes = TextureLevelSize(texture.width, texture.height, texture.format);
    return sourceBytes != 0 &&
           g_guestWriteGeneration(texture.data, sourceBytes) == texture.sourceGeneration;
}

void EvictTextureEntry(TextureCacheEntry& entry) {
    if (!entry.valid) {
        return;
    }
    if (entry.glTexture != 0) {
        glDeleteTextures(1, &entry.glTexture);
    }
    g_textureCacheBytes = entry.gpuBytes <= g_textureCacheBytes
        ? g_textureCacheBytes - entry.gpuBytes : 0;
    entry = {};
}

TextureCacheEntry* OldestTextureEntry() {
    TextureCacheEntry* oldest = nullptr;
    for (auto& entry : g_textureCache) {
        if (!entry.valid) {
            return &entry;
        }
        if (!oldest || entry.lastUse < oldest->lastUse) {
            oldest = &entry;
        }
    }
    return oldest;
}

GLuint ResolveTexture(const DrawTextureState& texture, TextureRenderCounters& counters) {
    if (!texture.enabled || !texture.data || texture.width == 0 || texture.height == 0) {
        return 0;
    }
    if (!SupportedTextureFormat(texture.format) || texture.width > kMaxTextureDimension ||
        texture.height > kMaxTextureDimension) {
        ++counters.unsupportedDraws;
        return 0;
    }
    if (!TextureSourceStillMatches(texture)) {
        ++counters.sourceRaceDraws;
        return 0;
    }

    ++g_textureUseSerial;
    for (auto& entry : g_textureCache) {
        if (TextureCacheMatches(entry, texture)) {
            entry.lastUse = g_textureUseSerial;
            ++counters.cacheHits;
            return entry.glTexture;
        }
    }

    ++counters.cacheMisses;
    if (!g_textureScratch) {
        ++counters.uploadFailures;
        return 0;
    }
    const size_t rgbaBytes = static_cast<size_t>(texture.width) * texture.height * 4u;
    if (!ConvertTextureLevel0(texture, g_textureScratch.get(), kTextureScratchBytes)) {
        ++counters.uploadFailures;
        return 0;
    }
    if (!TextureSourceStillMatches(texture)) {
        ++counters.sourceRaceDraws;
        return 0;
    }

    while (g_textureCacheBytes + rgbaBytes > kTextureCacheBudgetBytes) {
        TextureCacheEntry* victim = OldestTextureEntry();
        if (!victim || !victim->valid) {
            break;
        }
        EvictTextureEntry(*victim);
    }
    TextureCacheEntry* entry = OldestTextureEntry();
    if (!entry) {
        ++counters.uploadFailures;
        return 0;
    }
    if (entry->valid) {
        EvictTextureEntry(*entry);
    }

    GLuint glTexture = 0;
    glGenTextures(1, &glTexture);
    if (glTexture == 0) {
        ++counters.uploadFailures;
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, glTexture);
    while (glGetError() != GL_NO_ERROR) {
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, g_textureScratch.get());
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &glTexture);
        ++counters.uploadFailures;
        return 0;
    }

    *entry = {};
    entry->valid = true;
    entry->data = reinterpret_cast<uintptr_t>(texture.data);
    entry->dataRevision = texture.dataRevision;
    entry->globalEpoch = texture.globalEpoch;
    entry->sourceGeneration = texture.sourceGeneration;
    entry->format = texture.format;
    entry->width = texture.width;
    entry->height = texture.height;
    entry->glTexture = glTexture;
    entry->gpuBytes = rgbaBytes;
    entry->lastUse = g_textureUseSerial;
    g_textureCacheBytes += rgbaBytes;
    ++counters.uploads;
    counters.bytesUploaded += rgbaBytes;
    switch (texture.format) {
    case GX_TF_I4: ++counters.i4Uploads; break;
    case GX_TF_I8: ++counters.i8Uploads; break;
    case GX_TF_IA4: ++counters.ia4Uploads; break;
    case GX_TF_IA8: ++counters.ia8Uploads; break;
    case GX_TF_RGB565: ++counters.rgb565Uploads; break;
    case GX_TF_RGB5A3: ++counters.rgb5a3Uploads; break;
    case GX_TF_RGBA8: ++counters.rgba8Uploads; break;
    case GX_TF_CMPR: ++counters.cmprUploads; break;
    case GX_TF_RGBA8_PC: ++counters.rgba8PcUploads; break;
    default: break;
    }
    return glTexture;
}

void ApplyTextureSampler(const DrawTextureState& texture) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, TextureWrapMode(texture.wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, TextureWrapMode(texture.wrapT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, TextureBaseFilter(texture.minFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, TextureBaseFilter(texture.magFilter));
}

void DestroyTextureCache() {
    for (auto& entry : g_textureCache) {
        EvictTextureEntry(entry);
    }
    g_textureCacheBytes = 0;
    g_textureUseSerial = 0;
    g_textureScratch.reset();
}

void RenderWorkerMain() {
    // vitaGL owns a GXM context and must be initialized, used and presented
    // from the same USER_1 render lane. ram_threshold is memory left outside
    // vitaGL, not its pool size. Keep 8 MiB for host-side runtime allocations;
    // a 32 MiB threshold would yield a zero-sized RAM pool after guest MEM1.
    // vitaGL's GLboolean result reports whether it had to fall back from the
    // requested resolution; it is not an initialization success flag. Once
    // the call returns, the GXM context and vitaGL pools are initialized.
    // The M12.6 core enters SceGxm from vitaGL's FBO scene/scissor reset path.
    // Eight is vitaGL's supported maximum and gives diagnostic headroom even
    // though M12.7 defaults to the synchronous EFB readback path below.
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
    vglSetupRenderTargetScenesNum(kRenderTargetScenes, kRenderTargetScenes);
#endif
    const uint64_t vglInitBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=vglInitExtended phase=begin t_us=%llu renderer=%s vitagl=%s rt_scenes=%u/%u "
            "frame_draw_cap=%u frame_vertex_cap=%u efb_cap=%u packet_bytes=%u efb_gpu_blit=%u "
            "movies_disabled=%u native_thp=%u lyt_direct=%u lyt_faithful=%u dl_indexed_raw=%u "
            "perf_skip_efb=%u perf_skip_billboards=%u perf_skip_lighttexture=%u "
            "perf_force_3d_solid=%u\n",
            static_cast<unsigned long long>(vglInitBeginUs), kRendererVariant, kVitaGlVariant,
            static_cast<unsigned>(kRenderTargetScenes), static_cast<unsigned>(kRenderTargetScenes),
            static_cast<unsigned>(kMaxFrameDraws), static_cast<unsigned>(kMaxFrameVertices),
            static_cast<unsigned>(kMaxFrameEfbCommands), static_cast<unsigned>(sizeof(FramePacket)),
            static_cast<unsigned>(MKW_VITA_EFB_GPU_BLIT),
            static_cast<unsigned>(MKW_VITA_DISABLE_MOVIES),
            static_cast<unsigned>(MKW_VITA_NATIVE_THP),
            static_cast<unsigned>(MKW_VITA_LYT_DIRECT),
            static_cast<unsigned>(MKW_VITA_LYT_FAITHFUL),
            static_cast<unsigned>(MKW_VITA_DL_INDEXED_RAW),
            static_cast<unsigned>(MKW_VITA_PERF_SKIP_EFB),
            static_cast<unsigned>(MKW_VITA_PERF_SKIP_BILLBOARDS),
            static_cast<unsigned>(MKW_VITA_PERF_SKIP_LIGHTTEXTURE),
            static_cast<unsigned>(MKW_VITA_PERF_FORCE_3D_SOLID));
    const bool resolutionFallback =
        vglInitExtended(0, kSurfaceWidth, kSurfaceHeight, kVitaGlUserRamReserve,
                        SCE_GXM_MULTISAMPLE_NONE) == GL_TRUE;
    const uint64_t vglInitEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=vglInitExtended phase=end t_us=%llu elapsed_us=%llu fallback=%u renderer=%s vitagl=%s\n",
            static_cast<unsigned long long>(vglInitEndUs),
            static_cast<unsigned long long>(vglInitEndUs - vglInitBeginUs),
            static_cast<unsigned>(resolutionFallback), kRendererVariant, kVitaGlVariant);
    RT_LOGF(RT_TAG_GX,
            "vitaGL pools total_ram=%u total_cdram=%u total_phycont=%u bytes renderer=%s vitagl=%s gc=single allocator=custom\n",
            static_cast<unsigned>(vglMemTotal(VGL_MEM_RAM)),
            static_cast<unsigned>(vglMemTotal(VGL_MEM_VRAM)),
            static_cast<unsigned>(vglMemTotal(VGL_MEM_SLOW)),
            kRendererVariant, kVitaGlVariant);
#if defined(MKW_VITA_AURORA_RENDERER)
    const uint64_t auroraInitBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=AuroraPacketRendererInitialize phase=begin t_us=%llu vitagl=%s\n",
            static_cast<unsigned long long>(auroraInitBeginUs), kVitaGlVariant);
    const bool gpuReady = WiiCompiledVita::AuroraPacketRendererInitialize();
    const uint64_t auroraInitEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=AuroraPacketRendererInitialize phase=end t_us=%llu elapsed_us=%llu ready=%u vitagl=%s\n",
            static_cast<unsigned long long>(auroraInitEndUs),
            static_cast<unsigned long long>(auroraInitEndUs - auroraInitBeginUs),
            static_cast<unsigned>(gpuReady), kVitaGlVariant);
    RT_LOGF(RT_TAG_GX,
            "renderer=aurora vitagl=%s packet_bridge=1 stream_upload=map_range "
            "cpu_fastpath=rebased_indices+pipeline_run_cache ready=%u\n",
            kVitaGlVariant, static_cast<unsigned>(gpuReady));
#else
    const uint64_t legacyScratchBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=legacy_texture_scratch phase=begin t_us=%llu vitagl=%s bytes=%u\n",
            static_cast<unsigned long long>(legacyScratchBeginUs), kVitaGlVariant,
            static_cast<unsigned>(kTextureScratchBytes));
    const bool gpuReady = true;
    g_textureScratch.reset(new (std::nothrow) u8[kTextureScratchBytes]);
    const uint64_t legacyScratchEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=legacy_texture_scratch phase=end t_us=%llu elapsed_us=%llu ok=%u vitagl=%s\n",
            static_cast<unsigned long long>(legacyScratchEndUs),
            static_cast<unsigned long long>(legacyScratchEndUs - legacyScratchBeginUs),
            static_cast<unsigned>(g_textureScratch != nullptr), kVitaGlVariant);
    RT_LOGF(RT_TAG_GX, "renderer=legacy_vitagl vitagl=%s packet_bridge=0 ready=1\n",
            kVitaGlVariant);
#endif
    if (!gpuReady) {
        const uint64_t initPublishBeginUs = sceKernelGetProcessTimeWide();
        RT_LOGF(RT_TAG_GX,
                "init_marker=g_renderInitDone phase=before_publish t_us=%llu ready=0\n",
                static_cast<unsigned long long>(initPublishBeginUs));
        {
            const SceUID threadId = sceKernelGetThreadId();
            std::lock_guard<std::mutex> lock(g_renderMutex);
            g_stats.renderAffinityMask = threadId >= 0 ? sceKernelGetThreadCpuAffinityMask(threadId) : 0;
            g_stats.renderStackFree = threadId >= 0 ? sceKernelGetThreadStackFreeSize(threadId) : 0;
            g_stats.gpuInitialized = false;
            g_stats.resolutionFallback = resolutionFallback;
            g_renderInitOk = false;
            g_renderInitDone = true;
        }
        g_renderIdle.notify_all();
        RT_LOGF(RT_TAG_GX,
                "init_marker=g_renderInitDone phase=notified t_us=%llu ready=0\n",
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide()));
        return;
    }

    // Complete the initial GPU clear/present before releasing USER_0. Hardware
    // timing shows this first clear can take about one second, so publishing the
    // ready flag earlier lets guest boot race unfinished renderer initialization.
    const uint64_t initialClearBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=initial_clear phase=begin t_us=%llu\n",
            static_cast<unsigned long long>(initialClearBeginUs));
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, kSurfaceWidth, kSurfaceHeight);
    glClearColor(0.015f, 0.02f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const uint64_t initialClearEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=initial_clear phase=end t_us=%llu elapsed_us=%llu\n",
            static_cast<unsigned long long>(initialClearEndUs),
            static_cast<unsigned long long>(initialClearEndUs - initialClearBeginUs));
    const uint64_t initialSwapBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=initial_swap phase=begin t_us=%llu\n",
            static_cast<unsigned long long>(initialSwapBeginUs));
    vglSwapBuffers(GL_FALSE);
    const uint64_t initialSwapEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=initial_swap phase=end t_us=%llu elapsed_us=%llu\n",
            static_cast<unsigned long long>(initialSwapEndUs),
            static_cast<unsigned long long>(initialSwapEndUs - initialSwapBeginUs));

    bool renderReady = true;
#if !defined(MKW_VITA_AURORA_RENDERER) && defined(MKW_VITA_VITAGL_SPEEDHACK)
    const uint64_t legacyVboBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=legacy_vertex_vbo phase=begin t_us=%llu bytes=%u\n",
            static_cast<unsigned long long>(legacyVboBeginUs),
            static_cast<unsigned>(sizeof(g_renderVertices)));
    while (glGetError() != GL_NO_ERROR) {
    }
    glGenBuffers(1, &g_legacyVertexBuffer);
    if (g_legacyVertexBuffer != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, g_legacyVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(g_renderVertices), nullptr, GL_DYNAMIC_DRAW);
    }
    const GLenum legacyVboError = glGetError();
    renderReady = g_legacyVertexBuffer != 0 && legacyVboError == GL_NO_ERROR;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    const uint64_t legacyVboEndUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=legacy_vertex_vbo phase=end t_us=%llu elapsed_us=%llu id=%u gl_error=0x%X ready=%u\n",
            static_cast<unsigned long long>(legacyVboEndUs),
            static_cast<unsigned long long>(legacyVboEndUs - legacyVboBeginUs),
            static_cast<unsigned>(g_legacyVertexBuffer),
            static_cast<unsigned>(legacyVboError), static_cast<unsigned>(renderReady));
#endif

    const uint64_t initPublishBeginUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=g_renderInitDone phase=before_publish t_us=%llu ready=%u after_initial_present=1\n",
            static_cast<unsigned long long>(initPublishBeginUs), static_cast<unsigned>(renderReady));
    {
        const SceUID threadId = sceKernelGetThreadId();
        std::lock_guard<std::mutex> lock(g_renderMutex);
        g_stats.renderAffinityMask = threadId >= 0 ? sceKernelGetThreadCpuAffinityMask(threadId) : 0;
        g_stats.renderStackFree = threadId >= 0 ? sceKernelGetThreadStackFreeSize(threadId) : 0;
        g_stats.gpuInitialized = renderReady;
        g_stats.resolutionFallback = resolutionFallback;
        g_renderInitOk = renderReady;
        g_renderInitDone = true;
    }
    const uint64_t initPublishedUs = sceKernelGetProcessTimeWide();
    RT_LOGF(RT_TAG_GX,
            "init_marker=g_renderInitDone phase=published t_us=%llu elapsed_us=%llu after_initial_present=1\n",
            static_cast<unsigned long long>(initPublishedUs),
            static_cast<unsigned long long>(initPublishedUs - initPublishBeginUs));
    g_renderIdle.notify_all();
    RT_LOGF(RT_TAG_GX,
            "init_marker=g_renderInitDone phase=notified t_us=%llu after_initial_present=1\n",
            static_cast<unsigned long long>(sceKernelGetProcessTimeWide()));
    if (!renderReady) {
        return;
    }

    bool bootConsoleActive = true;
    uint32_t renderableTraceCount = 0;
    for (;;) {
        // SubmitFrame waits for !g_renderPending && !g_renderBusy before reusing this
        // storage, so the worker can consume g_pendingFrame directly. This avoids a
        // redundant ~MiB-scale packet copy and one whole duplicate geometry buffer.
        FramePacket& packet = g_pendingFrame;
        uint64_t serial = 0;
        {
            std::unique_lock<std::mutex> lock(g_renderMutex);
            if (!g_renderWake.wait_for(lock, std::chrono::milliseconds(100),
                                       [] { return g_renderStop || g_renderPending; })) {
                lock.unlock();
#if !defined(MKW_VITA_PORTING_PROBE)
                GuestStallWatchdog::Poll(sceKernelGetProcessTimeWide());
                if (bootConsoleActive) {
                    RenderBootConsole();
                }
#else
                (void)bootConsoleActive;
#endif
                continue;
            }
            if (g_renderStop) {
                break;
            }
            serial = g_submittedSerial;
            g_renderPending = false;
            g_renderBusy = true;
        }

        const bool hasRenderableGeometry =
            packet.geometry.drawCount != 0 && packet.geometry.vertexCount != 0;
        if (!hasRenderableGeometry) {
#if defined(MKW_TARGET_VITA)
            if (serial <= 8u || (serial % 120u) == 0u) {
                RT_LOGF(
                    RT_TAG_GX,
                    "frame=%llu skipped_empty=1 packet_draws=%u packet_vertices=%u calls=%llu "
                    "raw_ok=%llu raw_fail=%llu raw_cap=%llu begin_cap=%llu dropped=%u "
                    "tex_state=%llu/%llu/%llu/%llu/%llu/%llu recovered=%llu texcoord_gt0=%llu custom=%llu "
                    "custom_mode=%llu/%llu/%llu/%llu/%llu/%llu\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned>(packet.geometry.drawCount),
                    static_cast<unsigned>(packet.geometry.vertexCount),
                    static_cast<unsigned long long>(packet.counters.drawCalls),
                    static_cast<unsigned long long>(packet.counters.rawDrawsDecoded),
                    static_cast<unsigned long long>(packet.counters.rawDrawDecodeFailures),
                    static_cast<unsigned long long>(packet.counters.rawDrawCapacityFailures),
                    static_cast<unsigned long long>(packet.counters.immediateDrawCapacityFailures),
                    packet.geometry.droppedVertices,
                    static_cast<unsigned long long>(packet.counters.textureStateNoTexGen),
                    static_cast<unsigned long long>(packet.counters.textureStateNoTexAttr),
                    static_cast<unsigned long long>(packet.counters.textureStateNoTevStage),
                    static_cast<unsigned long long>(packet.counters.textureStateBadOrder),
                    static_cast<unsigned long long>(packet.counters.textureStateUnbound),
                    static_cast<unsigned long long>(packet.counters.textureStateInvalidObject),
                    static_cast<unsigned long long>(packet.counters.textureStateRecoveredLaterStage),
                    static_cast<unsigned long long>(packet.counters.textureStateUnsupportedTexCoord),
                    static_cast<unsigned long long>(packet.counters.textureStateRecoveredCustomStage),
                    static_cast<unsigned long long>(packet.counters.textureStateCustomPresetModulate),
                    static_cast<unsigned long long>(packet.counters.textureStateCustomPresetDecal),
                    static_cast<unsigned long long>(packet.counters.textureStateCustomPresetBlend),
                    static_cast<unsigned long long>(packet.counters.textureStateCustomPresetReplace),
                    static_cast<unsigned long long>(packet.counters.textureStateCustomPresetPassClr),
                    static_cast<unsigned long long>(packet.counters.textureStateCustomPresetUnknown));
            }
#endif
            {
                std::lock_guard<std::mutex> lock(g_renderMutex);
                g_renderBusy = false;
                g_completedSerial = serial;
                g_stats.framesCompleted = g_completedSerial;
                g_stats.geometryVerticesDropped += packet.geometry.droppedVertices;
            }
            g_renderIdle.notify_all();
            continue;
        }

        const bool traceLargeFrame = packet.geometry.drawCount >= 1000u;
        if (traceLargeFrame) {
            RT_LOGF(RT_TAG_GX,
                    "render_large phase=begin serial=%llu draws=%u vertices=%u efb=%u\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned>(packet.geometry.drawCount),
                    static_cast<unsigned>(packet.geometry.vertexCount),
                    static_cast<unsigned>(packet.geometry.efbCommandCount));
        }

        if (bootConsoleActive) {
            bootConsoleActive = false;
            RT_LOGF(RT_TAG_GX, "boot console disabled on first renderable frame\n");
        }
        const uint32_t renderableOrdinal = ++renderableTraceCount;
        const bool traceGpu = renderableOrdinal <= 16u;
        const uint64_t frameRenderBeginUs = sceKernelGetProcessTimeWide();
#if defined(MKW_VITA_AURORA_RENDERER)
        uint64_t auroraBeginEndUs = frameRenderBeginUs;
        uint64_t auroraSubmitEndUs = frameRenderBeginUs;
        uint64_t auroraEndFrameEndUs = frameRenderBeginUs;
#endif
        if (traceGpu) {
            RT_LOGF(RT_TAG_GX,
                    "gpu_trace frame=%llu ordinal=%u phase=frame_begin draws=%u vertices=%u renderer=%s vitagl=%s\n",
                    static_cast<unsigned long long>(serial), renderableOrdinal,
                    static_cast<unsigned>(packet.geometry.drawCount),
                    static_cast<unsigned>(packet.geometry.vertexCount),
                    kRendererVariant, kVitaGlVariant);
        }

        // Viewport/scissor are captured in the guest packet so USER_1 never
        // races the live GX state. Aurora consumes top-left GX coordinates and
        // performs the OpenGL Y flip in Renderer::draw.
        const float scaleX = static_cast<float>(kSurfaceWidth) / 640.0f;
        const float scaleY = static_cast<float>(kSurfaceHeight) / 480.0f;
        const GLint viewportX = static_cast<GLint>(packet.viewport[0] * scaleX);
        const GLsizei viewportW = std::max<GLsizei>(1, static_cast<GLsizei>(packet.viewport[2] * scaleX));
        const GLsizei viewportH = std::max<GLsizei>(1, static_cast<GLsizei>(packet.viewport[3] * scaleY));
        const GLint viewportY = static_cast<GLint>(kSurfaceHeight -
            (packet.viewport[1] + packet.viewport[3]) * scaleY);
        const GLint scissorX = static_cast<GLint>(packet.scissor[0] * scaleX);
        const GLsizei scissorW = std::max<GLsizei>(1, static_cast<GLsizei>(packet.scissor[2] * scaleX));
        const GLsizei scissorH = std::max<GLsizei>(1, static_cast<GLsizei>(packet.scissor[3] * scaleY));
        const GLint scissorY = static_cast<GLint>(kSurfaceHeight -
            (packet.scissor[1] + packet.scissor[3]) * scaleY);

#if defined(MKW_VITA_AURORA_RENDERER)
        const bool auroraFrameReady = WiiCompiledVita::AuroraPacketRendererBeginFrame(
            serial,
            packet.viewport[0] * scaleX, packet.viewport[1] * scaleY,
            static_cast<float>(viewportW), static_cast<float>(viewportH),
            scissorX, static_cast<GLint>(packet.scissor[1] * scaleY), scissorW, scissorH);
        auroraBeginEndUs = sceKernelGetProcessTimeWide();
#else
        // Clear/present is the first real GPU milestone for the legacy A/B arm.
        if (traceGpu) {
            RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu phase=legacy_clear_begin\n",
                    static_cast<unsigned long long>(serial));
        }
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.015f, 0.02f, 0.035f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(viewportX, viewportY, viewportW, viewportH);
        glScissor(scissorX, scissorY, scissorW, scissorH);
        glEnable(GL_SCISSOR_TEST);
        if (traceGpu) {
            RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu phase=legacy_clear_end\n",
                    static_cast<unsigned long long>(serial));
        }
#endif

        uint64_t geometryDrawsPresented = 0;
        uint64_t geometryVerticesPresented = 0;
        uint64_t geometryVerticesTransformed = 0;
        uint64_t geometryPnMatrixVertices = 0;
        uint64_t geometryTransformFailures = 0;
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
        uint64_t geometryTexGenIdentityDraws = 0;
        uint64_t geometryTexGenAppliedDraws = 0;
        uint64_t geometryTexGenUnsupportedDraws = 0;
        uint64_t geometryOrthoDraws = 0;
        uint64_t geometryPerspectiveDraws = 0;
        uint64_t geometryOversizeNdcDraws = 0;
        f32 geometryMaxNdcSpanX = 0.0f;
        f32 geometryMaxNdcSpanY = 0.0f;
        // M12: Aurora submit rejection breakdown. Index = AuroraPacketSubmitResult.prepareError
        // (0 None .. 7 PipelineFailed, 255 -> slot 8 "never enqueued").
        std::array<uint32_t, 9> submitFailByReason{};
        TextureRenderCounters textureCounters{};
        uint64_t efbCopiesExecuted = 0;
        uint64_t efbCopyFailures = 0;
        uint64_t efbTexturesSampled = 0;
#if defined(MKW_VITA_AURORA_RENDERER)
        WiiCompiledVita::AuroraPacketFrameStats auroraFrameStats{};
        size_t nextEfbCommand = 0;
        const auto ExecuteEfbCommandsAt = [&](u16 drawBoundary) {
            while (nextEfbCommand < packet.geometry.efbCommandCount) {
                const EfbFrameCommand& command = packet.geometry.efbCommands[nextEfbCommand];
                if (command.afterDrawCount > drawBoundary) break;
                ++nextEfbCommand;
                if (command.type == EfbFrameCommandType::Destroy) {
                    WiiCompiledVita::AuroraPacketRendererDestroyEfbCopy(
                        static_cast<std::uint64_t>(command.destination));
                    continue;
                }
                WiiCompiledVita::AuroraPacketEfbCopy copy{};
                copy.destinationId = static_cast<std::uint64_t>(command.destination);
                copy.srcX = static_cast<std::int32_t>(std::lround(command.srcLeft * scaleX));
                copy.srcY = static_cast<std::int32_t>(std::lround(command.srcTop * scaleY));
                copy.srcWidth = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(command.srcWidth * scaleX)));
                copy.srcHeight = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(command.srcHeight * scaleY)));
                copy.dstWidth = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(command.dstWidth));
                copy.dstHeight = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(command.dstHeight));
                copy.format = command.format;
                copy.clearR = static_cast<float>(command.clearColor.r) / 255.0f;
                copy.clearG = static_cast<float>(command.clearColor.g) / 255.0f;
                copy.clearB = static_cast<float>(command.clearColor.b) / 255.0f;
                copy.clearA = static_cast<float>(command.clearColor.a) / 255.0f;
                copy.clearDepthValue = static_cast<float>(command.clearDepth & 0x00ffffffu) / 16777215.0f;
                copy.clear = command.clear != 0;
                copy.clearColor = command.clearColorEnable != 0;
                copy.clearAlpha = command.clearAlphaEnable != 0;
                copy.clearDepth = command.clearDepthEnable != 0;
                if (WiiCompiledVita::AuroraPacketRendererCopyEfb(copy)) {
                    ++efbCopiesExecuted;
                } else {
                    ++efbCopyFailures;
                }
            }
        };
#endif
        if (packet.geometry.vertexCount != 0) {
#if !defined(MKW_VITA_AURORA_RENDERER)
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glFrontFace(GL_CW);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
            glBindBuffer(GL_ARRAY_BUFFER, g_legacyVertexBuffer);
            glVertexPointer(3, GL_FLOAT, sizeof(RenderVertex),
                            reinterpret_cast<const GLvoid*>(offsetof(RenderVertex, x)));
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(RenderVertex),
                           reinterpret_cast<const GLvoid*>(offsetof(RenderVertex, r)));
            glTexCoordPointer(2, GL_FLOAT, sizeof(RenderVertex),
                              reinterpret_cast<const GLvoid*>(offsetof(RenderVertex, s)));
#else
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glVertexPointer(3, GL_FLOAT, sizeof(RenderVertex), &g_renderVertices[0].x);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(RenderVertex), &g_renderVertices[0].r);
            glTexCoordPointer(2, GL_FLOAT, sizeof(RenderVertex), &g_renderVertices[0].s);
#endif
            if (traceGpu) {
                RT_LOGF(RT_TAG_GX,
                        "gpu_trace frame=%llu phase=client_arrays_ready vbo=%u stride=%u\n",
                        static_cast<unsigned long long>(serial),
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
                        static_cast<unsigned>(g_legacyVertexBuffer),
#else
                        0u,
#endif
                        static_cast<unsigned>(sizeof(RenderVertex)));
            }
#endif

            for (u16 i = 0; i < packet.geometry.drawCount; ++i) {
#if defined(MKW_VITA_AURORA_RENDERER)
                if (traceLargeFrame && (i % 128u) == 0u) {
                    RT_LOGF(RT_TAG_GX,
                            "render_large phase=draw_progress serial=%llu draw=%u/%u efb_next=%u elapsed_us=%llu\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i),
                            static_cast<unsigned>(packet.geometry.drawCount),
                            static_cast<unsigned>(nextEfbCommand),
                            static_cast<unsigned long long>(sceKernelGetProcessTimeWide() - frameRenderBeginUs));
                }
                ExecuteEfbCommandsAt(i);
#endif
                const GeometryDraw& draw = packet.geometry.draws[i];
                const uint32_t endVertex = static_cast<uint32_t>(draw.firstVertex) + draw.vertexCount;
                if (draw.vertexCount == 0 || endVertex > packet.geometry.vertexCount) {
                    continue;
                }
                if (traceGpu) {
                    RT_LOGF(RT_TAG_GX,
                            "gpu_trace frame=%llu draw=%u phase=draw_begin first=%u count=%u primitive=0x%X textured=%u fmt=0x%X size=%ux%u\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i),
                            static_cast<unsigned>(draw.firstVertex), static_cast<unsigned>(draw.vertexCount),
                            static_cast<unsigned>(draw.primitive), static_cast<unsigned>(draw.texture.enabled),
                            static_cast<unsigned>(draw.texture.format), static_cast<unsigned>(draw.texture.width),
                            static_cast<unsigned>(draw.texture.height));
                }

                const bool trianglePrimitive = IsTrianglePrimitive(draw.primitive);
                if (trianglePrimitive && draw.raster.cullMode == GX_CULL_ALL) {
                    ++geometryCullAllSkipped;
                    continue;
                }

                if (draw.transform.projectionType == GX_ORTHOGRAPHIC) {
                    ++geometryOrthoDraws;
                } else {
                    ++geometryPerspectiveDraws;
                }

#if !defined(MKW_VITA_AURORA_RENDERER)
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(draw.raster.depthCompare ? DepthCompareMode(draw.raster.depthFunc) : GL_ALWAYS);
                glDepthMask(draw.raster.depthUpdate ? GL_TRUE : GL_FALSE);
#endif
                if (draw.raster.depthCompare) {
                    ++geometryDepthCompareDraws;
                }
                if (draw.raster.depthUpdate) {
                    ++geometryDepthWriteDraws;
                }

                if (!trianglePrimitive || draw.raster.cullMode == GX_CULL_NONE) {
#if !defined(MKW_VITA_AURORA_RENDERER)
                    glDisable(GL_CULL_FACE);
#endif
                    ++geometryCullNoneDraws;
                } else {
#if !defined(MKW_VITA_AURORA_RENDERER)
                    glEnable(GL_CULL_FACE);
#endif
                    if (draw.raster.cullMode == GX_CULL_FRONT) {
#if !defined(MKW_VITA_AURORA_RENDERER)
                        glCullFace(GL_FRONT);
#endif
                        ++geometryCullFrontDraws;
                    } else {
#if !defined(MKW_VITA_AURORA_RENDERER)
                        glCullFace(GL_BACK);
#endif
                        ++geometryCullBackDraws;
                    }
                }

#if !defined(MKW_VITA_AURORA_RENDERER)
                glColorMask(draw.raster.colorUpdate ? GL_TRUE : GL_FALSE,
                            draw.raster.colorUpdate ? GL_TRUE : GL_FALSE,
                            draw.raster.colorUpdate ? GL_TRUE : GL_FALSE,
                            draw.raster.alphaUpdate ? GL_TRUE : GL_FALSE);
#endif
                if (draw.raster.blendMode == GX_BM_BLEND) {
#if !defined(MKW_VITA_AURORA_RENDERER)
                    glEnable(GL_BLEND);
                    glBlendFunc(BlendFactorMode(draw.raster.blendSrc, false),
                                BlendFactorMode(draw.raster.blendDst, true));
#endif
                    ++geometryBlendDraws;
                } else {
#if !defined(MKW_VITA_AURORA_RENDERER)
                    glDisable(GL_BLEND);
#endif
                    if (draw.raster.blendMode != GX_BM_NONE) {
                        ++geometryBlendFallbackDraws;
                    }
                }

                const AlphaTestSelection alphaTest = SelectAlphaTest(draw.raster);
#if !defined(MKW_VITA_AURORA_RENDERER)
                if (!alphaTest.exact) {
                    glDisable(GL_ALPHA_TEST);
                    ++geometryAlphaCompareFallbackDraws;
                } else if (alphaTest.enabled) {
                    glEnable(GL_ALPHA_TEST);
                    glAlphaFunc(DepthCompareMode(alphaTest.compare),
                                static_cast<GLclampf>(alphaTest.reference) / 255.0f);
                    ++geometryAlphaTestDraws;
                } else {
                    glDisable(GL_ALPHA_TEST);
                }
#else
                if (draw.raster.alphaComp0 != GX_ALWAYS ||
                    draw.raster.alphaComp1 != GX_ALWAYS) {
                    ++geometryAlphaTestDraws;
                }
#endif

#if !defined(MKW_VITA_AURORA_RENDERER)
                if (draw.texture.enabled) {
                    if (traceGpu) {
                        RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu draw=%u phase=texture_begin\n",
                                static_cast<unsigned long long>(serial), static_cast<unsigned>(i));
                    }
                    const GLuint texture = ResolveTexture(draw.texture, textureCounters);
                    if (traceGpu) {
                        RT_LOGF(RT_TAG_GX,
                                "gpu_trace frame=%llu draw=%u phase=texture_end gl_texture=%u uploads=%llu failures=%llu\n",
                                static_cast<unsigned long long>(serial), static_cast<unsigned>(i),
                                static_cast<unsigned>(texture),
                                static_cast<unsigned long long>(textureCounters.uploads),
                                static_cast<unsigned long long>(textureCounters.uploadFailures));
                    }
                    if (texture != 0) {
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, texture);
                        ApplyTextureSampler(draw.texture);
                        switch (static_cast<GXTevMode>(draw.texture.tevMode)) {
                        case GX_MODULATE:
                            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                            break;
                        case GX_DECAL:
                            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
                            break;
                        case GX_BLEND: {
                            GLfloat white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                            glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, white);
                            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
                            break;
                        }
                        case GX_REPLACE:
                            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                            break;
                        case GX_PASSCLR:
                            glDisable(GL_TEXTURE_2D);
                            break;
                        default:
                            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                            break;
                        }
                        if (draw.texture.tevSimple) {
                            ++geometryTevSimpleDraws;
                        } else {
                            ++geometryTevFallbackDraws;
                        }
                        ++textureCounters.draws;
                        if (draw.texture.mipmap || draw.texture.minFilter > static_cast<u8>(GX_LINEAR)) {
                            ++textureCounters.mipFallbackDraws;
                        }
                    } else {
                        glBindTexture(GL_TEXTURE_2D, 0);
                        glDisable(GL_TEXTURE_2D);
                    }
                } else {
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glDisable(GL_TEXTURE_2D);
                }
#endif

                bool texGenFailed = false;
                f32 ndcMinX = std::numeric_limits<f32>::infinity();
                f32 ndcMinY = std::numeric_limits<f32>::infinity();
                f32 ndcMaxX = -std::numeric_limits<f32>::infinity();
                f32 ndcMaxY = -std::numeric_limits<f32>::infinity();
                for (uint32_t vertexIndex = draw.firstVertex; vertexIndex < endVertex; ++vertexIndex) {
                    const u8 pnMtxRef = packet.geometry.pnMtxRefs[vertexIndex];
                    if ((pnMtxRef & kPnMtxExplicitBit) != 0) {
                        ++geometryPnMatrixVertices;
                    }
                    if (TransformVertex(draw, pnMtxRef, packet.geometry.vertices[vertexIndex],
                                        g_renderVertices[vertexIndex])) {
                        ++geometryVerticesTransformed;
                        ndcMinX = std::min(ndcMinX, g_renderVertices[vertexIndex].x);
                        ndcMinY = std::min(ndcMinY, g_renderVertices[vertexIndex].y);
                        ndcMaxX = std::max(ndcMaxX, g_renderVertices[vertexIndex].x);
                        ndcMaxY = std::max(ndcMaxY, g_renderVertices[vertexIndex].y);
                    } else {
                        // Keep malformed transform state from escaping into vitaGL. This
                        // position clips against identity GL state and the failure remains
                        // observable through the hardware probe counters.
                        g_renderVertices[vertexIndex] = packet.geometry.vertices[vertexIndex];
                        g_renderVertices[vertexIndex].x = 2.0f;
                        g_renderVertices[vertexIndex].y = 2.0f;
                        g_renderVertices[vertexIndex].z = 2.0f;
                        ++geometryTransformFailures;
                    }
                    if (draw.texture.enabled &&
                        !TransformTexCoord(draw.texture, packet.geometry.vertices[vertexIndex],
                                           g_renderVertices[vertexIndex])) {
                        texGenFailed = true;
                    }
                }
                if (draw.texture.enabled) {
                    if (draw.texture.texGenMode == 0u) {
                        ++geometryTexGenIdentityDraws;
                    } else if (draw.texture.texGenMode == 1u && !texGenFailed) {
                        ++geometryTexGenAppliedDraws;
                    } else {
                        ++geometryTexGenUnsupportedDraws;
                    }
                }
                if (std::isfinite(ndcMinX) && std::isfinite(ndcMaxX) &&
                    std::isfinite(ndcMinY) && std::isfinite(ndcMaxY)) {
                    const f32 spanX = ndcMaxX - ndcMinX;
                    const f32 spanY = ndcMaxY - ndcMinY;
                    geometryMaxNdcSpanX = std::max(geometryMaxNdcSpanX, spanX);
                    geometryMaxNdcSpanY = std::max(geometryMaxNdcSpanY, spanY);
                    if (spanX > 2.05f || spanY > 2.05f) {
                        ++geometryOversizeNdcDraws;
                    }
                }
                if (traceGpu) {
                    RT_LOGF(RT_TAG_GX,
                            "gpu_trace frame=%llu draw=%u phase=transform_end count=%u failures=%llu\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i),
                            static_cast<unsigned>(draw.vertexCount),
                            static_cast<unsigned long long>(geometryTransformFailures));
                }
#if !defined(MKW_VITA_AURORA_RENDERER) && defined(MKW_VITA_VITAGL_SPEEDHACK)
                const size_t uploadOffset = static_cast<size_t>(draw.firstVertex) * sizeof(RenderVertex);
                const size_t uploadBytes = static_cast<size_t>(draw.vertexCount) * sizeof(RenderVertex);
                if (traceGpu) {
                    RT_LOGF(RT_TAG_GX,
                            "gpu_trace frame=%llu draw=%u phase=vbo_upload_begin offset=%u bytes=%u\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i),
                            static_cast<unsigned>(uploadOffset), static_cast<unsigned>(uploadBytes));
                }
                glBindBuffer(GL_ARRAY_BUFFER, g_legacyVertexBuffer);
                void* mappedVertices = glMapBufferRange(
                    GL_ARRAY_BUFFER, static_cast<GLintptr>(uploadOffset),
                    static_cast<GLsizeiptr>(uploadBytes), GL_MAP_WRITE_BIT);
                bool vertexUploadReady = mappedVertices != nullptr;
                if (mappedVertices != nullptr) {
                    std::memcpy(mappedVertices, &g_renderVertices[draw.firstVertex], uploadBytes);
                    vertexUploadReady = glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE;
                }
                if (traceGpu || !vertexUploadReady) {
                    RT_LOGF(RT_TAG_GX,
                            "gpu_trace frame=%llu draw=%u phase=vbo_upload_end mapped=%u ready=%u\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i),
                            static_cast<unsigned>(mappedVertices != nullptr),
                            static_cast<unsigned>(vertexUploadReady));
                }
                if (!vertexUploadReady) {
                    continue;
                }
#endif
#if defined(MKW_VITA_AURORA_RENDERER)
                WiiCompiledVita::AuroraPacketDraw auroraDraw{};
                auroraDraw.vertices = reinterpret_cast<const WiiCompiledVita::AuroraPacketVertex*>(
                    &g_renderVertices[draw.firstVertex]);
                auroraDraw.vertexCount = draw.vertexCount;
                auroraDraw.primitive = static_cast<uint32_t>(draw.primitive);
                auroraDraw.depthFunc = static_cast<uint32_t>(draw.raster.depthFunc);
                auroraDraw.cullMode = static_cast<uint32_t>(draw.raster.cullMode);
                auroraDraw.blendMode = static_cast<uint32_t>(draw.raster.blendMode);
                auroraDraw.blendSrc = static_cast<uint32_t>(draw.raster.blendSrc);
                auroraDraw.blendDst = static_cast<uint32_t>(draw.raster.blendDst);
                auroraDraw.logicOp = static_cast<uint32_t>(draw.raster.logicOp);
                auroraDraw.alphaComp0 = static_cast<uint32_t>(draw.raster.alphaComp0);
                auroraDraw.alphaComp1 = static_cast<uint32_t>(draw.raster.alphaComp1);
                auroraDraw.alphaOp = static_cast<uint32_t>(draw.raster.alphaOp);
                auroraDraw.alphaRef0 = draw.raster.alphaRef0;
                auroraDraw.alphaRef1 = draw.raster.alphaRef1;
                auroraDraw.depthCompare = draw.raster.depthCompare != GX_FALSE;
                auroraDraw.depthUpdate = draw.raster.depthUpdate != GX_FALSE;
                auroraDraw.colorUpdate = draw.raster.colorUpdate != GX_FALSE;
                auroraDraw.alphaUpdate = draw.raster.alphaUpdate != GX_FALSE;
                auroraDraw.texture.data = draw.texture.data;
                auroraDraw.texture.dataBytes =
                    TextureLevelSize(draw.texture.width, draw.texture.height, draw.texture.format);
                auroraDraw.texture.sourceId = reinterpret_cast<uintptr_t>(draw.texture.data);
                auroraDraw.texture.sourceGeneration = draw.texture.sourceGeneration;
                auroraDraw.texture.revision = draw.texture.dataRevision;
                auroraDraw.texture.globalEpoch = draw.texture.globalEpoch;
                auroraDraw.texture.format = draw.texture.format;
                auroraDraw.texture.width = draw.texture.width;
                auroraDraw.texture.height = draw.texture.height;
                auroraDraw.texture.wrapS = draw.texture.wrapS;
                auroraDraw.texture.wrapT = draw.texture.wrapT;
                auroraDraw.texture.minFilter = draw.texture.minFilter;
                auroraDraw.texture.magFilter = draw.texture.magFilter;
                auroraDraw.texture.tevMode = draw.texture.tevMode;
                auroraDraw.texture.enabled = draw.texture.enabled != 0;
                auroraDraw.texture.thpUData = draw.texture.thpUData;
                auroraDraw.texture.thpVData = draw.texture.thpVData;
                auroraDraw.texture.thpUBytes = TextureLevelSize(
                    draw.texture.thpChromaWidth, draw.texture.thpChromaHeight, GX_TF_I8);
                auroraDraw.texture.thpVBytes = auroraDraw.texture.thpUBytes;
                auroraDraw.texture.thpUGeneration = draw.texture.thpUGeneration;
                auroraDraw.texture.thpVGeneration = draw.texture.thpVGeneration;
                auroraDraw.texture.thpURevision = draw.texture.thpURevision;
                auroraDraw.texture.thpVRevision = draw.texture.thpVRevision;
                auroraDraw.texture.thpChromaWidth = draw.texture.thpChromaWidth;
                auroraDraw.texture.thpChromaHeight = draw.texture.thpChromaHeight;
                auroraDraw.texture.thpYuv420 = draw.texture.thpYuv420 != 0;
#if defined(MKW_VITA_PERF_FORCE_3D_SOLID) && MKW_VITA_PERF_FORCE_3D_SOLID
                // Diagnostic visibility mode: perspective G3D geometry keeps its real
                // transform/depth but bypasses the currently incomplete multi-stage TEV,
                // textures, blending, alpha test and culling. White opaque silhouettes prove
                // whether missing models are a material problem rather than a geometry problem.
                if (draw.transform.projectionType == GX_PERSPECTIVE) {
                    // GX_PASSCLR consumes the raster/vertex color. Disabling the
                    // texture alone is not a white-material probe: G3D vertices
                    // can legitimately carry black or zero-alpha colors. This is
                    // a render-worker copy, so replacing its color does not alter
                    // guest GX state or the queued producer packet.
                    for (u16 vertex = 0; vertex < draw.vertexCount; ++vertex) {
                        RenderVertex& solid = g_renderVertices[draw.firstVertex + vertex];
                        solid.r = 0xFF;
                        solid.g = 0xFF;
                        solid.b = 0xFF;
                        solid.a = 0xFF;
                    }
                    auroraDraw.texture.enabled = false;
                    auroraDraw.texture.tevMode = static_cast<uint8_t>(GX_PASSCLR);
                    auroraDraw.cullMode = static_cast<uint32_t>(GX_CULL_NONE);
                    auroraDraw.blendMode = static_cast<uint32_t>(GX_BM_NONE);
                    auroraDraw.blendSrc = static_cast<uint32_t>(GX_BL_ONE);
                    auroraDraw.blendDst = static_cast<uint32_t>(GX_BL_ZERO);
                    auroraDraw.alphaComp0 = static_cast<uint32_t>(GX_ALWAYS);
                    auroraDraw.alphaComp1 = static_cast<uint32_t>(GX_ALWAYS);
                    auroraDraw.alphaOp = static_cast<uint32_t>(GX_AOP_AND);
                    auroraDraw.alphaRef0 = 0;
                    auroraDraw.alphaRef1 = 0;
                    auroraDraw.colorUpdate = true;
                    auroraDraw.alphaUpdate = true;
                    static uint64_t s_forceSolidDraws = 0;
                    const uint64_t forceSolidN = ++s_forceSolidDraws;
                    if (forceSolidN <= 16u || (forceSolidN & (forceSolidN - 1u)) == 0u) {
                        RT_LOGF(RT_TAG_GX,
                                "perf_probe force_3d_solid n=%llu serial=%llu idx=%u verts=%u tev_simple=%u tex=%u\n",
                                static_cast<unsigned long long>(forceSolidN),
                                static_cast<unsigned long long>(serial),
                                static_cast<unsigned>(i),
                                static_cast<unsigned>(draw.vertexCount),
                                static_cast<unsigned>(draw.texture.tevSimple),
                                static_cast<unsigned>(draw.texture.enabled));
                    }
                }
#endif
                if (auroraDraw.texture.enabled) {
                    if (auroraDraw.texture.sourceGeneration == AURORA_GUEST_WRITE_UNTRACKED) {
                        ++textureCounters.untrackedDraws;
                    } else {
                        ++textureCounters.trackedDraws;
                    }
                }
                if (auroraDraw.texture.enabled && !TextureSourceStillMatches(draw.texture)) {
                    auroraDraw.texture.enabled = false;
                    ++textureCounters.sourceRaceDraws;
                }

                const WiiCompiledVita::AuroraPacketSubmitResult auroraResult =
                    auroraFrameReady
                        ? WiiCompiledVita::AuroraPacketRendererSubmit(auroraDraw)
                        : WiiCompiledVita::AuroraPacketSubmitResult{};
                textureCounters.cacheHits += auroraResult.textureHit;
                textureCounters.cacheMisses += auroraResult.textureMiss;
                textureCounters.uploads += auroraResult.textureUploaded;
                textureCounters.uploadFailures += auroraResult.textureUploadFailed;
                textureCounters.unsupportedDraws += auroraResult.textureUnsupported;
                textureCounters.bytesUploaded += auroraResult.textureBytesUploaded;
                if (auroraResult.textureDrawn) {
                    ++textureCounters.draws;
                    if (auroraResult.textureEfb) ++efbTexturesSampled;
                    if (draw.texture.mipmap || draw.texture.minFilter > static_cast<u8>(GX_LINEAR)) {
                        ++textureCounters.mipFallbackDraws;
                    }
                    if (draw.texture.tevSimple) {
                        ++geometryTevSimpleDraws;
                    } else {
                        ++geometryTevFallbackDraws;
                    }
                }
                if (auroraResult.textureUploaded) {
                    switch (draw.texture.format) {
                    case GX_TF_I4: ++textureCounters.i4Uploads; break;
                    case GX_TF_I8: ++textureCounters.i8Uploads; break;
                    case GX_TF_IA4: ++textureCounters.ia4Uploads; break;
                    case GX_TF_IA8: ++textureCounters.ia8Uploads; break;
                    case GX_TF_RGB565: ++textureCounters.rgb565Uploads; break;
                    case GX_TF_RGB5A3: ++textureCounters.rgb5a3Uploads; break;
                    case GX_TF_RGBA8: ++textureCounters.rgba8Uploads; break;
                    case GX_TF_CMPR: ++textureCounters.cmprUploads; break;
                    case GX_TF_RGBA8_PC: ++textureCounters.rgba8PcUploads; break;
                    default: break;
                    }
                }
                if (auroraResult.submitted) {
                    ++geometryDrawsPresented;
                    geometryVerticesPresented += draw.vertexCount;
                } else {
                    const uint8_t reasonSlot =
                        auroraResult.prepareError == 255u
                            ? 8u
                            : static_cast<uint8_t>(std::min<uint32_t>(auroraResult.prepareError, 7u));
                    ++submitFailByReason[reasonSlot];
                }
#if defined(MKW_TARGET_VITA)
                {
                    const bool unusual =
                        !auroraResult.submitted ||
                        auroraResult.textureUnsupported ||
                        auroraResult.textureUploadFailed ||
                        (draw.texture.enabled && draw.texture.texGenMode == 2u) ||
                        texGenFailed ||
                        (std::isfinite(ndcMaxX) &&
                         (ndcMaxX - ndcMinX > 2.05f || ndcMaxY - ndcMinY > 2.05f));
                    static std::atomic<uint32_t> s_drawDetailCount{0};
                    if (unusual && s_drawDetailCount.fetch_add(1, std::memory_order_relaxed) < 16u) {
                        const f32* pm = draw.transform.projection.data();
                        RT_LOGF(RT_TAG_GX,
                                "m12_draw serial=%llu idx=%u lr=%08X prim=0x%X verts=%u proj=%s "
                                "pnmtx=%u vp=%.0f,%.0f,%.0f,%.0f sc=%u,%u,%u,%u "
                                "ndc_x=%.2f..%.2f ndc_y=%.2f..%.2f proj_diag=%.3f,%.3f,%.3f,%.3f "
                                "tex=%u fmt=0x%X %ux%u tevmode=%u tevsimple=%u texgen=%u "
                                "submitted=%u prepare_err=%u tex_unsupported=%u tex_upload_fail=%u\n",
                                static_cast<unsigned long long>(serial), static_cast<unsigned>(i),
                                draw.guestLr, static_cast<unsigned>(draw.primitive),
                                static_cast<unsigned>(draw.vertexCount),
                                draw.transform.projectionType == GX_ORTHOGRAPHIC ? "ortho" : "persp",
                                static_cast<unsigned>(draw.pnMtxIndex),
                                packet.viewport[0], packet.viewport[1], packet.viewport[2], packet.viewport[3],
                                packet.scissor[0], packet.scissor[1], packet.scissor[2], packet.scissor[3],
                                std::isfinite(ndcMinX) ? ndcMinX : 0.0f,
                                std::isfinite(ndcMaxX) ? ndcMaxX : 0.0f,
                                std::isfinite(ndcMinY) ? ndcMinY : 0.0f,
                                std::isfinite(ndcMaxY) ? ndcMaxY : 0.0f,
                                pm[0], pm[5], pm[10], pm[15],
                                static_cast<unsigned>(draw.texture.enabled),
                                static_cast<unsigned>(draw.texture.format),
                                static_cast<unsigned>(draw.texture.width),
                                static_cast<unsigned>(draw.texture.height),
                                static_cast<unsigned>(draw.texture.tevMode),
                                static_cast<unsigned>(draw.texture.tevSimple),
                                static_cast<unsigned>(draw.texture.texGenMode),
                                static_cast<unsigned>(auroraResult.submitted),
                                static_cast<unsigned>(auroraResult.prepareError),
                                static_cast<unsigned>(auroraResult.textureUnsupported),
                                static_cast<unsigned>(auroraResult.textureUploadFailed));
                    }
                }
#endif
#else
                if (traceGpu) {
                    RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu draw=%u phase=glDrawArrays_begin\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i));
                }
                glDrawArrays(PrimitiveMode(draw.primitive), draw.firstVertex, draw.vertexCount);
                if (traceGpu) {
                    RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu draw=%u phase=glDrawArrays_returned\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i));
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
                    RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu draw=%u phase=glFinish_begin\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i));
                    glFinish();
                    RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu draw=%u phase=glFinish_end\n",
                            static_cast<unsigned long long>(serial), static_cast<unsigned>(i));
#endif
                }
                ++geometryDrawsPresented;
                geometryVerticesPresented += draw.vertexCount;
#endif
            }

#if defined(MKW_VITA_AURORA_RENDERER)
            ExecuteEfbCommandsAt(packet.geometry.drawCount);
            auroraSubmitEndUs = sceKernelGetProcessTimeWide();
            if (traceLargeFrame) {
                RT_LOGF(RT_TAG_GX,
                        "render_large phase=submit_done serial=%llu efb_done=%u elapsed_us=%llu\n",
                        static_cast<unsigned long long>(serial), static_cast<unsigned>(nextEfbCommand),
                        static_cast<unsigned long long>(auroraSubmitEndUs - frameRenderBeginUs));
            }
            auroraFrameStats = WiiCompiledVita::AuroraPacketRendererEndFrame();
            auroraEndFrameEndUs = sceKernelGetProcessTimeWide();
            if (traceLargeFrame) {
                RT_LOGF(RT_TAG_GX,
                        "render_large phase=endframe_done serial=%llu elapsed_us=%llu\n",
                        static_cast<unsigned long long>(serial),
                        static_cast<unsigned long long>(auroraEndFrameEndUs - frameRenderBeginUs));
            }
#else
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
            glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_ALPHA_TEST);
            glDisable(GL_BLEND);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
#endif
        }
#if defined(MKW_VITA_AURORA_RENDERER)
        if (packet.geometry.vertexCount == 0 && packet.geometry.efbCommandCount != 0) {
            ExecuteEfbCommandsAt(packet.geometry.drawCount);
            auroraSubmitEndUs = sceKernelGetProcessTimeWide();
            auroraFrameStats = WiiCompiledVita::AuroraPacketRendererEndFrame();
            auroraEndFrameEndUs = sceKernelGetProcessTimeWide();
        }
#endif
        const uint64_t swapBeginUs = sceKernelGetProcessTimeWide();
        if (traceLargeFrame) {
            RT_LOGF(RT_TAG_GX, "render_large phase=swap_begin serial=%llu elapsed_us=%llu\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned long long>(swapBeginUs - frameRenderBeginUs));
        }
        if (traceGpu) {
            RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu phase=swap_begin\n",
                    static_cast<unsigned long long>(serial));
        }
        vglSwapBuffers(GL_FALSE);
        const uint64_t swapEndUs = sceKernelGetProcessTimeWide();
        if (traceLargeFrame) {
            RT_LOGF(RT_TAG_GX, "render_large phase=swap_end serial=%llu elapsed_us=%llu\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned long long>(swapEndUs - frameRenderBeginUs));
        }
        if (traceGpu) {
            RT_LOGF(RT_TAG_GX, "gpu_trace frame=%llu phase=swap_end\n",
                    static_cast<unsigned long long>(serial));
        }

#if defined(MKW_TARGET_VITA)
        if (serial <= 8u || (serial % 120u) == 0u) {
            RT_LOGF(
                RT_TAG_GX,
                "frame=%llu skipped_empty=0 packet_draws=%u packet_vertices=%u calls=%llu raw_ok=%llu raw_fail=%llu "
                "raw_cap=%llu begin_cap=%llu dropped=%u xf_idx=%llu/%llu xf=%llu/%llu "
                "tex_state=%llu/%llu/%llu/%llu/%llu/%llu recovered=%llu texcoord_gt0=%llu custom=%llu "
                "custom_mode=%llu/%llu/%llu/%llu/%llu/%llu "
                "presented_draws=%llu transformed=%llu transform_fail=%llu tex_draws=%llu "
                "tex_uploads=%llu tex_fail=%llu tex_unsupported=%llu source_race=%llu "
                "tex_cache=%llu/%llu tex_track=%llu/%llu tex_bytes=%llu invalidate_all=%llu "
                "tev_simple=%llu tev_fallback=%llu texgen=%llu/%llu/%llu "
                "proj=%llu/%llu oversize=%llu max_ndc=%.2f,%.2f viewport=%.1f,%.1f,%.1f,%.1f\n",
                static_cast<unsigned long long>(serial),
                static_cast<unsigned>(packet.geometry.drawCount),
                static_cast<unsigned>(packet.geometry.vertexCount),
                static_cast<unsigned long long>(packet.counters.drawCalls),
                static_cast<unsigned long long>(packet.counters.rawDrawsDecoded),
                static_cast<unsigned long long>(packet.counters.rawDrawDecodeFailures),
                static_cast<unsigned long long>(packet.counters.rawDrawCapacityFailures),
                static_cast<unsigned long long>(packet.counters.immediateDrawCapacityFailures),
                packet.geometry.droppedVertices,
                static_cast<unsigned long long>(packet.counters.xfIndexedLoads),
                static_cast<unsigned long long>(packet.counters.xfIndexedWords),
                static_cast<unsigned long long>(packet.counters.xfPacketsApplied),
                static_cast<unsigned long long>(packet.counters.xfWordsApplied),
                static_cast<unsigned long long>(packet.counters.textureStateNoTexGen),
                static_cast<unsigned long long>(packet.counters.textureStateNoTexAttr),
                static_cast<unsigned long long>(packet.counters.textureStateNoTevStage),
                static_cast<unsigned long long>(packet.counters.textureStateBadOrder),
                static_cast<unsigned long long>(packet.counters.textureStateUnbound),
                static_cast<unsigned long long>(packet.counters.textureStateInvalidObject),
                static_cast<unsigned long long>(packet.counters.textureStateRecoveredLaterStage),
                static_cast<unsigned long long>(packet.counters.textureStateUnsupportedTexCoord),
                static_cast<unsigned long long>(packet.counters.textureStateRecoveredCustomStage),
                static_cast<unsigned long long>(packet.counters.textureStateCustomPresetModulate),
                static_cast<unsigned long long>(packet.counters.textureStateCustomPresetDecal),
                static_cast<unsigned long long>(packet.counters.textureStateCustomPresetBlend),
                static_cast<unsigned long long>(packet.counters.textureStateCustomPresetReplace),
                static_cast<unsigned long long>(packet.counters.textureStateCustomPresetPassClr),
                static_cast<unsigned long long>(packet.counters.textureStateCustomPresetUnknown),
                static_cast<unsigned long long>(geometryDrawsPresented),
                static_cast<unsigned long long>(geometryVerticesTransformed),
                static_cast<unsigned long long>(geometryTransformFailures),
                static_cast<unsigned long long>(textureCounters.draws),
                static_cast<unsigned long long>(textureCounters.uploads),
                static_cast<unsigned long long>(textureCounters.uploadFailures),
                static_cast<unsigned long long>(textureCounters.unsupportedDraws),
                static_cast<unsigned long long>(textureCounters.sourceRaceDraws),
                static_cast<unsigned long long>(textureCounters.cacheHits),
                static_cast<unsigned long long>(textureCounters.cacheMisses),
                static_cast<unsigned long long>(textureCounters.trackedDraws),
                static_cast<unsigned long long>(textureCounters.untrackedDraws),
                static_cast<unsigned long long>(textureCounters.bytesUploaded),
                static_cast<unsigned long long>(packet.counters.textureInvalidateAllCalls),
                static_cast<unsigned long long>(geometryTevSimpleDraws),
                static_cast<unsigned long long>(geometryTevFallbackDraws),
                static_cast<unsigned long long>(geometryTexGenIdentityDraws),
                static_cast<unsigned long long>(geometryTexGenAppliedDraws),
                static_cast<unsigned long long>(geometryTexGenUnsupportedDraws),
                static_cast<unsigned long long>(geometryOrthoDraws),
                static_cast<unsigned long long>(geometryPerspectiveDraws),
                static_cast<unsigned long long>(geometryOversizeNdcDraws),
                geometryMaxNdcSpanX, geometryMaxNdcSpanY,
                packet.viewport[0], packet.viewport[1],
                packet.viewport[2], packet.viewport[3]);
#if defined(MKW_VITA_AURORA_RENDERER)
            RT_LOGF(RT_TAG_GX,
                    "m12_submit serial=%llu presented=%llu none=%u invalid=%u decode=%u xform=%u "
                    "toomany=%u lineexp=%u overflow=%u pipeline=%u noenqueue=%u\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned long long>(geometryDrawsPresented),
                    submitFailByReason[0], submitFailByReason[1], submitFailByReason[2],
                    submitFailByReason[3], submitFailByReason[4], submitFailByReason[5],
                    submitFailByReason[6], submitFailByReason[7], submitFailByReason[8]);
            RT_LOGF(
                RT_TAG_GX,
                "aurora_frame=%llu ready=%u logical_draws=%llu physical_draws=%u triangles=%u "
                "pipeline=%u/%u state_changes=%u stream_bytes=%llu/%llu stream_overflow=%llu/%llu "
                "texture_cache=%llu/%llu high=%llu entries=%u evictions=%llu\n",
                static_cast<unsigned long long>(serial),
                static_cast<unsigned>(auroraFrameReady),
                static_cast<unsigned long long>(geometryDrawsPresented),
                auroraFrameStats.physicalDrawCalls,
                auroraFrameStats.triangles,
                auroraFrameStats.pipelineHits,
                auroraFrameStats.pipelineMisses,
                auroraFrameStats.stateChanges,
                static_cast<unsigned long long>(auroraFrameStats.vertexBytes),
                static_cast<unsigned long long>(auroraFrameStats.indexBytes),
                static_cast<unsigned long long>(auroraFrameStats.vertexOverflows),
                static_cast<unsigned long long>(auroraFrameStats.indexOverflows),
                static_cast<unsigned long long>(auroraFrameStats.textureBytes),
                static_cast<unsigned long long>(auroraFrameStats.textureBudgetBytes),
                static_cast<unsigned long long>(auroraFrameStats.textureHighWaterBytes),
                auroraFrameStats.textureEntries,
                static_cast<unsigned long long>(auroraFrameStats.textureEvictions));
            RT_LOGF(RT_TAG_GX,
                    "m12_1_tex serial=%llu cache_bytes=%llu budget=%llu high=%llu entries=%u "
                    "alloc_fail=%llu pre_evict=%llu pre_evict_bytes=%llu requested=%llu\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned long long>(auroraFrameStats.textureBytes),
                    static_cast<unsigned long long>(auroraFrameStats.textureBudgetBytes),
                    static_cast<unsigned long long>(auroraFrameStats.textureHighWaterBytes),
                    auroraFrameStats.textureEntries,
                    static_cast<unsigned long long>(auroraFrameStats.textureAllocFailTotal),
                    static_cast<unsigned long long>(auroraFrameStats.texturePreEvictions),
                    static_cast<unsigned long long>(auroraFrameStats.texturePreEvictedBytes),
                    static_cast<unsigned long long>(auroraFrameStats.textureRequestedBytes));
            RT_LOGF(RT_TAG_GX,
                    "m12_5_mem serial=%llu efb_bytes=%llu efb_high=%llu efb_entries=%u "
                    "efb_budget=%llu efb_blocked=%llu efb_blocked_bytes=%llu gc=single allocator=custom\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned long long>(auroraFrameStats.efbBytes),
                    static_cast<unsigned long long>(auroraFrameStats.efbHighWaterBytes),
                    auroraFrameStats.efbEntries,
                    static_cast<unsigned long long>(auroraFrameStats.efbBudgetBytes),
                    static_cast<unsigned long long>(auroraFrameStats.efbAllocationBlocked),
                    static_cast<unsigned long long>(auroraFrameStats.efbAllocationBlockedBytes));
            RT_LOGF(
                RT_TAG_GX,
                "perf_frame=%llu render_us=%llu begin_us=%llu submit_us=%llu endframe_us=%llu "
                "swap_us=%llu tex_cache=%llu/%llu tex_track=%llu/%llu tex_uploads=%llu tex_bytes=%llu invalidate_all=%llu\n",
                static_cast<unsigned long long>(serial),
                static_cast<unsigned long long>(swapEndUs - frameRenderBeginUs),
                static_cast<unsigned long long>(auroraBeginEndUs - frameRenderBeginUs),
                static_cast<unsigned long long>(auroraSubmitEndUs - auroraBeginEndUs),
                static_cast<unsigned long long>(auroraEndFrameEndUs - auroraSubmitEndUs),
                static_cast<unsigned long long>(swapEndUs - swapBeginUs),
                static_cast<unsigned long long>(textureCounters.cacheHits),
                static_cast<unsigned long long>(textureCounters.cacheMisses),
                static_cast<unsigned long long>(textureCounters.trackedDraws),
                static_cast<unsigned long long>(textureCounters.untrackedDraws),
                static_cast<unsigned long long>(textureCounters.uploads),
                static_cast<unsigned long long>(textureCounters.bytesUploaded),
                static_cast<unsigned long long>(packet.counters.textureInvalidateAllCalls));
#endif
#if defined(MKW_VITA_AURORA_RENDERER)
            RT_LOGF(RT_TAG_GX,
                    "efb_frame=%llu calls=%llu recorded=%llu capacity_fail=%llu commands=%u "
                    "executed=%llu failed=%llu sampled=%llu gpu=%u readback=%u\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned long long>(packet.counters.efbCopyCalls),
                    static_cast<unsigned long long>(packet.counters.efbCopyRecorded),
                    static_cast<unsigned long long>(packet.counters.efbCopyCapacityFailures),
                    static_cast<unsigned>(packet.geometry.efbCommandCount),
                    static_cast<unsigned long long>(efbCopiesExecuted),
                    static_cast<unsigned long long>(efbCopyFailures),
                    static_cast<unsigned long long>(efbTexturesSampled),
                    auroraFrameStats.efbGpuCopies,
                    auroraFrameStats.efbReadbackCopies);
#endif
            for (size_t signatureIndex = 0;
                 signatureIndex < packet.counters.customTevSignatures.size();
                 ++signatureIndex) {
                const TevSignature& signature =
                    packet.counters.customTevSignatures[signatureIndex];
                if (signature.count == 0) {
                    continue;
                }
                RT_LOGF(
                    RT_TAG_GX,
                    "tev_sig slot=%u count=%u stages=%u selected=%u color=%06X alpha=%06X order=%06X ksel=%06X overflow=%llu\n",
                    static_cast<unsigned>(signatureIndex),
                    static_cast<unsigned>(signature.count),
                    static_cast<unsigned>(signature.stageCount),
                    static_cast<unsigned>(signature.selectedStage),
                    static_cast<unsigned>(signature.color),
                    static_cast<unsigned>(signature.alpha),
                    static_cast<unsigned>(signature.order),
                    static_cast<unsigned>(signature.ksel),
                    static_cast<unsigned long long>(
                        packet.counters.textureStateCustomSignatureOverflow));
            }
        }
#endif

        {
            std::lock_guard<std::mutex> lock(g_renderMutex);
            g_renderBusy = false;
            g_completedSerial = serial;
            g_stats.framesCompleted = g_completedSerial;
            ++g_stats.framesPresented;
            g_stats.geometryDrawsPresented += geometryDrawsPresented;
            g_stats.geometryVerticesPresented += geometryVerticesPresented;
            g_stats.geometryVerticesDropped += packet.geometry.droppedVertices;
            g_stats.geometryVerticesTransformed += geometryVerticesTransformed;
            g_stats.geometryPnMatrixVertices += geometryPnMatrixVertices;
            g_stats.geometryTransformFailures += geometryTransformFailures;
            g_stats.geometryDepthCompareDraws += geometryDepthCompareDraws;
            g_stats.geometryDepthWriteDraws += geometryDepthWriteDraws;
            g_stats.geometryCullNoneDraws += geometryCullNoneDraws;
            g_stats.geometryCullFrontDraws += geometryCullFrontDraws;
            g_stats.geometryCullBackDraws += geometryCullBackDraws;
            g_stats.geometryCullAllSkipped += geometryCullAllSkipped;
            g_stats.geometryBlendDraws += geometryBlendDraws;
            g_stats.geometryBlendFallbackDraws += geometryBlendFallbackDraws;
            g_stats.geometryAlphaTestDraws += geometryAlphaTestDraws;
            g_stats.geometryAlphaCompareFallbackDraws += geometryAlphaCompareFallbackDraws;
            g_stats.geometryTevSimpleDraws += geometryTevSimpleDraws;
            g_stats.geometryTevFallbackDraws += geometryTevFallbackDraws;
            g_stats.textureDrawsPresented += textureCounters.draws;
            g_stats.textureCacheHits += textureCounters.cacheHits;
            g_stats.textureCacheMisses += textureCounters.cacheMisses;
            g_stats.textureUploads += textureCounters.uploads;
            g_stats.textureUploadFailures += textureCounters.uploadFailures;
            g_stats.textureUnsupportedDraws += textureCounters.unsupportedDraws;
            g_stats.textureSourceRaceDraws += textureCounters.sourceRaceDraws;
            g_stats.textureMipFallbackDraws += textureCounters.mipFallbackDraws;
            g_stats.textureBytesUploaded += textureCounters.bytesUploaded;
            g_stats.textureRgb565Uploads += textureCounters.rgb565Uploads;
            g_stats.textureRgb5a3Uploads += textureCounters.rgb5a3Uploads;
            g_stats.textureRgba8Uploads += textureCounters.rgba8Uploads;
            g_stats.textureRgba8PcUploads += textureCounters.rgba8PcUploads;
            g_stats.textureI4Uploads += textureCounters.i4Uploads;
            g_stats.textureI8Uploads += textureCounters.i8Uploads;
            g_stats.textureIa4Uploads += textureCounters.ia4Uploads;
            g_stats.textureIa8Uploads += textureCounters.ia8Uploads;
            g_stats.textureCmprUploads += textureCounters.cmprUploads;
        }
        if (traceLargeFrame) {
            RT_LOGF(RT_TAG_GX, "render_large phase=completed serial=%llu total_us=%llu\n",
                    static_cast<unsigned long long>(serial),
                    static_cast<unsigned long long>(sceKernelGetProcessTimeWide() - frameRenderBeginUs));
        }
        g_renderIdle.notify_all();
    }

#if defined(MKW_VITA_AURORA_RENDERER)
    WiiCompiledVita::AuroraPacketRendererShutdown();
#else
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
    if (g_legacyVertexBuffer != 0) {
        glDeleteBuffers(1, &g_legacyVertexBuffer);
        g_legacyVertexBuffer = 0;
    }
#endif
    DestroyTextureCache();
#endif
    if (g_bootFontTexture != 0) {
        glDeleteTextures(1, &g_bootFontTexture);
        g_bootFontTexture = 0;
    }
#if defined(MKW_VITA_VITAGL_SPEEDHACK)
    if (g_bootVertexBuffer != 0) {
        glDeleteBuffers(1, &g_bootVertexBuffer);
        g_bootVertexBuffer = 0;
        g_bootVertexBufferCapacity = 0;
    }
#endif
    g_bootTextVertices.clear();
    g_bootTextVertices.shrink_to_fit();
    std::lock_guard<std::mutex> lock(g_renderMutex);
    g_renderBusy = false;
    g_renderPending = false;
    g_completedSerial = g_submittedSerial;
    g_renderIdle.notify_all();
}

bool EnsureRenderWorker() {
    std::unique_lock<std::mutex> lock(g_renderMutex);
    if (g_renderStarted) {
        g_renderIdle.wait(lock, [] { return g_renderInitDone; });
        return g_renderInitOk;
    }
    g_renderStop = false;
    g_renderInitDone = false;
    g_renderInitOk = false;
    if (!g_renderThread.start(HostThreadRole::Render, kRenderWorkerStack, RenderWorkerMain)) {
        return false;
    }
    g_renderStarted = true;
    g_renderIdle.wait(lock, [] { return g_renderInitDone; });
    if (g_renderInitOk) {
        return true;
    }
    lock.unlock();
    g_renderThread.join();
    lock.lock();
    g_renderStarted = false;
    return false;
}

void SubmitFrame() {
    const uint64_t producerEnterUs = sceKernelGetProcessTimeWide();
    const uint64_t producerIntervalUs = g_lastProducerSubmitUs != 0
        ? producerEnterUs - g_lastProducerSubmitUs : 0;
    g_lastProducerSubmitUs = producerEnterUs;
    const uint64_t priorWaitCalls = g_guestWaitRenderCallsSinceSubmit;
    const uint64_t priorWaitUs = g_guestWaitRenderUsSinceSubmit;
    g_guestWaitRenderCallsSinceSubmit = 0;
    g_guestWaitRenderUsSinceSubmit = 0;
    const uint32_t producerDraws = g_gx.geometry.drawCount;
    const uint32_t producerVertices = g_gx.geometry.vertexCount;

    const auto AccumulateFrameStats = [](const FrameCounters& frame) {
        g_stats.drawCalls += frame.drawCalls;
        g_stats.vertices += frame.vertices;
        g_stats.displayListBytes += frame.displayListBytes;
        g_stats.displayListsReplayed += frame.displayListsReplayed;
        g_stats.rawDrawBytes += frame.rawDrawBytes;
        g_stats.rawDrawsDecoded += frame.rawDrawsDecoded;
        g_stats.rawDrawDecodeFailures += frame.rawDrawDecodeFailures;
        g_stats.rawDirectAttributesDecoded += frame.rawDirectAttributesDecoded;
        g_stats.rawIndexedAttributesDecoded += frame.rawIndexedAttributesDecoded;
        g_stats.xfPacketsApplied += frame.xfPacketsApplied;
        g_stats.xfWordsApplied += frame.xfWordsApplied;
        g_stats.xfPositionMatrixWords += frame.xfPositionMatrixWords;
        g_stats.xfNormalMatrixWords += frame.xfNormalMatrixWords;
        g_stats.xfProjectionWrites += frame.xfProjectionWrites;
        g_stats.xfViewportWrites += frame.xfViewportWrites;
        g_stats.xfMatrixIndexWrites += frame.xfMatrixIndexWrites;
        g_stats.xfUnsupportedWords += frame.xfUnsupportedWords;
        g_stats.xfIndexedLoads += frame.xfIndexedLoads;
        g_stats.xfIndexedWords += frame.xfIndexedWords;
        for (size_t i = 0; i < g_stats.primitiveDraws.size(); ++i) {
            g_stats.primitiveDraws[i] += frame.primitiveDraws[i];
        }
        for (size_t i = 0; i < g_stats.vertexFormatDraws.size(); ++i) {
            g_stats.vertexFormatDraws[i] += frame.vertexFormatDraws[i];
        }
    };

    if (!EnsureRenderWorker()) {
        // Fallback keeps the serial contract valid even if thread creation fails.
        ++g_submittedSerial;
        g_completedSerial = g_submittedSerial;
        g_stats.framesSubmitted = g_submittedSerial;
        g_stats.framesCompleted = g_completedSerial;
        AccumulateFrameStats(g_gx.frame);
        g_stats.geometryVerticesDropped +=
            static_cast<uint64_t>(g_gx.geometry.vertexCount) + g_gx.geometry.droppedVertices;
        g_gx.frame = {};
        g_gx.geometry.vertexCount = 0;
        g_gx.geometry.drawCount = 0;
        g_gx.geometry.efbCommandCount = 0;
        g_gx.geometry.droppedVertices = 0;
        g_gx.activeDraw = -1;
        return;
    }

    const uint64_t queueWaitBeginUs = sceKernelGetProcessTimeWide();
    std::unique_lock<std::mutex> lock(g_renderMutex);
    g_renderIdle.wait(lock, [] { return !g_renderPending && !g_renderBusy; });
    const uint64_t queueWaitEndUs = sceKernelGetProcessTimeWide();
    const uint64_t packetCopyBeginUs = queueWaitEndUs;
    g_pendingFrame.counters = g_gx.frame;
    // Copy only the active prefixes. FrameGeometry is capacity-sized for worst-case
    // G3D scenes, so assigning the whole object would memcpy ~1.9 MiB even for a
    // 12-draw menu transition. Counts make the unused tail semantically irrelevant.
    g_pendingFrame.geometry.vertexCount = g_gx.geometry.vertexCount;
    g_pendingFrame.geometry.drawCount = g_gx.geometry.drawCount;
    g_pendingFrame.geometry.efbCommandCount = g_gx.geometry.efbCommandCount;
    g_pendingFrame.geometry.droppedVertices = g_gx.geometry.droppedVertices;
    std::copy_n(g_gx.geometry.vertices.begin(), g_gx.geometry.vertexCount,
                g_pendingFrame.geometry.vertices.begin());
    std::copy_n(g_gx.geometry.pnMtxRefs.begin(), g_gx.geometry.vertexCount,
                g_pendingFrame.geometry.pnMtxRefs.begin());
    std::copy_n(g_gx.geometry.draws.begin(), g_gx.geometry.drawCount,
                g_pendingFrame.geometry.draws.begin());
    std::copy_n(g_gx.geometry.efbCommands.begin(), g_gx.geometry.efbCommandCount,
                g_pendingFrame.geometry.efbCommands.begin());
    g_pendingFrame.viewport = g_gx.viewport;
    g_pendingFrame.scissor = g_gx.scissor;
    AccumulateFrameStats(g_pendingFrame.counters);
    g_gx.frame = {};
    g_gx.geometry.vertexCount = 0;
    g_gx.geometry.drawCount = 0;
    g_gx.geometry.efbCommandCount = 0;
    g_gx.geometry.droppedVertices = 0;
    g_gx.activeDraw = -1;
    ++g_submittedSerial;
    const uint64_t packetCopyEndUs = sceKernelGetProcessTimeWide();
    g_stats.framesSubmitted = g_submittedSerial;
    g_renderPending = true;
    const uint64_t submittedSerial = g_submittedSerial;
    lock.unlock();
    g_renderWake.notify_one();

#if !defined(MKW_VITA_PORTING_PROBE)
    GuestStallWatchdog::RecordFrame(submittedSerial, packetCopyEndUs, producerIntervalUs,
                                    TryGetCpuContext());
#endif

#if defined(MKW_TARGET_VITA)
    if (submittedSerial <= 8u || (submittedSerial % 30u) == 0u ||
        producerDraws >= 1000u || producerIntervalUs >= 1000000u ||
        g_pendingFrame.counters.immediateDrawCapacityFailures != 0 ||
        g_pendingFrame.counters.rawDrawCapacityFailures != 0 ||
        g_pendingFrame.counters.efbCopyCapacityFailures != 0) {
        RT_LOGF(RT_TAG_GX,
                "producer_frame=%llu interval_us=%llu queue_wait_us=%llu packet_copy_us=%llu "
                "prior_wait_calls=%llu prior_wait_us=%llu draws=%u vertices=%u efb_cmds=%u "
                "requested_draws=%llu begin_cap=%llu raw_cap=%llu dropped=%u "
                "efb_calls=%llu efb_recorded=%llu efb_cap_fail=%llu efb_destroy=%llu\n",
                static_cast<unsigned long long>(submittedSerial),
                static_cast<unsigned long long>(producerIntervalUs),
                static_cast<unsigned long long>(queueWaitEndUs - queueWaitBeginUs),
                static_cast<unsigned long long>(packetCopyEndUs - packetCopyBeginUs),
                static_cast<unsigned long long>(priorWaitCalls),
                static_cast<unsigned long long>(priorWaitUs),
                producerDraws, producerVertices,
                static_cast<unsigned>(g_pendingFrame.geometry.efbCommandCount),
                static_cast<unsigned long long>(g_pendingFrame.counters.drawCalls),
                static_cast<unsigned long long>(g_pendingFrame.counters.immediateDrawCapacityFailures),
                static_cast<unsigned long long>(g_pendingFrame.counters.rawDrawCapacityFailures),
                g_pendingFrame.geometry.droppedVertices,
                static_cast<unsigned long long>(g_pendingFrame.counters.efbCopyCalls),
                static_cast<unsigned long long>(g_pendingFrame.counters.efbCopyRecorded),
                static_cast<unsigned long long>(g_pendingFrame.counters.efbCopyCapacityFailures),
                static_cast<unsigned long long>(g_pendingFrame.counters.efbDestroyRecorded));
    }
#endif
}

void WaitRender() {
    const uint64_t waitBeginUs = sceKernelGetProcessTimeWide();
    std::unique_lock<std::mutex> lock(g_renderMutex);
    const uint64_t serial = g_submittedSerial;
    while (g_completedSerial < serial) {
        if (g_renderIdle.wait_for(lock, std::chrono::milliseconds(1)) == std::cv_status::timeout) {
            lock.unlock();
            if (g_waitCallback) {
                g_waitCallback();
            }
            lock.lock();
        }
    }
    const uint64_t waitEndUs = sceKernelGetProcessTimeWide();
    ++g_guestWaitRenderCallsSinceSubmit;
    g_guestWaitRenderUsSinceSubmit += waitEndUs - waitBeginUs;
}

bool WaitRenderFor(uint32_t timeoutMicros) {
    std::unique_lock<std::mutex> lock(g_renderMutex);
    const uint64_t serial = g_submittedSerial;
    return g_renderIdle.wait_for(lock, std::chrono::microseconds(timeoutMicros),
                                 [serial] { return g_completedSerial >= serial; });
}

uint32_t TextureLevelSize(u16 width, u16 height, u32 fmt) {
    if (fmt == GX_TF_R8_PC) {
        return static_cast<u32>(width) * height;
    }
    if (fmt == GX_TF_RGBA8_PC) {
        return static_cast<u32>(width) * height * 4u;
    }
    u32 shiftX = 2;
    u32 shiftY = 2;
    switch (fmt) {
    case GX_TF_I4:
    case GX_TF_C4:
    case GX_TF_CMPR:
    case GX_CTF_R4:
    case GX_CTF_Z4:
        shiftX = 3;
        shiftY = 3;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_C8:
    case GX_TF_Z8:
    case GX_CTF_RA4:
    case GX_CTF_A8:
    case GX_CTF_R8:
    case GX_CTF_G8:
    case GX_CTF_B8:
    case GX_CTF_Z8M:
    case GX_CTF_Z8L:
        shiftX = 3;
        shiftY = 2;
        break;
    default:
        break;
    }
    const u32 bytesPerTile = (fmt == GX_TF_RGBA8 || fmt == GX_TF_Z24X8) ? 64u : 32u;
    const u32 tileX = (static_cast<u32>(width) + ((1u << shiftX) - 1u)) >> shiftX;
    const u32 tileY = (static_cast<u32>(height) + ((1u << shiftY) - 1u)) >> shiftY;
    return tileX * tileY * bytesPerTile;
}

} // namespace

namespace WiiCompiledVita::GxBackend {

void SetGuestBeginLr(uint32_t lr) noexcept {
    if (lr != 0u) {
        g_lastGuestBeginLr.store(lr, std::memory_order_relaxed);
    }
}

bool Initialize() noexcept {
    InitializeTransformDefaults();
    std::fill(g_gx.vtxDesc.begin(), g_gx.vtxDesc.end(), GX_NONE);
    std::fill(g_gx.sourceVtxDesc.begin(), g_gx.sourceVtxDesc.end(), GX_NONE);
    return EnsureRenderWorker();
}

void Shutdown() noexcept {
    {
        std::lock_guard<std::mutex> lock(g_renderMutex);
        if (!g_renderStarted) {
            return;
        }
        g_renderStop = true;
    }
    g_renderWake.notify_all();
    g_renderThread.join();
    std::lock_guard<std::mutex> lock(g_renderMutex);
    g_renderStarted = false;
}

Stats SnapshotStats() noexcept {
    std::lock_guard<std::mutex> lock(g_renderMutex);
    return g_stats;
}

bool ApplyXfPacket(const uint8_t* packet, uint32_t packetBytes) noexcept {
    InitializeTransformDefaults();
    return ApplyXfPacketImpl(packet, packetBytes);
}

bool ApplyIndexedXfPacket(uint32_t value, const uint8_t* source,
                          uint32_t sourceBytes) noexcept {
    const uint32_t wordCount = ((value >> 12u) & 0x0Fu) + 1u;
    const uint32_t requiredBytes = wordCount * sizeof(uint32_t);
    if (!source || sourceBytes < requiredBytes) {
        return false;
    }

    // LOAD_INDX_* copies words from a CP XF array into the XF register file.
    // The generic Aurora command processor implements this directly, but the
    // WiiCompiled Vita replay path owns a smaller GX frontend and previously
    // only rebound the source array. Re-express the indexed load as the
    // equivalent LOAD_XF_REG packet so position/normal matrices reach the same
    // state machine as ordinary XF writes.
    std::array<uint8_t, 5u + 16u * sizeof(uint32_t)> packet{};
    const uint32_t header = ((wordCount - 1u) << 16u) | (value & 0x0FFFu);
    packet[0] = 0x10u;
    packet[1] = static_cast<uint8_t>(header >> 24u);
    packet[2] = static_cast<uint8_t>(header >> 16u);
    packet[3] = static_cast<uint8_t>(header >> 8u);
    packet[4] = static_cast<uint8_t>(header);
    std::memcpy(packet.data() + 5u, source, requiredBytes);

    InitializeTransformDefaults();
    const bool applied = ApplyXfPacketImpl(packet.data(), 5u + requiredBytes);
    if (applied) {
        ++g_gx.frame.xfIndexedLoads;
        g_gx.frame.xfIndexedWords += wordCount;
#if defined(MKW_TARGET_VITA)
        // Periodic frame telemetry can miss a short-lived G3D scene because the
        // counters are reset every submitted frame. Emit the first few indexed
        // loads immediately so real-hardware logs prove whether NW4R/G3D reaches
        // the new matrix path before a later renderer fault/transition.
        static uint32_t s_indexedXfTraceCount = 0;
        ++s_indexedXfTraceCount;
        if (s_indexedXfTraceCount <= 16u ||
            (s_indexedXfTraceCount & (s_indexedXfTraceCount - 1u)) == 0u) {
            RT_LOGF(RT_TAG_GX,
                    "xf_indexed n=%u dst=0x%03X words=%u source=%p\n",
                    static_cast<unsigned>(s_indexedXfTraceCount),
                    static_cast<unsigned>(value & 0x0FFFu),
                    static_cast<unsigned>(wordCount),
                    static_cast<const void*>(source));
        }
#endif
    }
    return applied;
}

} // namespace WiiCompiledVita::GxBackend

extern "C" {

AuroraInfo aurora_initialize(int, char*[], const AuroraConfig* config) {
    WiiCompiledVita::GxBackend::Initialize();
    AuroraInfo info{};
    info.backend = BACKEND_NULL;
    info.userPath = config ? config->userPath : nullptr;
    info.cachePath = config ? config->cachePath : nullptr;
    info.windowSize.width = kSurfaceWidth;
    info.windowSize.height = kSurfaceHeight;
    info.windowSize.fb_width = kSurfaceWidth;
    info.windowSize.fb_height = kSurfaceHeight;
    info.windowSize.native_fb_width = kSurfaceWidth;
    info.windowSize.native_fb_height = kSurfaceHeight;
    info.windowSize.scale = 1.0f;
    return info;
}

void aurora_shutdown() { WiiCompiledVita::GxBackend::Shutdown(); }
const AuroraEvent* aurora_update() { return nullptr; }
bool aurora_begin_frame() { return WiiCompiledVita::GxBackend::Initialize(); }
void aurora_end_frame() { SubmitFrame(); }
void aurora_set_frame_worker_wait_callback(AuroraFrameWorkerWaitCallback callback) { g_waitCallback = callback; }
void aurora_wait_for_frame_worker() { WaitRender(); }
bool aurora_wait_for_frame_worker_for(uint32_t timeoutMicros) { return WaitRenderFor(timeoutMicros); }
void aurora_set_present_schedule(uint64_t, uint64_t) {}
void aurora_report_producer_paced(bool) {}
void aurora_request_frame_capture(uint32_t, const char*) {}
bool aurora_flush_efb_copies_to_ram() { return true; }
bool aurora_flush_efb_copy_to_ram(void*) { return true; }
void aurora_set_log_level(AuroraLogLevel) {}
void aurora_set_pause_on_focus_lost(bool) {}
void aurora_set_background_input(bool) {}
void aurora_set_display_mode(AuroraDisplayMode mode) { g_displayMode = mode; }
AuroraDisplayMode aurora_get_display_mode() { return g_displayMode; }
AuroraBackend aurora_get_backend() { return BACKEND_NULL; }
const AuroraBackend* aurora_get_available_backends(size_t* count) {
    static const AuroraBackend backend = BACKEND_NULL;
    if (count) {
        *count = 1;
    }
    return &backend;
}

void aurora_set_guest_write_hooks(AuroraGuestWriteGenerationCallback generation,
                                  AuroraGuestWriteNotifyCallback notify) {
    g_guestWriteGeneration = generation;
    g_guestWriteNotify = notify;
}

void AuroraSetViewportPolicy(AuroraViewportPolicy policy) { g_viewportPolicy = policy; }
void AuroraGetRenderSize(u32* width, u32* height) {
    if (width) *width = kSurfaceWidth;
    if (height) *height = kSurfaceHeight;
}
void AuroraGetSurfaceSize(u32* width, u32* height) { AuroraGetRenderSize(width, height); }
void GXSetViewportRender(f32 left, f32 top, f32 width, f32 height, f32 nearz, f32 farz) {
    GXSetViewport(left, top, width, height, nearz, farz);
}
void GXSetScissorRender(u32 left, u32 top, u32 width, u32 height) { GXSetScissor(left, top, width, height); }
void GXSetViewportScissorRenderSafeArea(f32) {}
void GXRestoreViewportScissorRender() {}
void GXSetTexCopySrcRender(u16 left, u16 top, u16 width, u16 height) {
    GXSetTexCopySrc(left, top, width, height);
}
void GXCreateFrameBuffer(u32, u32) {}
void GXRestoreFrameBuffer() {}
void GXPushDebugGroup(const char*) {}
void GXPopDebugGroup() {}
void GXInsertDebugMarker(const char*) {}

GXFifoObj* GXInit(void* base, u32 size) {
    std::memset(&g_defaultFifo, 0, sizeof(g_defaultFifo));
    GXInitFifoBase(&g_defaultFifo, base, size);
    g_cpuFifo = &g_defaultFifo;
    g_gpFifo = &g_defaultFifo;
    return &g_defaultFifo;
}

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {
    if (!fifo) return;
    std::memset(fifo, 0, sizeof(*fifo));
    auto& meta = Fifo(fifo);
    meta.base = base;
    meta.read = base;
    meta.write = base;
    meta.size = size;
}

void GXSetCPUFifo(GXFifoObj* fifo) { if (fifo) g_cpuFifo = fifo; }
void GXSetGPFifo(GXFifoObj* fifo) { if (fifo) g_gpFifo = fifo; }
GXFifoObj* GXGetCPUFifo() { return g_cpuFifo; }
GXFifoObj* GXGetGPFifo() { return g_gpFifo; }
void GXSaveCPUFifo(GXFifoObj* fifo) { if (fifo && g_cpuFifo) std::memcpy(fifo, g_cpuFifo, sizeof(*fifo)); }
void GXFlush() {}
void GXDrawDone() { WaitRender(); }
void GXPixModeSync() {}

void GXSetVtxDesc(GXAttr attr, GXAttrType type) {
    if (attr >= 0 && attr < GX_VA_MAX_ATTR) g_gx.vtxDesc[static_cast<size_t>(attr)] = type;
}
void GXSetSourceVtxDesc(GXAttr attr, GXAttrType type) {
    if (attr >= 0 && attr < GX_VA_MAX_ATTR) g_gx.sourceVtxDesc[static_cast<size_t>(attr)] = type;
}
void GXGetVtxDesc(GXAttr attr, GXAttrType* type) {
    if (!type) return;
    *type = (attr >= 0 && attr < GX_VA_MAX_ATTR) ? g_gx.vtxDesc[static_cast<size_t>(attr)] : GX_NONE;
}
void GXClearVtxDesc() {
    std::fill(g_gx.vtxDesc.begin(), g_gx.vtxDesc.end(), GX_NONE);
    std::fill(g_gx.sourceVtxDesc.begin(), g_gx.sourceVtxDesc.end(), GX_NONE);
}
void GXSetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    if (fmt >= 0 && fmt < GX_MAX_VTXFMT && attr >= 0 && attr < GX_VA_MAX_ATTR) {
        g_gx.vtxFmt[static_cast<size_t>(fmt)][static_cast<size_t>(attr)] = {cnt, type, frac};
    }
}
void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride, bool le) {
    if (attr >= 0 && attr < GX_VA_MAX_ATTR) {
        g_gx.arrays[static_cast<size_t>(attr)] = {data, size, stride, le};
    }
}
void GXSetNumTexGens(u8 n) { g_gx.numTexGens = n; }
void GXSetTexCoordGen2(GXTexCoordID dst, GXTexGenType type, GXTexGenSrc src,
                       u32 mtx, GXBool normalize, u32 postMtx) {
    if (dst < GX_TEXCOORD0 || dst >= GX_MAX_TEXCOORD) {
        return;
    }
    TexGenState& texGen = g_gx.texGen[static_cast<size_t>(dst)];
    texGen.type = type;
    texGen.src = src;
    texGen.mtx = mtx;
    texGen.normalize = normalize;
    texGen.postMtx = postMtx;
}
void GXSetLineWidth(u8, GXTexOffset) {}
void GXSetPointSize(u8, GXTexOffset) {}
void GXEnableTexOffsets(GXTexCoordID, GXBool, GXBool) {}

void GXBegin(GXPrimitive primitive, GXVtxFmt fmt, u16 nverts) {
    g_gx.primitive = primitive;
    g_gx.currentVtxFmt = fmt;
    g_gx.declaredVertices = nverts;
    g_gx.pendingPnMtxRef = DefaultPnMtxRef();
    g_gx.activeDraw = -1;
    if (g_gx.geometry.drawCount < g_gx.geometry.draws.size()) {
        const u16 drawIndex = g_gx.geometry.drawCount++;
        GeometryDraw& draw = g_gx.geometry.draws[drawIndex];
        draw = {};
        draw.primitive = primitive;
        draw.firstVertex = g_gx.geometry.vertexCount;
        CaptureDrawState(draw);
        g_gx.activeDraw = drawIndex;
    } else {
        ++g_gx.frame.immediateDrawCapacityFailures;
        static std::uint32_t capacityTraceCount = 0;
        if (capacityTraceCount < 8u) {
            ++capacityTraceCount;
            RT_LOGF(RT_TAG_GX,
                    "frame_draw_capacity trace=%u frame_hits=%llu cap=%u vertices=%u declared=%u primitive=0x%X\n",
                    capacityTraceCount,
                    static_cast<unsigned long long>(g_gx.frame.immediateDrawCapacityFailures),
                    static_cast<unsigned>(g_gx.geometry.draws.size()),
                    static_cast<unsigned>(g_gx.geometry.vertexCount),
                    static_cast<unsigned>(nverts), static_cast<unsigned>(primitive));
        }
    }
    ++g_gx.frame.drawCalls;
    g_gx.frame.vertices += nverts;
    ++g_gx.frame.primitiveDraws[PrimitiveBucket(primitive)];
    if (fmt >= 0 && fmt < GX_MAX_VTXFMT) {
        ++g_gx.frame.vertexFormatDraws[static_cast<size_t>(fmt)];
    }
}
void GXEnd() {
    g_gx.declaredVertices = 0;
    g_gx.activeDraw = -1;
}

void GXPosition3f32(f32 x, f32 y, f32 z) { AppendPosition(x, y, z); }
void GXPosition3u16(u16 x, u16 y, u16 z) {
    AppendPosition(DecodeImmediatePositionInteger(x),
                   DecodeImmediatePositionInteger(y),
                   DecodeImmediatePositionInteger(z));
}
void GXPosition3s16(s16 x, s16 y, s16 z) {
    AppendPosition(DecodeImmediatePositionInteger(x),
                   DecodeImmediatePositionInteger(y),
                   DecodeImmediatePositionInteger(z));
}
void GXPosition3u8(u8 x, u8 y, u8 z) {
    AppendPosition(DecodeImmediatePositionInteger(x),
                   DecodeImmediatePositionInteger(y),
                   DecodeImmediatePositionInteger(z));
}
void GXPosition3s8(s8 x, s8 y, s8 z) {
    AppendPosition(DecodeImmediatePositionInteger(x),
                   DecodeImmediatePositionInteger(y),
                   DecodeImmediatePositionInteger(z));
}
void GXPosition2f32(f32 x, f32 y) { AppendPosition(x, y, 0.0f); }
void GXPosition2u16(u16 x, u16 y) {
    AppendPosition(DecodeImmediatePositionInteger(x),
                   DecodeImmediatePositionInteger(y), 0.0f);
}
void GXPosition2s16(s16 x, s16 y) {
    AppendPosition(DecodeImmediatePositionInteger(x),
                   DecodeImmediatePositionInteger(y), 0.0f);
}
void GXPosition2u8(u8 x, u8 y) {
    AppendPosition(DecodeImmediatePositionInteger(x),
                   DecodeImmediatePositionInteger(y), 0.0f);
}
void GXPosition2s8(s8 x, s8 y) {
    AppendPosition(DecodeImmediatePositionInteger(x),
                   DecodeImmediatePositionInteger(y), 0.0f);
}

#define VITA_GX_VERTEX_FN(name, signature) void name signature {}
VITA_GX_VERTEX_FN(GXNormal3f32, (f32, f32, f32))
VITA_GX_VERTEX_FN(GXNormal3u16, (u16, u16, u16))
VITA_GX_VERTEX_FN(GXNormal3s16, (s16, s16, s16))
VITA_GX_VERTEX_FN(GXNormal3u8, (u8, u8, u8))
VITA_GX_VERTEX_FN(GXNormal3s8, (s8, s8, s8))
#undef VITA_GX_VERTEX_FN
void GXTexCoord2f32(f32 s, f32 t) { SetCurrentVertexTexCoord(s, t); }
void GXTexCoord2u16(u16 s, u16 t) {
    SetCurrentVertexTexCoord(DecodeImmediateTexCoordInteger(s), DecodeImmediateTexCoordInteger(t));
}
void GXTexCoord2s16(s16 s, s16 t) {
    SetCurrentVertexTexCoord(DecodeImmediateTexCoordInteger(s), DecodeImmediateTexCoordInteger(t));
}
void GXTexCoord2u8(u8 s, u8 t) {
    SetCurrentVertexTexCoord(DecodeImmediateTexCoordInteger(s), DecodeImmediateTexCoordInteger(t));
}
void GXTexCoord2s8(s8 s, s8 t) {
    SetCurrentVertexTexCoord(DecodeImmediateTexCoordInteger(s), DecodeImmediateTexCoordInteger(t));
}
void GXTexCoord1f32(f32 s) { SetCurrentVertexTexCoord(s, 0.0f); }
void GXTexCoord1u16(u16 s) { SetCurrentVertexTexCoord(DecodeImmediateTexCoordInteger(s), 0.0f); }
void GXTexCoord1s16(s16 s) { SetCurrentVertexTexCoord(DecodeImmediateTexCoordInteger(s), 0.0f); }
void GXTexCoord1u8(u8 s) { SetCurrentVertexTexCoord(DecodeImmediateTexCoordInteger(s), 0.0f); }
void GXTexCoord1s8(s8 s) { SetCurrentVertexTexCoord(DecodeImmediateTexCoordInteger(s), 0.0f); }
void GXColor4u8(u8 r, u8 g, u8 b, u8 a) { SetCurrentVertexColor(r, g, b, a); }
void GXColor3u8(u8 r, u8 g, u8 b) { SetCurrentVertexColor(r, g, b, 255); }
void GXMatrixIndex1u8(GXAttr attr, u8 index) {
    if (attr == GX_VA_PNMTXIDX) {
        g_gx.pendingPnMtxRef = static_cast<u8>(PnMtxSlot(index) | kPnMtxExplicitBit);
    }
}

void GXCallDisplayList(const void* data, u32 nbytes) {
    g_gx.frame.displayListBytes += nbytes;
#if defined(MKW_TARGET_VITA)
    if (data && nbytes != 0 && !g_replayingDisplayList && GX_HLE_ReplayDisplayListVita) {
        g_replayingDisplayList = true;
        ++g_gx.frame.displayListsReplayed;
        GX_HLE_ReplayDisplayListVita(static_cast<const uint8_t*>(data), nbytes);
        g_replayingDisplayList = false;
    }
#endif
}

void GXApplyBPReg(u8 reg, u32 value) {
    value &= 0x00ffffffu;
    g_gx.bpRegs[reg] = value;
    if (reg >= 0xC0u && reg <= 0xDFu) {
        const size_t stage = static_cast<size_t>(reg - 0xC0u) / 2u;
        if (stage < g_gx.tevPresetValid.size()) {
            g_gx.tevPresetValid[stage] = 0u;
        }
    }
    switch (reg) {
    case 0x00: {
        // BP GEN_MODE mirrors the pipeline counts used by GXSetNumTexGens,
        // GXSetNumChans, GXSetNumTevStages and GXSetNumIndStages.
        g_gx.numTexGens = static_cast<u8>(value & 0xFu);
        g_gx.numChans = static_cast<u8>((value >> 4u) & 0x7u);
        g_gx.numTevStages = static_cast<u8>(((value >> 10u) & 0xFu) + 1u);
        g_gx.numIndStages = static_cast<u8>((value >> 16u) & 0x7u);
        const auto hwCull = static_cast<GXCullMode>((value >> 14) & 0x3u);
        if (hwCull == GX_CULL_FRONT) {
            g_gx.cullMode = GX_CULL_BACK;
        } else if (hwCull == GX_CULL_BACK) {
            g_gx.cullMode = GX_CULL_FRONT;
        } else {
            g_gx.cullMode = hwCull;
        }
        break;
    }
    case 0x28:
    case 0x29:
    case 0x2A:
    case 0x2B:
    case 0x2C:
    case 0x2D:
    case 0x2E:
    case 0x2F: {
        const size_t firstStage = static_cast<size_t>(reg - 0x28u) * 2u;
        if (firstStage < g_gx.tevTexMaps.size()) {
            g_gx.tevTexMaps[firstStage] = (value & (1u << 6u))
                ? static_cast<GXTexMapID>(value & 0x7u) : GX_TEXMAP_NULL;
            g_gx.tevTexCoords[firstStage] = static_cast<GXTexCoordID>((value >> 3u) & 0x7u);
        }
        if (firstStage + 1u < g_gx.tevTexMaps.size()) {
            g_gx.tevTexMaps[firstStage + 1u] = (value & (1u << 18u))
                ? static_cast<GXTexMapID>((value >> 12u) & 0x7u) : GX_TEXMAP_NULL;
            g_gx.tevTexCoords[firstStage + 1u] = static_cast<GXTexCoordID>((value >> 15u) & 0x7u);
        }
        break;
    }
    case 0x40:
        g_gx.depthCompare = (value & 0x1u) ? GX_TRUE : GX_FALSE;
        g_gx.depthFunc = static_cast<GXCompare>((value >> 1) & 0x7u);
        g_gx.depthUpdate = (value & 0x10u) ? GX_TRUE : GX_FALSE;
        break;
    case 0x41: {
        const bool blendEnabled = (value & 0x1u) != 0;
        const bool logicEnabled = (value & 0x2u) != 0;
        const bool subtract = (value & (1u << 11u)) != 0;
        g_gx.dither = (value & (1u << 2u)) ? GX_TRUE : GX_FALSE;
        g_gx.colorUpdate = (value & (1u << 3u)) ? GX_TRUE : GX_FALSE;
        g_gx.alphaUpdate = (value & (1u << 4u)) ? GX_TRUE : GX_FALSE;
        g_gx.blendDst = static_cast<GXBlendFactor>((value >> 5u) & 0x7u);
        g_gx.blendSrc = static_cast<GXBlendFactor>((value >> 8u) & 0x7u);
        g_gx.logicOp = static_cast<GXLogicOp>((value >> 12u) & 0xFu);
        g_gx.blendMode = subtract ? GX_BM_SUBTRACT
            : blendEnabled ? GX_BM_BLEND
            : logicEnabled ? GX_BM_LOGIC : GX_BM_NONE;
        break;
    }
    case 0x4F:
        g_gx.copyClearColor.r = static_cast<u8>(value & 0xFFu);
        g_gx.copyClearColor.a = static_cast<u8>((value >> 8u) & 0xFFu);
        break;
    case 0x50:
        g_gx.copyClearColor.b = static_cast<u8>(value & 0xFFu);
        g_gx.copyClearColor.g = static_cast<u8>((value >> 8u) & 0xFFu);
        break;
    case 0x51:
        g_gx.copyClearDepth = value & 0x00ffffffu;
        break;
    case 0xF3:
        g_gx.alphaRef0 = static_cast<u8>(value & 0xFFu);
        g_gx.alphaRef1 = static_cast<u8>((value >> 8u) & 0xFFu);
        g_gx.alphaComp0 = static_cast<GXCompare>((value >> 16u) & 0x7u);
        g_gx.alphaComp1 = static_cast<GXCompare>((value >> 19u) & 0x7u);
        g_gx.alphaOp = static_cast<GXAlphaOp>((value >> 22u) & 0x3u);
        break;
    default:
        break;
    }
}

void GXSetProjection(const void* mtx, GXProjectionType type) {
    InitializeTransformDefaults();
    if (mtx) {
        std::memcpy(g_gx.projection.data(), mtx, sizeof(f32) * 16);
        const auto* p = static_cast<const f32*>(mtx);
        g_gx.xfProjection = {
            p[0], type == GX_ORTHOGRAPHIC ? p[3] : p[2],
            p[5], type == GX_ORTHOGRAPHIC ? p[7] : p[6],
            p[10], p[11],
        };
    }
    g_gx.projectionType = type;
}
void GXLoadPosMtxImm(const void* mtx, u32 id) {
    if (!mtx) return;
    InitializeTransformDefaults();
    const size_t slot = std::min<size_t>(id / 3u, g_gx.posMtx.size() - 1u);
    std::memcpy(g_gx.posMtx[slot].data(), mtx, sizeof(f32) * 12);
}
void GXLoadNrmMtxImm(const void* mtx, u32 id) {
    if (!mtx) return;
    const size_t slot = std::min<size_t>(id / 3u, g_gx.nrmMtx.size() - 1u);
    std::memcpy(g_gx.nrmMtx[slot].data(), mtx, sizeof(f32) * 12);
}
void GXLoadTexMtxImm(const void* mtx, u32 id, GXTexMtxType type) {
    if (!mtx) return;
    const size_t count = type == GX_MTX2x4 ? 8u : 12u;
    if (id >= GX_PTTEXMTX0 && id <= GX_PTIDENTITY) {
        if (id == GX_PTIDENTITY) return;
        const size_t slot = static_cast<size_t>((id - GX_PTTEXMTX0) / 3u);
        if (slot < g_gx.postTexMtx.size()) {
            auto& dst = g_gx.postTexMtx[slot];
            dst = {1.0f, 0.0f, 0.0f, 0.0f,
                   0.0f, 1.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 1.0f, 0.0f};
            std::memcpy(dst.data(), mtx, sizeof(f32) * count);
        }
        return;
    }
    if (id == GX_IDENTITY) return;
    const size_t slot = static_cast<size_t>(id / 3u);
    if (slot < g_gx.texMtx.size()) {
        auto& dst = g_gx.texMtx[slot];
        dst = {1.0f, 0.0f, 0.0f, 0.0f,
               0.0f, 1.0f, 0.0f, 0.0f,
               0.0f, 0.0f, 1.0f, 0.0f};
        std::memcpy(dst.data(), mtx, sizeof(f32) * count);
    }
}
void GXSetCurrentMtx(u32 id) {
    g_gx.currentMtx = id;
    g_gx.pendingPnMtxRef = DefaultPnMtxRef();
}
void GXSetViewport(f32 left, f32 top, f32 width, f32 height, f32 nearz, f32 farz) {
    g_gx.viewport = {left, top, width, height, nearz, farz};
    const f32 sx = width * 0.5f;
    const f32 sy = -height * 0.5f;
    const f32 zmin = 16777216.0f * nearz;
    const f32 zmax = 16777216.0f * farz;
    g_gx.xfViewport = {
        sx, sy, zmax - zmin,
        340.0f + left + width * 0.5f,
        340.0f + top + height * 0.5f,
        zmax,
    };
}
void GXSetViewportJitter(f32 left, f32 top, f32 width, f32 height, f32 nearz, f32 farz, u32) {
    GXSetViewport(left, top, width, height, nearz, farz);
}
void GXSetZScaleOffset(f32 scale, f32 offset) { g_gx.zScale = scale; g_gx.zOffset = offset; }
void GXSetScissorBoxOffset(s32 x, s32 y) { g_gx.scissorOffsetX = x; g_gx.scissorOffsetY = y; }
void GXSetScissor(u32 left, u32 top, u32 width, u32 height) { g_gx.scissor = {left, top, width, height}; }
void GXSetClipMode(GXClipMode mode) { g_gx.clipMode = mode; }
void GXSetCullMode(GXCullMode mode) { g_gx.cullMode = mode; }
void GXSetCoPlanar(GXBool) {}

void GXSetBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, GXLogicOp op) {
    g_gx.blendMode = type; g_gx.blendSrc = src; g_gx.blendDst = dst; g_gx.logicOp = op;
}
void GXSetColorUpdate(GXBool enabled) { g_gx.colorUpdate = enabled; }
void GXSetAlphaUpdate(GXBool enabled) { g_gx.alphaUpdate = enabled; }
void GXSetZMode(GXBool compare, GXCompare func, GXBool update) {
    g_gx.depthCompare = compare; g_gx.depthFunc = func; g_gx.depthUpdate = update;
}
void GXSetZCompLoc(GXBool before) { g_gx.zCompBeforeTex = before; }
void GXSetPixelFmt(GXPixelFmt fmt, GXZFmt16 zfmt) { g_gx.pixelFmt = fmt; g_gx.zFmt = zfmt; }
void GXSetDither(GXBool enabled) { g_gx.dither = enabled; }
void GXSetDstAlpha(GXBool enabled, u8 alpha) { g_gx.dstAlphaEnable = enabled; g_gx.dstAlpha = alpha; }
void GXSetFog(GXFogType, f32, f32, f32, f32, GXColor) {}
void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
    g_gx.alphaComp0 = comp0;
    g_gx.alphaRef0 = ref0;
    g_gx.alphaOp = op;
    g_gx.alphaComp1 = comp1;
    g_gx.alphaRef1 = ref1;
}
void GXSetZTexture(GXZTexOp, GXTexFmt, u32) {}

void GXSetNumTevStages(u8 n) { g_gx.numTevStages = n; }
void GXSetTevOp(GXTevStageID stage, GXTevMode mode) {
    if (stage >= 0 && stage < GX_MAX_TEVSTAGE) {
        g_gx.tevModes[static_cast<size_t>(stage)] = mode;
        g_gx.tevPresetValid[static_cast<size_t>(stage)] = 1u;
    }
}
void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID) {
    if (stage >= 0 && stage < GX_MAX_TEVSTAGE) {
        g_gx.tevTexCoords[static_cast<size_t>(stage)] = coord;
        g_gx.tevTexMaps[static_cast<size_t>(stage)] = map;
    }
}
void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b,
                     GXTevColorArg c, GXTevColorArg d) {
    if (stage >= 0 && stage < GX_MAX_TEVSTAGE) {
        const size_t index = static_cast<size_t>(stage);
        g_gx.tevPresetValid[index] = 0u;
        u32& reg = g_gx.bpRegs[0xC0u + index * 2u];
        SetPackedBits(reg, 12u, 4u, static_cast<u32>(a));
        SetPackedBits(reg, 8u, 4u, static_cast<u32>(b));
        SetPackedBits(reg, 4u, 4u, static_cast<u32>(c));
        SetPackedBits(reg, 0u, 4u, static_cast<u32>(d));
    }
}
void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b,
                     GXTevAlphaArg c, GXTevAlphaArg d) {
    if (stage >= 0 && stage < GX_MAX_TEVSTAGE) {
        const size_t index = static_cast<size_t>(stage);
        g_gx.tevPresetValid[index] = 0u;
        u32& reg = g_gx.bpRegs[0xC1u + index * 2u];
        SetPackedBits(reg, 13u, 3u, static_cast<u32>(a));
        SetPackedBits(reg, 10u, 3u, static_cast<u32>(b));
        SetPackedBits(reg, 7u, 3u, static_cast<u32>(c));
        SetPackedBits(reg, 4u, 3u, static_cast<u32>(d));
    }
}
void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale,
                     GXBool clamp, GXTevRegID outReg) {
    if (stage >= 0 && stage < GX_MAX_TEVSTAGE) {
        const size_t index = static_cast<size_t>(stage);
        g_gx.tevPresetValid[index] = 0u;
        u32& reg = g_gx.bpRegs[0xC0u + index * 2u];
        SetPackedBits(reg, 18u, 1u, static_cast<u32>(op) & 1u);
        if (op <= GX_TEV_SUB) {
            SetPackedBits(reg, 20u, 2u, static_cast<u32>(scale));
            SetPackedBits(reg, 16u, 2u, static_cast<u32>(bias));
        } else {
            SetPackedBits(reg, 20u, 2u, (static_cast<u32>(op) >> 1u) & 3u);
            SetPackedBits(reg, 16u, 2u, 3u);
        }
        SetPackedBits(reg, 19u, 1u, clamp ? 1u : 0u);
        SetPackedBits(reg, 22u, 2u, static_cast<u32>(outReg));
    }
}
void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale,
                     GXBool clamp, GXTevRegID outReg) {
    if (stage >= 0 && stage < GX_MAX_TEVSTAGE) {
        const size_t index = static_cast<size_t>(stage);
        g_gx.tevPresetValid[index] = 0u;
        u32& reg = g_gx.bpRegs[0xC1u + index * 2u];
        SetPackedBits(reg, 18u, 1u, static_cast<u32>(op) & 1u);
        if (op <= GX_TEV_SUB) {
            SetPackedBits(reg, 20u, 2u, static_cast<u32>(scale));
            SetPackedBits(reg, 16u, 2u, static_cast<u32>(bias));
        } else {
            SetPackedBits(reg, 20u, 2u, (static_cast<u32>(op) >> 1u) & 3u);
            SetPackedBits(reg, 16u, 2u, 3u);
        }
        SetPackedBits(reg, 19u, 1u, clamp ? 1u : 0u);
        SetPackedBits(reg, 22u, 2u, static_cast<u32>(outReg));
    }
}
void GXSetTevColor(GXTevRegID id, GXColor color) {
    if (id >= 0 && static_cast<size_t>(id) < g_gx.tevColor.size()) g_gx.tevColor[static_cast<size_t>(id)] = color;
}
void GXSetTevColorS10(GXTevRegID, GXColorS10) {}
void GXSetTevKColor(GXTevKColorID id, GXColor color) {
    if (id >= 0 && id < GX_MAX_KCOLOR) g_gx.kColor[static_cast<size_t>(id)] = color;
}
void GXSetTevKColorSel(GXTevStageID, GXTevKColorSel) {}
void GXSetTevKAlphaSel(GXTevStageID, GXTevKAlphaSel) {}
void GXSetTevSwapMode(GXTevStageID, GXTevSwapSel, GXTevSwapSel) {}
void GXSetTevSwapModeTable(GXTevSwapSel, GXTevColorChan, GXTevColorChan, GXTevColorChan, GXTevColorChan) {}

void GXSetNumIndStages(u8 n) { g_gx.numIndStages = n; }
void GXSetIndTexMtx(GXIndTexMtxID, const void*, s8) {}
void GXSetIndTexOrder(GXIndTexStageID, GXTexCoordID, GXTexMapID) {}
void GXSetIndTexCoordScale(GXIndTexStageID, GXIndTexScale, GXIndTexScale) {}
void GXSetTevDirect(GXTevStageID) {}
void GXSetTevIndirect(GXTevStageID, GXIndTexStageID, GXIndTexFormat, GXIndTexBiasSel, GXIndTexMtxID,
                      GXIndTexWrap, GXIndTexWrap, GXBool, GXBool, GXIndTexAlphaSel) {}
void GXSetTevIndWarp(GXTevStageID, GXIndTexStageID, GXBool, GXBool, GXIndTexMtxID) {}

void GXSetNumChans(u8 n) { g_gx.numChans = n; }
void GXSetChanAmbColor(GXChannelID chan, GXColor color) {
    const size_t idx = static_cast<size_t>(chan) & 3u;
    g_gx.chanAmb[idx] = color;
}
void GXSetChanMatColor(GXChannelID chan, GXColor color) {
    const size_t idx = static_cast<size_t>(chan) & 3u;
    g_gx.chanMat[idx] = color;
}
void GXSetChanCtrl(GXChannelID, GXBool, GXColorSrc, GXColorSrc, u32, GXDiffuseFn, GXAttnFn) {}

void GXInitLightColor(GXLightObj* obj, GXColor color) { if (obj) Light(obj).color = color; }
void GXInitLightAttn(GXLightObj* obj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
    if (!obj) return;
    auto& l = Light(obj); l.a0 = a0; l.a1 = a1; l.a2 = a2; l.k0 = k0; l.k1 = k1; l.k2 = k2;
}
void GXInitLightPos(GXLightObj* obj, f32 x, f32 y, f32 z) {
    if (!obj) return; auto& l = Light(obj); l.px = x; l.py = y; l.pz = z;
}
void GXInitLightDir(GXLightObj* obj, f32 x, f32 y, f32 z) {
    if (!obj) return; auto& l = Light(obj); l.nx = x; l.ny = y; l.nz = z;
}
void GXLoadLightObjImm(GXLightObj*, GXLightID) {}

void GXInitTexObj(GXTexObj* obj, const void* data, u16 width, u16 height, GXTexFmt format,
                  GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap) {
    if (!obj) return;
    std::memset(obj, 0, sizeof(*obj));
    auto& t = Tex(obj);
    t.data = data; t.width = width; t.height = height; t.format = format;
    t.wrapS = wrapS; t.wrapT = wrapT; t.mipmap = mipmap;
    t.dataRevision = NextTextureRevision();
}
void GXInitTexObjCI(GXTexObj* obj, const void* data, u16 width, u16 height, GXCITexFmt format,
                    GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap, u32 tlut) {
    GXInitTexObj(obj, data, width, height, static_cast<GXTexFmt>(format), wrapS, wrapT, mipmap);
    if (obj) Tex(obj).tlut = tlut;
}
void GXInitTexObjData(GXTexObj* obj, const void* data) {
    if (!obj) return;
    Tex(obj).data = data;
    Tex(obj).dataRevision = NextTextureRevision();
}
void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter minFilter, GXTexFilter magFilter, f32 minLod, f32 maxLod,
                     f32 bias, GXBool biasClamp, GXBool edgeLod, GXAnisotropy aniso) {
    if (!obj) return;
    auto& t = Tex(obj); t.minFilter = minFilter; t.magFilter = magFilter; t.minLod = minLod;
    t.maxLod = maxLod; t.lodBias = bias; t.biasClamp = biasClamp; t.edgeLod = edgeLod; t.maxAniso = aniso;
}
void GXInitTexObjWrapMode(GXTexObj* obj, GXTexWrapMode s, GXTexWrapMode t) {
    if (obj) { Tex(obj).wrapS = s; Tex(obj).wrapT = t; }
}
void GXInitTexObjTlut(GXTexObj* obj, u32 tlut) { if (obj) Tex(obj).tlut = tlut; }
void GXInitTexObjUserData(GXTexObj* obj, void* data) { if (obj) Tex(obj).userData = data; }
void GXLoadTexObj(GXTexObj* obj, GXTexMapID id) {
    if (id >= 0 && id < GX_MAX_TEXMAP) g_gx.textures[static_cast<size_t>(id)] = obj;
}
void GXDestroyTexObj(GXTexObj* obj) {
    if (!obj) return;
    for (auto& bound : g_gx.textures) {
        if (bound == obj) {
            bound = nullptr;
        }
    }
    std::memset(obj, 0, sizeof(*obj));
}
void GXInvalidateTexAll() {
    ++g_gx.frame.textureInvalidateAllCalls;
    ++g_gx.textureGlobalEpoch;
    if (g_gx.textureGlobalEpoch == 0) {
        g_gx.textureGlobalEpoch = 1;
    }
}
void GXSetTexCoordScaleManually(GXTexCoordID, GXBool, u16, u16) {}
void GXSetTexCoordBias(GXTexCoordID, GXBool, GXBool) {}

u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, GXBool mipmap, u8 maxLod) {
    u32 total = 0;
    const u32 levels = mipmap ? static_cast<u32>(maxLod) + 1u : 1u;
    for (u32 level = 0; level < levels; ++level) {
        total += TextureLevelSize(width, height, format);
        if (width == 1 && height == 1) break;
        width = std::max<u16>(1, width / 2);
        height = std::max<u16>(1, height / 2);
    }
    return total;
}

void GXInitTlutObj(GXTlutObj* obj, const void* data, GXTlutFmt format, u16 entries) {
    if (!obj) return;
    std::memset(obj, 0, sizeof(*obj));
    auto& t = Tlut(obj); t.data = data; t.format = format; t.entries = entries;
}
void GXLoadTlut(const GXTlutObj* obj, u32 idx) {
    if (idx < g_gx.tluts.size()) g_gx.tluts[idx] = const_cast<GXTlutObj*>(obj);
}
void GXDestroyTlutObj(GXTlutObj* obj) { if (obj) std::memset(obj, 0, sizeof(*obj)); }
void GXDestroyCopyTex(void* destination) {
    if (!destination || g_gx.geometry.efbCommandCount >= kMaxFrameEfbCommands) return;
    EfbFrameCommand& command = g_gx.geometry.efbCommands[g_gx.geometry.efbCommandCount++];
    command = {};
    command.type = EfbFrameCommandType::Destroy;
    command.afterDrawCount = g_gx.geometry.drawCount;
    command.destination = reinterpret_cast<uintptr_t>(destination);
    ++g_gx.frame.efbDestroyRecorded;
}

void GXSetDispCopySrc(u16 left, u16 top, u16 width, u16 height) {
    g_gx.dispCopyLeft = left; g_gx.dispCopyTop = top; g_gx.dispCopySrcWidth = width; g_gx.dispCopySrcHeight = height;
}
void GXSetDispCopyDst(u16 width, u16 height) { g_gx.dispCopyWidth = width; g_gx.dispCopyHeight = height; }
void GXSetTexCopySrc(u16 left, u16 top, u16 width, u16 height) {
    g_gx.texCopyLeft = left; g_gx.texCopyTop = top; g_gx.texCopySrcWidth = width; g_gx.texCopySrcHeight = height;
}
void GXSetTexCopyDst(u16 width, u16 height, GXTexFmt fmt, GXBool mipmap) {
    g_gx.texCopyWidth = width; g_gx.texCopyHeight = height; g_gx.texCopyFmt = fmt; g_gx.texCopyMipmap = mipmap;
}
void GXSetCopyFilter(GXBool, u8[12][2], GXBool, u8[7]) {}
void GXSetDispCopyGamma(GXGamma gamma) { g_gx.dispGamma = gamma; }
void GXSetDispCopyFrame2Field(u32 mode) { g_gx.frame2Field = mode; }
void GXSetCopyClamp(GXFBClamp clamp) { g_gx.copyClamp = clamp; }
void GXCopyDisp(void*, GXBool) {}
void GXCopyTex(void* destination, GXBool clear) {
    ++g_gx.frame.efbCopyCalls;
    if (!destination || g_gx.texCopySrcWidth == 0 || g_gx.texCopySrcHeight == 0 ||
        g_gx.texCopyWidth == 0 || g_gx.texCopyHeight == 0) {
        return;
    }
    if (g_gx.geometry.efbCommandCount >= kMaxFrameEfbCommands) {
        ++g_gx.frame.efbCopyCapacityFailures;
        return;
    }
    EfbFrameCommand& command = g_gx.geometry.efbCommands[g_gx.geometry.efbCommandCount++];
    command = {};
    command.type = EfbFrameCommandType::Copy;
    command.afterDrawCount = g_gx.geometry.drawCount;
    command.destination = reinterpret_cast<uintptr_t>(destination);
    command.srcLeft = g_gx.texCopyLeft;
    command.srcTop = g_gx.texCopyTop;
    command.srcWidth = g_gx.texCopySrcWidth;
    command.srcHeight = g_gx.texCopySrcHeight;
    command.dstWidth = g_gx.texCopyWidth;
    command.dstHeight = g_gx.texCopyHeight;
    command.format = static_cast<u32>(g_gx.texCopyFmt);
    command.clearColor = g_gx.copyClearColor;
    command.clearDepth = g_gx.copyClearDepth;
    command.clear = clear ? 1u : 0u;
    command.clearColorEnable = g_gx.colorUpdate ? 1u : 0u;
    command.clearAlphaEnable = g_gx.alphaUpdate ? 1u : 0u;
    command.clearDepthEnable = g_gx.depthUpdate ? 1u : 0u;
    ++g_gx.frame.efbCopyRecorded;
    static std::uint64_t s_efbRecordTrace = 0;
    const std::uint64_t trace = ++s_efbRecordTrace;
    if (trace <= 32u) {
        RT_LOGF(RT_TAG_GX,
                "efb_record n=%llu after_draw=%u dest=%p src=%u,%u %ux%u dst=%ux%u fmt=0x%X clear=%u\n",
                static_cast<unsigned long long>(trace),
                static_cast<unsigned>(command.afterDrawCount), destination,
                static_cast<unsigned>(command.srcLeft), static_cast<unsigned>(command.srcTop),
                static_cast<unsigned>(command.srcWidth), static_cast<unsigned>(command.srcHeight),
                static_cast<unsigned>(command.dstWidth), static_cast<unsigned>(command.dstHeight),
                static_cast<unsigned>(command.format), static_cast<unsigned>(command.clear));
    }
}
void GXClearBoundingBox() { g_gx.boundingBox = {1023, 0, 1023, 0}; }

} // extern "C"

namespace aurora::gx::fifo {

bool submit_raw_draw(GXPrimitive primitive, GXVtxFmt fmt, const uint8_t* vertices,
                     uint16_t vtxCount, uint32_t vertexBytes) {
    if (DecodeRawDraw(primitive, fmt, vertices, vtxCount, vertexBytes)) {
        return true;
    }
    ++g_gx.frame.rawDrawDecodeFailures;
#if defined(MKW_TARGET_VITA)
    static uint64_t s_rawFailTrace = 0;
    const uint64_t trace = ++s_rawFailTrace;
    if (trace <= 32u || (trace & (trace - 1u)) == 0u) {
        const RawDecodeFailure failure = g_rawDecodeFailure;
        RT_LOGF(RT_TAG_GX,
                "raw_decode_fail n=%llu reason=%s(%u) prim=0x%X fmt=%u verts=%u bytes=%u "
                "vertex=%u attr=%u desc=%u index=%u elem=%u array=%u stride=%u cursor=%u\n",
                static_cast<unsigned long long>(trace), RawDecodeFailName(failure.reason),
                static_cast<unsigned>(failure.reason), static_cast<unsigned>(primitive),
                static_cast<unsigned>(fmt), static_cast<unsigned>(vtxCount),
                static_cast<unsigned>(vertexBytes), static_cast<unsigned>(failure.vertex),
                static_cast<unsigned>(failure.attr), static_cast<unsigned>(failure.desc),
                static_cast<unsigned>(failure.index), static_cast<unsigned>(failure.elementBytes),
                static_cast<unsigned>(failure.arraySize), static_cast<unsigned>(failure.arrayStride),
                static_cast<unsigned>(failure.cursorOffset));
    }
#endif
    return false;
}

} // namespace aurora::gx::fifo
