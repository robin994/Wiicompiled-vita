#pragma once

#include <array>
#include <cstdint>

namespace HostContext {

// Diagnostic wall time on the calling OS thread. Callback time is contained
// in request time; waits and FIFO work are contained in callback time. Nested
// requests/callbacks count calls but contribute elapsed time only once.
struct NativeCallProfile {
    bool enabled = false;
    uint64_t requests = 0;
    uint64_t stackDispatches = 0;
    uint64_t requestUs = 0;
    uint64_t callbackUs = 0;
    std::array<uint64_t, 4> waitCalls{};
    std::array<uint64_t, 4> waitUs{};
};

// Enable/reset only before entering native-stack calls. Other OS threads keep
// their default OFF setting. Snapshot includes elapsed time in active scopes.
void SetNativeCallProfiling(bool enabled);
bool NativeCallProfilingEnabled();
NativeCallProfile SnapshotNativeCallProfile();
void RecordNativeWait(unsigned site, uint64_t elapsedUs);

} // namespace HostContext
