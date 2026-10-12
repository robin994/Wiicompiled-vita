#pragma once

// GXCopyTex is queued behind the FIFO producer's earlier commands on Vita.
// Only bypass the producer wait when no draw-done callback can observe it.
namespace wiicompiled::gx {
constexpr bool copy_tex_must_wait(bool experimentalAsync, bool workerRunning,
                                  bool drawDoneCallbackActive) noexcept {
    return !experimentalAsync || !workerRunning || drawDoneCallbackActive;
}
}

#if defined(MKW_TARGET_VITA)
extern "C" void WicSetAsyncEfbCopies(bool enabled) noexcept;
#endif
