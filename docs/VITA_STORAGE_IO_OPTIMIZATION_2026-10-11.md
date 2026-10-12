# WiiCompiled-Vita — storage I/O budget, first optimization pass (2026-10-11)

## Baseline and problem statement

The Vita is limited by flash/SD-card latency and CPU contention. The runtime
must not turn every small GX/DOL resource request into a physical filesystem
operation, or flush a logfile for every diagnostic line while rendering.

The prior Vita3K run in
`build/vita3k-trace/draw-isolation-20261011/synchronized_retry/runtime-session.log`
reported these PSARC snapshots (data **from the emulator**, not PS Vita):

| Snapshot | Logical archive reads | Requested bytes | Native archive read time |
|---|---:|---:|---:|
| Early boot | 7 | 9,851,341 | 17,774 us |
| Intermediate | 9 | 16,272,865 | 62,764 us |
| Later, frame 300 run | 287 | 60,074,367 | 995,122 us |

Physical latency on a retail Vita can be different. These totals do not
prove that I/O is the main rendering bottleneck: GX frontend decode and CPU
shader work are separately significant. The old `reads` counter measured
logical requests, *not* `sceIoPread` transactions.

That emulator session also wrote a 212,888-byte/2,091-line game transcript
with `_IOLBF`. The process previously forced
`performance_attribution=true` and
`texture_decode_diagnostics=true` even when diagnostics were disabled.

## New implementation

### 1. One archive worker, blocking wakeup, no busy waits

`vita/psarc_backend.cpp` already routes PSARC calls to a single native Vita
thread using `sceIoPread`. It previously polled its command slot every
100 microseconds and both the caller and contending submitters used tight
ARM `yield` spin loops. Those cycles steal CPU from the guest/GX/GXM threads
during slow storage operations.

The worker now uses separate **request-ready** and **completion** Vita kernel
semaphores. The caller blocks while physical I/O runs. Contending submitters
briefly spin, then call `sceKernelDelayThread(100)` with a strictly
positive delay until their turn.

The existing per-archive serialization, read ordering and progress state
values (0 idle, 1 published, 2 completed) are unchanged. No additional
parallel disk reader was created; random flash reads remain serialized.

### 2. Adaptive logical PSARC read cache (64 KiB RAM)

`vita/psarc_read_cache.hpp` implements a four-entry LRU of **16 KiB
immutable logical-file windows** (4 × 16 KiB = 65,536 bytes), with at most
eight lightweight access hints. It is owned exclusively by the serial PSARC
worker; **not** a GPU texture cache and no extra CDRAM allocation.

- Only requests **≤4 KiB, contained in one 16 KiB logical window**
  can use the cache. Large or boundary-crossing reads are passed to the
  existing archive implementation unchanged.
- The **first** small read into a window reads only the exact bytes
  requested. Read-ahead happens only on a subsequent access to that
  same file and logical window. Sparse, one-off small reads do not
  provoke 16 KiB speculative disk requests.
- Subsequent reads hitting the cached window are memcpy-only. This
  avoids repeating `sceIoPread` calls for DVD access patterns involving
  many adjacent small reads.
- Entry index is part of the key; no bytes from one PSARC member can
  be served as another. The fill respects the member's logical size
  (partial final windows are supported).
- If a read-ahead fill fails, the code retries **only the exact
  requested bytes** through the original reader. Wrong ranges and
  null buffers still fail before disk access. The cache is reset on
  PSARC mount and unmount.
- Compressed PSARC members are cached *after* decompression; there is
  no dependency on the physical archive block layout.

**Tradeoff:** a second access to a window may read more bytes than a
single small request. That can decrease seek count for hot/adjacent
accesses, but it is not guaranteed to improve every workload. The
new counters quantify whether read-ahead pays off.

### 3. Storage counters

The `[psarc]` periodic/exit line now preserves the old fields
(`reads`, `bytes`, `native_io_us`, `failures`) and appends:

```
physical_reads=... physical_bytes=... small_reads=...
cache_hits=... cache_fills=... io_failures=...
```

`reads` counts guest logical requests; `physical_reads` counts
actual backend archive read calls. `physical_bytes` includes
successful prefetches. Cache hits do not count toward
`native_io_us`, which measures only physical read functions.
Mount-time TOC/manifest I/O is still excluded from these counters.

A/B comparison must measure **deltas** over equivalent intervals, not
unrelated cumulative totals.

### 4. Writing less during normal gameplay

- `vita/game_main.cpp`: when `[diagnostics] enabled = false`,
  `runtime.log` uses two fixed, full-buffered **32 KiB** stdio
  buffers instead of line-buffering each `stdout` and `stderr`
  line. Diagnostic runs keep the original immediate line-buffered
  behavior. This reduces the frequency of syscalls during a quiet race,
  at the cost of a short delay before logs become visible on disk.
  A sudden hard freeze may lose the most recent buffered quiet-mode
  lines; use `diagnostics.enabled=true` for crash investigations.
- `vita/backend.cpp`: performance-attribution and texture-decoder
  diagnostics are now enabled **only** when diagnostics are explicitly
  enabled. Normal rendering no longer pays the full tracing overhead.
- `vita/native_assets.cpp`: the persisted hot-sidecar manifest is
  marked dirty on the first real access to a new asset in the current
  scene. Repeated cache hits do **not** trigger otherwise redundant
  flash writes at every 128-frame checkpoint.
- NAND/ISFS **writes are unchanged**. Their `fflush`, safe-save
  `fsync` and rename contracts protect game saves. It would be
  unsafe to remove them without durability tests.

## Tests and compatibility

`vita/tests/psarc_read_cache_test.cpp` tests identical bytes for
cached/uncached reads, cross-file isolation, large/boundary-crossing
reads, partial-file EOF, failed lookahead fallback, invalid ranges,
LRU eviction, cache reset and zero overfetch for sparse small reads.
Host PSARC and native-assets end-to-end tests exercise the unchanged
PSARC API on generated archives.

Validation performed:
- WiiCompiled host: **32/32** tests passed.
- Aurora-Vita host: **27/27** tests passed (no changes made to Aurora).
- VitaSDK cross-compiler: **five updated Vita translation units**
  passed `-fsyntax-only` (compile-only, no SELF or VPK).
- `git diff --check` passed.
- **No PS Vita/real-device or new Vita3K FPS test yet**; no measured
  runtime performance gain is claimed.

## Device A/B procedure (manual Vita build only on request)

1. Keep a copy of the previous known installed SELF and the exact PSARC;
   do not modify `ux0:data/wiicompiled-vita/mariokart.psarc`.
2. Build WiiCompiled with the existing manual Vita build command.
3. Run the same course/character twice with
   `[diagnostics] enabled = false`. Use the first run to warm
   program and native-sidecar caches; capture the second run's
   throughput and frame time.
4. On stable paths that finish cleanly, compare the old and new build's
   `[psarc]` deltas: disk operations, bytes and total native I/O time.
   A large `cache_fills` with few subsequent `cache_hits` indicates
   wasted read-ahead and calls for lowering cache aggressiveness.
5. For detailed unexpected stalls run a **separate** session with
   diagnostics enabled. Do not compare FPS of trace-enabled and
   trace-disabled builds: profiling itself changes scheduling/I/O.
6. Check that time-trial boot, saves, assets (especially white Mario
   materials), audio and EFB copying are unchanged.
7. If a real Vita hangs in a DVD request, capture
   `WicReadPsarcProgress` as well as the crash dump: wait state 1
   now means blocked in a kernel semaphore rather than CPU spin.

## Follow-up improvements after actual device measurements

- Delay or move newly compiled GXP program-cache writes from frame
  execution to a dedicated bounded, low-priority I/O queue. The current
  one-file-per-new-shader persistence is crash-resistant, so it must
  not be turned off without a durable alternative.
- If profiling shows write bursts around saves, review NAND/ISFS
  buffering **without compromising NANDSafeClose atomicity**.
- Load only scene-hot sidecar assets, not the entire 3.2 GB PSARC,
  and allow prewarm I/O to yield to gameplay DVD requests. The
  prewarm manifest remains opt-in (`native_asset_prewarm_mb=0` default).
- Avoid using full diagnostic logs as FPS references.

Existing PSARC files, cache blobs, user INI and hardware save data are
not modified by the source changes in this pass.
