#include "../aurora_vita_backend.hpp"
#include "../gfx/vita_draw_adapter.hpp"
#include "../gfx/vita_renderer.hpp"
#include "../gfx/vita_pipeline_key.hpp"
#include "../gfx/vita_streaming_arena.hpp"
#if defined(__vita__)
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#else
#include <chrono>
#endif
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace aurora::vita::gfx;
namespace {
struct Vtx { float x,y,z; uint8_t c0[4]; uint8_t c1[4]; float u,v; };
constexpr std::array<Vtx,4> quad{{
 {-0.75f,-0.65f,0.5f,{255,60,60,255},{255,255,255,255},0,1},
 { 0.75f,-0.65f,0.5f,{60,255,80,255},{255,255,255,255},1,1},
 {-0.75f, 0.65f,0.5f,{60,100,255,255},{255,255,255,255},0,0},
 { 0.75f, 0.65f,0.5f,{255,255,255,255},{255,255,255,255},1,0},
}};
constexpr std::array<uint16_t,6> qidx{{0,1,2,2,1,3}};
VertexLayout layout(){VertexLayout l{};l.count=4;l.attributes[0]={0,3,VertexScalar::F32,false,sizeof(Vtx),0};l.attributes[1]={1,4,VertexScalar::U8,true,sizeof(Vtx),12};l.attributes[2]={2,4,VertexScalar::U8,true,sizeof(Vtx),16};l.attributes[3]={3,2,VertexScalar::F32,false,sizeof(Vtx),20};return l;}
PipelineDesc base_pipe(){PipelineDesc p{};p.layout=layout();p.cull=CullMode::None;p.depthTest=true;p.depthWrite=true;p.depthFunc=Compare::LessEqual;p.reversedZ=false;p.tev.stageCount=1;auto&s=p.tev.stages[0];s.color={TevColorArg::Zero,TevColorArg::RasColor,TevColorArg::One,TevColorArg::Zero};s.alpha={TevAlphaArg::Zero,TevAlphaArg::RasAlpha,TevAlphaArg::Konst,TevAlphaArg::Zero};s.konstAlpha=KonstAlphaSel::One;return p;}
std::array<uint8_t,32> rgb565_tex(){std::array<uint8_t,32> d{};for(int y=0;y<4;y++)for(int x=0;x<4;x++){uint16_t r=(x*31/3),g=(y*63/3),b=((x+y)*31/6);uint16_t v=(r<<11)|(g<<5)|b;size_t o=(y*4+x)*2;d[o]=v>>8;d[o+1]=v&255;}return d;}
struct Assets {Handle vb=0,ib=0,tex=0,efb=0;uint64_t ras=0,texp=0,tev2=0,blend=0,multi=0;};
struct PhasePerf {
  uint64_t frames=0;
  uint64_t workUs=0,submitUs=0,drainUs=0,frameUs=0;
  uint64_t workMin=0,workMax=0,frameMin=0,frameMax=0;
  uint64_t draws=0,triangles=0;
};
uint64_t probe_now_us() noexcept {
#if defined(__vita__)
  return sceKernelGetProcessTimeWide();
#else
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}
void add_perf(PhasePerf& p,uint64_t workUs,uint64_t submitUs,uint64_t drainUs,uint64_t frameUs,const FrameStats& s) noexcept {
  ++p.frames;p.workUs+=workUs;p.submitUs+=submitUs;p.drainUs+=drainUs;p.frameUs+=frameUs;p.draws+=s.drawCalls;p.triangles+=s.triangles;
  if(p.frames==1){p.workMin=p.workMax=workUs;p.frameMin=p.frameMax=frameUs;}
  else {p.workMin=std::min(p.workMin,workUs);p.workMax=std::max(p.workMax,workUs);p.frameMin=std::min(p.frameMin,frameUs);p.frameMax=std::max(p.frameMax,frameUs);}
}
Assets make_assets(Renderer&r){Assets a{};a.vb=r.create_vertex_buffer(quad.data(),sizeof(quad));a.ib=r.create_index_buffer(qidx.data(),sizeof(qidx));auto pd=base_pipe();a.ras=r.create_pipeline(pd);pd.tev.stages[0].color={TevColorArg::Zero,TevColorArg::TexColor,TevColorArg::One,TevColorArg::Zero};pd.tev.stages[0].alpha={TevAlphaArg::Zero,TevAlphaArg::TexAlpha,TevAlphaArg::Konst,TevAlphaArg::Zero};pd.tev.stages[0].texture=0;pd.tev.stages[0].texCoord=0;a.texp=r.create_pipeline(pd);
  pd.tev.stageCount=2;auto&s1=pd.tev.stages[1];s1.color={TevColorArg::Prev,TevColorArg::RasColor,TevColorArg::Half,TevColorArg::Zero};s1.alpha={TevAlphaArg::PrevA,TevAlphaArg::RasAlpha,TevAlphaArg::Konst,TevAlphaArg::Zero};s1.konstAlpha=KonstAlphaSel::FourEighths;a.tev2=r.create_pipeline(pd);
  auto multiPd=pd;multiPd.primitive=Primitive::TriangleStrip;a.multi=r.create_pipeline(multiPd);
  pd.blendMode=BlendMode::Blend;pd.srcFactor=BlendFactor::SrcAlpha;pd.dstFactor=BlendFactor::OneMinusSrcAlpha;a.blend=r.create_pipeline(pd);
  static auto raw=rgb565_tex();TextureDesc td{};td.width=4;td.height=4;td.format=TextureFormat::RGB565;td.data=raw.data();td.dataSize=raw.size();td.sourceId=0x415652564954415Full;a.tex=r.create_texture(td);a.efb=r.create_efb(480,272,true);return a;}
DrawPacket packet(const Assets&a,uint64_t pipe){DrawPacket d{};d.pipelineKey=pipe;d.vertices={a.vb,0,sizeof(quad)};d.indices={a.ib,0,sizeof(qidx)};d.vertexCount=4;d.indexCount=6;d.textures[0].texture=a.tex;d.viewport={0,0,960,544,0,1};d.scissor={0,0,960,544};return d;}
void translate(DrawUniforms&u,float x,float y,float sx=1,float sy=1){u.mvp={sx,0,0,0, 0,sy,0,0, 0,0,1,0, x,y,0,1};}
PipelineDesc batch_pipe(){auto pd=base_pipe();pd.layout=canonical_vertex_layout();pd.tev.stages[0].color={TevColorArg::Zero,TevColorArg::TexColor,TevColorArg::One,TevColorArg::Zero};pd.tev.stages[0].alpha={TevAlphaArg::Zero,TevAlphaArg::TexAlpha,TevAlphaArg::Konst,TevAlphaArg::Zero};pd.tev.stages[0].texture=0;pd.tev.stages[0].texCoord=0;pd.tev.stageCount=2;auto&s1=pd.tev.stages[1];s1.color={TevColorArg::Prev,TevColorArg::RasColor,TevColorArg::Half,TevColorArg::Zero};s1.alpha={TevAlphaArg::PrevA,TevAlphaArg::RasAlpha,TevAlphaArg::Konst,TevAlphaArg::Zero};s1.konstAlpha=KonstAlphaSel::FourEighths;return pd;}
const PreparedDraw& batch_prepared(){static const PreparedDraw p=[](){PreparedDraw d{};d.vertices.resize(4);for(size_t i=0;i<quad.size();i++){auto&v=d.vertices[i];v.position[0]=quad[i].x;v.position[1]=quad[i].y;v.position[2]=quad[i].z;v.position[3]=1.f;std::copy(std::begin(quad[i].c0),std::end(quad[i].c0),v.color0);std::copy(std::begin(quad[i].c1),std::end(quad[i].c1),v.color1);v.texcoord[0][0]=quad[i].u;v.texcoord[0][1]=quad[i].v;v.texcoord[0][2]=1.f;}d.indices.assign(qidx.begin(),qidx.end());d.primitive=Primitive::Triangles;return d;}();return p;}
void scene(Renderer&r,StreamingArena&batchArena,const Assets&a,unsigned phase,uint64_t frame){CommandStream cs;ClearCommand cc{};const float pulse=0.02f*(1.f+std::sin(frame*0.03f));cc.color={0.015f+pulse,0.025f,0.06f+phase*0.012f,1};cc.depth=1.f;cs.clear(cc);
  if(phase==0){r.execute(cs);return;}
  if(phase==7&&a.efb){r.execute(cs);r.bind_efb(a.efb);CommandStream off;ClearCommand oc{};oc.color={0.06f,0.01f,0.08f,1};off.clear(oc);auto d=packet(a,a.tev2);d.viewport={0,0,480,272,0,1};d.scissor={0,0,480,272};d.textures[0].texture=a.tex;off.draw(d);r.execute(off);r.blit_efb(a.efb);return;}
  if(phase==1){auto d=packet(a,a.ras);d.indices={};d.indexCount=0;d.vertexCount=3;cs.draw(d);}
  else if(phase==2){auto d=packet(a,a.ras);cs.draw(d);}
  else if(phase==3){cs.draw(packet(a,a.texp));}
  else if(phase==4){auto d=packet(a,a.blend);d.uniforms.tevreg[0][3]=0.65f;translate(d.uniforms,-0.22f,0.1f,0.72f,0.72f);cs.draw(d);translate(d.uniforms,0.22f,-0.1f,0.72f,0.72f);cs.draw(d);}
  else if(phase==5){cs.draw(packet(a,a.tev2));}
  else if(phase==10){
#if defined(__vita__)
    auto d=packet(a,a.multi);d.indices={};d.indexCount=0;d.vertexCount=4;translate(d.uniforms,0.f,0.f,0.055f,0.07f);r.draw(d);
    static const std::array<GLint,1000> first=[](){std::array<GLint,1000> v{};return v;}();
    static const std::array<GLsizei,1000> counts=[](){std::array<GLsizei,1000> v{};v.fill(4);return v;}();
    glMultiDrawArrays(GL_TRIANGLE_STRIP,first.data(),counts.data(),static_cast<GLsizei>(counts.size()));
    return;
#else
    auto d=packet(a,a.multi);d.indices={};d.indexCount=0;d.vertexCount=4;translate(d.uniforms,0.f,0.f,0.055f,0.07f);cs.draw(d);
#endif
  }else if(phase==11){
    r.execute(cs);batchArena.begin_frame(frame);CommandStream batched;auto pd=batch_pipe();DrawUniforms u{};translate(u,0.f,0.f,0.055f,0.07f);Viewport vp{0,0,960,544,0,1};Scissor sc{0,0,960,544};std::array<TextureBinding,MaxTextures> textures{};textures[0].texture=a.tex;PrepareDrawError e{};
    const uint64_t resolvedPipeline=resolve_draw_pipeline(r,batch_prepared(),pd);
    for(unsigned i=0;i<1000;i++)if(!enqueue_draw(r,batchArena,batched,batch_prepared(),pd,u,vp,sc,textures,&e,nullptr,resolvedPipeline))break;
    if(batchArena.flush()) r.execute(batched);
    return;
  }else {const unsigned count=phase==6?200:1000;for(unsigned i=0;i<count;i++){auto d=packet(a,a.tev2);if(phase!=9){float x=((int)(i%20)-9.5f)*0.09f,y=((int)((i/20)%12)-5.5f)*0.13f;translate(d.uniforms,x,y,0.055f,0.07f);}else translate(d.uniforms,0.f,0.f,0.055f,0.07f);cs.draw(d);} }
  r.execute(cs);
}
const char* pname(unsigned p){static const char* n[]={"M0 clear","M1 geometry","M2 indexed vertex","M3 Wii texture decode","M4 fixed state/blend","M5 2-stage TEV","M6 command batching 200 draws","M7 EFB offscreen/copy","M8 benchmark 1000 draws","M9 benchmark 1000 fixed MVP","M10 vitaGL multidraw 1000 fixed MVP","M11 GX adapter coalesced 1000 draws"};return p<12?n[p]:"?";}
#if defined(__vita__)
constexpr const char* ProbeStatusPath="ux0:data/aurora-vita/probe_status.log";
void probe_status(bool truncate,const char* fmt,...) noexcept {
  sceIoMkdir("ux0:data/aurora-vita",0777);
  FILE* fp=std::fopen(ProbeStatusPath,truncate?"wb":"ab");
  if(!fp)return;
  va_list ap;va_start(ap,fmt);std::vfprintf(fp,fmt,ap);va_end(ap);
  std::fclose(fp);
}
void log_phase_perf(unsigned phase,const PhasePerf& p) noexcept {
  if(!p.frames)return;
  probe_status(false,
               "[probe][perf] phase=%u %s frames=%llu work_us min/avg/max=%llu/%llu/%llu submit_avg_us=%llu drain_avg_us=%llu frame_us min/avg/max=%llu/%llu/%llu avg_draws=%llu avg_tris=%llu%s\n",
               phase,pname(phase),(unsigned long long)p.frames,
               (unsigned long long)p.workMin,(unsigned long long)(p.workUs/p.frames),(unsigned long long)p.workMax,
               (unsigned long long)(p.submitUs/p.frames),(unsigned long long)(p.drainUs/p.frames),
               (unsigned long long)p.frameMin,(unsigned long long)(p.frameUs/p.frames),(unsigned long long)p.frameMax,
               (unsigned long long)(p.draws/p.frames),(unsigned long long)(p.triangles/p.frames),
               (phase==6||phase==8||phase==9||phase==10||phase==11)?" gpu_sync=glFinish":"");
}
#endif
}
int main(){
#if defined(__vita__)
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  probe_status(true,"[probe] boot\n");
#if defined(AURORA_VITAGL_MAPPED_STREAM_UPLOAD)
  probe_status(false,"[probe] build local_vitagl=1 no_debug=1 draw_speedhack=1 indices_draw_speedhack=1 buffers_speedhack=0 stream_upload=map_range cpu_fastpath=rebased_indices+pipeline_run_cache\n");
#else
  probe_status(false,"[probe] build buffers_speedhack=0 stream_upload=buffer_sub_data\n");
#endif
#endif
  aurora::vita::BackendConfig cfg{};
  cfg.vgl_legacy_pool_size=0;
  cfg.vgl_ram_threshold=16*1024*1024;
  cfg.texture_cache_budget=24*1024*1024;
  cfg.wait_vblank=true;
  cfg.diagnostics=true;
  cfg.strict_unsupported=false;
#if defined(__vita__)
  cfg.diagnostics_period_frames=300;
  cfg.telemetry_log_path="ux0:data/aurora-vita/probe_telemetry.log";
  cfg.coverage_log_path="ux0:data/aurora-vita/probe_coverage.log";
  cfg.trace_log_path="ux0:data/aurora-vita/probe_trace.log";
#else
  cfg.diagnostics_period_frames=0;
#endif
  if(!aurora::vita::initialize(cfg)){
#if defined(__vita__)
    probe_status(false,"[probe] backend initialize FAILED reason=%u detail=%s\n",
                 static_cast<unsigned>(aurora::vita::last_init_failure()),
                 aurora::vita::last_init_failure_detail());
#endif
    return 1;
  }
  auto&r=aurora::vita::renderer();
  auto a=make_assets(r);
  StreamingArena batchArena(r.buffers(),{1024*1024,128*1024,3,4});
  if(!batchArena.initialize())return 2;
  unsigned phase=0;PhasePerf phasePerf{};
#if defined(__vita__)
  uint32_t prev=0;
  probe_status(false,"[probe] backend initialize PASS 960x544 msaa=none\n");
  probe_status(false,"[probe] assets vb=%u ib=%u tex=%u efb=%u pipelines ras=%llu tex=%llu tev2=%llu blend=%llu\n",
               (unsigned)a.vb,(unsigned)a.ib,(unsigned)a.tex,(unsigned)a.efb,
               (unsigned long long)a.ras,(unsigned long long)a.texp,(unsigned long long)a.tev2,(unsigned long long)a.blend);
  probe_status(false,"[probe] phase=0 %s\n",pname(0));
#endif
  std::printf("[aurora-vita] all-phases probe ready. SELECT cycles phases, START exits\n");bool run=true;uint64_t report=0;
  while(run){
#if defined(__vita__)
    uint32_t buttons=0;
    SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);buttons=pad.buttons;
    if((buttons&SCE_CTRL_START)&&!(prev&SCE_CTRL_START)){log_phase_perf(phase,phasePerf);run=false;}
    if((buttons&SCE_CTRL_SELECT)&&!(prev&SCE_CTRL_SELECT)){log_phase_perf(phase,phasePerf);phasePerf={};phase=(phase+1)%12;std::printf("[aurora-vita] phase=%u %s\n",phase,pname(phase));probe_status(false,"[probe] phase=%u %s frame=%llu\n",phase,pname(phase),(unsigned long long)aurora::vita::frame_index());}
    prev=buttons;
#else
    #ifndef AURORA_VITA_HOST_SMOKE_FRAMES
#define AURORA_VITA_HOST_SMOKE_FRAMES 72
#endif
    if(aurora::vita::frame_index()>AURORA_VITA_HOST_SMOKE_FRAMES) run=false;
    phase=(aurora::vita::frame_index()/120)%12;
#endif
    if(!run||!aurora::vita::begin_frame()) break;
#if defined(__vita__)
    const bool throughputPhase=phase==6||phase==8||phase==9||phase==10||phase==11;
    // Start throughput samples from an idle GPU so previous present work cannot
    // be charged to this frame's submit/drain split.
    if(throughputPhase)glFinish();
#endif
    const uint64_t workStart=probe_now_us();
    scene(r,batchArena,a,phase,aurora::vita::frame_index());
    const uint64_t submitUs=probe_now_us()-workStart;
    uint64_t drainUs=0;
#if defined(__vita__)
    // Throughput phases force completion before stopping the work
    // timer so the result includes GPU execution instead of just CPU enqueue.
    if(throughputPhase){const uint64_t drainStart=probe_now_us();glFinish();drainUs=probe_now_us()-drainStart;}
#endif
    const uint64_t workUs=submitUs+drainUs;
    aurora::vita::end_frame();
    add_perf(phasePerf,workUs,submitUs,drainUs,aurora::vita::last_frame_time_us(),r.stats());
    if(aurora::vita::frame_index()-report>=300){report=aurora::vita::frame_index();const auto&s=r.stats();auto us=aurora::vita::last_frame_time_us();std::printf("[aurora-vita] %s frame=%llu %llu us %.2f fps draws=%u tris=%u pipe(h/m)=%u/%u tex(h/m/up)=%u/%u/%u states=%u\n",pname(phase),(unsigned long long)report,(unsigned long long)us,us?1000000.0/(double)us:0.0,s.drawCalls,s.triangles,s.pipelineHits,s.pipelineMisses,s.textureHits,s.textureMisses,s.textureUploads,s.stateChanges);}}
  batchArena.shutdown();aurora::vita::shutdown();
#if defined(__vita__)
  probe_status(false,"[probe] clean exit frame=%llu\n",(unsigned long long)aurora::vita::frame_index());
  sceKernelExitProcess(0);
#endif
  return 0;}
