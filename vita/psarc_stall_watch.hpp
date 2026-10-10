#pragma once

#include <cstdint>

struct WicPsarcProgress {
    uint64_t submitted=0, completed=0;
    uint32_t state=0, activeOp=0; // 0 idle, 1 Mount, 2 Close, 3 Size, 4 Read, 5 Snapshot
};

extern "C" void WicReadPsarcProgress(WicPsarcProgress* out) noexcept;
