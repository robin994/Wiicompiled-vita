#include "aurora-main/platforms/vita/gfx/vita_efb_resample.hpp"
#include "wiicompiled_vita/frame_optimization_policy.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include <array>
#include <random>

using aurora::vita::gfx::resample_efb_rgba;
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
            if(!queue.empty() && op.type!=2 && queue.back().type!=2 &&
                WiiCompiledVita::CanReplaceEfbCommand(queue.back().boundary==op.boundary,
                    queue.back().dest==op.dest,queue.back().clear,queue.back().type==0,op.type==1)) {
                queue.back()=op; ++merged;
            } else queue.push_back(op);
            if(op.type==2) ++boundary;
        }
        for(const Op& op:queue) optimized.apply(op);
        assert(reference.observations==optimized.observations);
        assert(reference.surface==optimized.surface && reference.texture==optimized.texture);
    }
    assert(merged>0);
    std::printf("PASS: strided EFB resampling; 1,000,000 FIFO operations (%u coalesced)\n",merged);
}
