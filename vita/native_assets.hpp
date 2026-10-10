#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace wiicompiled::vita::assets {
// Initialize after mounting the PSARC and before entering the guest. A missing
// catalogue keeps the original asset path available for the control build.
bool Initialize(size_t cacheBudget=2*1024*1024) noexcept;
bool Read(const char* path, std::vector<uint8_t>& bytes, void* context) noexcept;
void Shutdown() noexcept;
}
