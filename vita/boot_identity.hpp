#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace wiicompiled::vita {
struct BootFileIdentity {
    std::array<char,65> sha256{};
    uint64_t bytes=0;
    bool available=false;
};
BootFileIdentity HashBootBytes(std::string_view bytes) noexcept;
BootFileIdentity ReadBootFileIdentity(const char* path) noexcept;
}
