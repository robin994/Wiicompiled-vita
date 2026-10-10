#include "game_cpu_diagnostics.hpp"
#include "game_heap_watch.hpp"
#include "isa/ppc_isa_context.h"
#include "memory.h"
#include "native_stack_call.h"
#include "float_fma_fast.h"
#include "gfx/vita_cpu_workers.hpp"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr/thread.h>
#include <array>
#include <cfenv>
#include <cstdio>
#if defined(__arm__)
#include <arm_neon.h>
#endif

extern "C" double __real_fma(double, double, double);
extern "C" float __real_fmaf(float, float, float);
extern "C" void __real_func_8007A170(CpuContext*);
extern "C" void __real_func_8007AB90(CpuContext*);
extern "C" void __real_func_80199B98(CpuContext*);
extern "C" float __wrap_fmaf(float, float, float);
extern "C" void __wrap_func_8007A170(CpuContext*);

namespace {
struct Site {
    uintptr_t caller = 0;
    unsigned precision = 0;
    uint64_t calls = 0, samples = 0, sampledUs = 0;
    uint64_t Estimate() const { return samples ? sampledUs * calls / samples : 0; }
};
struct State {
    std::array<Site, 512> sites{};
    std::array<Site, 512> pairSites{};
    std::array<uint64_t, 2> pairCalls{};
    std::array<uint32_t, 2> pairOffset{};
    uint32_t pairRandom = 0x94fe317bu;
    uint64_t floatCalls = 0, doubleCalls = 0, dropped = 0;
    bool busy = false;
};
State gameState;
thread_local State* currentState = nullptr;

struct LayoutTrace {
    CpuContext* context;
    unsigned paneCalls = 0, paneFailures = 0, allocations = 0, allocationFailures = 0;
    uint32_t firstPane = 0, firstFailedPaneKind = 0;
    uint64_t allocatedBytes = 0;
    struct Failure { uint32_t caller, allocator, bytes; };
    std::array<Failure, 4> failures{};
};
thread_local LayoutTrace* currentLayout = nullptr;

float FloatFma(float a, float b, float c) {
#if defined(WIC_EXACT_FLOAT_FMA) && WIC_EXACT_FLOAT_FMA
#if defined(__arm__)
    uint32_t control;
    asm volatile("vmrs %0, fpscr" : "=r"(control));
    return FloatFmaFast::Evaluate(a, b, c, (control & 0x00c00000u) == 0, __real_fmaf);
#else
    return FloatFmaFast::Evaluate(a, b, c, std::fegetround() == FE_TONEAREST, __real_fmaf);
#endif
#else
    return __real_fmaf(a, b, c);
#endif
}

Site* FindSite(State& state, uintptr_t caller, unsigned precision, bool pair = false) {
    auto& sites = pair ? state.pairSites : state.sites;
    const size_t slot = ((caller >> 2) ^ (caller >> 11) ^ precision) % sites.size();
    for (size_t offset = 0; offset < 8; ++offset) {
        auto& site = sites[(slot + offset) % sites.size()];
        if (!site.caller) { site.caller = caller; site.precision = precision; }
        if (site.caller == caller && site.precision == precision) return &site;
    }
    ++state.dropped;
    return nullptr;
}

#if defined(__arm__)
template <class Callback>
__attribute__((always_inline)) inline float32x2_t ProfilePair(float32x2_t a, float32x2_t b, float32x2_t c,
                       uintptr_t caller, unsigned operation, Callback real) {
    auto* state = currentState;
    if (!state || state->busy) return real(a, b, c);
    // Sample callers above the shared PPC helper. The existing fmaf returns
    // identify the helper itself, not the translated function invoking it.
    const unsigned position = ++state->pairCalls[operation] % 128;
    if (position == 1) {
        state->pairRandom ^= state->pairRandom << 13;
        state->pairRandom ^= state->pairRandom >> 17;
        state->pairRandom ^= state->pairRandom << 5;
        state->pairOffset[operation] = state->pairRandom % 128;
    }
    // One random position in each group avoids repeatedly selecting the same
    // caller when a scene executes a periodic sequence of matrix operations.
    if (position != state->pairOffset[operation]) return real(a, b, c);
    auto* site = FindSite(*state, caller, operation, true);
    if (!site) return real(a, b, c);
    const auto start = sceKernelGetProcessTimeWide();
    const auto result = real(a, b, c);
    ++site->calls; ++site->samples;
    site->sampledUs += sceKernelGetProcessTimeWide() - start;
    return result;
}
#endif

template <class T, class Callback>
T ProfileMath(T a, T b, T c, uintptr_t caller, unsigned precision, Callback real) {
    auto* state = currentState;
    if (!state || state->busy) return real(a, b, c);
    if (precision == 32) ++state->floatCalls; else ++state->doubleCalls;
    auto* site = FindSite(*state, caller, precision);
    const bool sample = site && (++site->calls % 128) == 0;
    // Only the outer software operation counts if libm internally uses another
    // wrapped operation. No allocations or native-stack dispatch per math call.
    state->busy = true;
    const uint64_t start = sample ? sceKernelGetProcessTimeWide() : 0;
    const T result = real(a, b, c);
    if (sample) { ++site->samples; site->sampledUs += sceKernelGetProcessTimeWide() - start; }
    state->busy = false;
    return result;
}

uint32_t ReadWord(uint32_t address) {
    return address && Memory::Contains(address, 4) ? Memory::Read32(address) : 0;
}
}

void WicEnableCpuDiagnostics() {
    currentState = &gameState;
#if defined(WIC_CPU_DIAGNOSTICS)
    WicEnableHeapWatch();
#endif
    std::fprintf(stderr, "[cpu-code] fmaf_wrapper=%p layout_wrapper=%p exact_float_fma=%d diagnostic=1\n",
        reinterpret_cast<void*>(&__wrap_fmaf), reinterpret_cast<void*>(&__wrap_func_8007A170),
#if defined(WIC_EXACT_FLOAT_FMA) && WIC_EXACT_FLOAT_FMA
        1
#else
        0
#endif
    );
}

extern "C" __attribute__((noinline)) double __wrap_fma(double a, double b, double c) {
    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    return ProfileMath(a, b, c, caller, 64, __real_fma);
}
extern "C" __attribute__((noinline)) float __wrap_fmaf(float a, float b, float c) {
#if defined(WIC_CPU_DIAGNOSTICS)
    const auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    return ProfileMath(a, b, c, caller, 32, FloatFma);
#else
    return FloatFma(a, b, c);
#endif
}

#if defined(__arm__)
extern "C" float32x2_t WicRealPairAdd(float32x2_t, float32x2_t, float32x2_t)
    asm("__real__Z18PpcFmaddPairInline18__simd64_float32_tS_S_");
extern "C" float32x2_t WicRealPairSub(float32x2_t, float32x2_t, float32x2_t)
    asm("__real__Z18PpcFmsubPairInline18__simd64_float32_tS_S_");
extern "C" float32x2_t WicWrappedPairAdd(float32x2_t, float32x2_t, float32x2_t)
    asm("__wrap__Z18PpcFmaddPairInline18__simd64_float32_tS_S_");
extern "C" float32x2_t WicWrappedPairSub(float32x2_t, float32x2_t, float32x2_t)
    asm("__wrap__Z18PpcFmsubPairInline18__simd64_float32_tS_S_");
extern "C" __attribute__((noinline)) float32x2_t WicWrappedPairAdd(float32x2_t a, float32x2_t b, float32x2_t c) {
    return ProfilePair(a, b, c, reinterpret_cast<uintptr_t>(__builtin_return_address(0)), 0, WicRealPairAdd);
}
extern "C" __attribute__((noinline)) float32x2_t WicWrappedPairSub(float32x2_t a, float32x2_t b, float32x2_t c) {
    return ProfilePair(a, b, c, reinterpret_cast<uintptr_t>(__builtin_return_address(0)), 1, WicRealPairSub);
}
#endif

extern "C" void __wrap_func_8007AB90(CpuContext* ctx) {
    auto* trace = currentLayout;
    const uint32_t kind = ctx->gpr[3];
    const bool active = trace && trace->context == ctx && ctx->lr == 0x8007a37cu;
    __real_func_8007AB90(ctx);
    if (active) {
        ++trace->paneCalls;
        if (ctx->gpr[3]) { if (!trace->firstPane) trace->firstPane = ctx->gpr[3]; }
        else { if (!trace->paneFailures) trace->firstFailedPaneKind = kind; ++trace->paneFailures; }
    }
}

extern "C" void __wrap_func_80199B98(CpuContext* ctx) {
    auto* trace = currentLayout;
    const uint32_t caller = ctx->lr, allocator = ctx->gpr[3], bytes = ctx->gpr[4];
    // Only SDK Layout/BuildPane allocations belong to this trace. Other guest
    // fibers may execute on the same OS thread while an outer call is alive.
    const bool active = trace && trace->context == ctx &&
        ((caller >= 0x8007abe0u && caller < 0x8007ad60u) ||
         caller == 0x8007a3e8u || caller == 0x8007a440u);
    __real_func_80199B98(ctx);
    if (active) {
        ++trace->allocations; trace->allocatedBytes += bytes;
        if (!ctx->gpr[3]) {
            if (trace->allocationFailures < trace->failures.size())
                trace->failures[trace->allocationFailures] = {caller, allocator, bytes};
            ++trace->allocationFailures;
        }
    }
}

// Linker wrapping leaves the generated registry, function kind and guest ABI
// intact. Inspect the resource before Build and its result before MainLayout
// attempts to traverse the root pane's child list.
extern "C" void __wrap_func_8007A170(CpuContext* ctx) {
    const uint32_t layout = ctx->gpr[3], data = ctx->gpr[4], accessor = ctx->gpr[5];
    const uint32_t caller = ctx->lr;
    static unsigned logged = 0;
    const bool enabled = currentState != nullptr;
    const bool log = enabled && logged < 32;
    if (log) {
        ++logged;
        HostContext::CallOnNativeStack([=] {
            std::fprintf(stderr,
                "[layout-build] phase=begin caller=0x%08x layout=0x%08x data=0x%08x accessor=0x%08x "
                "header=0x%08x bom_version=0x%08x size=%u header_blocks=0x%08x\n",
                caller, layout, data, accessor, ReadWord(data), ReadWord(data ? data + 4 : 0),
                ReadWord(data ? data + 8 : 0), ReadWord(data ? data + 12 : 0));
        });
    }
    LayoutTrace trace{ctx};
    struct RestoreTrace { LayoutTrace* previous; ~RestoreTrace() { currentLayout = previous; } } restore{currentLayout};
    if (enabled) currentLayout = &trace;
    __real_func_8007A170(ctx);
    const uint32_t result = ctx->gpr[3];
    static unsigned failures = 0;
    if (enabled) {
        HostContext::CallOnNativeStack([=] {
            const auto root = ReadWord(layout + 16), groups = ReadWord(layout + 20);
            const auto children = ReadWord(root ? root + 20 : 0);
            // Later menus must still report a failure after the startup sample
            // cap. A null link is invalid even when Build reported success.
            if (!log && (result != 0 && root && children && !trace.allocationFailures)) return;
            if (!log && failures++ >= 16) return;
            std::fprintf(stderr,
                "[layout-build] phase=end caller=0x%08x layout=0x%08x result=%u root=0x%08x groups=0x%08x "
                "children_next=0x%08x children_prev=0x%08x sentinel=0x%08x data=0x%08x header=0x%08x bom_version=0x%08x\n",
                caller, layout, result, root, groups, children,
                ReadWord(root ? root + 24 : 0), root ? root + 20 : 0, data,
                ReadWord(data), ReadWord(data ? data + 4 : 0));
            if (!root || result == 0 || trace.allocationFailures) {
                std::fprintf(stderr, "[layout-result] data=0x%08x size=%u header_blocks=0x%08x "
                    "panes=%u pane_failures=%u allocs=%u requested_bytes=%llu alloc_failures=%u allocator=0x%08x\n",
                    data, ReadWord(data ? data + 8 : 0), ReadWord(data ? data + 12 : 0),
                    trace.paneCalls, trace.paneFailures, trace.allocations,
                    static_cast<unsigned long long>(trace.allocatedBytes), trace.allocationFailures,
                    ReadWord(ctx->gpr[13] - 27280u));
                std::fprintf(stderr, "[layout-pane-result] first_pane=0x%08x first_failed_kind=0x%08x "
                    "first_pane_vtable=0x%08x children_next=0x%08x children_prev=0x%08x\n",
                    trace.firstPane, trace.firstFailedPaneKind, ReadWord(trace.firstPane),
                    ReadWord(trace.firstPane ? trace.firstPane + 20 : 0),
                    ReadWord(trace.firstPane ? trace.firstPane + 24 : 0));
                for (unsigned i = 0; i < trace.allocationFailures && i < trace.failures.size(); ++i) {
                    const auto& failure = trace.failures[i];
                    const auto table = ReadWord(failure.allocator);
                    std::fprintf(stderr, "[layout-alloc-failure] caller=0x%08x allocator=0x%08x "
                        "size=%u vtable=0x%08x callback=0x%08x heap=0x%08x alignment=%d\n",
                        failure.caller, failure.allocator, failure.bytes, table, ReadWord(table),
                        ReadWord(failure.allocator + 4), int32_t(ReadWord(failure.allocator + 8)));
                    if (i == 0 && ReadWord(table) == 0x80199b58u) {
                        // This callback tail-calls MEMAllocFromExpHeapEx. Its
                        // header/free-list are guest state, unrelated to the
                        // host's free user/CDRAM counters. Read on native stack
                        // only, bound every traversal, never repair a list here.
                        const auto heap = ReadWord(failure.allocator + 4);
                        if (heap && Memory::Contains(heap, 80)) {
                            std::fprintf(stderr, "[layout-heap] address=0x%08x signature=0x%08x "
                                "start=0x%08x end=0x%08x flags=0x%08x free_first=0x%08x free_last=0x%08x "
                                "used_first=0x%08x used_last=0x%08x settings=0x%08x\n",
                                heap, ReadWord(heap), ReadWord(heap+24), ReadWord(heap+28),
                                ReadWord(heap+56), ReadWord(heap+60), ReadWord(heap+64),
                                ReadWord(heap+68), ReadWord(heap+72), ReadWord(heap+76));
                            uint32_t node = ReadWord(heap+60), previous = 0, largest = 0;
                            uint64_t total = 0;
                            unsigned nodes = 0;
                            bool valid = true;
                            while (node && nodes < 4096) {
                                if (!Memory::Contains(node, 16) || node == previous) { valid=false; break; }
                                const auto size = ReadWord(node+4), next = ReadWord(node+12);
                                const auto prev = ReadWord(node+8);
                                if (nodes < 8) std::fprintf(stderr, "[layout-heap-free] index=%u address=0x%08x "
                                    "tag=0x%08x bytes=%u prev=0x%08x next=0x%08x\n",
                                    nodes, node, ReadWord(node), size, prev, next);
                                if (prev != previous) {
                                    std::fprintf(stderr,"[layout-heap-link-error] address=0x%08x expected_prev=0x%08x actual_prev=0x%08x bytes=%u next=0x%08x\n",
                                        node,previous,prev,size,next);
                                    valid=false; break;
                                }
                                total += size; if (size > largest) largest = size;
                                ++nodes; previous = node; node = next;
                            }
                            std::fprintf(stderr, "[layout-heap-summary] nodes=%u free_bytes=%llu "
                                "largest=%u links_valid=%d truncated=%d\n", nodes,
                                static_cast<unsigned long long>(total), largest, valid, node != 0);
                            // PPCHalt exits normally and produces no kernel
                            // coredump. Preserve the actual guest heap once,
                            // including its header/free/used blocks, for an
                            // offline allocator replay tied to this binary.
                            static bool heapDumped=false;
                            const uint64_t end=ReadWord(heap+28);
                            if(!heapDumped && end>heap && end-heap<=64u*1024*1024 && Memory::Contains(heap,size_t(end-heap))) {
                                if(const auto* image=Memory::GetPointer(heap,size_t(end-heap))) {
                                    const char* path="ux0:data/wiicompiled-vita/heap-failure-0.bin";
                                    if(auto* file=std::fopen(path,"wb")) {
                                        heapDumped=true;
                                        const auto written=std::fwrite(image,1,size_t(end-heap),file);
                                        const auto closed=std::fclose(file);
                                        std::fprintf(stderr,"[layout-heap-dump] path=%s address=0x%08x bytes=%llu written=%llu close=%d diagnostic_only=1\n",
                                            path,heap,static_cast<unsigned long long>(end-heap),static_cast<unsigned long long>(written),closed);
                                    }
                                }
                            }
                        }
                    }
                }
                static unsigned dumped = 0;
                const auto size = ReadWord(data ? data + 8 : 0);
                if (dumped < 2 && size >= 16 && size <= 2 * 1024 * 1024 && Memory::Contains(data, size)) {
                    const auto* bytes = Memory::GetPointer(data, size);
                    if (bytes) {
                        char path[128];
                        std::snprintf(path, sizeof(path), "ux0:data/wiicompiled-vita/layout-failure-%u.brlyt", dumped);
                        if (auto* output = std::fopen(path, "wb")) {
                            const auto written = std::fwrite(bytes, 1, size, output);
                            std::fclose(output); ++dumped;
                            std::fprintf(stderr, "[layout-resource] path=%s bytes=%u written=%u\n", path, size, unsigned(written));
                        }
                    }
                }
            }
        });
    }
}

void WicLogCpuDiagnostics(uint64_t frame) {
    auto* state = currentState;
    if (!state) return;
    const int thread = sceKernelGetThreadId();
    std::fprintf(stderr,
        "[cpu-math] frame=%llu fmaf_calls=%llu fma_calls=%llu dropped_sites=%llu sample_period=128 "
        "thread=%d affinity=0x%x cpu=%d diagnostic=1\n",
        static_cast<unsigned long long>(frame), static_cast<unsigned long long>(state->floatCalls),
        static_cast<unsigned long long>(state->doubleCalls), static_cast<unsigned long long>(state->dropped),
        thread, sceKernelGetThreadCpuAffinityMask(thread), sceKernelGetCpuId());
    // Keep at most eight sites per window, sorted by sampled time estimate.
    std::array<const Site*, 8> top{};
    for (const auto& site : state->sites) {
        if (!site.calls) continue;
        const Site* candidate = &site;
        for (auto& position : top) {
            if (!position || candidate->Estimate() > position->Estimate()) {
                const Site* displaced = position; position = candidate; candidate = displaced;
                if (!candidate) break;
            }
        }
    }
    for (const auto* site : top) {
        if (!site) break;
        std::fprintf(stderr,
            "[cpu-math-site] frame=%llu return_pc=0x%08x precision=%u calls=%llu samples=%llu "
            "sampled_us=%llu estimated_us=%llu estimate_only=1\n",
            static_cast<unsigned long long>(frame), unsigned(site->caller), site->precision,
            static_cast<unsigned long long>(site->calls), static_cast<unsigned long long>(site->samples),
            static_cast<unsigned long long>(site->sampledUs), static_cast<unsigned long long>(site->Estimate()));
    }
    std::fprintf(stderr, "[cpu-pair] frame=%llu madd_calls=%llu msub_calls=%llu sample_period=128 randomized_offset=1 diagnostic=1\n",
        static_cast<unsigned long long>(frame), static_cast<unsigned long long>(state->pairCalls[0]),
        static_cast<unsigned long long>(state->pairCalls[1]));
    top = {};
    for (const auto& site : state->pairSites) {
        if (!site.calls) continue;
        const Site* candidate = &site;
        for (auto& position : top) {
            if (!position || candidate->sampledUs > position->sampledUs) {
                const Site* displaced = position; position = candidate; candidate = displaced;
                if (!candidate) break;
            }
        }
    }
    for (const auto* site : top) {
        if (!site) break;
        std::fprintf(stderr, "[cpu-pair-caller] frame=%llu return_pc=0x%08x operation=%s samples=%llu "
            "estimated_calls=%llu sampled_us=%llu estimated_us=%llu estimate_only=1 nested_times=1\n",
            static_cast<unsigned long long>(frame), unsigned(site->caller), site->precision ? "msub" : "madd",
            static_cast<unsigned long long>(site->samples), static_cast<unsigned long long>(site->calls * 128),
            static_cast<unsigned long long>(site->sampledUs), static_cast<unsigned long long>(site->sampledUs * 128));
    }
    *state = {};
    const auto parallel = aurora::vita::gfx::cpu_vertex_parallel_snapshot();
    static aurora::vita::gfx::CpuVertexParallelSnapshot previous{};
    std::fprintf(stderr,
        "[cpu-workers] frame=%llu helpers=%u lanes=%u calls=%llu dynamic_calls=%llu wall_us=%llu caller_wait_us=%llu nested_times=1\n",
        static_cast<unsigned long long>(frame), aurora::vita::gfx::cpu_worker_threads(),
        aurora::vita::gfx::cpu_execution_lanes(),
        static_cast<unsigned long long>(parallel.calls - previous.calls),
        static_cast<unsigned long long>(parallel.dynamicCalls - previous.dynamicCalls),
        static_cast<unsigned long long>(parallel.totalWallUs - previous.totalWallUs),
        static_cast<unsigned long long>(parallel.callerWaitUs - previous.callerWaitUs));
    for (unsigned lane = 0; lane < aurora::vita::gfx::MaxExecutionLanes; ++lane)
        std::fprintf(stderr, "[cpu-worker-lane] frame=%llu lane=%u items=%llu chunks=%llu work_us=%llu\n",
            static_cast<unsigned long long>(frame), lane,
            static_cast<unsigned long long>(parallel.laneItems[lane] - previous.laneItems[lane]),
            static_cast<unsigned long long>(parallel.laneChunks[lane] - previous.laneChunks[lane]),
            static_cast<unsigned long long>(parallel.laneWorkUs[lane] - previous.laneWorkUs[lane]));
    previous = parallel;
}
