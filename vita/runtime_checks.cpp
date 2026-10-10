#include "runtime_checks.hpp"
#include "host_context.h"
#include "native_stack_call.h"
#include <psp2/kernel/threadmgr.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace wiicompiled::vita {
namespace {
thread_local std::uint32_t probeValue = 0;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool OnNativeStack()
{
    SceKernelThreadInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) < 0) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(&info);
    const auto begin = reinterpret_cast<std::uintptr_t>(info.stack);
    return address >= begin && address < begin + info.stackSize;
}

struct ThreadProbe {
    SceUID thread = -1;
    SceUID callbackThread = -1;
    const void* tlsAddress = nullptr;
    std::uint32_t initialValue = 0xffffffff;
    bool nativeStack = false;
};

void ProbeThread(ThreadProbe& result)
{
    result.thread = sceKernelGetThreadId();
    result.tlsAddress = &probeValue;
    result.initialValue = probeValue;
    probeValue = 0x13579bdf;
    HostContext::CallOnNativeStack([&] {
        result.callbackThread = sceKernelGetThreadId();
        result.nativeStack = OnNativeStack();
    });
}

int KernelProbe(SceSize size, void* arguments)
{
    if (size != sizeof(ThreadProbe*)) return -1;
    auto* result = *static_cast<ThreadProbe**>(arguments);
    ProbeThread(*result);
    return 0;
}

struct FiberProbe {
    HostContext::Handle root = nullptr;
    HostContext::Handle first = nullptr;
    HostContext::Handle second = nullptr;
    std::array<char, 8> trace{};
    unsigned count = 0;
    bool nativeReturns = true;
    bool tlsKept = true;
    bool floatRegistersKept = true;
    void Mark(char value) { if (count < trace.size()) trace[count++] = value; }
};

void FirstFiber(void* argument)
{
    auto& state = *static_cast<FiberProbe*>(argument);
    // Keep real AAPCS callee-saved VFP registers live across both native
    // returns and the peer switch. The peer overwrites each register.
    register double fp8 asm("d8") = 8.125, fp9 asm("d9") = 9.125;
    register double fp10 asm("d10") = 10.125, fp11 asm("d11") = 11.125;
    register double fp12 asm("d12") = 12.125, fp13 asm("d13") = 13.125;
    register double fp14 asm("d14") = 14.125, fp15 asm("d15") = 15.125;
    asm volatile("" : "+w"(fp8), "+w"(fp9), "+w"(fp10), "+w"(fp11),
                      "+w"(fp12), "+w"(fp13), "+w"(fp14), "+w"(fp15));
    state.Mark('A');
    HostContext::CallOnNativeStack([&] {
        state.nativeReturns &= OnNativeStack() && HostContext::IsCurrent(state.root);
        state.Mark('N');
    });
    HostContext::Switch(state.second);
    asm volatile("" : "+w"(fp8), "+w"(fp9), "+w"(fp10), "+w"(fp11),
                      "+w"(fp12), "+w"(fp13), "+w"(fp14), "+w"(fp15));
    state.floatRegistersKept = fp8 == 8.125 && fp9 == 9.125 && fp10 == 10.125 &&
        fp11 == 11.125 && fp12 == 12.125 && fp13 == 13.125 && fp14 == 14.125 && fp15 == 15.125;
    state.tlsKept &= probeValue == 0x2468ace0;
    state.Mark('a');
    HostContext::Switch(state.root);
    std::abort();
}

void SecondFiber(void* argument)
{
    auto& state = *static_cast<FiberProbe*>(argument);
    state.Mark('B');
    HostContext::CallOnNativeStack([&] {
        state.nativeReturns &= OnNativeStack() && HostContext::IsCurrent(state.root);
        state.Mark('M');
    });
    state.Mark('b');
    register double fp8 asm("d8") = 80.5, fp9 asm("d9") = 90.5;
    register double fp10 asm("d10") = 100.5, fp11 asm("d11") = 110.5;
    register double fp12 asm("d12") = 120.5, fp13 asm("d13") = 130.5;
    register double fp14 asm("d14") = 140.5, fp15 asm("d15") = 150.5;
    asm volatile("" : "+w"(fp8), "+w"(fp9), "+w"(fp10), "+w"(fp11),
                      "+w"(fp12), "+w"(fp13), "+w"(fp14), "+w"(fp15));
    HostContext::Switch(state.first);
    std::abort();
}
}

void CheckRuntimeContracts()
{
    Require(OnNativeStack(), "runtime check started outside the native stack");
    std::fprintf(stderr, "[runtime-check] start backend=scefiber pthread=linked\n");
    FiberProbe state;
    Require(HostContext::InitializeScheduler(&state.root), "runtime check could not load SceFiber");
    probeValue = 0x2468ace0;
    const SceUID mainThread = sceKernelGetThreadId();
    auto validate = [&](const ThreadProbe& result) {
        Require(result.thread != mainThread && result.initialValue == 0 &&
                result.tlsAddress != &probeValue && probeValue == 0x2468ace0,
                "runtime check failed: native threads share TLS");
        Require(result.callbackThread == result.thread && result.nativeStack,
                "runtime check failed: native worker used another thread's scheduler");
    };

    ThreadProbe standard;
    std::thread worker([&] { ProbeThread(standard); });
    worker.join();
    validate(standard);
    std::fprintf(stderr, "[runtime-check] std-thread=ok tls=isolated dispatch=local\n");

    ThreadProbe kernel;
    const SceUID id = sceKernelCreateThread("wic_tls_probe", KernelProbe,
        0x10000100, 64 * 1024, 0, 0, nullptr);
    Require(id >= 0, "runtime check could not create kernel thread");
    auto* argument = &kernel;
    const int started = sceKernelStartThread(id, sizeof(argument), &argument);
    if (started < 0) {
        sceKernelDeleteThread(id);
        throw std::runtime_error("runtime check could not start kernel thread");
    }
    int status = -1;
    const int waited = sceKernelWaitThreadEnd(id, &status, nullptr);
    if (waited < 0) throw std::runtime_error("runtime check could not join kernel thread");
    sceKernelDeleteThread(id);
    Require(status == 0, "runtime check kernel thread failed");
    validate(kernel);
    std::fprintf(stderr, "[runtime-check] kernel-thread=ok tls=isolated dispatch=local\n");

    state.first = HostContext::Create(128 * 1024, FirstFiber, &state);
    state.second = HostContext::Create(128 * 1024, SecondFiber, &state);
    Require(state.first && state.second, "runtime check could not create SceFiber contexts");
    HostContext::Switch(state.first);
    Require(state.count == 6 && state.trace == std::array<char, 8>{'A','N','B','M','b','a',0,0},
            "runtime check failed: direct peer switch or requester continuation");
    Require(state.nativeReturns && state.tlsKept, "runtime check failed: SceFiber native return/TLS");
    Require(state.floatRegistersKept, "runtime check failed: SceFiber lost d8-d15");
    HostContext::Destroy(state.first);
    HostContext::Destroy(state.second);
    HostContext::ShutdownScheduler(state.root);
    std::fprintf(stderr, "[runtime-check] scefiber=ok peer-switch=ok native-return=ok requester=ok d8-d15=ok\n");
}
}
