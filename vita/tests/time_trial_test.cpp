#include "game_time_trial.hpp"
#include "abi_bridge.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <vector>
#include <unordered_map>

// Execute the real optional boot hook and actual translated player-count
// routine. Only the other guest calls and storage are replaced.
#include "../../generated/functions/func_8052F788.cpp"
#include "../../generated/functions/func_8051BED0.cpp"
#include "../../generated/functions/func_80554E14.cpp"
#include "../../generated/functions/func_805A8FBC.cpp"
#include "../../generated/functions/func_8061BE40.cpp"
#include "../../generated/functions/func_8061B490.cpp"
#include "../../generated/functions/func_8061B9BC.cpp"
#include "../../generated/functions/func_8052286C.cpp"
#include "../../generated/functions/func_80522864.cpp"

namespace {
constexpr uint32_t Manager=0x80010000,Section=0x80011000,Context=0x80012000,
    Racedata=0x80020000,Input=0x80030000,Save=0x80040000,RawSave=0x80080000;
std::unordered_map<uint32_t,uint8_t> bytes;
CpuContext cpu{};
struct Call {uint32_t address;std::array<uint32_t,4> args;};
std::vector<Call> calls;
void check(bool condition,const char* message) {
    if(!condition){std::fprintf(stderr,"Time Trial: %s\n",message);std::abort();}
}
template<uint32_t Address> void guest_call(CpuContext* c) {
    calls.push_back({Address,{c->gpr[3],c->gpr[4],c->gpr[5],c->gpr[6]}});
    if constexpr(Address==0x80544CD8)Memory::Write16(Save+0x36,c->gpr[4]);
    if constexpr(Address==0x80635A3C)Memory::Write32(Manager+12,c->gpr[4]);
    if constexpr(Address==0x80635AC8)Memory::Write32(Manager+0x30,2);
    if constexpr(Address==0x8061B490)func_8061B490(c);
    // Force the boot hook to restore volatile and nonvolatile registers alike.
    std::fill(std::begin(c->gpr),std::end(c->gpr),0xcccccccc);c->lr=0xdeadbeef;c->fpr[1].d=99;
}
const std::array records{
    RawDispatchRecord{0x8052286C,func_8052286C,0,false},
    RawDispatchRecord{0x80522864,func_80522864,0,false},
    RawDispatchRecord{0x80544CD8,guest_call<0x80544CD8>,0,false},
    RawDispatchRecord{0x805E40A8,guest_call<0x805E40A8>,0,false},
    RawDispatchRecord{0x808516BC,guest_call<0x808516BC>,0,false},
    RawDispatchRecord{0x80524438,guest_call<0x80524438>,0,false},
    RawDispatchRecord{0x8061B490,guest_call<0x8061B490>,0,false},
    RawDispatchRecord{0x8061B420,guest_call<0x8061B420>,0,false},
    RawDispatchRecord{0x805E39D8,guest_call<0x805E39D8>,0,false},
    RawDispatchRecord{0x80635A3C,guest_call<0x80635A3C>,0,false},
    RawDispatchRecord{0x80635AC8,guest_call<0x80635AC8>,0,false}};
void setup() {
    bytes.clear();calls.clear();cpu={};cpu.gpr[3]=Manager;cpu.gpr[1]=0x81700000;
    cpu.lr=0x80001234;cpu.fpr[1].d=1.25;
    Memory::Write32(0x809c1e38,Manager);Memory::Write32(0x809bd728,Racedata);
    Memory::Write32(0x809bd70c,Input);Memory::Write32(0x809bd748,Save);
    Memory::Write32(Manager,Section);Memory::Write32(Manager+0x98,Context);
    Memory::Write32(Manager+12,UINT32_MAX);Memory::Write32(Section,0x3f);
    Memory::Write32(Save+0x14,RawSave);Memory::Write16(Save+0x36,0xffff);
    Memory::Write8(Save+0x25001,1);
}
void update(){ApplyRuntimeCallOptions(0x8063550c,&cpu);}
}
namespace GuestFlat {wiicompiled::RamMirrors g_vitaRamMirrors{};}
namespace RecompMod {std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};}
namespace MemoryInline {
PageEntry g_pageTable[kPageCount]{};
uintptr_t g_fullPageBias[kPageCount]{},g_fullReadablePageBias[kPageCount]{},g_fullWritablePageBias[kPageCount]{};
const SparseWritablePageTable* g_sparseWritablePageTables[kPageCount]{};
uint8_t g_deferredReadCoveredPages[kPageCount]{};
uint8_t Read8Slow(uint32_t a){const auto it=bytes.find(a);return it==bytes.end()?0:it->second;}
uint16_t Read16Slow(uint32_t a){return (uint16_t(Read8Slow(a))<<8)|Read8Slow(a+1);}
uint32_t Read32Slow(uint32_t a){return (uint32_t(Read16Slow(a))<<16)|Read16Slow(a+2);}
void Write8Slow(uint32_t a,uint8_t v){bytes[a]=v;}
void Write16Slow(uint32_t a,uint16_t v){bytes[a]=v>>8;bytes[a+1]=v;}
void Write32Slow(uint32_t a,uint32_t v){Write16Slow(a,v>>16);Write16Slow(a+2,v);}
}
bool Memory::Contains(uint32_t a,size_t n){return a>=0x80000000 && uint64_t(a)+n<=0x81800000;}
CpuContext& GetPersistentCpuContext(){return cpu;}
void TranslatedFunctionRegistry::Register(TranslatedFunctionInfo){}
void TranslatedFunctionRegistry::Finalize(){lookupPublished_.store(true,std::memory_order_release);}
const TranslatedFunctionInfo* TranslatedFunctionRegistry::FindByAddressPtrSlow(uint32_t address){
    // Only the reference pause routine calls this helper. It preserves the
    // fields populated by the test; the observer must never call guest code.
    static const auto pauseHelper=[] {
        TranslatedFunctionInfo info;info.address=0x806354C0;info.nonvolatileFprWriteMask=0;
        info.rawCpuInvoker=+[](CpuContext*) {};return info;
    }();
    return address==pauseHelper.address?&pauseHelper:nullptr;
}
const RawDispatchRecord* TranslatedFunctionRegistry::FindRawByAddressPtrSlow(uint32_t a){
    for(const auto& record:records)if(record.address==a)return &record;
    return nullptr;
}
namespace RuntimeCrash {[[noreturn]] void FatalMissingGuestTarget(uint32_t,CpuContext*) noexcept{std::abort();}}

int main(){
    using namespace wiicompiled::vita::time_trial;
    TranslatedFunctionRegistry::Finalize();
    setup();initialize(false);Memory::Write32(RawSave+8,0x524b5044);
    const auto disabled=bytes;update();check(calls.empty() && bytes==disabled,"disabled flag altered boot");
    setup();initialize(true);
    Memory::Write32(Section,0x1f);update();check(calls.empty(),"hook changed an existing race");
    Memory::Write32(Section,0x3f);Memory::Write32(Manager+0x30,2);update();
    check(calls.empty(),"hook interrupted an in-progress scene transition");
    Memory::Write32(Manager+0x30,0);Memory::Write8(Save+0x25000,1);update();
    check(calls.empty(),"hook ran while save data was busy");
    Memory::Write8(Save+0x25000,0);update();check(calls.empty(),"hook created or used a missing licence");
    Memory::Write32(RawSave+2*0x8cc0+8,0x524b5044);
    Memory::Write32(Context+0x500,0xaabbccdd);Memory::Write32(Racedata+0xc0c,0x11223344);
    const auto saved=cpu;update();
    check(calls.size()==9,"normal scene/controller/licence setup was skipped");
    check(!std::memcmp(cpu.gpr,saved.gpr,sizeof(cpu.gpr)) && cpu.lr==saved.lr && cpu.fpr[1].d==saved.fpr[1].d,"registers not restored");
    check(calls[0].args[1]==2 && calls[2].args[1]==1,"licence or local player count incorrect");
    check(calls[3].args==std::array<uint32_t,4>{Input,0,3,0},"physical GameCube port not bound");
    // Obtain the controller ID from the original game routine with its actual
    // translated GameCube type/channel virtual methods. Kind alone (0x24)
    // omits the port and makes SectionPad report a disconnected local player.
    constexpr uint32_t Gcn=Input+0x3ba0,GcnVtable=0x80090000;
    Memory::Write32(Input+12,Gcn);Memory::Write32(Gcn,GcnVtable);
    Memory::Write32(GcnVtable+0x10,0x8052286C);Memory::Write32(GcnVtable+0x34,0x80522864);
    // The translated GCN update sets +0x50 to one after a successful sample.
    // It is the connection-ready flag used by CalculateID, despite some
    // external layout headers naming this byte "hasError".
    Memory::Write32(Gcn+0x9c,0);Memory::Write8(Gcn+0x50,1);
    auto padCpu=cpu;padCpu.gpr[3]=Input+4;func_8061BE40(&padCpu);
    const auto originalId=padCpu.gpr[3];
    check(originalId==0x124 && calls[4].args[2]==originalId && calls[5].args[2]==0,
        "pad registration disagrees with original type/channel ID");
    const auto sectionPad=Manager+0x34;
    check(Memory::Read32(sectionPad+4)==originalId,"boot did not register the complete controller ID");
    Memory::Write32(sectionPad+8,originalId);Memory::Write32(sectionPad+0x58,1);
    padCpu=cpu;padCpu.gpr[3]=sectionPad;func_8061B9BC(&padCpu);
    check(padCpu.gpr[3]==UINT32_MAX,"correctly registered port reported disconnected");
    Memory::Write32(sectionPad+4,0x24);
    padCpu=cpu;padCpu.gpr[3]=sectionPad;func_8061B9BC(&padCpu);
    check(padCpu.gpr[3]==0,"old kind-only ID did not reproduce false disconnect");
    Memory::Write32(sectionPad+4,originalId);
    // Other physical channels must remain distinct, and a disconnected pad
    // must still be rejected rather than bypassing the game's pause logic.
    Memory::Write32(Gcn+0x9c,1);padCpu=cpu;padCpu.gpr[3]=Input+4;func_8061BE40(&padCpu);
    check(padCpu.gpr[3]==0x224,"original channel encoding changed");
    Memory::Write8(Gcn+0x50,0);padCpu=cpu;padCpu.gpr[3]=Input+4;func_8061BE40(&padCpu);
    check(padCpu.gpr[3]!=originalId,"unready controller was reported as the connected port");
    Memory::Write32(Gcn+0x9c,0);Memory::Write8(Gcn+0x50,1);
    check(Memory::Read32(Manager+12)==0x1f && Memory::Read32(Manager+0x30)==2,"normal TA transition not requested");
    const auto menu=Racedata+0xc10;
    check(Memory::Read32(menu+20)==0 && Memory::Read32(menu+16)==1 && Memory::Read32(menu+0xb48)==8,"Mario/kart/course mismatch");
    check(Memory::Read32(menu+0xb50)==2 && Memory::Read32(menu+0xb4c)==2 && Memory::Read32(menu+0xb54)==0,"TA/150cc/player camera mismatch");
    check(Memory::Read32(menu+0xb70)==0 && Memory::Read8(menu+0xb6d)==3,"mirror or lap count mismatch");
    check(Memory::Read32(Context+0x500)==0xaabbccdd && Memory::Read32(Racedata+0xc0c)==0x11223344,"unrelated fields changed");
    // The title's own computation must agree: 1 total, 1 local, 1 screen.
    cpu.gpr[3]=menu;cpu.gpr[4]=menu+4;cpu.gpr[5]=menu+5;cpu.gpr[6]=menu+6;
    func_8052F788(&cpu);
    check(Memory::Read8(menu+4)==1 && Memory::Read8(menu+5)==1 && Memory::Read8(menu+6)==1,"translated player counts reject the test scenario");
    cpu.gpr[3]=Manager;Memory::Write32(Manager+0x30,0);Memory::Write32(Manager+12,UINT32_MAX);
    update();check(calls.size()==9,"test race restarted automatically a second time");
    // Verify the observer against actual translated pause/camera routines,
    // not just a duplicate set of assumed layout constants.
    constexpr uint32_t Rk=0x800f0000,SceneManager=0x800f1000,Scene=0x80100000,
        Race=0x80200000,Home=0x80210000,CameraManager=0x80220000,
        CameraArray=0x80230000,Camera=0x80240000,Pan=0x80250000;
    Memory::Write32(0x80385fc8,Rk);Memory::Write32(Rk+0x54,SceneManager);
    Memory::Write32(SceneManager+0xc,Scene);Memory::Write32(Scene+0x28,2);
    Memory::Write32(Scene+0x2534,75);Memory::Write8(Scene+0x2540,0);
    Memory::Write8(Scene+0x2541,1);Memory::Write32(0x809c1874,0);
    Memory::Write32(0x809bd730,Race);Memory::Write16(Race+0x1e,0xfffd);
    Memory::Write32(Race+0x20,80);Memory::Write32(Race+0x28,1);
    Memory::Write8(Race+0x2e,1);Memory::Write32(0x809bd6f8,Home);
    Memory::Write8(Home+0x4c,0);Memory::Write32(Section,0x1f);
    Memory::Write32(Section+0x37c,1);Memory::Write32(Section+0x354,0x80260000);
    Memory::Write8(Section+0x38b,0);
    Memory::Write32(0x809c19b8,CameraManager);
    Memory::Write32(CameraManager+0x18,CameraArray);Memory::Write32(CameraManager+0x20,1);
    Memory::Write32(CameraArray,Camera);Memory::Write32(Camera+0x198,Pan);
    Memory::Write16(Camera+0x334,0x20);Memory::Write16(Pan+0x6c,2);
    const auto memoryBefore=bytes;const auto cpuBefore=cpu;const auto callsBefore=calls.size();
    auto progress=snapshot_progress();
    check(bytes==memoryBefore && !std::memcmp(&cpu,&cpuBefore,sizeof(cpu)) && calls.size()==callsBefore,
        "progress observation mutated guest memory/registers or invoked guest code");
    check(progress.valid==63 && progress.sectionId==0x1f && progress.layers==1 && progress.topPage==0x80260000,
        "section/layer snapshot is invalid");
    check(progress.padRegisteredId==0x124 && progress.padActiveId==0x124 && progress.padStatus==0,
        "registered/active controller IDs were not observed");
    check(progress.stage==1 && progress.introTimer==-3 && progress.raceFrames==80 && progress.countdownReady==1,
        "intro/countdown snapshot is invalid");
    auto referenceCpu=cpu;func_8051BED0(&referenceCpu);
    check(progress.scene==referenceCpu.gpr[3] && progress.sceneCounter==75 && progress.scenePhase==1,
        "scene snapshot disagrees with translated GetCurrent");
    referenceCpu=cpu;func_80554E14(&referenceCpu);
    check(progress.homeActive==0 && progress.sectionPaused==referenceCpu.gpr[3],
        "unpaused snapshot disagrees with translated RaceScene::isPaused");
    referenceCpu=cpu;referenceCpu.gpr[3]=CameraManager;func_805A8FBC(&referenceCpu);
    check(progress.pansFinished==0 && referenceCpu.gpr[3]==0 && progress.firstPanFlags==2,
        "opening camera snapshot disagrees with translated pan-ready check");
    Memory::Write16(Pan+0x6c,8);Memory::Write8(Home+0x4c,1);
    progress=snapshot_progress();referenceCpu=cpu;func_80554E14(&referenceCpu);
    check(progress.homeActive==1 && referenceCpu.gpr[3]==1 && progress.sectionPaused==0,
        "home-menu pause must be distinct from section pause");
    referenceCpu=cpu;referenceCpu.gpr[3]=CameraManager;func_805A8FBC(&referenceCpu);
    check(progress.pansFinished==1 && referenceCpu.gpr[3]==1,"finished opening camera is not reported");
    Memory::Write8(Home+0x4c,0);Memory::Write8(Section+0x38b,1);
    progress=snapshot_progress();referenceCpu=cpu;func_80554E14(&referenceCpu);
    check(progress.sectionPaused==1 && referenceCpu.gpr[3]==1,"section pause is not reported");
    Memory::Write32(Scene+0x28,0);Memory::Write32(CameraManager+0x20,UINT32_MAX);
    Memory::Write32(0x809bd6f8,0x817fffff);Memory::Write32(0x809bd730,0);
    progress=snapshot_progress();
    check(!(progress.valid & (ProgressSnapshot::SceneValid|ProgressSnapshot::CameraValid|
        ProgressSnapshot::HomeValid|ProgressSnapshot::RaceValid)),"invalid objects reported as live race state");
    std::puts("Time Trial optional boot/controller/scenario contract passed");
}
