#pragma once

#include <cstdint>

struct CpuContext;

// Low-overhead producer-stall diagnostics. The producer records scalar state at
// frame boundaries; the Vita render worker polls it while waiting for the next
// packet. This deliberately uses no guest execution, locks, or instruction
// tracing from the watchdog path.
namespace GuestStallWatchdog {

void RecordFrame(uint64_t serial, uint64_t progressUs, uint64_t intervalUs,
                 const CpuContext* cpu) noexcept;

void RecordTaskThread(uint32_t taskThread, uint32_t job, uint32_t callback,
                      uint32_t argument) noexcept;

void RecordMovieManager(uint32_t manager, uint32_t state,
                        uint32_t result) noexcept;

// kind: 1=sleep 2=wake(queue) 3=wake_thr 4=resume 5=suspend 6=send 7=recv_blk 8=reschd_idle 9=io_cb
void TraceSchedEvent(uint32_t kind, uint32_t a, uint32_t b, uint32_t c) noexcept;

void Poll(uint64_t nowUs) noexcept;

} // namespace GuestStallWatchdog
