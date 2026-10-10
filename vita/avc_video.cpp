#include "avc_video.hpp"
#include <cstring>

namespace wic::avc {
namespace {
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3]; }
size_t StartCode(std::span<const uint8_t> b, size_t i) {
    if (i+3<=b.size() && b[i]==0 && b[i+1]==0 && b[i+2]==1) return 3;
    if (i+4<=b.size() && b[i]==0 && b[i+1]==0 && b[i+2]==0 && b[i+3]==1) return 4;
    return 0;
}
}
bool Parse(std::span<const uint8_t> input, Component& output, std::string& error) {
    output={};
    if (input.size()<HeaderBytes || input.size()>MaxComponentBytes ||
        std::memcmp(input.data(),"WAVC",4) || Be32(input.data()+4)!=1 || Be32(input.data()+20)!=1) {
        error="invalid WAVC v1 header"; return false;
    }
    const auto w=Be32(input.data()+8), h=Be32(input.data()+12), bytes=Be32(input.data()+16);
    if (!w || !h || w>704 || h>768 || w%16 || h%16 || !bytes ||
        bytes>input.size()-HeaderBytes || input.size()-HeaderBytes-bytes>3) {
        error="invalid AVC geometry/extent"; return false;
    }
    for (size_t i=HeaderBytes+bytes;i<input.size();++i) if(input[i]) {
        error="invalid AVC padding"; return false;
    }
    auto es=input.subspan(HeaderBytes,bytes);
    bool sps=false,pps=false,idr=false,aud=false;
    for (size_t at=0;at<es.size();) {
        const auto prefix=StartCode(es,at);
        if (!prefix || at+prefix>=es.size()) { error="invalid Annex B NAL"; return false; }
        const auto nal=at+prefix;
        const auto type=es[nal]&31;
        if (es[nal]&128 || type==1 || type==2 || type==3 || type==4) {
            error="AVC component requires independent IDR pictures"; return false;
        }
        if (!at && type!=9) { error="missing AVC access unit delimiter"; return false; }
        if(type==7) {
            if(nal+1>=es.size() || es[nal+1]!=66) { error="AVC Baseline required"; return false; }
            sps=true;
        }
        pps|=type==8; idr|=type==5; aud|=type==9;
        size_t next=nal+1;
        while(next<es.size() && !StartCode(es,next)) ++next;
        at=next;
    }
    if(!sps || !pps || !idr || !aud) { error="incomplete AVC access unit"; return false; }
    output={w,h,es}; error.clear(); return true;
}

bool CopyNv12(std::span<const uint8_t> y, std::span<const uint8_t> uv,
              unsigned pitch, unsigned w, unsigned h, wic::thp::Image& out, std::string& error) {
    if(!w || !h || w>704 || h>768 || w%16 || h%16 || pitch<w || pitch>4096 ||
        size_t(pitch)*h>y.size() || size_t(pitch)*(h/2)>uv.size()) {
        error="invalid decoded NV12 extent"; return false;
    }
    out.planes[0].resize(size_t(w)*h);
    out.planes[1].resize(size_t(w/2)*(h/2)); out.planes[2].resize(out.planes[1].size());
    for(unsigned row=0;row<h;++row) std::memcpy(out.planes[0].data()+size_t(row)*w,y.data()+size_t(row)*pitch,w);
    for(unsigned row=0;row<h/2;++row) for(unsigned col=0;col<w/2;++col) {
        out.planes[1][size_t(row)*(w/2)+col]=uv[size_t(row)*pitch+2*col];
        out.planes[2][size_t(row)*(w/2)+col]=uv[size_t(row)*pitch+2*col+1];
    }
    out.width=w; out.height=h; error.clear(); return true;
}
}
