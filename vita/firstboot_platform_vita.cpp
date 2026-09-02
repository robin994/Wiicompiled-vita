#include <dolphin/pad.h>
#include <dolphin/vi.h>

#include <psp2/ctrl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

int8_t VitaAxisToPad(uint8_t value) {
    const int centered = static_cast<int>(value) - 128;
    return static_cast<int8_t>(std::clamp(centered, -128, 127));
}

} // namespace

extern "C" BOOL PADInit() {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    return TRUE;
}

extern "C" u32 PADRead(PADStatus* status) {
    if (status == nullptr) {
        return 0;
    }

    std::memset(status, 0, sizeof(PADStatus) * PAD_CHANMAX);
    for (int i = 1; i < PAD_CHANMAX; ++i) {
        status[i].err = PAD_ERR_NO_CONTROLLER;
    }

    SceCtrlData pad{};
    if (sceCtrlPeekBufferPositive(0, &pad, 1) <= 0) {
        status[0].err = PAD_ERR_NOT_READY;
        return 0;
    }

    status[0].err = PAD_ERR_NONE;
    status[0].stickX = VitaAxisToPad(pad.lx);
    status[0].stickY = static_cast<int8_t>(-VitaAxisToPad(pad.ly));
    status[0].substickX = VitaAxisToPad(pad.rx);
    status[0].substickY = static_cast<int8_t>(-VitaAxisToPad(pad.ry));

    const uint32_t buttons = pad.buttons;
    if (buttons & SCE_CTRL_UP) status[0].button |= PAD_BUTTON_UP;
    if (buttons & SCE_CTRL_DOWN) status[0].button |= PAD_BUTTON_DOWN;
    if (buttons & SCE_CTRL_LEFT) status[0].button |= PAD_BUTTON_LEFT;
    if (buttons & SCE_CTRL_RIGHT) status[0].button |= PAD_BUTTON_RIGHT;
    if (buttons & SCE_CTRL_CROSS) status[0].button |= PAD_BUTTON_A;
    if (buttons & SCE_CTRL_CIRCLE) status[0].button |= PAD_BUTTON_B;
    if (buttons & SCE_CTRL_SQUARE) status[0].button |= PAD_BUTTON_X;
    if (buttons & SCE_CTRL_TRIANGLE) status[0].button |= PAD_BUTTON_Y;
    if (buttons & SCE_CTRL_START) status[0].button |= PAD_BUTTON_START;
    if (buttons & SCE_CTRL_LTRIGGER) {
        status[0].button |= PAD_TRIGGER_L;
        status[0].triggerL = 0xff;
    }
    if (buttons & SCE_CTRL_RTRIGGER) {
        status[0].button |= PAD_TRIGGER_R;
        status[0].triggerR = 0xff;
    }

    return 0;
}

extern "C" BOOL PADReset(u32) { return TRUE; }
extern "C" BOOL PADRecalibrate(u32) { return TRUE; }
extern "C" void PADControlMotor(u32, u32) {}

// The runtime VI HLE owns timing and pending/committed render-mode state.
// The Vita backend has a fixed 960x544 surface, so there is no host window to
// reconfigure here.
extern "C" void VIConfigure(const GXRenderModeObj*) {}

// Aurora's desktop GX frontend derives SU texture registers here. The Vita GX
// bridge consumes guest texture state directly, so the host-side helper is not
// required during first-boot bring-up.
extern "C" void __GXSetSUTexRegs() {}
