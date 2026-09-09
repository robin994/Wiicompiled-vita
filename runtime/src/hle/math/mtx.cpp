#include "hle_stubs.h"
#include "isa/big_endian.h"
#include "memory.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr size_t kMtxFloats = 12;
constexpr size_t kMtxBytes = kMtxFloats * sizeof(uint32_t);
constexpr size_t kVecFloats = 3;
constexpr size_t kVecBytes = kVecFloats * sizeof(uint32_t);

float ReadBeFloat(const uint8_t* p) noexcept {
    return std::bit_cast<float>(BigEndian::Read32(p));
}

void WriteBeFloat(uint8_t* p, float value) noexcept {
    BigEndian::Write32(p, std::bit_cast<uint32_t>(value));
}

template <size_t Count>
std::array<float, Count> LoadFloats(uint32_t address) {
    const uint8_t* p = Memory::GetPointer(address, Count * sizeof(uint32_t));
    std::array<float, Count> out{};
    for (size_t i = 0; i < Count; ++i) out[i] = ReadBeFloat(p + i * sizeof(uint32_t));
    return out;
}

template <size_t Count>
void StoreFloats(uint32_t address, const std::array<float, Count>& values) {
    uint8_t* p = Memory::GetPointer(address, Count * sizeof(uint32_t));
    for (size_t i = 0; i < Count; ++i) WriteBeFloat(p + i * sizeof(uint32_t), values[i]);
}

std::array<float, kMtxFloats> ConcatMtx(const std::array<float, kMtxFloats>& a,
                                       const std::array<float, kMtxFloats>& b) noexcept {
    std::array<float, kMtxFloats> out{};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t col = 0; col < 3; ++col) {
            out[row * 4 + col] =
                a[row * 4 + 0] * b[0 * 4 + col] +
                a[row * 4 + 1] * b[1 * 4 + col] +
                a[row * 4 + 2] * b[2 * 4 + col];
        }
        out[row * 4 + 3] =
            a[row * 4 + 0] * b[0 * 4 + 3] +
            a[row * 4 + 1] * b[1 * 4 + 3] +
            a[row * 4 + 2] * b[2 * 4 + 3] +
            a[row * 4 + 3];
    }
    return out;
}

bool InvertMtx(const std::array<float, kMtxFloats>& m,
               std::array<float, kMtxFloats>& out) noexcept {
    const float a00 = m[0], a01 = m[1], a02 = m[2];
    const float a10 = m[4], a11 = m[5], a12 = m[6];
    const float a20 = m[8], a21 = m[9], a22 = m[10];
    const float det =
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
    if (det == 0.0f) return false;
    const float invDet = 1.0f / det;

    out[0] = (a11 * a22 - a12 * a21) * invDet;
    out[1] = (a02 * a21 - a01 * a22) * invDet;
    out[2] = (a01 * a12 - a02 * a11) * invDet;
    out[4] = (a12 * a20 - a10 * a22) * invDet;
    out[5] = (a00 * a22 - a02 * a20) * invDet;
    out[6] = (a02 * a10 - a00 * a12) * invDet;
    out[8] = (a10 * a21 - a11 * a20) * invDet;
    out[9] = (a01 * a20 - a00 * a21) * invDet;
    out[10] = (a00 * a11 - a01 * a10) * invDet;

    const float tx = m[3], ty = m[7], tz = m[11];
    out[3] = -(out[0] * tx + out[1] * ty + out[2] * tz);
    out[7] = -(out[4] * tx + out[5] * ty + out[6] * tz);
    out[11] = -(out[8] * tx + out[9] * ty + out[10] * tz);
    return true;
}

} // namespace

// High-confidence, state-free Revolution SDK MTX/VEC leaf routines. These are
// intentionally ordinary IEEE float operations: no fast-math and no approximate
// reciprocal/rsqrt. Besides removing paired-single emulation overhead, keeping
// these routines native gives the producer profiler clean HLE boundaries.

extern "C" void MTX__PSMTXIdentity_80199d04(uint32_t dst) {
    const std::array<float, kMtxFloats> identity = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
    };
    StoreFloats(dst, identity);
}
PPC_NATIVE_OVERRIDE_VOID(80199d04, MTX__PSMTXIdentity_80199d04, (uint32_t dst), (dst));

extern "C" void MTX__PSMTXCopy_80199d30(uint32_t src, uint32_t dst) {
    const void* source = Memory::GetPointer(src, kMtxBytes);
    void* destination = Memory::GetPointer(dst, kMtxBytes);
    std::memmove(destination, source, kMtxBytes);
}
PPC_NATIVE_OVERRIDE_VOID(80199d30, MTX__PSMTXCopy_80199d30, (uint32_t src, uint32_t dst), (src, dst));

extern "C" void MTX__PSMTXConcat_80199d64(uint32_t aAddr, uint32_t bAddr, uint32_t outAddr) {
    const auto a = LoadFloats<kMtxFloats>(aAddr);
    const auto b = LoadFloats<kMtxFloats>(bAddr);
    StoreFloats(outAddr, ConcatMtx(a, b));
}
PPC_NATIVE_OVERRIDE_VOID(80199d64, MTX__PSMTXConcat_80199d64,
                         (uint32_t a, uint32_t b, uint32_t out), (a, b, out));

extern "C" void MTX__PSMTXConcatArray_80199e30(uint32_t aAddr, uint32_t srcBase,
                                                uint32_t dstBase, uint32_t count) {
    const auto a = LoadFloats<kMtxFloats>(aAddr);
    for (uint32_t i = 0; i < count; ++i) {
        const auto b = LoadFloats<kMtxFloats>(srcBase + i * kMtxBytes);
        StoreFloats(dstBase + i * kMtxBytes, ConcatMtx(a, b));
    }
}
PPC_NATIVE_OVERRIDE_VOID(80199e30, MTX__PSMTXConcatArray_80199e30,
                         (uint32_t a, uint32_t src, uint32_t dst, uint32_t count),
                         (a, src, dst, count));

extern "C" uint32_t MTX__PSMTXInverse_80199fc8(uint32_t src, uint32_t dst) {
    const auto m = LoadFloats<kMtxFloats>(src);
    std::array<float, kMtxFloats> inverse{};
    if (!InvertMtx(m, inverse)) return 0;
    StoreFloats(dst, inverse);
    return 1;
}
PPC_NATIVE_OVERRIDE(80199fc8, MTX__PSMTXInverse_80199fc8, uint32_t,
                    (uint32_t src, uint32_t dst), (src, dst));

extern "C" uint32_t MTX__PSMTXInvXpose_8019a0c0(uint32_t src, uint32_t dst) {
    const auto m = LoadFloats<kMtxFloats>(src);
    std::array<float, kMtxFloats> inverse{};
    if (!InvertMtx(m, inverse)) return 0;
    const std::array<float, kMtxFloats> invXpose = {
        inverse[0], inverse[4], inverse[8], 0.f,
        inverse[1], inverse[5], inverse[9], 0.f,
        inverse[2], inverse[6], inverse[10], 0.f,
    };
    StoreFloats(dst, invXpose);
    return 1;
}
PPC_NATIVE_OVERRIDE(8019a0c0, MTX__PSMTXInvXpose_8019a0c0, uint32_t,
                    (uint32_t src, uint32_t dst), (src, dst));

extern "C" void MTX__PSMTXTrans_8019a3e0(uint32_t dst, float x, float y, float z) {
    const std::array<float, kMtxFloats> out = {
        1.f, 0.f, 0.f, x,
        0.f, 1.f, 0.f, y,
        0.f, 0.f, 1.f, z,
    };
    StoreFloats(dst, out);
}
PPC_NATIVE_OVERRIDE_VOID(8019a3e0, MTX__PSMTXTrans_8019a3e0,
                         (uint32_t dst, float x, float y, float z), (dst, x, y, z));

extern "C" void MTX__PSMTXTransApply_8019a414(uint32_t src, uint32_t dst,
                                               float x, float y, float z) {
    auto m = LoadFloats<kMtxFloats>(src);
    m[3] += x;
    m[7] += y;
    m[11] += z;
    StoreFloats(dst, m);
}
PPC_NATIVE_OVERRIDE_VOID(8019a414, MTX__PSMTXTransApply_8019a414,
                         (uint32_t src, uint32_t dst, float x, float y, float z),
                         (src, dst, x, y, z));

extern "C" void MTX__PSMTXScale_8019a460(uint32_t dst, float x, float y, float z) {
    const std::array<float, kMtxFloats> out = {
        x, 0.f, 0.f, 0.f,
        0.f, y, 0.f, 0.f,
        0.f, 0.f, z, 0.f,
    };
    StoreFloats(dst, out);
}
PPC_NATIVE_OVERRIDE_VOID(8019a460, MTX__PSMTXScale_8019a460,
                         (uint32_t dst, float x, float y, float z), (dst, x, y, z));

extern "C" void MTX__PSMTXScaleApply_8019a488(uint32_t src, uint32_t dst,
                                               float x, float y, float z) {
    const auto m = LoadFloats<kMtxFloats>(src);
    std::array<float, kMtxFloats> out{};
    for (size_t col = 0; col < 4; ++col) {
        out[col] = m[col] * x;
        out[4 + col] = m[4 + col] * y;
        out[8 + col] = m[8 + col] * z;
    }
    StoreFloats(dst, out);
}
PPC_NATIVE_OVERRIDE_VOID(8019a488, MTX__PSMTXScaleApply_8019a488,
                         (uint32_t src, uint32_t dst, float x, float y, float z),
                         (src, dst, x, y, z));

extern "C" void MTX__PSMTXMultVec_8019a91c(uint32_t mAddr, uint32_t srcAddr,
                                            uint32_t dstAddr) {
    const auto m = LoadFloats<kMtxFloats>(mAddr);
    const auto v = LoadFloats<kVecFloats>(srcAddr);
    const std::array<float, kVecFloats> out = {
        m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3],
        m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7],
        m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11],
    };
    StoreFloats(dstAddr, out);
}
PPC_NATIVE_OVERRIDE_VOID(8019a91c, MTX__PSMTXMultVec_8019a91c,
                         (uint32_t m, uint32_t src, uint32_t dst), (m, src, dst));

extern "C" void MTX__PSMTXMultVecSR_8019a970(uint32_t mAddr, uint32_t srcAddr,
                                              uint32_t dstAddr) {
    const auto m = LoadFloats<kMtxFloats>(mAddr);
    const auto v = LoadFloats<kVecFloats>(srcAddr);
    const std::array<float, kVecFloats> out = {
        m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
        m[4] * v[0] + m[5] * v[1] + m[6] * v[2],
        m[8] * v[0] + m[9] * v[1] + m[10] * v[2],
    };
    StoreFloats(dstAddr, out);
}
PPC_NATIVE_OVERRIDE_VOID(8019a970, MTX__PSMTXMultVecSR_8019a970,
                         (uint32_t m, uint32_t src, uint32_t dst), (m, src, dst));

extern "C" void MTX__PSVECAdd_8019abe4(uint32_t aAddr, uint32_t bAddr, uint32_t outAddr) {
    const auto a = LoadFloats<kVecFloats>(aAddr);
    const auto b = LoadFloats<kVecFloats>(bAddr);
    StoreFloats(outAddr, std::array<float, kVecFloats>{a[0] + b[0], a[1] + b[1], a[2] + b[2]});
}
PPC_NATIVE_OVERRIDE_VOID(8019abe4, MTX__PSVECAdd_8019abe4,
                         (uint32_t a, uint32_t b, uint32_t out), (a, b, out));

extern "C" void MTX__PSVECScale_8019ac08(uint32_t srcAddr, uint32_t outAddr, float scale) {
    const auto v = LoadFloats<kVecFloats>(srcAddr);
    StoreFloats(outAddr, std::array<float, kVecFloats>{v[0] * scale, v[1] * scale, v[2] * scale});
}
PPC_NATIVE_OVERRIDE_VOID(8019ac08, MTX__PSVECScale_8019ac08,
                         (uint32_t src, uint32_t out, float scale), (src, out, scale));

extern "C" void MTX__PSVECCrossProduct_8019accc(uint32_t aAddr, uint32_t bAddr,
                                                 uint32_t outAddr) {
    const auto a = LoadFloats<kVecFloats>(aAddr);
    const auto b = LoadFloats<kVecFloats>(bAddr);
    const std::array<float, kVecFloats> out = {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
    StoreFloats(outAddr, out);
}
PPC_NATIVE_OVERRIDE_VOID(8019accc, MTX__PSVECCrossProduct_8019accc,
                         (uint32_t a, uint32_t b, uint32_t out), (a, b, out));
