#include "boot_identity.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

using namespace wiicompiled::vita;
static void check(bool ok) {if(!ok)std::abort();}
int main() {
    const auto expected=HashBootBytes("abc");
    check(expected.available&&expected.bytes==3&&std::string(expected.sha256.data())==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    char name[]="/tmp/wic-boot-identity-XXXXXX";
    const int descriptor=mkstemp(name);check(descriptor>=0);
    FILE* file=fdopen(descriptor,"wb");check(file);
    check(std::fwrite("abc",1,3,file)==3&&std::fclose(file)==0);
    const auto read=ReadBootFileIdentity(name);
    check(read.available&&read.bytes==3&&read.sha256==expected.sha256);
    const std::string large(10000,'x');file=std::fopen(name,"wb");check(file);
    check(std::fwrite(large.data(),1,large.size(),file)==large.size()&&std::fclose(file)==0);
    const auto changed=ReadBootFileIdentity(name);
    check(changed.available&&changed.bytes==large.size()&&changed.sha256==HashBootBytes(large).sha256);
    check(changed.sha256!=read.sha256);
    check(std::remove(name)==0&&!ReadBootFileIdentity(name).available);
    check(!ReadBootFileIdentity(nullptr).available);
    const auto empty=HashBootBytes("");
    check(empty.available&&std::string(empty.sha256.data())==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
