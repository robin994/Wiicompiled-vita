#include "wiicompiled/disc_index.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <functional>
using namespace wiicompiled;
static void check(bool ok) { if(!ok)throw std::runtime_error("disc index contract failed"); }
static void rejects(const std::function<void()>& f) { bool yes=false;try{f();}catch(const std::exception&){yes=true;}check(yes); }
static bool read(void*,const std::string& path,uint64_t at,void* out,size_t n) {
    const char* data=path=="disc/a.bin"?"abcd":"EFGH";
    std::memcpy(out,data+at,n);return true;
}
int main() {
    try {
        DiscIndex index{"RMCP01",5000000000ull,4500000000ull,{
            {"/a.bin",0x90000000ull,4,false},{"/b.bin",0x90000004ull,4,false},
            {"/dir",0,0,true}}};
        std::array<char,8> bytes{};
        readDiscIndex(index,0x90000002ull,bytes.data(),5,read,nullptr);
        check(std::memcmp(bytes.data(),"cdEFG",5)==0);
        bytes.fill('x');
        rejects([&]{readDiscIndex(index,0x90000002ull,bytes.data(),7,read,nullptr);});
        check(bytes[0]=='x'); // Missing range detected before any read is issued.
        rejects([&]{readDiscIndex(index,UINT64_MAX,bytes.data(),1,read,nullptr);});
        rejects([&]{readDiscIndex(index,0x90000000ull,nullptr,1,read,nullptr);});
        readDiscIndex(index,index.partitionSize,nullptr,0,read,nullptr);
        std::vector<uint8_t> encoded{'W','I','C','I','D','X','0','1'};
        auto put=[&](uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)encoded.push_back(static_cast<uint8_t>(v>>(8*i)));};
        put(1,4);put(1,4);put(index.isoSize,8);put(index.partitionSize,8);
        for(char c:std::string("RMCP01\0\0",8))encoded.push_back(c);
        put(0x90000000ull,8);put(4,4);put(6,2);put(0,2);
        for(char c:std::string("/a.bin"))encoded.push_back(c);
        check(decodeDiscIndex(encoded.data(),encoded.size()).entries[0].offset==0x90000000ull);
        auto bad=encoded;bad[12]=bad[13]=bad[14]=bad[15]=255;
        rejects([&]{decodeDiscIndex(bad.data(),bad.size());});
        bad=encoded;std::memcpy(bad.data()+56,"/../aa",6);
        rejects([&]{decodeDiscIndex(bad.data(),bad.size());});
        bad=encoded;bad[57]=0;
        rejects([&]{decodeDiscIndex(bad.data(),bad.size());});
        rejects([&]{decodeDiscIndex(encoded.data(),encoded.size()-1);});
        std::cout<<"disc index: cross-file 64-bit ranges, rejected gaps and malformed metadata PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
