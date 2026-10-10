#include "thp_native.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <jpeglib.h>

void Require(bool condition) { if (!condition) std::abort(); }
std::vector<uint8_t> Encode(unsigned width, unsigned height) {
    jpeg_compress_struct c{}; jpeg_error_mgr error{};
    c.err=jpeg_std_error(&error); jpeg_create_compress(&c);
    unsigned char* data=nullptr; unsigned long size=0;
    jpeg_mem_dest(&c, &data, &size);
    c.image_width=width; c.image_height=height; c.input_components=3; c.in_color_space=JCS_RGB;
    jpeg_set_defaults(&c); jpeg_set_quality(&c, 85, TRUE); jpeg_start_compress(&c, TRUE);
    std::vector<uint8_t> row(size_t(width)*3);
    while (c.next_scanline<height) {
        for (unsigned x=0; x<width; ++x) {
            row[x*3]=uint8_t(x*7+c.next_scanline*3);
            row[x*3+1]=uint8_t(x*2+c.next_scanline*11);
            row[x*3+2]=uint8_t(x*17+c.next_scanline*5);
        }
        JSAMPROW p=row.data(); Require(jpeg_write_scanlines(&c, &p, 1)==1);
    }
    jpeg_finish_compress(&c); std::vector<uint8_t> result(data,data+size); std::free(data); jpeg_destroy_compress(&c);
    return result;
}
std::vector<uint8_t> Unstuff(const std::vector<uint8_t>& jpeg, unsigned padding) {
    size_t scan=2;
    while (jpeg[scan+1]!=0xda) scan+=2+unsigned(jpeg[scan+2])*256+jpeg[scan+3];
    scan+=2+unsigned(jpeg[scan+2])*256+jpeg[scan+3];
    std::vector<uint8_t> thp(jpeg.begin(), jpeg.begin()+scan);
    for (size_t i=scan; i<jpeg.size()-2; ++i) {
        thp.push_back(jpeg[i]);
        if (jpeg[i]==0xff) { Require(jpeg[i+1]==0); ++i; }
    }
    thp.push_back(0xff); thp.push_back(0xd9); thp.insert(thp.end(),padding,0); return thp;
}
void CheckTiles(const wic::thp::Image& image) {
    for (unsigned p=0; p<3; ++p) {
        const auto width=p?image.width/2:image.width;
        const auto height=p?image.height/2:image.height;
        const auto& input=image.planes[p]; const auto count=input.size()/32;
        std::vector<uint8_t> output(input.size()+64,0xa5);
        std::thread a([&] { wic::thp::TileRange(input.data(),output.data()+32,width,0,count/3); });
        std::thread b([&] { wic::thp::TileRange(input.data(),output.data()+32,width,count/3,2*count/3); });
        wic::thp::TileRange(input.data(),output.data()+32,width,2*count/3,count); a.join(); b.join();
        for (unsigned i=0;i<32;++i) Require(output[i]==0xa5 && output[output.size()-1-i]==0xa5);
        for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
            const size_t tile=size_t(y/4)*(width/8)+x/8;
            Require(output[32+tile*32+(y%4)*8+x%8]==input[size_t(y)*width+x]);
        }
    }
}
int main(int argc, char** argv) {
#if defined(WIC_THP_WRAPPER_TEST)
    extern void CheckWrapper();
    CheckWrapper();
#endif
    std::string error; wic::thp::Image image, reference;
    for (auto dimensions: {std::pair{32u,16u}, {608u,456u}, {512u,448u}, {640u,480u}}) {
        const auto jpeg=Encode(dimensions.first,dimensions.second);
        for (unsigned padding=0; padding<=3; ++padding) {
            auto thp=Unstuff(jpeg,padding); Require(wic::thp::Decode(thp,image,error));
            Require(image.width==dimensions.first && image.height==dimensions.second);
            if (!padding) reference=image;
            else Require(reference.planes==image.planes);
            CheckTiles(image);
            // Incomplete data must fail without terminating the process.
            thp.resize(thp.size()/2); Require(!wic::thp::Decode(thp,image,error));
        }
    }
    auto malformed=Unstuff(Encode(32,16),0); malformed[4]=0; malformed[5]=1;
    Require(!wic::thp::Decode(malformed,image,error));
    Require(!wic::thp::Decode({},image,error));
    // Optional copyrighted game samples remain ignored host-only fixtures.
    for (int i=1; i<argc; ++i) {
        std::ifstream file(argv[i],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        if (!wic::thp::Decode(bytes,image,error)) { std::fprintf(stderr,"%s: %s\n",argv[i],error.c_str()); return 1; }
        CheckTiles(image);
        std::ofstream output(std::string(argv[i])+".yuv",std::ios::binary);
        for (const auto& p:image.planes) output.write(reinterpret_cast<const char*>(p.data()),p.size());
        std::printf("%s: %ux%u decoded and GX I8 packing checked\n",argv[i],image.width,image.height);
    }
}
