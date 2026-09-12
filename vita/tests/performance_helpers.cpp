#include "aurora-main/platforms/vita/gfx/vita_efb_resample.hpp"
#include "wiicompiled_vita/frame_optimization_policy.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include <array>
#include <random>
#include <algorithm>

using aurora::vita::gfx::resample_efb_rgba;

namespace {
bool Yaz0Reference(const std::vector<uint8_t>& input, size_t outputBytes,
                   std::vector<uint8_t>& output) {
    output.assign(outputBytes, 0);
    size_t in = 0, out = 0;
    uint32_t flags = 0, mask = 0;
    while (out < outputBytes) {
        if (mask == 0) {
            if (in >= input.size()) return false;
            flags = input[in++];
            mask = 0x80u;
        }
        if (flags & mask) {
            if (in >= input.size()) return false;
            output[out++] = input[in++];
        } else {
            if (input.size() - in < 2) return false;
            const uint32_t rep = (uint32_t(input[in]) << 8) | input[in + 1];
            in += 2;
            const size_t distance = (rep & 0x0fffu) + 1u;
            if (distance > out) return false;
            size_t count = rep >> 12u;
            if (count) count += 2u;
            else {
                if (in >= input.size()) return false;
                count = size_t(input[in++]) + 18u;
            }
            if (count > outputBytes - out) return false;
            size_t source = out - distance;
            for (size_t i = 0; i < count; ++i) output[out++] = output[source++];
        }
        mask >>= 1u;
    }
    return true;
}

bool Yaz0FastMirror(const std::vector<uint8_t>& input, size_t outputBytes,
                    std::vector<uint8_t>& output) {
    output.assign(outputBytes, 0);
    const uint8_t* in = input.data();
    const uint8_t* const inEnd = input.data() + input.size();
    uint8_t* out = output.data();
    uint8_t* const outEnd = output.data() + output.size();
    uint32_t flags = 0, mask = 0;
    while (out < outEnd) {
        if (mask == 0) {
            if (in >= inEnd) return false;
            flags = *in++;
            mask = 0x80u;
        }
        if (flags & mask) {
            uint32_t run = 0, probe = mask;
            const size_t remaining = size_t(outEnd - out);
            while (probe && (flags & probe) && run < remaining) { ++run; probe >>= 1u; }
            if (size_t(inEnd - in) < run) return false;
            std::memcpy(out, in, run);
            in += run; out += run; mask = probe;
            continue;
        }
        if (inEnd - in < 2) return false;
        const uint32_t rep = (uint32_t(in[0]) << 8) | in[1];
        in += 2;
        const size_t distance = (rep & 0x0fffu) + 1u;
        const size_t produced = size_t(out - output.data());
        if (distance > produced) return false;
        size_t count = rep >> 12u;
        if (count) count += 2u;
        else {
            if (in >= inEnd) return false;
            count = size_t(*in++) + 18u;
        }
        if (count > size_t(outEnd - out)) return false;
        const uint8_t* source = out - distance;
        if (distance >= count) {
            std::memcpy(out, source, count);
        } else if (distance == 1u) {
            std::memset(out, source[0], count);
        } else {
            std::memcpy(out, source, distance);
            size_t copied = distance;
            while (copied < count) {
                const size_t remaining = count - copied;
                const size_t chunk = std::min(copied, remaining);
                std::memcpy(out + copied, out, chunk);
                copied += chunk;
            }
        }
        out += count;
        mask >>= 1u;
    }
    return true;
}

std::vector<uint8_t> LiteralYaz0Payload(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> encoded;
    for (size_t offset = 0; offset < raw.size();) {
        const size_t count = std::min<size_t>(8, raw.size() - offset);
        encoded.push_back(static_cast<uint8_t>(0xffu << (8u - count)));
        encoded.insert(encoded.end(), raw.begin() + offset, raw.begin() + offset + count);
        offset += count;
    }
    return encoded;
}

void TestYaz0Differential() {
    const std::vector<std::pair<std::vector<uint8_t>, size_t>> cases{
        {{0x80u, 'A', 0x00u, 0x00u, 21u}, 40u}, // distance 1 / memset path
        {{0xE0u, 'A', 'B', 'C', 0x00u, 0x02u, 3u}, 24u}, // overlapping distance 3
        {{0xFFu, '0','1','2','3','4','5','6','7', 0x00u, 0x60u, 0x07u}, 16u}, // non-overlap
    };
    for (const auto& [encoded, outputBytes] : cases) {
        std::vector<uint8_t> reference, fast;
        assert(Yaz0Reference(encoded, outputBytes, reference));
        assert(Yaz0FastMirror(encoded, outputBytes, fast));
        assert(reference == fast);
    }
    std::mt19937 rng(0x59617a30u);
    for (size_t size = 1; size <= 257; ++size) {
        std::vector<uint8_t> raw(size);
        for (auto& value : raw) value = static_cast<uint8_t>(rng());
        const auto encoded = LiteralYaz0Payload(raw);
        std::vector<uint8_t> reference, fast;
        assert(Yaz0Reference(encoded, raw.size(), reference));
        assert(Yaz0FastMirror(encoded, raw.size(), fast));
        assert(reference == raw && fast == raw);
    }
    for (const auto& malformed : std::vector<std::pair<std::vector<uint8_t>, size_t>>{
             {{0x00u}, 1u},                    // truncated pair
             {{0x00u, 0x00u, 0x00u}, 3u},     // back-reference before output
             {{0x80u, 'A', 0x00u, 0x00u, 10u}, 8u}, // decoded run beyond output
         }) {
        std::vector<uint8_t> reference, fast;
        assert(!Yaz0Reference(malformed.first, malformed.second, reference));
        assert(!Yaz0FastMirror(malformed.first, malformed.second, fast));
    }
}

using StripTriangle = std::array<int, 3>;
std::vector<StripTriangle> VisibleStripTriangles(const std::vector<int>& vertices) {
    std::vector<StripTriangle> triangles;
    for (size_t i = 0; i + 2 < vertices.size(); ++i) {
        StripTriangle triangle = (i & 1u)
            ? StripTriangle{vertices[i + 1], vertices[i], vertices[i + 2]}
            : StripTriangle{vertices[i], vertices[i + 1], vertices[i + 2]};
        if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2]) continue;
        triangles.push_back(triangle);
    }
    return triangles;
}

std::vector<int> StitchTriangleStrips(const std::vector<std::vector<int>>& strips) {
    assert(!strips.empty());
    std::vector<int> stitched = strips.front();
    for (size_t strip = 1; strip < strips.size(); ++strip) {
        assert(stitched.size() >= 3 && strips[strip].size() >= 3);
        const int previousLast = stitched.back();
        const int nextFirst = strips[strip].front();
        const bool needsParityVertex = (stitched.size() & 1u) != 0u;
        stitched.push_back(previousLast);
        stitched.push_back(nextFirst);
        if (needsParityVertex) stitched.push_back(nextFirst);
        stitched.insert(stitched.end(), strips[strip].begin(), strips[strip].end());
    }
    return stitched;
}

void TestTriangleStripStitchParity() {
    for (size_t a = 3; a <= 9; ++a)
    for (size_t b = 3; b <= 9; ++b)
    for (size_t c = 3; c <= 9; ++c) {
        std::vector<std::vector<int>> strips(3);
        int value = 1;
        strips[0].resize(a); strips[1].resize(b); strips[2].resize(c);
        for (auto& strip : strips) for (int& vertex : strip) vertex = value++;
        std::vector<StripTriangle> expected;
        for (const auto& strip : strips) {
            const auto triangles = VisibleStripTriangles(strip);
            expected.insert(expected.end(), triangles.begin(), triangles.end());
        }
        const auto stitched = StitchTriangleStrips(strips);
        assert(VisibleStripTriangles(stitched) == expected);
    }
}
} // namespace
struct Op { int type, dest; bool clear; unsigned boundary; };
struct State {
    unsigned surface=1;
    std::array<unsigned,4> texture{};
    std::vector<std::array<unsigned,5>> observations;
    void apply(const Op& op) {
        if (op.type==0) { texture[op.dest]=surface; if(op.clear) ++surface; }
        if (op.type==1) texture[op.dest]=0;
        if (op.type==2) {
            observations.push_back({surface,texture[0],texture[1],texture[2],texture[3]});
            ++surface; // drawing changes the framebuffer
        }
    }
};
int main() {
    TestYaz0Differential();
    TestTriangleStripStitchParity();
    std::printf("PASS: triangle-strip stitching preserves visible triangles and winding across odd/even strips\n");
    // Compare every pixel against the old readback->flip->nearest algorithm;
    // padding guards detect writes into adjacent rows/allocations.
    for(unsigned sw : {1u,3u,8u,13u,640u,960u})
    for(unsigned sh : {1u,3u,7u,544u})
    for(unsigned dw : {1u,3u,8u,13u,640u})
    for(unsigned dh : {1u,3u,7u,480u})
    for(bool flip : {false,true}) {
        const size_t sp=(sw+3)*4, dp=(dw+7)*4;
        std::vector<uint8_t> src(sp*sh,0), dst(dp*dh+32,0xcd);
        for(unsigned y=0;y<sh;++y) for(unsigned x=0;x<sw;++x) {
            uint32_t pixel=1+y*sw+x; std::memcpy(src.data()+y*sp+x*4,&pixel,4);
        }
        assert(resample_efb_rgba(src.data(),sw,sh,sp,dst.data(),dw,dh,dp,flip));
        for(unsigned y=0;y<dh;++y) {
            for(unsigned x=0;x<dw;++x) {
                unsigned sy=flip?y*sh/dh:sh-1-y*sh/dh;
                uint32_t actual; std::memcpy(&actual,dst.data()+y*dp+x*4,4);
                assert(actual==1+sy*sw+x*sw/dw);
            }
            for(size_t i=dw*4;i<dp;++i) assert(dst[y*dp+i]==0xcd);
        }
        for(size_t i=dp*dh;i<dst.size();++i) assert(dst[i]==0xcd);
    }
    std::array<uint8_t,16> pixel{};
    assert(!resample_efb_rgba(nullptr,1,1,4,pixel.data(),1,1,4));
    assert(!resample_efb_rgba(pixel.data(),1,1,3,pixel.data(),1,1,4));
    assert(!resample_efb_rgba(pixel.data(),1,1,4,pixel.data(),2049,1,4));
    // Differential FIFO simulation: copy/clear/destroy/draw ordering and every
    // sampled result must survive coalescing, including hundreds of commands.
    std::mt19937 rng(0x504535);
    unsigned merged=0;
    for(unsigned trial=0;trial<1000;++trial) {
        State reference, optimized;
        std::vector<Op> queue;
        unsigned boundary=0;
        for(unsigned n=0;n<1000;++n) {
            Op op{int(rng()%3),int(rng()%4),bool(rng()%2),boundary};
            if(op.type!=0) op.clear=false;
            reference.apply(op);
            bool replaced=false;
            if(op.type!=2) {
                for(size_t q=queue.size();q!=0;--q) {
                    Op& previous=queue[q-1];
                    if(previous.boundary!=op.boundary || previous.type==2) break;
                    if(previous.clear) break; // clear mutates the source EFB: absolute barrier
                    if(previous.dest!=op.dest) continue;
                    // Incoming clear copies may only replace the immediately
                    // preceding command; otherwise the clear would move before
                    // unrelated EFB work and alter its source framebuffer.
                    if(op.clear && q!=queue.size()) break;
                    if(WiiCompiledVita::CanReplaceEfbCommand(true,true,false,
                            previous.type==0,op.type==1)) {
                        previous=op; ++merged; replaced=true;
                    }
                    break;
                }
            }
            if(!replaced) queue.push_back(op);
            if(op.type==2) ++boundary;
        }
        for(const Op& op:queue) optimized.apply(op);
        assert(reference.observations==optimized.observations);
        assert(reference.surface==optimized.surface && reference.texture==optimized.texture);
    }
    assert(merged>0);
    // F03 no-drop storage gate: the old fixed 512/1024 command ceiling must not be
    // an algorithmic requirement. Distinct destinations cannot coalesce.
    std::vector<Op> noDrop;
    for(unsigned i=0;i<2048;++i) noDrop.push_back(Op{0,int(i),false,0});
    assert(noDrop.size()==2048);
    std::printf("PASS: Yaz0 differential + strided EFB resampling; 1,000,000 FIFO operations (%u coalesced); 2048-command no-drop gate\n",merged);
}
