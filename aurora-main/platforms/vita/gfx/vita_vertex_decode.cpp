#include "vita_vertex_decode.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace aurora::vita::gfx {
namespace {
uint16_t read16(const uint8_t* p, bool le) noexcept { return le ? static_cast<uint16_t>(p[0] | (p[1] << 8)) : static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t read32(const uint8_t* p, bool le) noexcept { return le ? (uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24) : (uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | uint32_t(p[3])); }
float readf(const uint8_t* p, bool le) noexcept { return std::bit_cast<float>(read32(p,le)); }
uint8_t ex4(uint32_t v) noexcept { return static_cast<uint8_t>((v<<4)|v); }
uint8_t ex5(uint32_t v) noexcept { return static_cast<uint8_t>((v<<3)|(v>>2)); }
uint8_t ex6(uint32_t v) noexcept { return static_cast<uint8_t>((v<<2)|(v>>4)); }
size_t component_bytes(VertexComponent c, uint8_t n) noexcept {
  switch(c){case VertexComponent::U8:case VertexComponent::S8:return n;case VertexComponent::U16:case VertexComponent::S16:return size_t(n)*2;case VertexComponent::F32:return size_t(n)*4;case VertexComponent::RGB565:case VertexComponent::RGBA4:return 2;case VertexComponent::RGB8:return 3;case VertexComponent::RGBX8:case VertexComponent::RGBA8:return 4;case VertexComponent::RGBA6:return 3;}return 0;
}
bool numeric(const uint8_t* p, size_t avail, const VertexDecodeAttribute& a, bool le, float out[4]) noexcept {
  if (a.components == 0 || a.components > 4 || avail < component_bytes(a.component, a.components)) return false;
  const float scale = std::ldexp(1.0f, -static_cast<int>(a.frac));
  for (unsigned i = 0; i < a.components; i++) {
    switch (a.component) {
    case VertexComponent::U8: out[i] = p[i] * scale; break;
    case VertexComponent::S8: out[i] = static_cast<int8_t>(p[i]) * scale; break;
    case VertexComponent::U16: out[i] = read16(p + i * 2, le) * scale; break;
    case VertexComponent::S16: out[i] = static_cast<int16_t>(read16(p + i * 2, le)) * scale; break;
    case VertexComponent::F32: out[i] = readf(p + i * 4, le); break;
    default: return false;
    }
  }
  return true;
}
bool color(const uint8_t*p,size_t avail,VertexComponent c,bool le,uint8_t out[4]) noexcept {
  out[0]=out[1]=out[2]=255;out[3]=255;switch(c){case VertexComponent::RGB565:{if(avail<2)return false;auto v=read16(p,le);out[0]=ex5((v>>11)&31);out[1]=ex6((v>>5)&63);out[2]=ex5(v&31);return true;}case VertexComponent::RGB8:if(avail<3)return false;std::copy_n(p,3,out);return true;case VertexComponent::RGBX8:if(avail<4)return false;std::copy_n(p,3,out);return true;case VertexComponent::RGBA4:{if(avail<2)return false;auto v=read16(p,le);out[0]=ex4((v>>12)&15);out[1]=ex4((v>>8)&15);out[2]=ex4((v>>4)&15);out[3]=ex4(v&15);return true;}case VertexComponent::RGBA6:{if(avail<3)return false;uint32_t v=le?(uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16):(uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2]);out[0]=ex6((v>>18)&63);out[1]=ex6((v>>12)&63);out[2]=ex6((v>>6)&63);out[3]=ex6(v&63);return true;}case VertexComponent::RGBA8:if(avail<4)return false;std::copy_n(p,4,out);return true;default:return false;}
}
bool resolve(const uint8_t* stream, size_t streamSize, size_t base, const VertexDecodeAttribute& a, bool streamLe,
             const uint8_t*& p, size_t& avail, bool& le) noexcept {
  if (a.source == VertexSource::None) return false;
  if (base + a.streamOffset >= streamSize) return false;
  const uint8_t* s = stream + base + a.streamOffset;
  if (a.source == VertexSource::Direct) {
    if (base + a.streamOffset + a.valueOffset >= streamSize) return false;
    p = s + a.valueOffset;
    avail = streamSize - (base + a.streamOffset + a.valueOffset);
    le = streamLe;
    return true;
  }
  const size_t indexBytes = a.source == VertexSource::Index8 ? 1 : 2;
  if (streamSize - (base + a.streamOffset) < indexBytes) return false;
  const size_t index = a.source == VertexSource::Index8 ? s[0] : read16(s, streamLe);
  if (!a.array.data || !a.array.stride) return false;
  const size_t off = index * a.array.stride;
  if (off >= a.array.size) return false;
  if (off + a.valueOffset >= a.array.size) return false;
  p = a.array.data + off + a.valueOffset;
  avail = a.array.size - off - a.valueOffset;
  le = a.array.littleEndian;
  return true;
}
}
VertexLayout canonical_vertex_layout() noexcept {
  VertexLayout l{};l.count=11;
  l.attributes[0]={0,4,VertexScalar::F32,false,sizeof(CanonicalVertex),offsetof(CanonicalVertex,position)};
  l.attributes[1]={1,4,VertexScalar::U8,true,sizeof(CanonicalVertex),offsetof(CanonicalVertex,color0)};
  l.attributes[2]={2,4,VertexScalar::U8,true,sizeof(CanonicalVertex),offsetof(CanonicalVertex,color1)};
  for(unsigned i=0;i<8;i++)l.attributes[3+i]={static_cast<uint8_t>(3+i),3,VertexScalar::F32,false,sizeof(CanonicalVertex),static_cast<uint16_t>(offsetof(CanonicalVertex,texcoord)+sizeof(float)*3*i)};
  return l;
}
VertexDecodeResult decode_vertices(const uint8_t*stream,size_t streamSize,uint32_t vertexCount,const VertexDecodeLayout&layout) noexcept {VertexDecodeResult r{};if(!stream||!layout.streamStride||layout.count>layout.attributes.size())return r;if(size_t(vertexCount)*layout.streamStride>streamSize)return r;r.vertices.resize(vertexCount);for(uint32_t vi=0;vi<vertexCount;vi++){size_t base=size_t(vi)*layout.streamStride;auto&v=r.vertices[vi];for(unsigned ai=0;ai<layout.count;ai++){const auto&a=layout.attributes[ai];if(a.source==VertexSource::None)continue;const uint8_t*p=nullptr;size_t avail=0;bool le=false;if(!resolve(stream,streamSize,base,a,layout.streamLittleEndian,p,avail,le)){r.badVertex=vi;return r;}if(a.semantic==VertexSemantic::Color0||a.semantic==VertexSemantic::Color1){uint8_t*c=a.semantic==VertexSemantic::Color0?v.color0:v.color1;if(!color(p,avail,a.component,le,c)){r.badVertex=vi;return r;}continue;}float tmp[4]{};if(!numeric(p,avail,a,le,tmp)){r.badVertex=vi;return r;}switch(a.semantic){
case VertexSemantic::PnMatrixIndex:v.pnMatrixIndex=static_cast<uint8_t>(tmp[0]/3.f);break;
case VertexSemantic::TexMatrixIndex0:case VertexSemantic::TexMatrixIndex1:case VertexSemantic::TexMatrixIndex2:case VertexSemantic::TexMatrixIndex3:case VertexSemantic::TexMatrixIndex4:case VertexSemantic::TexMatrixIndex5:case VertexSemantic::TexMatrixIndex6:case VertexSemantic::TexMatrixIndex7:{unsigned mi=static_cast<unsigned>(a.semantic)-static_cast<unsigned>(VertexSemantic::TexMatrixIndex0);v.texMatrixIndex[mi]=static_cast<uint8_t>(tmp[0]);break;}
case VertexSemantic::Position:for(unsigned j=0;j<std::min<unsigned>(3,a.components);j++)v.position[j]=tmp[j];break;
case VertexSemantic::Normal:for(unsigned j=0;j<std::min<unsigned>(3,a.components);j++)v.normal[j]=tmp[j];break;
case VertexSemantic::Binormal:for(unsigned j=0;j<std::min<unsigned>(3,a.components);j++)v.binormal[j]=tmp[j];break;
case VertexSemantic::Tangent:for(unsigned j=0;j<std::min<unsigned>(3,a.components);j++)v.tangent[j]=tmp[j];break;
case VertexSemantic::Tex0:case VertexSemantic::Tex1:case VertexSemantic::Tex2:case VertexSemantic::Tex3:case VertexSemantic::Tex4:case VertexSemantic::Tex5:case VertexSemantic::Tex6:case VertexSemantic::Tex7:{unsigned ti=static_cast<unsigned>(a.semantic)-static_cast<unsigned>(VertexSemantic::Tex0);for(unsigned j=0;j<std::min<unsigned>(3,a.components);j++)v.texcoord[ti][j]=tmp[j];if(a.components<3)v.texcoord[ti][2]=1.f;break;}default:break;}}}r.ok=true;return r;}
} // namespace aurora::vita::gfx
