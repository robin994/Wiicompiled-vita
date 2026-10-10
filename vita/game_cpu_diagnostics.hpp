#pragma once
#include <cstdint>

// These counters belong to the game OS thread, including its cooperative
// fibers. Worker-thread math is excluded. They are diagnostic-only.
void WicEnableCpuDiagnostics();
void WicLogCpuDiagnostics(uint64_t frame);
