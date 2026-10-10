#include "wii_remote_input.h"
#include "input_bindings.h"
#include "host_context.h"
#include <psp2/ctrl.h>
#include <algorithm>
#include <cstdio>

namespace {
uint64_t reads=0, failedReads=0;
uint32_t lastButtons=0;
unsigned transitionReports=0;

void ReadPad(void* argument) {
    auto* statuses=static_cast<PADStatus*>(argument);
    for(unsigned i=0;i<PAD_CHANMAX;++i) {
        statuses[i]={};
        statuses[i].err=PAD_ERR_NO_CONTROLLER;
    }
    SceCtrlData sample{};
    const int result=sceCtrlPeekBufferPositive(0,&sample,1);
    ++reads;
    if(result<=0) {
        ++failedReads;
        if(failedReads<=4 || reads%300==0)
            std::fprintf(stderr,"[input-vita] read=%llu result=0x%08x no_sample=1\n",
                static_cast<unsigned long long>(reads),static_cast<unsigned>(result));
        return;
    }
    auto& pad=statuses[0];
    pad.err=PAD_ERR_NONE;
    struct Mapping { uint32_t vita; uint16_t gamecube; };
    constexpr Mapping mapping[]{
        {SCE_CTRL_CROSS,PAD_BUTTON_A},{SCE_CTRL_CIRCLE,PAD_BUTTON_B},
        {SCE_CTRL_SQUARE,PAD_BUTTON_X},{SCE_CTRL_TRIANGLE,PAD_BUTTON_Y},
        {SCE_CTRL_UP,PAD_BUTTON_UP},{SCE_CTRL_DOWN,PAD_BUTTON_DOWN},
        {SCE_CTRL_LEFT,PAD_BUTTON_LEFT},{SCE_CTRL_RIGHT,PAD_BUTTON_RIGHT},
        {SCE_CTRL_START,PAD_BUTTON_START},{SCE_CTRL_SELECT,PAD_TRIGGER_Z},
        {SCE_CTRL_LTRIGGER,PAD_TRIGGER_L},{SCE_CTRL_RTRIGGER,PAD_TRIGGER_R},
    };
    for(const auto& binding:mapping)
        if(sample.buttons&binding.vita)pad.button|=binding.gamecube;
    pad.stickX=static_cast<s8>(std::clamp(int(sample.lx)-128,-127,127));
    pad.stickY=static_cast<s8>(std::clamp(128-int(sample.ly),-127,127));
    pad.substickX=static_cast<s8>(std::clamp(int(sample.rx)-128,-127,127));
    pad.substickY=static_cast<s8>(std::clamp(128-int(sample.ry),-127,127));
    pad.triggerLeft=(sample.buttons&SCE_CTRL_LTRIGGER)?255:0;
    pad.triggerRight=(sample.buttons&SCE_CTRL_RTRIGGER)?255:0;
    pad.analogA=(pad.button&PAD_BUTTON_A)?255:0;
    pad.analogB=(pad.button&PAD_BUTTON_B)?255:0;
    const bool changed=sample.buttons!=lastButtons;
    if(reads<=3 || reads%300==0 || (changed&&transitionReports<32)) {
        std::fprintf(stderr,"[input-vita] read=%llu profile=gamecube port=0 result=%d vita=0x%08x pad=0x%04x stick=%d,%d errors=%llu\n",
            static_cast<unsigned long long>(reads),result,static_cast<unsigned>(sample.buttons),
            unsigned(pad.button),int(pad.stickX),int(pad.stickY),static_cast<unsigned long long>(failedReads));
        if(changed)++transitionReports;
    }
    lastButtons=sample.buttons;
}
}

namespace WiiRemoteInput {
// Vita is a single GameCube pad. Do not also advertise an incomplete WPAD
// device: the game's connection/format negotiation must agree with its samples.
Kind KindForPort(uint32_t) { return Kind::NotWii; }
Kind EffectiveKind(uint32_t port) { return KindForPort(port); }
bool IsRemoteChannel(uint32_t) { return false; }
void Poll() {}
bool ReadKpadSample(uint32_t,KpadSample& sample) { sample={};return false; }
void HideRemotesFromPad(PADStatus*,uint32_t) {}
}
namespace InputBindings {
bool InputBlocked() noexcept { return false; }
void Apply(PADStatus*) noexcept {}
}
extern "C" {
BOOL PADInit() { return TRUE; }
u32 PADRead(PADStatus* statuses) {
    if(statuses)HostContext::RunOnNativeStack(ReadPad,statuses);
    return 0; // No motor capability; connection is reported by PADStatus.err.
}
BOOL PADReset(u32) { return TRUE; }
BOOL PADRecalibrate(u32) { return TRUE; }
void PADControlMotor(u32,u32) {}
s32 PADGetIndexForPort(u32) { return -1; }
SDL_Gamepad* PADGetSDLGamepadForIndex(u32) { return nullptr; }
PADButtonMapping* PADGetButtonMappings(u32,u32* n) { *n=0;return nullptr; }
PADButtonMapping* PADGetAltButtonMappings(u32,u32* n) { *n=0;return nullptr; }
}
