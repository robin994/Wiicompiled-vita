# WiiCompiled-Vita — I/O stage 2: deferred GXP writes and incremental scene prewarm

Date: 2026-10-11. Repository: `wiicompiled-vita`; reusable GXM changes
are isolated in the `aurora-main` git submodule, with defaults that preserve
other games' immediate GXP cache persistence.

## Scope and observed costs

The baseline Vita3K session
`build/vita3k-trace/draw-isolation-20261011/synchronized_retry/runtime-session.log`
contained 287 PSARC requests / 60,074,367 logical bytes / 995,122 us of
aggregate native reads by the later snapshot. This is not PS Vita hardware
evidence and does **not** show that storage is the only or dominant 1-FPS
bottleneck; GX decoding and shader state preparation also consume CPU.

Stage 1 previously implemented a 4×16 KiB adaptive PSARC read cache,
semaphore wakeups rather than busy spin, and buffered quiet-mode log writes.
This stage focuses on two remaining latency sources:

1. On a shader cache miss, `gxm_renderer.cpp::compile_stage()` immediately
   called `ProgramBinaryCache::save()`, issuing fopen/fwrite/fclose/
   remove/rename while handling the draw.
2. At a scene change, `native_assets::EnterScene()` could synchronously
   read the **entire** selected sidecar prewarm budget on a presentation
   boundary. The existing max prewarm budget is up to 8 MiB.

## Changes made

### A. Deferred GXP persistence — WiiCompiled opt-in, rollback by INI

New setting:

```toml
[vita]
shader_cache_deferred_writes = true
```

- Default **true for WiiCompiled**; **false at every Aurora-Vita API level**
  unless a title explicitly opts in.
- A new bounded `DeferredProgramWrites` class stages compiled, validated
  GXP program files in at most **2 MiB** of RAM, **256 records**, up to
  **256 KiB per program**. Stage and source hash jointly identify records.
  Identical stages are deduplicated; a recompiled variant replaces the prior
  staged copy rather than creating a second entry.
- The runtime shader is still compiled, validated by
  `sceGxmProgramCheck`, registered and used immediately; only the **disk
  persistence** is deferred. Queue overflow, OOM and failed cache writes
  cannot invalidate an already working GXM shader. Large programs outside
  the staging limit simply are not persisted by the deferred mode.
- After GXM's *startup* pipeline prewarm and **before the first gameplay
  frame**, drain the startup queue, writing the same checksum-protected GXP
  file format as before. This persists the startup/hot shader set even if
  a later game session freezes.
- Newly compiled stages during gameplay remain in RAM until an orderly
  renderer shutdown, when the remaining queue is flushed. No cache-file
  writes occur from the per-shader compile/draw path in deferred mode.
- Failed writes retain queued records for a subsequent flush attempt.
  A crash or a hard power-off *during gameplay* loses the pending records,
  but existing cache files and already compiled GPU programs remain intact.
- A later request for a shader staged but not yet written now reads the
  **checksum-validated GXP directly from the bounded RAM queue**, avoiding
  a pointless failed filesystem lookup and recompilation. This is keyed
  by both source hash and shader stage, and the count is exported as
  `ram_hits` in the flush diagnostics.
- Deferred publication no longer unlinks an existing GXP before rename.
  This avoids removing the previous file if power is lost between the
  two operations, and avoids an unnecessary remove syscall on normal
  cache misses. A Vita filesystem that refuses rename-over-existing will
  retain the older file and the pending record and report a write error;
  it does not make the live shader fail.
- The original direct-save path remains reachable with:
  `shader_cache_deferred_writes = false` **on the same SELF**. No change
  to GXP file extensions, compatibility, native PSARC fallback, checksum
  validation or normal ShaderPatcher lifetime.
- A startup/shutdown log records saved records, **bytes written**,
  remaining staging bytes, rejected records, write errors and RAM hits:
  `[aurora-gxm] deferred_shader_cache flush=... written_bytes=...
     remaining=... queued_bytes=... dropped=... errors=... ram_hits=...`.
- `gxm_program_cache.cpp` alone enables `-fexceptions` in the Aurora
  Vita CMake configuration so any RAM allocation failure while staging a
  cache record is handled without aborting the application. Rendering and
  other Aurora source files retain their original compile policy.

Relevant files:
`aurora-main/platforms/vita/gxm/gxm_deferred_program_writes.hpp`,
`gxm_program_cache.hpp/.cpp`, `gxm_renderer.hpp/.cpp`,
`gxm_facade.cpp`, `gfx/vita_renderer.hpp`, `aurora_vita_backend.hpp/.cpp`,
`aurora-main/cmake/aurora_vita.cmake`; WiiCompiled
`runtime/include/vita_render_options.h`, `runtime/include/runtime_config.h`,
`vita/render_config.hpp`, `vita/backend.cpp`.

### B. Incremental prewarm for native sidecars

The **existing synchronous** `WarmBoot()` and `EnterScene()` API
contracts are preserved for other callers and host tests. The game-specific
scene flow now uses new functions:

- `QueueScene(scene)`: checkpoint the previous hot manifest as before,
  read and validate the new **small metadata manifest**, and build an
  immutable bounded candidate list. **No sidecar PSARC asset reads** occur
  at this stage. The scene identity includes Mario/PAL race parameters.
- `StepScenePrewarm(maxBytes,maxAssets)`: on the presentation-side tick,
  at most the supplied number of sidecars are loaded. Each valid read
  reuses the same `psarc::ReadWhole`, source validation and LRU cache
  insertion used before.
- WiiCompiled services this queue with **one asset ≤64 KiB every 8
  completed frames** (frame % 8 == 4). Excessively large assets are skipped
  for speculative prewarm but still loaded correctly on demand. Cache hits
  remain in RAM and are not counted as physical PSARC requests.
- The pending list resets on new scenes, catalog reinitialization and
  shutdown. The hottest selected assets are inserted *last*, preserving
  the MRU property of the original all-at-once prewarm.
- When a multi-asset step runs out of bytes *partway through the step*, a
  candidate that fits an empty step remains scheduled for the **next**
  tick. It is skipped permanently only if its individual size exceeds
  the per-step maximum. This prevents lost prewarm candidates with
  `StepScenePrewarm(maxBytes, maxAssets>1)`.
- Best-effort dirty manifest checkpoint frequency changed from 128 to
  **512 completed frames**, in addition to scene transitions and shutdown.
  This cuts filesystem write frequency while retaining bounded periodic
  recovery of learned sidecars.
- The runtime now emits
  `[asset-prewarm] phase=scene-plan ... selected=... pending=... bytes=0`
  and
  `[asset-prewarm] phase=step ... loaded=... skipped_large=...
     bytes=... remaining=...`.
  The earlier boot-prewarm log remains unchanged.

**Important limitation:** the step is still synchronous on the
post-presentation thread and can stall that thread for a single
64 KiB disk request. This stage bounds the burst but does not yet provide
full asynchronous I/O preemption or guaranteed nonblocking playback.
The PSARC source archive is unchanged and the prewarm default remains
`native_asset_prewarm_mb = 0` unless the user opts in.

Relevant files: `vita/native_assets.cpp/.hpp`, `vita/game_aurora.cpp`,
`vita/tests/native_assets_test.cpp` and
`scripts/test_mkw_native_assets.py`.

### C. NAND saves and ordinary I/O remain untouched

No NAND/ISFS `fflush` / `fsync` / atomic-safe-write semantics changed.
No PSARC file, user configuration or save data was edited by this work.

## Validation

- WiiCompiled: `ctest --test-dir build/host --output-on-failure -j2`
  **32/32 PASS**. Under parallelism 6, the existing
  `host-wait-native-contract` timing test aborted once; it passed alone
  and passed again in the complete suite under 2-way concurrency.
- Aurora-Vita: `ctest --test-dir build/aurora-host
  --output-on-failure -j6` **28/28 PASS**, including the new
  `vita_deferred_program_writes` test.
- Deferred staging test covers encoded GXP header validity, deduplication,
  replacement, vertex/fragment stage keys, errors and retry, bounded
  memory and queue length, limits, discarding, RAM lookup, validated
  decoding and persisted-byte accounting.
- Native PSARC fixture now includes **two different sidecars**; the
  native-assets integration test demonstrates that planning reads no
  PSARC sidecar data, each 1-item step loads only one asset, later reads
  hit the RAM cache, a tiny per-step budget skips oversized assets
  without corrupting the request, and the second of two assets **is
  deferred rather than lost** when a multi-item step runs out of
  bandwidth.
- Cross compilation: affected GXM/Aurora/WiiCompiled translation units
  checked using VitaSDK `arm-vita-eabi-g++ -fsyntax-only`. No ELF, SELF
  or VPK compiled; no hardware installation performed.
- Final verification after the RAM-reuse, no-unlink, persisted-byte and
  deferred-budget fixes: full WiiCompiled host **32/32 PASS** (ctest -j4),
  full Aurora host **28/28 PASS** (ctest -j4), targeted GXP queue **PASS**,
  and VitaSDK syntax-only on gxm_program_cache.cpp, gxm_renderer.cpp,
  gxm_facade.cpp, Aurora backend, vita/native_assets.cpp,
  vita/game_aurora.cpp, vita/backend.cpp and vita/game_main.cpp
  **8/8 PASS**. These tests do not establish device FPS.
- `git diff --check` passed for parent and submodule.

## Hardware A/B test (after **manual** build and install)

Keep the **same** SELF, game PSARC, textures, clocks and circuit:

1. Baseline with `[vita] shader_cache_deferred_writes = false`,
   `native_asset_prewarm_mb = 0`, `[diagnostics] enabled = false`.
2. Same hardware and build with `shader_cache_deferred_writes = true`
   and prewarm still 0. Compare frame-time spikes when **cold GXP programs
   must compile**, not just steady-state FPS after caches are hot.
3. If stable, train scene assets with `native_asset_cache_mb = 2`,
   `native_asset_prewarm_mb = 1`. Re-enter the same course once
   the hot manifest has been recorded; look for `scene-plan` and
   `step` logs rather than a multi-megabyte `phase=scene` burst.
4. Compare `[psarc]` physical reads, bytes, I/O time and cache hits,
   `[asset-prewarm]` queued/loaded/skipped counts,
   `[aurora-gxm] deferred_shader_cache` saved/dropped/errors, and
   visible rendering (especially Mario texture, eyes and lightmaps).
5. For crash diagnosis use a **separate** `diagnostics.enabled=true`
   session. It changes logging and CPU/GPU scheduling, so FPS from
   diagnostic and quiet runs are not directly comparable.

There are **no device FPS results for stage 2**; all performance benefits
are hypotheses pending hardware measurements. Avoid blaming all 1-FPS
rendering on I/O based solely on the ~1 second aggregate PSARC read time
in the earlier emulator run.
