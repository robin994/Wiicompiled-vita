#include "vita_texture_decode.hpp"
#include <algorithm>
#include <array>

namespace aurora::vita::gfx {
namespace {
inline uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline uint8_t expand4(uint8_t v) { return static_cast<uint8_t>((v << 4) | v); }
inline uint8_t expand5(uint8_t v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
inline uint8_t expand6(uint8_t v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }
inline uint8_t expand3(uint8_t v) { return static_cast<uint8_t>((v << 5) | (v << 2) | (v >> 1)); }
struct RGBA { uint8_t r,g,b,a; };
RGBA decode_rgb565(uint16_t v) { return {expand5((v>>11)&31), expand6((v>>5)&63), expand5(v&31), 255}; }
RGBA decode_rgb5a3(uint16_t v) {
  if (v & 0x8000) return {expand5((v>>10)&31), expand5((v>>5)&31), expand5(v&31), 255};
  return {expand4((v>>8)&15), expand4((v>>4)&15), expand4(v&15), expand3((v>>12)&7)};
}
RGBA palette_color(const TextureDesc& d, uint32_t idx) {
  const auto* p = static_cast<const uint8_t*>(d.palette);
  if (!p || idx*2+1 >= d.paletteSize) return {255,0,255,255};
  const uint16_t v = be16(p + idx*2);
  switch (d.paletteFormat) {
  case PaletteFormat::IA8: return {static_cast<uint8_t>(v&255),static_cast<uint8_t>(v&255),static_cast<uint8_t>(v&255),static_cast<uint8_t>(v>>8)};
  case PaletteFormat::RGB565: return decode_rgb565(v);
  case PaletteFormat::RGB5A3: return decode_rgb5a3(v);
  default: return {255,0,255,255};
  }
}
void put(std::vector<uint8_t>& out, uint32_t w, uint32_t h, uint32_t x, uint32_t y, RGBA c) {
  if (x>=w || y>=h) return;
  const size_t o=(static_cast<size_t>(y)*w+x)*4;
  out[o]=c.r; out[o+1]=c.g; out[o+2]=c.b; out[o+3]=c.a;
}
size_t blocks(uint32_t n, uint32_t b) { return (n+b-1)/b; }
void cmpr_block(const uint8_t* src, std::vector<uint8_t>& out, uint32_t w, uint32_t h, uint32_t ox, uint32_t oy) {
  const uint16_t c0v=be16(src), c1v=be16(src+2);
  std::array<RGBA,4> c{}; c[0]=decode_rgb565(c0v); c[1]=decode_rgb565(c1v);
  auto dxt_blend=[](uint8_t v1,uint8_t v2)->uint8_t{return static_cast<uint8_t>((v1*3u+v2*5u)>>3);};
  if (c0v>c1v) {
    // GameCube/Wii CMPR is not PC DXT1: interpolation uses a 3/8,5/8 blend.
    c[2]={dxt_blend(c[1].r,c[0].r),dxt_blend(c[1].g,c[0].g),dxt_blend(c[1].b,c[0].b),255};
    c[3]={dxt_blend(c[0].r,c[1].r),dxt_blend(c[0].g,c[1].g),dxt_blend(c[0].b,c[1].b),255};
  } else {
    // GX keeps the RGB average for the transparent selector (DXT1 normally uses black).
    RGBA avg{static_cast<uint8_t>((c[0].r+c[1].r)/2),static_cast<uint8_t>((c[0].g+c[1].g)/2),static_cast<uint8_t>((c[0].b+c[1].b)/2),255};
    c[2]=avg; c[3]={avg.r,avg.g,avg.b,0};
  }
  for (uint32_t y=0;y<4;y++) {
    uint8_t bits=src[4+y];
    for (uint32_t x=0;x<4;x++) put(out,w,h,ox+x,oy+y,c[(bits>>(6-2*x))&3]);
  }
}
}

size_t encoded_texture_size(uint32_t w,uint32_t h,TextureFormat f) noexcept {
  switch(f){
  case TextureFormat::I4: case TextureFormat::C4: return blocks(w,8)*blocks(h,8)*32;
  case TextureFormat::I8: case TextureFormat::IA4: case TextureFormat::C8: return blocks(w,8)*blocks(h,4)*32;
  case TextureFormat::IA8: case TextureFormat::RGB565: case TextureFormat::RGB5A3: case TextureFormat::C14X2: return blocks(w,4)*blocks(h,4)*32;
  case TextureFormat::RGBA8: return blocks(w,4)*blocks(h,4)*64;
  case TextureFormat::CMPR: return blocks(w,8)*blocks(h,8)*32;
  case TextureFormat::RGBA8888: return static_cast<size_t>(w)*h*4;
  }
  return 0;
}

size_t encoded_mip_chain_size(uint32_t w,uint32_t h,TextureFormat f,uint8_t mipCount) noexcept {
  size_t total=0;const unsigned levels=std::max<unsigned>(1,mipCount);
  for(unsigned level=0;level<levels;level++){total+=encoded_texture_size(std::max(1u,w>>level),std::max(1u,h>>level),f);}
  return total;
}

DecodeResult decode_texture_rgba8(const TextureDesc& d) noexcept {
  DecodeResult r; r.width=d.width; r.height=d.height;
  if(!d.data || !d.width || !d.height) return r;
  const size_t need=encoded_texture_size(d.width,d.height,d.format);
  if(d.dataSize && d.dataSize<need) return r;
  r.rgba.assign(static_cast<size_t>(d.width)*d.height*4,0);
  const auto* s=static_cast<const uint8_t*>(d.data); size_t off=0;
  if(d.format==TextureFormat::RGBA8888){ std::copy_n(s,need,r.rgba.data()); r.ok=true; return r; }
  auto tile=[&](uint32_t bw,uint32_t bh,auto fn){
    for(uint32_t by=0;by<d.height;by+=bh) for(uint32_t bx=0;bx<d.width;bx+=bw) fn(bx,by);
  };
  switch(d.format){
  case TextureFormat::I4: tile(8,8,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<8;y++)for(uint32_t x=0;x<8;x+=2){uint8_t v=s[off++];uint8_t a=expand4(v>>4),b=expand4(v&15);put(r.rgba,d.width,d.height,bx+x,by+y,{a,a,a,a});put(r.rgba,d.width,d.height,bx+x+1,by+y,{b,b,b,b});}});break;
  case TextureFormat::I8: tile(8,4,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<4;y++)for(uint32_t x=0;x<8;x++){uint8_t v=s[off++];put(r.rgba,d.width,d.height,bx+x,by+y,{v,v,v,v});}});break;
  case TextureFormat::IA4: tile(8,4,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<4;y++)for(uint32_t x=0;x<8;x++){uint8_t v=s[off++],a=expand4(v>>4),i=expand4(v&15);put(r.rgba,d.width,d.height,bx+x,by+y,{i,i,i,a});}});break;
  case TextureFormat::IA8: tile(4,4,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<4;y++)for(uint32_t x=0;x<4;x++){uint8_t a=s[off++],i=s[off++];put(r.rgba,d.width,d.height,bx+x,by+y,{i,i,i,a});}});break;
  case TextureFormat::RGB565: tile(4,4,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<4;y++)for(uint32_t x=0;x<4;x++){put(r.rgba,d.width,d.height,bx+x,by+y,decode_rgb565(be16(s+off)));off+=2;}});break;
  case TextureFormat::RGB5A3: tile(4,4,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<4;y++)for(uint32_t x=0;x<4;x++){put(r.rgba,d.width,d.height,bx+x,by+y,decode_rgb5a3(be16(s+off)));off+=2;}});break;
  case TextureFormat::RGBA8: tile(4,4,[&](uint32_t bx,uint32_t by){size_t base=off; for(uint32_t y=0;y<4;y++)for(uint32_t x=0;x<4;x++){size_t i=y*4+x; RGBA c{s[base+i*2+1],s[base+32+i*2],s[base+32+i*2+1],s[base+i*2]};put(r.rgba,d.width,d.height,bx+x,by+y,c);}off+=64;});break;
  case TextureFormat::C4: tile(8,8,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<8;y++)for(uint32_t x=0;x<8;x+=2){uint8_t v=s[off++];put(r.rgba,d.width,d.height,bx+x,by+y,palette_color(d,v>>4));put(r.rgba,d.width,d.height,bx+x+1,by+y,palette_color(d,v&15));}});break;
  case TextureFormat::C8: tile(8,4,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<4;y++)for(uint32_t x=0;x<8;x++)put(r.rgba,d.width,d.height,bx+x,by+y,palette_color(d,s[off++]));});break;
  case TextureFormat::C14X2: tile(4,4,[&](uint32_t bx,uint32_t by){for(uint32_t y=0;y<4;y++)for(uint32_t x=0;x<4;x++){uint16_t v=be16(s+off)&0x3fff;off+=2;put(r.rgba,d.width,d.height,bx+x,by+y,palette_color(d,v));}});break;
  case TextureFormat::CMPR: tile(8,8,[&](uint32_t bx,uint32_t by){cmpr_block(s+off,r.rgba,d.width,d.height,bx,by);cmpr_block(s+off+8,r.rgba,d.width,d.height,bx+4,by);cmpr_block(s+off+16,r.rgba,d.width,d.height,bx,by+4);cmpr_block(s+off+24,r.rgba,d.width,d.height,bx+4,by+4);off+=32;});break;
  case TextureFormat::RGBA8888: break;
  }
  r.ok=true; return r;
}
} // namespace aurora::vita::gfx
