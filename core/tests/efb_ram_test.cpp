#include "gfx/efb_ram_encoder.hpp"
#include "vita_efb_copy.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value) { if(!value)throw std::runtime_error("EFB RAM encoding contract failed"); }
}
int main() {
    try {
        using namespace aurora::gfx::efb_ram;
        using namespace aurora::vita::gfx;
        constexpr uint32_t width=9,height=5;
        std::vector<uint8_t> pixels(width*height*4);
        for(size_t i=0;i<pixels.size();++i)pixels[i]=uint8_t(i*13+5);
        std::vector<uint8_t> tiled(encoded_size(GX_TF_RGBA8,width,height));
        check(encode(tiled.data(),tiled.size(),GX_TF_RGBA8,width,height,pixels.data(),width,height,width*4,HostPixelOrder::RGBA));
        // Check GX AR/GB planes, tile boundaries and clamped padding independently.
        for(uint32_t y=0;y<8;++y)for(uint32_t x=0;x<12;++x) {
            const size_t tile=((y/4)*3+x/4)*64;
            const size_t lane=((y%4)*4+x%4)*2;
            const size_t source=(size_t(std::min(y,height-1))*width+std::min(x,width-1))*4;
            check(tiled[tile+lane]==pixels[source+3] && tiled[tile+lane+1]==pixels[source]);
            check(tiled[tile+32+lane]==pixels[source+1] && tiled[tile+33+lane]==pixels[source+2]);
        }
        check(!encode(tiled.data(),tiled.size()-1,GX_TF_RGBA8,width,height,pixels.data(),width,height,width*4,HostPixelOrder::RGBA));
        std::vector<uint8_t> converted;
        check(copy_efb_rgba8(pixels.data(),width,height,{0,0,int(width),int(height)},width,height,EfbCopyFormat::I8,false,false,converted));
        for(size_t i=0;i<pixels.size();i+=4) {
            const auto intensity=uint8_t(std::clamp(std::floor(.257f*pixels[i]+.504f*pixels[i+1]+.098f*pixels[i+2]+16+.5f),0.f,255.f));
            check(converted[i]==intensity && converted[i+3]==intensity);
        }
        tiled.resize(encoded_size(GX_TF_I8,width,height));
        check(encode(tiled.data(),tiled.size(),GX_TF_I8,width,height,converted.data(),width,height,width*4,HostPixelOrder::RGBA));
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)
            check(tiled[((y/4)*2+x/8)*32+(y%4)*8+x%8]==converted[(size_t(y)*width+x)*4]);
        check(!copy_efb_rgba8(pixels.data(),width,height,{0,0,int(width),int(height)},width,height,EfbCopyFormat::DepthZ16,false,false,converted));
        check(!copy_efb_rgba8(pixels.data(),width,height,{1,0,int(width),int(height)},width,height,EfbCopyFormat::I8,false,false,converted));
        std::cout << "EFB RAM contracts: GX tiled color, padding, conversion and rejected depth PASS\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
