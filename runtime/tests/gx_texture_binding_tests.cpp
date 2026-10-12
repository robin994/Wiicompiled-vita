#include "../src/hle/gx/gx_texture_binding_contract.h"
#include "gx_guest_write.h"
#include <array>
#include <cstdio>

namespace {
unsigned failures=0;
void check(bool value,const char* name) {if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",name);}}
}
int main() {
    using namespace GxTextureBindingContract;
    std::array<State,8> bound{};
    for(unsigned i=0;i<8;++i) {bound[i].objAddr=0x80010000+i*32;bound[i].dataAddr=0x81000000+i*4096;}
    const auto original=bound;
    check(CanSkipHostLoad(bound[0],original[0],false,false,false),"equal live binding stays hot");
    check(!CanSkipHostLoad(bound[0],original[0],false,false,false,true),"recorded list must contain its bind");
    // Native A -> a raw IMAGE3 binding B on slot 0 -> explicit native A. The
    // final load must emit A, even when its GXTexObj and sampler are unchanged.
    InvalidateBindings(bound,BindingMaskForBpRegister(0x94));
    check(!CanSkipHostLoad(bound[0],original[0],false,false,false),"raw BP cannot leave native A's shadow valid");
    for(unsigned i=1;i<8;++i)check(CanSkipHostLoad(bound[i],original[i],false,false,false),"unrelated units keep load suppression");
    for(unsigned bank=0;bank<2;++bank)for(unsigned group=0;group<7;++group)for(unsigned unit=0;unit<4;++unit) {
        const uint8_t reg=uint8_t(0x80+bank*32+group*4+unit);
        check(BindingMaskForBpRegister(reg)==uint8_t(1u<<(bank*4+unit)),"all texture BP groups map to the right unit");
    }
    for(unsigned reg:{0u,0x40u,0x9cu,0xbcu,0xc0u,0xfeu})check(BindingMaskForBpRegister(uint8_t(reg))==0,"non-texture BP does not invalidate bindings");
    std::array<uint8_t,256> ram{};
    using GxGuestWrite::PhysicalRangeToHost;
    check(PhysicalRangeToHost(ram.data(),ram.size(),0x80000000,0,256)==ram.data(),"MEM1 physical zero is a valid bank address");
    check(PhysicalRangeToHost(ram.data(),ram.size(),0x90000000,0x10000020,32)==ram.data()+32,"MEM2 physical alias");
    check(!PhysicalRangeToHost(ram.data(),ram.size(),0x90000000,0x0fffffff,1),"before-bank address rejected");
    check(!PhysicalRangeToHost(ram.data(),ram.size(),0x80000000,255,2),"encoded span cannot cross the bank");
    check(!PhysicalRangeToHost(ram.data(),ram.size(),0x80000000,256,1),"end pointer cannot contain a byte");
    check(!PhysicalRangeToHost(nullptr,ram.size(),0x80000000,0,1),"missing bank rejected");
    check(!PhysicalRangeToHost(ram.data(),ram.size(),0x80000000,0,0),"empty span rejected");
    check(!PhysicalRangeToHost(ram.data(),ram.size(),0x80000000,0,UINT32_MAX),"oversized encoded span rejected");
    std::printf("GX native/raw binding and RAM alias contracts: %u failures\n",failures);
    return failures?1:0;
}
