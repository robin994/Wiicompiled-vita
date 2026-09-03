#include "gx_internal.h"

#include "hle_stubs.h"
#include "memory.h"
#include "memory_access.h"

#include <array>
#include <cstdint>

extern "C" void GX__Begin_8016f0f0(uint32_t primitive, uint32_t vtxFmt,
                                      uint32_t vertexCount);
extern "C" void GX__LoadTexObj_80170f2c(uint32_t objAddr, uint32_t texMap);

namespace {

inline void StoreBe16(uint8_t* destination, uint16_t value) noexcept {
    destination[0] = static_cast<uint8_t>(value >> 8u);
    destination[1] = static_cast<uint8_t>(value);
}

// Faithful native implementation of Text::GlyphDrawer::Draw. The original is
// called once per glyph and repeatedly enters translated SetupGXColors even
// when both cached state keys already match. Keep all GX-visible behaviour but
// collapse the 32-byte vertex payload into one FIFO burst.
void Text__GlyphDrawer__Draw_HLE_805cf598(CpuContext* ctx) {
    const uint32_t savedLr = ctx->lr;
    const uint32_t drawer = ctx->gpr[3];
    const uint32_t glyph = ctx->gpr[4];
    const uint32_t colorKey = ctx->gpr[5];

    const uint16_t format = MemoryInline::FlatRead16(glyph + 10u);
    SetCRResident(ctx->cr, ctx->xer, 0, static_cast<uint32_t>(format), 8u);
    if (format == 8u) {
        ctx->gpr[0] = savedLr;
        ctx->gpr[5] = format;
        return;
    }

    uint8_t* glyphBytes = MemoryInline::ResolveRangeHost(glyph, 0, 24u, true, false);
    const int32_t x0 = static_cast<int16_t>(MemoryInline::ReadResolved16(glyphBytes, 0u, glyph));
    const int32_t x1 = static_cast<int16_t>(MemoryInline::ReadResolved16(glyphBytes, 2u, glyph + 2u));
    SetCRResident(ctx->cr, ctx->xer, 0, x0, x1);
    if (x0 < x1) {
        ctx->gpr[0] = savedLr;
        ctx->gpr[5] = format;
        ctx->gpr[6] = static_cast<uint32_t>(x0);
        return;
    }

    const uint16_t setupKey = MemoryInline::ReadResolved16(glyphBytes, 8u, glyph + 8u);
    const bool setupRequired =
        MemoryInline::FlatRead32(drawer + 4u) != setupKey ||
        MemoryInline::FlatRead32(drawer + 8u) != colorKey;
    if (setupRequired) {
        ctx->lr = 0x805CF5D8u;
        ctx->gpr[3] = drawer;
        ctx->gpr[4] = setupKey;
        ctx->gpr[5] = colorKey;
        InvokeDirectCpu<0x805CF7E4u>(ctx);
    }

    const uint32_t textureObject =
        MemoryInline::ReadResolved32(glyphBytes, 12u, glyph + 12u);
    const uint32_t previousTextureObject = MemoryInline::FlatRead32(drawer + 12u);
    SetCRResident(ctx->cr, ctx->xer, 0, textureObject, previousTextureObject);
    const bool textureLoaded = textureObject != previousTextureObject;
    if (textureLoaded) {
        MemoryInline::FlatWrite32(drawer + 12u, textureObject);
        ctx->lr = 0x805CF5F4u;
        GX__LoadTexObj_80170f2c(textureObject, 0u);
    }

    // The translated function sets LR to the instruction after GXBegin (0x805CF604).
    // Keep that exact caller PC so guest hot-call attribution stays faithful.
    ctx->lr = 0x805CF604u;
    GX__Begin_8016f0f0(GX_QUADS, GX_VTXFMT0, 4u);

    const int32_t y0 = static_cast<int16_t>(
        MemoryInline::ReadResolved16(glyphBytes, 4u, glyph + 4u));
    const int32_t y1 = static_cast<int16_t>(
        MemoryInline::ReadResolved16(glyphBytes, 6u, glyph + 6u));
    const uint16_t s0 = MemoryInline::ReadResolved16(glyphBytes, 16u, glyph + 16u);
    const uint16_t s1 = MemoryInline::ReadResolved16(glyphBytes, 18u, glyph + 18u);
    const uint16_t t0 = MemoryInline::ReadResolved16(glyphBytes, 20u, glyph + 20u);
    const uint16_t t1 = MemoryInline::ReadResolved16(glyphBytes, 22u, glyph + 22u);

    std::array<uint8_t, 32> fifo{};
    const std::array<uint16_t, 16> words{
        static_cast<uint16_t>(y0), static_cast<uint16_t>(x0), s0, t0,
        static_cast<uint16_t>(y1), static_cast<uint16_t>(x0), s1, t0,
        static_cast<uint16_t>(y1), static_cast<uint16_t>(x1), s1, t1,
        static_cast<uint16_t>(y0), static_cast<uint16_t>(x1), s0, t1,
    };
    for (size_t i = 0; i < words.size(); ++i) {
        StoreBe16(fifo.data() + i * 2u, words[i]);
    }
    GX_HLE_FIFO_WriteBurst(fifo.data(), static_cast<uint32_t>(fifo.size()));
    GX_HLE_RecordGlyphFast(setupRequired, textureLoaded);

    // Match the volatile register image left by the translated implementation.
    ctx->gpr[0] = savedLr;
    ctx->gpr[3] = static_cast<uint32_t>(y1);
    ctx->gpr[4] = s0;
    ctx->gpr[5] = 0xCC000000u;
    ctx->gpr[6] = static_cast<uint32_t>(y0);
    ctx->gpr[7] = static_cast<uint32_t>(x1);
    ctx->gpr[8] = t0;
    ctx->gpr[9] = t1;
    ctx->lr = savedLr;
}

} // namespace

PPC_NATIVE_OVERRIDE_VOID(805CF598, Text__GlyphDrawer__Draw_HLE_805cf598,
                         (CpuContext* ctx), (ctx));
