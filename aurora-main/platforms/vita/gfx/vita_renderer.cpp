#include "vita_renderer.hpp"
#include "vita_pipeline_key.hpp"
#include <algorithm>
#if defined(__vita__)
#include <vitaGL.h>
#endif
namespace aurora::vita::gfx {
namespace {
#if defined(__vita__)
GLenum prim(Primitive p){switch(p){case Primitive::Triangles:return GL_TRIANGLES;case Primitive::TriangleStrip:return GL_TRIANGLE_STRIP;case Primitive::TriangleFan:return GL_TRIANGLE_FAN;case Primitive::Lines:return GL_LINES;case Primitive::LineStrip:return GL_LINE_STRIP;case Primitive::Points:return GL_POINTS;}return GL_TRIANGLES;}
GLenum scalar(VertexScalar s){switch(s){case VertexScalar::F32:return GL_FLOAT;case VertexScalar::S8:return GL_BYTE;case VertexScalar::U8:return GL_UNSIGNED_BYTE;case VertexScalar::S16:return GL_SHORT;case VertexScalar::U16:return GL_UNSIGNED_SHORT;}return GL_FLOAT;}
bool same_viewport(const Viewport&a,const Viewport&b) noexcept{return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height&&a.znear==b.znear&&a.zfar==b.zfar;}
bool same_scissor(const Scissor&a,const Scissor&b) noexcept{return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
bool same_sampler(const SamplerDesc&a,const SamplerDesc&b) noexcept{return a.wrapS==b.wrapS&&a.wrapT==b.wrapT&&a.minFilter==b.minFilter&&a.magFilter==b.magFilter&&a.lodBias==b.lodBias&&a.minLod==b.minLod&&a.maxLod==b.maxLod;}
bool same_texture_binding(const TextureBinding&a,const TextureBinding&b) noexcept{return a.texture==b.texture&&a.source==b.source&&same_sampler(a.sampler,b.sampler);}
#endif
}
Renderer::Renderer(const RendererConfig&cfg):cfg_(cfg),pipelines_(cfg.pipelineBudget),textures_(cfg.textureBudget){}Renderer::~Renderer(){shutdown();}
bool Renderer::initialize() noexcept {targetWidth_=cfg_.width;targetHeight_=cfg_.height;initialized_=true;return true;}
void Renderer::shutdown() noexcept {if(!initialized_)return;pipelines_.clear();textures_.clear();buffers_.clear();efb_.clear();invalidate_draw_state();initialized_=false;}
void Renderer::begin_frame() noexcept {pipelines_.clear_pins();pipelines_.trim_to_budget();stats_={};invalidate_draw_state();}
void Renderer::end_frame() noexcept {textures_.trim(frame_);frame_++;}
uint64_t Renderer::create_pipeline(const PipelineDesc&d) noexcept {auto*p=pipelines_.get_or_create(d,&stats_);if(!p)return 0;pipelines_.pin(p->key);return p->key;}
Handle Renderer::create_texture(const TextureDesc&d) noexcept{return textures_.get_or_upload(d,frame_,&stats_);}
void Renderer::invalidate_draw_state() noexcept {
#if defined(__vita__)
  viewportValid_=false;scissorValid_=false;vertexStateValid_=false;indexStateValid_=false;textureStateValid_.fill(false);
#endif
  pipelines_.invalidate_bound();
}
bool Renderer::bind_efb(Handle h) noexcept {uint32_t w=0,hgt=0;if(!efb_.dimensions(h,w,hgt)||!efb_.bind(h))return false;boundEfb_=h;targetWidth_=w;targetHeight_=hgt;invalidate_draw_state();return true;}
void Renderer::bind_default() noexcept {efb_.bind_default(cfg_.width,cfg_.height);boundEfb_=InvalidHandle;targetWidth_=cfg_.width;targetHeight_=cfg_.height;invalidate_draw_state();}
bool Renderer::blit_efb(Handle h) noexcept {
  const bool ok=efb_.blit_to_default(h,cfg_.width,cfg_.height);
  if(ok){boundEfb_=InvalidHandle;targetWidth_=cfg_.width;targetHeight_=cfg_.height;invalidate_draw_state();}
  return ok;
}
void Renderer::restore_target(Handle target,uint32_t width,uint32_t height) noexcept {if(target){if(efb_.bind(target)){boundEfb_=target;targetWidth_=width;targetHeight_=height;invalidate_draw_state();return;}}efb_.bind_default(cfg_.width,cfg_.height);boundEfb_=InvalidHandle;targetWidth_=cfg_.width;targetHeight_=cfg_.height;invalidate_draw_state();}
Handle Renderer::capture_current(Handle existing,const Scissor& src,uint32_t dstWidth,uint32_t dstHeight,EfbCopyFormat format) noexcept {if(src.width<=0||src.height<=0||!dstWidth||!dstHeight)return InvalidHandle;const Handle prev=boundEfb_;const uint32_t prevW=targetWidth_,prevH=targetHeight_;const int32_t y=static_cast<int32_t>(targetHeight_)-(src.y+src.height);const int32_t x=std::max<int32_t>(0,src.x);const int32_t sy=std::max<int32_t>(0,y);const uint32_t sw=std::min<uint32_t>(static_cast<uint32_t>(src.width),targetWidth_-std::min<uint32_t>(static_cast<uint32_t>(x),targetWidth_));const uint32_t sh=std::min<uint32_t>(static_cast<uint32_t>(src.height),targetHeight_-std::min<uint32_t>(static_cast<uint32_t>(sy),targetHeight_));if(!sw||!sh)return InvalidHandle;Handle out=efb_.capture_from_bound(existing,x,sy,sw,sh,dstWidth,dstHeight,format);restore_target(prev,prevW,prevH);return out;}
void Renderer::clear_current(const Color& color,float depth,bool clearRgb,bool clearAlpha,bool clearDepth) noexcept {
#if defined(__vita__)
  glColorMask(clearRgb?GL_TRUE:GL_FALSE,clearRgb?GL_TRUE:GL_FALSE,clearRgb?GL_TRUE:GL_FALSE,clearAlpha?GL_TRUE:GL_FALSE);glDepthMask(clearDepth?GL_TRUE:GL_FALSE);glClearColor(color.r,color.g,color.b,color.a);glClearDepth(depth);GLbitfield mask=0;if(clearRgb||clearAlpha)mask|=GL_COLOR_BUFFER_BIT;if(clearDepth)mask|=GL_DEPTH_BUFFER_BIT;if(mask)glClear(mask);pipelines_.invalidate_bound();
#else
  (void)color;(void)depth;(void)clearRgb;(void)clearAlpha;(void)clearDepth;
#endif
}
void Renderer::execute(const CommandStream&s) noexcept {for(const auto&c:s.commands())switch(c.type){case CommandType::Clear:
#if defined(__vita__)
  if(c.clear.colorEnable) glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  if(c.clear.depthEnable) glDepthMask(GL_TRUE);
  glClearColor(c.clear.color.r,c.clear.color.g,c.clear.color.b,c.clear.color.a);
  glClearDepth(c.clear.depth);
  {
    GLbitfield m=0;
    if(c.clear.colorEnable) m|=GL_COLOR_BUFFER_BIT;
    if(c.clear.depthEnable) m|=GL_DEPTH_BUFFER_BIT;
    if(m) glClear(m);
  }
  pipelines_.invalidate_bound();
  break;
#else
  break;
#endif
case CommandType::Draw:draw(c.draw);break;case CommandType::SetRenderTarget:if(c.target.target)bind_efb(c.target.target);else bind_default();break;case CommandType::CopyEfb:if(c.copy.destination)blit_efb(c.copy.destination);break;case CommandType::Barrier:
#if defined(__vita__)
  glFlush();
#endif
  break;}pipelines_.clear_pins();pipelines_.trim_to_budget();}
void Renderer::draw(const DrawPacket&d) noexcept {const auto*p=pipelines_.find(d.pipelineKey);if(!p)return;pipelines_.bind(*p,d.uniforms,&stats_);
#if defined(__vita__)
  if(!viewportValid_||!same_viewport(cachedViewport_,d.viewport)){const GLint vy=static_cast<GLint>(targetHeight_)-static_cast<GLint>(d.viewport.y+d.viewport.height);glViewport((GLint)d.viewport.x,vy,(GLsizei)d.viewport.width,(GLsizei)d.viewport.height);glDepthRangef(std::clamp(d.viewport.znear,0.0f,1.0f),std::clamp(d.viewport.zfar,0.0f,1.0f));cachedViewport_=d.viewport;viewportValid_=true;}
  if(!scissorValid_||!same_scissor(cachedScissor_,d.scissor)){glEnable(GL_SCISSOR_TEST);const GLint sy=static_cast<GLint>(targetHeight_)-(d.scissor.y+d.scissor.height);glScissor(d.scissor.x,sy,d.scissor.width,d.scissor.height);cachedScissor_=d.scissor;scissorValid_=true;}
  for(unsigned i=0;i<MaxTextures;i++)if(d.textures[i].texture&&(!textureStateValid_[i]||!same_texture_binding(cachedTextures_[i],d.textures[i]))){if(d.textures[i].source==TextureSource::Efb)efb_.bind_texture(d.textures[i].texture,i,d.textures[i].sampler);else textures_.bind(d.textures[i].texture,i,d.textures[i].sampler);cachedTextures_[i]=d.textures[i];textureStateValid_[i]=true;}
  GLuint vb=buffers_.gl_id(d.vertices.buffer);if(!vb)return;
  const uint32_t vertexBaseOffset=d.absoluteVertexIndices?0:d.vertices.offset;
  if(!vertexStateValid_||cachedVertexBuffer_!=d.vertices.buffer||cachedVertexOffset_!=vertexBaseOffset||cachedVertexPipeline_!=p->key){glBindBuffer(GL_ARRAY_BUFFER,vb);for(unsigned i=0;i<MaxVertexAttributes;i++)glDisableVertexAttribArray(i);glVertexAttrib4f(1,1,1,1,1);glVertexAttrib4f(2,1,1,1,1);for(unsigned i=3;i<11;i++)glVertexAttrib4f(i,0,0,0,1);const auto&l=p->desc.layout;for(unsigned i=0;i<l.count&&i<MaxVertexAttributes;i++){const auto&a=l.attributes[i];if(a.location>=MaxVertexAttributes||!a.components)continue;glEnableVertexAttribArray(a.location);const uintptr_t off=(uintptr_t)vertexBaseOffset+a.offset;glVertexAttribPointer(a.location,a.components,scalar(a.scalar),a.normalized?GL_TRUE:GL_FALSE,a.stride,(const void*)off);}cachedVertexBuffer_=d.vertices.buffer;cachedVertexOffset_=vertexBaseOffset;cachedVertexPipeline_=p->key;vertexStateValid_=true;}
  const GLenum mode=prim(p->desc.primitive);const unsigned reps=std::max(1u,d.instanceCount);if(d.indexCount){GLuint ib=buffers_.gl_id(d.indices.buffer);if(!ib)return;if(!indexStateValid_||cachedIndexBuffer_!=d.indices.buffer){glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ib);cachedIndexBuffer_=d.indices.buffer;indexStateValid_=true;}for(unsigned i=0;i<reps;i++)glDrawElements(mode,d.indexCount,GL_UNSIGNED_SHORT,(const void*)(uintptr_t)d.indices.offset);}else for(unsigned i=0;i<reps;i++)glDrawArrays(mode,d.firstVertex,d.vertexCount);
#else
  (void)d;
#endif
  stats_.drawCalls+=std::max(1u,d.instanceCount);const uint32_t n=d.indexCount?d.indexCount:d.vertexCount;if(p->desc.primitive==Primitive::Triangles)stats_.triangles+=(n/3)*std::max(1u,d.instanceCount);else if(p->desc.primitive==Primitive::TriangleStrip||p->desc.primitive==Primitive::TriangleFan)stats_.triangles+=(n>2?n-2:0)*std::max(1u,d.instanceCount);
}
} // namespace aurora::vita::gfx
