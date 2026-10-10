#pragma once
#include <cstdint>
// Host test double for the VitaSDK input API. Buttons use the SDK wire values.
enum : uint32_t {
    SCE_CTRL_SELECT=0x000001, SCE_CTRL_START=0x000008,
    SCE_CTRL_UP=0x000010, SCE_CTRL_RIGHT=0x000020,
    SCE_CTRL_DOWN=0x000040, SCE_CTRL_LEFT=0x000080,
    SCE_CTRL_LTRIGGER=0x000100, SCE_CTRL_RTRIGGER=0x000200,
    SCE_CTRL_TRIANGLE=0x001000, SCE_CTRL_CIRCLE=0x002000,
    SCE_CTRL_CROSS=0x004000, SCE_CTRL_SQUARE=0x008000,
};
struct SceCtrlData { uint32_t buttons=0; uint8_t lx=128,ly=128,rx=128,ry=128; };
extern "C" int sceCtrlPeekBufferPositive(int,SceCtrlData*,int);
