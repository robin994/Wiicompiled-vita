#include "avc_video.hpp"
#include <psp2/videodec.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/sysmem.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
void Check(bool value) { if(!value)std::abort(); }
enum Failure { None,Module,Library,Alloc,Decode,Geometry,Pending } failure=None;
bool loaded=false,library=false,created=false;
unsigned deletes=0,terminations=0,unloads=0,decodeCalls=0;
unsigned lastStreamWidth=0,lastStreamHeight=0;
std::map<int,std::vector<uint8_t>> blocks;
std::vector<uint8_t> Component(unsigned w,unsigned h) {
    const std::vector<uint8_t> es={0,0,0,1,9,16,0,0,1,7,66,0,32,0,0,1,8,128,0,0,1,5,128};
    std::vector<uint8_t> b={'W','A','V','C'};
    for(auto n:{1u,w,h,unsigned(es.size()),1u}) for(unsigned i=0;i<4;++i)b.push_back(uint8_t(n>>(24-i*8)));
    b.insert(b.end(),es.begin(),es.end());return b;
}
void Clean() { Check(!loaded && !library && !created && blocks.empty()); }
}
int sceSysmoduleIsLoaded(uint16_t) { return loaded?0:-1; }
int sceSysmoduleLoadModule(uint16_t) { if(failure==Module)return -3;Check(!loaded);loaded=true;return 0; }
int sceSysmoduleUnloadModule(uint16_t) { Check(loaded && !library);loaded=false;++unloads;return 0; }
int sceVideodecInitLibrary(SceVideodecType,const SceVideodecQueryInitInfoHwAvcdec* info) {
    Check(loaded && !library && info->horizontal==704 && info->vertical==768 && info->numOfRefFrames==1 && info->numOfStreams==1);
    if(failure==Library)return -4;library=true;return 0;
}
int sceVideodecTermLibrary(SceVideodecType) { Check(library && !created && blocks.empty());library=false;++terminations;return 0; }
int sceAvcdecQueryDecoderMemSize(SceVideodecType,const SceAvcdecQueryDecoderInfo*,SceAvcdecDecoderInfo* info) {
    Check(library);info->frameMemSize=1048576+1;return 0;
}
SceUID sceKernelAllocMemBlock(const char*,int type,unsigned bytes,const void*) {
    Check(library && type==SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW && bytes%1048576==0);
    if(failure==Alloc)return -5;
    static int next=0;blocks[++next].resize(bytes);return next;
}
int sceKernelGetMemBlockBase(SceUID id,void** out) { Check(blocks.count(id));*out=blocks[id].data();return 0; }
int sceKernelFreeMemBlock(SceUID id) { Check(!created && blocks.count(id));blocks.erase(id);return 0; }
int sceAvcdecCreateDecoder(SceVideodecType,SceAvcdecCtrl* ctrl,const SceAvcdecQueryDecoderInfo*) {
    Check(library && !created && blocks.size()==2 && ctrl->frameBuf.size==2097152);
    ctrl->handle=11;created=true;lastStreamWidth=lastStreamHeight=0;return 0;
}
int sceAvcdecDeleteDecoder(SceAvcdecCtrl* ctrl) {
    Check(created && ctrl->handle==11);created=false;lastStreamWidth=lastStreamHeight=0;++deletes;return 0;
}
int sceAvcdecDecode(const SceAvcdecCtrl*,const SceAvcdecAu* au,SceAvcdecArrayPicture* array) {
    Check(created && au->pts.lower==UINT32_MAX && au->pts.upper==UINT32_MAX && array->numOfElm==1);++decodeCalls;
    if(failure==Decode)return -6;
    const auto* es=static_cast<const uint8_t*>(au->es.pBuf);
    constexpr uint8_t eos[]={0,0,0,1,10,0x80};
    Check(au->es.size>=sizeof(eos) && std::memcmp(es+au->es.size-sizeof(eos),eos,sizeof(eos))==0);
    for(unsigned i=0;i<64;++i) Check(es[au->es.size+i]==0);
    auto& p=*array->pPicture[0];auto& f=p.frame;
    const unsigned delayedWidth=lastStreamWidth,delayedHeight=lastStreamHeight;
    lastStreamWidth=f.frameWidth;lastStreamHeight=f.frameHeight;
    if(failure==Pending) { array->numOfOutput=0; return 0; }
    Check(p.size==sizeof(p) && f.pixelType==SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER && f.framePitch==704);
    auto* y=static_cast<uint8_t*>(f.pPicture[0]);auto* uv=static_cast<uint8_t*>(f.pPicture[1]);
    Check(uv==y+704*f.frameHeight);
    for(unsigned row=0;row<f.frameHeight;++row)for(unsigned col=0;col<f.frameWidth;++col)y[row*704+col]=uint8_t(row+col);
    for(unsigned row=0;row<f.frameHeight/2;++row)for(unsigned col=0;col<f.frameWidth/2;++col) {
        uv[row*704+2*col]=11;uv[row*704+2*col+1]=23;
    }
    // Hardware may return the previously decoded picture when an existing
    // decoder is fed a new geometry. Only a recreated decoder clears it.
    const bool stale=delayedWidth && (delayedWidth!=f.frameWidth || delayedHeight!=f.frameHeight);
    f.horizontalSize=failure==Geometry?f.frameWidth+16:(stale?delayedWidth:f.frameWidth);
    f.verticalSize=stale?delayedHeight:f.frameHeight;
    array->numOfOutput=1;return 0;
}
int main() {
    using wic::avc::DecodeResult;
    auto component=Component(672,752);wic::thp::Image image;std::string error;
    for(auto fail:{Module,Library,Alloc}) {
        failure=fail;Check(wic::avc::DecodeHardware(component,image,error)==DecodeResult::Error && !error.empty());Clean();
    }
    failure=None;Check(wic::avc::DecodeHardware(component,image,error)==DecodeResult::Picture);
    Check(image.width==672 && image.height==752 && image.planes[0][671]==uint8_t(671) && image.planes[1][0]==11 && image.planes[2][0]==23);
    // Regression: switch directly after a successful Picture, with no Pending
    // in between. Previously this skipped Reset and returned a stale picture.
    auto afterPicture=Component(512,192);
    Check(wic::avc::DecodeHardware(afterPicture,image,error)==DecodeResult::Picture &&
          image.width==512 && image.height==192 && deletes==1);
    auto singlePlayer=Component(512,512);
    Check(wic::avc::DecodeHardware(singlePlayer,image,error)==DecodeResult::Picture &&
          image.width==512 && image.height==512 && deletes==2);
    Check(wic::avc::DecodeHardware(component,image,error)==DecodeResult::Picture &&
          image.width==672 && image.height==752 && deletes==3);
    const auto saved=image;
    failure=Pending;error="stale error";
    for(unsigned i=0;i<3;++i) {
        Check(wic::avc::DecodeHardware(component,image,error)==DecodeResult::Pending && error.empty());
        Check(image.width==saved.width && image.height==saved.height && image.planes==saved.planes);
        Check(loaded && library && created && blocks.size()==2 && deletes==3);
    }
    failure=None;Check(wic::avc::DecodeHardware(component,image,error)==DecodeResult::Picture && deletes==3);
    failure=Pending;Check(wic::avc::DecodeHardware(component,image,error)==DecodeResult::Pending);
    // A delayed old picture must not be delivered into a different geometry.
    auto second=Component(256,192);failure=None;
    Check(wic::avc::DecodeHardware(second,image,error)==DecodeResult::Picture && image.width==256 && image.height==192);
    Check(deletes==4 && terminations==5 && unloads==6);
    Check(blocks.size()==2 && decodeCalls==10);
    failure=Decode;Check(wic::avc::DecodeHardware(component,image,error)==DecodeResult::Error);Clean();
    failure=Geometry;Check(wic::avc::DecodeHardware(component,image,error)==DecodeResult::Error);Clean();
    Check(deletes==7 && terminations==8 && unloads==9);
    std::fprintf(stderr,"AVC native API lifecycle/stride/switch/error contracts PASS; hardware simulated\n");
}
