#include "avc_video.hpp"
#include <psp2/videodec.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/sysmem.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace wic::avc {
namespace {
constexpr unsigned Pitch=704, Rows=768;
constexpr size_t MemAlignment=1024*1024;
struct Decoder {
    std::mutex mutex;
    SceAvcdecCtrl control{};
    SceUID reference=-1, raster=-1;
    uint8_t* pixels=nullptr;
    bool moduleOwned=false, library=false, created=false;
    // Track the last submitted stream even after a successful picture: the
    // Vita decoder may keep reference/delayed output for the previous size.
    unsigned activeWidth=0, activeHeight=0;
    std::vector<uint8_t> elementary;
    ~Decoder() { Reset(); }
    void Reset() noexcept {
        if(created) sceAvcdecDeleteDecoder(&control);
        created=false; control={};
        if(reference>=0) sceKernelFreeMemBlock(reference);
        if(raster>=0) sceKernelFreeMemBlock(raster);
        reference=raster=-1; pixels=nullptr;
        if(library) sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
        library=false;
        if(moduleOwned) sceSysmoduleUnloadModule(SCE_SYSMODULE_AVCDEC);
        moduleOwned=false;
        activeWidth=activeHeight=0;
    }
    bool Fail(const char* operation,int result,std::string& error) {
        char text[128]; std::snprintf(text,sizeof(text),"%s returned 0x%08x",operation,unsigned(result));
        error=text; Reset(); return false;
    }
    bool Init(std::string& error) {
        if(created) return true;
        int rc;
        if(sceSysmoduleIsLoaded(SCE_SYSMODULE_AVCDEC)!=0) {
            rc=sceSysmoduleLoadModule(SCE_SYSMODULE_AVCDEC);
            if(rc<0) return Fail("AVCDEC module",rc,error);
            moduleOwned=true;
        }
        SceVideodecQueryInitInfoHwAvcdec init{sizeof(init),Pitch,Rows,1,1};
        rc=sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC,&init);
        if(rc<0) return Fail("sceVideodecInitLibrary",rc,error);
        library=true;
        SceAvcdecQueryDecoderInfo query{Pitch,Rows,1}; SceAvcdecDecoderInfo info{};
        rc=sceAvcdecQueryDecoderMemSize(SCE_VIDEODEC_TYPE_HW_AVCDEC,&query,&info);
        if(rc<0 || !info.frameMemSize || info.frameMemSize>32*MemAlignment)
            return Fail("sceAvcdecQueryDecoderMemSize",rc,error);
        const auto size=(info.frameMemSize+MemAlignment-1)&~(MemAlignment-1);
        reference=sceKernelAllocMemBlock("wic_avc_reference",SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,size,nullptr);
        if(reference<0) return Fail("AVC reference memory",reference,error);
        control.frameBuf.size=size;
        rc=sceKernelGetMemBlockBase(reference,&control.frameBuf.pBuf);
        if(rc<0) return Fail("AVC reference pointer",rc,error);
        raster=sceKernelAllocMemBlock("wic_avc_nv12",SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,MemAlignment,nullptr);
        if(raster<0) return Fail("AVC output memory",raster,error);
        void* base=nullptr; rc=sceKernelGetMemBlockBase(raster,&base);
        if(rc<0) return Fail("AVC output pointer",rc,error);
        pixels=static_cast<uint8_t*>(base);
        rc=sceAvcdecCreateDecoder(SCE_VIDEODEC_TYPE_HW_AVCDEC,&control,&query);
        if(rc<0) return Fail("sceAvcdecCreateDecoder",rc,error);
        created=true;
        std::fprintf(stderr,"[avc-hardware] initialized max=%ux%u refs=1 reference_bytes=%u raster_bytes=%u format=nv12\n",
                     Pitch,Rows,control.frameBuf.size,unsigned(MemAlignment));
        return true;
    }
};
}

DecodeResult DecodeHardware(std::span<const uint8_t> data,wic::thp::Image& image,std::string& error) {
    Component component;
    if(!Parse(data,component,error)) return DecodeResult::Error;
    // One hardware stream, serialized native-stack calls. Every source picture
    // is an IDR with its own parameter sets, so menu loops/seeks and alternating
    // movie textures do not depend on another movie's reference pictures.
    static Decoder decoder;
    std::lock_guard lock(decoder.mutex);
    // A successful output does not prove the decoder has drained its old
    // sequence. Restart on every geometry change, including after Picture,
    // or a delayed 512x192 frame can be returned for the next 512x512 movie.
    if(decoder.activeWidth && (decoder.activeWidth!=component.width || decoder.activeHeight!=component.height)) {
        static unsigned switches=0;
        ++switches;
        if(switches<=8 || (switches&(switches-1))==0)
            std::fprintf(stderr,"[avc-hardware] geometry-switch n=%u old=%ux%u new=%ux%u reset=1\n",
                         switches,decoder.activeWidth,decoder.activeHeight,component.width,component.height);
        decoder.Reset();
    }
    if(!decoder.Init(error)) return DecodeResult::Error;
    decoder.activeWidth=component.width; decoder.activeHeight=component.height;
    // The player submits one independent picture, not a continuous byte
    // stream. Close its sequence explicitly so a parser need not wait for the
    // next movie's AUD to recognize the end of this access unit.
    constexpr uint8_t endSequence[]={0,0,0,1,10,0x80};
    const auto esBytes=component.elementary.size()+sizeof(endSequence);
    decoder.elementary.resize(esBytes+64);
    std::memcpy(decoder.elementary.data(),component.elementary.data(),component.elementary.size());
    std::memcpy(decoder.elementary.data()+component.elementary.size(),endSequence,sizeof(endSequence));
    std::memset(decoder.elementary.data()+esBytes,0,64);
    SceAvcdecAu au{}; au.pts={UINT32_MAX,UINT32_MAX}; au.dts=au.pts;
    au.es={decoder.elementary.data(),uint32_t(esBytes)};
    SceAvcdecPicture picture{}; picture.size=sizeof(picture);
    picture.frame.pixelType=SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER;
    picture.frame.framePitch=Pitch; picture.frame.frameWidth=component.width; picture.frame.frameHeight=component.height;
    picture.frame.pPicture[0]=decoder.pixels;
    const auto yBytes=size_t(Pitch)*component.height;
    picture.frame.pPicture[1]=decoder.pixels+yBytes;
    auto* pointer=&picture; SceAvcdecArrayPicture array{0,1,&pointer};
    const auto rc=sceAvcdecDecode(&decoder.control,&au,&array);
    if(rc<0) { decoder.Fail("sceAvcdecDecode",rc,error); return DecodeResult::Error; }
    if(array.numOfOutput==0) {
        static unsigned pending=0;
        if(++pending<=8 || pending%60==0)
            std::fprintf(stderr,"[avc-hardware] accepted-without-picture count=%u size=%ux%u es_bytes=%u sequence_closed=1\n",
                         pending,component.width,component.height,au.es.size);
        error.clear(); return DecodeResult::Pending;
    }
    const auto& frame=picture.frame;
    if(array.numOfOutput!=1 || frame.pixelType!=SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER || frame.framePitch!=Pitch ||
       frame.pPicture[0]!=decoder.pixels || frame.pPicture[1]!=decoder.pixels+yBytes ||
       frame.horizontalSize!=component.width || frame.verticalSize!=component.height ||
       frame.frameCropLeftOffset || frame.frameCropRightOffset || frame.frameCropTopOffset || frame.frameCropBottomOffset) {
        std::fprintf(stderr,"[avc-hardware] rejected output count=%u size=%ux%u expected=%ux%u pitch=%u format=0x%x crop=%u,%u,%u,%u\n",
            array.numOfOutput,frame.horizontalSize,frame.verticalSize,component.width,component.height,
            frame.framePitch,frame.pixelType,frame.frameCropLeftOffset,frame.frameCropRightOffset,
            frame.frameCropTopOffset,frame.frameCropBottomOffset);
        error="AVC output count/geometry/crop mismatch"; decoder.Reset(); return DecodeResult::Error;
    }
    return CopyNv12({decoder.pixels,yBytes},
                    {decoder.pixels+yBytes,yBytes/2},
                    Pitch,component.width,component.height,image,error) ? DecodeResult::Picture : DecodeResult::Error;
}
}
