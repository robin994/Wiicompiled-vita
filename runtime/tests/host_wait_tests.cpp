#include "host_context.h"
#include "host_wait.h"
#include "native_call_profile.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#if defined(_WIN32)
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace {
using namespace std::chrono_literals;
HostContext::Handle scheduler = nullptr, worker = nullptr, peer = nullptr;
uintptr_t nativeAnchor = 0, workerAnchor = 0;
unsigned workerSteps = 0, peerSteps = 0;

void Check(bool condition) { if (!condition) std::abort(); }

void WaitUntil()
{
    const auto deadline = HostWait::Clock::now() + 2ms;
    HostWait::SleepUntil(deadline, HostWait::Site::VideoRetrace);
    Check(HostWait::Clock::now() >= deadline);
}

void Peer(void*)
{
    WaitUntil();
    Check(HostContext::IsCurrent(peer));
    ++peerSteps;
    HostContext::Switch(worker);
    std::abort();
}

void Worker(void*)
{
    int stackMarker = 0;
    workerAnchor = reinterpret_cast<uintptr_t>(&stackMarker);
    HostWait::SleepUntil(HostWait::Clock::now() - 1ms, HostWait::Site::VideoRetrace);
    HostWait::SleepFor(0ns, HostWait::Site::FrameRetry);
    HostWait::SleepFor(-1ms, HostWait::Site::FrameRetry);
    WaitUntil();
    for (const auto site : {HostWait::Site::FrameRetry, HostWait::Site::ControllerDelay,
                            HostWait::Site::FatalExit}) {
        const auto before = HostWait::Clock::now();
        HostWait::SleepFor(2ms, site);
        Check(HostWait::Clock::now() >= before + 2ms);
        Check(HostContext::IsCurrent(worker) && peerSteps == 0);
    }
    ++workerSteps;
    HostContext::Switch(peer);
    Check(HostContext::IsCurrent(worker) && peerSteps == 1);
    ++workerSteps;
    HostContext::Switch(scheduler);
    std::abort();
}

uintptr_t Distance(uintptr_t a, uintptr_t b) { return a > b ? a - b : b - a; }
} // namespace

int main()
{
    HostContext::SetNativeCallProfiling(true);
    int stackMarker = 0;
    nativeAnchor = reinterpret_cast<uintptr_t>(&stackMarker);
    FILE* captured = std::tmpfile();
    Check(captured != nullptr);
    std::fflush(stderr);
    const int savedStderr = dup(fileno(stderr));
    Check(savedStderr >= 0 && dup2(fileno(captured), fileno(stderr)) >= 0);

    // Ordinary threads and the scheduler use the same boundary directly.
    WaitUntil();
    Check(HostContext::InitializeScheduler(&scheduler));
    WaitUntil();
    worker = HostContext::Create(64 * 1024, Worker, nullptr);
    peer = HostContext::Create(64 * 1024, Peer, nullptr);
    Check(worker && peer);
    HostContext::Switch(worker);
    Check(HostContext::IsCurrent(scheduler) && workerSteps == 2 && peerSteps == 1);
    HostContext::Destroy(peer);
    HostContext::Destroy(worker);
    HostContext::ShutdownScheduler(scheduler);

    std::fflush(stderr);
    Check(dup2(savedStderr, fileno(stderr)) >= 0);
    close(savedStderr);
    std::rewind(captured);
    unsigned counts[4]{};
    const char* names[] = {"vi", "gx-retry", "gki", "panic"};
    char line[256];
    while (std::fgets(line, sizeof(line), captured)) {
        char site[32];
        unsigned count;
        void* address = nullptr;
        long long remaining;
        Check(std::sscanf(line, "[wait-native] site=%31s wait=%u stack=%p remaining_us=%lld",
                          site, &count, &address, &remaining) == 4);
        const auto stack = reinterpret_cast<uintptr_t>(address);
        // Observe the production sleep callback, rather than substituting a
        // fake sleeper that could hide an actual coroutine-stack syscall.
        Check(Distance(stack, nativeAnchor) < 64 * 1024);
        Check(Distance(stack, workerAnchor) > 64 * 1024);
        unsigned index = 0;
        while (index < 4 && std::strcmp(site, names[index]) != 0) ++index;
        Check(index < 4 && count == ++counts[index] && remaining >= 0);
    }
    std::fclose(captured);
    Check(counts[0] == 4 && counts[1] == 1 && counts[2] == 1 && counts[3] == 1);
    const auto profile = HostContext::SnapshotNativeCallProfile();
    uint64_t waitUs = 0;
    for (unsigned i = 0; i < 4; ++i) {
        Check(profile.waitCalls[i] == counts[i] && profile.waitUs[i] > 0);
        waitUs += profile.waitUs[i];
    }
    Check(profile.requests == 7 && profile.stackDispatches == 5);
    Check(profile.callbackUs >= waitUs && profile.requestUs >= profile.callbackUs);
    std::puts("PASS: real host waits use the native stack, preserve deadlines and resume the exact caller.");
}
