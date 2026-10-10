#include "backend.hpp"
#include "render_config.hpp"
#include "aurora_vita_backend.hpp"
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <array>
#include <cctype>
#include <cstdio>
#include <vector>

extern "C" bool WicReadNativeAsset(const char*,std::vector<uint8_t>&,void*) noexcept __attribute__((weak));

namespace wiicompiled::backend {
namespace {
struct UiVertex { float x,y;uint8_t r,g,b; };
std::vector<UiVertex> vertices;
// Small monochrome diagnostic font, owned by the frontend. The game uses its
// original UI/font assets and never depends on this launcher font.
constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .:/-_";
constexpr std::array<std::array<uint8_t,5>,42> glyphs{{
 {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
 {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
 {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0,0x41,0x7f,0x41,0},
 {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
 {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
 {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
 {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
 {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
 {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x62,0x51,0x49,0x49,0x46},
 {0x22,0x41,0x49,0x49,0x36},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
 {6,0x49,0x49,0x29,0x1e},{0,0,0,0,0},{0,0x60,0x60,0,0},{0,0x36,0x36,0,0},
 {0x20,0x10,8,4,2},{8,8,8,8,8},{0x40,0x40,0x40,0x40,0x40}
}};
void setup() {
    AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
    const float projection[4][4]{{2.f/960,0,0,-1},{0,-2.f/544,0,1},{0,0,-1,0},{0,0,0,1}};
    const float identity[3][4]{{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    GXSetProjection(projection,GX_ORTHOGRAPHIC);GXLoadPosMtxImm(identity,GX_PNMTX0);GXSetCurrentMtx(GX_PNMTX0);
    GXSetViewport(0,0,960,544,0,1);GXSetScissor(0,0,960,544);GXSetCullMode(GX_CULL_NONE);
    GXSetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GXSetColorUpdate(GX_TRUE);GXSetAlphaUpdate(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GXSetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GXSetNumChans(1);GXSetChanCtrl(GX_COLOR0A0,GX_FALSE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
    GXSetNumTexGens(0);GXSetNumTevStages(1);GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);GXClearVtxDesc();GXSetVtxDesc(GX_VA_POS,GX_DIRECT);GXSetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GXSetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
}
}
bool initialize(bool diagnosticsEnabled,const VitaRenderOptions& options) {
    aurora::vita::BackendConfig config;
    // RMCP01 uses a 608x456 visible VI image and renders light maps below it.
    // Preserve the full 640x528 GX EFB at that scale (1011x630 physical),
    // rounded up for GXM storage, without changing the 960x544 visible raster.
    config.efb_width=1024;config.efb_height=640;
    std::fprintf(stderr,"[efb-config] visible=960x544 storage=1024x640 lightmap_scratch=1\n");
    config.data_root_path="ux0:data/wiicompiled-vita/aurora";
    config.static_geometry_budget=WIC_RESIDENT_GEOMETRY?8*1024*1024:0;
    config.resident_geometry_cdram=WIC_RESIDENT_GEOMETRY != 0;
    config.gxm_lit_fixed_vertex_gpu=WIC_RESIDENT_GEOMETRY != 0;
    config.texture_snapshot_budget=WIC_RESIDENT_TEXTURES?8*1024*1024:0;
    config.gxm_local_draw_batching=false;
    config.cpu_worker_threads=2;
    config.cpu_distinct_core_dispatch=WIC_DISTINCT_CPU_CORES != 0;
    apply_render_options(config,options);
    config.resident_geometry_cdram=config.static_geometry_budget!=0;
    config.cpu_distinct_core_dispatch=WIC_DISTINCT_CPU_CORES != 0 || options.distinctCpuDispatch;
    config.startup_progress=[](const char* phase,size_t completed,size_t total,void*) {
        if(!completed || completed==total || completed%32==0)
            std::fprintf(stderr,"[render-prewarm] phase=%s completed=%u total=%u\n",
                phase,unsigned(completed),unsigned(total));
    };
    std::fprintf(stderr,"[native-render-config] shader_preload=%u shader_limit=%u pipeline_limit=%u prepared_dl=%u draw_batching=%u immediate_view=%u fragment_cache=%u uniform_pool=%u geometry_preflight=%u min_vertices=%u gpu_lighting=%u native_materials=%u helpers=%u renderer_lanes=%u distinct_dispatch=%u direct_submit=%u direct_stream=%u\n",
        unsigned(config.gxm_preload_program_cache),unsigned(config.gxm_program_cache_preload_limit),
        unsigned(config.pipeline_prewarm_limit),unsigned(config.prepared_display_lists),
        unsigned(config.gxm_local_draw_batching),unsigned(config.gxm_immediate_draw_view),
        unsigned(config.gxm_fragment_prepare_cache),unsigned(config.gxm_fixed_uniform_pool),
        unsigned(config.gxm_geometry_preflight),unsigned(config.static_geometry_min_vertices),
        unsigned(config.gxm_lit_fixed_vertex_gpu),unsigned(options.nativeMaterials),
        unsigned(config.cpu_worker_threads),unsigned(config.cpu_renderer_execution_lanes),
        unsigned(config.cpu_distinct_core_dispatch),unsigned(WIC_NATIVE_DIRECT_SUBMIT),unsigned(WIC_NATIVE_DIRECT_STREAM));
#if WIC_PRECOMPILED_ASSETS
    config.native_asset_reader=WicReadNativeAsset;
#endif
    // Diagnostic candidate: count fallback textures and retain a bounded draw
    // trace without changing TEV, scissor, scene ordering or copy semantics.
    config.performance_attribution=true;
    config.diagnostics_enabled=diagnosticsEnabled;
    config.texture_decode_diagnostics=true;
    config.coverage_log_path="ux0:data/wiicompiled-vita/aurora/diagnostics/wic-coverage.log";
    config.trace_log_path="ux0:data/wiicompiled-vita/aurora/diagnostics/wic-draw-trace.log";
    config.trace_capacity=256;
    std::fprintf(stderr,"[resident-config] geometry_cdram=%u geometry_budget=%u texture_snapshots=%u texture_budget=%u snapshot_source_limit=%u snapshot_entry_limit=%u\n",
        unsigned(config.resident_geometry_cdram),unsigned(config.static_geometry_budget),
        unsigned(config.texture_snapshot_budget),unsigned(config.texture_cache_budget),
        unsigned(aurora::vita::gfx::TextureSnapshots::MaxSourceBytes),
        unsigned(aurora::vita::gfx::TextureSnapshots::MaxEntries));
    if(!aurora::vita::initialize(config))return false;
    GXInit(nullptr,0);return true;
}
void shutdown() { aurora::vita::shutdown(); }
bool beginFrame() { if(!aurora::vita::begin_frame())return false;vertices.clear();setup();return true; }
void endFrame() {
    constexpr size_t maxVertices=65532; // Complete quads within GX's u16 count.
    for(size_t first=0;first<vertices.size();first+=maxVertices) {
        const size_t count=std::min(maxVertices,vertices.size()-first);
        GXBegin(GX_QUADS,GX_VTXFMT0,static_cast<uint16_t>(count));
        for(size_t i=first;i<first+count;++i) {
            const auto& v=vertices[i];GXPosition3f32(v.x,v.y,0);GXColor4u8(v.r,v.g,v.b,255);
        }
        GXEnd();
    }
    aurora::vita::end_frame();
}
void rectangle(float x,float y,float w,float h,uint8_t r,uint8_t g,uint8_t b) {
    vertices.push_back({x,y,r,g,b});vertices.push_back({x+w,y,r,g,b});
    vertices.push_back({x+w,y+h,r,g,b});vertices.push_back({x,y+h,r,g,b});
}
void text(float x,float y,const char* value,float scale) {
    for(;*value;++value,x+=6*scale) {
        const char c=static_cast<char>(std::toupper(static_cast<unsigned char>(*value)));
        size_t index=0;while(alphabet[index]&&alphabet[index]!=c)++index;
        if(index>=glyphs.size())continue;
        for(unsigned col=0;col<5;++col)for(unsigned row=0;row<7;++row)
            if(glyphs[index][col]&(1u<<row))rectangle(x+col*scale,y+row*scale,scale,scale,230,235,245);
    }
}
}
