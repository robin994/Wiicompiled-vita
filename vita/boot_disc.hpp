#pragma once
#include "wiicompiled/disc.hpp"
#include "psarc_backend.hpp"
#include <openssl/sha.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace wiicompiled {
struct BootExecutableExpected { const char* path; const char* hash; };
inline constexpr BootExecutableExpected kBootExecutables[]{
    {"/sys/main.dol","80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05"},
    {"/rel/StaticR.rel","16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d"}
};

inline void verifyBootExecutableBytes(const char* path, const std::vector<uint8_t>& bytes,
                                      const char* expectedHash) {
    unsigned char hash[SHA256_DIGEST_LENGTH];SHA256(bytes.data(),bytes.size(),hash);
    char hex[SHA256_DIGEST_LENGTH*2+1]{};
    for(size_t i=0;i<SHA256_DIGEST_LENGTH;++i)std::snprintf(hex+i*2,3,"%02x",hash[i]);
    if(std::string(hex)!=expectedHash)
        throw std::runtime_error(std::string("Executable differs from translated RMCP01 rev0: ")+path);
}

inline void verifyBootDisc(Disc& disc) {
    for(const auto& expected:kBootExecutables) {
        const auto bytes=disc.file(expected.path);
        verifyBootExecutableBytes(expected.path, bytes, expected.hash);
    }
}

inline void verifyBootPsarc() {
    for(const auto& expected:kBootExecutables) {
        std::string archivePath="disc";
        archivePath+=expected.path;
        std::vector<uint8_t> bytes;
        if(!vita::psarc::ReadWhole(archivePath,bytes,32*1024*1024))
            throw std::runtime_error(std::string("PSARC boot file unavailable: ")+expected.path);
        verifyBootExecutableBytes(expected.path,bytes,expected.hash);
    }
}
} // namespace wiicompiled
