#include "game_time_trial.hpp"
#include "abi_bridge.h"
#include "memory.h"
#include <cstdio>

namespace wiicompiled::vita::time_trial {
namespace {
constexpr uint32_t SectionManagerPointer=0x809c1e38;
constexpr uint32_t RacedataPointer=0x809bd728;
constexpr uint32_t InputManagerPointer=0x809bd70c;
constexpr uint32_t SaveManagerPointer=0x809bd748;
constexpr uint32_t TimeTrialSection=0x1f;
// SectionPad::CalculateID (8061BE40) packs (channel + 1) in bits 8+.
// GameCube kind 0x24 on physical port 0 is 0x124, not just 0x24.
constexpr uint32_t GameCubePortZeroId=0x124;
bool started=false, reportedNoLicense=false, testEnabled=false;

// The original calls can use/yield the current guest stack. Restore the whole
// register file before the intercepted SectionMgr::Update resumes.
struct RestoreCpu {
    CpuContext* cpu;
    CpuContext saved;
    explicit RestoreCpu(CpuContext* c):cpu(c),saved(*c) {}
    ~RestoreCpu() { *cpu=saved; }
};

void on_call(uint32_t target, CpuContext* cpu) {
    if (target==0x805302C4u) {
        if (started) {
            const auto menu=cpu->gpr[3]+0xc10;
            std::fprintf(stderr,"[test-time-trial] init_race character=%u kart=%u course=%u mode=%u player_type=%u\n",
                Memory::Read32(menu+20),Memory::Read32(menu+16),Memory::Read32(menu+0xb48),
                Memory::Read32(menu+0xb50),Memory::Read32(menu+24));
        }
        return;
    }
    if (target==0x80634FBCu) {
        if (started && Memory::Read32(cpu->gpr[3]+12)==TimeTrialSection)
            std::fprintf(stderr,"[test-time-trial] load_section=0x1f\n");
        return;
    }
    if (started || target!=0x8063550Cu) return;
    const uint32_t manager=cpu->gpr[3];
    if (!Memory::Contains(manager,0x9c) || manager!=Memory::Read32(SectionManagerPointer)) return;
    const auto section=Memory::Read32(manager);
    if (!Memory::Contains(section,4) || Memory::Read32(manager+0x30)!=0 ||
        Memory::Read32(manager+0xc)!=UINT32_MAX) return;
    const auto id=Memory::Read32(section);
    // Wait for the normal boot/save/title initialization. Also allow the first
    // Single Player menu after creating a licence on a fresh NAND.
    if (!((id>=0x3f && id<=0x43) || id==0x48)) return;
    GuestState state{manager,Memory::Read32(manager+0x98),Memory::Read32(RacedataPointer),
                     Memory::Read32(InputManagerPointer),Memory::Read32(SaveManagerPointer)};
    if (!Memory::Contains(state.context,0x510) || !Memory::Contains(state.racedata,0x1800) ||
        !Memory::Contains(state.inputManager,0x3c50) || !Memory::Contains(state.saveManager,0x25008)) return;
    if (Memory::Read8(state.saveManager+0x25000)!=0 || Memory::Read8(state.saveManager+0x25001)==0) return;
    const auto raw=Memory::Read32(state.saveManager+0x14);
    if (!Memory::Contains(raw,4*0x8cc0+12)) return;
    int license=-1;
    const int selected=static_cast<int16_t>(Memory::Read16(state.saveManager+0x36));
    for (int i=0;i<4;++i) {
        const int candidate=(selected>=0 && selected<4)?(selected+i)%4:i;
        if (Memory::Read32(raw+candidate*0x8cc0+8)==0x524b5044) {license=candidate;break;}
    }
    if (license<0) {
        if (!reportedNoLicense) {
            reportedNoLicense=true;
            std::fprintf(stderr,"[test-time-trial] waiting_for_existing_license=1\n");
        }
        return;
    }
    started=true;
    RestoreCpu guard(cpu);
    const auto call=[&](uint32_t address,uint32_t a,uint32_t b=0,uint32_t c=0,uint32_t d=0) {
        cpu->gpr[3]=a;cpu->gpr[4]=b;cpu->gpr[5]=c;cpu->gpr[6]=d;
        InvokeIndirectCpu(address,cpu);
    };
    call(0x80544CD8,state.saveManager,static_cast<uint32_t>(license)); // Select existing licence.
    call(0x805E40A8,state.context); // Original licence/context defaults.
    call(0x808516BC,0,1); // Original single-player scenario/menu initialization.
    call(0x80524438,state.inputManager,0,3,0); // GameCube controller, physical port 0.
    call(0x8061B490,manager+0x34,0,GameCubePortZeroId); // Registered pad: GameCube port 0.
    call(0x8061B420,manager+0x34,0,0); // Manual drift.
    configure_scenario(state);
    call(0x805E39D8,state.context); // Preserve the normal Mii metadata copy.
    call(0x80635A3C,manager,TimeTrialSection,0); // Normal scene transition/resource loading.
    call(0x80635AC8,manager,0,255);
    std::fprintf(stderr,"[test-time-trial] requested=1 license=%d character=0 kart=1 course=8 mode=2 section=0x1f controller=gamecube_port0 controller_id=0x%x\n",license,GameCubePortZeroId);
}
}

void configure_scenario(const GuestState& state) {
    const auto context=state.context;
    Memory::Write32(context+0x124,1); // One local player.
    Memory::Write32(context+0x12c,0); // Mario.
    Memory::Write32(context+0x13c,1); // Standard Kart M.
    Memory::Write32(context+0x14c,8); // Luigi Circuit.
    Memory::Write32(context+0x164,1); // Manual drift selection.
    Memory::Write32(context+0x3c4,0); // No ghost selected.
    Memory::Write32(context+0x3c8,8);
    Memory::Write32(context+0x3cc,Memory::Read16(state.saveManager+0x36));
    const auto menu=state.racedata+0xc10;
    Memory::Write8(menu+4,1);Memory::Write8(menu+5,1);Memory::Write8(menu+6,1);
    for (uint32_t i=0;i<12;++i) {
        const auto player=menu+8+i*0xf0;
        Memory::Write32(player+0x10,i?5:0); // Absent players, one local player; no CPUs/ghosts.
        Memory::Write8(player+5,i?0xff:0);Memory::Write8(player+6,i?0xff:0);
    }
    Memory::Write32(menu+16,1);Memory::Write32(menu+20,0);
    Memory::Write32(menu+0xd8,3); // GameCube controller type.
    Memory::Write32(menu+0xb48,8);Memory::Write32(menu+0xb4c,2); // Course, 150cc.
    Memory::Write32(menu+0xb50,2);Memory::Write32(menu+0xb54,0); // Time Trial, player camera.
    Memory::Write32(menu+0xb58,0);Memory::Write32(menu+0xb5c,0);Memory::Write32(menu+0xb60,0);
    for (uint32_t i=0;i<4;++i) Memory::Write8(menu+0xb64+i,i?0xff:0);
    Memory::Write32(menu+0xb68,0);Memory::Write8(menu+0xb6c,0);Memory::Write8(menu+0xb6d,3);
    Memory::Write32(menu+0xb70,0); // No mirror/teams/competition.
}

void initialize(bool enabled) {
    started=false;reportedNoLicense=false;
    testEnabled=enabled;
    RuntimeGameTestOptions::callHook=enabled?on_call:nullptr;
    std::fprintf(stderr,"[test-time-trial] enabled=%u character=0 kart=1 course=8\n",unsigned(enabled));
}

ProgressSnapshot snapshot_progress() {
    ProgressSnapshot out;
    // Same singleton chains/fields as translated SectionMgr, GameScene::
    // GetCurrent (8051BED0), RaceScene::isPaused (80554E14), RaceInfo::Update,
    // and RaceCameraMgr::HasEveryOpeningPanEnded (805A8FBC). Never invoke guest
    // callbacks from diagnostics: they may mutate state, yield, or use its CPU.
    const auto readable=[](uint32_t p,size_t n) { return p && Memory::Contains(p,n); };
    const auto manager=Memory::Read32(SectionManagerPointer);
    if(readable(manager,0x34)) {
        out.transition=Memory::Read32(manager+0x30);
        out.section=Memory::Read32(manager);
        if(readable(manager,0x48)) {
            out.valid|=ProgressSnapshot::PadValid;
            out.padRegisteredId=Memory::Read32(manager+0x38);
            out.padActiveId=Memory::Read32(manager+0x3c);
            out.padStatus=Memory::Read32(manager+0x40);
            out.padHolder=Memory::Read32(manager+0x44);
        }
        if(readable(out.section,0x38c)) {
            out.valid|=ProgressSnapshot::SectionValid;
            out.sectionId=Memory::Read32(out.section);
            out.layers=Memory::Read32(out.section+0x37c);
            if(out.layers && out.layers<=11)
                out.topPage=Memory::Read32(out.section+0x354+4*(out.layers-1));
            out.sectionPaused=Memory::Read8(out.section+0x38b);
            out.controllerPaused=Memory::Read8(out.section+0x38a);
        }
    }
    const auto rk=Memory::Read32(0x80385fc8);
    if(readable(rk,0x58)) {
        const auto sceneManager=Memory::Read32(rk+0x54);
        if(readable(sceneManager,0x10)) {
            out.scene=Memory::Read32(sceneManager+0xc);
            if(readable(out.scene,0x2c))out.sceneId=Memory::Read32(out.scene+0x28);
            if(out.sceneId!=0 && out.sceneId!=5 && out.sceneId!=UINT32_MAX && readable(out.scene,0x2542)) {
                out.valid|=ProgressSnapshot::SceneValid;
                out.scenePaused=Memory::Read8(out.scene+0x2540);
                out.scenePhase=Memory::Read8(out.scene+0x2541);
                out.sceneCounter=Memory::Read32(out.scene+0x2534);
                out.sceneFlags=Memory::Read32(0x809c1874);
            }
        }
    }
    out.race=Memory::Read32(0x809bd730);
    if(readable(out.race,0x30)) {
        out.valid|=ProgressSnapshot::RaceValid;
        out.introTimer=static_cast<int16_t>(Memory::Read16(out.race+0x1e));
        out.raceFrames=Memory::Read32(out.race+0x20);
        out.stage=Memory::Read32(out.race+0x28);
        out.countdownReady=Memory::Read8(out.race+0x2e);
    }
    const auto home=Memory::Read32(0x809bd6f8);
    if(readable(home,0x4d)) {
        out.valid|=ProgressSnapshot::HomeValid;
        out.homeActive=Memory::Read8(home+0x4c);
    }
    const auto cameraManager=Memory::Read32(0x809c19b8);
    if(readable(cameraManager,0x24)) {
        out.cameras=Memory::Read32(cameraManager+0x20);
        const auto array=Memory::Read32(cameraManager+0x18);
        // PAL normal race has at most four screens. Reject corrupt counts
        // before walking guest pointers or treating a partial read as ready.
        bool valid=out.cameras<=4 && (!out.cameras || readable(array,out.cameras*4));
        uint32_t finished=0;
        for(uint32_t i=0;valid && i<out.cameras;++i) {
            const auto camera=Memory::Read32(array+4*i);
            if(!readable(camera,0x336)) { valid=false;break; }
            const auto pan=Memory::Read32(camera+0x198);
            if(!readable(pan,0x6e)) { valid=false;break; }
            const auto flags=Memory::Read16(pan+0x6c);
            if(!i) {
                out.firstCameraFlags=Memory::Read16(camera+0x334);
                out.firstPanFlags=flags;
            }
            if(flags&8)++finished;
        }
        if(valid) { out.valid|=ProgressSnapshot::CameraValid;out.pansFinished=finished; }
    }
    return out;
}

void log_progress(uint64_t frame) {
    if(!testEnabled)return;
    try {
        const auto s=snapshot_progress();
        std::fprintf(stderr,"[race-progress] frame=%llu valid=0x%02x section=0x%08x section_id=0x%x transition=%u layers=%u top_page=0x%08x section_paused=%u controller_paused=%u scene=0x%08x scene_id=%u scene_paused=%u scene_phase=%u scene_counter=%u scene_flags=0x%x race=0x%08x stage=%u intro_timer=%d race_frames=%u countdown_ready=%u home_active=%u cameras=%u pans_finished=%u camera_flags=0x%x pan_flags=0x%x pad_registered=0x%x pad_active=0x%x pad_status=%u pad_holder=0x%08x\n",
            static_cast<unsigned long long>(frame),s.valid,s.section,s.sectionId,s.transition,
            s.layers,s.topPage,s.sectionPaused,s.controllerPaused,s.scene,s.sceneId,s.scenePaused,
            s.scenePhase,s.sceneCounter,s.sceneFlags,s.race,s.stage,s.introTimer,s.raceFrames,
            s.countdownReady,s.homeActive,s.cameras,s.pansFinished,s.firstCameraFlags,s.firstPanFlags,
            s.padRegisteredId,s.padActiveId,s.padStatus,s.padHolder);
    } catch(const Memory::AccessViolation&) {
        std::fprintf(stderr,"[race-progress] frame=%llu snapshot_failed=1\n",
            static_cast<unsigned long long>(frame));
    }
}
}
