#pragma once
#include <cstdint>

namespace wiicompiled::vita::time_trial {
void initialize(bool enabled);
// PAL RMCP01 layouts verified against the translated constructors and original
// scenario/controller setup. These are guest addresses, never native structs.
struct GuestState {
    uint32_t sectionManager=0, context=0, racedata=0, inputManager=0, saveManager=0;
};
void configure_scenario(const GuestState& state);

// Read-only PAL RMCP01 state at a completed presentation boundary. Valid bits
// distinguish objects not yet created from a real zero-valued guest state.
struct ProgressSnapshot {
    enum : uint32_t { SectionValid=1, SceneValid=2, RaceValid=4, HomeValid=8, CameraValid=16, PadValid=32 };
    uint32_t valid=0, section=0, scene=0, race=0;
    uint32_t sectionId=UINT32_MAX, transition=UINT32_MAX, layers=UINT32_MAX, topPage=0;
    uint32_t sectionPaused=UINT32_MAX, controllerPaused=UINT32_MAX;
    uint32_t sceneId=UINT32_MAX, scenePaused=UINT32_MAX, scenePhase=UINT32_MAX;
    uint32_t sceneCounter=UINT32_MAX, sceneFlags=0;
    uint32_t stage=UINT32_MAX, raceFrames=UINT32_MAX, countdownReady=UINT32_MAX;
    int32_t introTimer=0;
    uint32_t homeActive=UINT32_MAX, cameras=UINT32_MAX, pansFinished=UINT32_MAX;
    uint32_t firstCameraFlags=UINT32_MAX, firstPanFlags=UINT32_MAX;
    uint32_t padRegisteredId=UINT32_MAX, padActiveId=UINT32_MAX, padStatus=UINT32_MAX, padHolder=0;
};
ProgressSnapshot snapshot_progress();
void log_progress(uint64_t completedFrame);
}
