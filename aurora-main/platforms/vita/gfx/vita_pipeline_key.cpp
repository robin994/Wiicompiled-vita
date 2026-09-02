#include "vita_pipeline_key.hpp"
#include "vita_hash.hpp"
namespace aurora::vita::gfx {
namespace { template<class T> void add(uint64_t& h,const T& v){h=hash_combine(h,hash_pod(v));} }
uint64_t pipeline_key(const PipelineDesc& d) noexcept {
  uint64_t h=0xcbf29ce484222325ull;
  add(h,d.primitive);add(h,d.depthFunc);add(h,d.cull);add(h,d.blendMode);add(h,d.srcFactor);add(h,d.dstFactor);add(h,d.logicOp);
  add(h,d.depthTest);add(h,d.depthWrite);add(h,d.colorWrite);add(h,d.alphaWrite);add(h,d.reversedZ);add(h,d.polygonOffset);add(h,d.polygonOffsetFactor);add(h,d.polygonOffsetUnits);add(h,d.dstAlpha);add(h,d.fogMode);add(h,d.fogOrthographic);add(h,d.fogRangeEnabled);add(h,d.positionIsClipSpace);
  add(h,d.layout.count);for(unsigned i=0;i<d.layout.count&&i<MaxVertexAttributes;i++){const auto&a=d.layout.attributes[i];add(h,a.location);add(h,a.components);add(h,a.scalar);add(h,a.normalized);add(h,a.stride);add(h,a.offset);}
  add(h,d.texgenCount);for(unsigned i=0;i<d.texgenCount&&i<MaxTextures;i++){const auto&t=d.texgens[i];add(h,t.type);add(h,t.source);add(h,t.matrix);add(h,t.postMatrix);add(h,t.embossSource);add(h,t.normalize);add(h,t.matrixFromVertex);}
  for(const auto&c:d.colorChannels){add(h,c.materialSource);add(h,c.ambientSource);add(h,c.diffuse);add(h,c.attenuation);add(h,c.lightingEnabled);}
  add(h,d.tev.stageCount);add(h,d.tev.texCoordCount);add(h,d.tev.rasterColorCount);add(h,d.tev.indirectStageCount);
  for(const auto&s:d.tev.swapTable){add(h,s.r);add(h,s.g);add(h,s.b);add(h,s.a);}
  for(unsigned i=0;i<d.tev.indirectStageCount&&i<MaxIndStages;i++){const auto&s=d.tev.indirectStages[i];add(h,s.texCoord);add(h,s.texture);add(h,s.scaleSShift);add(h,s.scaleTShift);}
  add(h,d.tev.alphaCompare.comp0);add(h,d.tev.alphaCompare.ref0);add(h,d.tev.alphaCompare.op);add(h,d.tev.alphaCompare.comp1);add(h,d.tev.alphaCompare.ref1);
  for(unsigned i=0;i<d.tev.stageCount&&i<MaxTevStages;i++){
    const auto&s=d.tev.stages[i];
    add(h,s.color.a);add(h,s.color.b);add(h,s.color.c);add(h,s.color.d);add(h,s.alpha.a);add(h,s.alpha.b);add(h,s.alpha.c);add(h,s.alpha.d);
    add(h,s.colorOp);add(h,s.alphaOp);add(h,s.colorBias);add(h,s.alphaBias);add(h,s.colorScale);add(h,s.alphaScale);add(h,s.colorOut);add(h,s.alphaOut);add(h,s.konstColor);add(h,s.konstAlpha);add(h,s.texture);add(h,s.texCoord);add(h,s.rasterSource);add(h,s.rasSwap);add(h,s.texSwap);add(h,s.colorClamp);add(h,s.alphaClamp);add(h,s.indirectEnabled);add(h,s.indirectStage);add(h,s.indirectFormat);add(h,s.indirectBias);add(h,s.indirectAlpha);add(h,s.indirectMatrix);add(h,s.indirectWrapS);add(h,s.indirectWrapT);add(h,s.indirectUseOrigLod);add(h,s.indirectAddPrev);
  }
  return h;
}
uint64_t texture_key(const TextureDesc& d) noexcept {
  uint64_t h=0xcbf29ce484222325ull;add(h,d.sourceId);add(h,d.paletteSourceId);add(h,d.revision);add(h,d.paletteRevision);add(h,d.width);add(h,d.height);add(h,d.format);add(h,d.paletteFormat);add(h,d.mipCount);add(h,d.cacheable);add(h,d.generateMipmaps);
  if(!d.sourceId){const auto p=reinterpret_cast<uintptr_t>(d.data);add(h,p);}return h;
}
} // namespace aurora::vita::gfx
