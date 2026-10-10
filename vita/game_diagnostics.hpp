#pragma once
#include <cstdint>

// Bounded diagnostic windows, independent of the 300-frame Aurora report.
void WicLogFrameDiagnostics(uint64_t frame, uint64_t draws, bool capture);
