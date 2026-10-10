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
#include "gx/aurora_vita_draw_sink.hpp"
#include <dolphin/gx.h>
#include <type_traits>
#include <cstdio>
#include <array>
#include <atomic>
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
AuroraGuestWriteNotifyCallback writeNotify=nullptr;
uint64_t drawCaptureFrame=0;
bool drawCaptureAttempted=false;

void PrepareDrawCapture(uint64_t completedFrame) {
    const auto& config=RuntimeConfigFile::Get();
    if(drawCaptureAttempted || !config.diagnosticsEnabled.value_or(true) ||
       !config.diagnosticsCaptureFrame || !*config.diagnosticsCaptureFrame ||
       completedFrame+1!=*config.diagnosticsCaptureFrame)return;
    drawCaptureAttempted=true;
    // The prior end_frame joined the GX consumer. An explicit FIFO marker
    // associates every submission in the next frame with this capture; 255
    // means the application has no more specific view classification.
    if(aurora_vita_view_draw_start_ex(16384,1)) {
        drawCaptureFrame=completedFrame+1;
        aurora_vita_view_draw_mark(255,drawCaptureFrame);
    }
    std::fprintf(stderr,"[draw-capture] requested=%llu started=%u capacity=16384 payload_hashes=1\n",
        static_cast<unsigned long long>(completedFrame+1),unsigned(drawCaptureFrame!=0));
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
        "sequence,type,producer_frame,consumer_frame,view,draw,target,vertices,indices,tev_stages,texture_mask,texgen_mask,native,indexed_pn,blend,depth,alpha,alpha_ref0,alpha_ref1,alpha_op,scissor,pipeline_requested,pipeline_active,vertex_shader,fragment_shader,vertex_payload,index_payload,uniform_payload,draw_state,payload_hashes,submit_cpu_us,uniform_bytes\n")>0;
    std::array<AuroraViewDrawRecord,32> batch{};
    size_t offset=0;bool complete=false;
    while(ok && offset<count) {
        const auto n=aurora_vita_view_draw_read(offset,batch.data(),batch.size());
        if(!n) { ok=false;break; }
        for(size_t i=0;ok && i<n;++i) {
            const auto& r=batch[i];
            if(r.type==AURORA_VIEW_DRAW_FRAME_COMPLETE && r.consumer_frame==completedFrame)complete=true;
            ok=std::fprintf(output,
                "%llu,%u,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%u,%llu,%llu\n",
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
                static_cast<unsigned long long>(r.submit_cpu_us),static_cast<unsigned long long>(r.uniform_bytes))>0;
        }
        offset+=n;
    }
    if(output && std::fclose(output)!=0)ok=false;
    std::fprintf(stderr,"[draw-capture] frame=%llu saved=%u complete=%u records=%u lost=%llu path=%s diagnostic=1\n",
        static_cast<unsigned long long>(completedFrame),unsigned(ok),unsigned(complete),unsigned(count),
        static_cast<unsigned long long>(lost),path);
    drawCaptureFrame=0;
}

void CaptureFrame(uint64_t frame) {
    // Bounded captures only. Readback inserts a GPU fence, so these runs
    // are for correctness diagnosis, never performance comparison.
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
    if(!aurora::vita::frame_index())PrepareDrawCapture(0);
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
