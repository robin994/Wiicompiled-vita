#include "aurora_gx_bridge.hpp"
#if defined(AURORA_VITA_UPSTREAM)
#if defined(AURORA_VITA_UPSTREAM_STUB)
#include "../../../tests/upstream_gx_stub.hpp"
#else
#include "../../../lib/gx/gx.hpp"
#include "../../../lib/gx/pipeline.hpp"
#endif
#include "../gfx/vita_vertex_decode.hpp"
#include "../gfx/vita_vertex_pipeline.hpp"
#include "../gfx/vita_draw_adapter.hpp"
#include "../gfx/vita_texture_decode.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace aurora::vita::gxbridge {
namespace {
using namespace aurora::vita::gfx;

Compare cmp(GXCompare v) noexcept { switch(v){case GX_NEVER:return Compare::Never;case GX_LESS:return Compare::Less;case GX_EQUAL:return Compare::Equal;case GX_LEQUAL:return Compare::LessEqual;case GX_GREATER:return Compare::Greater;case GX_NEQUAL:return Compare::NotEqual;case GX_GEQUAL:return Compare::GreaterEqual;case GX_ALWAYS:return Compare::Always;}return Compare::Always; }
CullMode cull(GXCullMode v) noexcept {switch(v){case GX_CULL_NONE:return CullMode::None;case GX_CULL_FRONT:return CullMode::Front;case GX_CULL_BACK:return CullMode::Back;case GX_CULL_ALL:return CullMode::All;}return CullMode::None;}
BlendMode blend(GXBlendMode v) noexcept {switch(v){case GX_BM_NONE:return BlendMode::None;case GX_BM_BLEND:return BlendMode::Blend;case GX_BM_SUBTRACT:return BlendMode::Subtract;case GX_BM_LOGIC:return BlendMode::Logic;}return BlendMode::None;}
BlendFactor blend_factor(GXBlendFactor v,bool dst) noexcept {switch(v){case GX_BL_ZERO:return BlendFactor::Zero;case GX_BL_ONE:return BlendFactor::One;case GX_BL_SRCCLR:return dst?BlendFactor::SrcColor:BlendFactor::DstColor;case GX_BL_INVSRCCLR:return dst?BlendFactor::OneMinusSrcColor:BlendFactor::OneMinusDstColor;case GX_BL_SRCALPHA:return BlendFactor::SrcAlpha;case GX_BL_INVSRCALPHA:return BlendFactor::OneMinusSrcAlpha;case GX_BL_DSTALPHA:return BlendFactor::DstAlpha;case GX_BL_INVDSTALPHA:return BlendFactor::OneMinusDstAlpha;}return BlendFactor::One;}
LogicOp logic(GXLogicOp v) noexcept {const auto n=static_cast<unsigned>(v);return n<=15?static_cast<LogicOp>(n):LogicOp::Copy;}
FogMode fog_mode(GXFogType v) noexcept {switch(v){case GX_FOG_PERSP_LIN:case GX_FOG_ORTHO_LIN:return FogMode::Linear;case GX_FOG_PERSP_EXP:case GX_FOG_ORTHO_EXP:return FogMode::Exp;case GX_FOG_PERSP_EXP2:case GX_FOG_ORTHO_EXP2:return FogMode::Exp2;case GX_FOG_PERSP_REVEXP:case GX_FOG_ORTHO_REVEXP:return FogMode::RevExp;case GX_FOG_PERSP_REVEXP2:case GX_FOG_ORTHO_REVEXP2:return FogMode::RevExp2;default:return FogMode::None;}}
TevColorArg color_arg(GXTevColorArg v) noexcept {switch(v){case GX_CC_CPREV:return TevColorArg::Prev;case GX_CC_APREV:return TevColorArg::PrevA;case GX_CC_C0:return TevColorArg::Reg0;case GX_CC_A0:return TevColorArg::Reg0A;case GX_CC_C1:return TevColorArg::Reg1;case GX_CC_A1:return TevColorArg::Reg1A;case GX_CC_C2:return TevColorArg::Reg2;case GX_CC_A2:return TevColorArg::Reg2A;case GX_CC_TEXC:return TevColorArg::TexColor;case GX_CC_TEXA:return TevColorArg::TexAlpha;case GX_CC_RASC:return TevColorArg::RasColor;case GX_CC_RASA:return TevColorArg::RasAlpha;case GX_CC_ONE:return TevColorArg::One;case GX_CC_HALF:return TevColorArg::Half;case GX_CC_KONST:return TevColorArg::Konst;case GX_CC_ZERO:return TevColorArg::Zero;}return TevColorArg::Zero;}
TevAlphaArg alpha_arg(GXTevAlphaArg v) noexcept {switch(v){case GX_CA_APREV:return TevAlphaArg::PrevA;case GX_CA_A0:return TevAlphaArg::Reg0A;case GX_CA_A1:return TevAlphaArg::Reg1A;case GX_CA_A2:return TevAlphaArg::Reg2A;case GX_CA_TEXA:return TevAlphaArg::TexAlpha;case GX_CA_RASA:return TevAlphaArg::RasAlpha;case GX_CA_KONST:return TevAlphaArg::Konst;case GX_CA_ZERO:return TevAlphaArg::Zero;}return TevAlphaArg::Zero;}
TevOp tev_op(GXTevOp v) noexcept {switch(v){case GX_TEV_ADD:return TevOp::Add;case GX_TEV_SUB:return TevOp::Sub;case GX_TEV_COMP_R8_GT:return TevOp::CompR8Greater;case GX_TEV_COMP_R8_EQ:return TevOp::CompR8Equal;case GX_TEV_COMP_GR16_GT:return TevOp::CompGR16Greater;case GX_TEV_COMP_GR16_EQ:return TevOp::CompGR16Equal;case GX_TEV_COMP_BGR24_GT:return TevOp::CompBGR24Greater;case GX_TEV_COMP_BGR24_EQ:return TevOp::CompBGR24Equal;case GX_TEV_COMP_RGB8_GT:return TevOp::CompRGB8Greater;case GX_TEV_COMP_RGB8_EQ:return TevOp::CompRGB8Equal;default:return TevOp::Add;}}
TevBias bias(GXTevBias v) noexcept {switch(v){case GX_TB_ZERO:return TevBias::Zero;case GX_TB_ADDHALF:return TevBias::AddHalf;case GX_TB_SUBHALF:return TevBias::SubHalf;default:return TevBias::Zero;}}
TevScale scale(GXTevScale v) noexcept {switch(v){case GX_CS_SCALE_1:return TevScale::Scale1;case GX_CS_SCALE_2:return TevScale::Scale2;case GX_CS_SCALE_4:return TevScale::Scale4;case GX_CS_DIVIDE_2:return TevScale::Divide2;default:return TevScale::Scale1;}}
TevReg reg(GXTevRegID v) noexcept {switch(v){case GX_TEVPREV:return TevReg::Prev;case GX_TEVREG0:return TevReg::Reg0;case GX_TEVREG1:return TevReg::Reg1;case GX_TEVREG2:return TevReg::Reg2;default:return TevReg::Prev;}}
TevChannel tev_chan(GXTevColorChan v) noexcept {switch(v){case GX_CH_RED:return TevChannel::Red;case GX_CH_GREEN:return TevChannel::Green;case GX_CH_BLUE:return TevChannel::Blue;case GX_CH_ALPHA:return TevChannel::Alpha;}return TevChannel::Red;}
RasterSource raster(GXChannelID v) noexcept {switch(v){case GX_COLOR0:case GX_ALPHA0:case GX_COLOR0A0:return RasterSource::Color0;case GX_COLOR1:case GX_ALPHA1:case GX_COLOR1A1:return RasterSource::Color1;case GX_ALPHA_BUMP:return RasterSource::AlphaBump;case GX_ALPHA_BUMPN:return RasterSource::AlphaBumpN;case GX_COLOR_ZERO:case GX_COLOR_NULL:return RasterSource::Zero;default:return RasterSource::Color0;}}
IndirectFormat ind_format(GXIndTexFormat v) noexcept {switch(v){case GX_ITF_8:return IndirectFormat::Bits8;case GX_ITF_5:return IndirectFormat::Bits5;case GX_ITF_4:return IndirectFormat::Bits4;case GX_ITF_3:return IndirectFormat::Bits3;}return IndirectFormat::Bits8;}
IndirectBias ind_bias(GXIndTexBiasSel v) noexcept {switch(v){case GX_ITB_NONE:return IndirectBias::None;case GX_ITB_S:return IndirectBias::S;case GX_ITB_T:return IndirectBias::T;case GX_ITB_ST:return IndirectBias::ST;case GX_ITB_U:return IndirectBias::U;case GX_ITB_SU:return IndirectBias::SU;case GX_ITB_TU:return IndirectBias::TU;case GX_ITB_STU:return IndirectBias::STU;}return IndirectBias::None;}
IndirectAlphaSel ind_alpha(GXIndTexAlphaSel v) noexcept {switch(v){case GX_ITBA_OFF:return IndirectAlphaSel::Off;case GX_ITBA_S:return IndirectAlphaSel::S;case GX_ITBA_T:return IndirectAlphaSel::T;case GX_ITBA_U:return IndirectAlphaSel::U;}return IndirectAlphaSel::Off;}
IndirectMatrix ind_mtx(GXIndTexMtxID v) noexcept {switch(v){case GX_ITM_OFF:return IndirectMatrix::Off;case GX_ITM_0:return IndirectMatrix::Mtx0;case GX_ITM_1:return IndirectMatrix::Mtx1;case GX_ITM_2:return IndirectMatrix::Mtx2;case GX_ITM_S0:return IndirectMatrix::S0;case GX_ITM_S1:return IndirectMatrix::S1;case GX_ITM_S2:return IndirectMatrix::S2;case GX_ITM_T0:return IndirectMatrix::T0;case GX_ITM_T1:return IndirectMatrix::T1;case GX_ITM_T2:return IndirectMatrix::T2;}return IndirectMatrix::Off;}
IndirectWrap ind_wrap(GXIndTexWrap v) noexcept {switch(v){case GX_ITW_OFF:return IndirectWrap::Off;case GX_ITW_256:return IndirectWrap::W256;case GX_ITW_128:return IndirectWrap::W128;case GX_ITW_64:return IndirectWrap::W64;case GX_ITW_32:return IndirectWrap::W32;case GX_ITW_16:return IndirectWrap::W16;case GX_ITW_0:return IndirectWrap::W0;}return IndirectWrap::Off;}
uint8_t ind_scale(GXIndTexScale v) noexcept {switch(v){case GX_ITS_1:return 0;case GX_ITS_2:return 1;case GX_ITS_4:return 2;case GX_ITS_8:return 3;case GX_ITS_16:return 4;case GX_ITS_32:return 5;case GX_ITS_64:return 6;case GX_ITS_128:return 7;case GX_ITS_256:return 8;}return 0;}
ColorSource color_source(GXColorSrc v) noexcept {return v==GX_SRC_VTX?ColorSource::Vertex:ColorSource::Register;}
DiffuseFn diffuse(GXDiffuseFn v) noexcept {switch(v){case GX_DF_NONE:return DiffuseFn::None;case GX_DF_SIGN:return DiffuseFn::Signed;case GX_DF_CLAMP:return DiffuseFn::Clamp;}return DiffuseFn::None;}
AttenuationFn attenuation(GXAttnFn v) noexcept {switch(v){case GX_AF_NONE:return AttenuationFn::None;case GX_AF_SPEC:return AttenuationFn::Specular;case GX_AF_SPOT:return AttenuationFn::Spot;}return AttenuationFn::None;}
TexGenType texgen_type(GXTexGenType v) noexcept {switch(v){case GX_TG_MTX3x4:return TexGenType::Matrix3x4;case GX_TG_MTX2x4:return TexGenType::Matrix2x4;case GX_TG_BUMP0:return TexGenType::Bump0;case GX_TG_BUMP1:return TexGenType::Bump1;case GX_TG_BUMP2:return TexGenType::Bump2;case GX_TG_BUMP3:return TexGenType::Bump3;case GX_TG_BUMP4:return TexGenType::Bump4;case GX_TG_BUMP5:return TexGenType::Bump5;case GX_TG_BUMP6:return TexGenType::Bump6;case GX_TG_BUMP7:return TexGenType::Bump7;case GX_TG_SRTG:return TexGenType::SRTG;}return TexGenType::Matrix2x4;}
TexGenSource texgen_source(GXTexGenSrc v) noexcept {switch(v){case GX_TG_POS:return TexGenSource::Position;case GX_TG_NRM:return TexGenSource::Normal;case GX_TG_BINRM:return TexGenSource::Binormal;case GX_TG_TANGENT:return TexGenSource::Tangent;case GX_TG_TEX0:return TexGenSource::Tex0;case GX_TG_TEX1:return TexGenSource::Tex1;case GX_TG_TEX2:return TexGenSource::Tex2;case GX_TG_TEX3:return TexGenSource::Tex3;case GX_TG_TEX4:return TexGenSource::Tex4;case GX_TG_TEX5:return TexGenSource::Tex5;case GX_TG_TEX6:return TexGenSource::Tex6;case GX_TG_TEX7:return TexGenSource::Tex7;case GX_TG_COLOR0:return TexGenSource::Color0;case GX_TG_COLOR1:return TexGenSource::Color1;default:return TexGenSource::Tex0;}}
int8_t tex_mtx(GXTexMtx v) noexcept {if(v==GX_IDENTITY)return -1;if(v>=GX_TEXMTX0&&v<=GX_TEXMTX9)return static_cast<int8_t>((v-GX_TEXMTX0)/3);return -1;}
int8_t post_mtx(GXPTTexMtx v) noexcept {if(v==GX_PTIDENTITY)return -1;if(v>=GX_PTTEXMTX0&&v<=GX_PTTEXMTX19)return static_cast<int8_t>((v-GX_PTTEXMTX0)/3);return -1;}

KonstColorSel kc(GXTevKColorSel v) noexcept {switch(v){case GX_TEV_KCSEL_8_8:return KonstColorSel::One;case GX_TEV_KCSEL_7_8:return KonstColorSel::SevenEighths;case GX_TEV_KCSEL_6_8:return KonstColorSel::SixEighths;case GX_TEV_KCSEL_5_8:return KonstColorSel::FiveEighths;case GX_TEV_KCSEL_4_8:return KonstColorSel::FourEighths;case GX_TEV_KCSEL_3_8:return KonstColorSel::ThreeEighths;case GX_TEV_KCSEL_2_8:return KonstColorSel::TwoEighths;case GX_TEV_KCSEL_1_8:return KonstColorSel::OneEighth;case GX_TEV_KCSEL_K0:return KonstColorSel::K0;case GX_TEV_KCSEL_K1:return KonstColorSel::K1;case GX_TEV_KCSEL_K2:return KonstColorSel::K2;case GX_TEV_KCSEL_K3:return KonstColorSel::K3;case GX_TEV_KCSEL_K0_R:return KonstColorSel::K0R;case GX_TEV_KCSEL_K1_R:return KonstColorSel::K1R;case GX_TEV_KCSEL_K2_R:return KonstColorSel::K2R;case GX_TEV_KCSEL_K3_R:return KonstColorSel::K3R;case GX_TEV_KCSEL_K0_G:return KonstColorSel::K0G;case GX_TEV_KCSEL_K1_G:return KonstColorSel::K1G;case GX_TEV_KCSEL_K2_G:return KonstColorSel::K2G;case GX_TEV_KCSEL_K3_G:return KonstColorSel::K3G;case GX_TEV_KCSEL_K0_B:return KonstColorSel::K0B;case GX_TEV_KCSEL_K1_B:return KonstColorSel::K1B;case GX_TEV_KCSEL_K2_B:return KonstColorSel::K2B;case GX_TEV_KCSEL_K3_B:return KonstColorSel::K3B;case GX_TEV_KCSEL_K0_A:return KonstColorSel::K0A;case GX_TEV_KCSEL_K1_A:return KonstColorSel::K1A;case GX_TEV_KCSEL_K2_A:return KonstColorSel::K2A;case GX_TEV_KCSEL_K3_A:return KonstColorSel::K3A;default:return KonstColorSel::One;}}
KonstAlphaSel ka(GXTevKAlphaSel v) noexcept {switch(v){case GX_TEV_KASEL_8_8:return KonstAlphaSel::One;case GX_TEV_KASEL_7_8:return KonstAlphaSel::SevenEighths;case GX_TEV_KASEL_6_8:return KonstAlphaSel::SixEighths;case GX_TEV_KASEL_5_8:return KonstAlphaSel::FiveEighths;case GX_TEV_KASEL_4_8:return KonstAlphaSel::FourEighths;case GX_TEV_KASEL_3_8:return KonstAlphaSel::ThreeEighths;case GX_TEV_KASEL_2_8:return KonstAlphaSel::TwoEighths;case GX_TEV_KASEL_1_8:return KonstAlphaSel::OneEighth;case GX_TEV_KASEL_K0_R:return KonstAlphaSel::K0R;case GX_TEV_KASEL_K1_R:return KonstAlphaSel::K1R;case GX_TEV_KASEL_K2_R:return KonstAlphaSel::K2R;case GX_TEV_KASEL_K3_R:return KonstAlphaSel::K3R;case GX_TEV_KASEL_K0_G:return KonstAlphaSel::K0G;case GX_TEV_KASEL_K1_G:return KonstAlphaSel::K1G;case GX_TEV_KASEL_K2_G:return KonstAlphaSel::K2G;case GX_TEV_KASEL_K3_G:return KonstAlphaSel::K3G;case GX_TEV_KASEL_K0_B:return KonstAlphaSel::K0B;case GX_TEV_KASEL_K1_B:return KonstAlphaSel::K1B;case GX_TEV_KASEL_K2_B:return KonstAlphaSel::K2B;case GX_TEV_KASEL_K3_B:return KonstAlphaSel::K3B;case GX_TEV_KASEL_K0_A:return KonstAlphaSel::K0A;case GX_TEV_KASEL_K1_A:return KonstAlphaSel::K1A;case GX_TEV_KASEL_K2_A:return KonstAlphaSel::K2A;case GX_TEV_KASEL_K3_A:return KonstAlphaSel::K3A;default:return KonstAlphaSel::One;}}
}

Capabilities inspect(const aurora::gx::ShaderConfig& c) noexcept {
  Capabilities r{};r.hasFog=c.fogType!=GX_FOG_NONE;r.hasLineExpansion=c.lineMode!=0;
  for(const auto&ch:c.colorChannels)r.hasLighting|=ch.lightingEnabled;
  for(unsigned i=0;i<c.tevStageCount&&i<c.tevStages.size();i++){const auto&s=c.tevStages[i];const bool valid=static_cast<u32>(s.indTexStage)<c.numIndStages;const bool alphaBump=s.channelId==GX_ALPHA_BUMP||s.channelId==GX_ALPHA_BUMPN;if(valid&&(s.indTexMtxId!=GX_ITM_OFF||s.indTexWrapS!=GX_ITW_OFF||s.indTexWrapT!=GX_ITW_OFF||s.indTexAddPrev||s.indTexAlphaSel!=GX_ITBA_OFF||alphaBump)){r.hasIndirect=true;r.hasIndirectOrigLod|=s.indTexUseOrigLOD;}}
  for(const auto&t:c.tcgs)if(t.src!=GX_MAX_TEXGENSRC){r.hasGeneratedTexcoords=true;r.hasEmbossTexgen|=(t.type>=GX_TG_BUMP0&&t.type<=GX_TG_BUMP7);}
  // Aurora upstream currently tracks indTexUseOrigLOD but does not branch shader generation on it.
  // Keep parity with upstream: expose the capability bit for diagnostics without downgrading exactTev.
  r.exactTev=true;r.exactFixedState=true;return r;
}


uint8_t translate_line_mode(uint8_t primitive) noexcept {
  switch (static_cast<GXPrimitive>(primitive)) {
  case GX_LINES: return 1;
  case GX_LINESTRIP: return 2;
  case GX_POINTS: return 3;
  default: return 0;
  }
}

gfx::SourcePrimitive translate_source_primitive(uint8_t primitive) noexcept {
  switch (static_cast<GXPrimitive>(primitive)) {
  case GX_QUADS: return gfx::SourcePrimitive::Quads;
  case GX_TRIANGLES: return gfx::SourcePrimitive::Triangles;
  case GX_TRIANGLESTRIP: return gfx::SourcePrimitive::TriangleStrip;
  case GX_TRIANGLEFAN: return gfx::SourcePrimitive::TriangleFan;
  case GX_LINES: return gfx::SourcePrimitive::Lines;
  case GX_LINESTRIP: return gfx::SourcePrimitive::LineStrip;
  case GX_POINTS: return gfx::SourcePrimitive::Points;
  default: return gfx::SourcePrimitive::Triangles;
  }
}

namespace {
uint8_t current_component_count(GXAttr attr, GXCompCnt cnt) noexcept {
  switch (attr) {
  case GX_VA_PNMTXIDX:
  case GX_VA_TEX0MTXIDX: case GX_VA_TEX0MTXIDX + 1: case GX_VA_TEX0MTXIDX + 2:
  case GX_VA_TEX0MTXIDX + 3: case GX_VA_TEX0MTXIDX + 4: case GX_VA_TEX0MTXIDX + 5:
  case GX_VA_TEX0MTXIDX + 6: case GX_VA_TEX0MTXIDX + 7:
    return 1;
  case GX_VA_POS: return cnt == GX_POS_XY ? 2 : 3;
  case GX_VA_NRM: return (cnt == GX_NRM_NBT || cnt == GX_NRM_NBT3) ? 9 : 3;
  case GX_VA_CLR0: case GX_VA_CLR1: return 1;
  default:
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX0 + 7) return cnt == GX_TEX_S ? 1 : 2;
    return 1;
  }
}
uint8_t current_component_size(GXAttr attr, GXCompType type) noexcept {
  if (attr == GX_VA_PNMTXIDX || (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX0MTXIDX + 7)) return 1;
  if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
    switch (type) {
    case GX_RGB565: case GX_RGBA4: return 2;
    case GX_RGB8: case GX_RGBA6: return 3;
    case GX_RGBX8: case GX_RGBA8: return 4;
    default: return 4;
    }
  }
  switch (type) {
  case GX_U8: case GX_S8: return 1;
  case GX_U16: case GX_S16: return 2;
  case GX_F32: return 4;
  default: return 1;
  }
}

aurora::gx::ShaderConfig build_current_shader_config(GXVtxFmt fmt, uint8_t lineMode) noexcept {
  const auto& g = aurora::gx::g_gxState;
  aurora::gx::ShaderConfig sc{};
  sc.fogType = static_cast<u8>(g.fog.type);
  sc.fogRangeEnabled = g.fog.rangeEnabled;
  sc.lineMode = lineMode;
  const auto& vf = g.vtxFmts[static_cast<size_t>(fmt)];
  uint16_t streamOffset = 0;
  for (int i = GX_VA_PNMTXIDX; i <= GX_VA_TEX0 + 7; ++i) {
    const auto attr = static_cast<GXAttr>(i);
    const auto source = g.vtxDesc[static_cast<size_t>(i)];
    auto& m = sc.attrs[static_cast<size_t>(i)];
    if (source == GX_NONE) { m = {}; continue; }
    const auto& af = vf.attrs[static_cast<size_t>(i)];
    const uint8_t count = current_component_count(attr, af.cnt);
    m.attrType = source;
    m.cnt = count;
    m.compType = af.type;
    m.offset = static_cast<decltype(m.offset)>(streamOffset);
    m.frac = af.frac;
    m.nbt3 = attr == GX_VA_NRM && af.cnt == GX_NRM_NBT3;
    if (source == GX_INDEX8 || source == GX_INDEX16) {
      const auto& arr = g.arrays[static_cast<size_t>(i)];
      m.stride = arr.stride;
      m.le = arr.le;
      const uint16_t indexSize = source == GX_INDEX16 ? 2u : 1u;
      streamOffset += static_cast<uint16_t>(indexSize * (m.nbt3 ? 3u : 1u));
    } else {
      m.stride = 0;
      m.le = false;
      streamOffset += static_cast<uint16_t>(current_component_size(attr, af.type) * count);
    }
  }
  sc.vtxStride = static_cast<u8>(std::min<uint16_t>(streamOffset, 255u));
  sc.tevSwapTable = g.tevSwapTable;
  sc.tevStageCount = std::min<u32>(g.numTevStages, aurora::gx::MaxTevStages);
  for (u32 i = 0; i < sc.tevStageCount; ++i) sc.tevStages[i] = g.tevStages[i];
  sc.colorChannels = g.colorChannelConfig;
  const u32 texgenCount = std::min<u32>(g.numTexGens, aurora::gx::MaxTexCoord);
  for (u32 i = 0; i < texgenCount; ++i) sc.tcgs[i] = g.tcgs[i];
  sc.alphaCompare = g.alphaCompare;
  sc.numIndStages = std::min<u32>(g.numIndStages, aurora::gx::MaxIndStages);
  for (u32 i = 0; i < sc.numIndStages; ++i) sc.indStages[i] = g.indStages[i];
  return sc;
}

aurora::gx::PipelineConfig build_current_pipeline_config(GXPrimitive primitive, GXVtxFmt fmt) noexcept {
  const auto& g = aurora::gx::g_gxState;
  aurora::gx::PipelineConfig pc{};
  pc.shaderConfig = build_current_shader_config(fmt, translate_line_mode(static_cast<uint8_t>(primitive)));
  pc.depthFunc = g.depthFunc;
  pc.cullMode = g.cullMode;
  pc.blendMode = g.blendMode;
  pc.blendFacSrc = g.blendFacSrc;
  pc.blendFacDst = g.blendFacDst;
  pc.blendOp = g.blendOp;
  pc.dstAlpha = g.dstAlpha;
  const float offset = g.cullMode == GX_CULL_FRONT ? g.backOffset : g.frontOffset;
  const float slope = g.cullMode == GX_CULL_FRONT ? g.backScale : g.frontScale;
  pc.polygonOffsetBits = std::bit_cast<uint32_t>(offset);
  pc.polygonOffsetScaleBits = std::bit_cast<uint32_t>(slope);
  pc.polygonOffsetClampBits = std::bit_cast<uint32_t>(g.clamp);
  pc.depthCompare = g.depthCompare;
  pc.depthUpdate = g.depthUpdate;
  pc.colorUpdate = g.colorUpdate;
  pc.alphaUpdate = g.alphaUpdate;
  return pc;
}
}

gfx::PipelineDesc translate_current_pipeline(uint8_t primitive, uint8_t fmt) noexcept {
  return translate_pipeline(build_current_pipeline_config(static_cast<GXPrimitive>(primitive), static_cast<GXVtxFmt>(fmt)));
}

gfx::VertexDecodeLayout translate_current_vertex_layout(uint8_t fmt) noexcept {
  const auto sc = build_current_shader_config(static_cast<GXVtxFmt>(fmt), 0);
  return translate_vertex_layout(sc);
}

Capabilities inspect_current(uint8_t primitive, uint8_t fmt) noexcept {
  const auto sc = build_current_shader_config(static_cast<GXVtxFmt>(fmt), translate_line_mode(primitive));
  return inspect(sc);
}

gfx::PipelineDesc translate_pipeline(const aurora::gx::PipelineConfig& c) noexcept {
  gfx::PipelineDesc o{};o.primitive=gfx::Primitive::Triangles;o.depthFunc=cmp(c.depthFunc);o.cull=cull(c.cullMode);o.blendMode=blend(c.blendMode);o.srcFactor=blend_factor(c.blendFacSrc,false);o.dstFactor=blend_factor(c.blendFacDst,true);o.logicOp=logic(c.blendOp);o.depthTest=c.depthCompare;o.depthWrite=c.depthCompare&&c.depthUpdate;o.colorWrite=c.colorUpdate;o.alphaWrite=c.alphaUpdate;o.reversedZ=aurora::gx::UseReversedZ;o.dstAlpha=c.dstAlpha==UINT32_MAX?-1:static_cast<int16_t>(c.dstAlpha);o.fogMode=fog_mode(static_cast<GXFogType>(c.shaderConfig.fogType));o.fogOrthographic=(c.shaderConfig.fogType&0x08)!=0;o.fogRangeEnabled=c.shaderConfig.fogRangeEnabled;o.layout=gfx::canonical_vertex_layout();
  const auto&sc=c.shaderConfig;
  for(unsigned i=0;i<sc.tcgs.size()&&i<gfx::MaxTextures;i++){const auto&t=sc.tcgs[i];if(t.src==GX_MAX_TEXGENSRC)continue;o.texgenCount=static_cast<uint8_t>(i+1);auto&d=o.texgens[i];d.type=texgen_type(t.type);d.source=texgen_source(t.src);d.matrix=tex_mtx(t.mtx);d.postMatrix=post_mtx(t.postMtx);d.embossSource=t.embossSrc;d.normalize=t.normalize;d.matrixFromVertex=sc.attrs[GX_VA_TEX0MTXIDX+i].attrType!=GX_NONE;}
  for(unsigned i=0;i<o.colorChannels.size()&&i<sc.colorChannels.size();i++){const auto&s=sc.colorChannels[i];auto&d=o.colorChannels[i];d.materialSource=color_source(s.matSrc);d.ambientSource=color_source(s.ambSrc);d.diffuse=diffuse(s.diffFn);d.attenuation=attenuation(s.attnFn);d.lightingEnabled=s.lightingEnabled;}
  for(unsigned i=0;i<o.tev.swapTable.size()&&i<sc.tevSwapTable.size();i++){const auto&s=sc.tevSwapTable[i];o.tev.swapTable[i]={tev_chan(s.red),tev_chan(s.green),tev_chan(s.blue),tev_chan(s.alpha)};}
  o.tev.indirectStageCount=static_cast<uint8_t>(std::min<size_t>(sc.numIndStages,gfx::MaxIndStages));for(unsigned i=0;i<o.tev.indirectStageCount;i++){const auto&s=sc.indStages[i];auto&d=o.tev.indirectStages[i];d.texCoord=s.texCoordId>=GX_TEXCOORD0&&s.texCoordId<=GX_TEXCOORD7?static_cast<uint8_t>(s.texCoordId-GX_TEXCOORD0):0xff;d.texture=s.texMapId>=GX_TEXMAP0&&s.texMapId<=GX_TEXMAP7?static_cast<uint8_t>(s.texMapId-GX_TEXMAP0):0xff;d.scaleSShift=ind_scale(s.scaleS);d.scaleTShift=ind_scale(s.scaleT);}
  o.tev.stageCount=static_cast<uint8_t>(std::min<size_t>(sc.tevStageCount,gfx::MaxTevStages));o.tev.texCoordCount=o.texgenCount;o.tev.alphaCompare={cmp(sc.alphaCompare.comp0),static_cast<uint8_t>(sc.alphaCompare.ref0),static_cast<uint8_t>(sc.alphaCompare.op),cmp(sc.alphaCompare.comp1),static_cast<uint8_t>(sc.alphaCompare.ref1)};
  for(unsigned i=0;i<o.tev.stageCount;i++){const auto&s=sc.tevStages[i];auto&d=o.tev.stages[i];d.color={color_arg(s.colorPass.a),color_arg(s.colorPass.b),color_arg(s.colorPass.c),color_arg(s.colorPass.d)};d.alpha={alpha_arg(s.alphaPass.a),alpha_arg(s.alphaPass.b),alpha_arg(s.alphaPass.c),alpha_arg(s.alphaPass.d)};d.colorOp=tev_op(s.colorOp.op);d.alphaOp=tev_op(s.alphaOp.op);d.colorBias=bias(s.colorOp.bias);d.alphaBias=bias(s.alphaOp.bias);d.colorScale=scale(s.colorOp.scale);d.alphaScale=scale(s.alphaOp.scale);d.colorOut=reg(s.colorOp.outReg);d.alphaOut=reg(s.alphaOp.outReg);d.konstColor=kc(s.kcSel);d.konstAlpha=ka(s.kaSel);d.texture=s.texMapId>=GX_TEXMAP0&&s.texMapId<=GX_TEXMAP7?static_cast<uint8_t>(s.texMapId-GX_TEXMAP0):0xff;d.texCoord=s.texCoordId>=GX_TEXCOORD0&&s.texCoordId<=GX_TEXCOORD7?static_cast<uint8_t>(s.texCoordId-GX_TEXCOORD0):0xff;d.rasterSource=raster(s.channelId);d.rasSwap=static_cast<uint8_t>(s.tevSwapRas);d.texSwap=static_cast<uint8_t>(s.tevSwapTex);d.colorClamp=s.colorOp.clamp;d.alphaClamp=s.alphaOp.clamp;const bool alphaBump=s.channelId==GX_ALPHA_BUMP||s.channelId==GX_ALPHA_BUMPN;d.indirectEnabled=static_cast<u32>(s.indTexStage)<sc.numIndStages&&(s.indTexMtxId!=GX_ITM_OFF||s.indTexWrapS!=GX_ITW_OFF||s.indTexWrapT!=GX_ITW_OFF||s.indTexAddPrev||s.indTexAlphaSel!=GX_ITBA_OFF||alphaBump);d.indirectStage=static_cast<uint8_t>(s.indTexStage);d.indirectFormat=ind_format(s.indTexFormat);d.indirectBias=ind_bias(s.indTexBiasSel);d.indirectAlpha=ind_alpha(s.indTexAlphaSel);d.indirectMatrix=ind_mtx(s.indTexMtxId);d.indirectWrapS=ind_wrap(s.indTexWrapS);d.indirectWrapT=ind_wrap(s.indTexWrapT);d.indirectUseOrigLod=s.indTexUseOrigLOD;d.indirectAddPrev=s.indTexAddPrev;}
  return o;
}

namespace {
VertexSource vertex_source(GXAttrType v) noexcept {
  switch (v) {
  case GX_NONE: return VertexSource::None;
  case GX_DIRECT: return VertexSource::Direct;
  case GX_INDEX8: return VertexSource::Index8;
  case GX_INDEX16: return VertexSource::Index16;
  default: return VertexSource::None;
  }
}
VertexComponent vertex_component(GXCompType v) noexcept {
  switch (v) {
  case GX_U8: return VertexComponent::U8;
  case GX_S8: return VertexComponent::S8;
  case GX_U16: return VertexComponent::U16;
  case GX_S16: return VertexComponent::S16;
  case GX_F32: return VertexComponent::F32;
  case GX_RGB565: return VertexComponent::RGB565;
  case GX_RGB8: return VertexComponent::RGB8;
  case GX_RGBX8: return VertexComponent::RGBX8;
  case GX_RGBA4: return VertexComponent::RGBA4;
  case GX_RGBA6: return VertexComponent::RGBA6;
  case GX_RGBA8: return VertexComponent::RGBA8;
  default: return VertexComponent::F32;
  }
}
uint16_t vertex_component_size(GXCompType v) noexcept {
  switch (v) {
  case GX_U8: case GX_S8: return 1;
  case GX_U16: case GX_S16: return 2;
  case GX_F32: return 4;
  case GX_RGB565: case GX_RGBA4: return 2;
  case GX_RGB8: return 3;
  case GX_RGBX8: case GX_RGBA8: return 4;
  case GX_RGBA6: return 3;
  default: return 1;
  }
}
VertexSemantic tex_mtx_semantic(unsigned i) noexcept {
  return static_cast<VertexSemantic>(static_cast<unsigned>(VertexSemantic::TexMatrixIndex0) + i);
}
VertexSemantic tex_semantic(unsigned i) noexcept {
  return static_cast<VertexSemantic>(static_cast<unsigned>(VertexSemantic::Tex0) + i);
}
void copy_matrix(Matrix3x4& out, const aurora::Mat3x4<float>& in) noexcept {
  for (unsigned r = 0; r < 4; ++r) {
    out.v[r] = in.m0[r];
    out.v[4 + r] = in.m1[r];
    out.v[8 + r] = in.m2[r];
  }
}
std::array<float,4> copy_vec4(const aurora::Vec4<float>& in) noexcept {
  return {in[0], in[1], in[2], in[3]};
}
}

gfx::VertexDecodeLayout translate_vertex_layout(const aurora::gx::ShaderConfig& c) noexcept {
  gfx::VertexDecodeLayout out{};
  out.streamStride = c.vtxStride;
  out.streamLittleEndian = false; // GX FIFO/display-list bytes are big-endian.
  auto add = [&](GXAttr attr, VertexSemantic semantic, const aurora::gx::AttrConfig& m,
                       uint8_t components, uint16_t streamExtra = 0, uint16_t valueExtra = 0) mutable {
    if (m.attrType == GX_NONE || out.count >= out.attributes.size()) return;
    auto& d = out.attributes[out.count++];
    d.semantic = semantic;
    d.source = vertex_source(static_cast<GXAttrType>(m.attrType));
    d.component = vertex_component(static_cast<GXCompType>(m.compType));
    d.components = components;
    d.frac = m.frac;
    d.streamOffset = static_cast<uint16_t>(m.offset + streamExtra);
    d.valueOffset = valueExtra;
    if (d.source == VertexSource::Index8 || d.source == VertexSource::Index16) {
      const auto& a = aurora::gx::g_gxState.arrays[static_cast<size_t>(attr)];
      d.array = {static_cast<const uint8_t*>(a.data), a.size, a.stride, a.le};
    }
  };

  // Matrix indices are always byte-sized values in the GX vertex stream.
  if (c.attrs[GX_VA_PNMTXIDX].attrType != GX_NONE) {
    auto m = c.attrs[GX_VA_PNMTXIDX]; m.compType = GX_U8; m.cnt = 1; m.frac = 0;
    add(GX_VA_PNMTXIDX, VertexSemantic::PnMatrixIndex, m, 1);
  }
  for (unsigned i = 0; i < MaxTextures; ++i) {
    const auto attr = static_cast<GXAttr>(GX_VA_TEX0MTXIDX + i);
    if (c.attrs[attr].attrType == GX_NONE) continue;
    auto m = c.attrs[attr]; m.compType = GX_U8; m.cnt = 1; m.frac = 0;
    add(attr, tex_mtx_semantic(i), m, 1);
  }

  add(GX_VA_POS, VertexSemantic::Position, c.attrs[GX_VA_POS],
      std::min<uint8_t>(c.attrs[GX_VA_POS].cnt, 3));

  const auto& n = c.attrs[GX_VA_NRM];
  if (n.attrType != GX_NONE) {
    if (n.cnt == 9) {
      const uint16_t comp3 = static_cast<uint16_t>(3u * vertex_component_size(static_cast<GXCompType>(n.compType)));
      const uint16_t indexBytes = n.attrType == GX_INDEX16 ? 2u : 1u;
      // NBT3 has three independent indices in the display list; ordinary NBT has one
      // index/direct value referencing nine consecutive components.
      add(GX_VA_NRM, VertexSemantic::Normal, n, 3, 0, 0);
      add(GX_VA_NRM, VertexSemantic::Binormal, n, 3, n.nbt3 ? indexBytes : 0, n.nbt3 ? 0 : comp3);
      add(GX_VA_NRM, VertexSemantic::Tangent, n, 3, n.nbt3 ? indexBytes * 2u : 0, n.nbt3 ? 0 : comp3 * 2u);
    } else {
      add(GX_VA_NRM, VertexSemantic::Normal, n, std::min<uint8_t>(n.cnt, 3));
    }
  }
  add(GX_VA_CLR0, VertexSemantic::Color0, c.attrs[GX_VA_CLR0], 4);
  add(GX_VA_CLR1, VertexSemantic::Color1, c.attrs[GX_VA_CLR1], 4);
  for (unsigned i = 0; i < MaxTextures; ++i) {
    const auto attr = static_cast<GXAttr>(GX_VA_TEX0 + i);
    add(attr, tex_semantic(i), c.attrs[attr], std::min<uint8_t>(c.attrs[attr].cnt, 2));
  }
  return out;
}

void translate_vertex_state(gfx::VertexTransformState& state, gfx::DrawUniforms& uniforms) noexcept {
  const auto& g = aurora::gx::g_gxState;
  for (unsigned i = 0; i < aurora::gx::MaxPnMtx; ++i) {
    copy_matrix(state.postexMatrices[i], g.pnMtx[i].pos);
    copy_matrix(state.normalMatrices[i], g.pnMtx[i].nrm);
  }
  for (unsigned i = 0; i < aurora::gx::MaxTexMtx; ++i) copy_matrix(state.postexMatrices[10 + i], g.texMtxs[i]);
  for (unsigned i = 0; i < aurora::gx::MaxPTTexMtx; ++i) copy_matrix(state.postMatrices[i], g.ptTexMtxs[i]);
  state.currentPnMatrix = static_cast<uint8_t>(std::min<u32>(g.currentPnMtx, 9));

  auto proj = g.proj;
  if constexpr (aurora::gx::UseReversedZ) proj.m2 = proj.m2 * aurora::Vec4{-1.f, -1.f, -1.f, -1.f};
  else proj.m2 = proj.m2 + proj.m3;
  // Aurora/WGSL uses row-vector * matrix. The Vita GLSL path uses matrix * column-vector,
  // so upload the transposed matrix to preserve identical clip-space results.
  const auto glProj = proj.transpose();
  std::memcpy(state.projection.data(), &glProj, sizeof(glProj));
  uniforms.mvp = state.projection;

  for (unsigned ch = 0; ch < 4; ++ch) {
    state.channelAmbient[ch] = copy_vec4(g.colorChannelState[ch].ambColor);
    state.channelMaterial[ch] = copy_vec4(g.colorChannelState[ch].matColor);
    uniforms.channelAmbient[ch] = state.channelAmbient[ch];
    uniforms.channelMaterial[ch] = state.channelMaterial[ch];
    const auto mask = g.colorChannelState[ch].lightMask.to_ulong();
    for (unsigned li = 0; li < gfx::MaxLights; ++li) {
      state.lightEnabled[ch][li] = (mask & (1ul << li)) ? 1 : 0;
      uniforms.lightEnabled[ch][li] = state.lightEnabled[ch][li] ? 1.f : 0.f;
    }
  }
  for (unsigned i = 0; i < gfx::MaxLights; ++i) {
    const auto& s = g.lights[i]; auto& d = state.lights[i];
    d.position = copy_vec4(s.pos); d.direction = copy_vec4(s.dir); d.color = copy_vec4(s.color);
    d.cosAtt = copy_vec4(s.cosAtt); d.distAtt = copy_vec4(s.distAtt); uniforms.lights[i] = d;
  }
  for (unsigned i = 0; i < 4; ++i) {
    uniforms.tevreg[i] = copy_vec4(g.colorRegs[i]);
    uniforms.kcolor[i] = copy_vec4(g.kcolors[i]);
  }
  uniforms.fogColor = copy_vec4(g.fog.color);
  const float logicalWidth = std::max(g.logicalViewport.width, 1.f);
  const float renderWidth = std::max(g.renderViewport.width, 1.f);
  const float rangeCenter = ((static_cast<float>(g.fog.rangeCenter) - g.logicalViewport.left) / logicalWidth) * 2.f - 1.f +
                            (g.renderViewport.left / renderWidth) * 2.f;
  uniforms.fogParams = {g.fog.a, g.fog.b, g.fog.c, rangeCenter};
  uniforms.renderViewportWidth = renderWidth;
  for (unsigned i=0;i<g.fog.rangeK.size();++i) {
    const unsigned source=(i&~1u)|(1u-(i&1u));
    uniforms.fogRangeK[i]=static_cast<float>(g.fog.rangeK[source])/64.f;
  }

  for (unsigned i = 0; i < gfx::MaxTextures; ++i) {
    const auto& s = g.texCoordScales[i];
    uniforms.texcoordScale[i] = {static_cast<float>(s.scaleS) + 1.f, static_cast<float>(s.scaleT) + 1.f, 0.f, 0.f};
    const auto& t = g.textures[i].texObj;
    uniforms.textureSizeBias[i] = {static_cast<float>(t.width()), static_cast<float>(t.height()), t.lod_bias(), 0.f};
  }
  for (unsigned i = 0; i < gfx::MaxIndMatrices; ++i) {
    const auto& m = g.indTexMtxs[i];
    uniforms.indirectMatrices[i * 2] = {m.mtx.m0.x, m.mtx.m0.y, m.mtx.m1.x, m.mtx.m1.y};
    uniforms.indirectMatrices[i * 2 + 1] = {m.mtx.m2.x, m.mtx.m2.y, std::exp2f(m.scaleExp), 0.f};
  }
}


namespace {
gfx::TextureFormat texture_format(GXTexFmt v, bool& ok) noexcept {
  ok=true;switch(v){case GX_TF_I4:return gfx::TextureFormat::I4;case GX_TF_I8:return gfx::TextureFormat::I8;case GX_TF_IA4:return gfx::TextureFormat::IA4;case GX_TF_IA8:return gfx::TextureFormat::IA8;case GX_TF_RGB565:return gfx::TextureFormat::RGB565;case GX_TF_RGB5A3:return gfx::TextureFormat::RGB5A3;case GX_TF_RGBA8:return gfx::TextureFormat::RGBA8;case GX_TF_C4:return gfx::TextureFormat::C4;case GX_TF_C8:return gfx::TextureFormat::C8;case GX_TF_C14X2:return gfx::TextureFormat::C14X2;case GX_TF_CMPR:return gfx::TextureFormat::CMPR;default:ok=false;return gfx::TextureFormat::RGBA8888;}
}
gfx::PaletteFormat palette_format(GXTlutFmt v) noexcept {switch(v){case GX_TL_IA8:return gfx::PaletteFormat::IA8;case GX_TL_RGB565:return gfx::PaletteFormat::RGB565;case GX_TL_RGB5A3:return gfx::PaletteFormat::RGB5A3;}return gfx::PaletteFormat::None;}
gfx::WrapMode wrap_mode(GXTexWrapMode v) noexcept {switch(v){case GX_CLAMP:return gfx::WrapMode::Clamp;case GX_REPEAT:return gfx::WrapMode::Repeat;case GX_MIRROR:return gfx::WrapMode::Mirror;}return gfx::WrapMode::Repeat;}
gfx::Filter filter(GXTexFilter v) noexcept {switch(v){case GX_NEAR:return gfx::Filter::Nearest;case GX_LINEAR:return gfx::Filter::Linear;case GX_NEAR_MIP_NEAR:return gfx::Filter::NearestMipmapNearest;case GX_LIN_MIP_NEAR:return gfx::Filter::LinearMipmapNearest;case GX_NEAR_MIP_LIN:return gfx::Filter::NearestMipmapLinear;case GX_LIN_MIP_LIN:return gfx::Filter::LinearMipmapLinear;}return gfx::Filter::Linear;}
float tex_offset(GXTexOffset o) noexcept {switch(o){case GX_TO_ZERO:return 0.f;case GX_TO_SIXTEENTH:return 1.f/16.f;case GX_TO_EIGHTH:return 1.f/8.f;case GX_TO_FOURTH:return 1.f/4.f;case GX_TO_HALF:return .5f;case GX_TO_ONE:return 1.f;}return 0.f;}
uint8_t texcoord_mask(bool point) noexcept {uint8_t m=0;for(unsigned i=0;i<gfx::MaxTextures;i++){const auto&s=aurora::gx::g_gxState.texCoordScales[i];if(point?s.pointOffset:s.lineOffset)m|=static_cast<uint8_t>(1u<<i);}return m;}
}

TextureTranslation translate_texture(unsigned slot) noexcept {
  TextureTranslation out{};
  if(slot>=gfx::MaxTextures)return out;
  const auto& bind=aurora::gx::g_gxState.textures[slot];
  const auto& o=bind.texObj;

  // Sampler state is meaningful for both static textures and GXCopyTex-backed textures.
  auto& sm=out.sampler;
  sm.wrapS=wrap_mode(o.wrap_s());sm.wrapT=wrap_mode(o.wrap_t());
  sm.minFilter=filter(o.min_filter());sm.magFilter=filter(o.mag_filter());
  sm.lodBias=o.lod_bias();sm.minLod=o.min_lod();sm.maxLod=o.max_lod();

  // Aurora resolves EFB copies by identity of GXTexObj::data. Prefer that GPU copy
  // even when the destination pointer looks like ordinary readable guest memory.
  if (aurora::gx::g_gxState.copyTextures.find(o.data) != aurora::gx::g_gxState.copyTextures.end()) {
    out.dynamicCopy=true;
    return out;
  }
  if(!o.has_data()){
    out.dynamicCopy=static_cast<bool>(bind.ref);
    return out;
  }
  bool supported=false;
  const auto f=texture_format(static_cast<GXTexFmt>(o.format()),supported);
  if(!supported)return out;
  auto& d=out.texture;
  d.width=o.width();d.height=o.height();d.format=f;d.data=o.data;
  d.sourceId=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(o.data));
  d.revision=o.texDataVersion;d.cacheable=!o.no_cache();
  d.mipCount=static_cast<uint8_t>(std::clamp<u32>(o.mip_count(),1,16));
  d.dataSize=gfx::encoded_mip_chain_size(d.width,d.height,d.format,d.mipCount);
  if(f==gfx::TextureFormat::C4||f==gfx::TextureFormat::C8||f==gfx::TextureFormat::C14X2){
    const auto ti=static_cast<unsigned>(o.tlut);if(ti>=aurora::gx::MaxTluts)return out;
    const auto&t=aurora::gx::g_gxState.loadedTluts[ti];
    d.palette=t.data;d.paletteSourceId=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(t.data));d.paletteSize=static_cast<size_t>(t.numEntries)*2;
    d.paletteFormat=palette_format(t.format);d.paletteRevision=t.tlutDataVersion;
    if(!d.palette||d.paletteFormat==gfx::PaletteFormat::None)return out;
  }
  out.valid=true;
  return out;
}

gfx::PrimitiveExpansionState translate_primitive_expansion(uint8_t lineMode) noexcept {
  const auto&g=aurora::gx::g_gxState;gfx::PrimitiveExpansionState e{};e.viewportWidth=std::max(g.renderViewport.width,1.f);e.viewportHeight=std::max(g.renderViewport.height,1.f);const float sx=e.viewportWidth/std::max(g.logicalViewport.width,1.f),sy=e.viewportHeight/std::max(g.logicalViewport.height,1.f),scale=std::min(sx,sy);
  e.lineWidthPixels=(static_cast<float>(g.lineWidth)/6.f)*scale;e.pointSizePixels=(static_cast<float>(g.pointSize)/6.f)*scale;e.lineTexOffset=tex_offset(g.lineTexOffset);e.pointTexOffset=tex_offset(g.pointTexOffset);e.lineTexcoordMask=texcoord_mask(false);e.pointTexcoordMask=texcoord_mask(true);(void)lineMode;return e;
}

gfx::Viewport translate_viewport() noexcept {
  const auto& v = aurora::gx::g_gxState.renderViewport;
  return {v.left, v.top, v.width, v.height, v.znear, v.zfar};
}

gfx::Scissor translate_scissor() noexcept {
  const auto& s = aurora::gx::g_gxState.renderScissor;
  return {s.x, s.y, s.width, s.height};
}

} // namespace aurora::vita::gxbridge
#endif
