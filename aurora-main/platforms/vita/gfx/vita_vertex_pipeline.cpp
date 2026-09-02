#include "vita_vertex_pipeline.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurora::vita::gfx {
namespace {
struct V3 { float x=0,y=0,z=0; };
struct V4 { float x=0,y=0,z=0,w=0; };
V3 make3(const float v[3]) noexcept{return {v[0],v[1],v[2]};}
V4 make_color(const uint8_t c[4]) noexcept{return {c[0]/255.f,c[1]/255.f,c[2]/255.f,c[3]/255.f};}
V3 sub(V3 a,V3 b) noexcept{return {a.x-b.x,a.y-b.y,a.z-b.z};}
V3 mul(V3 a,float s) noexcept{return {a.x*s,a.y*s,a.z*s};}
V4 add(V4 a,V4 b) noexcept{return {a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w};}
V4 mul(V4 a,V4 b) noexcept{return {a.x*b.x,a.y*b.y,a.z*b.z,a.w*b.w};}
V4 mul(V4 a,float s) noexcept{return {a.x*s,a.y*s,a.z*s,a.w*s};}
float dot(V3 a,V3 b) noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
float len(V3 a) noexcept{return std::sqrt(std::max(dot(a,a),0.f));}
V3 norm(V3 a) noexcept{const float l=len(a);return l>1e-10f?mul(a,1.f/l):V3{};}
V4 clamp01(V4 a) noexcept{return {std::clamp(a.x,0.f,1.f),std::clamp(a.y,0.f,1.f),std::clamp(a.z,0.f,1.f),std::clamp(a.w,0.f,1.f)};}
uint8_t byte(float f) noexcept{return static_cast<uint8_t>(std::clamp(std::lround(f*255.f),0l,255l));}

V3 transform(const Matrix3x4& m,V4 p) noexcept {
  // Each four-float group is one result column: result = row-vector(p) * mat3x4.
  return {p.x*m.v[0]+p.y*m.v[1]+p.z*m.v[2]+p.w*m.v[3],
          p.x*m.v[4]+p.y*m.v[5]+p.z*m.v[6]+p.w*m.v[7],
          p.x*m.v[8]+p.y*m.v[9]+p.z*m.v[10]+p.w*m.v[11]};
}
V3 transform_dir(const Matrix3x4&m,V3 p) noexcept{return transform(m,{p.x,p.y,p.z,0.f});}

V4 vertex_color(const CanonicalVertex&v,unsigned channel) noexcept{return make_color(channel&1?v.color1:v.color0);}
V4 reg_color(const std::array<std::array<float,4>,4>& a,unsigned ch) noexcept{return {a[ch][0],a[ch][1],a[ch][2],a[ch][3]};}
V3 light_vec3(const std::array<float,4>&a) noexcept{return {a[0],a[1],a[2]};}
V4 light_vec4(const std::array<float,4>&a) noexcept{return {a[0],a[1],a[2],a[3]};}

V4 light_channel(const CanonicalVertex&in,const ColorChannelDesc&cc,unsigned ch,const VertexTransformState&st,V3 mvPos,V3 mvNrm) noexcept {
  const V4 vcol=vertex_color(in,ch);
  const V4 amb=cc.ambientSource==ColorSource::Vertex?vcol:reg_color(st.channelAmbient,ch);
  const V4 mat=cc.materialSource==ColorSource::Vertex?vcol:reg_color(st.channelMaterial,ch);
  if(!cc.lightingEnabled)return mat;
  V4 lighting=amb;
  for(unsigned i=0;i<MaxLights;i++){
    if(!st.lightEnabled[ch][i])continue;
    const auto&lu=st.lights[i];
    V3 ldir=sub(light_vec3(lu.position),mvPos);const float dist2=dot(ldir,ldir);const float dist=std::sqrt(std::max(dist2,1e-20f));ldir=mul(ldir,1.f/dist);
    float attn=1.f;
    if(cc.attenuation==AttenuationFn::Spot){
      const float cosine=std::max(0.f,dot(ldir,light_vec3(lu.direction)));const auto ca=light_vec3(lu.cosAtt),da=light_vec3(lu.distAtt);const float cosAtt=ca.x+ca.y*cosine+ca.z*cosine*cosine;const float distAtt=da.x+da.y*dist+da.z*dist2;attn=distAtt!=0.f?std::max(0.f,cosAtt/distAtt):0.f;
    }else if(cc.attenuation==AttenuationFn::Specular){
      float spec=dot(mvNrm,ldir)>=0.f?std::max(0.f,dot(mvNrm,light_vec3(lu.direction))):0.f;const auto ca=light_vec3(lu.cosAtt);auto da=light_vec3(lu.distAtt);const float cosAtt=ca.x+ca.y*spec+ca.z*spec*spec;if(cc.diffuse!=DiffuseFn::None)da=norm(da);const float distAtt=std::max(0.f,da.x+da.y*spec+da.z*spec*spec);attn=distAtt!=0.f?std::max(0.f,cosAtt/distAtt):0.f;
    }
    float diff=1.f;if(cc.diffuse==DiffuseFn::Signed)diff=dot(ldir,mvNrm);else if(cc.diffuse==DiffuseFn::Clamp)diff=std::max(0.f,dot(ldir,mvNrm));
    lighting=add(lighting,mul(light_vec4(lu.color),attn*diff));
  }
  return mul(mat,clamp01(lighting));
}

V4 tex_source(const CanonicalVertex&in,TexGenSource s) noexcept {
  switch(s){
  case TexGenSource::Position:return {in.position[0],in.position[1],in.position[2],1};
  case TexGenSource::Normal:return {in.normal[0],in.normal[1],in.normal[2],1};
  case TexGenSource::Binormal:return {in.binormal[0],in.binormal[1],in.binormal[2],1};
  case TexGenSource::Tangent:return {in.tangent[0],in.tangent[1],in.tangent[2],1};
  case TexGenSource::Color0:return make_color(in.color0);
  case TexGenSource::Color1:return make_color(in.color1);
  default:{unsigned i=static_cast<unsigned>(s)-static_cast<unsigned>(TexGenSource::Tex0);if(i<8)return {in.texcoord[i][0],in.texcoord[i][1],1,1};break;}
  }
  return {0,0,1,1};
}

bool is_bump(TexGenType t) noexcept{return t>=TexGenType::Bump0&&t<=TexGenType::Bump7;}
unsigned bump_light(TexGenType t) noexcept{return static_cast<unsigned>(t)-static_cast<unsigned>(TexGenType::Bump0);}
V3 normalize3(V3 v) noexcept{return norm(v);}

} // namespace

bool run_vertex_pipeline(std::vector<CanonicalVertex>& vertices,const PipelineDesc& pipeline,const VertexTransformState& state,DrawUniforms* uniforms) noexcept {
  if(uniforms)uniforms->mvp=state.projection;
  for(auto&v:vertices){
    const CanonicalVertex in=v;
    const unsigned pn=in.pnMatrixIndex==0xff?state.currentPnMatrix:in.pnMatrixIndex;if(pn>=10)return false;
    const V3 mvPos=transform(state.postexMatrices[pn],{in.position[0],in.position[1],in.position[2],1.f});
    V3 mvNrm=transform_dir(state.normalMatrices[pn],make3(in.normal));if(len(mvNrm)>1e-10f)mvNrm=norm(mvNrm);
    V3 mvBin=transform_dir(state.normalMatrices[pn],make3(in.binormal));if(len(mvBin)>1e-10f)mvBin=norm(mvBin);
    V3 mvTan=transform_dir(state.normalMatrices[pn],make3(in.tangent));if(len(mvTan)>1e-10f)mvTan=norm(mvTan);
    v.position[0]=mvPos.x;v.position[1]=mvPos.y;v.position[2]=mvPos.z;v.normal[0]=mvNrm.x;v.normal[1]=mvNrm.y;v.normal[2]=mvNrm.z;v.binormal[0]=mvBin.x;v.binormal[1]=mvBin.y;v.binormal[2]=mvBin.z;v.tangent[0]=mvTan.x;v.tangent[1]=mvTan.y;v.tangent[2]=mvTan.z;
    for(unsigned base=0;base<2;base++){
      const V4 rgb=light_channel(in,pipeline.colorChannels[base],base,state,mvPos,mvNrm);const V4 alpha=light_channel(in,pipeline.colorChannels[base+2],base+2,state,mvPos,mvNrm);uint8_t*out=base?v.color1:v.color0;out[0]=byte(rgb.x);out[1]=byte(rgb.y);out[2]=byte(rgb.z);out[3]=byte(alpha.w);
    }
    for(unsigned i=0;i<pipeline.texgenCount&&i<MaxTextures;i++){
      const auto&t=pipeline.texgens[i];
      if(is_bump(t.type)){
        const unsigned src=std::min<unsigned>(t.embossSource,MaxTextures-1),li=std::min<unsigned>(bump_light(t.type),MaxLights-1);V3 ldir=norm(sub(light_vec3(state.lights[li].position),mvPos));v.texcoord[i][0]=v.texcoord[src][0]+dot(ldir,mvTan);v.texcoord[i][1]=v.texcoord[src][1]+dot(ldir,mvBin);v.texcoord[i][2]=1.f;continue;
      }
      V4 src=tex_source(in,t.source);V3 tmp{};
      if(t.type==TexGenType::SRTG){tmp={src.x,src.y,1.f};}
      else if(t.matrixFromVertex&&in.texMatrixIndex[i]!=0xff){const unsigned mi=in.texMatrixIndex[i]/3;if(mi>=state.postexMatrices.size())return false;tmp=transform(state.postexMatrices[mi],src);}
      else if(t.matrix<0){tmp={src.x,src.y,src.z};}
      else {const unsigned mi=10u+static_cast<unsigned>(t.matrix);if(mi>=state.postexMatrices.size())return false;tmp=transform(state.postexMatrices[mi],src);}
      if(t.type==TexGenType::Matrix2x4) tmp.z=1.f;
      if(t.normalize) tmp=normalize3(tmp);
      if(t.postMatrix>=0){const unsigned pi=static_cast<unsigned>(t.postMatrix);if(pi>=state.postMatrices.size())return false;tmp=transform(state.postMatrices[pi],{tmp.x,tmp.y,tmp.z,1.f});}
      v.texcoord[i][0]=tmp.x;v.texcoord[i][1]=tmp.y;v.texcoord[i][2]=t.type==TexGenType::Matrix3x4?tmp.z:1.f;
    }
  }
  return true;
}

} // namespace aurora::vita::gfx
