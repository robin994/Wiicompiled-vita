#include "vita_shader_gen.hpp"
#include "vita_pipeline_key.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

namespace aurora::vita::gfx {
namespace {

const char* color_reg(TevReg r) {
  switch (r) {
  case TevReg::Prev: return "prev";
  case TevReg::Reg0: return "reg0";
  case TevReg::Reg1: return "reg1";
  case TevReg::Reg2: return "reg2";
  }
  return "prev";
}

const char* channel(TevChannel c) {
  switch (c) {
  case TevChannel::Red: return "r";
  case TevChannel::Green: return "g";
  case TevChannel::Blue: return "b";
  case TevChannel::Alpha: return "a";
  }
  return "r";
}

std::string swapped(std::string_view value, const TevSwapDesc& sw) {
  return "vec4(" + std::string(value) + "." + channel(sw.r) + "," + std::string(value) + "." + channel(sw.g) +
         "," + std::string(value) + "." + channel(sw.b) + "," + std::string(value) + "." + channel(sw.a) + ")";
}

std::string texcoord_expr(const PipelineDesc& desc, unsigned idx) {
  if (idx >= MaxTextures) return "vec2(0.0)";
  if (idx < desc.texgenCount && desc.texgens[idx].type == TexGenType::Matrix3x4) {
    return "(v_tex" + std::to_string(idx) + ".xy / v_tex" + std::to_string(idx) + ".z)";
  }
  return "v_tex" + std::to_string(idx) + ".xy";
}

std::string fmtcmp(Compare c, std::string_view lhs, std::string_view rhs) {
  const char* op = "==";
  switch (c) {
  case Compare::Never: return "false";
  case Compare::Always: return "true";
  case Compare::Less: op = "<"; break;
  case Compare::Equal: op = "=="; break;
  case Compare::LessEqual: op = "<="; break;
  case Compare::Greater: op = ">"; break;
  case Compare::NotEqual: op = "!="; break;
  case Compare::GreaterEqual: op = ">="; break;
  }
  return "(" + std::string(lhs) + " " + op + " " + std::string(rhs) + ")";
}

std::string q8(std::string_view v) { return "floor((" + std::string(v) + ")*255.0+0.5)"; }

std::string kcolor(KonstColorSel k) {
  switch (k) {
  case KonstColorSel::One: return "vec3(1.0)";
  case KonstColorSel::SevenEighths: return "vec3(0.875)";
  case KonstColorSel::SixEighths: return "vec3(0.75)";
  case KonstColorSel::FiveEighths: return "vec3(0.625)";
  case KonstColorSel::FourEighths: return "vec3(0.5)";
  case KonstColorSel::ThreeEighths: return "vec3(0.375)";
  case KonstColorSel::TwoEighths: return "vec3(0.25)";
  case KonstColorSel::OneEighth: return "vec3(0.125)";
  case KonstColorSel::K0: return "u_kcolor[0].rgb";
  case KonstColorSel::K1: return "u_kcolor[1].rgb";
  case KonstColorSel::K2: return "u_kcolor[2].rgb";
  case KonstColorSel::K3: return "u_kcolor[3].rgb";
  default: break;
  }
  int idx = 0;
  char comp = 'r';
  const int v = static_cast<int>(k);
  if (v >= static_cast<int>(KonstColorSel::K0R) && v <= static_cast<int>(KonstColorSel::K3R)) {
    idx = v - static_cast<int>(KonstColorSel::K0R); comp = 'r';
  } else if (v >= static_cast<int>(KonstColorSel::K0G) && v <= static_cast<int>(KonstColorSel::K3G)) {
    idx = v - static_cast<int>(KonstColorSel::K0G); comp = 'g';
  } else if (v >= static_cast<int>(KonstColorSel::K0B) && v <= static_cast<int>(KonstColorSel::K3B)) {
    idx = v - static_cast<int>(KonstColorSel::K0B); comp = 'b';
  } else if (v >= static_cast<int>(KonstColorSel::K0A) && v <= static_cast<int>(KonstColorSel::K3A)) {
    idx = v - static_cast<int>(KonstColorSel::K0A); comp = 'a';
  }
  char b[64];
  std::snprintf(b, sizeof(b), "vec3(u_kcolor[%d].%c)", idx, comp);
  return b;
}

std::string kalpha(KonstAlphaSel k) {
  switch (k) {
  case KonstAlphaSel::One: return "1.0";
  case KonstAlphaSel::SevenEighths: return "0.875";
  case KonstAlphaSel::SixEighths: return "0.75";
  case KonstAlphaSel::FiveEighths: return "0.625";
  case KonstAlphaSel::FourEighths: return "0.5";
  case KonstAlphaSel::ThreeEighths: return "0.375";
  case KonstAlphaSel::TwoEighths: return "0.25";
  case KonstAlphaSel::OneEighth: return "0.125";
  default: break;
  }
  int idx = 0;
  char comp = 'r';
  const int v = static_cast<int>(k);
  const int r0 = static_cast<int>(KonstAlphaSel::K0R);
  const int g0 = static_cast<int>(KonstAlphaSel::K0G);
  const int b0 = static_cast<int>(KonstAlphaSel::K0B);
  const int a0 = static_cast<int>(KonstAlphaSel::K0A);
  if (v >= r0 && v < r0 + 4) { idx = v - r0; comp = 'r'; }
  else if (v >= g0 && v < g0 + 4) { idx = v - g0; comp = 'g'; }
  else if (v >= b0 && v < b0 + 4) { idx = v - b0; comp = 'b'; }
  else if (v >= a0 && v < a0 + 4) { idx = v - a0; comp = 'a'; }
  char b[48];
  std::snprintf(b, sizeof(b), "u_kcolor[%d].%c", idx, comp);
  return b;
}

std::string carg(TevColorArg a, const TevStage& s) {
  switch (a) {
  case TevColorArg::Prev: return "prev.rgb";
  case TevColorArg::PrevA: return "vec3(prev.a)";
  case TevColorArg::Reg0: return "reg0.rgb";
  case TevColorArg::Reg0A: return "vec3(reg0.a)";
  case TevColorArg::Reg1: return "reg1.rgb";
  case TevColorArg::Reg1A: return "vec3(reg1.a)";
  case TevColorArg::Reg2: return "reg2.rgb";
  case TevColorArg::Reg2A: return "vec3(reg2.a)";
  case TevColorArg::TexColor: return s.texture == 0xff ? "vec3(1.0)" : "texc.rgb";
  case TevColorArg::TexAlpha: return s.texture == 0xff ? "vec3(1.0)" : "vec3(texc.a)";
  case TevColorArg::RasColor: return "rasc.rgb";
  case TevColorArg::RasAlpha: return "vec3(rasc.a)";
  case TevColorArg::One: return "vec3(1.0)";
  case TevColorArg::Half: return "vec3(0.5)";
  case TevColorArg::Konst: return kcolor(s.konstColor);
  case TevColorArg::Zero: return "vec3(0.0)";
  }
  return "vec3(0.0)";
}

std::string aarg(TevAlphaArg a, const TevStage& s) {
  switch (a) {
  case TevAlphaArg::PrevA: return "prev.a";
  case TevAlphaArg::Reg0A: return "reg0.a";
  case TevAlphaArg::Reg1A: return "reg1.a";
  case TevAlphaArg::Reg2A: return "reg2.a";
  case TevAlphaArg::TexAlpha: return s.texture == 0xff ? "1.0" : "texc.a";
  case TevAlphaArg::RasAlpha: return "rasc.a";
  case TevAlphaArg::Konst: return kalpha(s.konstAlpha);
  case TevAlphaArg::Zero: return "0.0";
  }
  return "0.0";
}

const char* bias(TevBias b) {
  switch (b) { case TevBias::Zero: return ""; case TevBias::AddHalf: return "+ 0.5"; case TevBias::SubHalf: return "- 0.5"; }
  return "";
}
const char* scale(TevScale s) {
  switch (s) { case TevScale::Scale1: return "1.0"; case TevScale::Scale2: return "2.0"; case TevScale::Scale4: return "4.0"; case TevScale::Divide2: return "0.5"; }
  return "1.0";
}

bool color_arg_normalized(TevColorArg a,const std::array<bool,4>& cn,const std::array<bool,4>& an) {
  switch(a){
  case TevColorArg::Prev:return cn[0]; case TevColorArg::PrevA:return an[0];
  case TevColorArg::Reg0:return cn[1]; case TevColorArg::Reg0A:return an[1];
  case TevColorArg::Reg1:return cn[2]; case TevColorArg::Reg1A:return an[2];
  case TevColorArg::Reg2:return cn[3]; case TevColorArg::Reg2A:return an[3];
  default:return true;
  }
}
bool alpha_arg_normalized(TevAlphaArg a,const std::array<bool,4>& an) {
  switch(a){case TevAlphaArg::PrevA:return an[0];case TevAlphaArg::Reg0A:return an[1];case TevAlphaArg::Reg1A:return an[2];case TevAlphaArg::Reg2A:return an[3];default:return true;}
}
std::string color_arg_expr(TevColorArg a,const TevStage&s,const std::array<bool,4>&cn,const std::array<bool,4>&an){
  auto v=carg(a,s);return color_arg_normalized(a,cn,an)?v:"tev_overflow3("+v+")";
}
std::string alpha_arg_expr(TevAlphaArg a,const TevStage&s,const std::array<bool,4>&an){
  auto v=aarg(a,s);return alpha_arg_normalized(a,an)?v:"tev_overflow1("+v+")";
}

std::string color_calc(const TevStage& s,const std::array<bool,4>&cn,const std::array<bool,4>&an) {
  const auto A=color_arg_expr(s.color.a,s,cn,an),B=color_arg_expr(s.color.b,s,cn,an),C=color_arg_expr(s.color.c,s,cn,an),D=color_arg_expr(s.color.d,s,cn,an);
  std::string e;
  switch (s.colorOp) {
  case TevOp::Add: e = "(" + D + " + mix(" + A + ", " + B + ", " + C + ") " + bias(s.colorBias) + ") * " + scale(s.colorScale); break;
  case TevOp::Sub: e = "(" + D + " - mix(" + A + ", " + B + ", " + C + ") " + bias(s.colorBias) + ") * " + scale(s.colorScale); break;
  case TevOp::CompR8Greater: e = D + " + ((" + q8(A + ".r") + " > " + q8(B + ".r") + ") ? " + C + " : vec3(0.0))"; break;
  case TevOp::CompR8Equal: e = D + " + ((" + q8(A + ".r") + " == " + q8(B + ".r") + ") ? " + C + " : vec3(0.0))"; break;
  case TevOp::CompGR16Greater: e = D + " + ((floor(dot(" + A + ".rg*255.0,vec2(1.0,256.0))+0.5) > floor(dot(" + B + ".rg*255.0,vec2(1.0,256.0))+0.5)) ? " + C + " : vec3(0.0))"; break;
  case TevOp::CompGR16Equal: e = D + " + ((floor(dot(" + A + ".rg*255.0,vec2(1.0,256.0))+0.5) == floor(dot(" + B + ".rg*255.0,vec2(1.0,256.0))+0.5)) ? " + C + " : vec3(0.0))"; break;
  case TevOp::CompBGR24Greater: e = D + " + ((floor(dot(" + A + ".rgb*255.0,vec3(1.0,256.0,65536.0))+0.5) > floor(dot(" + B + ".rgb*255.0,vec3(1.0,256.0,65536.0))+0.5)) ? " + C + " : vec3(0.0))"; break;
  case TevOp::CompBGR24Equal: e = D + " + ((floor(dot(" + A + ".rgb*255.0,vec3(1.0,256.0,65536.0))+0.5) == floor(dot(" + B + ".rgb*255.0,vec3(1.0,256.0,65536.0))+0.5)) ? " + C + " : vec3(0.0))"; break;
  case TevOp::CompRGB8Greater: e = D + " + vec3((" + q8(A + ".r") + " > " + q8(B + ".r") + ") ? " + C + ".r : 0.0, (" + q8(A + ".g") + " > " + q8(B + ".g") + ") ? " + C + ".g : 0.0, (" + q8(A + ".b") + " > " + q8(B + ".b") + ") ? " + C + ".b : 0.0)"; break;
  case TevOp::CompRGB8Equal: e = D + " + vec3((" + q8(A + ".r") + " == " + q8(B + ".r") + ") ? " + C + ".r : 0.0, (" + q8(A + ".g") + " == " + q8(B + ".g") + ") ? " + C + ".g : 0.0, (" + q8(A + ".b") + " == " + q8(B + ".b") + ") ? " + C + ".b : 0.0)"; break;
  }
  return "clamp(" + e + ", " + (s.colorClamp ? std::string("0.0") : std::string("-4.0")) + ", " +
         (s.colorClamp ? std::string("1.0") : std::string("4.0")) + ")";
}

std::string alpha_calc(const TevStage& s,const std::array<bool,4>&an) {
  const auto A=alpha_arg_expr(s.alpha.a,s,an),B=alpha_arg_expr(s.alpha.b,s,an),C=alpha_arg_expr(s.alpha.c,s,an),D=alpha_arg_expr(s.alpha.d,s,an);
  std::string e;
  switch (s.alphaOp) {
  case TevOp::Add: e = "(" + D + " + mix(" + A + ", " + B + ", " + C + ") " + bias(s.alphaBias) + ") * " + scale(s.alphaScale); break;
  case TevOp::Sub: e = "(" + D + " - mix(" + A + ", " + B + ", " + C + ") " + bias(s.alphaBias) + ") * " + scale(s.alphaScale); break;
  case TevOp::CompR8Equal: case TevOp::CompGR16Equal: case TevOp::CompBGR24Equal: case TevOp::CompRGB8Equal:
    e = D + " + (("+q8(A)+" == "+q8(B)+") ? " + C + " : 0.0)"; break;
  default: e = D + " + (("+q8(A)+" > "+q8(B)+") ? " + C + " : 0.0)"; break;
  }
  return "clamp(" + e + ", " + (s.alphaClamp ? std::string("0.0") : std::string("-4.0")) + ", " +
         (s.alphaClamp ? std::string("1.0") : std::string("4.0")) + ")";
}

float indirect_divisor(IndirectFormat f) {
  switch (f) { case IndirectFormat::Bits8: return 1.f; case IndirectFormat::Bits5: return 8.f; case IndirectFormat::Bits4: return 16.f; case IndirectFormat::Bits3: return 32.f; }
  return 1.f;
}

bool bias_s(IndirectBias b) { return b == IndirectBias::S || b == IndirectBias::ST || b == IndirectBias::SU || b == IndirectBias::STU; }
bool bias_t(IndirectBias b) { return b == IndirectBias::T || b == IndirectBias::ST || b == IndirectBias::TU || b == IndirectBias::STU; }
bool bias_u(IndirectBias b) { return b == IndirectBias::U || b == IndirectBias::SU || b == IndirectBias::TU || b == IndirectBias::STU; }

std::string wrap_component(IndirectWrap w, std::string_view expr) {
  switch (w) {
  case IndirectWrap::Off: return std::string(expr);
  case IndirectWrap::W256: return "mod(" + std::string(expr) + ",256.0)";
  case IndirectWrap::W128: return "mod(" + std::string(expr) + ",128.0)";
  case IndirectWrap::W64: return "mod(" + std::string(expr) + ",64.0)";
  case IndirectWrap::W32: return "mod(" + std::string(expr) + ",32.0)";
  case IndirectWrap::W16: return "mod(" + std::string(expr) + ",16.0)";
  case IndirectWrap::W0: return "0.0";
  }
  return std::string(expr);
}

std::string raster_base(RasterSource source) {
  switch (source) {
  case RasterSource::Color0: return "v_color0";
  case RasterSource::Color1: return "v_color1";
  case RasterSource::AlphaBump: return "vec4(vec3(ind_alpha),ind_alpha)";
  case RasterSource::AlphaBumpN: return "vec4(vec3(min(ind_alpha*(255.0/248.0),1.0)),min(ind_alpha*(255.0/248.0),1.0))";
  case RasterSource::Zero: return "vec4(0.0)";
  }
  return "v_color0";
}

} // namespace

ShaderSources build_tev_glsl(const PipelineDesc& desc) noexcept {
  ShaderSources out;
  out.key = pipeline_key(desc);

  // Compact Vita vertices retain NDC for CPU diagnostics and carry W in a
  // separate attribute. Restore homogeneous position before GPU clipping and
  // interpolation. Canonical vertices already carry a four-component position.
  bool separateClipW = false;
  for (unsigned i=0;i<desc.layout.count && i<MaxVertexAttributes;++i)
    separateClipW |= desc.positionIsClipSpace && desc.layout.attributes[i].location==11 &&
                     desc.layout.attributes[i].components==1;

  std::ostringstream vs;
  vs << "precision highp float;\n"
        "attribute vec4 a_position;\nattribute vec4 a_color0;\nattribute vec4 a_color1;\n";
  for (unsigned i = 0; i < MaxTextures; i++) vs << "attribute vec3 a_tex" << i << ";\nvarying vec3 v_tex" << i << ";\n";
  if (separateClipW) vs << "attribute float a_clip_w;\n";
  vs << "varying vec4 v_color0;\nvarying vec4 v_color1;\nuniform mat4 u_mvp;\n"
        "void main(){ gl_Position=" << (separateClipW ? "vec4(a_position.xyz*a_clip_w,a_clip_w)" :
           desc.positionIsClipSpace ? "a_position" : "u_mvp*a_position") << "; v_color0=a_color0; v_color1=a_color1;";
  for (unsigned i = 0; i < MaxTextures; i++) vs << "v_tex" << i << "=a_tex" << i << ";";
  vs << "}\n";
  out.vertex = vs.str();

  std::ostringstream fs;
  fs << "precision highp float;\n";
  for (unsigned i = 0; i < MaxTextures; i++) fs << "uniform sampler2D u_tex" << i << "; varying vec3 v_tex" << i << ";\n";
  fs << "varying vec4 v_color0; varying vec4 v_color1;\n"
        "uniform vec4 u_kcolor[4]; uniform vec4 u_tevreg[4];\n"
        "uniform vec4 u_fog_color; uniform vec4 u_fog_params;\n"
        "uniform float u_fog_range_k[10]; uniform float u_render_viewport_width;\n"
        "float fog_range_k(float i){if(i<0.5)return u_fog_range_k[0];if(i<1.5)return u_fog_range_k[1];if(i<2.5)return u_fog_range_k[2];if(i<3.5)return u_fog_range_k[3];if(i<4.5)return u_fog_range_k[4];if(i<5.5)return u_fog_range_k[5];if(i<6.5)return u_fog_range_k[6];if(i<7.5)return u_fog_range_k[7];if(i<8.5)return u_fog_range_k[8];return u_fog_range_k[9];}\n"
        "uniform vec4 u_ind_mtx[6]; uniform vec4 u_texcoord_scale[8]; uniform vec4 u_texture_size_bias[8];\n"
        "float tev_overflow1(float x){float b=x*255.0;return (b-floor(b/256.0)*256.0)/255.0;}\n"
        "vec3 tev_overflow3(vec3 x){vec3 b=x*255.0;return (b-floor(b/256.0)*256.0)/255.0;}\n"
        "void main(){\n"
        " vec4 prev=u_tevreg[0], reg0=u_tevreg[1], reg1=u_tevreg[2], reg2=u_tevreg[3];\n"
        " vec2 prev_ind_uv=vec2(0.0);\n";

  const unsigned n = std::max(1u, std::min<unsigned>(desc.tev.stageCount, MaxTevStages));
  std::array<bool,4> colorNormalized{};
  std::array<bool,4> alphaNormalized{};
  for (unsigned i = 0; i < n; i++) {
    const auto& s = desc.tev.stages[i];
    fs << " {\n  float ind_alpha=0.0;\n  vec2 tev_uv=";
    if (s.texCoord < MaxTextures) fs << texcoord_expr(desc, s.texCoord);
    else fs << "vec2(0.0)";
    fs << ";\n";

    if (s.indirectEnabled && s.indirectStage < desc.tev.indirectStageCount && s.indirectStage < MaxIndStages) {
      const auto& ind = desc.tev.indirectStages[s.indirectStage];
      const unsigned itc = ind.texCoord < MaxTextures ? ind.texCoord : 0;
      const unsigned itex = ind.texture < MaxTextures ? ind.texture : 0;
      const float ss = 1.0f / static_cast<float>(1u << std::min<unsigned>(ind.scaleSShift, 8));
      const float st = 1.0f / static_cast<float>(1u << std::min<unsigned>(ind.scaleTShift, 8));
      fs << "  vec2 ind_uv=" << texcoord_expr(desc,itc) << "*max(u_texcoord_scale[" << itc << "].xy,vec2(1.0))*vec2(" << ss << "," << st << ")/max(u_texture_size_bias[" << itex << "].xy,vec2(1.0));\n"
            "  vec4 ind_sample=texture2D(u_tex" << itex << ",ind_uv);\n"
            "  vec3 ind_raw=vec3(ind_sample.a,ind_sample.b,ind_sample.g)*255.0;\n"
            "  vec3 indv=floor(ind_raw/" << indirect_divisor(s.indirectFormat) << ");\n";
      const float bv = s.indirectFormat == IndirectFormat::Bits8 ? -128.0f : 1.0f;
      if (bias_s(s.indirectBias)) fs << "  indv.x+=" << bv << ";\n";
      if (bias_t(s.indirectBias)) fs << "  indv.y+=" << bv << ";\n";
      if (bias_u(s.indirectBias)) fs << "  indv.z+=" << bv << ";\n";
      const float alphaStep = s.indirectFormat == IndirectFormat::Bits5 ? 32.0f : (s.indirectFormat == IndirectFormat::Bits4 ? 16.0f : 8.0f);
      switch (s.indirectAlpha) {
      case IndirectAlphaSel::Off: fs << "  ind_alpha=0.0;\n"; break;
      case IndirectAlphaSel::S: fs << "  ind_alpha=floor(ind_raw.x/" << alphaStep << ")*" << alphaStep << "/255.0;\n"; break;
      case IndirectAlphaSel::T: fs << "  ind_alpha=floor(ind_raw.y/" << alphaStep << ")*" << alphaStep << "/255.0;\n"; break;
      case IndirectAlphaSel::U: fs << "  ind_alpha=floor(ind_raw.z/" << alphaStep << ")*" << alphaStep << "/255.0;\n"; break;
      }
      const unsigned tc = s.texCoord < MaxTextures ? s.texCoord : 0;
      const unsigned tex = s.texture < MaxTextures ? s.texture : 0;
      const bool simple = s.indirectMatrix == IndirectMatrix::Off && !s.indirectAddPrev;
      if (simple) fs << "  vec2 base_texel=tev_uv;\n";
      else fs << "  vec2 base_texel=tev_uv*max(u_texcoord_scale[" << tc << "].xy,vec2(1.0));\n";
      fs << "  base_texel=vec2(" << wrap_component(s.indirectWrapS, "base_texel.x") << "," << wrap_component(s.indirectWrapT, "base_texel.y") << ");\n";
      switch (s.indirectMatrix) {
      case IndirectMatrix::Off: fs << "  vec2 ind_off=vec2(0.0);\n"; break;
      case IndirectMatrix::Mtx0: case IndirectMatrix::Mtx1: case IndirectMatrix::Mtx2: {
        const unsigned mi = static_cast<unsigned>(s.indirectMatrix) - static_cast<unsigned>(IndirectMatrix::Mtx0);
        fs << "  vec4 im0=u_ind_mtx[" << mi * 2 << "], im1=u_ind_mtx[" << mi * 2 + 1 << "]; vec2 ind_off=vec2(dot(vec3(im0.x,im0.z,im1.x),indv),dot(vec3(im0.y,im0.w,im1.y),indv))*im1.z;\n";
        break;
      }
      case IndirectMatrix::S0: case IndirectMatrix::S1: case IndirectMatrix::S2: {
        const unsigned mi = static_cast<unsigned>(s.indirectMatrix) - static_cast<unsigned>(IndirectMatrix::S0);
        fs << "  vec2 ind_off=" << texcoord_expr(desc,tc) << "*max(u_texcoord_scale[" << tc << "].xy,vec2(1.0))*indv.x*u_ind_mtx[" << mi * 2 + 1 << "].z/256.0;\n";
        break;
      }
      case IndirectMatrix::T0: case IndirectMatrix::T1: case IndirectMatrix::T2: {
        const unsigned mi = static_cast<unsigned>(s.indirectMatrix) - static_cast<unsigned>(IndirectMatrix::T0);
        fs << "  vec2 ind_off=" << texcoord_expr(desc,tc) << "*max(u_texcoord_scale[" << tc << "].xy,vec2(1.0))*indv.y*u_ind_mtx[" << mi * 2 + 1 << "].z/256.0;\n";
        break;
      }
      }
      fs << "  vec2 final_texel=base_texel+ind_off;\n";
      if (s.indirectAddPrev) fs << "  prev_ind_uv+=final_texel;\n"; else fs << "  prev_ind_uv=final_texel;\n";
      if (simple) fs << "  tev_uv=prev_ind_uv;\n"; else fs << "  tev_uv=prev_ind_uv/max(u_texture_size_bias[" << tex << "].xy,vec2(1.0));\n";
    }

    fs << "  vec4 raw_tex=";
    if (s.texture < MaxTextures) fs << "texture2D(u_tex" << unsigned(s.texture) << ",tev_uv)";
    else fs << "vec4(1.0)";
    const auto& ts = desc.tev.swapTable[std::min<unsigned>(s.texSwap, 3)];
    const auto& rs = desc.tev.swapTable[std::min<unsigned>(s.rasSwap, 3)];
    fs << "; vec4 texc=" << swapped("raw_tex", ts) << ";\n";
    fs << "  vec4 raw_ras=" << raster_base(s.rasterSource) << "; vec4 rasc=" << swapped("raw_ras", rs) << ";\n";
    fs << "  vec3 c=" << color_calc(s,colorNormalized,alphaNormalized) << "; float a=" << alpha_calc(s,alphaNormalized) << ";\n"
          "  " << color_reg(s.colorOut) << ".rgb=c; " << color_reg(s.alphaOut) << ".a=a;\n }\n";
    colorNormalized[static_cast<unsigned>(s.colorOut)]=s.colorClamp;
    alphaNormalized[static_cast<unsigned>(s.alphaOut)]=s.alphaClamp;
  }

  const auto& last = desc.tev.stages[n - 1];
  if (last.colorOut != TevReg::Prev) fs << " prev.rgb=" << color_reg(last.colorOut) << ".rgb;\n";
  if (last.alphaOut != TevReg::Prev) fs << " prev.a=" << color_reg(last.alphaOut) << ".a;\n";
  if(!colorNormalized[static_cast<unsigned>(last.colorOut)]) fs << " prev.rgb=tev_overflow3(prev.rgb);\n";
  if(!alphaNormalized[static_cast<unsigned>(last.alphaOut)]) fs << " prev.a=tev_overflow1(prev.a);\n";

  const auto& ac = desc.tev.alphaCompare;
  const std::string alpha255 = "floor(prev.a*255.0+0.5)";
  const std::string r0 = std::to_string(static_cast<unsigned>(ac.ref0)) + ".0";
  const std::string r1 = std::to_string(static_cast<unsigned>(ac.ref1)) + ".0";
  const auto c0 = fmtcmp(ac.comp0, alpha255, r0), c1 = fmtcmp(ac.comp1, alpha255, r1);
  std::string pass;
  switch (ac.op & 3) { case 0: pass = "(" + c0 + " && " + c1 + ")"; break; case 1: pass = "(" + c0 + " || " + c1 + ")"; break; case 2: pass = "(" + c0 + " != " + c1 + ")"; break; default: pass = "(" + c0 + " == " + c1 + ")"; break; }
  if (ac.comp0 != Compare::Always || ac.comp1 != Compare::Always) fs << " if(!" << pass << ") discard;\n";
  if (desc.dstAlpha >= 0) fs << " prev.a=" << (static_cast<float>(desc.dstAlpha) / 255.0f) << ";\n";
  if (desc.fogMode != FogMode::None) {
    fs << " { float fd=" << (desc.reversedZ ? "(1.0-gl_FragCoord.z)" : "gl_FragCoord.z") << "; float fb=";
    if (desc.fogOrthographic) fs << "u_fog_params.x*fd";
    else fs << "u_fog_params.x/max(u_fog_params.y-fd,0.000001)";
    if(desc.fogRangeEnabled){
      fs << "; float sx=((gl_FragCoord.x-0.5+0.5)/max(u_render_viewport_width,1.0))*2.0-1.0;"
            " float fo=sx-u_fog_params.w; float ri=clamp(9.0-abs(fo)*9.0,0.0,9.0);"
            " float lo=floor(ri); float hi=min(lo+1.0,9.0); float fr=ri-lo;"
            " float fk=max(mix(fog_range_k(lo),fog_range_k(hi),fr),0.000001);"
            " fb*=sqrt(fo*fo+fk*fk)/fk";
    }
    fs << "; float f=clamp(fb-u_fog_params.z,0.0,1.0);";
    switch (desc.fogMode) {
    case FogMode::Linear: break;
    case FogMode::Exp: fs << "f=1.0-exp2(-8.0*f);"; break;
    case FogMode::Exp2: fs << "f=1.0-exp2(-8.0*f*f);"; break;
    case FogMode::RevExp: fs << "f=exp2(-8.0*(1.0-f));"; break;
    case FogMode::RevExp2: fs << "f=1.0-f;f=exp2(-8.0*f*f);"; break;
    case FogMode::None: break;
    }
    fs << " f=clamp(f,0.0,1.0); prev.rgb=mix(prev.rgb,u_fog_color.rgb,f); }\n";
  }
  fs << " gl_FragColor=prev;\n}\n";
  out.fragment = fs.str();
  return out;
}

} // namespace aurora::vita::gfx
