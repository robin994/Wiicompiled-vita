#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"

#include <psp2/ctrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

// Mario Kart Wii uses the Revolution SDK 2008 layouts, not the newer
// publicly documented KPAD layout.
constexpr uint32_t kKpadStatusSize = 0x84;
constexpr uint32_t kKpadUnifiedStatusSize = 0x38;
constexpr uint8_t kWpadExtensionClassic = 0x02;
constexpr uint8_t kWpadFormatClassic = 0x06;

constexpr uint32_t kWpadButtonLeft = 0x0001;
constexpr uint32_t kWpadButtonRight = 0x0002;
constexpr uint32_t kWpadButtonDown = 0x0004;
constexpr uint32_t kWpadButtonUp = 0x0008;
constexpr uint32_t kWpadButtonPlus = 0x0010;
constexpr uint32_t kWpadButtonTwo = 0x0100;
constexpr uint32_t kWpadButtonOne = 0x0200;
constexpr uint32_t kWpadButtonB = 0x0400;
constexpr uint32_t kWpadButtonA = 0x0800;
constexpr uint32_t kWpadButtonMinus = 0x1000;

constexpr uint32_t kClassicButtonUp = 0x00000001;
constexpr uint32_t kClassicButtonLeft = 0x00000002;
constexpr uint32_t kClassicButtonZr = 0x00000004;
constexpr uint32_t kClassicButtonX = 0x00000008;
constexpr uint32_t kClassicButtonA = 0x00000010;
constexpr uint32_t kClassicButtonY = 0x00000020;
constexpr uint32_t kClassicButtonB = 0x00000040;
constexpr uint32_t kClassicButtonZl = 0x00000080;
constexpr uint32_t kClassicButtonR = 0x00000200;
constexpr uint32_t kClassicButtonPlus = 0x00000400;
constexpr uint32_t kClassicButtonMinus = 0x00001000;
constexpr uint32_t kClassicButtonL = 0x00002000;
constexpr uint32_t kClassicButtonDown = 0x00004000;
constexpr uint32_t kClassicButtonRight = 0x00008000;

struct VitaKpadSample {
    uint32_t core = 0;
    uint32_t classic = 0;
    float stickX = 0.0f;
    float stickY = 0.0f;
    bool ready = false;
};

std::array<uint32_t, 4> g_previousCore{};
std::array<uint32_t, 4> g_previousClassic{};

float VitaAxis(uint8_t value)
{
    const int centered = static_cast<int>(value) - 128;
    if (std::abs(centered) <= 16) {
        return 0.0f;
    }
    const float scale = centered < 0 ? 128.0f : 127.0f;
    return std::clamp(static_cast<float>(centered) / scale, -1.0f, 1.0f);
}

VitaKpadSample ReadVitaKpad()
{
    static bool samplingConfigured = false;
    if (!samplingConfigured) {
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
        samplingConfigured = true;
    }

    SceCtrlData pad{};
    VitaKpadSample sample{};
    if (sceCtrlPeekBufferPositive(0, &pad, 1) <= 0) {
        return sample;
    }
    sample.ready = true;
    sample.stickX = VitaAxis(pad.lx);
    sample.stickY = -VitaAxis(pad.ly);

    const uint32_t buttons = pad.buttons;
    if (buttons & SCE_CTRL_UP) {
        sample.core |= kWpadButtonUp;
        sample.classic |= kClassicButtonUp;
    }
    if (buttons & SCE_CTRL_DOWN) {
        sample.core |= kWpadButtonDown;
        sample.classic |= kClassicButtonDown;
    }
    if (buttons & SCE_CTRL_LEFT) {
        sample.core |= kWpadButtonLeft;
        sample.classic |= kClassicButtonLeft;
    }
    if (buttons & SCE_CTRL_RIGHT) {
        sample.core |= kWpadButtonRight;
        sample.classic |= kClassicButtonRight;
    }
    if (buttons & SCE_CTRL_CROSS) {
        sample.core |= kWpadButtonA;
        sample.classic |= kClassicButtonA;
    }
    if (buttons & SCE_CTRL_CIRCLE) {
        sample.core |= kWpadButtonB;
        sample.classic |= kClassicButtonB;
    }
    if (buttons & SCE_CTRL_SQUARE) {
        sample.core |= kWpadButtonOne;
        sample.classic |= kClassicButtonX;
    }
    if (buttons & SCE_CTRL_TRIANGLE) {
        sample.core |= kWpadButtonTwo;
        sample.classic |= kClassicButtonY;
    }
    if (buttons & SCE_CTRL_START) {
        sample.core |= kWpadButtonPlus;
        sample.classic |= kClassicButtonPlus;
    }
    if (buttons & SCE_CTRL_SELECT) {
        sample.core |= kWpadButtonMinus;
        sample.classic |= kClassicButtonMinus;
    }
    if (buttons & SCE_CTRL_LTRIGGER) {
        sample.classic |= kClassicButtonL | kClassicButtonZl;
    }
    if (buttons & SCE_CTRL_RTRIGGER) {
        sample.classic |= kClassicButtonR | kClassicButtonZr;
    }
    return sample;
}

void WriteKpadStatus(uint32_t address, const VitaKpadSample& sample,
                     uint32_t coreTrigger, uint32_t coreRelease,
                     uint32_t classicTrigger, uint32_t classicRelease)
{
    std::memset(Memory::GetPointer(address, kKpadStatusSize), 0, kKpadStatusSize);
    Memory::Write32(address + 0x00, sample.core);
    Memory::Write32(address + 0x04, coreTrigger);
    Memory::Write32(address + 0x08, coreRelease);
    Memory::Write8(address + 0x5C, kWpadExtensionClassic);
    Memory::Write8(address + 0x5F, kWpadFormatClassic);
    Memory::Write32(address + 0x60, sample.classic);
    Memory::Write32(address + 0x64, classicTrigger);
    Memory::Write32(address + 0x68, classicRelease);
    Memory::WriteFloat32(address + 0x6C, sample.stickX);
    Memory::WriteFloat32(address + 0x70, sample.stickY);
}

void WriteUnifiedStatus(uint32_t address, const VitaKpadSample& sample)
{
    std::memset(Memory::GetPointer(address, kKpadUnifiedStatusSize), 0,
                kKpadUnifiedStatusSize);
    Memory::Write16(address + 0x00, static_cast<uint16_t>(sample.core));
    Memory::Write8(address + 0x28, kWpadExtensionClassic);
    Memory::Write16(address + 0x2A, static_cast<uint16_t>(sample.classic));
    const int16_t stickX = static_cast<int16_t>(std::lround(sample.stickX * 511.0f));
    const int16_t stickY = static_cast<int16_t>(std::lround(sample.stickY * 511.0f));
    Memory::Write16(address + 0x2C, static_cast<uint16_t>(stickX));
    Memory::Write16(address + 0x2E, static_cast<uint16_t>(stickY));
    Memory::Write8(address + 0x36, kWpadFormatClassic);
}

void LogInputChange(uint32_t core, uint32_t classic, float stickX, float stickY)
{
    static uint32_t loggedCore = 0;
    static uint32_t loggedClassic = 0;
    if (core == loggedCore && classic == loggedClassic) {
        return;
    }
    loggedCore = core;
    loggedClassic = classic;
    RT_LOGF(RT_TAG_HLE, "input Vita state core=%04x classic=%04x stick=(%.2f,%.2f)\n",
            core, classic, static_cast<double>(stickX), static_cast<double>(stickY));
}

} // namespace

extern "C" bool KPAD_IsVitaChannelConnected(uint32_t chan)
{
    return chan == 0;
}

extern "C" int32_t KPAD__Read_HLE(uint32_t chan, uint32_t statusPtr, uint32_t count)
{
    static bool logged = false;
    if (!logged) {
        RT_LOGF(RT_TAG_HLE, "input KPADRead bridge active: chan=%u count=%u\n", chan, count);
        logged = true;
    }
    if (chan >= g_previousCore.size() || chan != 0 || statusPtr == 0 || count == 0) {
        return 0;
    }

    const VitaKpadSample sample = ReadVitaKpad();
    if (!sample.ready) {
        return 0;
    }
    const uint32_t coreTrigger = sample.core & ~g_previousCore[chan];
    const uint32_t coreRelease = g_previousCore[chan] & ~sample.core;
    const uint32_t classicTrigger = sample.classic & ~g_previousClassic[chan];
    const uint32_t classicRelease = g_previousClassic[chan] & ~sample.classic;
    g_previousCore[chan] = sample.core;
    g_previousClassic[chan] = sample.classic;
    LogInputChange(sample.core, sample.classic, sample.stickX, sample.stickY);

    try {
        WriteKpadStatus(statusPtr, sample, coreTrigger, coreRelease,
                        classicTrigger, classicRelease);
    } catch (const Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_HLE, "input KPADRead", e);
        return 0;
    }
    return 1;
}
PPC_NATIVE_OVERRIDE(80197380, KPAD__Read_HLE, int32_t, (uint32_t chan, uint32_t statusPtr, uint32_t count),
         (chan, statusPtr, count));

extern "C" int32_t KPAD__GetUnifiedWpadStatus_HLE(uint32_t chan, uint32_t statusPtr, uint32_t count)
{
    static bool logged = false;
    if (!logged) {
        RT_LOGF(RT_TAG_HLE, "input KPADGetUnifiedWpadStatus bridge active: chan=%u count=%u\n",
                chan, count);
        logged = true;
    }
    if (chan != 0 || statusPtr == 0 || count == 0) {
        return 0;
    }
    const VitaKpadSample sample = ReadVitaKpad();
    if (!sample.ready) {
        return 0;
    }
    LogInputChange(sample.core, sample.classic, sample.stickX, sample.stickY);
    try {
        WriteUnifiedStatus(statusPtr, sample);
    } catch (const Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_HLE, "input KPADGetUnifiedWpadStatus", e);
    }
    return 0;
}
PPC_NATIVE_OVERRIDE(8019812C, KPAD__GetUnifiedWpadStatus_HLE, int32_t,
         (uint32_t chan, uint32_t statusPtr, uint32_t count), (chan, statusPtr, count));
