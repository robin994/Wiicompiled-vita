#include "backend.hpp"
#include "wiicompiled/disc.hpp"
#include "boot_disc.hpp"
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <cstdio>
#include <exception>
#include <string>

extern "C" { unsigned int _newlib_heap_size_user=64*1024*1024; }
int main() {
    sceIoMkdir("ux0:data/wiicompiled-vita",0777);
    std::freopen("ux0:data/wiicompiled-vita/iso-check.log","w",stderr);
    std::setvbuf(stderr,nullptr,_IOLBF,0);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    scePowerSetArmClockFrequency(444);scePowerSetGpuClockFrequency(222);
    wiicompiled::Disc disc;
    std::string message="READING MARIOKART.ISO";
    bool mounted=false;
    try {
        disc.open("ux0:data/wiicompiled-vita/mariokart.iso");
        wiicompiled::verifyBootDisc(disc);
        mounted=true;message="PAL DISC VERIFIED";
        std::fprintf(stderr,"[iso-check] RMCP01 iso_bytes=%llu fst_entries=%zu dol=%zu rel=%zu\n",
            static_cast<unsigned long long>(disc.isoSize()),disc.entries().size(),
            size_t(disc.find("/sys/main.dol")->size),size_t(disc.find("/rel/StaticR.rel")->size));
    } catch(const std::exception& e) {
        message="CANNOT READ THE DISC";std::fprintf(stderr,"[iso-check] %s\n",e.what());
    }
    if(!wiicompiled::backend::initialize()) {
        std::fprintf(stderr,"[iso-check] GXM initialization failed\n");return 1;
    }
    for(;;) {
        SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);
        if(pad.buttons&SCE_CTRL_CIRCLE)break;
        if(!wiicompiled::backend::beginFrame()) { sceKernelDelayThread(1000);continue; }
        wiicompiled::backend::rectangle(0,0,960,544,16,22,35);
        wiicompiled::backend::text(48,54,"WIICOMPILED VITA",5);
        wiicompiled::backend::text(48,126,"ISO AND NATIVE GRAPHICS CHECK",3);
        wiicompiled::backend::rectangle(48,198,864,3,mounted?70:220,mounted?180:60,90);
        wiicompiled::backend::text(48,230,message.c_str(),3);
        wiicompiled::backend::text(48,298,"UX0:DATA/WIICOMPILED-VITA/MARIOKART.ISO",2);
        wiicompiled::backend::text(48,360,"THIS CHECK DOES NOT START THE GAME",3);
        wiicompiled::backend::text(48,454,"CIRCLE - EXIT",3);
        wiicompiled::backend::endFrame();
    }
    wiicompiled::backend::shutdown();return mounted?0:1;
}
