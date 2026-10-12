#include "boot_identity.hpp"
#include <openssl/sha.h>
#include <cstdio>

namespace wiicompiled::vita {
namespace {
void encode(BootFileIdentity& result,const unsigned char* digest) noexcept {
    constexpr char digits[]="0123456789abcdef";
    for(size_t i=0;i<SHA256_DIGEST_LENGTH;++i) {
        result.sha256[2*i]=digits[digest[i]>>4];
        result.sha256[2*i+1]=digits[digest[i]&15];
    }
    result.available=true;
}
}
BootFileIdentity HashBootBytes(std::string_view bytes) noexcept {
    BootFileIdentity result{};
    unsigned char digest[SHA256_DIGEST_LENGTH]{};
    if(SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest)) {
        result.bytes=bytes.size();encode(result,digest);
    }
    return result;
}
BootFileIdentity ReadBootFileIdentity(const char* path) noexcept {
    BootFileIdentity result{};
    FILE* file=path?std::fopen(path,"rb"):nullptr;
    if(!file)return result;
    SHA256_CTX context{};
    bool ok=SHA256_Init(&context)==1;
    std::array<unsigned char,4096> block{};
    while(ok) {
        const size_t count=std::fread(block.data(),1,block.size(),file);
        if(count) {result.bytes+=count;ok=SHA256_Update(&context,block.data(),count)==1;}
        if(count<block.size()) {ok=ok&&!std::ferror(file);break;}
    }
    ok=std::fclose(file)==0&&ok;
    unsigned char digest[SHA256_DIGEST_LENGTH]{};
    if(ok&&SHA256_Final(digest,&context)==1)encode(result,digest);
    return result;
}
}
