#include "host_wait.h"

#include <thread>

#if defined(MKW_TARGET_VITA)
#include "native_stack_call.h"
#include "native_call_profile.h"
#include <array>
#include <cstdio>
#endif

namespace HostWait {

void SleepUntil(Clock::time_point deadline, Site site)
{
    if (Clock::now() >= deadline) return;
#if defined(MKW_TARGET_VITA)
    HostContext::CallOnNativeStack([deadline, site] {
        // Keep the absolute deadline: dispatch time must not lengthen the wait.
        const auto now = Clock::now();
        if (now >= deadline) return;
        static thread_local std::array<unsigned, 4> counts{};
        constexpr const char* names[] = {"vi", "gx-retry", "gki", "panic"};
        const auto index = static_cast<unsigned>(site);
        if (counts[index] < 4) {
            int stackMarker = 0;
            const unsigned count = ++counts[index];
            std::fprintf(stderr, "[wait-native] site=%s wait=%u stack=%p remaining_us=%lld\n",
                         names[index], count, static_cast<void*>(&stackMarker),
                         static_cast<long long>(std::chrono::duration_cast<
                             std::chrono::microseconds>(deadline - now).count()));
        }
        const bool profiling = HostContext::NativeCallProfilingEnabled();
        const auto started = profiling ? Clock::now() : Clock::time_point{};
        std::this_thread::sleep_until(deadline);
        if (profiling) HostContext::RecordNativeWait(index, static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count()));
    });
#else
    (void)site;
    std::this_thread::sleep_until(deadline);
#endif
}

void SleepFor(std::chrono::nanoseconds duration, Site site)
{
    if (duration <= std::chrono::nanoseconds::zero()) return;
#if defined(MKW_TARGET_VITA)
    SleepUntil(Clock::now() + duration, site);
#else
    (void)site;
    std::this_thread::sleep_for(duration);
#endif
}

} // namespace HostWait
