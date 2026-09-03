#pragma once
#include "vita_buffer_pool.hpp"
#include "vita_command_stream.hpp"
#include "vita_efb.hpp"
#include "vita_pipeline_cache.hpp"
#include "vita_texture_cache.hpp"
#include <cstddef>
#include <cstdint>
#include <array>
namespace aurora::vita::gfx {
struct RendererConfig { uint32_t width=960,height=544;size_t textureBudget=24*1024*1024;size_t pipelineBudget=512; };
class Renderer {
public:
  explicit Renderer(const RendererConfig& cfg={});~Renderer();
  Renderer(const Renderer&)=delete;Renderer&operator=(const Renderer&)=delete;
  bool initialize() noexcept;void shutdown() noexcept;void begin_frame() noexcept;void end_frame() noexcept;
  uint64_t create_pipeline(const PipelineDesc& d) noexcept;
  Handle create_texture(const TextureDesc& d) noexcept;
  size_t invalidate_texture_source_range(uint64_t start,size_t bytes) noexcept{return textures_.invalidate_source_range(start,bytes);}
  Handle create_vertex_buffer(const void*d,size_t n,bool dynamic=false) noexcept{return buffers_.create_vertex(d,n,dynamic);}
  Handle create_index_buffer(const void*d,size_t n,bool dynamic=false) noexcept{return buffers_.create_index(d,n,dynamic);}
  bool update_buffer(Handle h,const void*d,size_t n,size_t off=0) noexcept{return buffers_.update(h,d,n,off);}
  Handle create_efb(uint32_t w,uint32_t h,bool depth=true) noexcept{return efb_.create(w,h,depth);}
  bool bind_efb(Handle h) noexcept;void bind_default() noexcept;
  bool blit_efb(Handle h) noexcept;
  // GPU-resident framebuffer copy used for GXCopyTex. Source coordinates are top-left Aurora/GX coordinates.
  Handle capture_current(Handle existing,const Scissor& src,uint32_t dstWidth,uint32_t dstHeight,
                         EfbCopyFormat format=EfbCopyFormat::Passthrough) noexcept;
  Handle upload_efb_rgba(Handle existing,uint32_t width,uint32_t height,const void* rgba) noexcept;
  void clear_current(const Color& color,float depth,bool clearRgb,bool clearAlpha,bool clearDepth) noexcept;
  void execute(const CommandStream& stream) noexcept;void draw(const DrawPacket& d) noexcept;
  const FrameStats& stats()const noexcept{return stats_;}uint64_t frame()const noexcept{return frame_;}
  uint32_t target_width()const noexcept{return targetWidth_;}uint32_t target_height()const noexcept{return targetHeight_;}
  PipelineCache& pipelines() noexcept{return pipelines_;}TextureCache& textures() noexcept{return textures_;}BufferPool& buffers() noexcept{return buffers_;}EfbManager& efb() noexcept{return efb_;}
private:
  void restore_target(Handle target,uint32_t width,uint32_t height) noexcept;
  void invalidate_draw_state() noexcept;
  RendererConfig cfg_{};uint32_t targetWidth_=960,targetHeight_=544;Handle boundEfb_=InvalidHandle;
  PipelineCache pipelines_{};TextureCache textures_;BufferPool buffers_{};EfbManager efb_{};FrameStats stats_{};uint64_t frame_=0;bool initialized_=false;
#if defined(__vita__)
  bool viewportValid_=false,scissorValid_=false,vertexStateValid_=false,indexStateValid_=false;
  Viewport cachedViewport_{};Scissor cachedScissor_{};
  Handle cachedVertexBuffer_=InvalidHandle,cachedIndexBuffer_=InvalidHandle;
  uint32_t cachedVertexOffset_=0;
  uint64_t cachedVertexPipeline_=0;
  std::array<TextureBinding,MaxTextures> cachedTextures_{};
  std::array<bool,MaxTextures> textureStateValid_{};
#endif
};
} // namespace aurora::vita::gfx
