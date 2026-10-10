#include "game_cpu_diagnostics.hpp"
#include "isa/ppc_isa_context.h"
#include "host_context.h"
#include "memory.h"
#include "gfx/vita_cpu_workers.hpp"
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

extern "C" double __wrap_fma(double, double, double);
void WicEnableHeapWatch() {} // Tested independently with guest heap memory.
extern "C" float __wrap_fmaf(float, float, float);
extern "C" void __wrap_func_8007A170(CpuContext*);
extern "C" void __wrap_func_8007AB90(CpuContext*);
extern "C" void __wrap_func_80199B98(CpuContext*);
namespace {
uint64_t timerCalls = 0;
bool native = false;
CpuContext expected{};
bool failLayoutAllocation = false;
unsigned paneCalls = 0, allocationCalls = 0;
void RequireAt(bool value, int line) {
    if (!value) { std::fprintf(stderr, "CPU diagnostic contract failed at line %d\n", line); std::abort(); }
}
#define Require(value) RequireAt((value), __LINE__)
__attribute__((noinline)) double RepeatedSite() {
    // Prevent the host optimizer's loop unrolling from creating many sites
    // with fewer than 128 calls each, which correctly receive no timing sample.
    volatile double result = __wrap_fma(1., 2., 3.);
    return result;
}
}
extern "C" uint64_t sceKernelGetProcessTimeWide() { return ++timerCalls; }
extern "C" int sceKernelGetCpuId() { return 0; }
extern "C" int sceKernelGetThreadId() { return 1; }
extern "C" int sceKernelGetThreadCpuAffinityMask(int) { return 0x10000; }
extern "C" double __real_fma(double a, double b, double c) { return std::fma(a, b, c); }
extern "C" float __real_fmaf(float a, float b, float c) { return std::fma(a, b, c); }
extern "C" void __real_func_8007A170(CpuContext* ctx) {
    Require(!native && std::memcmp(ctx, &expected, sizeof(expected)) == 0);
    ctx->lr = 0x8007a37c;
    __wrap_func_8007AB90(ctx);
    ctx->lr = 0x8007abf0; ctx->gpr[3] = 0x5000; ctx->gpr[4] = 216;
    __wrap_func_80199B98(ctx);
    *ctx = expected;
    ctx->gpr[3] = 1; // Build's only change in this test double.
}
extern "C" void __real_func_8007AB90(CpuContext* ctx) {
    Require(!native); ++paneCalls; ctx->gpr[3] = failLayoutAllocation ? 0 : 0x3000;
}
extern "C" void __real_func_80199B98(CpuContext* ctx) {
    Require(!native); ++allocationCalls; ctx->gpr[3] = failLayoutAllocation ? 0 : 0x4000;
}
void HostContext::RunOnNativeStack(Entry entry, void* argument) {
    Require(!native); native = true; entry(argument); native = false;
}
bool Memory::Contains(uint32_t, size_t) { Require(native); return true; }
uint8_t* Memory::GetPointer(uint32_t, size_t) { Require(native); return nullptr; }
namespace MemoryInline {
uintptr_t g_fullReadablePageBias[kPageCount]{};
uint32_t Read32Slow(uint32_t address) {
    Require(native);
    if (address == 0x2010) return failLayoutAllocation ? 0 : 0x3000; // Layout root pane.
    if (address == 0x3014 || address == 0x3018) return 0x3014; // Empty valid sentinel.
    return 0;
}
}
namespace aurora::vita::gfx {
CpuVertexParallelSnapshot cpu_vertex_parallel_snapshot() noexcept { return {}; }
uint32_t cpu_worker_threads() noexcept { return 2; }
uint32_t cpu_execution_lanes() noexcept { return 3; }
}
int main() {
    // Disabled profiling must not even read the timer.
    Require(__wrap_fma(1., 2., 3.) == 5. && timerCalls == 0);
    WicEnableCpuDiagnostics();
    const double inputs[] = {0., -0., 1., -1., 1e-300, 1e300,
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
    for (double a : inputs) for (double b : inputs) for (double c : inputs) {
        const auto reference = std::fma(a, b, c), actual = __wrap_fma(a, b, c);
        Require(std::bit_cast<uint64_t>(reference) == std::bit_cast<uint64_t>(actual));
        const float x = float(a), y = float(b), z = float(c);
        Require(std::bit_cast<uint32_t>(std::fma(x, y, z)) ==
            std::bit_cast<uint32_t>(__wrap_fmaf(x, y, z)));
    }
    for (int i = 0; i < 256; ++i) Require(RepeatedSite() == 5.);
    Require(timerCalls > 0 && timerCalls < 40); // Sparse sampling, rather than per-call timers.
    const auto previous = timerCalls;
    std::thread worker([] { for (int i = 0; i < 256; ++i) Require(__wrap_fma(1., 2., 3.) == 5.); });
    worker.join(); Require(timerCalls == previous); // Worker math is excluded.
    for (unsigned i = 0; i < 32; ++i) { expected.gpr[i] = i; expected.fpr[i].raw = i + 77; }
    expected.gpr[3] = 0x2000; expected.gpr[4] = 0x4000; expected.lr = 0x805e88f4;
    auto actual = expected;
    __wrap_func_8007A170(&actual);
    auto returned = expected; returned.gpr[3] = 1;
    Require(std::memcmp(&returned, &actual, sizeof(actual)) == 0);
    for (unsigned i = 0; i < 40; ++i) {
        actual = expected; __wrap_func_8007A170(&actual);
        Require(std::memcmp(&returned, &actual, sizeof(actual)) == 0);
    }
    // Late allocation failures must remain observable after the startup log
    // cap. The diagnostic wrappers still preserve all unrelated registers.
    failLayoutAllocation = true;
    actual = expected; __wrap_func_8007A170(&actual);
    Require(std::memcmp(&returned, &actual, sizeof(actual)) == 0);
    Require(paneCalls == 42 && allocationCalls == 42);
    WicLogCpuDiagnostics(60);
    Require(!native);
}
