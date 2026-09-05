#include "vita_texture_cache.hpp"
#include "vita_pipeline_key.hpp"
#include "vita_texture_decode.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>
#if defined(__vita__)
#include <vitaGL.h>
#endif
namespace aurora::vita::gfx {
namespace {
#ifndef MKW_VITA_TEXTURE_SAFE_RETRY
#define MKW_VITA_TEXTURE_SAFE_RETRY 0
#endif
void vgl_log_texture_alloc_fail(uint32_t w,uint32_t h,unsigned fmt,uint8_t mips,uint64_t sourceId,
                                size_t estBytes,size_t cacheBytes,size_t budget) noexcept {
  std::fprintf(stderr,
    "[gx] m12_1_tex_fail w=%u h=%u fmt=0x%X mips=%u source=0x%llX est=%llu cache_bytes=%llu budget=%llu\n",
    w,h,fmt,static_cast<unsigned>(mips),static_cast<unsigned long long>(sourceId),
    static_cast<unsigned long long>(estBytes),static_cast<unsigned long long>(cacheBytes),
    static_cast<unsigned long long>(budget));
}
// vitaGL uploads RGBA8 as VGL_ALIGN(w,8)*h*4 (row-stride padded to 8 texels).
inline size_t aligned_rgba_bytes(uint32_t width,uint32_t height) noexcept {
  return static_cast<size_t>((width+7u)&~7u)*height*4u;
}
size_t rgba_full_mip_bytes(uint32_t width,uint32_t height) noexcept {
  size_t total=0;
  for(;;){
    total+=aligned_rgba_bytes(width,height);
    if(width==1&&height==1)break;
    width=std::max(1u,width>>1);height=std::max(1u,height>>1);
  }
  return total;
}
// Conservative GPU-side footprint estimate for a texture we are about to upload.
// Deliberately over-estimates: vitaGL row alignment, mip chain, and internal
// temp buffers can all push real usage above width*height*4. Never underestimate
// here or the pre-eviction below will let vitaGL run out of mapped memory.
size_t estimate_gpu_bytes(const TextureDesc& d) noexcept {
  size_t total=0;
  const unsigned levels=std::max<unsigned>(1u,d.mipCount);
  for(unsigned l=0;l<levels;l++){
    total+=aligned_rgba_bytes(std::max(1u,d.width>>l),std::max(1u,d.height>>l));
  }
  if(d.generateMipmaps){
    const size_t chain=rgba_full_mip_bytes(d.width,d.height);
    if(chain>total)total=chain;
  }
  return total+total/5u+65536u; // +20% slack +64 KiB fixed overhead
}
constexpr size_t kEvictHeadroom=512u*1024u;
#if defined(__vita__)
GLint wrap(WrapMode w){switch(w){case WrapMode::Clamp:return GL_CLAMP_TO_EDGE;case WrapMode::Repeat:return GL_REPEAT;case WrapMode::Mirror:return GL_MIRRORED_REPEAT;}return GL_REPEAT;}
GLint filt(Filter f){switch(f){case Filter::Nearest:return GL_NEAREST;case Filter::Linear:return GL_LINEAR;case Filter::NearestMipmapNearest:return GL_NEAREST_MIPMAP_NEAREST;case Filter::LinearMipmapNearest:return GL_LINEAR_MIPMAP_NEAREST;case Filter::NearestMipmapLinear:return GL_NEAREST_MIPMAP_LINEAR;case Filter::LinearMipmapLinear:return GL_LINEAR_MIPMAP_LINEAR;}return GL_LINEAR;}
bool mip_filter(Filter f) noexcept {return f==Filter::NearestMipmapNearest||f==Filter::LinearMipmapNearest||f==Filter::NearestMipmapLinear||f==Filter::LinearMipmapLinear;}
Filter without_mips(Filter f) noexcept {switch(f){case Filter::NearestMipmapNearest:case Filter::NearestMipmapLinear:return Filter::Nearest;case Filter::LinearMipmapNearest:case Filter::LinearMipmapLinear:return Filter::Linear;default:return f;}}
#endif
}
TextureCache::~TextureCache(){clear();}
void TextureCache::mark_gpu_idle(uint64_t frame) noexcept {
  (void)frame;
  completedUseEpoch_=useEpoch_;
  completedUseEpochValid_=true;
}
void TextureCache::pre_evict(size_t requiredBytes,uint64_t frame,uint64_t protectKey) noexcept {
  lastBlockedByProtection_=false;
  protectedBytesLast_=0;
  while(bytes_+requiredBytes+kEvictHeadroom>budget_&&!byKey_.empty()){
    const Entry* victim=nullptr;
    size_t protectedBytes=0;
#if MKW_VITA_TEXTURE_SAFE_RETRY
    // Queue depth is two in the P5.1 profile. Frame N and N-1 remain protected
    // unless a real GPU drain explicitly retired them; N-2 is safe because the
    // streaming-slot reuse path drains before overwriting that slot.
    const uint64_t recentFloor=frame>0?frame-1u:0u;
#endif
    for(const auto& kv:byKey_){
      if(kv.first==protectKey)continue;
      // Never evict a texture already referenced this frame: its GL id may sit in
      // an enqueued Aurora draw command and deleting it would crash the GPU.
      bool protectedEntry=false;
#if MKW_VITA_TEXTURE_SAFE_RETRY
      protectedEntry=kv.second.lastUse>=recentFloor &&
          (!completedUseEpochValid_||kv.second.useEpoch>completedUseEpoch_);
#else
      protectedEntry=kv.second.lastUse>=frame;
#endif
      if(protectedEntry){protectedBytes+=kv.second.bytes;continue;}
      if(!victim||kv.second.lastUse<victim->lastUse)victim=&kv.second;
    }
    if(!victim){
      lastBlockedByProtection_=protectedBytes!=0;
      protectedBytesLast_=protectedBytes;
      protectedBytesHighWater_=std::max<uint64_t>(protectedBytesHighWater_,protectedBytes);
      if(lastBlockedByProtection_)++evictBlocked_;
      break;
    }
    const size_t vb=victim->bytes;const Handle vh=victim->handle;
    erase(vh);
    ++preEvictions_;preEvictedBytes_+=vb;
  }
}
Handle TextureCache::get_or_upload(const TextureDesc& d,uint64_t frame,FrameStats* st) noexcept {
  uint64_t key=texture_key(d);if(!d.cacheable)key^=(frame+0x9e3779b97f4a7c15ull)+(key<<6)+(key>>2);auto it=byKey_.find(key);if(d.cacheable&&it!=byKey_.end()){it->second.lastUse=frame;it->second.useEpoch=++useEpoch_;if(st)st->textureHits++;return it->second.handle;}if(st)st->textureMisses++;
  if(!d.data||!d.width||!d.height)return InvalidHandle;
  // M12.1: reserve GPU memory BEFORE touching vitaGL. The speedhack vitaGL alloc
  // path is not null-safe on OOM (crashes in write_rgba8888 / gpu_alloc_mipmaps),
  // so a texture that will not fit under budget must never reach glTexImage2D.
  const size_t estBytes=estimate_gpu_bytes(d);
  lastRequestedBytes_=estBytes;
  pre_evict(estBytes,frame,key);
  if(estBytes>budget_||bytes_+estBytes>budget_){
    ++allocFailTotal_;
    if(allocFailTotal_==1||(allocFailTotal_&(allocFailTotal_-1))==0){
      vgl_log_texture_alloc_fail(d.width,d.height,static_cast<unsigned>(d.format),d.mipCount,d.sourceId,estBytes,bytes_,budget_);
    }
    return InvalidHandle;
  }
  Entry e{};e.handle=next_++;e.key=key;e.lastUse=frame;e.useEpoch=++useEpoch_;e.hasMipmaps=false;e.sourceId=d.sourceId;e.paletteSourceId=d.paletteSourceId;e.sourceBytes=d.dataSize;e.paletteBytes=d.paletteSize;
#if defined(__vita__)
  GLuint id=0;glGenTextures(1,&id);if(!id){++allocFailTotal_;return InvalidHandle;}e.gl=id;glBindTexture(GL_TEXTURE_2D,id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
#else
  e.gl=e.handle;
#endif
  const auto* base=static_cast<const uint8_t*>(d.data);size_t encodedOffset=0;const unsigned levels=std::max<unsigned>(1,d.mipCount);unsigned uploadedLevels=0;
  for(unsigned level=0;level<levels;level++){
    TextureDesc ld=d;ld.width=std::max(1u,d.width>>level);ld.height=std::max(1u,d.height>>level);ld.data=base+encodedOffset;ld.mipCount=1;ld.generateMipmaps=false;const size_t encoded=encoded_texture_size(ld.width,ld.height,ld.format);if(d.dataSize&&encodedOffset+encoded>d.dataSize)break;ld.dataSize=encoded;
    auto decoded=decode_texture_rgba8(ld);if(!decoded.ok)break;++uploadedLevels;
#if defined(__vita__)
    glTexImage2D(GL_TEXTURE_2D,static_cast<GLint>(level),GL_RGBA,decoded.width,decoded.height,0,GL_RGBA,GL_UNSIGNED_BYTE,decoded.rgba.data());
#endif
    encodedOffset+=encoded;
  }
  if(uploadedLevels==0){
#if defined(__vita__)
    GLuint id=e.gl;glDeleteTextures(1,&id);
#endif
    return InvalidHandle;
  }
#if defined(__vita__)
  // M12.1: vitaGL's gpu_alloc_texture is null-safe on OOM but leaves the texture
  // with no backing (data == NULL, status != TEX_VALID). Binding + drawing that
  // is a GPU crash. Detect it deterministically and fail the upload instead.
  if(vglGetTexDataPointer(GL_TEXTURE_2D)==nullptr){
    GLuint id=e.gl;glDeleteTextures(1,&id);
    ++allocFailTotal_;
    if(allocFailTotal_==1||(allocFailTotal_&(allocFailTotal_-1))==0){
      vgl_log_texture_alloc_fail(d.width,d.height,static_cast<unsigned>(d.format),d.mipCount,d.sourceId,estBytes,bytes_,budget_);
    }
    return InvalidHandle;
  }
#endif
  // M12.1: never call glGenerateMipmap. gpu_alloc_mipmaps() in the speedhack
  // vitaGL has an unchecked vgl_memcpy on an alloc that can return NULL. Menu
  // textures render ~1:1 and do not need a generated chain.
  e.hasMipmaps=uploadedLevels>1;
  e.bytes=estBytes; // GPU-side footprint (row-aligned + slack), not the CPU decode size
#if MKW_VITA_TEXTURE_SHARED_HEADROOM
  // The fixed upload scratch allowance is transient, not resident per texture.
  // Keep row/mip padding +20% per entry, and reserve the 64 KiB again on every
  // allocation (estBytes above), plus pre_evict's shared 512 KiB headroom.
  e.bytes-=65536u;
#endif
  bytes_+=e.bytes;if(bytes_>highWaterBytes_)highWaterBytes_=bytes_;byHandle_[e.handle]=key;byKey_[key]=e;if(st)st->textureUploads++;trim(frame);return e.handle;
}
void TextureCache::bind(Handle h,unsigned unit,const SamplerDesc&s) noexcept {auto hi=byHandle_.find(h);if(hi==byHandle_.end())return;auto it=byKey_.find(hi->second);if(it==byKey_.end())return;
#if defined(__vita__)
  glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,it->second.gl);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wrap(s.wrapS));glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wrap(s.wrapT));const Filter minFilter=!it->second.hasMipmaps&&mip_filter(s.minFilter)?without_mips(s.minFilter):s.minFilter;glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filt(minFilter));glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filt(s.magFilter));glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,s.lodBias);
#else
  (void)unit;(void)s;
#endif
}
void TextureCache::erase(Handle h) noexcept {auto hi=byHandle_.find(h);if(hi==byHandle_.end())return;auto it=byKey_.find(hi->second);if(it==byKey_.end()){byHandle_.erase(hi);return;}bytes_-=it->second.bytes;
#if defined(__vita__)
  GLuint id=it->second.gl;glDeleteTextures(1,&id);
#endif
  byKey_.erase(it);byHandle_.erase(hi);}
void TextureCache::clear() noexcept {std::vector<Handle> hs;hs.reserve(byHandle_.size());for(auto&[h,k]:byHandle_){(void)k;hs.push_back(h);}for(auto h:hs)erase(h);bytes_=0;}
void TextureCache::trim(uint64_t frame) noexcept {(void)frame;while(bytes_>budget_&&!byKey_.empty()){auto victim=std::min_element(byKey_.begin(),byKey_.end(),[](auto&a,auto&b){return a.second.lastUse<b.second.lastUse;});if(victim==byKey_.end())break;++evictions_;erase(victim->second.handle);}}
size_t TextureCache::invalidate_source_range(uint64_t start,size_t bytes) noexcept {
  if(bytes==0)return 0;
  const uint64_t end = bytes > UINT64_MAX - start ? UINT64_MAX : start + static_cast<uint64_t>(bytes);
  auto overlaps=[start,end](uint64_t p,size_t n) noexcept {
    if(p==0||n==0)return false;
    const uint64_t pe=n>UINT64_MAX-p?UINT64_MAX:p+static_cast<uint64_t>(n);
    return p<end&&start<pe;
  };
  std::vector<Handle> victims;
  for(const auto&[_,e]:byKey_)if(overlaps(e.sourceId,e.sourceBytes)||overlaps(e.paletteSourceId,e.paletteBytes))victims.push_back(e.handle);
  for(Handle h:victims)erase(h);
  return victims.size();
}

} // namespace aurora::vita::gfx
