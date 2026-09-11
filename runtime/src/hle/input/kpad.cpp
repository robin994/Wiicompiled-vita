#include "hle_stubs.h"
#include "memory.h"
#include "runtime_log.h"
#if !defined(MKW_TARGET_VITA)
#include "wii_remote_input.h"
#endif

#if defined(MKW_TARGET_VITA)
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

#ifndef MKW_VITA_TIMELINE_PROFILE
#define MKW_VITA_TIMELINE_PROFILE 0
#endif
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(MKW_TARGET_VITA)
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
// Preserve short Vita button presses until KPADRead has delivered them at least once.
// This matters while the port is temporarily running below real-time: PeekBufferPositive
// otherwise loses a complete press/release that occurs between two guest polls.
uint32_t g_previousPhysicalCore = 0;
uint32_t g_previousPhysicalClassic = 0;
uint32_t g_latchedCorePress = 0;
uint32_t g_latchedClassicPress = 0;

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

    const uint32_t physicalCore = sample.core;
    const uint32_t physicalClassic = sample.classic;
    g_latchedCorePress |= physicalCore & ~g_previousPhysicalCore;
    g_latchedClassicPress |= physicalClassic & ~g_previousPhysicalClassic;
    g_previousPhysicalCore = physicalCore;
    g_previousPhysicalClassic = physicalClassic;
    sample.core |= g_latchedCorePress;
    sample.classic |= g_latchedClassicPress;
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
#if MKW_VITA_TIMELINE_PROFILE
    RT_LOGF(RT_TAG_HLE, "input_timeline t_us=%llu core=%04x classic=%04x stick=(%.2f,%.2f)\n",
            static_cast<unsigned long long>(sceKernelGetProcessTimeWide()), core, classic,
            static_cast<double>(stickX), static_cast<double>(stickY));
#else
    RT_LOGF(RT_TAG_HLE, "input Vita state core=%04x classic=%04x stick=(%.2f,%.2f)\n",
            core, classic, static_cast<double>(stickX), static_cast<double>(stickY));
#endif
}

} // namespace

extern "C" bool KPAD_IsVitaChannelConnected(uint32_t chan)
{
    return chan == 0;
}
#else

// KPAD HLE fed by a real Bluetooth Wii Remote. The game calls KPADRead once per
// frame with room for 16 KPADStatus entries and only looks at entry 0; with a
// Classic Controller it also calls KPADGetUnifiedWpadStatus for the raw
// WPADCLStatus (buttons, sticks and triggers of the extension).
namespace {

constexpr uint32_t kKpadStatusSize = 0x84;

// KPADStatus field offsets (RVL SDK).
constexpr uint32_t kHold = 0x00, kTrig = 0x04, kRelease = 0x08, kAcc = 0x0C, kAccValue = 0x18,
                   kAccSpeed = 0x1C, kPos = 0x20, kAccVertical = 0x54, kDevType = 0x5C, kWpadErr = 0x5D,
                   kDpdValidFg = 0x5E, kDataFormat = 0x5F, kFsStick = 0x60, kFsAcc = 0x68, kFsAccValue = 0x74,
                   kFsAccSpeed = 0x78;
// KPADStatus.ex_status.cl (KPADEXStatus, Classic Controller view).
constexpr uint32_t kClHold = 0x60, kClTrig = 0x64, kClRelease = 0x68, kClLStick = 0x6C, kClRStick = 0x74,
                   kClLTrigger = 0x7C, kClRTrigger = 0x80;

// KPADUnifiedWpadStatus: WPADStatus / WPADFSStatus / WPADCLStatus union, then fmt.
constexpr uint32_t kUnifiedSize = 0x38;
constexpr uint32_t kUButton = 0x00, kUAccX = 0x02, kUAccY = 0x04, kUAccZ = 0x06, kUObj = 0x08, kUDev = 0x28,
                   kUErr = 0x29, kUFsStickX = 0x2A, kUFsStickY = 0x2B, kUFsAccX = 0x2C, kUFsAccY = 0x2E,
                   kUFsAccZ = 0x30, kUClButton = 0x2A, kUClLStickX = 0x2C, kUClLStickY = 0x2E, kUClRStickX = 0x30,
                   kUClRStickY = 0x32, kUClTriggerL = 0x34, kUClTriggerR = 0x35, kUFmt = 0x36;

// WPAD device types (WPAD_DEV_*) and the data formats KPAD runs each of them
// in (WPAD_FMT_*_ACC_DPD): the values KPADStatus.dev_type / data_format and
// KPADUnifiedWpadStatus.dev / fmt carry on the console.
constexpr uint8_t kDevCore = 0;
constexpr uint8_t kDevFreestyle = 1;
constexpr uint8_t kDevClassic = 2;
constexpr uint8_t kFmtCoreAccDpd = 2;
constexpr uint8_t kFmtFreestyleAccDpd = 5;
constexpr uint8_t kFmtClassicAccDpd = 8;
constexpr int8_t kWpadErrNone = 0;
constexpr int8_t kWpadErrNoController = -1;

// Raw accelerometer as WPADStatus carries it: 10 bits, 0x200 at 0 g, 100 per g.
constexpr float kRawAccZero = 512.0f;
constexpr float kRawAccPerG = 100.0f;

struct ChannelState {
    uint32_t prevHold = 0;
    uint32_t prevClHold = 0;
    float prevAcc[3] = {0.0f, -1.0f, 0.0f};
    float prevFsAcc[3] = {0.0f, -1.0f, 0.0f};
};

std::array<ChannelState, 4> g_channels{};

// Euclidean length of a 3-vector.
float Length(const float* v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// Euclidean distance between two 3-vectors.
float Distance(const float* a, const float* b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return Length(d);
}

// Writes three big-endian floats to guest memory.
void WriteVec3(uint32_t addr, const float* v) {
    Memory::WriteFloat32(addr, v[0]);
    Memory::WriteFloat32(addr + 4, v[1]);
    Memory::WriteFloat32(addr + 8, v[2]);
}

// Zeroes `count` consecutive floats in guest memory.
void WriteZeroFloats(uint32_t addr, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        Memory::WriteFloat32(addr + i * 4, 0.0f);
    }
}

// Fills one KPADStatus at `addr` and returns the number of valid entries (1),
// or writes an "unplugged" status and returns 0.
int32_t WriteStatus(uint32_t chan, uint32_t addr, const WiiRemoteInput::KpadSample* sample) {
    ChannelState& state = g_channels[chan];
    if (sample == nullptr) {
        state = {};
        Memory::Write32(addr + kHold, 0);
        Memory::Write32(addr + kTrig, 0);
        Memory::Write32(addr + kRelease, 0);
        Memory::Write8(addr + kDevType, kDevCore);
        Memory::Write8(addr + kWpadErr, static_cast<uint8_t>(kWpadErrNoController));
        Memory::Write8(addr + kDpdValidFg, 0);
        return 0;
    }

    const uint32_t hold = sample->hold;
    Memory::Write32(addr + kHold, hold);
    Memory::Write32(addr + kTrig, hold & ~state.prevHold);
    Memory::Write32(addr + kRelease, state.prevHold & ~hold);
    state.prevHold = hold;

    WriteVec3(addr + kAcc, sample->acc);
    Memory::WriteFloat32(addr + kAccValue, Length(sample->acc));
    Memory::WriteFloat32(addr + kAccSpeed, Distance(sample->acc, state.prevAcc));
    for (int i = 0; i < 3; ++i) state.prevAcc[i] = sample->acc[i];

    // No IR pointer: pos .. acc_vertical zeroed and dpd_valid_fg clear, which
    // the game treats as "pointing away from the screen".
    WriteZeroFloats(addr + kPos, (kAccVertical + 8 - kPos) / 4);
    Memory::Write8(addr + kDpdValidFg, 0);

    const uint8_t devType = sample->hasClassic ? kDevClassic : sample->hasNunchuk ? kDevFreestyle : kDevCore;
    const uint8_t dataFormat =
        sample->hasClassic ? kFmtClassicAccDpd : sample->hasNunchuk ? kFmtFreestyleAccDpd : kFmtCoreAccDpd;
    Memory::Write8(addr + kDevType, devType);
    Memory::Write8(addr + kWpadErr, static_cast<uint8_t>(kWpadErrNone));
    Memory::Write8(addr + kDataFormat, dataFormat);

    if (sample->hasClassic) {
        const uint32_t clHold = sample->clHold;
        Memory::Write32(addr + kClHold, clHold);
        Memory::Write32(addr + kClTrig, clHold & ~state.prevClHold);
        Memory::Write32(addr + kClRelease, state.prevClHold & ~clHold);
        state.prevClHold = clHold;
        Memory::WriteFloat32(addr + kClLStick, sample->clLStick[0]);
        Memory::WriteFloat32(addr + kClLStick + 4, sample->clLStick[1]);
        Memory::WriteFloat32(addr + kClRStick, sample->clRStick[0]);
        Memory::WriteFloat32(addr + kClRStick + 4, sample->clRStick[1]);
        Memory::WriteFloat32(addr + kClLTrigger, sample->clTriggerL / 255.0f);
        Memory::WriteFloat32(addr + kClRTrigger, sample->clTriggerR / 255.0f);
    } else if (sample->hasNunchuk) {
        state.prevClHold = 0;
        Memory::WriteFloat32(addr + kFsStick, sample->stick[0]);
        Memory::WriteFloat32(addr + kFsStick + 4, sample->stick[1]);
        WriteVec3(addr + kFsAcc, sample->nunchukAcc);
        Memory::WriteFloat32(addr + kFsAccValue, Length(sample->nunchukAcc));
        Memory::WriteFloat32(addr + kFsAccSpeed, Distance(sample->nunchukAcc, state.prevFsAcc));
        for (int i = 0; i < 3; ++i) state.prevFsAcc[i] = sample->nunchukAcc[i];
    } else {
        state.prevClHold = 0;
        WriteZeroFloats(addr + kFsStick, (kKpadStatusSize - kFsStick) / 4);
    }
    return 1;
}

// One accelerometer axis of KPAD's g vector back to the 10-bit raw WPAD value.
uint16_t RawAcc(float g) {
    const float raw = kRawAccZero + g * kRawAccPerG;
    return static_cast<uint16_t>(std::clamp(raw, 0.0f, 1023.0f));
}

// Fills one KPADUnifiedWpadStatus at `addr` from the sample: the WPADStatus core
// (remote buttons, raw accelerometer, no IR objects), then the Nunchuk or Classic
// Controller tail, then the data format.
void WriteUnifiedStatus(uint32_t addr, const WiiRemoteInput::KpadSample* sample) {
    for (uint32_t offset = 0; offset < kUnifiedSize; offset += 4) {
        Memory::Write32(addr + offset, 0);
    }
    if (sample == nullptr) {
        Memory::Write8(addr + kUDev, kDevCore);
        Memory::Write8(addr + kUErr, static_cast<uint8_t>(kWpadErrNoController));
        Memory::Write8(addr + kUFmt, kFmtCoreAccDpd);
        return;
    }
    Memory::Write16(addr + kUButton, static_cast<uint16_t>(sample->hold & 0xFFFF));
    // KPAD's acc is (-wiiX, -wiiZ, wiiY); WPADStatus keeps the remote's own axes.
    Memory::Write16(addr + kUAccX, RawAcc(-sample->acc[0]));
    Memory::Write16(addr + kUAccY, RawAcc(sample->acc[2]));
    Memory::Write16(addr + kUAccZ, RawAcc(-sample->acc[1]));
    // No IR: every DPDObject invalid (x/y at the sensor's out-of-range value).
    for (uint32_t i = 0; i < 4; ++i) {
        Memory::Write16(addr + kUObj + i * 8, 0x3FF);
        Memory::Write16(addr + kUObj + i * 8 + 2, 0x3FF);
    }
    Memory::Write8(addr + kUErr, static_cast<uint8_t>(kWpadErrNone));
    if (sample->hasClassic) {
        Memory::Write8(addr + kUDev, kDevClassic);
        Memory::Write16(addr + kUClButton, static_cast<uint16_t>(sample->clHold & 0xFFFF));
        Memory::Write16(addr + kUClLStickX, static_cast<uint16_t>(sample->clLStickRaw[0]));
        Memory::Write16(addr + kUClLStickY, static_cast<uint16_t>(sample->clLStickRaw[1]));
        Memory::Write16(addr + kUClRStickX, static_cast<uint16_t>(sample->clRStickRaw[0]));
        Memory::Write16(addr + kUClRStickY, static_cast<uint16_t>(sample->clRStickRaw[1]));
        Memory::Write8(addr + kUClTriggerL, sample->clTriggerL);
        Memory::Write8(addr + kUClTriggerR, sample->clTriggerR);
        Memory::Write8(addr + kUFmt, kFmtClassicAccDpd);
    } else if (sample->hasNunchuk) {
        Memory::Write8(addr + kUDev, kDevFreestyle);
        // WPADFSStatus: 8-bit stick (centre 128) and 10-bit Nunchuk accelerometer.
        Memory::Write8(addr + kUFsStickX,
                       static_cast<uint8_t>(std::clamp(128.0f + sample->stick[0] * 100.0f, 0.0f, 255.0f)));
        Memory::Write8(addr + kUFsStickY,
                       static_cast<uint8_t>(std::clamp(128.0f + sample->stick[1] * 100.0f, 0.0f, 255.0f)));
        Memory::Write16(addr + kUFsAccX, RawAcc(-sample->nunchukAcc[0]));
        Memory::Write16(addr + kUFsAccY, RawAcc(sample->nunchukAcc[2]));
        Memory::Write16(addr + kUFsAccZ, RawAcc(-sample->nunchukAcc[1]));
        Memory::Write8(addr + kUFmt, kFmtFreestyleAccDpd);
    } else {
        Memory::Write8(addr + kUDev, kDevCore);
        Memory::Write8(addr + kUFmt, kFmtCoreAccDpd);
    }
}

} // namespace
#endif

// KPADRead: fills KPADStatus[0] for `chan` from the active host input source, returns the entry count.
extern "C" int32_t KPAD__Read_HLE(uint32_t chan, uint32_t statusPtr, uint32_t count)
{
#if defined(MKW_TARGET_VITA)
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
        g_latchedCorePress = 0;
        g_latchedClassicPress = 0;
    } catch (const Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_HLE, "input KPADRead", e);
        return 0;
    }
    return 1;
#else
    if (chan >= g_channels.size() || statusPtr == 0 || count == 0) {
        return 0;
    }
    WiiRemoteInput::KpadSample sample;
    const bool have = WiiRemoteInput::ReadKpadSample(chan, sample);
    try {
        return WriteStatus(chan, statusPtr, have ? &sample : nullptr);
    } catch (const Memory::AccessViolation&) {
        return 0;
    }
#endif
}
PPC_NATIVE_OVERRIDE(80197380, KPAD__Read_HLE, int32_t, (uint32_t chan, uint32_t statusPtr, uint32_t count),
         (chan, statusPtr, count));

// KPADGetUnifiedWpadStatus: the raw WPAD status behind KPADStatus. The game
// reads the Classic Controller's buttons, sticks and triggers from here. The
// SDK fills `count` entries with the channel's recent samples (the game asks for
// as many as it asked KPADRead for and looks at entry 0); with one sample per
// frame here, every entry gets the current one.
extern "C" int32_t KPAD__GetUnifiedWpadStatus_HLE(uint32_t chan, uint32_t statusPtr, uint32_t count)
{
#if defined(MKW_TARGET_VITA)
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
#else
    constexpr uint32_t kMaxEntries = 16; // KPAD_MAX_READ_BUFS
    if (chan >= g_channels.size() || statusPtr == 0 || count == 0) {
        return 0;
    }
    WiiRemoteInput::KpadSample sample;
    const bool have = WiiRemoteInput::ReadKpadSample(chan, sample);
    try {
        const uint32_t entries = std::min(count, kMaxEntries);
        for (uint32_t i = 0; i < entries; ++i) {
            WriteUnifiedStatus(statusPtr + i * kUnifiedSize, have ? &sample : nullptr);
        }
    } catch (const Memory::AccessViolation&) {
        return 0;
    }
    return have ? 1 : 0;
#endif
}
PPC_NATIVE_OVERRIDE(8019812C, KPAD__GetUnifiedWpadStatus_HLE, int32_t,
         (uint32_t chan, uint32_t statusPtr, uint32_t count), (chan, statusPtr, count));
