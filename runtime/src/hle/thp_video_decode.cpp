#if defined(MKW_VITA_NATIVE_THP) && MKW_VITA_NATIVE_THP

#include "hle_stubs.h"
#include "memory.h"
#include "abi_bridge.h"
#include "ppc_runtime.h"
#include "runtime_log.h"

#include <turbojpeg.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kThpFrameScanCap = 768u * 1024u;
constexpr uint32_t kThpMaxDimension = 2048u;

constexpr uint32_t kThpErrNone = 0u;
constexpr uint32_t kThpErrBadStream = 3u;
constexpr uint32_t kThpErrNullArg = 25u;
constexpr uint32_t kThpErrDecode = 11u;

thread_local tjhandle t_tj = nullptr;
thread_local std::vector<uint8_t> t_plane[3];

uint64_t g_thpNativeFrames = 0;

tjhandle AcquireDecoder() noexcept {
    if (t_tj == nullptr) {
        t_tj = tjInitDecompress();
    }
    return t_tj;
}

uint32_t ContiguousSpan(uint32_t addr, uint32_t cap) noexcept {
    uint32_t span = cap;
    while (span > 4u && !Memory::Contains(addr, span)) {
        span /= 2u;
    }
    return Memory::Contains(addr, span) ? span : 0u;
}

uint32_t ScanJpegSize(const uint8_t* data, uint32_t span) noexcept {
    for (uint32_t i = 2u; i + 1u < span; ++i) {
        if (data[i] == 0xFFu && data[i + 1u] == 0xD9u) {
            return i + 2u;
        }
    }
    return 0u;
}

void TileI8Plane(const uint8_t* linear, uint32_t linStride, uint32_t width,
                 uint32_t height, uint8_t* dstTiled) noexcept {
    const uint32_t blocksPerRow = (width + 7u) / 8u;
    const uint32_t blockRows = (height + 3u) / 4u;
    for (uint32_t by = 0u; by < blockRows; ++by) {
        for (uint32_t bx = 0u; bx < blocksPerRow; ++bx) {
            uint8_t* block = dstTiled + (static_cast<size_t>(by) * blocksPerRow + bx) * 32u;
            for (uint32_t iy = 0u; iy < 4u; ++iy) {
                const uint32_t sy = by * 4u + iy;
                const uint32_t cy = sy < height ? sy : height - 1u;
                const uint8_t* srcRow = linear + static_cast<size_t>(cy) * linStride;
                for (uint32_t ix = 0u; ix < 8u; ++ix) {
                    const uint32_t sx = bx * 8u + ix;
                    const uint32_t cx = sx < width ? sx : width - 1u;
                    block[iy * 8u + ix] = srcRow[cx];
                }
            }
        }
    }
}

uint32_t TiledPlaneBytes(uint32_t width, uint32_t height) noexcept {
    return ((width + 7u) / 8u) * ((height + 3u) / 4u) * 32u;
}

void NativeThpVideoDecode(CpuContext* ctx) {
    const uint32_t srcAddr = ctx->gpr[3];
    const uint32_t dstYAddr = ctx->gpr[4];
    const uint32_t dstUAddr = ctx->gpr[5];
    const uint32_t dstVAddr = ctx->gpr[6];

    if (srcAddr == 0u || dstYAddr == 0u || dstUAddr == 0u || dstVAddr == 0u) {
        ctx->gpr[3] = kThpErrNullArg;
        return;
    }

    const uint32_t span = ContiguousSpan(srcAddr, kThpFrameScanCap);
    if (span < 4u) {
        ctx->gpr[3] = kThpErrBadStream;
        return;
    }

    const uint8_t* src = Memory::GetPointer(srcAddr, span);
    if (src == nullptr || src[0] != 0xFFu || src[1] != 0xD8u) {
        ctx->gpr[3] = kThpErrBadStream;
        return;
    }

    uint32_t jpegSize = ScanJpegSize(src, span);
    if (jpegSize == 0u) {
        jpegSize = span;
    }

    tjhandle tj = AcquireDecoder();
    if (tj == nullptr) {
        ctx->gpr[3] = kThpErrDecode;
        return;
    }

    int width = 0;
    int height = 0;
    int subsamp = 0;
    int colorspace = 0;
    if (tjDecompressHeader3(tj, src, jpegSize, &width, &height, &subsamp, &colorspace) != 0 ||
        width <= 0 || height <= 0 ||
        static_cast<uint32_t>(width) > kThpMaxDimension ||
        static_cast<uint32_t>(height) > kThpMaxDimension) {
        ctx->gpr[3] = kThpErrBadStream;
        return;
    }

    int strides[3];
    unsigned char* planes[3];
    uint32_t planeW[3];
    uint32_t planeH[3];
    for (int c = 0; c < 3; ++c) {
        planeW[c] = static_cast<uint32_t>(tjPlaneWidth(c, width, subsamp));
        planeH[c] = static_cast<uint32_t>(tjPlaneHeight(c, height, subsamp));
        strides[c] = static_cast<int>(planeW[c]);
        t_plane[c].resize(static_cast<size_t>(planeW[c]) * planeH[c]);
        planes[c] = t_plane[c].data();
    }

    if (tjDecompressToYUVPlanes(tj, src, jpegSize, planes, width, strides, height, 0) != 0) {
        ctx->gpr[3] = kThpErrDecode;
        return;
    }

    const uint32_t dstAddr[3] = {dstYAddr, dstUAddr, dstVAddr};
    for (int c = 0; c < 3; ++c) {
        const uint32_t need = TiledPlaneBytes(planeW[c], planeH[c]);
        uint8_t* dst = Memory::GetPointer(dstAddr[c], need);
        if (dst == nullptr) {
            ctx->gpr[3] = kThpErrDecode;
            return;
        }
        TileI8Plane(t_plane[c].data(), planeW[c], planeW[c], planeH[c], dst);
    }

    ++g_thpNativeFrames;
    if (g_thpNativeFrames <= 8u || (g_thpNativeFrames & (g_thpNativeFrames - 1u)) == 0u) {
        RT_LOGF(RT_TAG_HLE, "thp: native decode n=%llu %dx%d subsamp=%d jpeg=%u\n",
                static_cast<unsigned long long>(g_thpNativeFrames), width, height, subsamp,
                jpegSize);
    }

    ctx->gpr[3] = kThpErrNone;
}

}  // namespace

PPC_NATIVE_OVERRIDE_VOID(801B3BAC, NativeThpVideoDecode, (CpuContext* ctx), (ctx));

#endif  // MKW_VITA_NATIVE_THP
