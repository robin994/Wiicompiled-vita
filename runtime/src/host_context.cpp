#include "host_context.h"
#include "native_call_profile.h"
#include <chrono>
#include <exception>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(MKW_TARGET_VITA) && defined(__vita__)
#include <psp2/fiber.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#elif defined(__APPLE__) && defined(__aarch64__)
#include <sys/mman.h>
#include <unistd.h>

extern "C" void mkw_co_switch(void** targetSp, void** sourceSp);
extern "C" void* mkw_co_init(void* stackTop, void (*entry)(void*), void* argument);
#elif defined(MKW_TARGET_VITA) || defined(__linux__) || (defined(__APPLE__) && defined(__x86_64__))
#include <libco.h>

#include <cstdlib>
#include <unordered_map>
#else
#error "HostContext needs a supported cooperative-context backend"
#endif

namespace HostContext {

namespace {
struct NativeCall {
    Entry entry;
    void* argument;
    Handle caller;
    std::exception_ptr failure;
};
thread_local Handle g_nativeScheduler = nullptr;
thread_local NativeCall* g_nativeCall = nullptr;
struct ProfileState {
    NativeCallProfile counters{};
    unsigned requestDepth = 0, callbackDepth = 0;
    uint64_t requestStartNs = 0, callbackStartNs = 0;
    uint64_t requestNs = 0, callbackNs = 0;
};
thread_local ProfileState g_profile{};

uint64_t ProfileNowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

class ProfileScope {
public:
    explicit ProfileScope(bool callback) : callback_(callback), active_(g_profile.counters.enabled) {
        if (!active_) return;
        auto& depth = callback_ ? g_profile.callbackDepth : g_profile.requestDepth;
        auto& start = callback_ ? g_profile.callbackStartNs : g_profile.requestStartNs;
        if (depth++ == 0) start = ProfileNowNs();
        if (!callback_) ++g_profile.counters.requests;
    }
    ~ProfileScope() {
        if (!active_) return;
        auto& depth = callback_ ? g_profile.callbackDepth : g_profile.requestDepth;
        const auto start = callback_ ? g_profile.callbackStartNs : g_profile.requestStartNs;
        auto& total = callback_ ? g_profile.callbackNs : g_profile.requestNs;
        if (--depth == 0) total += ProfileNowNs() - start;
    }
private:
    bool callback_, active_;
};
}

void SetNativeCallProfiling(bool enabled) {
    if (g_profile.requestDepth || g_profile.callbackDepth) return;
    g_profile = {};
    g_profile.counters.enabled = enabled;
}

bool NativeCallProfilingEnabled() { return g_profile.counters.enabled; }

NativeCallProfile SnapshotNativeCallProfile() {
    auto result = g_profile.counters;
    if (!result.enabled) return result;
    const auto now = ProfileNowNs();
    result.requestUs = (g_profile.requestNs +
        (g_profile.requestDepth ? now - g_profile.requestStartNs : 0)) / 1000;
    result.callbackUs = (g_profile.callbackNs +
        (g_profile.callbackDepth ? now - g_profile.callbackStartNs : 0)) / 1000;
    return result;
}

void RecordNativeWait(unsigned site, uint64_t elapsedUs) {
    if (!g_profile.counters.enabled || site >= g_profile.counters.waitUs.size()) return;
    ++g_profile.counters.waitCalls[site];
    g_profile.counters.waitUs[site] += elapsedUs;
}

#if defined(_WIN32)

namespace {
thread_local bool g_convertedScheduler = false;
}

bool InitializeScheduler(Handle* scheduler)
{
    void* context = ConvertThreadToFiber(nullptr);
    g_convertedScheduler = context != nullptr;
    if (!context) {
        context = GetCurrentFiber();
    }
    *scheduler = context;
    g_nativeScheduler = context;
    return context != nullptr;
}

void ShutdownScheduler(Handle scheduler)
{
    if (g_nativeScheduler == scheduler) g_nativeScheduler = nullptr;
    if (scheduler && g_convertedScheduler) {
        ConvertFiberToThread();
    }
    g_convertedScheduler = false;
}

Handle Create(std::size_t stackSize, Entry entry, void* argument)
{
    return CreateFiber(stackSize, entry, argument);
}

void Destroy(Handle context)
{
    if (context) {
        DeleteFiber(context);
    }
}

bool IsCurrent(Handle context)
{
    return context != nullptr && GetCurrentFiber() == context;
}

static void SwitchRaw(Handle target)
{
    SwitchToFiber(target);
}

#elif defined(MKW_TARGET_VITA) && defined(__vita__)

namespace {
struct Context {
    SceFiber fiber{};
    void* stack = nullptr;
    Entry entry = nullptr;
    void* argument = nullptr;
    SceUID owner = -1;
    bool scheduler = false;
};
thread_local Context* g_current = nullptr;

void ContextEntry(SceUInt32 initialized, SceUInt32)
{
    auto* context = reinterpret_cast<Context*>(static_cast<std::uintptr_t>(initialized));
    g_current = context;
    context->entry(context->argument);
    // FiberProc must hand control back to its scheduler, rather than return.
    std::abort();
}

void CheckFiberResult(const char* operation, int result)
{
    if (result >= 0) return;
    char message[128]{};
    std::snprintf(message, sizeof(message), "%s failed: 0x%08x", operation,
                  static_cast<unsigned>(result));
    throw std::runtime_error(message);
}
}

bool InitializeScheduler(Handle* scheduler)
{
    if (!scheduler || g_nativeScheduler) return false;
    if (sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER) < 0 &&
        sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER) < 0) return false;
    auto* context = new Context();
    context->scheduler = true;
    context->owner = sceKernelGetThreadId();
    g_current = context;
    g_nativeScheduler = context;
    *scheduler = context;
    return true;
}

void ShutdownScheduler(Handle scheduler)
{
    auto* context = static_cast<Context*>(scheduler);
    if (!context) return;
    if (g_current != context || !context->scheduler)
        throw std::runtime_error("SceFiber scheduler shutdown outside its native thread");
    g_nativeScheduler = nullptr;
    g_nativeCall = nullptr;
    g_current = nullptr;
    delete context;
}

Handle Create(std::size_t stackSize, Entry entry, void* argument)
{
    if (!g_nativeScheduler || !entry || stackSize < 4096 ||
        stackSize > std::numeric_limits<SceSize>::max() - 7) return nullptr;
    const auto alignedSize = static_cast<SceSize>((stackSize + 7) & ~std::size_t(7));
    auto* context = new Context();
    context->entry = entry;
    context->argument = argument;
    context->owner = sceKernelGetThreadId();
    context->stack = std::malloc(alignedSize);
    if (!context->stack) {
        delete context;
        return nullptr;
    }
    char name[] = "wic_guest";
    const int result = _sceFiberInitializeImpl(&context->fiber, name, ContextEntry,
        static_cast<SceUInt32>(reinterpret_cast<std::uintptr_t>(context)),
        context->stack, alignedSize, nullptr);
    if (result < 0) {
        std::fprintf(stderr, "[context] SceFiber initialize failed=0x%08x bytes=%u\n",
                     static_cast<unsigned>(result), static_cast<unsigned>(alignedSize));
        std::free(context->stack);
        delete context;
        return nullptr;
    }
    return context;
}

void Destroy(Handle handle)
{
    auto* context = static_cast<Context*>(handle);
    if (!context) return;
    if (context->scheduler || context == g_current || context->owner != sceKernelGetThreadId())
        throw std::runtime_error("Cannot destroy an active or foreign SceFiber context");
    CheckFiberResult("sceFiberFinalize", sceFiberFinalize(&context->fiber));
    std::free(context->stack);
    delete context;
}

bool IsCurrent(Handle context)
{
    return context != nullptr && context == g_current;
}

static void SwitchRaw(Handle target)
{
    auto* destination = static_cast<Context*>(target);
    Context* source = g_current;
    if (!destination || destination == source) return;
    if (!source || source->owner != sceKernelGetThreadId() || destination->owner != source->owner)
        throw std::runtime_error("SceFiber context switch across native threads");
    // Capture the source before publishing the destination: Run is only valid
    // from the native thread, Switch from another fiber, and ReturnToThread
    // resumes the native dispatcher. Direct guest-to-guest switching is kept.
    g_current = destination;
    const int result = destination->scheduler ? sceFiberReturnToThread(0, nullptr) :
        source->scheduler ? sceFiberRun(&destination->fiber, 0, nullptr) :
                            sceFiberSwitch(&destination->fiber, 0, nullptr);
    g_current = source;
    CheckFiberResult("SceFiber context switch", result);
}

#elif defined(__APPLE__) && defined(__aarch64__)

namespace {
struct Context {
    void* savedStackPointer = nullptr;
    void* stack = nullptr;
    std::size_t stackSize = 0;
};

// Guest scheduling is confined to the initialized main host thread. Keeping
// this as ordinary process state also avoids relying on Darwin TLS internals
// while executing on a manually managed stack.
Context* g_current = nullptr;
}

bool InitializeScheduler(Handle* scheduler)
{
    auto* context = new Context();
    g_current = context;
    *scheduler = context;
    g_nativeScheduler = context;
    return true;
}

void ShutdownScheduler(Handle scheduler)
{
    if (g_nativeScheduler == scheduler) g_nativeScheduler = nullptr;
    auto* context = static_cast<Context*>(scheduler);
    if (g_current == context) {
        g_current = nullptr;
    }
    delete context;
}

Handle Create(std::size_t stackSize, Entry entry, void* argument)
{
    auto* context = new Context();
    const std::size_t guardSize = static_cast<std::size_t>(getpagesize());
    const std::size_t totalSize = stackSize + guardSize;
    context->stack = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE,
                          MAP_ANON | MAP_PRIVATE, -1, 0);
    if (context->stack == MAP_FAILED) {
        delete context;
        return nullptr;
    }
    // Fault on stack overflow instead of corrupting the preceding mapping.
    if (mprotect(context->stack, guardSize, PROT_NONE) != 0) {
        munmap(context->stack, totalSize);
        delete context;
        return nullptr;
    }
    context->stackSize = totalSize;

    auto* stackTop = static_cast<char*>(context->stack) + totalSize;
    context->savedStackPointer = mkw_co_init(stackTop, entry, argument);
    return context;
}

void Destroy(Handle context)
{
    auto* nativeContext = static_cast<Context*>(context);
    if (!nativeContext) {
        return;
    }
    if (nativeContext->stack) {
        munmap(nativeContext->stack, nativeContext->stackSize);
    }
    delete nativeContext;
}

bool IsCurrent(Handle context)
{
    return context != nullptr && context == g_current;
}

static void SwitchRaw(Handle target)
{
    auto* destination = static_cast<Context*>(target);
    Context* source = g_current;
    if (!destination || destination == source) {
        return;
    }

    g_current = destination;
    mkw_co_switch(&destination->savedStackPointer, &source->savedStackPointer);
    g_current = source;
}

#elif defined(MKW_TARGET_VITA) || defined(__linux__) || (defined(__APPLE__) && defined(__x86_64__))

namespace {
struct Context {
    cothread_t native = nullptr;
    Entry entry = nullptr;
    void* argument = nullptr;
    bool ownsNative = false;
};

thread_local Context* g_current = nullptr;
thread_local std::unordered_map<cothread_t, Context*> g_contexts;

void ContextEntry()
{
    const auto found = g_contexts.find(co_active());
    if (found == g_contexts.end() || !found->second || !found->second->entry) {
        std::abort();
    }

    Context* context = found->second;
    g_current = context;
    context->entry(context->argument);

    // A guest fiber must return through FiberProc's scheduler handoff. There
    // is no valid native caller to return to from libco's entry trampoline.
    std::abort();
}
} // namespace

bool InitializeScheduler(Handle* scheduler)
{
    auto* context = new Context();
    context->native = co_active();
    if (!context->native) {
        delete context;
        return false;
    }

    g_current = context;
    g_contexts.emplace(context->native, context);
    *scheduler = context;
    g_nativeScheduler = context;
    return true;
}

void ShutdownScheduler(Handle scheduler)
{
    if (g_nativeScheduler == scheduler) g_nativeScheduler = nullptr;
    auto* context = static_cast<Context*>(scheduler);
    if (!context) {
        return;
    }

    g_contexts.erase(context->native);
    if (g_current == context) {
        g_current = nullptr;
    }
    delete context;
}

Handle Create(std::size_t stackSize, Entry entry, void* argument)
{
    auto* context = new Context();
    context->entry = entry;
    context->argument = argument;
    context->native = co_create(static_cast<unsigned int>(stackSize), ContextEntry);
    context->ownsNative = context->native != nullptr;
    if (!context->native) {
        delete context;
        return nullptr;
    }

    g_contexts.emplace(context->native, context);
    return context;
}

void Destroy(Handle context)
{
    auto* nativeContext = static_cast<Context*>(context);
    if (!nativeContext) {
        return;
    }

    g_contexts.erase(nativeContext->native);
    if (nativeContext->ownsNative) {
        co_delete(nativeContext->native);
    }
    delete nativeContext;
}

bool IsCurrent(Handle context)
{
    return context != nullptr && context == g_current;
}

static void SwitchRaw(Handle target)
{
    auto* destination = static_cast<Context*>(target);
    Context* source = g_current;
    if (!destination || destination == source) {
        return;
    }

    g_current = destination;
    co_switch(destination->native);
    g_current = source;
}

#endif

void Switch(Handle target)
{
    if (!g_nativeScheduler || !IsCurrent(g_nativeScheduler)) {
        SwitchRaw(target);
        return;
    }

    // A native request can arrive from any coroutine, including one reached
    // through another guest's direct context switch. Resume the requester, not
    // necessarily the coroutine this scheduler originally switched to.
    Handle next = target;
    for (;;) {
        SwitchRaw(next);
        NativeCall* call = g_nativeCall;
        if (!call) return; // Ordinary return to the guest main/scheduler context.
        g_nativeCall = nullptr;
        try {
            ProfileScope callbackProfile(true);
            call->entry(call->argument);
        } catch (...) {
            call->failure = std::current_exception();
        }
        next = call->caller;
    }
}

void RunOnNativeStack(Entry entry, void* argument)
{
    if (!entry) return;
    ProfileScope requestProfile(false);
    if (!g_nativeScheduler || IsCurrent(g_nativeScheduler)) {
        ProfileScope callbackProfile(true);
        entry(argument);
        return;
    }
#if defined(_WIN32)
    Handle caller = GetCurrentFiber();
#else
    Handle caller = g_current;
#endif
    NativeCall call{entry, argument, caller, {}};
    if (g_profile.counters.enabled) ++g_profile.counters.stackDispatches;
    // All contexts belong to one OS thread, and the scheduler clears this
    // pointer before executing a callback. Its arguments stay alive until return.
    g_nativeCall = &call;
    SwitchRaw(g_nativeScheduler);
    if (call.failure) std::rethrow_exception(call.failure);
}

} // namespace HostContext
