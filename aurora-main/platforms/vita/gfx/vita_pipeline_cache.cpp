#include "vita_pipeline_cache.hpp"
#include "vita_gl_util.hpp"
#include "vita_pipeline_key.hpp"
#include "vita_shader_gen.hpp"
#include <cstring>
#include <cstdio>
#if defined(__vita__)
#include <psp2/io/stat.h>
#include <vitaGL.h>
#endif
namespace aurora::vita::gfx {
namespace {
#if defined(__vita__)
void write_failure_blob(const char* path,const void* data,size_t size) noexcept {
  FILE* fp=std::fopen(path,"wb");
  if(!fp)return;
  if(size) (void)std::fwrite(data,1,size,fp);
  std::fclose(fp);
}

void dump_pipeline_failure(uint64_t key,const PipelineDesc& d,const ShaderSources& src,const std::string& diagnostics) noexcept {
  // Failure artifacts are intentionally overwrite-by-key: a recurring broken
  // pipeline must not grow storage without bound during a long Mario Kart run.
  sceIoMkdir("ux0:data/aurora-vita",0777);
  char base[128];
  std::snprintf(base,sizeof(base),"ux0:data/aurora-vita/shader_fail_%016llx",(unsigned long long)key);
  char path[160];
  std::snprintf(path,sizeof(path),"%s.vert.glsl",base);write_failure_blob(path,src.vertex.data(),src.vertex.size());
  std::snprintf(path,sizeof(path),"%s.frag.glsl",base);write_failure_blob(path,src.fragment.data(),src.fragment.size());
  std::snprintf(path,sizeof(path),"%s.compiler.log",base);write_failure_blob(path,diagnostics.data(),diagnostics.size());

  std::snprintf(path,sizeof(path),"%s.desc.txt",base);
  FILE* fp=std::fopen(path,"wb");
  if(!fp)return;
  std::fprintf(fp,"pipeline_key=0x%016llx\n",(unsigned long long)key);
  std::fprintf(fp,"sizeof_pipeline_desc=%u\n",(unsigned)sizeof(d));
  std::fprintf(fp,"primitive=%u depth_func=%u cull=%u blend_mode=%u src_factor=%u dst_factor=%u logic_op=%u\n",
               (unsigned)d.primitive,(unsigned)d.depthFunc,(unsigned)d.cull,(unsigned)d.blendMode,
               (unsigned)d.srcFactor,(unsigned)d.dstFactor,(unsigned)d.logicOp);
  std::fprintf(fp,"depth_test=%u depth_write=%u color_write=%u alpha_write=%u reversed_z=%u polygon_offset=%u factor=%g units=%g dst_alpha=%d\n",
               (unsigned)d.depthTest,(unsigned)d.depthWrite,(unsigned)d.colorWrite,(unsigned)d.alphaWrite,
               (unsigned)d.reversedZ,(unsigned)d.polygonOffset,(double)d.polygonOffsetFactor,(double)d.polygonOffsetUnits,(int)d.dstAlpha);
  std::fprintf(fp,"fog_mode=%u fog_ortho=%u fog_range=%u clip_space=%u texgen_count=%u tev_stages=%u tev_texcoords=%u tev_raster_colors=%u indirect_stages=%u\n",
               (unsigned)d.fogMode,(unsigned)d.fogOrthographic,(unsigned)d.fogRangeEnabled,(unsigned)d.positionIsClipSpace,
               (unsigned)d.texgenCount,(unsigned)d.tev.stageCount,(unsigned)d.tev.texCoordCount,
               (unsigned)d.tev.rasterColorCount,(unsigned)d.tev.indirectStageCount);
  std::fprintf(fp,"layout_count=%u\n",(unsigned)d.layout.count);
  for(unsigned i=0;i<d.layout.count && i<MaxVertexAttributes;i++){
    const auto& a=d.layout.attributes[i];
    std::fprintf(fp,"layout[%u] loc=%u comps=%u scalar=%u norm=%u stride=%u offset=%u\n",i,
                 (unsigned)a.location,(unsigned)a.components,(unsigned)a.scalar,(unsigned)a.normalized,
                 (unsigned)a.stride,(unsigned)a.offset);
  }
  for(unsigned i=0;i<d.texgenCount && i<MaxTextures;i++){
    const auto& t=d.texgens[i];
    std::fprintf(fp,"texgen[%u] type=%u source=%u matrix=%d post=%d emboss=%u normalize=%u matrix_from_vertex=%u\n",i,
                 (unsigned)t.type,(unsigned)t.source,(int)t.matrix,(int)t.postMatrix,(unsigned)t.embossSource,
                 (unsigned)t.normalize,(unsigned)t.matrixFromVertex);
  }
  for(unsigned i=0;i<d.colorChannels.size();i++){
    const auto& c=d.colorChannels[i];
    std::fprintf(fp,"color_channel[%u] material=%u ambient=%u diffuse=%u attenuation=%u lighting=%u\n",i,
                 (unsigned)c.materialSource,(unsigned)c.ambientSource,(unsigned)c.diffuse,
                 (unsigned)c.attenuation,(unsigned)c.lightingEnabled);
  }
  for(unsigned i=0;i<d.tev.swapTable.size();i++){
    const auto& s=d.tev.swapTable[i];
    std::fprintf(fp,"swap[%u] r=%u g=%u b=%u a=%u\n",i,(unsigned)s.r,(unsigned)s.g,(unsigned)s.b,(unsigned)s.a);
  }
  for(unsigned i=0;i<d.tev.indirectStageCount && i<MaxIndStages;i++){
    const auto& s=d.tev.indirectStages[i];
    std::fprintf(fp,"indirect[%u] texcoord=%u texture=%u scale_s_shift=%u scale_t_shift=%u\n",i,
                 (unsigned)s.texCoord,(unsigned)s.texture,(unsigned)s.scaleSShift,(unsigned)s.scaleTShift);
  }
  for(unsigned i=0;i<d.tev.stageCount && i<MaxTevStages;i++){
    const auto& s=d.tev.stages[i];
    std::fprintf(fp,"tev[%u] color=%u,%u,%u,%u alpha=%u,%u,%u,%u color_op=%u alpha_op=%u color_bias=%u alpha_bias=%u color_scale=%u alpha_scale=%u color_out=%u alpha_out=%u konst_color=%u konst_alpha=%u tex=%u tc=%u ras=%u ras_swap=%u tex_swap=%u clamp=%u/%u ind=%u ind_stage=%u ind_fmt=%u ind_bias=%u ind_alpha=%u ind_mtx=%u ind_wrap=%u/%u orig_lod=%u add_prev=%u\n",i,
                 (unsigned)s.color.a,(unsigned)s.color.b,(unsigned)s.color.c,(unsigned)s.color.d,
                 (unsigned)s.alpha.a,(unsigned)s.alpha.b,(unsigned)s.alpha.c,(unsigned)s.alpha.d,
                 (unsigned)s.colorOp,(unsigned)s.alphaOp,(unsigned)s.colorBias,(unsigned)s.alphaBias,
                 (unsigned)s.colorScale,(unsigned)s.alphaScale,(unsigned)s.colorOut,(unsigned)s.alphaOut,
                 (unsigned)s.konstColor,(unsigned)s.konstAlpha,(unsigned)s.texture,(unsigned)s.texCoord,
                 (unsigned)s.rasterSource,(unsigned)s.rasSwap,(unsigned)s.texSwap,(unsigned)s.colorClamp,(unsigned)s.alphaClamp,
                 (unsigned)s.indirectEnabled,(unsigned)s.indirectStage,(unsigned)s.indirectFormat,(unsigned)s.indirectBias,
                 (unsigned)s.indirectAlpha,(unsigned)s.indirectMatrix,(unsigned)s.indirectWrapS,(unsigned)s.indirectWrapT,
                 (unsigned)s.indirectUseOrigLod,(unsigned)s.indirectAddPrev);
  }
  std::fprintf(fp,"alpha_compare=%u,%u op=%u %u,%u\n",(unsigned)d.tev.alphaCompare.comp0,(unsigned)d.tev.alphaCompare.ref0,
               (unsigned)d.tev.alphaCompare.op,(unsigned)d.tev.alphaCompare.comp1,(unsigned)d.tev.alphaCompare.ref1);
  std::fclose(fp);
  std::printf("[aurora-vita] shader failure artifacts: %s.*\n",base);
}

GLenum compare(Compare c,bool reversed){if(reversed){switch(c){case Compare::Less:c=Compare::Greater;break;case Compare::LessEqual:c=Compare::GreaterEqual;break;case Compare::Greater:c=Compare::Less;break;case Compare::GreaterEqual:c=Compare::LessEqual;break;default:break;}}switch(c){case Compare::Never:return GL_NEVER;case Compare::Less:return GL_LESS;case Compare::Equal:return GL_EQUAL;case Compare::LessEqual:return GL_LEQUAL;case Compare::Greater:return GL_GREATER;case Compare::NotEqual:return GL_NOTEQUAL;case Compare::GreaterEqual:return GL_GEQUAL;case Compare::Always:return GL_ALWAYS;}return GL_ALWAYS;}
GLenum blend_factor(BlendFactor f){switch(f){case BlendFactor::Zero:return GL_ZERO;case BlendFactor::One:return GL_ONE;case BlendFactor::SrcColor:return GL_SRC_COLOR;case BlendFactor::OneMinusSrcColor:return GL_ONE_MINUS_SRC_COLOR;case BlendFactor::DstColor:return GL_DST_COLOR;case BlendFactor::OneMinusDstColor:return GL_ONE_MINUS_DST_COLOR;case BlendFactor::SrcAlpha:return GL_SRC_ALPHA;case BlendFactor::OneMinusSrcAlpha:return GL_ONE_MINUS_SRC_ALPHA;case BlendFactor::DstAlpha:return GL_DST_ALPHA;case BlendFactor::OneMinusDstAlpha:return GL_ONE_MINUS_DST_ALPHA;}return GL_ONE;}
void fixed(const PipelineDesc&d){if(d.depthTest){glEnable(GL_DEPTH_TEST);glDepthFunc(compare(d.depthFunc,d.reversedZ));}else glDisable(GL_DEPTH_TEST);glDepthMask(d.depthWrite?GL_TRUE:GL_FALSE);
  if(d.cull==CullMode::None)glDisable(GL_CULL_FACE);else if(d.cull==CullMode::All){glEnable(GL_CULL_FACE);glCullFace(GL_FRONT_AND_BACK);}else{glEnable(GL_CULL_FACE);glFrontFace(GL_CW);glCullFace(d.cull==CullMode::Front?GL_FRONT:GL_BACK);}glColorMask(d.colorWrite?GL_TRUE:GL_FALSE,d.colorWrite?GL_TRUE:GL_FALSE,d.colorWrite?GL_TRUE:GL_FALSE,d.alphaWrite?GL_TRUE:GL_FALSE);
  // GX destination alpha replaces the alpha value written to the EFB even when RGB is blended.
  // Preserve the RGB equation/factors, but force alpha to src*1 + dst*0 when dstAlpha is enabled.
  const bool forceDstAlpha=d.dstAlpha>=0&&d.alphaWrite;
  if(d.blendMode==BlendMode::None){glDisable(GL_BLEND);}else{glEnable(GL_BLEND);if(d.blendMode==BlendMode::Subtract){
      glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT,forceDstAlpha?GL_FUNC_ADD:GL_FUNC_REVERSE_SUBTRACT);
      glBlendFuncSeparate(GL_ONE,GL_ONE,forceDstAlpha?GL_ONE:GL_ONE,forceDstAlpha?GL_ZERO:GL_ONE);
    }else if(d.blendMode==BlendMode::Logic){
      glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);GLenum sr=GL_ONE,dr=GL_ZERO;switch(d.logicOp){case LogicOp::Clear:sr=GL_ZERO;dr=GL_ZERO;break;case LogicOp::Noop:sr=GL_ZERO;dr=GL_ONE;break;case LogicOp::Copy:default:break;}
      glBlendFuncSeparate(sr,dr,forceDstAlpha?GL_ONE:sr,forceDstAlpha?GL_ZERO:dr);
    }else{
      glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);const GLenum sr=blend_factor(d.srcFactor),dr=blend_factor(d.dstFactor);
      glBlendFuncSeparate(sr,dr,forceDstAlpha?GL_ONE:sr,forceDstAlpha?GL_ZERO:dr);
    }}
  if(d.polygonOffset){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(d.polygonOffsetFactor,d.polygonOffsetUnits);}else glDisable(GL_POLYGON_OFFSET_FILL);
}
#endif
}
PipelineCache::~PipelineCache(){clear();}

void PipelineCache::destroy_pipeline(CompiledPipeline& p) noexcept {
#if defined(__vita__)
  if(p.program) glDeleteProgram(p.program);
#else
  (void)p;
#endif
}

bool PipelineCache::evict_one() noexcept {
  if(map_.empty()) return false;
  auto victim=map_.end();
  for(auto it=map_.begin();it!=map_.end();++it){
    if(pinned_.contains(it->first)) continue;
    if(it->first==bound_ && map_.size()>1) continue;
    if(victim==map_.end() || it->second.lastUsed<victim->second.lastUsed) victim=it;
  }
  if(victim==map_.end()) {
    for(auto it=map_.begin();it!=map_.end();++it) {
      if(!pinned_.contains(it->first)) { victim=it; break; }
    }
  }
  if(victim==map_.end()) return false;
  if(victim->first==bound_){
#if defined(__vita__)
    glUseProgram(0);
#endif
    bound_=0;
  }
  destroy_pipeline(victim->second);
  map_.erase(victim);
  ++evictions_;
  return true;
}

void PipelineCache::set_max_entries(size_t maxEntries) noexcept {
  maxEntries_=maxEntries;
  trim_to_budget();
}

void PipelineCache::trim_to_budget() noexcept {
  while(maxEntries_!=0 && map_.size()>maxEntries_) {
    if(!evict_one()) break;
  }
}

const CompiledPipeline* PipelineCache::get_or_create(const PipelineDesc& d,FrameStats* st) noexcept {
  const uint64_t k=pipeline_key(d);
  auto it=map_.find(k);
  if(it!=map_.end()){
    it->second.lastUsed=++useSequence_;
    if(st)st->pipelineHits++;
    return &it->second;
  }
  if(st)st->pipelineMisses++;
  if(maxEntries_!=0 && map_.size()>=maxEntries_) (void)evict_one();
  CompiledPipeline p{};
  p.key=k;
  p.lastUsed=++useSequence_;
  p.desc=d;
  p.uTex.fill(-1);
#if defined(__vita__)
  auto src=build_tev_glsl(d);
  std::string shaderDiagnostics;
  p.program=link_program(src.vertex.c_str(),src.fragment.c_str(),&shaderDiagnostics);
  if(!p.program){++compileFailures_;std::printf("[aurora-vita] pipeline compile failed key=%llx\n",(unsigned long long)k);dump_pipeline_failure(k,d,src,shaderDiagnostics);return nullptr;}
  p.uMvp=glGetUniformLocation(p.program,"u_mvp");
  p.uKColor=glGetUniformLocation(p.program,"u_kcolor");
  p.uTevReg=glGetUniformLocation(p.program,"u_tevreg");
  p.uFogColor=glGetUniformLocation(p.program,"u_fog_color");
  p.uFogParams=glGetUniformLocation(p.program,"u_fog_params");
  p.uFogRangeK=glGetUniformLocation(p.program,"u_fog_range_k");
  p.uRenderViewportWidth=glGetUniformLocation(p.program,"u_render_viewport_width");
  p.uIndMtx=glGetUniformLocation(p.program,"u_ind_mtx");
  p.uTexcoordScale=glGetUniformLocation(p.program,"u_texcoord_scale");
  p.uTextureSizeBias=glGetUniformLocation(p.program,"u_texture_size_bias");
  glUseProgram(p.program);
  for(unsigned i=0;i<MaxTextures;i++){
    char n[16];std::snprintf(n,sizeof(n),"u_tex%u",i);
    p.uTex[i]=glGetUniformLocation(p.program,n);
    if(p.uTex[i]>=0)glUniform1i(p.uTex[i],i);
  }
#else
  p.program=static_cast<unsigned>(useSequence_);
#endif
  auto [ni,_]=map_.emplace(k,std::move(p));
  if(map_.size()>highWaterEntries_)highWaterEntries_=map_.size();
  return &ni->second;
}

const CompiledPipeline* PipelineCache::find(uint64_t key) noexcept {
  auto it=map_.find(key);
  if(it==map_.end()) return nullptr;
  it->second.lastUsed=++useSequence_;
  return &it->second;
}

void PipelineCache::bind(const CompiledPipeline&p,const DrawUniforms&u,FrameStats*st) noexcept {
#if defined(__vita__)
  if(bound_!=p.key){glUseProgram(p.program);fixed(p.desc);bound_=p.key;if(st)st->stateChanges++;}
  auto changed=[&](uint16_t bit,const void* a,const void* b,size_t n) noexcept {
    return (p.uniformValidMask&bit)==0 || std::memcmp(a,b,n)!=0;
  };
  if(p.uMvp>=0 && changed(1u<<0,u.mvp.data(),p.cachedUniforms.mvp.data(),sizeof(u.mvp)))glUniformMatrix4fv(p.uMvp,1,GL_FALSE,u.mvp.data());
  if(p.uKColor>=0 && changed(1u<<1,u.kcolor.data(),p.cachedUniforms.kcolor.data(),sizeof(u.kcolor)))glUniform4fv(p.uKColor,4,u.kcolor[0].data());
  if(p.uTevReg>=0 && changed(1u<<2,u.tevreg.data(),p.cachedUniforms.tevreg.data(),sizeof(u.tevreg)))glUniform4fv(p.uTevReg,4,u.tevreg[0].data());
  if(p.uFogColor>=0 && changed(1u<<3,u.fogColor.data(),p.cachedUniforms.fogColor.data(),sizeof(u.fogColor)))glUniform4fv(p.uFogColor,1,u.fogColor.data());
  if(p.uFogParams>=0 && changed(1u<<4,u.fogParams.data(),p.cachedUniforms.fogParams.data(),sizeof(u.fogParams)))glUniform4fv(p.uFogParams,1,u.fogParams.data());
  if(p.uFogRangeK>=0 && changed(1u<<5,u.fogRangeK.data(),p.cachedUniforms.fogRangeK.data(),sizeof(u.fogRangeK)))glUniform1fv(p.uFogRangeK,10,u.fogRangeK.data());
  if(p.uRenderViewportWidth>=0 && changed(1u<<6,&u.renderViewportWidth,&p.cachedUniforms.renderViewportWidth,sizeof(u.renderViewportWidth)))glUniform1f(p.uRenderViewportWidth,u.renderViewportWidth);
  if(p.uIndMtx>=0 && changed(1u<<7,u.indirectMatrices.data(),p.cachedUniforms.indirectMatrices.data(),sizeof(u.indirectMatrices)))glUniform4fv(p.uIndMtx,MaxIndMatrices*2,u.indirectMatrices[0].data());
  if(p.uTexcoordScale>=0 && changed(1u<<8,u.texcoordScale.data(),p.cachedUniforms.texcoordScale.data(),sizeof(u.texcoordScale)))glUniform4fv(p.uTexcoordScale,MaxTextures,u.texcoordScale[0].data());
  if(p.uTextureSizeBias>=0 && changed(1u<<9,u.textureSizeBias.data(),p.cachedUniforms.textureSizeBias.data(),sizeof(u.textureSizeBias)))glUniform4fv(p.uTextureSizeBias,MaxTextures,u.textureSizeBias[0].data());
  p.cachedUniforms=u;
  p.uniformValidMask=0x03ffu;
#else
  (void)u;
  if(bound_!=p.key){bound_=p.key;if(st)st->stateChanges++;}
#endif
}

void PipelineCache::clear() noexcept {
  for(auto&[k,p]:map_){(void)k;destroy_pipeline(p);}
  map_.clear();
  pinned_.clear();
  bound_=0;
}
} // namespace aurora::vita::gfx
