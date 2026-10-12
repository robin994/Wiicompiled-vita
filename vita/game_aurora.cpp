#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include "aurora_vita_backend.hpp"
#include <aurora_vita_view_draw_capture.h>
#include "gfx/vita_memory_revision.hpp"
#include "gfx/vita_renderer.hpp"
#include "gx/fifo.hpp"
#include "host_context.h"
#include "runtime_config.h"
#include "game_diagnostics.hpp"
#include "game_gx_stall_watch.hpp"
#if WIC_PRECOMPILED_ASSETS
#include "game_time_trial.hpp"
#include "native_assets.hpp"
#include "native_scene_prewarm.hpp"
#include "memory.h"
#endif
#include "gx/aurora_vita_draw_sink.hpp"
#include <dolphin/gx.h>
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <type_traits>
#include <cstdio>
#include <array>
#include <atomic>
#include <fstream>
#include <optional>
#include <vector>

void WicLogAudioHealth();
extern "C" uint32_t aurora_vita_gxm_kernel_operation() noexcept;

namespace aurora::gx::fifo {
// The runtime's raw packets already use GX big-endian vertex fields. Put the
// header and payload through the normal producer stream so state and draws
// retain FIFO ordering; GXEnd drains only outside a recorded display list.
bool submit_raw_draw(GXPrimitive primitive,GXVtxFmt format,const uint8_t* vertices,
                     uint16_t count,uint32_t bytes) {
    if(!vertices || !count || !bytes || bytes%count || format>=GX_MAX_VTXFMT)return false;
    GXBegin(primitive,format,count);write_data(vertices,bytes);GXEnd();return true;
}
}

namespace {
std::atomic<uint64_t> sWatchCompletedFrame{0};
#if WIC_PRECOMPILED_ASSETS
std::optional<uint64_t> NativeAssetSceneKey() {
    const auto snapshot=wiicompiled::vita::time_trial::snapshot_progress();
    using Snapshot=wiicompiled::vita::time_trial::ProgressSnapshot;
    if(!(snapshot.valid&Snapshot::SectionValid) || snapshot.sectionId>255)return std::nullopt;
    const uint32_t section=snapshot.sectionId;
    const uint32_t scene=snapshot.sceneId<=255?snapshot.sceneId:255;
    uint32_t course=255,character=255,kart=255;
    if(section==0x1fu) { // PAL time-trial/race section: distinguish course and driver.
        constexpr uint32_t managerAddress=0x809c1e38u;
        const uint32_t manager=Memory::Read32(managerAddress);
        if(!manager || !Memory::Contains(manager,0x9c))return std::nullopt;
        const uint32_t context=Memory::Read32(manager+0x98);
        if(!context || !Memory::Contains(context+0x14c,4))return std::nullopt;
        course=Memory::Read32(context+0x14c);
        character=Memory::Read32(context+0x12c);
        kart=Memory::Read32(context+0x13c);
        if(course>255 || character>255 || kart>255)return std::nullopt;
    }
    return wiicompiled::vita::assets::prewarm::scene_key(section,scene,course,character,kart);
}
void NativeAssetSceneTick(uint64_t frame) {
    if(!wiicompiled::vita::assets::PrewarmEnabled())return;
    if(frame%8==0) {
        try {
            const auto scene=NativeAssetSceneKey();
            if(scene) {
                static uint64_t previous=0;
                if(*scene!=previous) {
                    const auto started=sceKernelGetProcessTimeWide();
                    const auto warmed=wiicompiled::vita::assets::QueueScene(*scene);
                    previous=*scene;
                    std::fprintf(stderr,
                        "[asset-prewarm] phase=scene-plan frame=%llu scene=%016llx manifest=%u selected=%u pending=%u bytes=%u elapsed_us=%llu\n",
                        static_cast<unsigned long long>(frame),
                        static_cast<unsigned long long>(*scene),unsigned(warmed.manifestLoaded),
                        warmed.selected,warmed.remaining,
                        unsigned(warmed.bytes),
                        static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-started));
                }
            }
        } catch(const Memory::AccessViolation&) {
            // On boot/scene teardown, guest singletons are not yet populated.
            // Never interrupt the game because a prewarm hint is incomplete.
        }
    }
    // Leave the just-presented boundary available for gameplay DVD reads.
    // One <=64 KiB sidecar every eight frames; never stream an entire scene
    // into RAM at once. The ordinary on-demand path handles large textures.
    if(frame%8==4) {
        const auto warm=wiicompiled::vita::assets::StepScenePrewarm(64*1024,1);
        if(warm.loaded || warm.rejected || warm.skippedLarge)
            std::fprintf(stderr,
                "[asset-prewarm] phase=step frame=%llu loaded=%u existing=%u skipped_large=%u rejected=%u bytes=%u remaining=%u\n",
                static_cast<unsigned long long>(frame),
                warm.loaded,warm.alreadyCached,warm.skippedLarge,warm.rejected,
                unsigned(warm.bytes),warm.remaining);
    }
    // Best effort checkpoint for sessions terminated by a crash or power loss.
    // Persist a learned manifest less frequently; transition/shutdown also
    // checkpoint. Avoid writing flash on a two-second gameplay cadence.
    if(frame%512==0)(void)wiicompiled::vita::assets::Checkpoint();
}
#endif
AuroraGuestWriteNotifyCallback writeNotify=nullptr;
uint64_t drawCaptureFrame=0;
bool drawCaptureAttempted=false;

uint64_t CaptureFragmentHash() {
    // A capture-only option intentionally read here rather than added to the
    // shared runtime configuration: enabling a one-frame GPU bisection must
    // not force unrelated guest runtime translation units to rebuild.
    std::ifstream input(RuntimeConfigFile::ResolveConfigPath(),std::ios::binary);
    if(!input)return 0;
    try {
        const auto document=toml::parse(input,"capture-fragment");
        const auto value=RuntimeConfigFile::FindConfigValue<std::string>(
            document,"diagnostics","draw_suppress_fragment_hash");
        if(!value)return 0;
        uint64_t hash=0;
        if(value->size()!=16) {
            std::fprintf(stderr,"[draw-capture] invalid fragment hash length (expected 16 hex digits)\n");
            return 0;
        }
        for(char c:*value) {
            unsigned digit=0;
            if(c>='0'&&c<='9')digit=static_cast<unsigned>(c-'0');
            else if(c>='a'&&c<='f')digit=static_cast<unsigned>(c-'a'+10);
            else if(c>='A'&&c<='F')digit=static_cast<unsigned>(c-'A'+10);
            else {
                std::fprintf(stderr,"[draw-capture] invalid fragment hash digit\n");
                return 0;
            }
            hash=(hash<<4)|digit;
        }
        return hash;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"[draw-capture] failed to parse fragment filter: %s\n",e.what());
        return 0;
    }
}

void PrepareDrawCapture(uint64_t completedFrame) {
    const auto& config=RuntimeConfigFile::Get();
    if(drawCaptureAttempted || !config.diagnosticsEnabled.value_or(true) ||
       !config.diagnosticsCaptureFrame || !*config.diagnosticsCaptureFrame ||
       completedFrame+1!=*config.diagnosticsCaptureFrame)return;
    drawCaptureAttempted=true;
    // The prior end_frame joined the GX consumer. An explicit FIFO marker
    // associates every submission in the next frame with this capture; 255
    // means the application has no more specific view classification.
    const uint32_t first=config.diagnosticsDrawSuppressFirst.value_or(0);
    const uint32_t last=config.diagnosticsDrawSuppressLast.value_or(0);
    const uint64_t fragmentHash=CaptureFragmentHash();
    const bool validRange=(first==0&&last==0) || (first>0&&first<=last&&last<=16384);
    const bool started=validRange && !(fragmentHash&&first) &&
        (fragmentHash ? aurora_vita_view_draw_start_ex_hash(16384,1,fragmentHash):
                        aurora_vita_view_draw_start_ex_range(16384,1,first,last));
    if(started) {
        drawCaptureFrame=completedFrame+1;
        aurora_vita_view_draw_mark(255,drawCaptureFrame);
    }
    std::fprintf(stderr,"[draw-capture] requested=%llu started=%u capacity=16384 payload_hashes=1 suppress_first=%u suppress_last=%u suppress_fragment=%016llx\n",
        static_cast<unsigned long long>(completedFrame+1),unsigned(drawCaptureFrame!=0),first,last,
        static_cast<unsigned long long>(fragmentHash));
}

void FinishDrawCapture(uint64_t completedFrame) {
    if(!drawCaptureFrame || completedFrame!=drawCaptureFrame)return;
    // Only after end_frame has joined the consumer: stop publishes a stable
    // buffer, then read/write it on the game thread. No extra GPU fence here.
    aurora_vita_view_draw_stop();
    const auto count=aurora_vita_view_draw_count();
    const auto lost=aurora_vita_view_draw_lost();
    char path[160]{};
    std::snprintf(path,sizeof(path),"ux0:data/wiicompiled-vita/aurora/diagnostics/frame-%llu-draws.csv",
        static_cast<unsigned long long>(completedFrame));
    FILE* output=std::fopen(path,"wb");
    bool ok=output && std::fprintf(output,
        "sequence,type,producer_frame,consumer_frame,view,draw,target,vertices,indices,tev_stages,texture_mask,texgen_mask,native,indexed_pn,blend,depth,alpha,alpha_ref0,alpha_ref1,alpha_op,scissor,pipeline_requested,pipeline_active,vertex_shader,fragment_shader,vertex_payload,index_payload,uniform_payload,draw_state,payload_hashes,submit_cpu_us,uniform_bytes,diagnostic_suppressed,bounds_valid,bounds_vertices,bounds_x0,bounds_y0,bounds_x1,bounds_y1,tex_handle0,tex_handle1,tex_handle2,tex_handle3,tex_handle4,tex_handle5,tex_handle6,tex_handle7,tev_map_coords,tev_color_args,tev_alpha_args\n")>0;
    std::array<AuroraViewDrawRecord,32> batch{};
    size_t offset=0;bool complete=false;
    while(ok && offset<count) {
        const auto n=aurora_vita_view_draw_read(offset,batch.data(),batch.size());
        if(!n) { ok=false;break; }
        for(size_t i=0;ok && i<n;++i) {
            const auto& r=batch[i];
            if(r.type==AURORA_VIEW_DRAW_FRAME_COMPLETE && r.consumer_frame==completedFrame)complete=true;
            // Each TEV stage contributes four hex digits per field. This is
            // a capture-only representation of the translated GX state, not
            // a shader/source hash or a material name inferred from the AABB.
            char tevMapCoords[65]{},tevColorArgs[65]{},tevAlphaArgs[65]{};
            for(unsigned stage=0;stage<std::min<unsigned>(r.tev_stages,16);++stage) {
                std::snprintf(tevMapCoords+stage*4,5,"%02x%02x",
                              unsigned(r.tev_maps[stage]),unsigned(r.tev_coords[stage]));
                std::snprintf(tevColorArgs+stage*4,5,"%x%x%x%x",
                              unsigned(r.tev_color_args[stage][0]),unsigned(r.tev_color_args[stage][1]),
                              unsigned(r.tev_color_args[stage][2]),unsigned(r.tev_color_args[stage][3]));
                std::snprintf(tevAlphaArgs+stage*4,5,"%x%x%x%x",
                              unsigned(r.tev_alpha_args[stage][0]),unsigned(r.tev_alpha_args[stage][1]),
                              unsigned(r.tev_alpha_args[stage][2]),unsigned(r.tev_alpha_args[stage][3]));
            }
            ok=std::fprintf(output,
                "%llu,%u,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%u,%llu,%llu,%u,%u,%u,%.2f,%.2f,%.2f,%.2f,%u,%u,%u,%u,%u,%u,%u,%u,%s,%s,%s\n",
                static_cast<unsigned long long>(r.sequence),unsigned(r.type),
                static_cast<unsigned long long>(r.producer_frame),static_cast<unsigned long long>(r.consumer_frame),
                unsigned(r.view),r.logical_draw,r.target,r.vertex_count,r.index_count,unsigned(r.tev_stages),
                unsigned(r.texture_mask),unsigned(r.texgen_mask),unsigned(r.program_native),unsigned(r.indexed_pn),
                unsigned(r.blend),unsigned(r.depth),unsigned(r.alpha),unsigned(r.alpha_ref0),unsigned(r.alpha_ref1),
                unsigned(r.alpha_op),unsigned(r.scissor),static_cast<unsigned long long>(r.pipeline_requested),
                static_cast<unsigned long long>(r.pipeline_active),static_cast<unsigned long long>(r.vertex_hash),
                static_cast<unsigned long long>(r.fragment_hash),static_cast<unsigned long long>(r.vertex_payload_hash),
                static_cast<unsigned long long>(r.index_payload_hash),static_cast<unsigned long long>(r.uniform_payload_hash),
                static_cast<unsigned long long>(r.draw_state_hash),unsigned(r.payload_hashes_present),
                static_cast<unsigned long long>(r.submit_cpu_us),static_cast<unsigned long long>(r.uniform_bytes),
                unsigned(r.diagnostic_suppressed),unsigned(r.screen_bounds_valid),
                unsigned(r.projected_vertices),double(r.screen_bounds[0]),double(r.screen_bounds[1]),
                double(r.screen_bounds[2]),double(r.screen_bounds[3]),
                r.texture_handles[0],r.texture_handles[1],r.texture_handles[2],r.texture_handles[3],
                r.texture_handles[4],r.texture_handles[5],r.texture_handles[6],r.texture_handles[7],
                tevMapCoords,tevColorArgs,tevAlphaArgs)>0;
        }
        offset+=n;
    }
    if(output && std::fclose(output)!=0)ok=false;
    std::fprintf(stderr,"[draw-capture] frame=%llu saved=%u complete=%u records=%u lost=%llu path=%s diagnostic=1\n",
        static_cast<unsigned long long>(completedFrame),unsigned(ok),unsigned(complete),unsigned(count),
        static_cast<unsigned long long>(lost),path);
    char texturePath[160]{};
    std::snprintf(texturePath,sizeof(texturePath),
        "ux0:data/wiicompiled-vita/aurora/diagnostics/frame-%llu-textures.csv",
        static_cast<unsigned long long>(completedFrame));
    FILE* texOut=std::fopen(texturePath,"wb");
    bool texturesSaved=texOut&&std::fprintf(texOut,
        "handle,source_id,palette_source_id,source_fnv64,palette_fnv64,source_hash_bytes,palette_hash_bytes,source_bytes,width,height,gx_format,palette_format,mip_count,source_revision,binding_source\n")>0;
    const size_t textureCount=aurora_vita_view_texture_count();
    std::array<AuroraViewTextureRecord,32> textureBatch{};
    for(size_t pos=0;texturesSaved&&pos<textureCount;) {
        const auto n=aurora_vita_view_texture_read(pos,textureBatch.data(),textureBatch.size());
        if(!n){texturesSaved=false;break;}
        for(size_t i=0;texturesSaved&&i<n;++i) {
            const auto& t=textureBatch[i];
            texturesSaved=std::fprintf(texOut,
                "%u,%016llx,%016llx,%016llx,%016llx,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                t.handle,
                static_cast<unsigned long long>(t.source_id),
                static_cast<unsigned long long>(t.palette_source_id),
                static_cast<unsigned long long>(t.source_hash),
                static_cast<unsigned long long>(t.palette_hash),
                t.source_hash_bytes,t.palette_hash_bytes,t.source_bytes,
                t.width,t.height,unsigned(t.gx_format),unsigned(t.palette_format),
                unsigned(t.mip_count),t.source_revision,unsigned(t.binding_source))>0;
        }
        pos+=n;
    }
    if(texOut&&std::fclose(texOut)!=0)texturesSaved=false;
    std::fprintf(stderr,
        "[draw-capture] texture_table frame=%llu saved=%u count=%u lost=%llu path=%s diagnostic=1\n",
        static_cast<unsigned long long>(completedFrame),unsigned(texturesSaved),
        unsigned(textureCount),static_cast<unsigned long long>(lost),texturePath);
    // Upload fingerprinting is only needed up to this one-shot comparison.
    aurora_vita_view_draw_set_texture_probe(0);
    drawCaptureFrame=0;
}

void CaptureFrame(uint64_t frame) {
    // Bounded captures only. Readback inserts a GPU fence, so these runs
    // are for correctness diagnosis, never performance comparison.
    const auto requested=RuntimeConfigFile::Get().diagnosticsCaptureFrame;
    const bool explicitOneShot=requested&&*requested==frame;
    if(explicitOneShot) {
        // end_frame enqueues the display callback asynchronously. A screenshot
        // taken immediately after the readback may still show the prior frame.
        const int presentFinish=sceGxmDisplayQueueFinish();
        std::fprintf(stderr,"[frame-capture] frame=%llu display_queue_finish=%d diagnostic=1\n",
            static_cast<unsigned long long>(frame),presentFinish);
        if(presentFinish<0)return;
    }
    std::vector<uint8_t> rgba;
    if(!aurora::vita::renderer().readback_rgba8(rgba) || rgba.size()!=960*544*4) {
        std::fprintf(stderr,"[frame-capture] frame=%llu failed=%s\n",
            static_cast<unsigned long long>(frame),aurora::vita::renderer().last_error());
        return;
    }
    char path[160]{};
    std::snprintf(path,sizeof(path),"ux0:data/wiicompiled-vita/aurora/diagnostics/frame-%llu.ppm",
        static_cast<unsigned long long>(frame));
    FILE* output=std::fopen(path,"wb");
    bool ok=output && std::fprintf(output,"P6\n960 544\n255\n")>0;
    std::array<uint8_t,960*3> row{};
    for(size_t y=0;ok&&y<544;++y) {
        for(size_t x=0;x<960;++x)
            for(size_t c=0;c<3;++c)row[x*3+c]=rgba[(y*960+x)*4+c];
        ok=std::fwrite(row.data(),1,row.size(),output)==row.size();
    }
    if(output && std::fclose(output)!=0)ok=false;
    std::fprintf(stderr,"[frame-capture] frame=%llu saved=%u path=%s\n",
        static_cast<unsigned long long>(frame),unsigned(ok),path);
    // The Vita3K probe polls every two seconds. Keep this single completed
    // frame visible long enough to capture the actual presented surface.
    // This does not affect ordinary execution or unsolicited captures.
    if(explicitOneShot)sceKernelDelayThread(4000000);
}

template <class Callback>
auto OnNativeStack(Callback callback) {
    using Result = decltype(callback());
    if constexpr (std::is_void_v<Result>) {
        HostContext::RunOnNativeStack([](void* p) { (*static_cast<Callback*>(p))(); }, &callback);
    } else {
        struct Call { Callback* callback; Result result{}; } call{&callback};
        HostContext::RunOnNativeStack([](void* p) {
            auto& call = *static_cast<Call*>(p);
            call.result = (*call.callback)();
        }, &call);
        return call.result;
    }
}
}

extern "C" {
void WicReadGxProgress(WicGxProgress* out) noexcept {
    if(!out)return;
    const auto worker=aurora::gx::fifo::worker_progress();
    out->frame=sWatchCompletedFrame.load(std::memory_order_acquire);
    out->submitted=worker.submitted;
    out->completed=worker.completed;
    out->activeSerial=worker.activeSerial;
    out->activeType=worker.activeType;
    out->activeSegment=worker.segment;
    out->gxmOperation=aurora_vita_gxm_kernel_operation();
}
void WicInvalidateVideoTextures() { GXInvalidateTexAll(); }
void WicSetPresentationBlack(bool black) {
    if(black)OnNativeStack([] {
        // GXCopyDisp has bound the output surface. Clear only that surface;
        // leave the original EFB and pending post-copy clear ordering intact.
        aurora::vita::renderer().clear_current({0,0,0,1},1.f,true,true,false);
    });
}
bool aurora_begin_frame() { return OnNativeStack([] {
    // Frame 1 has no prior presentation boundary; initialization precedes all
    // GX submissions. Later capture starts happen at the joined end boundary.
    if(!aurora::vita::frame_index()) {
        const auto& diag=RuntimeConfigFile::Get();
        // Begin hashing encoded GX texture sources during the diagnostic boot:
        // most Mario BRRES textures are resident long before frame 300.
        aurora_vita_view_draw_set_texture_probe(diag.diagnosticsEnabled.value_or(true)&&
            diag.diagnosticsCaptureFrame.value_or(0)!=0);
        PrepareDrawCapture(0);
    }
    return aurora::vita::begin_frame();
}); }
#if defined(WIC_PROFILE_GUEST)
void wic_guest_profile_frame();
#endif
void aurora_end_frame() {
    OnNativeStack([] {
        static unsigned nativePresents=0;
        if(++nativePresents<=4) {
            unsigned stackMarker=0;
            std::fprintf(stderr,"[gx-native] present=%u stack=%p\n",nativePresents,static_cast<void*>(&stackMarker));
        }
        aurora::vita::end_frame();
        const uint64_t frame=aurora::vita::frame_index();
        sWatchCompletedFrame.store(frame,std::memory_order_release);
        FinishDrawCapture(frame);
        PrepareDrawCapture(frame);
#if WIC_PRECOMPILED_ASSETS
        NativeAssetSceneTick(frame);
#endif
        static const bool diagnostics=RuntimeConfigFile::Get().diagnosticsEnabled.value_or(true);
        if(!diagnostics) {
            // No completed telemetry, trace or readback is consumed here. The
            // low-frequency cadence report still counts completed presents.
            WicLogFrameDiagnostics(frame,0,false);
            return;
        }
        static uint64_t drawnFrames=0;
        extern void DvdLogIsoStats();
        const auto health=aurora::vita::completed_performance_snapshot();
        const bool completed=health.completedFrame && health.frameIndex==frame;
        const auto& counters=health.frontend.counters;
        const uint64_t draws=completed?counters.draws:0;
        if(draws)++drawnFrames;
        if(frame%300==0 || (draws&&drawnFrames<=3)) {
            std::fprintf(stderr,"[frame-health] frame=%llu completed=%u snapshot_frame=%llu draws=%llu vertices=%llu fallback_textures=%llu unsupported=%llu scenes=%u efb_copies=%u\n",
                static_cast<unsigned long long>(frame),unsigned(completed),
                static_cast<unsigned long long>(health.frameIndex),
                static_cast<unsigned long long>(draws),static_cast<unsigned long long>(completed?counters.vertices:0),
                static_cast<unsigned long long>(completed?counters.fallbackTextures:0),
                static_cast<unsigned long long>(completed?counters.unsupportedFeatures:0),
                completed?health.nativeSceneCount:0,completed?health.nativeEfbCopies:0);
            DvdLogIsoStats();WicLogAudioHealth();
            aurora::vita::frame_trace().write_report("ux0:data/wiicompiled-vita/aurora/diagnostics/wic-draw-trace.log",128);
        }
        static const auto captureFrame=RuntimeConfigFile::Get().diagnosticsCaptureFrame;
        // An explicit completed frame is independent of drawn-frame count:
        // loading/black frames otherwise push the only race capture far away.
        // Zero explicitly disables captures; the diagnostics switch gates all.
        const bool capture=captureFrame ? (*captureFrame!=0 && frame==*captureFrame) :
            (draws && (drawnFrames==30 || drawnFrames==180 || drawnFrames==600));
        if(capture)CaptureFrame(frame);
        WicLogFrameDiagnostics(frame,draws,capture);
#if defined(WIC_PROFILE_GUEST)
        wic_guest_profile_frame();
#endif
    });
}
void aurora_shutdown() { OnNativeStack([] { aurora::vita::shutdown(); }); }
const AuroraEvent* aurora_update() { static AuroraEvent none{};return &none; }
void aurora_wait_for_frame_worker() { OnNativeStack([] { aurora::vita::wait_for_render_idle(); }); }
bool aurora_wait_for_frame_worker_for(uint32_t) { aurora_wait_for_frame_worker();return true; }
void aurora_set_frame_worker_wait_callback(AuroraFrameWorkerWaitCallback) {}
void aurora_set_present_schedule(uint64_t,uint64_t) {}
void aurora_report_producer_paced(bool) {}
bool aurora_flush_efb_copies_to_ram() { return OnNativeStack([] { aurora::vita::wait_for_render_idle();return aurora::vita::draw_sink().materialize_copy_tex(nullptr,writeNotify); }); }
bool aurora_flush_efb_copy_to_ram(void* dest) { return OnNativeStack([dest] { aurora::vita::wait_for_render_idle();return aurora::vita::draw_sink().materialize_copy_tex(dest,writeNotify); }); }
// Native cache validation uses vita_memory_revision, updated by the HLE DMA/DC
// boundaries. Preserve the host write callback for EFB RAM materialization.
void aurora_set_guest_write_hooks(AuroraGuestWriteGenerationCallback,AuroraGuestWriteNotifyCallback notify) { writeNotify=notify; }
void aurora_set_log_level(AuroraLogLevel) {}
void aurora_set_pause_on_focus_lost(bool) {}
void aurora_set_background_input(bool) {}
void aurora_set_display_mode(AuroraDisplayMode) {}
AuroraDisplayMode aurora_get_display_mode() { return AURORA_DISPLAY_MODE_EXCLUSIVE; }
void aurora_push_debug_group(const char*) {}
void aurora_pop_debug_group() {}
}
