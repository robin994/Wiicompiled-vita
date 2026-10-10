#include "host_context.h"
#include "native_stack_call.h"
#include "native_call_profile.h"

#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>

namespace {
// The worker yields twice; each return to the scheduler must preserve both
// context identities and the worker's continuation point.
HostContext::Handle g_scheduler = nullptr;
HostContext::Handle g_worker = nullptr;
HostContext::Handle g_peer = nullptr;
int g_steps = 0;
int g_peerSteps = 0;
int g_nativeCalls = 0;
int g_nestedCalls = 0;
uintptr_t g_nativeAnchor = 0;
std::vector<unsigned char> g_transferBuffer;
int g_nativeLifetimes = 0;

struct NativeLifetime {
    ~NativeLifetime() {
        if (!HostContext::IsCurrent(g_scheduler)) std::abort();
        ++g_nativeLifetimes;
    }
};

void NestedNative(void*)
{
    if (!HostContext::IsCurrent(g_scheduler)) std::abort();
    ++g_nestedCalls;
}

void NativeProbe(void* argument)
{
    int marker = 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(&marker);
    const uintptr_t distance = address > g_nativeAnchor ? address - g_nativeAnchor : g_nativeAnchor - address;
    if (!HostContext::IsCurrent(g_scheduler) || distance > 64 * 1024) std::abort();
    ++g_nativeCalls;
    HostContext::RunOnNativeStack(NestedNative, nullptr);
    if (argument) *static_cast<int*>(argument) = 42;
}

void NativeThrows(void*)
{
    if (!HostContext::IsCurrent(g_scheduler)) std::abort();
    throw std::runtime_error("native callback failure");
}

void Peer(void*)
{
    int result = 0;
    HostContext::RunOnNativeStack(NativeProbe, &result);
    if (result != 42 || !HostContext::IsCurrent(g_peer)) std::abort();
    ++g_peerSteps;
    HostContext::Switch(g_worker);
    HostContext::RunOnNativeStack(NativeProbe, nullptr);
    ++g_peerSteps;
    HostContext::Switch(g_scheduler);
}

void Worker(void*)
{
    if (!HostContext::IsCurrent(g_worker)) {
        std::abort();
    }
    int result = 0;
    HostContext::RunOnNativeStack(NativeProbe, &result);
    if (result != 42 || !HostContext::IsCurrent(g_worker)) std::abort();
    // Match the size of the DVD buffer in the second Vita crash. Allocation,
    // zero-fill, copy and local destruction must finish before worker resumes.
    const auto transferred = HostContext::CallOnNativeStack([] {
        if (!HostContext::IsCurrent(g_scheduler)) std::abort();
        NativeLifetime lifetime;
        std::vector<unsigned char> staged(g_transferBuffer.size());
        staged.front() = 17;
        staged[staged.size() / 2] = 29;
        staged.back() = 41;
        std::memcpy(g_transferBuffer.data(), staged.data(), staged.size());
        return staged.size();
    });
    if (transferred != g_transferBuffer.size() || g_nativeLifetimes != 1 ||
        !HostContext::IsCurrent(g_worker)) std::abort();
    for (size_t i = 0; i < g_transferBuffer.size(); ++i) {
        const unsigned expected = i == 0 ? 17 : i == transferred / 2 ? 29 : i + 1 == transferred ? 41 : 0;
        if (g_transferBuffer[i] != expected) std::abort();
    }
    HostContext::Switch(g_peer);
    if (!HostContext::IsCurrent(g_worker)) std::abort();
    ++g_steps;
    HostContext::Switch(g_scheduler);
    if (!HostContext::IsCurrent(g_worker)) {
        std::abort();
    }
    bool caught = false;
    try {
        HostContext::CallOnNativeStack([]() -> bool {
            NativeLifetime lifetime;
            NativeThrows(nullptr);
            return false;
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    if (!caught || g_nativeLifetimes != 2 || !HostContext::IsCurrent(g_worker)) std::abort();
    ++g_steps;
    HostContext::Switch(g_scheduler);
}
} // namespace

int main()
{
    if (HostContext::SnapshotNativeCallProfile().enabled) return 1;
    HostContext::SetNativeCallProfiling(true);
    int nativeMarker = 0;
    g_nativeAnchor = reinterpret_cast<uintptr_t>(&nativeMarker);
    // Initialization is optional when called from an ordinary host thread.
    HostContext::RunOnNativeStack([](void* p) { *static_cast<int*>(p) = 7; }, &nativeMarker);
    if (nativeMarker != 7) return 1;
    g_transferBuffer.resize(0x4ad3c4);
    if (!HostContext::InitializeScheduler(&g_scheduler) ||
        !HostContext::IsCurrent(g_scheduler)) {
        return 1;
    }
    HostContext::RunOnNativeStack(NativeProbe, nullptr);
    g_worker = HostContext::Create(64 * 1024, Worker, nullptr);
    g_peer = HostContext::Create(64 * 1024, Peer, nullptr);
    if (!g_worker || !g_peer) {
        return 1;
    }

    HostContext::Switch(g_worker);
    if (g_steps != 1 || !HostContext::IsCurrent(g_scheduler)) {
        return 1;
    }
    HostContext::Switch(g_worker);
    if (g_steps != 2 || !HostContext::IsCurrent(g_scheduler)) {
        return 1;
    }

    HostContext::Switch(g_peer);
    if (g_peerSteps != 2 || g_nativeCalls != 4 || g_nestedCalls != 4 ||
        !HostContext::IsCurrent(g_scheduler)) return 1;

    HostContext::Destroy(g_worker);
    HostContext::Destroy(g_peer);
    HostContext::ShutdownScheduler(g_scheduler);

    // Calls from two guest contexts, direct calls, nested callbacks and the
    // throwing callback must all close their profiling scopes correctly.
    const auto profile = HostContext::SnapshotNativeCallProfile();
    if (!profile.enabled || profile.requests != 11 || profile.stackDispatches != 5 ||
        profile.requestUs < profile.callbackUs) return 1;
    HostContext::SetNativeCallProfiling(true);
    const auto started = std::chrono::steady_clock::now();
    HostContext::RunOnNativeStack([](void*) {
        const auto before = HostContext::SnapshotNativeCallProfile();
        HostContext::RunOnNativeStack([](void*) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }, nullptr);
        const auto after = HostContext::SnapshotNativeCallProfile();
        // Active snapshots include the parent; the child interval is counted
        // once even though both request/callback scopes remain open around it.
        if (after.requests != 2 || after.callbackUs < before.callbackUs + 4000 ||
            after.requestUs < after.callbackUs) std::abort();
    }, nullptr);
    const auto nested = HostContext::SnapshotNativeCallProfile();
    const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count());
    if (nested.requests != 2 || nested.stackDispatches || nested.callbackUs < 4000 ||
        nested.requestUs < nested.callbackUs || nested.callbackUs > elapsed) return 1;
    HostContext::SetNativeCallProfiling(false);
    HostContext::RunOnNativeStack([](void*) {}, nullptr);
    const auto disabled = HostContext::SnapshotNativeCallProfile();
    if (disabled.enabled || disabled.requests || disabled.callbackUs) return 1;
}
