// Host-only lossless DSP ADPCM -> signed PCM16 little-endian preparation.
// Runtime keeps the guest accelerator address, loop and resampling semantics.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
namespace fs=std::filesystem;
uint32_t word(const uint8_t* p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
int16_t sample(const uint8_t* p) {return static_cast<int16_t>(uint16_t(p[0])|(uint16_t(p[1])<<8));}
void put16(std::vector<uint8_t>& v,int16_t s) {v.push_back(uint16_t(s)&255);v.push_back(uint16_t(s)>>8);}
uint64_t hash(const uint8_t* p,size_t n) {uint64_t h=14695981039346656037ull;for(size_t i=0;i<n;++i)h=(h^p[i])*1099511628211ull;return h;}
int main(int argc,char** argv) {
    if(argc!=3){std::cerr<<"usage: wic-compile-audio REQUEST_DIRECTORY OUTPUT_ROOT\n";return 2;}
    size_t count=0;
    for(const auto& file:fs::directory_iterator(argv[1])) {
        if(file.path().extension()!=".awq")continue;
        std::ifstream in(file.path(),std::ios::binary);
        std::vector<uint8_t> request((std::istreambuf_iterator<char>(in)),{});
        if(request.size()<52||request.size()>64*1024*1024||std::string(reinterpret_cast<char*>(request.data()),8)!="WICAUR01")return 1;
        const size_t bytes=word(request.data()+8),samples=word(request.data()+12),frames=(samples+13)/14;
        if(!samples||bytes!=frames*8||request.size()!=52+bytes)return 1;
        int16_t y1=sample(request.data()+48),y2=sample(request.data()+50);
        for(size_t block=0;block<frames;block+=1024) {
            const size_t blockFrames=std::min(size_t(1024),frames-block);
            const size_t blockBytes=blockFrames*8;
            const size_t blockSamples=std::min(blockFrames*14,samples-block*14);
            std::vector<uint8_t> out(request.begin(),request.begin()+52);
            std::copy_n("WICPCM01",8,out.data());
            for(unsigned i=0;i<4;++i){out[8+i]=uint8_t(blockBytes>>(8*i));out[12+i]=uint8_t(blockSamples>>(8*i));}
            out[48]=uint16_t(y1)&255;out[49]=uint16_t(y1)>>8;out[50]=uint16_t(y2)&255;out[51]=uint16_t(y2)>>8;
            out.insert(out.end(),request.data()+52+block*8,request.data()+52+(block+blockFrames)*8);
            out.reserve(52+blockBytes+blockFrames*32+8);
            for(size_t frame=block;frame<block+blockFrames;++frame) {
                put16(out,y1);put16(out,y2);
                const auto* data=request.data()+52+frame*8;
                const unsigned predictor=(data[0]>>4)&7,scale=1u<<(data[0]&15);
                const int32_t c1=sample(request.data()+16+predictor*4),c2=sample(request.data()+18+predictor*4);
                for(unsigned i=0;i<14;++i) {
                    int32_t nibble=(i&1)?data[1+i/2]&15:data[1+i/2]>>4;
                    if(nibble>=8)nibble-=16;
                    const int32_t decoded=int32_t(scale)*nibble+((0x400+c1*y1+c2*y2)>>11);
                    // Match AX ClampAdpcmS16 exactly, including its asymmetric
                    // negative saturation. A different floor also changes all
                    // following predicted samples and block seek histories.
                    const int16_t pcm=static_cast<int16_t>(std::clamp(decoded,-32767,32767));
                    put16(out,pcm);y2=y1;y1=pcm;
                }
            }
            const auto checksum=hash(out.data(),out.size());for(unsigned i=0;i<8;++i)out.push_back(uint8_t(checksum>>(8*i)));
            const auto dest=fs::path(argv[2])/"native/v1/audio"/(file.path().stem().string()+"-"+std::to_string(block)+".awpc");
            fs::create_directories(dest.parent_path());
            if(fs::exists(dest)) {
                std::ifstream old(dest,std::ios::binary);std::vector<uint8_t> existing((std::istreambuf_iterator<char>(old)),{});
                if(existing!=out){std::cerr<<"audio content collision\n";return 1;}
            } else {
                std::ofstream f(dest,std::ios::binary);f.write(reinterpret_cast<const char*>(out.data()),out.size());if(!f)return 1;
            }
            ++count;
        }
    }
    std::cout<<"{\"compiled_audio_blocks\":"<<count<<"}\n";return 0;
}
