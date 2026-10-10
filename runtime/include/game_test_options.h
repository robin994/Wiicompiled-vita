#pragma once
#include <cstdint>

struct CpuContext;
namespace RuntimeGameTestOptions {
// Installed at boot on Vita only. Generated static calls, native dispatch and
// indirect dispatch all pass through ApplyRuntimeCallOptions.
using CallHook = void (*)(uint32_t, CpuContext*);
inline CallHook callHook = nullptr;
inline void Apply(uint32_t target, CpuContext* cpu) {
    if (callHook) callHook(target, cpu);
}
}
