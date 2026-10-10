#include "wiicompiled/disc.hpp"
#include "wiicompiled/guest_ram.hpp"
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("disc contract failed"); }
void put(std::vector<uint8_t>& b,size_t p,uint32_t v) { b[p]=v>>24;b[p+1]=v>>16;b[p+2]=v>>8;b[p+3]=v; }
void rejects(const std::function<void()>& action) {
    bool rejected=false; try { action(); } catch (const std::exception&) { rejected=true; } check(rejected);
}
struct Fixture {
    std::string path;
    static constexpr uint64_t partition=0x90000000ull; // Exercise a seek above 2 GiB.
    static constexpr uint64_t data=partition+0x20000;
    std::vector<uint8_t> logical=std::vector<uint8_t>(3*0x7c00);
    Fixture() {
        char name[]="/tmp/wiicompiled-disc-XXXXXX";
        int fd=mkstemp(name); check(fd>=0); close(fd); path=name;
        std::vector<uint8_t> outer(0x40028),ticket(0x2c0);
        std::memcpy(outer.data(),"RMCP01",6); put(outer,0x18,0x5d1c9ea3);
        put(outer,0x40000,1);put(outer,0x40004,0x40020/4);put(outer,0x40020,partition/4);
        std::array<uint8_t,16> retail{0xeb,0xe4,0x2a,0x22,0x5e,0x85,0x93,0xe4,0x48,0xd9,0xc5,0x45,0x73,0x81,0xaa,0xf7};
        std::array<uint8_t,16> title{},iv{}; for (size_t i=0;i<title.size();++i) title[i]=uint8_t(i*13);
        ticket[0x1dc]=7; std::memcpy(iv.data(),ticket.data()+0x1dc,8);
        AES_KEY common{},key{};AES_set_encrypt_key(retail.data(),128,&common);AES_set_encrypt_key(title.data(),128,&key);
        AES_cbc_encrypt(title.data(),ticket.data()+0x1bf,16,&common,iv.data(),AES_ENCRYPT);
        put(ticket,0x2b8,0x20000/4);put(ticket,0x2bc,3*0x8000/4);
        std::memcpy(logical.data(),outer.data(),8);put(logical,0x18,0x5d1c9ea3);
        put(logical,0x420,0x3000/4);put(logical,0x424,0x4000/4);put(logical,0x428,64/4);
        put(logical,0x4000,0x01000000);put(logical,0x4008,3);
        put(logical,0x400c,0x01000000);put(logical,0x4014,3);
        put(logical,0x4018,4);put(logical,0x401c,0x7bf0/4);put(logical,0x4020,0x8000);
        std::memcpy(logical.data()+0x4024,"dir\0file.bin\0",13);
        for(size_t i=0;i<0x8000;++i) logical[0x7bf0+i]=uint8_t(i*17+3);
        FILE* out=std::fopen(path.c_str(),"wb");check(out);
        check(std::fwrite(outer.data(),1,outer.size(),out)==outer.size());
        check(fseeko(out,partition,SEEK_SET)==0);check(std::fwrite(ticket.data(),1,ticket.size(),out)==ticket.size());
        check(fseeko(out,data,SEEK_SET)==0);
        for(size_t c=0;c<3;++c) {
            std::array<uint8_t,0x8000> plain{},encrypted{};
            const auto* bytes=logical.data()+c*0x7c00;
            for(size_t block=0;block<31;++block) SHA1(bytes+block*0x400,0x400,plain.data()+block*20);
            iv={};AES_cbc_encrypt(plain.data(),encrypted.data(),0x400,&key,iv.data(),AES_ENCRYPT);
            std::memcpy(iv.data(),encrypted.data()+0x3d0,16);
            AES_cbc_encrypt(bytes,encrypted.data()+0x400,0x7c00,&key,iv.data(),AES_ENCRYPT);
            check(std::fwrite(encrypted.data(),1,encrypted.size(),out)==encrypted.size());
        }
        check(std::fclose(out)==0);
    }
    ~Fixture() { std::remove(path.c_str()); }
};
}
int main() {
    try {
        std::array<uint8_t,256> bank1{},bank2{};
        wiicompiled::RamMirrors mirrors{bank1.data(),bank2.data(),bank1.size(),bank2.size()};
        for(uint32_t view : {0u,0x80000000u,0xc0000000u}) {
            check(mirrors.pointer(view+33,4)==bank1.data()+33);
            check(mirrors.pointer(view+0x10000021,4)==bank2.data()+33);
        }
        check(!mirrors.pointer(0x40000021,1) && !mirrors.pointer(0xcc008000,4));
        check(!mirrors.pointer(0x800000ff,2) && !mirrors.pointer(UINT32_MAX,2));
        rejects([] { wiicompiled::Disc d(0); });
        wiicompiled::Disc disc(2);rejects([&] { disc.open("/no-such-iso"); });
        rejects([&] { disc.read(0,nullptr,1); });
        Fixture fixture;disc.open(fixture.path);
        check(disc.isoSize()>0x80000000ull && disc.gameId()=="RMCP01");
        std::array<uint8_t,32> header{};disc.readRaw(0,header.data(),header.size());
        check(std::memcmp(header.data(),"RMCP01",6)==0);
        rejects([&] { disc.readRaw(0,nullptr,1); });
        rejects([&] { disc.readRaw(UINT64_MAX,header.data(),1); });
        disc.readRaw(disc.isoSize(),nullptr,0);
        check(disc.find("DIR\\FILE.BIN") && disc.find("/dir")->directory);
        auto bytes=disc.file("/dir/file.bin");
        check(bytes.size()==0x8000 && std::memcmp(bytes.data(),fixture.logical.data()+0x7bf0,bytes.size())==0);
        rejects([&] { disc.file("/dir/file.bin",1); });
        rejects([&] { disc.readFile("/dir/file.bin",0x7fff,bytes.data(),2); });
        rejects([&] { disc.read(UINT64_MAX,bytes.data(),1); });
        rejects([&] { disc.read(disc.partitionSize()-1,bytes.data(),2); });
        rejects([&] { disc.file("/dir"); });
        const auto first=disc.stats();
        disc.readFile("/dir/file.bin",0x40,bytes.data(),256);
        check(disc.stats().cacheHits>first.cacheHits);
        std::exception_ptr error[2];std::thread threads[2];
        for(size_t t=0;t<2;++t) threads[t]=std::thread([&,t] {
            try { std::array<uint8_t,127> buffer{};
                for(size_t i=0;i<300;++i) {
                    const size_t offset=(i*113+t*7000)%0x7f00;
                    disc.readFile("/dir/file.bin",offset,buffer.data(),buffer.size());
                    check(std::memcmp(buffer.data(),fixture.logical.data()+0x7bf0+offset,buffer.size())==0);
                }
            } catch (...) { error[t]=std::current_exception(); }
        });
        for(auto& thread:threads) thread.join(); for(auto& e:error) if(e) std::rethrow_exception(e);
        FILE* corrupt=std::fopen(fixture.path.c_str(),"r+b");check(corrupt);
        check(fseeko(corrupt,Fixture::data+0x400,SEEK_SET)==0);std::fputc(0xff,corrupt);std::fclose(corrupt);
        rejects([&] { disc.open(fixture.path); });
        rejects([&] { disc.file("/dir/file.bin"); });
        std::cout << "disc contracts: encrypted reads, H0, FST, aliases, bounds, cache, concurrent reads, 64-bit seek PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
