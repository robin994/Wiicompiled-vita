#include "host_context.h"
#include "wii_remote_input.h"
#include "hle/controller_status_contract.h"
#include <dolphin/pad.h>
#include <psp2/ctrl.h>
#include <cstdlib>
#include <cstdio>
#include <initializer_list>

namespace {
HostContext::Handle scheduler=nullptr,guest=nullptr;
SceCtrlData sample{};
int readResult=1;
unsigned sdkReads=0;
void Check(bool value) { if(!value)std::abort(); }
void CheckRead(uint16_t expected) {
    PADStatus pads[4]{};
    Check(PADRead(pads)==0);
    Check(pads[0].err==0 && pads[0].button==expected);
    for(unsigned i=1;i<4;++i)Check(pads[i].err==-1 && pads[i].button==0);
    WiiRemoteInput::HideRemotesFromPad(pads,4);
    Check(pads[0].err==0);
    const auto wire=PadStatusContract::Encode({pads[0].button,pads[0].stickX,pads[0].stickY,
        pads[0].substickX,pads[0].substickY,pads[0].triggerLeft,pads[0].triggerRight,
        pads[0].analogA,pads[0].analogB,pads[0].err});
    Check(wire[0]==(expected>>8) && wire[1]==(expected&255) && wire[10]==0);
}
void Exercise() {
    Check(PADInit());
    CheckRead(0);
    struct Binding { uint32_t vita; uint16_t wire; };
    constexpr Binding cases[]{
        {SCE_CTRL_CROSS,0x100},{SCE_CTRL_CIRCLE,0x200},{SCE_CTRL_SQUARE,0x400},
        {SCE_CTRL_TRIANGLE,0x800},{SCE_CTRL_START,0x1000},{SCE_CTRL_SELECT,0x10},
        {SCE_CTRL_UP,8},{SCE_CTRL_DOWN,4},{SCE_CTRL_LEFT,1},{SCE_CTRL_RIGHT,2},
        {SCE_CTRL_LTRIGGER,0x40},{SCE_CTRL_RTRIGGER,0x20}};
    for(const auto& binding:cases) {sample.buttons=binding.vita;CheckRead(binding.wire);}
    sample.buttons=SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER;
    sample.lx=0;sample.ly=0;sample.rx=255;sample.ry=255;
    PADStatus pads[4]{};PADRead(pads);
    Check(pads[0].button==0x360 && pads[0].stickX==-127 && pads[0].stickY==127);
    Check(pads[0].substickX==127 && pads[0].substickY==-127);
    Check(pads[0].triggerLeft==255 && pads[0].triggerRight==255);
    Check(pads[0].analogA==255 && pads[0].analogB==255);
    // A failed/empty read must not retain a previously pressed button.
    for(int failure:{0,-1}) {
        readResult=failure;PADRead(pads);
        for(const auto& pad:pads)Check(pad.err==-1 && pad.button==0 && pad.stickX==0);
    }
    readResult=1;sample={};CheckRead(0);
    for(unsigned port=0;port<4;++port) {
        Check(!WiiRemoteInput::IsRemoteChannel(port));
        Check(WiiRemoteInput::EffectiveKind(port)==WiiRemoteInput::Kind::NotWii);
        WiiRemoteInput::KpadSample remote{};remote.hasClassic=true;
        Check(!WiiRemoteInput::ReadKpadSample(port,remote) && !remote.hasClassic);
    }
}
void Guest(void*) { Exercise();Check(HostContext::IsCurrent(guest));HostContext::Switch(scheduler);std::abort(); }
}
extern "C" int sceCtrlPeekBufferPositive(int port,SceCtrlData* output,int count) {
    Check(port==0 && count==1 && !HostContext::IsCurrent(guest));
    ++sdkReads;*output=sample;return readResult;
}
int main() {
    Check(HostContext::InitializeScheduler(&scheduler));
    Exercise();
    guest=HostContext::Create(64*1024,Guest,nullptr);Check(guest);
    HostContext::Switch(guest);
    Check(sdkReads==34);
    HostContext::Destroy(guest);HostContext::ShutdownScheduler(scheduler);
    std::puts("PASS: Vita input reaches native SDK stack, one connected GameCube pad, buttons/axes/triggers, guest BE status and failed reads.");
}
