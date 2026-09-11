#pragma once

#include <atomic>
#include <cstdint>

#ifndef MKW_VITA_GUEST_PC_SAMPLER
#define MKW_VITA_GUEST_PC_SAMPLER 0
#endif
#ifndef MKW_VITA_GUEST_ACTIVE_CPU_PROFILE
#define MKW_VITA_GUEST_ACTIVE_CPU_PROFILE 0
#endif

// Low-overhead statistical producer profiler for the Vita build.  Generated
// translated functions publish their guest entry address with one relaxed
// 32-bit store; a helper-core thread samples that token.  USER_0 never allocates,
// locks, resolves symbols, or writes logs on this path.
namespace GuestHotProfiler {

inline constexpr uint32_t kUnknown = 0u;
inline constexpr uint32_t kPhaseWaitVi = 0xFFFF0001u;
inline constexpr uint32_t kPhaseWaitAlarm = 0xFFFF0002u;
inline constexpr uint32_t kPhaseWaitAudio = 0xFFFF0003u;
inline constexpr uint32_t kPhaseHostOther = 0xFFFF0004u;

#if defined(MKW_TARGET_VITA) && MKW_VITA_GUEST_PC_SAMPLER
extern std::atomic<uint32_t> g_currentToken;

inline void MarkGuestPc(uint32_t pc) noexcept {
    g_currentToken.store(pc, std::memory_order_relaxed);
}

class HostPhaseScope {
public:
    explicit HostPhaseScope(uint32_t phase) noexcept
        : previous_(g_currentToken.exchange(phase, std::memory_order_relaxed)) {}
    ~HostPhaseScope() noexcept {
        g_currentToken.store(previous_, std::memory_order_relaxed);
    }
    HostPhaseScope(const HostPhaseScope&) = delete;
    HostPhaseScope& operator=(const HostPhaseScope&) = delete;
private:
    uint32_t previous_;
};

bool Start() noexcept;
void Stop() noexcept;
#else
inline void MarkGuestPc(uint32_t) noexcept {}
class HostPhaseScope {
public:
    explicit HostPhaseScope(uint32_t) noexcept {}
};
inline bool Start() noexcept { return true; }
inline void Stop() noexcept {}
#endif

} // namespace GuestHotProfiler

