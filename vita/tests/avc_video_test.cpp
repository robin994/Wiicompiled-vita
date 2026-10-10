#include "avc_video.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <limits>

void Check(bool value) { if(!value) { std::fprintf(stderr,"AVC contract failed\n"); std::abort(); } }
void Put(std::vector<uint8_t>& b,size_t at,uint32_t n) {
    for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(n>>(24-i*8));
}
int main() {
    const std::vector<uint8_t> es={0,0,0,1,9,16,0,0,1,7,66,0,32,0,0,1,8,128,0,0,1,5,128};
    std::vector<uint8_t> b(24+es.size()); std::memcpy(b.data(),"WAVC",4);
    Put(b,4,1);Put(b,8,672);Put(b,12,752);Put(b,16,es.size());Put(b,20,1);
    std::copy(es.begin(),es.end(),b.begin()+24);
    wic::avc::Component component;std::string error;
    Check(wic::avc::Parse(b,component,error) && component.width==672 && component.height==752 && component.elementary.size()==es.size());
    auto bad=b;Put(bad,16,UINT32_MAX);Check(!wic::avc::Parse(bad,component,error));
    bad=b;Put(bad,8,720);Check(!wic::avc::Parse(bad,component,error));
    bad=b;Put(bad,12,760);Check(!wic::avc::Parse(bad,component,error));
    bad=b;Put(bad,4,2);Check(!wic::avc::Parse(bad,component,error));
    bad=b;bad[24+21]=1;Check(!wic::avc::Parse(bad,component,error));
    bad=b;bad[24+10]=100;Check(!wic::avc::Parse(bad,component,error));
    for(size_t end=0;end<b.size();++end) Check(!wic::avc::Parse(std::span(b).first(end),component,error));
    bad=b;bad.push_back(1);Check(!wic::avc::Parse(bad,component,error));
    bad=b;bad.push_back(0);Check(wic::avc::Parse(bad,component,error));
    constexpr unsigned pitch=704,w=672,h=752;
    std::vector<uint8_t> y(size_t(pitch)*768,231),uv(size_t(pitch)*384,232);
    for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col)y[size_t(row)*pitch+col]=uint8_t(row+col);
    for(unsigned row=0;row<h/2;++row)for(unsigned col=0;col<w/2;++col) {
        uv[size_t(row)*pitch+2*col]=uint8_t(row);uv[size_t(row)*pitch+2*col+1]=uint8_t(col);
    }
    wic::thp::Image image;
    Check(wic::avc::CopyNv12(y,uv,pitch,w,h,image,error));
    Check(image.width==w && image.height==h && image.planes[0].size()==size_t(w)*h);
    for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col)Check(image.planes[0][size_t(row)*w+col]==uint8_t(row+col));
    for(unsigned row=0;row<h/2;++row)for(unsigned col=0;col<w/2;++col) {
        Check(image.planes[1][size_t(row)*(w/2)+col]==uint8_t(row));
        Check(image.planes[2][size_t(row)*(w/2)+col]==uint8_t(col));
    }
    Check(!wic::avc::CopyNv12(std::span(y).first(size_t(pitch)*h-1),uv,pitch,w,h,image,error));
    Check(!wic::avc::CopyNv12(y,std::span(uv).first(size_t(pitch)*(h/2)-1),pitch,w,h,image,error));
    Check(!wic::avc::CopyNv12(y,uv,w-1,w,h,image,error));
}
