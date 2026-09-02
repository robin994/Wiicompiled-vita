#include "vita_buffer_pool.hpp"
#if defined(__vita__)
#include <vitaGL.h>
#include <cstring>
#endif
namespace aurora::vita::gfx {
BufferPool::~BufferPool(){clear();}
Handle BufferPool::create_vertex(const void* data,size_t bytes,bool dynamic) noexcept {
#if defined(__vita__)
  GLuint id=0;glGenBuffers(1,&id);if(!id)return InvalidHandle;glBindBuffer(GL_ARRAY_BUFFER,id);glBufferData(GL_ARRAY_BUFFER,bytes,data,dynamic?GL_DYNAMIC_DRAW:GL_STATIC_DRAW);glBindBuffer(GL_ARRAY_BUFFER,0);Handle h=next_++;map_[h]={id,GL_ARRAY_BUFFER,bytes,dynamic};return h;
#else
  (void)data;(void)bytes;(void)dynamic;Handle h=next_++;map_[h]={h,0,bytes,dynamic};return h;
#endif
}
Handle BufferPool::create_index(const void* data,size_t bytes,bool dynamic) noexcept {
#if defined(__vita__)
  GLuint id=0;glGenBuffers(1,&id);if(!id)return InvalidHandle;glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,id);glBufferData(GL_ELEMENT_ARRAY_BUFFER,bytes,data,dynamic?GL_DYNAMIC_DRAW:GL_STATIC_DRAW);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);Handle h=next_++;map_[h]={id,GL_ELEMENT_ARRAY_BUFFER,bytes,dynamic};return h;
#else
  (void)data;(void)bytes;(void)dynamic;Handle h=next_++;map_[h]={h,0,bytes,dynamic};return h;
#endif
}
bool BufferPool::update(Handle h,const void* data,size_t bytes,size_t offset) noexcept {auto it=map_.find(h);if(it==map_.end()||offset>it->second.bytes||bytes>it->second.bytes-offset)return false;if(bytes==0)return true;if(!data)return false;
#if defined(__vita__)
  glBindBuffer(it->second.target,it->second.id);
  if(it->second.dynamic){
    void* mapped=glMapBufferRange(it->second.target,static_cast<GLintptr>(offset),static_cast<GLsizeiptr>(bytes),GL_MAP_WRITE_BIT);
    if(!mapped)return false;
    std::memcpy(mapped,data,bytes);
    return glUnmapBuffer(it->second.target)==GL_TRUE;
  }
  glBufferSubData(it->second.target,static_cast<GLintptr>(offset),static_cast<GLsizeiptr>(bytes),data);
#else
  (void)data;
#endif
  return true;}
void BufferPool::destroy(Handle h) noexcept {auto it=map_.find(h);if(it==map_.end())return;
#if defined(__vita__)
  GLuint id=it->second.id;glDeleteBuffers(1,&id);
#endif
  map_.erase(it);}
void BufferPool::clear() noexcept {
#if defined(__vita__)
  for(auto&[h,e]:map_){(void)h;GLuint id=e.id;glDeleteBuffers(1,&id);}
#endif
  map_.clear();}
#if defined(__vita__)
unsigned BufferPool::gl_id(Handle h)const noexcept{auto it=map_.find(h);return it==map_.end()?0:it->second.id;}
unsigned BufferPool::gl_target(Handle h)const noexcept{auto it=map_.find(h);return it==map_.end()?0:it->second.target;}
#endif
} // namespace aurora::vita::gfx
