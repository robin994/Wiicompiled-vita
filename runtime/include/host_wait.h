#pragma once

#include <chrono>

namespace HostWait {

using Clock = std::chrono::steady_clock;

enum class Site { VideoRetrace, FrameRetry, ControllerDelay, FatalExit };

// Only the host wait crosses the Vita native-stack boundary. Guest timing,
// callbacks and the caller's continuation remain in their original context.
void SleepUntil(Clock::time_point deadline, Site site);
void SleepFor(std::chrono::nanoseconds duration, Site site);

} // namespace HostWait
