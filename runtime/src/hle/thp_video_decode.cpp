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

#ifndef MKW_VITA_THP_UNESCAPED_FIX
#define MKW_VITA_THP_UNESCAPED_FIX 0
#endif

constexpr uint32_t kThpFrameScanCap = 768u * 1024u;
constexpr uint32_t kThpMaxDimension = 2048u;

constexpr uint32_t kThpErrNone = 0u;
constexpr uint32_t kThpErrBadStream = 3u;
constexpr uint32_t kThpErrNullArg = 25u;
constexpr uint32_t kThpErrDecode = 11u;

thread_local tjhandle t_tj = nullptr;
thread_local std::vector<uint8_t> t_plane[3];
#if MKW_VITA_THP_UNESCAPED_FIX
thread_local std::vector<uint8_t> t_restuffedJpeg;
#endif

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

#if MKW_VITA_THP_UNESCAPED_FIX
bool FindSosScanStart(const uint8_t* data, uint32_t span, uint32_t& scanStart) noexcept {
    if (!data || span < 4u || data[0] != 0xFFu || data[1] != 0xD8u) return false;
    uint32_t p = 2u;
    while (p + 3u < span) {
        while (p < span && data[p] != 0xFFu) ++p;
        while (p < span && data[p] == 0xFFu) ++p;
        if (p >= span) return false;
        const uint8_t marker = data[p++];
        if (marker == 0xD9u) return false;
        if (marker == 0xD8u || marker == 0x01u || (marker >= 0xD0u && marker <= 0xD7u)) {
            continue;
        }
        if (p + 1u >= span) return false;
        const uint32_t length = (static_cast<uint32_t>(data[p]) << 8u) | data[p + 1u];
        if (length < 2u || p + length > span) return false;
        if (marker == 0xDAu) {
            scanStart = p + length;
            return scanStart < span;
        }
        p += length;
    }
    return false;
}

// Nintendo THP feeds its MJPEG entropy stream to the SDK decoder already
// unescaped. libjpeg-turbo expects standard JPEG byte stuffing, so on the
// fallback path convert every entropy 0xFF to 0xFF 0x00. A raw 0xFF 0xD9 can
// therefore occur inside THP entropy; try successive EOI candidates until the
// JPEG decoder reports a complete image rather than trusting the first pair.
bool TryDecodeRestuffedThp(tjhandle tj, const uint8_t* src, uint32_t span,
                           unsigned char* planes[3], int width, int strides[3], int height,
                           uint32_t& decodedSourceBytes, uint32_t& candidateOrdinal) {
    uint32_t scanStart = 0;
    if (!FindSosScanStart(src, span, scanStart)) return false;

    constexpr uint32_t kMaxEoiCandidates = 64u;
    uint32_t candidate = 0;
    for (uint32_t eoi = scanStart; eoi + 1u < span && candidate < kMaxEoiCandidates; ++eoi) {
        if (src[eoi] != 0xFFu || src[eoi + 1u] != 0xD9u) continue;
        ++candidate;

        t_restuffedJpeg.clear();
        t_restuffedJpeg.reserve(static_cast<size_t>(scanStart) +
                                static_cast<size_t>(eoi - scanStart) * 2u + 2u);
        t_restuffedJpeg.insert(t_restuffedJpeg.end(), src, src + scanStart);
        for (uint32_t p = scanStart; p < eoi; ++p) {
            const uint8_t value = src[p];
            t_restuffedJpeg.push_back(value);
            if (value == 0xFFu) t_restuffedJpeg.push_back(0x00u);
        }
        t_restuffedJpeg.push_back(0xFFu);
        t_restuffedJpeg.push_back(0xD9u);

        if (tjDecompressToYUVPlanes(tj, t_restuffedJpeg.data(), t_restuffedJpeg.size(),
                                    planes, width, strides, height, TJFLAG_FASTDCT) == 0) {
            decodedSourceBytes = eoi + 2u;
            candidateOrdinal = candidate;
            return true;
        }
    }
    candidateOrdinal = candidate;
    return false;
}
#endif

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
    static thread_local uint64_t calls=0, failures=0;
    const char* phase="arguments";
    const uint64_t call=++calls;
    if (call<=2) RT_LOGF(RT_TAG_HLE,"thp: decode_enter n=%llu src=0x%08X\n",
        static_cast<unsigned long long>(call),ctx->gpr[3]);
    struct TraceResult {
        CpuContext* ctx; const char*& phase; uint64_t& failures;
        ~TraceResult() {
            if (ctx->gpr[3]==kThpErrNone) return;
            const uint64_t n=++failures;
            if (n<=4 || (n&(n-1))==0)
                RT_LOGF(RT_TAG_HLE,"thp: decode_error n=%llu phase=%s code=%u\n",
                    static_cast<unsigned long long>(n),phase,ctx->gpr[3]);
        }
    } trace{ctx,phase,failures};
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
    phase="jpeg_header";
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

    // THP is Motion-JPEG and its frames are transient. FASTDCT is materially
    // cheaper on the Vita's Cortex-A9 while remaining more than adequate for a
    // 960x544 display; avoid spending guest-core time on the accurate DCT path.
    phase="jpeg_pixels";
    bool decoded = tjDecompressToYUVPlanes(tj, src, jpegSize, planes, width, strides, height,
                                           TJFLAG_FASTDCT) == 0;
#if MKW_VITA_THP_UNESCAPED_FIX
    if (!decoded) {
        static thread_local uint64_t restuffAttempts = 0;
        static thread_local uint64_t restuffSuccesses = 0;
        const uint64_t attempt = ++restuffAttempts;
        if (attempt <= 4u || (attempt & (attempt - 1u)) == 0u) {
            RT_LOGF(RT_TAG_HLE,
                    "thp: turbojpeg_standard_fail n=%llu jpeg=%u span=%u error=%s\n",
                    static_cast<unsigned long long>(attempt), jpegSize, span,
                    tjGetErrorStr2(tj));
        }
        uint32_t decodedSourceBytes = 0;
        uint32_t candidateOrdinal = 0;
        decoded = TryDecodeRestuffedThp(tj, src, span, planes, width, strides, height,
                                        decodedSourceBytes, candidateOrdinal);
        if (decoded) {
            const uint64_t success = ++restuffSuccesses;
            if (success <= 8u || (success & (success - 1u)) == 0u) {
                RT_LOGF(RT_TAG_HLE,
                        "thp: restuffed_decode n=%llu %dx%d source=%u candidate=%u packed=%u\n",
                        static_cast<unsigned long long>(success), width, height,
                        decodedSourceBytes, candidateOrdinal,
                        static_cast<unsigned>(t_restuffedJpeg.size()));
            }
            jpegSize = decodedSourceBytes;
        } else if (attempt <= 4u || (attempt & (attempt - 1u)) == 0u) {
            RT_LOGF(RT_TAG_HLE,
                    "thp: restuffed_fail n=%llu candidates=%u error=%s\n",
                    static_cast<unsigned long long>(attempt), candidateOrdinal,
                    tjGetErrorStr2(tj));
        }
    }
#endif
    if (!decoded) {
        ctx->gpr[3] = kThpErrDecode;
        return;
    }

    const uint32_t dstAddr[3] = {dstYAddr, dstUAddr, dstVAddr};
    phase="guest_planes";
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
