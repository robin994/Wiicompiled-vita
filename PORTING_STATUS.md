# WiiCompiled Vita / Mario Kart Wii – Porting Status

> **Snapshot:** 2026-09-04 (M13.1 hardware log analyzed; indexed-raw VCD bug identified;
> M13.2 indexed-raw + true-white visibility probe built and packaged)
> **Repository:** `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`  
> **Target:** Mario Kart Wii PAL `RMCP01`, static recompile PowerPC → ARM32, PS Vita hardware  
> **Current HEAD observed:** `c2d6db0 checkpoint` (external checkpoint commits appear in this
> repo — do not assume they are ours; never rewrite history). M1–M4 landed in `c2d6db0`; M5–M8
> are uncommitted working-tree changes.

This document is a technical handoff/state file. It summarizes what has already been implemented, what has been verified on real hardware, how the current Aurora-Vita integration works, what still needs to change, and what the next profiling/implementation steps should be.

> **Worktree rule:** the worktree is intentionally dirty. Do not use `git reset`, `git checkout`, `git restore`, `git clean`, `git revert`, `git commit` or `git push` unless explicitly requested.

---

## 1. Goal

The long-term goal is to run Mario Kart Wii PAL on PS Vita using:

- static recompilation of the Wii/PowerPC executable and `StaticR.rel` to ARM32;
- HLE/runtime replacements for Wii OS, GX, VI, input, audio, NAND/DVD and related SDK services;
- a PS Vita-native renderer based on Aurora's Vita graphics layer and vitaGL/GXM;
- real PS Vita hardware as the source of truth for runtime validation.

The immediate goals are:

1. make submitted 3D character/kart/race geometry visibly render, first with a conclusive solid-white probe and then with faithful materials;
2. make the indexed display-list raw path consume NW4R/G3D index streams correctly and remove the current hundreds of milliseconds of per-vertex fallback work;
3. restore required EFB, billboards, lighting and movies one at a time after the geometry/material boundary is established;
4. continue improving Aurora-Vita correctness without regressing the now-working Single Player/menu/race progression and readable in-race text.

---

## 2. Hardware-confirmed state

The newest supplied append-mode log is `runtime.log`, SHA-256
`6bb9489e583cae63467a415cd9bf39a0ff3dd1c77ba13066f60f839dcbc985a2`, 11,432,799 bytes.
Its final session is the M13.1 probe and runs through the race workload without a terminal crash.
The user confirms that in-race text is readable but 3D models remain invisible. See the M13.1
hardware result near the end of this file: the intended white-material A/B was not conclusive
because M13.1 did not actually replace vertex RGBA with white.

The earlier renderer/profiling baseline was:

`build/vita/wiicompiled-vita-mkw-firstboot-aurora-speedhack-hotguest-glyphraw-gaptrace.vpk`

SHA-256:

`179c4a74b461ac6900923ead4e767690a4ac178d45c0144088bf786c49158906`

Confirmed on real PS Vita:

- the application boots;
- the early loader/memory blockers are gone;
- the previous empty-frame flicker is gone;
- text is visible again;
- the four menu cards are visible;
- the animated videos inside the cards are visible;
- input can select `Single Player`;
- after selecting `Single Player`, the game reaches a black transition but does **not** reach the next menu even after roughly two minutes;
- the card screen remains extremely slow, around `1.5 FPS`.

Build success and runtime success must always be kept separate. The observations above are hardware evidence.

---

## 3. Latest local build not yet hardware-validated

`build/vita/wiicompiled-vita-mkw-firstboot-m13_2-indexed-raw-white.vpk` — M13.2, SHA-256
`97c9ed295e301178ac3864212bbcc4b0944a60eedfe3a4b910b919d5fc59ef50`.
This preserves the M13.1 diagnostic switches, fixes the indexed VCD/raw decode ordering bug and
makes the geometry probe truly white/opaque. Full ARM32 build, bridge/VI checks and packaging pass;
real-hardware validation is pending. The exact symbol ELF and test procedure are recorded in the
M13.2 section below.

Historical M6 artifact from the earlier scheduler phase:

`build/vita/wiicompiled-vita-mkw-firstboot-aurora-speedhack-m6-dvd-deferred-togglable.vpk`
(M6, this session) — SHA-256 `ea0ab46bc144954cc98ab8b338016e23c0abfe54c29d46cf5fb45aafc6e74d5a`.
= M5 (deferred-DVD-completion queue + block-copy DMA + Fable patch C host-file LRU) with a
working kill switch: `getenv` does nothing for a LiveArea `.vpk`, so `DvdDeferEnabled()` now also
honours a marker file `ux0:data/wiicompiled-vita/dvd_defer_off` (present => inline behaviour) and
the compile flag `MKW_VITA_DVD_DEFER=0` (`-D` in `COMMON_FLAGS`, default 1). Pending hardware
validation. Superseded: M4 `c622f26b…`, M5 `06343c67…`.

Prior builds this session:
- `...-phase-fiber-watchdog-nomovies.vpk` (M1) — SHA-256 `8a7f5401814176556baecd722774dd8509f142f682b7500d8fe2c9811b4f1f69` — movie disable only. Hardware-tested (see section 16).
- `...-nomovies-threaddump.vpk` (M2) — SHA-256 `610561d3947c44ea24d6c5040867c32c7b772698713faae9f7cea83f5c92f38d` — adds the guest-thread-table dump. Hardware-tested (see section 16).
- `...-nomovies-schedtrace.vpk` (M3) — SHA-256 `44ce9d07b25c5333b1050b00930bd65d1c3e670663ddee2a0fc2efc91beb713d` — scheduler-event ring-buffer trace. Superseded by M4 before hardware test.

---

## 4. Major milestones already completed

### 4.1 Static recompile and real Vita boot

The project has moved beyond a graphics probe. Mario Kart Wii's translated guest code is linked into a Vita executable and reaches real game code.

The port now has:

- translated DOL/REL functions;
- a guest symbol/dispatch registry;
- ARM32-compatible PPC helpers;
- a PS Vita guest-memory implementation;
- HLE bridges for Wii services;
- a fiber-based model for Wii guest threads.

The early `C1-2569-2`/loader phase and the first guest-memory allocation blocker were already passed.

### 4.2 Guest memory

The Vita build uses a controlled memory layout suitable for Vita constraints:

- MEM1 guest backing;
- MEM2 guest backing;
- physical/cached/uncached aliases;
- Kamek overlay mapping;
- a fast mapped locked-cache region around `0xE0000000` for THP decoder use.

The locked-cache change is important because THP decoding performs many accesses there. A sub-page-only mapping forced those accesses through the slow checked fallback.

### 4.3 Guest scheduling and host-core layout

Current host-core policy is intentionally conservative:

- `USER_0`: Wii guest execution / scheduler / guest fibers;
- `USER_1`: Aurora/vitaGL render worker;
- `USER_2`: audio worker;
- background helpers may use helper cores but should not migrate onto the guest CPU.

Do **not** parallelize arbitrary translated `CpuContext` execution across multiple Vita cores. The Wii scheduler/fiber model is cooperative and shares guest state; parallel guest execution would require explicit synchronization and determinism work.

### 4.4 Input

The runtime progressed from basic PAD assumptions to the input paths actually used by Mario Kart Wii (`KPAD/WPAD`), enough to navigate into `Single Player`.

### 4.5 Text

A native HLE fast path exists for:

`Text::GlyphDrawer::Draw @ 0x805CF598`

An earlier fast-path condition caused text to disappear. That was fixed and real hardware confirmed that text is visible again.

The current path also has a Vita-specific raw-draw shortcut:

- it builds the complete glyph quad;
- submits it directly to Aurora;
- bypasses the incremental `GXBegin + FIFO gather-pipe parser` path when the layout is exactly supported;
- falls back to the faithful path if the raw path cannot be used;
- preserves the original logical GX caller address for profiling.

Hardware profiling confirmed:

- `glyph_fast=526`
- `glyph_raw=526`
- `glyph_fallback=0`

for the hot menu frame.

Therefore the raw glyph path is working; glyph FIFO parsing is no longer the main explanation for the ~650-700 ms card frame.

### 4.6 THP/menu video

Support has been added for the Mario Kart menu's THP/YUV path:

- recognition of Y/U/V I8 planes;
- Wii I8 8x4 detiling;
- YUV420 → RGBA conversion;
- conversion/caching keyed by source generation/revision/frame state;
- upload through the Aurora Vita texture path.

The user has visually confirmed that the videos in the four cards are visible.

The diagnostic marker `thp_yuv420` is not guaranteed to appear in every current log, so marker absence alone must not be interpreted as a visual failure.

### 4.7 GXCopyTex / EFB

The renderer now has a GPU-resident EFB-copy path intended for dynamic menu preview textures.

The preferred path is:

1. flush queued draws so FIFO ordering is preserved;
2. capture the requested current framebuffer rectangle on GPU;
3. keep the result as an Aurora/Vita EFB texture;
4. reuse it directly when the guest later binds the corresponding texture destination.

A synchronous `glReadPixels`/RGBA upload fallback still exists for cases where the GPU capture cannot satisfy the request.

This was important for making the four cards/videos visible without forcing repeated CPU readbacks.

---

## 5. Current performance evidence

### 5.1 Card screen: guest CPU is the dominant bottleneck

Typical producer times from the latest hardware log are approximately:

- `646 ms`
- `668 ms`
- `674 ms`
- `678 ms`
- `694 ms`

per logical frame.

That is only about `1.4–1.55 FPS`.

The same scene's Aurora render worker is around:

`render_us ≈ 42,829 us`

or roughly `42.8 ms`.

So the current problem is not primarily framebuffer resolution or GPU raster cost. The guest producer on `USER_0` is an order of magnitude slower.

### 5.2 Renderer batching is already working

A representative frame showed roughly:

- `logical_draws = 665`
- `physical_draws = 110`

This means Aurora's pipeline/state batching is already reducing the number of physical draws substantially.

The renderer is not yet fast enough for a 60 FPS target, but it is not the reason the current menu runs at ~1.5 FPS.

### 5.3 The glyph hypothesis has been mostly ruled out

The card frame has roughly:

- 526 glyph draw calls at caller `0x805CF604`;
- 131 LYT/Picture-related calls around `0x8007B294`;
- 7 calls around `0x805FCB70`.

With `gx_gap_hot`, the accumulated inter-`GXBegin` time is only approximately:

- glyphs: `~16–17 ms`;
- LYT/Picture path: `~39 ms`;
- 7-call path: `~13–14 ms`.

Total observed time between those GX begin points is only around `70 ms`, while the producer takes around `670 ms`.

Therefore roughly **550–600 ms per card frame are occurring outside the intervals previously attributed to GXBegin callers**.

This is the key current performance question.

---

## 6. Current Single Player / black-screen evidence

After selecting `Single Player`, the hardware log reaches:

`------------------- 1 Scene Exit -------------------`

Then a transition frame takes approximately:

`6,329,592 us`

with:

- `535` draws;
- `3624` vertices;
- `45` EFB commands.

The strongest hot caller in that frame is:

`0x800C23C0`

with approximately:

- `284` calls;
- `5,683,795 us` accumulated gap time;
- one observed gap of roughly `4,971,324 us`.

Another relevant caller is:

`0x800C4CA4`

with roughly:

- `144` calls;
- `105,618 us`.

These addresses are in the RFL/Mii-related path and have been associated with work such as `RFLiInitShapeRes` / `RFLiDrawQuad`.

After that, another large frame is produced:

- `producer_frame=806`
- `interval_us≈1,652,001`
- `draws=1326`
- `vertices=9280`
- `efb_cmds=12`

But the log does not reach a corresponding `Scene Enter` before output stops progressing normally.

So there are likely **two separate problems** to distinguish:

1. a very expensive RFL/Mii transition frame (survivable — the frame completes and the guest recovers);
2. a scheduler wait after the later large frame that never resolves.

**Update (this session) — problem 2 is confirmed and characterized. See section 16.** It is a
cooperative-scheduler condition where, after the big transition frame (`render_large` serial
~684/891) completes on the render worker, every guest thread ends up `state=4` (WAITING) except
the nw4r SoundThread; the scheduler pending mask is `0`, so the idle loop only ever services the
SoundThread on the audio tick and the root/scene threads' wait is never satisfied. Disabling THP
movies did **not** fix it (video was not the blocker); RFL is not spinning (`rfl_working=0`).

### RFL database note

The runtime reports that `ux0:data/wiicompiled-vita/NAND/shared2/menu/FaceLib/RFL_DB.dat`
cannot be opened. `RFLiIsWorking` telemetry shows its working flag as `0`, not a persistent busy
value, in every stall snapshot so far. RFL/Mii is therefore **not the deadlock**, though the
missing database still causes a ~6 s `RFLiInitShapeRes` burst on the license screen and the
license/Mii UI likely misbehaves. A minimal valid default-Mii `RFL_DB.dat` (generated, no
Nintendo data, the way Dolphin does it) bundled at the NAND path is the intended fix for that,
tracked as a later quality milestone — not blindly bypassing RFL.

---

## 7. Latest uncommitted profiling changes

At this snapshot the source worktree contains additional diagnostics newer than the last hardware-tested `glyphraw-gaptrace` VPK.

Modified/untracked files observed:

- `runtime/include/fiber_manager.h`
- `runtime/src/fiber_manager.cpp`
- `runtime/src/hle/gx/gx_copy.cpp`
- `runtime/src/hle/gx/gx_dl.cpp`
- `runtime/src/hle/gx/gx_fifo.cpp`
- `runtime/src/hle/gx/gx_internal.h`
- `runtime/src/hle/gx/gx_text.cpp`
- `runtime/src/hle/os/os_alarm.cpp`
- `runtime/src/hle/task_thread.cpp`
- `vita/gx_backend.cpp`
- `runtime/include/guest_stall_watchdog.h`
- `runtime/src/guest_stall_watchdog.cpp`

These changes must not be discarded.

### 7.1 `gx_phase`: pre-begin and tail timing

The GX CPU profiler now records:

- `preFirstBeginUs`
- `tailAfterLastBeginUs`

and logs:

`gx_phase frame=... prebegin_us=... tail_us=... begins=...`

The goal is to account for the ~550–600 ms that `gx_gap_hot` cannot currently explain.

### 7.2 Reduced profiler log perturbation

The old hot-call profiler could print many unbuffered lines every slow frame. That can distort timing on Vita.

The current version samples routine frames and keeps exceptional frames:

- regular sampling around every 30 frames;
- always retain very large draw frames;
- always retain large `max_gap_us` transition stalls.

### 7.3 Fiber-slice timing

`GuestFiberManager::SwitchToThread` now times cooperative guest-fiber slices on Vita.

Long slices, with an initial threshold around 20 ms, produce a low-frequency log like:

`fiber_slice n=... from=... target=... entry=... elapsed_us=...`

This should reveal whether THP, RFL, TaskThread or another guest fiber is occupying `USER_0` for most of the missing frame time.

### 7.4 Guest stall watchdog

A new low-overhead watchdog is present in:

- `runtime/include/guest_stall_watchdog.h`
- `runtime/src/guest_stall_watchdog.cpp`

The producer records an atomic snapshot at frame boundaries. The render worker polls while idle and, if the guest has not produced a frame for more than one second, logs at most about once per second.

The snapshot includes:

- last frame serial;
- stall duration;
- previous producer interval;
- current guest thread;
- OS current/running context;
- thread state and priority;
- scheduler pending mask / idle state;
- guest `pc`, `srr0`, `lr`;
- current fiber entry point;
- TaskThread current job/callback/argument;
- MovieManager state/result;
- RFL manager/working flag.

This is intended to identify what happens after the last large `Single Player` transition frame without per-instruction tracing.

### 7.5 Large-render-frame progress

`vita/gx_backend.cpp` now adds progress markers for frames with `>=1000` draws:

- render begin;
- periodic draw progress;
- submit done;
- end-frame done;
- swap begin/end;
- completed.

This lets us distinguish a guest stall from a render-worker stall when a huge transition frame is submitted.

---

# 8. How Aurora-Vita is implemented in this project

There are two related but distinct layers in the repository:

1. a fairly complete **Aurora Vita backend implementation** under `aurora-main/platforms/vita`;
2. the **WiiCompiled-specific production bridge** actually used by `Makefile.vita`.

They are related, but the current game build does **not** simply link the full `aurora_vita_backend` CMake target.

---

## 8.1 Aurora Vita platform layer

The port added a Dawn-free Vita graphics backend under:

`aurora-main/platforms/vita/`

Important graphics modules include:

- `gfx/vita_renderer.*`
- `gfx/vita_pipeline_cache.*`
- `gfx/vita_pipeline_key.*`
- `gfx/vita_shader_gen.*`
- `gfx/vita_texture_cache.*`
- `gfx/vita_texture_decode.*`
- `gfx/vita_streaming_arena.*`
- `gfx/vita_buffer_pool.*`
- `gfx/vita_efb.*`
- `gfx/vita_draw_adapter.*`
- `gfx/vita_vertex_decode.*`
- `gfx/vita_vertex_pipeline.*`
- `gfx/vita_telemetry.*`

There are also higher-level components under:

- `platforms/vita/gx/`
- `platforms/vita/integration/`

including:

- a GX bridge;
- a Vita DrawSink;
- feature coverage;
- FIFO packet queue/capture/replay;
- a generic `VitaGxBackend`;
- a `WiiCompiledAuroraAdapter`.

`aurora-main/cmake/aurora_vita.cmake` defines a static `aurora_vita_backend` target.

### Important upstream limitation

The CMake path explicitly keeps normal Vita builds **Dawn-free**.

`AURORA_VITA_WITH_UPSTREAM_GX` remains an integration/desktop option because Aurora's upstream `aurora::gx` target still owns a Dawn dependency. The Vita toolchain path intentionally rejects enabling that option.

One architectural task still open is to split the upstream GX frontend/IR from Dawn strongly enough that Vita can use the upstream GX layer directly.

That cleanup is desirable, but it is **not required to diagnose the current 1.5 FPS guest bottleneck**.

---

## 8.2 What `Makefile.vita` actually builds today

The Mario Kart Vita build currently compiles only the graphics subset of Aurora Vita directly:

`aurora-main/platforms/vita/gfx/*.cpp`

selected through `AURORA_VITA_GFX_SRCS`.

It also compiles:

- `vita/gx_backend.cpp`
- `vita/aurora_packet_renderer.cpp`

When:

`MKW_VITA_AURORA_RENDERER=1`

the game links:

- the custom WiiCompiled GX backend;
- the WiiCompiled Aurora packet adapter;
- Aurora Vita GFX primitives/caches/renderer;
- vitaGL, vitaShaRK, ShaccCg/GXM and related Vita libraries.

The generic `aurora_vita_backend.cpp`, `platforms/vita/gx/*` and most of `platforms/vita/integration/*` are **not the active production path in this Makefile**.

This is currently intentional: WiiCompiled already has a large HLE GX state machine and needed a controlled packet boundary rather than forcing the full desktop/upstream Aurora GX stack onto Vita at once.

---

## 8.3 Current render data flow

The active path is approximately:

```text
Translated Mario Kart Wii / NW4R code
            |
            v
runtime HLE GX functions
(gx_fifo, gx_dl, gx_texture, gx_copy, gx_text, ...)
            |
            v
vita/gx_backend.cpp
- tracks Wii GX state
- decodes FIFO/display-list draws
- captures matrices/viewport/scissor/raster/texture state
- transforms guest vertices
- records EFB commands
- builds FramePacket
            |
            | producer: USER_0
            v
       frame queue
            |
            | render worker: USER_1
            v
vita/gx_backend.cpp::RenderWorkerMain
            |
            v
vita/aurora_packet_renderer.cpp
- maps GX enums to Aurora Vita GFX enums
- converts primitives / builds indices
- converts vertices to CanonicalVertex
- resolves texture/EFB sources
- creates pipeline descriptions
- enqueues draws into StreamingArena/CommandStream
            |
            v
aurora-main/platforms/vita/gfx
- Renderer
- PipelineCache / shader generation
- TextureCache / texture decode
- EFB manager
- StreamingArena / buffer pools
            |
            v
vitaGL speedhack
            |
            v
GXM / PS Vita GPU
```

The CPU guest and renderer are intentionally separated by a frame packet so translated Wii code does not make direct vitaGL calls.

---

## 8.4 Surface and resolution

The current packet renderer is hard-coded to initialize Aurora at:

`960 x 544`

which is the Vita display resolution.

The Wii logical GX viewport remains effectively based around `640 x 480`, and `vita/gx_backend.cpp` scales the logical viewport/scissor to the physical Vita surface.

This matters for any future 360p experiment.

---

## 8.5 Streaming and batching

The current Aurora packet renderer initializes a streaming arena approximately as:

- vertex storage: `512 KiB`;
- index storage: `64 KiB`;
- 3 slots;
- 16-byte alignment.

It uses:

- prepared canonical vertices;
- generated 16-bit indices for quads/strips/fans;
- pipeline-key caching;
- state caching in `vita_renderer`;
- mapped stream upload when the speedhack vitaGL variant is enabled.

The result is already measurable: hundreds of logical Wii draws can collapse to around a hundred physical GL/GXM draws.

---

## 8.6 Texture path

The packet bridge currently maps these important Wii texture formats:

- I4
- I8
- IA4
- IA8
- RGB565
- RGB5A3
- RGBA8
- CMPR
- internal RGBA8-PC path

The Aurora Vita texture cache uses:

- source identity;
- generation/revision;
- cache budget;
- sampler state.

The current Mario Kart build sets the Aurora packet texture budget to about:

`12 MiB`

Recent profiling showed the cache close to that limit with evictions in the card scene, so cache pressure is a secondary optimization target.

---

## 8.7 THP path

The WiiCompiled packet layer has extra THP knowledge that is not just generic Aurora:

- it recognizes the three Y/U/V planes supplied by the game;
- detiles Wii I8 planes;
- converts YUV420 to RGBA;
- keys/reuses the conversion.

This is one reason the current WiiCompiled bridge still contains runtime-specific logic instead of being a completely generic Aurora backend.

---

## 8.8 EFB copy path

`GXCopyTex` commands are preserved in frame order and executed between draws.

For current menu use, the preferred Aurora path keeps the copy GPU-resident:

`current framebuffer -> EFB texture -> later sampled texture`

This avoids synchronous readback for every preview.

There is still a readback fallback.

The current implementation is optimized for color-preview use and is **not yet a claim of complete Wii EFB semantics**.

---

# 9. Does Aurora-Vita still need modifications?

Yes. The current implementation is sufficient to boot and draw a meaningful portion of Mario Kart's UI, but it is not yet a complete Wii GX implementation.

## 9.1 Full GX/TEV representation

The WiiCompiled `AuroraPacketDraw` currently exposes a simplified texture/TEV representation.

`AuroraPacketRendererSubmit()` ultimately calls `ConfigureTev()` with a simplified mode and creates a limited pipeline description.

Mario Kart and NW4R can use:

- multiple TEV stages;
- multiple texture coordinates/maps;
- konst colors;
- more complex color/alpha combiners;
- indirect texture features;
- additional raster-channel behavior.

The runtime already tracks more GX state than the packet bridge currently transmits.

Before race rendering can be considered generally correct, the packet ABI should be expanded to carry the actual TEV stage state required by observed frames, and Aurora Vita shader generation must consume it accurately.

Do this based on logged unsupported/fallback signatures, not by implementing every GX feature blindly.

## 9.2 Additional texture formats and mip behavior

The active packet renderer does not yet represent every Wii texture/palette/depth format.

Potential future requirements include:

- C4/C8/C14X2 palette formats;
- additional copy/depth formats;
- true mip-chain handling;
- LOD/bias behavior.

Add these when hardware logs show they are required.

## 9.3 More faithful EFB semantics

The current fast EFB path is intentionally optimized for GPU-only color previews.

Future scenes may require:

- exact copy-format conversion;
- depth copies;
- CPU-visible readback;
- guest RAM packing;
- copy filters;
- gamma/scaling details;
- cases where copied EFB memory is modified/read by guest code before rebinding.

Those cases need a coherent GPU-resident + lazy-readback model rather than unconditional success stubs.

## 9.4 Texture cache pressure

Recent menu frames put the ~12 MiB texture cache near budget and trigger evictions.

Possible work:

- identify transient THP/EFB textures separately from persistent UI textures;
- improve generation/revision invalidation;
- avoid duplicate decoded copies;
- explicitly retire dead EFB copies;
- tune budget only after measuring total vitaGL/CDRAM/USER pressure.

Increasing the budget without understanding lifetime is not enough.

## 9.5 Streaming arena overflow/flush behavior

Profiling has shown vertex-stream overflows in some frames.

The current arena is intentionally small to protect Vita memory.

Possible improvements:

- flush/reuse slots more efficiently;
- size the arena based on real high-water marks;
- reduce duplicate vertex expansion/copy;
- merge compatible logical draws earlier.

## 9.6 Remove duplicate CPU work in the render worker

The current pipeline has multiple CPU-side representations:

1. guest/raw vertex state;
2. `RenderVertex` after Wii transform in `gx_backend.cpp`;
3. `AuroraPacketVertex`;
4. `CanonicalVertex` inside the packet renderer;
5. streaming-buffer copy.

This is safe and easy to debug, but not optimal.

Once correctness is stable, the Vita path could:

- emit the canonical Aurora vertex layout earlier;
- avoid per-draw vector clear/resize where possible;
- use contiguous frame storage directly;
- avoid rebuilding quad indices repeatedly for common fixed shapes;
- move more batching before the renderer submit loop.

This is primarily a USER_1/render-worker optimization; it does not explain the current ~600 ms missing on USER_0.

## 9.7 Unify the custom packet bridge with Aurora's generic Vita backend

Today there are effectively two integration approaches in-tree:

- generic Aurora Vita `DrawSink/VitaGxBackend/WiiCompiledAuroraAdapter`;
- the active `vita/gx_backend.cpp + vita/aurora_packet_renderer.cpp` path.

Long term, maintaining both is undesirable.

A good eventual architecture would be:

1. make upstream Aurora GX frontend backend-neutral and Dawn-free;
2. expose a stable GX IR/packet interface;
3. have WiiCompiled produce that interface;
4. let Aurora Vita own the renderer/pipeline/texture/EFB implementation;
5. keep Wii-specific HLE responsibilities in WiiCompiled, not duplicated in Aurora.

For the current bring-up, however, replacing the active packet bridge now would add risk without solving the main guest CPU blocker.

---

# 10. 360p / lower resolution

A temporary `640 x 360` physical surface is technically possible, but it is **not the next primary performance fix**.

Current rough costs:

- guest producer: `~650–700 ms`;
- renderer: `~43 ms`.

Even a very large GPU reduction would leave the game around the same ~1.5 FPS because `USER_0` remains dominant.

### What must change before a safe 360p build

The current Aurora packet renderer hard-codes `960 x 544`.

A proper resolution switch should:

- make the physical surface width/height configurable;
- keep Wii logical GX coordinates/viewport semantics unchanged;
- scale viewport/scissor to the physical surface;
- keep `GXCopyTex` destination dimensions in **logical copy space**, not blindly shrink them with the framebuffer;
- allow a physical source region smaller than a requested destination to be scaled correctly;
- keep EFB sampling and Y-origin conversions correct;
- validate text, card previews and THP after the change.

In particular, an EFB copy whose guest destination is `128 x 128` must remain a valid `128 x 128` destination even if the physical framebuffer is lower resolution.

### When to test it

Once the guest producer is reduced to tens of milliseconds, create an A/B build:

- native Vita `960x544`;
- reduced physical `640x360`.

Then compare:

- `render_us`;
- visual quality;
- EFB correctness;
- texture-cache pressure;
- total frame time.

---

# 11. What is still missing for a playable port

The project is not just missing "FPS". Several stages still need to be proved.

## Blocker A — account for the missing ~550–600 ms/card frame

This is the immediate performance blocker.

The next hardware log must tell us whether that time is:

- before the first GX draw;
- after the last GX draw;
- inside a long guest-fiber slice;
- THP decode;
- NW4R update/animation;
- scheduler/alarm work;
- another guest subsystem.

Do not continue optimizing draw submission by guesswork until this is known.

## Blocker B — complete Single Player scene transition

We need to determine:

- why the RFL/Mii initialization frame takes several seconds;
- what consumes the ~5-second single gap;
- whether identical Mii shape/resource work is repeated and cacheable;
- what thread/fiber/job owns execution after `producer_frame=806`;
- why `Scene Enter` never arrives.

## Blocker C — prove the next UI scenes

Once Single Player opens, validate:

- Grand Prix / Time Trials / VS / Battle screen;
- character selection;
- vehicle selection;
- cup/course selection.

Each step may expose new GX/LYT/G3D or filesystem/HLE requirements.

## Blocker D — first real race

A playable port still needs a race scene to prove:

- 3D models;
- world transforms;
- lighting/TEV;
- track textures;
- sky/effects;
- EFB effects;
- HUD;
- input;
- audio;
- race timing/game logic.

## Blocker E — renderer feature coverage

Before broad visual correctness, close the GX features actually hit by race frames:

- multi-stage TEV;
- additional textures/maps;
- palette/mipmap formats as required;
- EFB/depth copy semantics;
- unsupported blend/alpha/copy cases.

## Blocker F — performance

The eventual target is ideally 60 FPS where feasible, but performance work should proceed in this order:

1. remove pathological guest CPU costs;
2. remove repeated/static guest work via faithful caching/HLE;
3. optimize hot translated shards/helpers;
4. optimize USER_1 packet/renderer CPU cost;
5. reduce texture/cache/EFB churn;
6. test physical-resolution reduction if still necessary;
7. tune GPU/state/shader path.

---

# 12. Recommended next hardware test

Use the newest phase/fiber/watchdog build.

Before launch:

1. remove or archive the old `runtime.log`;
2. start the game on real PS Vita;
3. reach the four-card screen;
4. leave it untouched for `20–30 s`;
5. press `Single Player` exactly once;
6. leave the black screen running for at least `60 s`;
7. close the application;
8. preserve the new log without appending another run.

Search first for:

```text
gx_phase
prebegin_us
tail_us
fiber_slice
guest_watchdog_stall
render_large
producer_frame
gx_gap_hot
Scene Exit
Scene Enter
RFL
THP
task_
movie_
```

### Decision tree

If `prebegin_us` or `tail_us` accounts for ~500+ ms:

- map that phase to the active guest thread/function;
- optimize that routine, not GX draw submission.

If a `fiber_slice` is hundreds of milliseconds:

- identify its `entry=`;
- map the entry/call path to THP/RFL/TaskThread/etc.;
- profile that worker specifically.

If `guest_watchdog_stall` repeats after the last frame:

- compare `os_running`, `current_guest`, `fiber_entry`, `pending`, `task_cb`, `movie_state`, `rfl_working`;
- determine whether the guest is runnable, waiting, or stuck inside one cooperative slice.

If `render_large` stops before `completed`:

- investigate USER_1/Aurora instead of the guest scheduler.

---

# 13. Rules for future optimization

1. **Profile first.** The previous glyph hypothesis was plausible but the raw path proved that most time was elsewhere.
2. **Keep fast paths faithful.** Every native/HLE shortcut should preserve guest-visible state or have a fallback.
3. **Do not skip game logic to gain FPS.**
4. **Do not parallelize shared guest state casually.**
5. **Treat EFB ordering as FIFO ordering.**
6. **Keep build evidence separate from hardware evidence.**
7. **Keep logs sampled.** Excessive `stderr` logging can itself become a Vita bottleneck.
8. **Preserve the dirty worktree.**
9. **Do not make 360p the default until it has an A/B correctness/performance result.**
10. **Avoid replacing the active Aurora bridge with the generic one during bring-up unless it solves a measured blocker.**

---

# 14. Current source-of-truth files

## WiiCompiled Vita runtime / GX

- `vita/gx_backend.cpp`
- `vita/aurora_packet_renderer.cpp`
- `vita/aurora_packet_renderer.h`
- `runtime/src/hle/gx/gx_fifo.cpp`
- `runtime/src/hle/gx/gx_dl.cpp`
- `runtime/src/hle/gx/gx_copy.cpp`
- `runtime/src/hle/gx/gx_text.cpp`
- `runtime/src/hle/gx/gx_texture.cpp`
- `runtime/src/hle/gx/gx_internal.h`

## Scheduling / diagnostics

- `runtime/src/fiber_manager.cpp`
- `runtime/include/fiber_manager.h`
- `runtime/include/guest_stall_watchdog.h`
- `runtime/src/guest_stall_watchdog.cpp`
- `runtime/src/hle/task_thread.cpp`
- `runtime/src/hle/os/os_alarm.cpp`

## Aurora Vita

- `aurora-main/cmake/aurora_vita.cmake`
- `aurora-main/platforms/vita/aurora_vita_backend.cpp`
- `aurora-main/platforms/vita/gfx/vita_renderer.cpp`
- `aurora-main/platforms/vita/gfx/vita_pipeline_cache.cpp`
- `aurora-main/platforms/vita/gfx/vita_shader_gen.cpp`
- `aurora-main/platforms/vita/gfx/vita_texture_cache.cpp`
- `aurora-main/platforms/vita/gfx/vita_texture_decode.cpp`
- `aurora-main/platforms/vita/gfx/vita_streaming_arena.cpp`
- `aurora-main/platforms/vita/gfx/vita_efb.cpp`
- `aurora-main/platforms/vita/gx/aurora_vita_draw_sink.cpp`
- `aurora-main/platforms/vita/integration/vita_gx_backend.cpp`
- `aurora-main/platforms/vita/integration/wiicompiled_aurora_adapter.cpp`

## Build

- `Makefile.vita`

---

# 15. Short handoff summary

The renderer is far enough along to show Mario Kart's real menu, text and animated card videos on PS Vita. Aurora batching and GPU EFB copies are functioning, but the game is still dominated by guest CPU execution.

The most important current facts are:

- glyph raw submission is active and did **not** solve the ~650–700 ms card frame;
- Aurora rendering is around ~43 ms, so 360p is secondary;
- ~550–600 ms/card frame remain unaccounted for by the old GXBegin profiler;
- `Single Player` exits the old scene but spends several seconds in an RFL/Mii-heavy frame;
- after another large frame, no `Scene Enter` arrives;
- RFL's working flag is not stuck high, so missing `RFL_DB.dat` is not yet proven to be the blocker;
- the newest code adds phase timing, guest-fiber slice timing and a low-overhead stall watchdog specifically to answer these questions.

The next iteration should be driven by the new hardware log, not by another broad optimization pass.

---

# 16. Session progress log (2026-09-03, session 2)

Milestone-based work on the two remaining blockers. Each milestone ends with a hardware-testable
VPK. Build: `make -f Makefile.vita mkw-first-boot`, then copy the output to a `-<tag>` name.

## M0 baseline finding — THP video

Diagnosed the post-`Single Player` black screen from the phase/fiber/watchdog log: the guest main
thread parks after the big transition frame and never resumes; a `THP::VideoDecoder` guest thread
had been spawned for the menu background movie. The ~640 ms/frame `prebegin_us` on the four-card
menu was software YUV420->RGBA conversion for the THP frames, on the guest producer thread
(USER_0). SceAvPlayer was considered as a replacement and rejected (decodes H.264/AAC only, not
THP MJPEG; cannot run the 3-4 concurrent menu movie loops; bypasses the AX mixer). The right
long-term path is native per-frame JPEG decode (`sceJpegDec`) + GPU-shader YUV->RGB on a real
Vita thread.

## M1 — disable THP movies  (DONE, hardware-tested)

- `Makefile.vita`: `MKW_VITA_DISABLE_MOVIES ?= 1`, passed as `-D` in `COMMON_FLAGS`.
- `runtime/src/hle/task_thread.cpp`: with the flag set, `RunMovieManagerPrepareAsync` returns
  immediately leaving `MovieManager` state at 0 and never opens the THP player / spawns the
  decoder thread. Verified from the translated code that at state 0 both `MoviePlayer::Update`
  (`func_805FCE7C`) and `MovieManager::CloseAsync` (`func_80529FE0`) are no-ops, and that all
  menu prepares route through the async task-job path (the sync `MovieManager::Prepare` has no
  callers).

**Hardware result:** card-menu `producer_frame` interval ~707,000 us -> ~296,000 us (1.4 -> 3.3
fps); `prebegin_us` ~640,000 -> ~230,000. **The deadlock is unchanged** — video was neither the
blocker nor the dominant remaining CPU cost. `MKW_VITA_DISABLE_MOVIES` stays on (net win, removes
a confound) until M4.

## M2 — guest-thread-table dump  (DONE, hardware-tested)

- `runtime/src/guest_stall_watchdog.cpp`: at each stall, walk the guest thread list from
  `0x800000DC` via `+0x2FC` and log per thread {state, suspend, prio, wait_q, q_link, join_q,
  fiber entry}.
- `runtime/src/fiber_manager.cpp`: `fiber_slice` threshold 20 ms -> 4 ms, denser logging, adds
  `from_entry`.

**Hardware result — the deadlock is characterized:**
- Every guest thread is `state=4` (WAITING) except `nw4r::snd SoundThread` (`0x800A49C0`), which
  cycles state 2/4 on the audio tick.
- Root thread `0x80347498` (prio 16, boot thread = `RKSystem::mainLoop`): WAITING on a queue at
  `~0x804294A4`. A prio-15 `EGG::Thread::Run` worker (`0x8042A680`): WAITING on `~0x804294F8`
  (same object, +0x54).
- `pending=0`, so the scheduler idle loop (`os_scheduler.cpp`, pumps VI + alarms + audio +
  deferred NAND/net completions only) only ever wakes the SoundThread. Nothing satisfies the
  root/worker wait.
- Reproduces 100% at the same point, both renderer runs in the log.
- `render_large` (serial 684/891) **completes** on the render worker — it is not a render stall.
  Confirmed the frame is 2D UI (1326 draws / 9280 verts ~ 7 verts/draw, 12 small EFB copies for
  Mii-head RTT), i.e. the license/Mii-select screen, not a 3D scene.

Working hypothesis: an inter-thread signal the idle loop does not generate — a candidate is an
async I/O completion (DVD callbacks fire inline in the HLE, with no deferred queue, so a caller
that sleeps *after* the inline callback loses the wakeup; the scene load / `ArchiveMgr::
LoadArchiveAsync` path is the likely trigger).

## M3 — scheduler-event trace  (built, pending hardware test)

- `runtime/src/guest_stall_watchdog.cpp`: 4096-entry ring buffer of scheduler events
  (`sleep` / `wake` / `wake_thr` / `send` / `recv_blk`), high-frequency nw4r-snd sleep/recv
  events (LR in `[0x800A0000,0x800B4000)`) filtered out; last ~300 entries dumped at the first
  stall. Thread-table dump now also prints each `wait_q`'s head/tail words.
- Trace call sites: `OSSleepThread` (`os_sleep.cpp`), `OSWakeupThread` /
  `WakeupThreadQueue` incl. the wake-nothing case (`os_scheduler.cpp`), `MsgQueueOp`
  send + blocking-recv (`os_message.cpp`).

Goal: see the exact last sleep/wake sequence before the queue goes quiet — specifically whether a
`wake` on `~0x804294A4` fired with an empty queue (lost wakeup) or the wake never happened at all.

## M4 — deferred DVD completions  (built + hardware-tested — DID NOT fix the deadlock)

Hypothesis (Fable static audit of `dvd.cpp`, 2026-09-03): DVD async callbacks run **inline**
before the entry point returns, unlike NAND/ISFS which already use a deferred queue, so a caller
doing `DVDReadAsync` then `OSSleepThread(queue)` loses the wakeup. This is a **real HLE defect**
worth keeping fixed, but **the M6 hardware test disproved it as the primary Single-Player
deadlock fix** — the deadlock reproduces identically with the deferred queue active. Not a
confirmed root cause. `MKW_VITA_DVD_DEFER` is therefore back to **default 0** for the M7
baseline; the deferred-queue code + kill switch stay as opt-in infrastructure. (The "dvd_pump"
marker is emitted only every 64th non-empty pump, so its absence in the log is not proof the
queue was unused — replace with atomic counters if DVD diagnostics are needed.)

Patches applied (`runtime/src/hle/storage/dvd.cpp`, `runtime/src/hle/os/os_alarm.cpp`):
- **Patch A — deferred DVD completion queue.** Mirrors `nand_async.cpp`: the host read still runs
  synchronously, but the command-block state + callback are published only when the guest yields.
  `QueueDvdCompletion` marks the block `DVD_STATE_BUSY` and enqueues; `DvdProcessPendingCallbacks`
  publishes `DVD_STATE_END` + runs the callback on a scratch `CpuContext` (`CpuContextScope`).
  Wired into `ProcessAlarmQueue` next to `NandProcessPendingCallbacks`, so the scheduler idle
  loop drains it. Kill switch `MKW_VITA_DVD_DEFER=0` restores inline behaviour; pre-scheduler
  boot (`0x800000E4` running-context still 0) also stays inline. Re-entrancy depth guard keeps
  callback chains flat.
- **Patch A safety net** — `DVD::GetCommandBlockStatus` (`0x80162A88`) HLE override drains the
  pump before returning the state, for any caller that spins on the status without yielding.
- **Patch B** — `CopyToGuestAsDma` (and the boot-time FST publish loop) do one `memcpy` into the
  flat guest backing (`Memory::GetPointer`) instead of a `Memory::Write8` per byte; per-byte path
  kept as fallback for unmapped/aliased ranges. Cuts the multi-million checked-write loop on a
  ~4 MB SZS.
- **Patch C — host-file LRU + read-in-place.** 8-slot `FILE*` cache keyed by `DVDFileEntry*`
  (`AcquireHostFile`), and `DvdReadIntoGuest` `fread`s straight into the guest pointer, dropping
  the `std::vector` temp + second copy in `DVDReadPrio` / `DVDReadAbsAsyncPrio`. Cache is
  invalidated (`DropHostFileCache`) after every `g_fileEntries` mutation
  (`BuildAndPublishRuntimeFst`, `RegisterFileEntry`) so stale entry pointers can't be used.
  Unmapped/aliased dest falls back to the buffered path.

Not applied from the audit: Patch D (I/O helper thread) — do after A/C are green on hardware. The
build-flags patch (`005-makefile-flags.patch`) is **rejected**: as written it replaces
`MKW_TRANSLATED_CXXFLAGS` with a list that drops `$(CXXFLAGS)` entirely (loses every `-I`, `-D`,
`-std=gnu++20`, `-fexceptions`) and forces shards from `-Os` to `-O2` against the documented
Vita-loader text-budget constraint; it also appends flags to an object-list make variable and
defines `MKW_RUNTIME_CXXFLAGS`/`MKW_RUNTIME_LDFLAGS` that nothing in the Makefile consumes. It is
written against an assumed Makefile structure. Clock-444 is already done in `main_vita.cpp`.
Patch `004-os-scheduler-integration.patch` is also written against a hallucinated
`os_scheduler.cpp` (references `SelectThread_801A9C08`, `AudioTickPending()`,
`nand_internal.h` include — none present); the equivalent wiring is done correctly here by
pumping `DvdProcessPendingCallbacks` inside `ProcessAlarmQueue`, which the real idle loop
(`os_scheduler.cpp:244`, inside `while (pending == 0)`) calls every iteration.

## M5 — Fable audit patch C  (built + hardware-tested, superseded)

Applied Fable's host-`FILE*` LRU + read-in-place (`DvdReadIntoGuest`) on top of M4. Kept.

## M6 — DVD defer toggle  (built + hardware-tested — REGRESSION observed, cause understood)

Made `MKW_VITA_DVD_DEFER` a real Vita toggle: compile `-D` (default was 1) > marker file
`ux0:data/wiicompiled-vita/dvd_defer_off` > default. (`getenv` does nothing for a LiveArea vpk.)

**Hardware M6 result — two decisive facts:**

1. **The "performance regression" is mostly the M3 profiling itself.** The runtime.log was ~30 k
   lines, ~28 k of them `sched_trace`. `GuestStallWatchdog::Poll()` runs on the **render worker
   (USER_1)** (`vita/gx_backend.cpp`, the `g_renderWake.wait_for` timeout branch). Each stall
   dumped the thread table + ~300 `sched_trace` lines to the line-buffered `ux0:` log = hundreds
   of blocking `sceIoWrite` on USER_1 -> render worker stalls -> producer `prior_wait_us` climbs
   (frames of **7.8 s** observed) -> watchdog fires -> another dump. Self-amplifying. These frame
   times are a logging artifact, **not** game performance. Removing the destroyed frames, the
   card screen is ~328 ms/frame (~3 fps) — i.e. M1's real ~296 ms gain held; the old 1.5 fps
   regression did **not** actually return.

2. **The deadlock is `EGG::AsyncDisplay` / VI retrace, not DVD.** The M3 sched_trace named the
   queue: root `0x80347498` sleeps on `0x804294A4` from `LR 0x8020FE50`
   (`EGG::AsyncDisplay::beginFrame` @ 0x8020FE24). The normal wake comes from `LR 0x80210078`
   (`EGG::AsyncDisplay::postVRetrace` @ 0x80210024), invoked as the VI post-retrace callback.
   Normal loop: `beginFrame -> OSSleepThread(0x804294A4)` / VI retrace / post-retrace callback
   `-> postVRetrace -> OSWakeupThread(0x804294A4)`. Works for hundreds of frames; after the
   Single-Player transition the wake stops arriving and `Scene Enter` never prints. `dvd_pump`
   was 0. The idle scheduler **already** calls `VI_HLE_PollRetrace(cpu)` inside `while (pending
   == 0)` (`os_scheduler.cpp`), so the question is *why does `VI_HLE_PollRetrace` keep being
   called but stop producing `EGG::AsyncDisplay::postVRetrace`*.

Aurora is **not** the blocker: `render_large phase=completed` for the final 1326-draw frame
(~927 ms). Packet queue, EFB copies, EndFrame, Swap all complete; then the guest just sits.

## M7 — clean baseline  (built: `...-m7-clean-baseline.vpk`, SHA `09d51fb4e2...`)

Purely diagnostic hygiene, no functional fix:
- `guest_stall_watchdog.cpp`: `sched_trace` ring buffer + its ~300-line stall dump now behind
  `#if MKW_VITA_SCHED_TRACE` (default 0 — compiled out). `TraceSchedEvent` is a no-op.
  `DumpGuestThreadTable` fires **once per session** (was once per stall-serial). `Poll()` still
  logs the one-line `guest_watchdog_stall` at most 1/s, plus a `root_wait_q` line.
- `fiber_manager.cpp`: `fiber_slice` threshold back to 20 ms / first-8-then-pow2.
- `Makefile.vita`: `MKW_VITA_DVD_DEFER ?= 0`.
- Kept: `MKW_VITA_DISABLE_MOVIES ?= 1`, DVD patch B (memcpy) + patch C (file LRU).
- Baseline build flags this session: `MKW_TRANSLATED_BUILD_DIR=build/vita/mkwii_translated_neon_os`,
  `MKW_TRANSLATED_OPT='-Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard'`,
  `MKW_VITA_LYT_DIRECT=0 MKW_VITA_LYT_FAITHFUL=1` (the visually-stable bring-up LYT config).

## M8 — VI retrace diagnostics + RAII guard  (built: `...-m8-vi-retrace-trace.vpk`, SHA `461743b1c8...`)

`runtime/src/hle/vi.cpp`:
- **`s_inAdvanceRetrace` is now RAII / exception-safe.** Before, the guard was a bare
  `exchange(true)` at entry and `store(false)` at the very end. If `InvokeIndirectCpu(preCb)` /
  `InvokeIndirectCpu(postCb)` / `OSWakeupThread` / `VI_HLE_PresentFrame` threw
  (`Memory::AccessViolation` from a guest callback) or a fiber switched away and unwound
  elsewhere, `s_inAdvanceRetrace` stayed `true` forever -> every subsequent `AdvanceRetrace`
  early-returns -> no more post-retrace callback -> `EGG::AsyncDisplay::postVRetrace` never runs
  -> root deadlock. Prime suspect for the Single-Player hang. Now a `GuardReset` destructor
  always clears it.
- `AdvanceRetrace` returns `bool` (`false` = skipped by the re-entry guard).
- `AdvanceDueRetraces` now `break`s on a skipped `AdvanceRetrace` and does **not** set
  `advancedAny` for it (was unconditionally `true`, which both lied about progress and spun the
  catch-up loop `maxToProcess` times against a stale `lastRetrace`).
- Low-overhead atomic counters (`g_viDiag`): `poll_total`, `due_total`, `advance_enter`,
  `advance_reentry`, `advance_complete`, `pre_cb_total`, `post_cb_total`,
  `post_cb_no_system` (sSystem==0 guard hit), `post_cb_zero` (postCb==0),
  `retrace_wake_total`, plus `last_post_cb`, `last_post_cb_us`, `last_retrace_count`,
  `last_advance_complete_us`. **Zero `RT_LOGF` per retrace.**
- `VI_HLE_LogDiagnostics()` emits one `vi_stall …` line; the watchdog calls it once per second
  while a stall is active (alongside the existing `guest_watchdog_stall` one-liner).

Mapping (from M6 sched_trace + MAP.txt):
| addr | symbol |
|---|---|
| `0x8020FE24` | `EGG::AsyncDisplay::beginFrame` |
| `0x8020FE50` | sleep point (`OSSleepThread(0x804294A4)`) |
| `0x80210024` | `EGG::AsyncDisplay::postVRetrace` |
| `0x80210078` | wake point (`OSWakeupThread(0x804294A4)`) |
| `0x804294A4` | AsyncDisplay main-thread wait queue |
| `0x80386BC0` | `kViRetraceQueueAddr` (VIWaitForRetrace queue — the DVD thread sleeps here, not root) |

### What the M8 log must distinguish (from `vi_stall`)
- **CASE A** `in_advance=1` stuck, `retrace_count` frozen, `advance_reentry` climbing => the
  `s_inAdvanceRetrace` bug (RAII fix should have prevented it — if it recurs the guard is being
  defeated by a fiber-switch-away, not an exception).
- **CASE B** `in_advance=0`, `poll_total` climbs, `due_total` flat, `last_retrace_age_us` huge
  => VI deadline/timeline problem (`lastRetrace` not advancing / interval wrong).
- **CASE C** `retrace_count` climbs, `post_cb=0` => callback de-registered / pointer lost.
- **CASE D** `retrace_count` climbs, `post_cb!=0`, `sSystem=0` => the `sSystem != 0` guard in
  `AdvanceRetrace` is deliberately skipping the post-retrace callback.
- **CASE E** `post_cb_total` climbs but no `wake 0x804294A4` => `postVRetrace` is entered but its
  internal virtual call / condition prevents the `OSWakeupThread`; analyse `func_80210024`.
- **CASE F** `post_cb_total` flat, `retrace_count` climbs => post-callback pointer/state wrong.

### M8 HARDWARE RESULT — RAII guard is NOT the bug; USER_0 stops calling `VI_HLE_PollRetrace`

`vi_stall` counter progression (SP-menu stall):
- `in_advance=0`, `advance_reentry=0`, `advance_enter == advance_complete` the whole time -> the
  `s_inAdvanceRetrace` RAII fix is correct and **not** what deadlocks. **CASE A ruled out.**
- `post_cb=0x8020FCD4` (`EGG::PostRetraceCallback`, wraps `postVRetrace`), `sSystem=0x802A4080`
  non-null, `post_cb_no_system=0`, `post_cb_zero=10` (constant), `post_cb_total` tracks
  `retrace_count` -> callback stays registered and fires. **CASE C/D/F ruled out.**
- `retrace_count`, `poll_total`, `advance_*`, `post_cb_total` **all climb together to
  retrace_count=2697 / poll_total=1198, then FREEZE forever**; only `last_retrace_age_us` keeps
  climbing (to 74 s). So `VI_HLE_PollRetrace` **stops being called** — the scheduler idle loop on
  USER_0 stops running. This is the real mechanism, adjacent to CASE B but the cause is USER_0
  ownership, not a VI timeline bug.
- Coincident: `fiber_slice ... to=0x90112660 from_entry=0x800A49C0(SoundThread) to_entry=0x8024373C
  elapsed_us=1120420` right before the freeze, and the once-per-session thread table shows
  `0x90112660` (prio 6, MEM2, entry `EGG::Thread::Run`) as `state=1` **READY** (linked on the
  prio-6 run queue `0x803477E0`) while every other thread is `state=4`. `0x90112660` executes
  `DvdThread_main` (`func_80008D18`) — from M6 it slept at `DvdThread_main+0xEC`
  (`VIWaitForRetrace`, `OSSleepThread(0x80386BC0)`). `DvdThread_main` loop:
  `GetDriveStatus(); switch; if (*(state+72)==5) return; VIWaitForRetrace(); loop`. It is now
  READY/running and not reaching `VIWaitForRetrace` -> a busy path in its status-switch that
  never yields, monopolising USER_0.
- `render_large phase=completed serial=774` (947 ms) — Aurora still finishes the final frame.
- The watchdog `os_running`/`current_guest` are from the frozen producer snapshot, so they do
  **not** identify who owns USER_0 during the deadlock. -> M9.

### Secondary architectural note (validate after M9)

`EGG::AsyncDisplay::postVRetrace` (`func_80210024`) only calls `OSWakeupThread(this+88 =
0x804294A4)` when `*(this+96) != 0`. `beginFrame` (`func_8020FE24`) never writes `+96`; the
**only** writer is `EGG::AsyncDisplay::startSyncNTSC` (`0x8020FD8C`), which also arms a periodic
`OS::SetPeriodicAlarm` (`0x801A08E0`) whose handler is `EGG::AsyncDisplay::HandleAlarmWrapper`
(`0x8020FD10`). So the retrace->root wake depends on `startSyncNTSC` having been called and
`+96` staying set across the scene transition. If M9 shows USER_0 is *not* monopolised, check
`*(AsyncDisplay+96)` and whether `startSyncNTSC` / the periodic alarm survive the SP transition.

## M9 — live USER_0-ownership tracking  (built: `...-m9-live-fiber-scheduler.vpk`, SHA `391f9e49d2...`)

Pure diagnostics, minimal overhead, no functional change. Goal: identify **live** who owns USER_0
when `vi_poll_total` stops advancing.

- `guest_stall_watchdog.{h,cpp}`: live atomics (`g_live`, `sceKernelGetProcessTimeWide` clock):
  - `RecordFiberSwitchBegin(from,to,fromEntry,toEntry)` / `RecordFiberSwitchEnd()` — called in
    `GuestFiberManager::SwitchToThread` immediately around the real `co_switch` / `SwitchToFiber`.
    Stores `switch_seq`, `switching` (in-progress flag), `active_from/to`, `active_from/to_entry`,
    `switch_begin_us`, `last_return_us`. No logging per switch.
  - `RecordSchedulerTick(kind)` — kind 1 in `SelectThread_801a9c08` entry, kind 2 per idle-loop
    iteration (`while(pending==0)`), kind 3 in `VI_HLE_PollRetrace`. Stores
    `scheduler_select_total`, `scheduler_idle_total`, `vi_poll_total`, `last_scheduler_us`,
    `last_idle_us`, `last_vi_poll_us`.
- Watchdog `Poll()` (USER_1), once per second during a stall, now also:
  - reads **live** guest memory: `OSRunningContext 0x800000E4`, `OSCurrentContext 0x800000D4`,
    running/current thread `state` + `wait_q` + `prio`, scheduler `pending 0x80386920` — emits
    `guest_watchdog_live …` (distinct from the frozen `RecordFrame` snapshot).
  - emits `guest_live switch_seq=… switching=… from=… to=… from_entry=… to_entry=… slice_age_us=…`
  - emits `guest_live sched_select_total=… sched_idle_total=… vi_poll_total=… last_*_age_us=…`

### What the M9 log must distinguish
- **CASE 1** `switching=1`, `to=0x90112660`, ... => guest thread monopolises USER_0.
- **CASE 2** USER_0 wedged elsewhere.
- **CASE 3** `sched_idle_total` climbs but `vi_poll_total` does not => scheduler/idle-loop bug.
- **CASE 4** `vi_poll_total` climbs but VI counters do not => reopen VI path.

### M9 HARDWARE RESULT — CASE 3. VI is starved inside the idle loop by audio wakes.

- `fiber_switch_in_progress` stays 0; `switch_seq`, `sched_select_total`, `sched_idle_total` all
  climb fast for tens of seconds => USER_0 and the guest scheduler are **alive**. Fiber
  monopolisation (M9 CASE 1) **disproven**.
- `vi_poll_total` frozen at **1212** for the whole stall; `last_vi_poll_age_us` climbs.
- `guest_watchdog_live` (LIVE guest reads, not the frozen `RecordFrame` snapshot) shows the
  scheduler bouncing: `os_running=0x00000000` (idle thread, `os_current=0x803478B0`) <->
  `os_running=0x802ECEB0 run_prio=4` (nw4r SoundThread), with `pending=0x08000000` almost
  always set.
- `pending=0x08000000` = bit `31-27` => priority 4 (`MarkRunQueuePending: pending | (1<<(31-prio))`).
  prio 4 = SoundThread `0x802ECEB0`. Confirmed both by the bit and by `os_running` directly.

**Static proof (os_scheduler.cpp idle loop, `while (pending == 0)`):**
```
ProcessSleepTimers(cpu);
Audio_HLE_Poll(cpu);                 // completes up to kMaxBlocksPerTick=4 AI-DMA blocks;
                                     // each runs the guest AI-DMA callback -> __AXOutNewFrame
                                     // -> wakes SoundThread (prio 4) -> pending = 0x08000000
if (pending != 0) break;             // <<< BREAKS HERE, BEFORE VI
VI_HLE_PollRetrace(cpu);             // starved: never reached while audio keeps waking SoundThread
...
VI_HLE_WaitForNextRetracePoll();     // also skipped -> idle loop does not even sleep -> spins hot
```
`Audio_HLE_Poll` -> `Audio_HLE_Tick(ConsumeAudioPollDeltaMicros())`; the delta is real wall-clock
since the last poll (capped 100 ms). Running the AI-DMA callback (a full AX voice mix, ~hundreds
of PBs) costs more guest CPU than one ~3 ms block lasts, so once the SP transition has left an
audio backlog every idle pass completes >=1 block and re-wakes SoundThread -> the `break` fires
every pass -> `VI_HLE_PollRetrace` is never reached -> `retrace_count` frozen ->
`EGG::AsyncDisplay::postVRetrace` never runs -> root `0x80347498` (asleep on `0x804294A4` in
`beginFrame`) is never woken -> black screen. Before the SP transition root itself produced
frames and the frame-pacing loop also serviced audio, so the idle loop was never the sole
audio+VI pump and the trap never latched.

## M10 — scheduler time-event fairness  (built: `...-m10-vi-idle-fairness.vpk`, SHA `e542e2296d...`)

`runtime/src/hle/os/os_scheduler.cpp` idle loop: **removed the early `break` after
`Audio_HLE_Poll`.** Now each idle pass services *every* due time source once —
`ProcessSleepTimers`, `Audio_HLE_Poll`, `VI_HLE_PollRetrace`, `ProcessTimerEvents`,
`ProcessAlarmQueue` — and only then checks `pending` and reschedules / sleeps. A wake from one
hardware time source no longer prevents the others (VI retrace above all) from running that
pass. Order kept audio-before-VI to match the previous Wii-ish interleave; the fix is the
general fairness, not a reorder.

Diagnostics added (`g_live`, no per-event logging): `idle_audio_pending_total` (passes where
`Audio_HLE_Poll` left `pending` set), `idle_vi_after_audio_total` (passes where `VI_HLE_PollRetrace`
still ran despite that — the fix working), `idle_break_after_service_total`. Emitted on the
`guest_live sched_*` watchdog line.

**Audio backlog note (not touched in M10, flagged for a separate fix if M10's log shows it):**
`Audio_HLE_Poll` may stay tens of seconds behind wall-clock after a slow period and keep
completing blocks / waking SoundThread every poll. M10 makes that survivable (VI still runs),
but if the log shows `idle_audio_pending_total` ~= `sched_idle_total` indefinitely, add a
bounded/coalescing catch-up in `Audio_HLE_Tick` (faithful to the hardware DMA cadence),
separately and documented.

### M10 success criteria (from the SP transition)
1. `vi_poll_total` keeps advancing; 2. `retrace_count` keeps advancing;
3. `last_vi_poll_age_us` stops climbing for seconds; 4. `post_cb_total` keeps advancing;
5. AsyncDisplay keeps getting postRetrace; 6. root `0x80347498` woken from `0x804294A4`.

### M10 HARDWARE RESULT — PASS. VI/audio starvation fixed; black-screen blocker cleared.

The black screen after "Single Player" now **advances** — a subsequent scene/screen renders
(with a lot of graphics still missing). Log confirms the fix: `sched_idle_total=1882`,
`vi_poll_total=1882` (VI serviced every idle pass), `idle_audio_pending_total=994`,
`idle_vi_after_audio_total=994` (every pass where audio made SoundThread runnable, VI still
ran). Card-menu perf also improved: ~272 ms/frame (pre-M8) → ~122-140 ms/frame (~7-8 FPS).
**Do not revisit the old VI deadlock without new evidence.** Do not restore the `break` after
`Audio_HLE_Poll`; keep the order: temporal sources → Audio → VI → timers/alarms → only then
scheduler break/reschedule. `Audio_HLE_Tick` bounded catch-up is still a possible later fix
(`idle_audio_pending_total` ≈ half of `sched_idle_total` here — survivable, not urgent).

## Native THP (Motion-JPEG) decode  (ACTIVE — built into the M11 combined vpk)

`runtime/src/hle/thp_video_decode.cpp` — `PPC_NATIVE_OVERRIDE_VOID(801B3BAC, NativeThpVideoDecode)`
replaces the RVL SDK software MJPEG decoder `THPVideoDecode`.

RE of `func_801B3BAC` / `func_801B3E6C` (`__THPReadFrameHeader`) / `func_80552BA4`
(`THP::VideoDecode`): each THP frame handed to `THPVideoDecode` is a **complete baseline JPEG
bitstream** (`FF D8` … DQT/DHT/SOF0/SOS/entropy/`FF D9`), 3-component YCbCr. Args:
`r3` = guest ptr to the JPEG frame, `r4/r5/r6` = guest dst Y/U/V planes, `r7` = SDK scratch
(unused by the override). No size arg — the override scans for `FF D9` (safe: real EOI only;
entropy `FF` is always stuffed `00` or a marker). Return code in `r3` (0 = ok).

The guest dst planes are **GX I8-tiled** (8×4 tiles, 32 B/tile, row-major — matches
`ReadTiledI8` in `vita/aurora_packet_renderer.cpp`). The override decodes with turbojpeg
(`tjDecompressToYUVPlanes`, thread-local `tjhandle`) into linear temp planes, then re-tiles into
the guest buffers. Bypasses the whole translated `__THPDecompressYUV` (0x801B4A58) + paired-single
IDCT chain — so the GQR2-5 setup in `os_thread.cpp` `IsThpVideoDecoderEntry` no longer matters for
the decode itself (the decoder *thread* 0x805529A8 still runs translated for its message-queue
plumbing and calls straight into the override).

The file is guarded by `#if defined(MKW_VITA_NATIVE_THP) && MKW_VITA_NATIVE_THP`; **`Makefile.vita`
now sets `MKW_VITA_NATIVE_THP ?= 1` and `LIBS += -lturbojpeg`.** `make generate` was re-run — the
translator (preprocessor-blind regex scan of `runtime/src`) now excludes 0x801B3BAC from base
translation: `generated/functions/func_801B3BAC.cpp` is stale/unused, no shard compiles its body,
no `RECOMP_REGISTRATION` for it; the call site `InvokeDirectCpu<0x801B3BACu>` dispatches through
the registry to `NativeThpVideoDecode`. Only 8 shards + registration/dispatch shards + the two
native files recompiled; the rest of `mkwii_translated_neon_os` is untouched.

Built into the M11 combined vpk with `MKW_VITA_DISABLE_MOVIES=0` (movies re-enabled — the
`task_thread.cpp` "THP playback disabled" branch is compiled out; verified absent from the elf).
`turbojpeg` symbols + `NativeThpVideoDecode` + its registration ctor verified linked.

**Gotcha hit:** the Makefile has no `-D` flag-change tracking, so flipping `MKW_VITA_NATIVE_THP` /
`MKW_VITA_DISABLE_MOVIES` does NOT rebuild `thp_video_decode.o` / `task_thread.o` on its own —
`rm` those two objects (only files referencing those macros) before the build.

**To revert to translated THP:** `MKW_VITA_NATIVE_THP=0` in Makefile + `make generate` again
(re-includes 0x801B3BAC) + `rm` the two objects. Log marker when active:
`thp: native decode n=… WxH subsamp=… jpeg=…`.

## M11 — remove frame draw truncation + native THP  (COMBINED vpk)

- `...-m11-full-draw-capacity.vpk` SHA `84901a178f2f46e42ce6fb7cd5b62f53e76347cabab4283e7a129e5aff75af28`
  — draw-cap fix only, `MKW_VITA_DISABLE_MOVIES=1`, no re-translation. Kept for isolation.
- **`...-m11-drawcap-thp-native.vpk` SHA
  `2e70c6bd724e94e558e0e7c5163c1ab346bbf30c7069b3737e7b4082e5faffaf`** — draw-cap fix **+**
  native THP decode **+** movies re-enabled (`MKW_VITA_DISABLE_MOVIES=0 MKW_VITA_NATIVE_THP=1`),
  after `make generate`. This is the build to hardware-test (user chose the combined vpk).

Post-M10 the game reaches a much heavier scene: `producer_frame` shows `requested_draws≈3382`
but `stored_draws=2048`, `begin_cap≈1334` rejected, `dropped≈9394` vertices — every frame. The
truncated packet is the prime suspect for the missing graphics on hardware.

**Root cause:** `vita/gx_backend.cpp` `constexpr kMaxFrameDraws = 2048` / `kMaxFrameVertices =
16384` — a self-imposed bridge bound, not an ABI/renderer limit. Everything else derives from
those two constants via `.array.size()` (single source of truth): `FrameGeometry.draws/vertices/
pnMtxRefs`, `g_renderVertices`, all `>=`/`+n>` capacity checks, `drawCount`/`firstVertex` are
`u16` (fine to 65535). The Aurora consumer processes draws one at a time into growable
`std::vector`s — no cap there. `sizeof(GeometryDraw)=760` (dominated by `DrawTransform`'s
per-draw `posMtx[10][12]`=480 B palette copy), `sizeof(FrameGeometry)=1.97 MiB` at 2048.

**Fix:** `kMaxFrameDraws` 2048 → **4096**, `kMaxFrameVertices` 16384 → **32768**. Cost: two
`FrameGeometry` instances (`g_gx.geometry`, `g_pendingFrame.geometry`) + `g_renderVertices` →
**+~4.1 MiB static** (`.data`; `sizeof(FramePacket)` 1.97 → 3.75 MiB, verified
`g_pendingFrame`=0x3c0ba8, `g_renderVertices`=0xc0000). `static_assert` ceiling 2 → 5 MiB.
Not dynamic/chunked: RAM cost is acceptable, arrays are file/BSS globals (never stack).

**Logging:** folded `frame_capacity_summary` into the periodic `producer_frame` line, which now
carries `requested_draws` / `begin_cap` (`immediateDrawCapacityFailures`) / `dropped`
(`droppedVertices`) and also fires whenever `begin_cap != 0`. `frame_draw_capacity` trace
(≤8 total) and all counters kept.

Build (combined): `make -f Makefile.vita mkw-first-boot-package
MKW_TRANSLATED_BUILD_DIR=build/vita/mkwii_translated_neon_os
MKW_TRANSLATED_OPT='-Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard'
MKW_VITA_LYT_DIRECT=0 MKW_VITA_LYT_FAITHFUL=1 MKW_VITA_DISABLE_MOVIES=0 MKW_VITA_DVD_DEFER=0
MKW_VITA_NATIVE_THP=1` — preceded by `make generate` and `rm` of `thp_video_decode.o` +
`task_thread.o` (no flag tracking). ~8 translated shards + registration/dispatch + gx_backend.o
+ the two native files recompiled; the bulk of neon_os is preserved.

### M11 HARDWARE RESULT — PASS (draw truncation fixed)

`kMaxFrameDraws 4096` removed the cap: the post-Single-Player scene issues ~3381 draws / ~23032
vertices and the producer captures all of them — `requested_draws=3381 draws=3381 begin_cap=0
dropped=0`. `transform_fail=0`, `raw_fail=0`, NDC back to sane ranges, no old graphic overflow.
Aurora completes the heavy frames end to end (~330-350 ms/frame). **Graphics still incomplete on
that screen** — cause found in M12 below (Aurora streaming-arena overflow, not the producer).

## M12 — graphics-correctness: Aurora streaming-arena overflow  (built: `...-m12-graphics-correctness.vpk`, SHA `079eedeaa4…`)

**Root cause of the missing graphics (found, fixed).** `vita/aurora_packet_renderer.cpp`
`StreamingArenaConfig` was `vertexBytes = 512 KiB`, `indexBytes = 64 KiB`, `slots = 3`. The
arena is filled linearly by `enqueue_draw` and **never recycles within a frame** (`flush()`
uploads staged bytes but does not reset `slot.voff`; only `begin_frame` resets). `CanonicalVertex`
is 120 B, so 512 KiB holds ~4369 vertices; every `upload_vertices` past that returns `{}` →
`enqueue_draw` fails `StreamingOverflow` → the draw is silently dropped. Log evidence:
`aurora_frame=960 logical_draws=407 physical_draws=18 stream_bytes=524160 stream_overflow=357905`
— ~95 % of draws never reached GXM. The heavy scene needs ~2.8 MiB of vertices per frame.

**Fix:** `vertexBytes = 3 MiB`, `indexBytes = 512 KiB`, `slots = 2` (the render worker processes
one frame start-to-finish; 2 slots cover the Vita's one-frame GPU read latency). +~5.3 MiB
dynamic VBO (`VGL_MEM_RAM`). **Risk:** if the vitaGL pool cannot allocate it the log shows
`init_marker=aurora_streaming_arena phase=failed` and Aurora renders nothing — dial the size
back or grow the pool if that appears.

**Diagnostics added (all `#if MKW_TARGET_VITA`, low overhead):**
- `AuroraPacketSubmitResult.prepareError` — the `gfx::PrepareDrawError` (0 None … 6 StreamingOverflow
  … 7 PipelineFailed, 255 never-enqueued) is now returned instead of discarded.
- `m12_submit serial=… presented=… none/invalid/decode/xform/toomany/lineexp/overflow/pipeline/noenqueue=…`
  once per logged frame (serial ≤ 8 or % 120): the submit-rejection histogram.
- `m12_draw serial=… idx=… lr=… prim=… verts=… proj=… pnmtx=… vp=… sc=… ndc_x/ndc_y=… proj_diag=…
  tex=… fmt=… WxH tevmode=… tevsimple=… texgen=… submitted=… prepare_err=… tex_unsupported=…` —
  first 16 *unusual* draws per session (not submitted / texture unsupported / texgen fallback /
  oversize NDC). `lr` is the guest GXBegin return address, mappable via
  `vita/tools/decode_gx_begin_hot.py` / the guest symbol table.
- `GeometryDraw` gained `guestLr` (set from `GX_HLE_RecordBeginCaller` via
  `GxBackend::SetGuestBeginLr`; immediate-mode path only, G3D/FIFO draws leave it stale) and
  `pnMtxIndex` (= `g_gx.currentMtx`). +8 B/draw × 2 instances = +64 KiB static.

### M12 HARDWARE RESULT — native Vita crash in the texture upload path

`psp2core-…-GPUCRASH.psp2dmp`, symbolized against the M12 ELF. Native, not guest PPC:
`AuroraPacketRendererSubmit` → `Renderer::create_texture` (`aurora_packet_renderer.cpp:583`) →
`TextureCache::get_or_upload` (`vita_renderer.cpp:24`, `vita_texture_cache.cpp`) →
`glTexImage2D` → vitaGL-speedhack `gpu_alloc_mapped_for_gpu` /
`gpu_alloc_mapped_aligned_for_gpu_inner` → `write_rgba8888` → SceLibKernel. **DFAR ≈ 0x3
(near-null)** — a write into a failed/NULL texture allocation. The M12 streaming-arena bump
(+5.3 MiB) plus M11 pushing every scene draw through raised total GPU pressure; the texture
cache was already ~10.7/12 MiB before the transition, and vitaGL's speedhack build is
`-DSKIP_ERROR_HANDLING` + `-DHAVE_TEX_CACHE` — it does not fail cleanly on OOM.

Null-unsafe vitaGL spots found (cannot be patched without rebuilding the pinned "tested"
archive, so worked around from our side): `gpu_alloc_mipmaps()` (`gpu_utils.c:857`,
`vgl_memcpy(texture_data=NULL, …)` after `gpu_alloc_mapped_for_gpu`), and a bound texture with
`status != TEX_VALID` / `data == NULL` reaching a draw (GPU crash).

## M12.1 — texture-OOM hardening  (built: `...-m12_1-texture-oom-hardening.vpk`, SHA `19d42d84cc…`)

**Config:** `MKW_VITA_DISABLE_MOVIES=1` (movies OFF, THP dormant), `MKW_VITA_NATIVE_THP=1`,
`MKW_VITA_DVD_DEFER=0`, `MKW_VITA_LYT_DIRECT=0`, `MKW_VITA_LYT_FAITHFUL=1`. No re-translation.

Our-code-only (vitaGL archive untouched):

1. **`vita_texture_cache.cpp` pre-eviction (before any vitaGL call).**
   `estimate_gpu_bytes(desc)` — conservative: per level `VGL_ALIGN(w,8)*h*4`, mip chain if
   generateMipmaps, `+20% +64 KiB`. `pre_evict(est, frame, key)` evicts LRU until
   `bytes_ + est + 512 KiB ≤ budget_`, **never evicting a texture already used this frame**
   (its GL id may sit in an enqueued Aurora draw). If it still will not fit → return
   `InvalidHandle` **without calling `glTexImage2D`** (deterministic; vitaGL never sees an
   allocation it cannot serve). Entry accounting switched to the estimate (`e.bytes = est`) so
   `bytes_` tracks real GPU footprint, not the CPU decode size.
2. **Post-upload backing check.** After the `glTexImage2D` loop,
   `vglGetTexDataPointer(GL_TEXTURE_2D) == nullptr` ⇒ vitaGL OOM'd (fragmentation etc.) →
   `glDeleteTextures`, return `InvalidHandle`. Fresh GL slots have `data == NULL`, so this is
   reliable.
3. **`glGenerateMipmap` removed** from the cache path — it routes into the null-unsafe
   `gpu_alloc_mipmaps`; menu textures render ~1:1 and don't need a generated chain.
   `hasMipmaps` now = "≥2 real levels uploaded".
4. **Failure reaches Aurora unchanged:** `create_texture` → `InvalidHandle` →
   `AuroraPacketSubmitResult.textureUploadFailed = 1`, `textured = false`, draw proceeds
   **untextured** (already the existing behaviour — a draw without texture, not a crash).
5. **Budget 12 → 10 MiB** (`aurora_packet_renderer.cpp`). Graceful-OOM + eviction, not more
   memory. The M12 arena (3 MiB / slots 2) and M11 draw cap are unchanged.
6. **Telemetry:** `m12_1_tex serial=… cache_bytes=… budget=… high=… entries=… alloc_fail=…
   pre_evict=… pre_evict_bytes=… requested=… pool_ram_free=… pool_vram_free=… pool_slow_free=…`
   every logged frame; `m12_1_tex_fail w=… h=… fmt=… mips=… source=… est=… cache_bytes=…
   budget=…` on the first alloc failure then power-of-two. `m12_draw` gained `tex_upload_fail`.

**Not hardware-validated:** whether 10 MiB + pre-eviction fully clears the OOM on the heavy
scene, whether pre-eviction thrashes (watch `pre_evict` vs `alloc_fail` in `m12_1_tex`), and
whether losing generated mipmaps causes visible shimmer on the 3D menu models. If `alloc_fail`
stays high with `pool_*_free` showing plenty free, it is fragmentation — then the remaining
option is rebuilding the speedhack vitaGL archive with the null checks + a real failure flag.

## M12.2 — Aurora incremental ABI dependency fix

Second M12.1 hardware core (Data abort, DFAR 0x00000011) was symbolized against the exact 22:43 Aurora-speedhack ELF. The native crash is in BufferPool::gl_id() / its std::unordered_map lookup, called by Renderer::draw(). Core memory shows an impossible empty-map state: bucket_count=1, element_count=0, but _M_before_begin._M_nxt=0x0000000D. This is host-side object corruption, not guest PPC and not a missing BufferPool handle.

Root cause confirmed in the build system: M12.1 enlarged TextureCache by adding telemetry members. Renderer stores TextureCache textures_ immediately before BufferPool buffers_, so that header change moves buffers_. Aurora objects are compiled with -MMD -MP, and vita_renderer.d correctly depends on vita_texture_cache.hpp, but Makefile.vita did not include the packet-renderer/Aurora-gfx dependency files. Consequently vita_texture_cache.o was rebuilt at 22:42 while vita_renderer.o was still the 09:29 object compiled against the old class layout. TextureCache writes therefore landed in the old BufferPool offset and corrupted its unordered_map.

Fix: Makefile.vita now includes the Aurora packet-renderer and all Aurora Vita gfx .d files. Rebuilding without cleaning correctly recompiled all header-affected objects (including vita_renderer.cpp and vita_draw_adapter.cpp) before relinking. Do not add defensive BufferPool-map hacks: they would only mask this ABI mismatch. M10 fairness, M11 draw capacity, M12 streaming-arena changes, and M12.1 texture-OOM hardening are preserved. Hardware validation pending.

## M12.3 — VRAM allocator headroom / EFB allocation guard

M12.2 fixed the stale-Aurora-object ABI mismatch, but the next hardware run still crashed after
Single Player. This core is **not** the BufferPool/unordered_map failure: crash thread is the
native render pthread (TID 0x40010139), stop reason is Data Abort, and DFAR is again
`0x00000003`. App relocation is +0x7000.

The exact stack around SP plus the M12.2 ELF maps the active path to:
`AuroraPacketRendererCopyEfb` -> `Renderer::capture_current` ->
`EfbManager::capture_from_bound` -> `EfbManager::create` -> `glTexImage2D` ->
`gpu_alloc_texture` -> `gpu_alloc_mapped_for_gpu` ->
`gpu_alloc_mapped_aligned_for_gpu`. The failing EFB is a 128x128 RGBA backing (64 KiB).
Disassembly is decisive: the saved return `0x84EDC741` (core) relocates to `0x84ED5741`,
the instruction immediately after the **first** `vgl_memalign` call with type
`VGL_MEM_VRAM` (CDRAM). Therefore the current failure is inside the VRAM mspace allocator,
not a guest PPC fault and not the M12.2 object-layout bug. The earlier `write_rgba8888` address
was present as a preserved callback/register/stack value; it is not sufficient evidence that the
faulting store itself was in that callback.

M12.3 keeps M10/M11/M12/M12.1/M12.2 intact and adds a conservative allocation firewall:

- `TextureCache::get_or_upload` checks live `VGL_MEM_VRAM` before entering
  `glTexImage2D`; an upload is rejected with `InvalidHandle` if its conservative estimated
  footprint would leave less than **4 MiB CDRAM headroom**. Failure remains recoverable as an
  untextured draw and emits rate-limited `m12_3_tex_guard`.
- New EFB backings are gated the same way in `AuroraPacketRendererCopyEfb`. Existing EFB
  handles with matching dimensions continue to be reused without a new allocation. If a copy is
  blocked, GX clear side effects are still applied and the copy returns failure instead of
  entering the unstable VRAM allocator.
- `EfbManager::create` and the readback `upload_rgba` path now reject a texture whose
  `vglGetTexDataPointer(GL_TEXTURE_2D)` is null after upload, so a failed speedhack allocation
  is never attached/sampled as a valid GXM texture.
- Added `m12_3_efb` telemetry: EFB bytes/high-water/entries, blocked allocation count/bytes,
  and last observed VRAM-free value.

Last completed pre-transition hardware sample before the crash had texture cache
`10205075/10485760`, `pre_evict=27`, `pre_evict_bytes=6203204`, and
`pool_vram_free=13158816`; the crash happened later while executing the 45-EFB-command
transition frame, so the exact allocator headroom immediately before the fault was previously
unknown. M12.3 makes that state observable and prevents allocations once the reserve is crossed.

Build artifact: `...-m12_3-vram-headroom.vpk`, SHA-256
`0628e9ca4741e60ad32ed82e5bd0aeed16bb57c2686fa6bbbdf5523b19558883`. Exact ELF SHA-256
`68de1670017391515ebdff141073c5e04afce7276e179828a02106505d674c62`. Hardware validation
pending. If the same VRAM-mspace Data Abort occurs while telemetry proves >4 MiB headroom, the
next step is to treat it as allocator metadata corruption rather than exhaustion and harden/rebuild
the pinned vitaGL-speedhack allocator itself.

## M12.4 — single-thread vitaGL GC / no mspace-stat probes

M12.3 hardware **FAIL**. The new core changes the diagnosis: render pthread TID 0x40010139,
Data Abort 0x30004, DFAR 0x0000000B. The app frames on the crashed stack symbolize to
`vgl_mem_get_free_space` (return immediately after `sceClibMspaceMallocStats`) <-
`AuroraPacketRendererCopyEfb` <- RenderWorkerMain. Therefore the M12.3
`vglMemFree(VGL_MEM_VRAM)` headroom query itself crashed before the guarded EFB allocation.
The last completed card frame was otherwise healthy (664/664 draws, no transform/submit failures),
and reported about 13 MiB free CDRAM before the transition. This is no longer consistent with a
simple low-memory threshold failure.

Targeted M12.4 A/B:

- Rebuilt the pinned vitaGL speedhack archive with the same NO_DEBUG/DRAW/INDICES speedhacks but
  `SINGLE_THREADED_GC=1`. Deferred frees now run synchronously on the render lane at vitaGL
  frame/GC boundaries instead of a separate garbage-collector thread. This removes concurrent
  mspace free activity as a variable while allocations/EFB work happen on USER_1.
- Explicitly built with `HAVE_TEXTURE_CACHE=0`; Aurora continues to own its 10 MiB texture LRU.
- Removed every runtime `vglMemFree(...)` probe from the WiiCompiled/Aurora path, including the
  M12.3 texture/EFB guards and periodic free-space telemetry. We must not call
  `sceClibMspaceMallocStats` as a safety check when that query is itself faulting.
- Replaced the EFB guard with safe application-side accounting only: sampled EFB allocations are
  bounded to a 4 MiB `EfbManager::bytes()` budget. Existing same-size EFBs are reused; a new
  backing that would exceed the app-side budget is skipped while preserving GX clear side effects.
- M12.1 texture pre-eviction remains based on Aurora-owned byte accounting. Post-upload
  `vglGetTexDataPointer` checks remain. M10/M11/M12/M12.1/M12.2 semantics are otherwise preserved.
- Build identifies itself as `vitagl=speedhack-single-gc` and emits `m12_4_mem` /
  `m12_4_efb_budget` telemetry without querying mspace free-space stats.

This does **not** yet prove a background-GC race is the unique allocator root cause. It is the
smallest hardware A/B that removes the strongest remaining concurrency source after two crashes in
the same mspace family (M12.2 allocation path, M12.3 stats query). If M12.4 still faults inside
sceClib mspace code, the next step is to harden/replace the vitaGL mspace allocator path itself.

Artifact: `...-m12_4-single-gc-mspace-stability.vpk`, SHA-256
`0c7ad2973e20156c0a22faa08999d6c795594b3df20c4229e894740f0e499be7`. Exact ELF SHA-256
`5cd6ce0435c3dd0a4ce6e66af382ca15215393586ef02a676bd807e4737b01a5`. Hardware validation pending.

## M12.5 — vitaGL custom heap (sceClib mspace bypass)

M12.4 hardware **FAIL**. Single-threading the vitaGL GC did not remove the crash. The new core
(`psp2core-1788472853-0x00072c3203-eboot.bin.psp2dmp`) is again a native render-thread
Data Abort (0x30004), DFAR `0x00000003`. Runtime app relocation is +0x8000. The active
stack candidates symbolize to `write_rgba8888` -> `gpu_alloc_mapped_aligned_for_gpu_inner` ->
`gpu_alloc_mapped_for_gpu` -> `_glTexImage2D_FlatIMPL` -> `EfbManager::create` ->
`EfbManager::capture_from_bound` -> `Renderer::capture_current` ->
`AuroraPacketRendererCopyEfb` -> `RenderWorkerMain`. The saved return from
`gpu_alloc_mapped_aligned_for_gpu` is immediately after its first `vgl_memalign(..., VGL_MEM_VRAM)`
call. The fault therefore remains in the CDRAM allocation family even with background GC removed.

The last completed pre-transition frames are healthy and the transition still reaches 535 draws /
3624 vertices / 45 EFB commands with no frame-cap drops. This falsifies the background-GC race as
the sole explanation and makes the pinned vitaGL `sceClibMspace*` allocator itself (or metadata
corruption observed through it) the next isolation target.

M12.5 rebuilds the pinned vitaGL archive with `HAVE_CUSTOM_HEAP=1` plus
`SINGLE_THREADED_GC=1`, keeping `DRAW_SPEEDHACK=1` and `INDICES_DRAW_SPEEDHACK=1`.
`NO_DEBUG` is deliberately omitted so the custom heap's overflow/double-free/realloc corruption
checks remain active. This bypasses `sceClibMspaceMalloc/Memalign/Free/MallocStats` for vitaGL
managed pools and uses vitaGL's own allocator, documented upstream as the safer diagnostic heap.
The M12.4 app-side 4 MiB EFB budget and M12.1 texture-accounting/pre-eviction remain; no runtime
`vglMemFree` probes are reintroduced. Build identifies itself as `speedhack-custom-heap` and
uses `m12_5_mem` / `m12_5_efb_budget` telemetry.

Artifact: `...-m12_5-custom-heap.vpk`, SHA-256
`11331a13e8997c3459bad541d1ce99f9b3a3392725c0cc780f54a4feb9046bc3`. Exact ELF SHA-256
`5f96226047312ffb5ef9921a51dacc75c746894da11efeed26be4bbb0aaf5f31`. vitaGL archive SHA-256
`de04978951a09cdc28da1710face48051530be17358df7f50273d7a9a262600d`. Hardware validation pending.

## M12.6 — Select Class streaming-arena completion

M12.5 hardware PASS for allocator stability: with the vitaGL custom heap the previous
sceClibMspace crash is gone. The run survives the Single Player transition and continues for
thousands of heavy frames (producer serial >3160). The screen is still black instead of the
expected Select Class UI (50cc / 100cc / 150cc).

The new log exposes a deterministic renderer-side truncation that remained after M11. Every sampled
Select Class frame requests 3381 draws / 23032 vertices, but Aurora presents only 2580.
m12_submit reports exactly overflow=801 every frame. The streaming vertex arena is pinned at
stream_bytes=3145464, essentially the full 3 MiB capacity; index usage is only ~75 KiB. Therefore
the packet draw cap is healthy (begin_cap=0, dropped=0), but the later 801 draws are rejected by
StreamingArena::upload_vertices. This is especially consistent with the visual symptom because
the lost draws are the tail of the frame, where the NW4R layout/UI overlay is composed.

M12.6 raises only the per-slot vertex streaming arena from 3 MiB to 4 MiB. With two slots this costs
+2 MiB total and covers the observed ~23k-vertex frame with bounded headroom; the 512 KiB index arena
is unchanged. No scheduler, GX packet-cap, texture/EFB, custom-heap, TEV, matrix, THP, or guest-thread
semantics are changed. Hardware success criterion: m12_submit overflow=0, presented draws rise to
all non-transform-rejected draws, and stream_overflow stops increasing. Remaining transform_fail=148
and huge-NDC/oversize telemetry are a separate graphics-correctness issue to evaluate only after the
stream truncation is gone.

Artifact: ...-m12_6-full-stream.vpk, SHA-256
99fd874b38a2cea78430850b9867ccc9c5a1d1da8e907f12000d0e766d7fef65. Exact ELF SHA-256
6985faa721c94360760200f52346ce2be56949e895eaf4040674e8a3cb072d20.

### M12.6 hardware result — menu progression now reaches race start

M12.6 is a **hardware PASS for the 4 MiB streaming-arena fix**. The former per-frame Aurora
`StreamingOverflow` no longer blocks the menu path: sampled menu frames report `m12_submit
overflow=0`, and on hardware the game now progresses through multiple post-Single-Player screens.
The user was able to reach Select Class, continue through the following menu flow, select a race,
and start it. This is the furthest confirmed gameplay progression so far.

Visual correctness is still incomplete. The 2D/NW4R menu composition is now usable enough to
navigate, but the expected 3D character/kart/background models are still absent. Large frames still
show the previously known transform/XF anomaly (`transform_fail=148` in representative samples,
plus extreme NDC/oversize values), so 3D model visibility remains a separate graphics-correctness
blocker; do not treat the arena fix as solving matrices/XF/TEV semantics.

Embedded animated/video content is also still absent. The current diagnostic baseline keeps
`MKW_VITA_DISABLE_MOVIES=1`, and the existing generic texture path is not a complete solution for
MKW's THP/Motion-JPEG style video assets. Re-enabling these reliably on Vita requires a dedicated
decode/presentation path (preferred direction: native Vita JPEG decode where applicable, then a
bounded YUV->RGB path / GPU upload rather than replaying the current generic texture fallback).
Do not mark THP/video as renderer-complete until this dedicated path is hardware-validated.

The first attempt to enter the actual race exposes a **new, larger frame-capacity boundary**. Near
the end of the M12.6 hardware log, after another `Scene Exit`, the next heavy frame requests about
`6154` draw calls but the packet stores only `4096`: `begin_cap=2058`, with `32117` stored vertices,
`dropped=4340`, and `efb_cmds=64`. The render worker starts consuming this truncated frame and the
log ends during its submission (around draw 384/4096), coincident with the hardware crash. Therefore
the old 2048-draw blocker is solved for menus, but **4096 is not sufficient for the first in-race
workload**. The accompanying core dump is `psp2core-1788502217-0x00064f390b-eboot.bin.psp2dmp`;
its exact crash site still needs to be symbolized against the M12.6 ELF before choosing the crash fix.
Also validate whether `efb_cmds=64` is merely the observed count or another saturated command-array
limit before increasing any EFB capacity.

Next correctness milestone should therefore separate three issues rather than mixing them:

1. **M12.7 frame capacity / crash:** support the ~6154-draw first-race frame without truncation,
   then symbolize and fix the crash using the M12.6 core. Prefer measuring packet/vertex RAM cost
   before blindly raising 4096; chunking is acceptable if a larger bounded packet is too expensive.
2. **3D model correctness:** once the full in-race/menu frame is delivered, isolate the persistent
   transform/XF/projection failures that keep 3D models invisible.
3. **THP/Motion-JPEG video:** add the dedicated Vita decode/presentation path and only then re-enable
   movies for hardware validation.

## M12.7 — full race-frame capacity and EFB crash isolation

The M12.6 core is now symbolized against its exact ELF. The fault is a native USER_1 render-thread
data abort at `SceGxm+0x1E638` (`DFAR=0xFFFFFFFF`), not a translated PPC exception. Accounting for
the Vita loader's `+0x4D000` relocation, the return chain enters vitaGL through:

- `glViewport` (`misc.c:342`), immediately after `sceGxmSetViewport`;
- `update_scissor_test` (`tests.c:299`), immediately after its first
  `sceGxmSetUniformDataF` call;
- `glBlitNamedFramebuffer` (`framebuffers.c:816`);
- `glBlitFramebuffer` (`framebuffers.c:919`).

This identifies the active failure path as Aurora's GPU-resident EFB capture switching FBO render
targets and rebuilding vitaGL's scissor mask. It does not by itself prove whether the invalid GXM
state is scene exhaustion or earlier corruption. The M12.6 log stops at draw 384 with 28 of the 64
queued EFB commands already consumed, matching this stack and making a draw-array overflow an
unsupported explanation for that core.

M12.7 makes the crash isolation and capacity changes independently measurable:

1. Frame packet caps are now **8192 draws / 49152 vertices / 128 EFB commands**, from
   4096 / 32768 / 64. The exact `FramePacket` is 7,525,544 bytes (`g_pendingFrame=0x72D4A8`),
   bounded by an 8 MiB `static_assert`. Two frame geometries plus `g_renderVertices` cost
   7,508,488 bytes more static memory than M12.6. Counts remain inside their `u16`/`u8` storage.
2. The Aurora vertex streaming arena is **8 MiB per slot**, two slots. A complete bounded packet
   needs at most 49,152 x 168 = 8,257,536 vertex bytes, below the 8 MiB slot; the 512 KiB index
   arena is unchanged.
3. `MKW_VITA_EFB_GPU_BLIT=0` is the default. EFB copies use the existing synchronous
   `glReadPixels` + bounded downscale + `upload_efb_rgba` route, avoiding the crashing destination
   FBO/scissor-reset path while retaining the 4 MiB EFB allocation guard. Set the flag to `1` for
   an explicit GPU-blit A/B. `efbreadback` and `efbgpu` use distinct object/artifact names, so make
   cannot silently reuse the opposite implementation.
4. The custom vitaGL speedhack build is configured for its supported maximum of 8 display/FBO
   render-target scenes before `vglInitExtended`; stock vitaGL keeps its default because that API
   is not part of the installed public header.
5. Producer telemetry now reports EFB calls/recorded/capacity failures/destroys together with both
   immediate and raw draw-cap failures. Completed `efb_frame` lines identify `gpu` versus
   `readback` copy counts. This resolves whether M12.6's exact `efb_cmds=64` was saturation rather
   than assuming it from the array count alone.

Build baseline: reused `build/vita/mkwii_translated_neon_os`, translated flags
`-Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard`,
`MKW_VITA_LYT_DIRECT=0`, `MKW_VITA_LYT_FAITHFUL=1`, movies disabled, native THP enabled,
DVD defer disabled, Aurora + custom-heap speedhack vitaGL unchanged. `runtime-gx-bridge-check` and
`graphics-check` pass; the final ELF links, VELF/FSELF conversion succeeds, and `unzip -t` reports
no package errors. The only compiler diagnostic is the existing GCC enum-conversion warning in
Aurora `GXVert.cpp`. The readback and GPU-blit A/B object variants both compile successfully; only
the readback variant below was linked and packaged for hardware testing.

Artifacts:

- `build/vita/wiicompiled-vita-mkw-firstboot-aurora-speedhack-m12_7-full-race-frame.vpk` —
  SHA-256 `582afd7f5ffadaccac557fba5facc3d02e097460d632e33d9454c3904cfdd252`.
- Exact ELF `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-aurora-speedhack-m12_7-full-race-frame.elf` —
  SHA-256 `cfd212545297f6b63f2c6bfa815ba259cacf4c31ed021ed2dd3bc6de1b82e183`.
- Linked vitaGL archive remains SHA-256
  `de04978951a09cdc28da1710face48051530be17358df7f50273d7a9a262600d`.

Hardware validation is pending. Clear or rename the append-mode `runtime.log`, install this exact
VPK, repeat the same Single Player race path, and retain any new core. Required evidence is:
`frame_draw_cap=8192 frame_vertex_cap=49152 efb_cap=128 efb_gpu_blit=0`, a first-race
`producer_frame` with `begin_cap=0 raw_cap=0 dropped=0`, `efb_cap_fail=0`, and either a completed
`efb_frame ... gpu=0 readback=N`/`aurora_frame` or the exact last `render_large` marker if it still
crashes. This build/package result is not yet a real-hardware PASS.

### M12.7 hardware result — EFB crash fixed; draw/vertex capacity passes; EFB queue still saturates

Fresh single-session hardware log: `build/vita/runtime.log`, 2,337,268 bytes / 24,434 lines,
captured 2026-09-04. The expected build identifies itself at startup with
`frame_draw_cap=8192 frame_vertex_cap=49152 efb_cap=128 packet_bytes=7525544
efb_gpu_blit=0`, 8/8 render-target scenes and two 8 MiB vertex-stream slots.

M12.7 is a **hardware PASS for the old SceGxm EFB-blit crash and for the enlarged draw/vertex
packet**. The critical first-race transition is serial 1514:

`draws=6146 vertices=47946 requested_draws=6146 begin_cap=0 raw_cap=0 dropped=0`.

Unlike M12.6, which died near draw 384 after EFB command 28, M12.7 crosses the same boundary,
processes all 6146 stored draws, reaches `submit_done`, `endframe_done`, `swap_end`, and
`completed serial=1514 total_us=4691051`. It then continues through loading frames and repeatedly
completes the steady race workload through serial 1615, typically ~6167-6179 draws / ~38661-38741
vertices / 13 EFB commands. The final line follows a clean
`render_large phase=completed serial=1615 total_us=855631`; there is no crash/fatal marker or new
core evidence in this log. The repeated watchdog lines are slow-frame samples, not a deadlock,
because serials continue advancing.

The EFB completeness criterion fails and is now measured rather than inferred. Serial 1514 reports
`efb_cmds=128 efb_calls=399 efb_recorded=78 efb_cap_fail=321 efb_destroy=50`: all 128 command slots
are consumed by 78 copies plus 50 destroys, and 321 later copy requests are dropped. The worker
executes the 128 retained commands and survives, but this frame is not semantically complete.
Periodic frames confirm the intended stable path (`gpu=0 readback=12`, `failed=0`,
`capacity_fail=0`) outside that transition. Therefore do not increase the queue blindly without
also accounting for synchronous readback cost: the partial 128-command transition already takes
4.69 s. First measure unique destinations, overwrite-before-sample copies and destroy ordering;
then either coalesce provably dead copies or move the command count to `u16` and use a bounded
capacity of at least 512.

Memory remains stable but close to its guards. Sampled EFB high-water reaches 4,183,616 /
4,194,304 bytes with no EFB allocation blocks. The texture cache reaches 10,330,076 /
10,485,760 bytes and safely rejects 16 uploads instead of crashing: fourteen are the same
256x1024 format-0xA source `0x622523A0`, plus one 256x128 and one 128x128 source. These failures may
still explain missing visual assets and need correlation with the hardware image.

Steady race speed is still far from real time: renderer completion is approximately 0.83-0.90 s
per heavy frame and producer intervals are approximately 1.30-1.35 s (~0.74-0.77 FPS). The old M13
candidate `EGG::LightTexture::SetupTevInfo+0x13C` is no longer the dominant steady-race gap. Frames
1200-1209 instead spend ~372-378 ms at `0x8003EA94`, inside
`nw4r::ef::DrawBillboardStrategy::DrawNormalBillboard+0x764`. The serial-1514 transition also has a
one-off 1.827 s gap at `0x802155E0`, inside `EGG::ColorFader::draw+0x228`, plus the already known RFL
work. Performance work remains secondary to restoring complete EFB/texture output and confirming
what was visible on screen.

## Performance hotspot for M13 — `0x8022DEA4` = `EGG::LightTexture::SetupTevInfo+0x13C`

`gx_gap_hot rank=0 lr=8022dea4 gap_us=252750 max_gap_us=108417 count=10` on the heavy scene —
~250 ms/frame of guest CPU between GXBegin calls, only 10 begins, so ~25 ms *per* begin.
- Function: `EGG::LightTexture::SetupTevInfo`, guest `func_8022DD68` (0x8022DD68–0x8022DFA8),
  translated shard `generated/build_shards/base_common/shard_a848944f84522dfd842c73c3.cpp`.
  Related hot LRs: `8022e230` `SetupNextTevStage+0x288`, `8022dd34` `LightTexture::Draw+0x4C`.
- It is EGG's per-material lighting/projected-texture TEV setup for lit 3D menu models (kart /
  character showcase). The cost is translated PPC matrix work + the GX HLE calls it makes
  (`GXLoadTexObj` / `GXSetTevColor*` / `GXSetTexCoordGen` for the light projection), 10×/frame.
- Also present but transition-only: `0x800C23C0` `RFLiInitShapeRes+0x580` (Mii), `0x800c4ca4`
  `RFLiDrawQuad+0x134`.
- **M13 strategy (not started):** first per-shard-sample `func_8022DD68` on hardware to split
  translated-CPU vs GX-HLE cost. If GX-HLE dominates, HLE `EGG::LightTexture::SetupTevInfo` /
  `SetupNextTevStage` directly (mirror the TEV/texgen state into `g_hleGxState` without replaying
  every GX call). If translated PPC dominates, it is matrix math — candidate for a NEON
  `PPC_NATIVE_OVERRIDE` of the inner transform, same pattern as the fpu helpers. Keep it behind a
  flag and measure against `gx_gap_hot`.


## M12.8 — indexed XF + native THP + GPU EFB hardware A/B

M12.8 targeted the three remaining user-visible issues from M12.7 while keeping the hardware-proven faithful NW4R/lyt path (`MKW_VITA_LYT_DIRECT=0`, `MKW_VITA_LYT_FAITHFUL=1`):

- fixed `GX_LOAD_INDX_A/B/C/D` on the reduced Vita GX replay path. The old bridge resolved/rebound the CP XF source array but never copied the selected words into the Vita backend XF register state. `ApplyIndexedXfPacket` now converts each resolved indexed load into the equivalent `LOAD_XF_REG` packet and feeds the existing XF decoder;
- added per-frame `xf_idx=<loads>/<words>` telemetry;
- re-enabled movie playback (`MKW_VITA_DISABLE_MOVIES=0`) with native THP decode kept enabled;
- TurboJPEG native THP decode uses `TJFLAG_FASTDCT`; render-side YUV420→RGBA reuses each U/V sample across its 2×2 luma block and logs conversion `elapsed_us`;
- added a Makefile native-runtime configuration stamp so HLE/movie flag changes cannot silently reuse stale runtime objects;
- retried `MKW_VITA_EFB_GPU_BLIT=1` with scissor disabled across the transient READ/DRAW FBO switch, attempting to avoid the M12.6 `update_scissor_test` crash while removing synchronous EFB readback.

The complete M12.8 GPU-EFB artifact was built from the source checkout and tested on hardware: `build/vita/wiicompiled-vita-mkw-firstboot-aurora-speedhack-efbgpu.vpk`, SHA-256 `8677cc8c6ccd6771bb8751f0bcb8ed9172e47318381aa0a7c545cee1b0dd18ee`.

### M12.8 hardware result — GPU EFB FAIL; same native SceGxm crash as M12.6

The uploaded `runtime.log` contains multiple concatenated boots; the final session is the M12.8 run. That session reaches NW4R G3D and the RVL THP library, proving movie support is no longer compiled out, but it crashes before the first native THP frame-decode telemetry is reached.

The GPU EFB path is active and executes many `efb_copy_exec ... path=gpu` commands. The second G3D transition eventually submits a 535-draw / 3624-vertex frame with 45 EFB commands after a long guest stall; USER_1 then executes GPU EFB copies through `n=32` and the runtime log terminates abruptly without a completed-frame marker.

The accompanying core is `psp2core-1788515405-0x000692394f-eboot.bin.psp2dmp`. Parsing Vita `THREAD_INFO`, `THREAD_REG_INFO` and `MODULE_INFO` identifies the crashed native USER_1 `pthread` as:

- stop reason `0x30004` — Data Abort;
- PC `0xE00A2338` = `SceGxm` RX + `0x1E638`;
- DFAR `0xFFFFFFFF`;
- DFSR `0x8F5`.

This is the same `SceGxm+0x1E638 / DFAR=FFFFFFFF` signature already symbolized for M12.6. The M12.8 scissor guard therefore does not make vitaGL's `glBlitFramebuffer`/transient-FBO path safe under the real G3D/EFB workload. Treat that implementation as rejected for the correctness path; future GPU-resident EFB work should avoid repeated transient render-target switches.

There is also a separate CPU-side transition stall. `0x800C23C0` (`RFLiInitShapeRes+0x580`) reaches `gap_us=5747850`, including one ~5.01 s gap, and the same boot reports missing `ux0:data/wiicompiled-vita/NAND/shared2/menu/FaceLib/RFL_DB.dat`. The function does return, so this explains the multi-second freeze but not the terminal native crash.

Periodic `xf_idx` samples in this short M12.8 run remain `0/0` because the logged samples are from 2D/pre-G3D frames. This is inconclusive for indexed-XF correctness, not evidence the path is unused.

## M12.9 — stable EFB readback + indexed-XF trace + native THP

M12.9 is the progression/stability build after the failed M12.8 GPU-EFB A/B:

- `MKW_VITA_EFB_GPU_BLIT=0` is again the default (`aurora-speedhack-efbreadback`);
- all M12.8 indexed-XF semantics stay enabled;
- all M12.8 native THP/movie changes stay enabled (`MKW_VITA_DISABLE_MOVIES=0`, `MKW_VITA_NATIVE_THP=1`);
- faithful LYT remains the correctness baseline;
- `ApplyIndexedXfPacket` now emits immediate `xf_indexed n=... dst=... words=... source=...` telemetry for the first 16 successful loads and then powers of two. This avoids missing a short G3D indexed-matrix phase simply because frame counters reset before a periodic sample.

`runtime-gx-bridge-check` and `runtime-gx-vi-check` pass. The complete NEON build links, converts to SELF, packages successfully, and `unzip -t` reports no errors.

Artifact:

- `build/vita/wiicompiled-vita-mkw-firstboot-aurora-speedhack-efbreadback.vpk`
- SHA-256 `0d6a78998745e00865e0ef1cd5a12cfc73003bed6c53d3d04a2a02c2366af66e`
- approximately 39 MiB

Hardware success criterion: recover at least the M12.6/M12.7 menu progression without another SceGxm core. Then use the first `xf_indexed` lines to evaluate the missing 3D models and, only after surviving that transition, inspect native THP decode telemetry for movie playback. The RFL transition is expected to remain temporarily very slow and is now the next independent USER_0 blocker once renderer stability is restored.

## M12.9 hardware result — stable EFB readback; indexed XF validated; 3D still invisible

M12.9 was exercised on real PS Vita through the menu/G3D path and into the heavy race workload. The stable CPU-readback EFB path remains crash-free: no recurrence of the rejected SceGxm+0x1E638 / DFAR=0xFFFFFFFF transient-FBO blit crash was observed in the tested session. The run advances through the previous G3D/race boundaries and continues rendering heavy frames.

Indexed XF is now **hardware-validated**. Immediate xf_indexed traces advance from the first loads through powers of two up to at least n=65536. Periodic frame telemetry shows real indexed-matrix traffic; for example frame 1440 reports approximately xf_idx=108/1188 and xf=442/1338 with transform_fail=0. Earlier enormous NDC values from the pre-indexed-XF implementation disappear in corrected menu/G3D samples; representative corrected values are around max_ndc=4.93,5.61. Therefore the original missing indexed-XF semantics are no longer the primary explanation for the invisible models.

The 3D geometry is nevertheless still not visible on hardware. This is **not a draw-capacity or submission failure** in the menu/G3D samples: thousands of draws and tens of thousands of vertices reach the Aurora packet renderer with no frame draw-cap, vertex-cap or pipeline-submit failure and transform_fail=0. A representative G3D frame contains roughly 3381 draws / 23032 vertices, split between about 319 orthographic draws and 3062 perspective draws.

Material/TEV coverage is now the strongest correctness suspect. In the same kind of frame only about 500 textured draws are classified as the currently supported/simple TEV path while roughly 2856 use the generic TEV fallback. The Vita bridge still represents only a simplified subset of Wii/NW4R multi-stage material state. A model can therefore have valid vertices and matrices but still become black/transparent/otherwise invisible after the incomplete combiner/alpha translation. This must be separated from transform correctness with a material-free visibility A/B.

Performance remains far from real time even with correctness restored:

- menu/G3D display-list replay is commonly about 250-310 ms/frame in the latest M13.0-instrumented run; earlier M12.9 samples were about 127-129 ms/frame before the extra failed fast-path attempts;
- steady race display-list replay is about 330-370 ms/frame;
- nw4r::ef::DrawBillboardStrategy::DrawNormalBillboard around guest LR 0x8003EA94 / 0x8003EC84 remains a dominant race guest-CPU hotspot, often around 365-376 ms/frame;
- EGG::LightTexture::SetupTevInfo around LR 0x8022DEA4 remains a menu/G3D hotspot around 158-159 ms/frame when enabled;
- stable EFB readback itself remains expensive on USER_1, with sampled heavy-frame renderer/EFB work roughly 350-470 ms in the periodic samples;
- the RFL transition remains an independent multi-second one-shot stall and still reports missing RFL_DB.dat; it is not the main steady-state FPS limiter.

Native THP support is compiled and available in the correctness build, but the tested M12.9 session did not reach a useful native decode / thp_yuv420 sample. Do not mark movie playback hardware validated yet.

## M13.0 — indexed display-list raw fast path

M13.0 attempted to remove the high per-vertex HLE cost of static NW4R/BRRES display lists. The Vita backend's existing DecodeRawDraw already understands GX_INDEX8 / GX_INDEX16, so GX__CallDisplayList now permits packed indexed draws to bypass the per-vertex SubmitDLVertex loop when the outer scan proves the display list has no nested display-list call and no embedded array-base/stride mutation. Unexpected raw-decode failures automatically fall back to the previous correct per-vertex path.

The feature is controlled by MKW_VITA_DL_INDEXED_RAW (default 1) and is included in the native runtime configuration stamp. CPU telemetry adds dl_raw=<raw-success>/<indexed-success> verts=<raw-vertices> fail=<raw-decode-failures>.

All ARM32 GX/VI/graphics checks pass and the complete VPK links/packages successfully. The explicit M13.0 test artifact produced during this iteration is:

- build/vita/wiicompiled-vita-mkw-firstboot-m13_0-indexed-dl-fastpath.vpk
- SHA-256 c975699a9c5cfc6f7a083df6ba6ca5896497e1d9473444202bb52e116d1c4f24.

### M13.0 hardware result — fast path currently FAILS on essentially every perspective G3D draw

The first hardware log with M13.0 shows that the optimization does not yet provide its intended speedup. Representative 3D frames report dl_raw=0/0 with thousands of failures. Most importantly, one G3D sample has about **3062 perspective draws and 3062 raw failures** (plus about 319 orthographic draws). This one-to-one correlation strongly suggests that the packed decoder is rejecting the vertex layout/array state used by the real 3D model display lists and then falling back to the slow path for every model draw.

Because the fallback is correct but expensive, M13.0 remains a correctness-preserving experiment, not a performance win. The next diagnostic must record the exact raw decode failure category (indexed array bounds, descriptor, stream footprint, position/texcoord decode or final cursor mismatch) rather than only a total failure count.

## M13.1 — stripped performance + 3D visibility probe

M13.1 is a deliberately non-faithful diagnostic build. It is intended to answer two independent questions in a single hardware run:

1. does perspective 3D geometry become visible when the incomplete material path is removed; and
2. exactly why does the indexed raw display-list decoder reject the real G3D draws?

New profiling-only flags default to 0, so the normal correctness configuration remains unchanged:

- MKW_VITA_PERF_SKIP_EFB
- MKW_VITA_PERF_SKIP_BILLBOARDS
- MKW_VITA_PERF_SKIP_LIGHTTEXTURE
- MKW_VITA_PERF_FORCE_3D_SOLID

The M13.1 test VPK is built with all four enabled, plus MKW_VITA_DISABLE_MOVIES=1, MKW_VITA_NATIVE_THP=0, MKW_VITA_DL_INDEXED_RAW=1, stable MKW_VITA_EFB_GPU_BLIT=0, faithful LYT and the existing NEON translated build.

Behavior of this diagnostic build:

- GXCopyTex EFB copies return immediately and emit bounded perf_probe skip_efb_copy telemetry;
- nw4r::ef::DrawBillboardStrategy::DrawNormalBillboard is gated at the translated guest function entry, removing the known billboard/particle hotspot;
- EGG::LightTexture::SetupTevInfo is gated at its translated guest function entry, removing the known menu lighting hotspot;
- THP/movie work is disabled;
- every **perspective** geometry draw keeps its real vertices, PN matrices, projection and depth state but is submitted as a diagnostic untextured/opaque material with texture disabled, alpha compare forced to ALWAYS, blending disabled and culling disabled. The expected hardware output is therefore crude white/solid 3D silhouettes, not correct Mario Kart materials;
- indexed-XF, core G3D geometry, input, game logic, physics and race progression remain active.

vita/gx_backend.cpp now records bounded raw_decode_fail telemetry with the failure reason and relevant vertex/attribute/index/array/stride/cursor metadata. The first failure reason in the next hardware log should identify the blocker preventing M13.0's bulk decoder from being used.

The M13.1 configuration compiles through the full Vita runtime, including the modified GX backend, links successfully, converts ELF -> VELF -> FSELF and packages successfully. The generated package for the immediate hardware test is currently the standard variant path:

- build/vita/wiicompiled-vita-mkw-firstboot-aurora-speedhack-efbreadback.vpk

At packaging time this path contains the **M13.1 performance/visibility configuration**, despite the legacy generic filename. A dedicated M13.1 filename/hash was not recorded in this iteration, so do not use an older same-named VPK from another build directory as evidence.

### M13.1 hardware result — probe active, but the claimed white A/B was incomplete

The updated append-mode `runtime.log` is 11,432,799 bytes / 119,100 lines. Its final session starts
at line 91,251 and is the actual M13.1 run: startup reports `movies_disabled=1 native_thp=0`, EFB
copy bypass markers advance through powers of two, and `perf_probe force_3d_solid` advances from
the first perspective draw to at least `n=2097152`. The session reaches the race workload and
continues through `render_large phase=completed serial=1500 total_us=606951` without a terminal
crash marker. On hardware, 3D models remained invisible while the in-race text was readable.

That visual result does **not** yet disprove the material hypothesis. The M13.1 code selected
untextured `GX_PASSCLR`, disabled blend/alpha/culling, but left each vertex's original RGBA intact.
`GX_PASSCLR` consumes raster/vertex color, so black or zero-alpha G3D vertex colors could still
produce no visible silhouette. The implementation comment promised white geometry without
actually writing white.

The raw-decode failure is conclusive. Every sampled failure is `direct_stream(3)`: e.g. a draw
with 4 vertices / 8 payload bytes fails while attribute 9 (POS) is seen as `GX_DIRECT`, and other
draws fail when NRM/TEX descriptors demand 6/4/12 direct bytes. The display-list scanner correctly
computed the compact indexed payload, but `DlInterpretVisitor::OnDraw` called
`ApplyAuroraVtxStateForDlBegin` before raw submission. That helper intentionally converts
`GX_INDEX8/16` descriptors to `GX_DIRECT` for the old expanded fallback, so the raw decoder was
given index bytes under a direct layout. This explains the one-failure-per-perspective-draw
correlation and is not missing/corrupt BRRES data.

The bypass build also gives bounded performance evidence, not a correctness result. Near the end
of the race it produces about 6047 draws / 38426 vertices / 0 EFB commands, with producer intervals
around 1.01-1.07 s and render-worker completion around 0.52-0.61 s. The failed raw path still costs
roughly 0.37 s in `dl_us` on representative race frames, so M13.1 did not deliver the intended
display-list speedup.

## M13.2 — indexed-VCD raw fix + actual white geometry probe

M13.2 fixes both invalid M13.1 experiments without changing the normal correctness defaults:

- indexed display-list draws are submitted to the raw decoder while Aurora still holds the real
  indexed VCD/VAT and array bindings published by the outer scan; the direct-state publisher now
  runs only for all-direct lists or the established per-vertex fallback;
- if an indexed raw draw still fails for a different reason, the visitor disables further raw
  attempts for that one display list and falls back to the faithful expanded path, avoiding a
  chain of failures after the fallback switches the live VCD to direct;
- the solid probe now overwrites RGBA with `255,255,255,255` in the render-worker vertex copy for
  every perspective draw before selecting untextured `GX_PASSCLR`. Guest GX state and producer
  packets remain untouched;
- the startup marker now prints `dl_indexed_raw` and all four `perf_*` flags;
- `gx_backend.o` now depends on the native configuration stamp, so changing only the probe `-D`
  values cannot silently reuse an object from another variant.

The full ARM32 build, GX bridge/VI checks, ELF -> VELF -> FSELF conversion and VPK packaging pass.
`unzip -t` reports no errors, the packaged `eboot.bin` matches the staged FSELF, and the ELF embeds
the extended startup marker plus `raw_decode_fail` and `force_3d_solid` telemetry.

Hardware-test artifacts:

- `build/vita/wiicompiled-vita-mkw-firstboot-m13_2-indexed-raw-white.vpk`
  (40,397,045 bytes), SHA-256
  `97c9ed295e301178ac3864212bbcc4b0944a60eedfe3a4b910b919d5fc59ef50`;
- exact symbol ELF:
  `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-m13_2-indexed-raw-white.elf`, SHA-256
  `d1570fcf2c5722603a34a0a98c714e1a32462b5f368c63e339792459e2a6082c`.

This is still a deliberately non-faithful probe: EFB, billboards, LightTexture and movies remain
disabled. It is not the candidate for restoring final race graphics.

### M13.2 hardware result — indexed raw passes; forced-white 3D still absent

The updated append-mode `runtime.log` is 16,189,658 bytes / 170,289 lines, SHA-256
`d0b58c6224682babd31290d1a1565f416595cb66e801bea9b21d692fca60cf2e`. Its latest session starts
at line 119137 and reports the expected M13.2 configuration: indexed raw enabled, all four
`perf_*` probes enabled, stable CPU-readback EFB selected, movies/native THP disabled and faithful
LYT retained. The log ends after `render_large phase=completed serial=1834 total_us=512992`; there
is no terminal crash marker in this session.

The M13.2 indexed-VCD fix is a **hardware pass for the representative G3D display lists**:

- repeated menu/G3D samples report `dl_raw=3062/3062`, `verts=21756`, `fail=0`;
- their `dl_us` is approximately 141-143 ms, compared with approximately 246 ms for the same
  3062-draw M13.1 case that failed raw decoding and used the expanded fallback;
- later samples reach `dl_raw=3872/3872`, `verts=26412`, `fail=0`, at approximately 174-175 ms;
- the raw path therefore removes about 104 ms, roughly 42%, from the representative 3062-draw
  case, but display-list processing is still much too slow for real time.

The run also reaches heavier race packets. Most continue to use the raw path successfully, but
frames that approach the 49,152-vertex packet limit report bounded capacity failures, for example
frame 1170 with `verts=49149 fail=216`. These failures are a separate packet-capacity/streaming
issue; they do not invalidate the representative M13.2 raw-decoder pass and must not be conflated
with the fixed M13.1 indexed-VCD bug.

The visual A/B result was reported from real hardware: **the 3D models remain invisible even with
the actual RGBA=255 forced-white, untextured, opaque perspective probe, while the in-race text is
now readable**. Unlike M13.1, this is a valid material-free visibility result. Incomplete TEV and
materials still require implementation for final graphics, but they are no longer a sufficient
explanation for completely absent solid geometry. The immediate correctness investigation moves
to the position-to-clip path, PN-matrix selection, depth range/test, viewport/scissor and the
compact vertex/draw ranges consumed by the renderer.

### M13.2 performance architecture analysis

The latest hardware log and a source audit of WiiCompiled, the active Aurora packet bridge and
`vitaGL-speedhack` identify two independent CPU bottlenecks rather than a simple lack of Vita GPU
fill rate.

1. **USER_1 renderer preparation is dominated by per-logical-draw CPU work.** Representative
   race samples spend 293-567 ms in `submit_us`, while `swap` is about 5-7 ms. Thousands of GX
   draws are already collapsed into only a few dozen physical GPU draws, so the expensive work is
   vertex conversion, index construction, state/pipeline hashing, texture lookup and streaming
   upload before the final draw calls.
2. **USER_0 remains independently expensive.** The UI example with 665 logical draws renders in
   about 22.8 ms but takes about 153 ms in the producer. Heavy producer intervals reach about
   0.8-1.0 s, and multi-second RFL/guest stalls also occur with negligible renderer wait. Aurora
   cannot explain those intervals.
3. **The active vertex path is over-general for WiiCompiled.** A compact bridge vertex of about
   24 bytes is expanded into Aurora's approximately 168-byte `CanonicalVertex`, copied through a
   temporary vector and staging arena, then copied again into a vitaGL-mapped VBO. Batching occurs
   only after most per-draw preparation has already been paid.
4. **The frame packet duplicates state.** Each logical draw carries projection and a full matrix
   palette; the bounded packet is about 7.5 MiB and heavy-frame packet copies cost roughly
   13-45 ms in the latest run.
5. **Current logging contaminates the profile.** The M13.2 session contains 37,967
   `render_large phase=draw_progress` lines plus thousands of repeated warnings. stdout/stderr are
   line-buffered into an append-mode file, so hot-path telemetry can add synchronous storage I/O
   inside the measured submit loop.

The recommended design is not an immediate full GXM rewrite. Keep Aurora's validated shader,
texture and pipeline ownership, but add a WiiCompiled-specific compact frame submission path:

- compact 24/32-byte vertex layout consumed directly by the Vita shader;
- frame-wide batching before vertex packing, hashing and texture lookup;
- direct writing into a once-mapped VBO/IBO without the generic `CanonicalVertex` staging copy;
- immutable per-frame state tables with small IDs instead of per-draw matrix/state duplication;
- after packet compaction, a bounded two-slot SPSC producer/renderer queue with explicit EFB/XFB
  synchronization;
- a decoded display-list template cache with guest-write invalidation;
- faithful adjacent-quad batching for LYT/UI, retaining the already successful glyph fast path;
- selective `-O2`/`-O3` only for profiled hot ARM32 shards rather than the full translated image.

The complete ordered plan, proposed build flags, performance targets and hardware A/B procedure
are recorded in [`PERFORMANCE_OPTIMIZATION_PLAN.md`](PERFORMANCE_OPTIMIZATION_PLAN.md).

## M13.3-P4.1 — Astra performance program (2026-09-05)

Performance work is now the primary development priority. The M13.2 profiling architecture was
implemented incrementally behind build flags and validated on real PS Vita with EFB, LightTexture,
Billboards and real materials enabled. The objective is to reduce producer and consumer CPU cost
without replacing Aurora's already validated shader/texture/pipeline ownership.

### P0 — low-overhead profiling / reproducible builds  (DONE)

- Hot per-draw/progress logging is compiled out with `MKW_VITA_PERF_LOG=0`; bounded counters and
  periodic summaries remain.
- A small in-memory perf ring preserves critical events without synchronous per-draw file I/O.
- Producer waits are split into `GXDrawDone` and generic render-worker waits.
- EFB timing is split into sync / readback / scale / upload.
- Build identity is recorded in a generated manifest: git HEAD, native config, translated build
  directory/options, VitaGL archive/toolchain and SHA-256 for VitaGL/ELF/VPK. The selected VitaGL
  archive is also a real ELF prerequisite so replacing it forces a relink.

### P1-P3 — compact consumer path / frame queue / DL template cache  (DONE, hardware-tested)

The active performance path now uses:

- compact bridge vertices instead of the generic CanonicalVertex expansion;
- direct mapped VBO/IBO writes and U16 indices;
- adjacent run batching before expensive per-draw command construction;
- dense `renderStateId` values so only the first draw in a run resolves pipeline/bindings/uniforms;
- compact per-frame transform/raster/texture state tables instead of duplicating full state per draw;
- a two-slot producer/renderer queue;
- conservative display-list template caching;
- EFB boundaries still flush/serialize for correctness.

Hardware evidence confirms the run path is effective. A representative frame with 1,619 logical
draws is reduced to 87 physical draws; 1,444 of the 1,619 draws extend an existing state run. Heavy
frames similarly collapse thousands of logical draws into tens/low hundreds of physical submissions.
The consumer is therefore no longer dominated by constructing one full DrawPacket per logical draw.

### P4 — GX generations + layout decoder + object-space mesh cache  (DONE, hardware-tested)

P4 moves the main optimization focus to USER_0/display-list replay:

- separate generations for transform, raster, texture, vertex-layout and array state;
- conservative XF/BP invalidation split by state category;
- value-sensitive vertex descriptor/VAT/array invalidation;
- transform/raster/texture snapshots are reused while their generation is unchanged;
- raw GX layouts are precompiled per `GXVtxFmt`, so each vertex no longer rescans the complete
  PNMTXIDX..TEX7 descriptor range;
- a bounded object-space mesh cache stores decoded `RenderVertex` data plus PN-matrix references;
- cache entries are validated against guest-write generations for both the display-list payload and
  every indexed source array. Untracked memory is never cached.

The first P4 cache used 256 direct-mapped entries / 4 MiB payload budget. Hardware showed that state
and layout reuse were already excellent, but the mesh cache thrashed:

- representative `p4_state` reuse: roughly 88-90%;
- `p4_layout=1457/45` (~97% hits);
- `p4_mesh=194/1308/1106/0` on one representative frame (~13% hits, zero invalidations);
- another heavy sample reached only about 6% mesh hits, again with zero invalidations.

Even this first version reduced representative display-list CPU time from about 50 ms to about
36.9 ms, and a heavier ~82 ms case to about 64 ms. The low hit rate was therefore identified as a
capacity/collision problem rather than changing guest geometry.

Artifact preserved for the first P4 hardware pass:

- `build/vita/wiicompiled-vita-mkw-firstboot-astra-p4-performance.vpk`
- SHA-256 `f126e0bd1a73c9d90fca5ddfb9c518fd018b6b5879eb444d81e3dcf7b96667fc`
- exact ELF SHA-256 `943c18e3ca834295197e0c96d1ab9d515b9646a0c8d6923796233819e29a80f4`.

### P4.1 — 4-way object-space mesh cache  (DONE, hardware-tested, target reached)

P4.1 keeps the 4 MiB mesh payload budget but replaces the 256-entry direct-mapped cache with a
2,048-entry, 512-set, 4-way set-associative cache using per-set LRU replacement. Dependency metadata
was reduced to the maximum real indexed GX vertex attributes rather than reserving every GX enum
slot per entry. Guest-write validation and correctness fallbacks are unchanged.

Real-hardware results are the strongest performance result so far:

- `p4_mesh=1289/213/11/0` -> approximately **85.8% mesh-cache hits**, zero invalidations;
- `p4_layout=1457/45` remains approximately 97% hit;
- representative `dl_us` falls to **13.861 ms** for 1,300 raw draws / 9,176 vertices;
- this is ~62% faster than the first P4 cache and ~72% faster than the ~50 ms pre-P4 baseline;
- the Astra intermediate target **display-list replay <20 ms is reached**;
- the same representative producer interval is approximately 220.6 ms.

No correctness regression accompanies the speedup in the measured samples: `raw_fail=0`,
`transform_fail=0`, no draw/vertex overflow, no dropped draws and no texture failures. Further
mesh-cache enlargement is no longer the highest-value optimization.

P4.1 artifact:

- `build/vita/wiicompiled-vita-mkw-firstboot-astra-p4_1-meshcache.vpk`
- SHA-256 `9bbb8f109671a1d7e5beffeda1e70bb80d99cc36029dfa0c060abdfe93b655af`
- exact ELF SHA-256 `fd87ecd686910b3db1d08a23cfa2b4fab63b1c65edfbb267a03bd393c41d571e`.

### Current dominant performance blocker — EFB CPU round-trip / GXDrawDone

With DL replay reduced below 20 ms, the hardware profile now points overwhelmingly at EFB. In the
same representative P4.1 frame:

- renderer: ~101.9 ms;
- 12 EFB copies execute successfully;
- EFB sync: ~1.0 ms;
- EFB readback: ~42.6 ms;
- CPU scale/flip: ~19.5 ms;
- texture upload: ~18.0 ms;
- total measured EFB work: ~81.1 ms, roughly 80% of renderer CPU time;
- producer prior-wait: ~91.9 ms, almost entirely `GXDrawDone` (~91.9 ms), while generic worker
  wait is effectively zero.

This means the synchronous EFB path is hurting both USER_1 and USER_0: the framebuffer is read back
to CPU RAM, scaled/flipped, uploaded again, and the guest producer then waits for the renderer/GPU
barrier. Removing this round-trip has a much larger expected payoff than further mesh-cache tuning.

The old `MKW_VITA_EFB_GPU_BLIT=1` transient-FBO implementation remains **rejected**. It previously
crashed on hardware in `SceGxm+0x1E638` / DFAR `0xFFFFFFFF`; do not re-enable it as the P5 fix.
The next EFB architecture must keep sampled copies GPU-resident without repeated transient render-
target/FBO switching, preserve FIFO copy/clear semantics and retain a CPU-read fallback only for
copies that are genuinely exposed to guest CPU reads.

Other measured performance debt remains after P5:

- `prebegin_us` is still about 130-135 ms in representative G3D frames and must be decomposed after
  EFB/GXDrawDone no longer dominates synchronization;
- heavy scenes can request hundreds of EFB operations and exceed the current 128-command frame cap;
  simply increasing the cap while every copy performs a CPU readback is not viable;
- later scenes can saturate the ~10 MiB sampled texture cache and require bounded eviction/pre-
  eviction rather than failed uploads;
- UI/LYT run batching, full resource-fence ownership and selective translated-shard optimization
  remain later Astra stages.

## Next

- P5/P6/P7 sono ora hardware-tested: non tornare al vecchio readback EFB salvo fallback.
- La priorita performance successiva e il **ridimensionamento EFB interamente GPU** o un percorso equivalente che elimini i ~59 ms/copia-resize CPU osservati nei frame G3D.
- In parallelo, correggere la texture cache nelle scene pesanti: servono eviction/pre-eviction bounded che proteggano le texture del frame in-flight; non aumentare semplicemente il budget globale.
- Dopo queste due correzioni, rimisurare GXDrawDone e prebegin_us e solo allora scegliere il prossimo hotspot guest/producer.
- L O2 selettivo billboard non va reso default: i log P7 e P7-O2 non mostrano un beneficio ripetibile oltre il rumore.
- La correttezza 3D/materiale resta sospesa mentre performance ha priorita; le sagome bianche hanno gia dimostrato che la geometria reale viene rasterizzata.


## M13.3-P5–P8 — implementazioni offline e quattro VPK A/B (2026-09-05)

Valutate e preservate P0–P4.1. Il campione hardware seriale 900 resta la baseline:
producer 220,643 ms, renderer 101,851 ms, cache mesh 1289/213, EFB 12 copie
con sync/read/scale/upload 1,026/42,594/19,522/18,008 ms.

Nuovo codice implementato nel backend Aurora effettivamente compilato
(`aurora-main/platforms/vita/gfx`) e nel bridge `vita/`:

- **P5**: texture EFB persistenti senza FBO transitori; copia GXM sincronizzata
  per dimensioni uguali, nearest-neighbour CPU direttamente nella texture per
  dimensioni diverse. Fallback precedente conservato. Non è ancora scaling
  interamente GPU né readback/packing nella RAM guest.
- **P6**: riuso dei buffer protetto da drain GPU misurato (`reuse_wait_us`);
  coda EFB a 16 bit, capacità 512 nei profili avanzati, accorpamento soltanto
  di comandi adiacenti non osservabili e senza eliminare clear. Budget texture
  con margine temporaneo condiviso e conteggio EFB comprensivo di pitch.
- **P7**: unione nel producer di quad UI ortografici adiacenti con stato identico,
  senza attraversare copie EFB; LYT fedele invariato.
- **P8**: lista esplicita di shard O2/O3 con oggetti separati. Compilata variante
  O2 del solo shard billboard contenente il ritorno 0x8003EA94; staging completo
  `mkwii_translated_neon_os` conservato. Nessun fast-math globale.
- Profilo **full-features** compilato con filmati e THP nativo abilitati;
  questa compilazione non ne dimostra il funzionamento sulla console.

Passano i test host con ASan/UBSan: equivalenza dei pixel EFB con stride e
orientamenti diversi; 1.000.000 operazioni FIFO confrontate prima/dopo
accorpamento; budget texture, pin del frame, LRU e invalidazioni. Nel test
1 MiB/texture 16x16, residenti 15/30 prima e 30/30 con margine condiviso.
Passa anche `graphics-check`. I quattro pacchetti hanno compile/link,
VELF/FSELF e verifica ZIP completati con exit 0.

Report completo, limiti, comandi e hash:
`docs/performance-60fps-2026-09-05/IMPLEMENTATION.md`.
Evidenza aggregata: `docs/performance-60fps-2026-09-05/artifacts-implemented.json`.
Test: `build/vita/performance-helper-tests.json`.
Build riproducibili: `python3 vita/tools/build_performance_profile.py PROFILO`.

| Profilo | VPK byte | SHA-256 VPK | SHA-256 ELF |
|---|---:|---|---|
| p5-resident | 40875331 | `8d2f2873b49e8fffd906e61799b77917ce244be8635d5170f15a35f53a70c2fb` | `f8c6b0940d764e81e2f09e335d50eab8cef3e3da67fccb9da553fbcfcd362e89` |
| p7-ui | 40876061 | `5a40dfd924105a1170588edc563fdf4f0d8b9fa640b351d369b1e4d596de6625` | `afcfb0143a3e133e701d2b31256e83a5df1f375a9bcd325db85bd4b2722fb51a` |
| p7-ui-hot-O2-f171ce57 | 41016734 | `af0d6072de223520b53edac319abedad559e7c3ecfeba22932050fb7cc080010` | `22b37d0401dfad4007d49389c4c28dec6c36d0ffee21d8646e6ee2ac3cf3bd91` |
| full-features | 41277088 | `ae4066100ff4646876b11e99f403773d4df7d3cb3054d566bc64bcb2b0f6e811` | `9019cce437afd4ebe4c14a3862e895549ec16b95a6cd2b2a6543b23da9e27e8a` |

Vedere la tabella completa nel report e i manifest `.evidence.json` accanto ai VPK.

**Stato offline di questa sezione:** superato dai test hardware riportati sotto. Le build qui elencate restano gli artefatti di riferimento, ma P5/P6/P7 non sono piu soltanto candidate offline.


## M13.3-P5–P8 — validazione hardware P7 / P7-O2 / full-features (2026-09-05)

Tre nuovi log reali PS Vita validano il programma successivo a P4.1. I profili P7 e P7-O2 partono con movies disabilitati, mentre la sessione full-features corretta parte piu avanti nel log append e dichiara movies_disabled=0 / native_thp=1. Tutti i profili avanzati usano packet compatto, batching/state compatti, queue depth 2, DL template cache, GX generations, layout cache, raw mesh cache e capacita EFB 512.

### P5 — EFB resident copy  (HARDWARE-TESTED, PARZIALMENTE EFFICACE)

Il percorso resident e stabile nei campioni G3D testati: 12 copie EFB vengono eseguite senza fault e senza usare il vecchio transient-FBO. Il percorso elimina readback+upload intermedi, ma nel resize Wii continua a fare nearest-neighbour CPU direttamente dal framebuffer mappato alla texture persistente.

Campione P7 serial 900:

- renderer ~118.0 ms;
- 12/12 EFB resident, 0 failure;
- sync EFB ~19.2 ms;
- resident resize/copy ~59.7 ms;
- totale EFB osservato ~78.9 ms;
- producer interval ~261.8 ms;
- GXDrawDone wait ~106.0 ms.

Risultato: P5 e hardware-stabile ma **non rimuove ancora il blocker EFB dominante** nel caso 960x544 -> dimensioni Wii, perche circa 59 ms restano nel resize CPU. La prossima evoluzione deve spostare anche il resize/filtro su GPU o evitare la copia quando semanticamente non osservabile.

### P6 — sicurezza risorse, capacita EFB e texture headroom  (HARDWARE-TESTED)

La protezione del riuso streaming non introduce uno stall significativo nei campioni normali: reuse_wait_us e tipicamente ~30-56 us. La capacita EFB 512 evita il vecchio overflow a 128 nei percorsi testati; nei nuovi log efb_cap=0 e non compaiono drop/cap failure nei frame campione. Le scene con 28 copy + 17 destroy rientrano nella nuova capacita.

Il texture shared headroom migliora la contabilita, ma la full-feature dimostra che **non basta ancora per il working set pesante**. A serial 2100 la cache arriva a ~10 MiB e registra tex_fail=1700; compaiono failure anche per texture 1024x512 e 256x256. Serve quindi eviction/pre-eviction bounded con protezione delle texture referenziate dal frame in-flight, non un semplice aumento del budget.

### P7 — producer UI quad runs  (HARDWARE-TESTED, EFFICACE)

Il merge producer funziona realmente sulla UI fedele. Nel campione full-features serial 900:

- requested_draws=665;
- draws dopo merge=179;
- producer_merge=486;
- renderer ~8.9 ms;
- producer interval ~125.5 ms;
- reuse_wait_us=31 us.

Quindi circa il 73% dei quad richiesti in quella schermata viene assorbito dal producer merge senza cambiare LYT faithful. Nei frame G3D il merge e naturalmente piu piccolo, ad esempio 126 draw a serial 1500 o 410 a serial 1800, perche la geometria prospettica non viene unita da questo pass.

### P8 — hot-shard O2  (HARDWARE A/B: NESSUN VANTAGGIO CONVINCENTE)

La variante con il solo shard billboard O2 e stabile, ma i confronti equivalenti non mostrano un miglioramento ripetibile. Un frame G3D di transizione con requested_draws=3382 misura ~1.109 s in P7 e ~1.103 s in P7-O2 (~0.6% a favore O2), mentre un campione serial 900 comparabile misura ~261.8 ms in P7 e ~263.3 ms in P7-O2 (~0.6% peggiore).

Conclusione: la differenza e nel rumore del test. **Non rendere O2 billboard default**; mantenere l infrastruttura selective-hot-shard, ma scegliere futuri shard solo da nuovi hotspot misurati dopo EFB/texture fixes.

### Full-features — hardware run avanzato

La sessione full-features corretta dichiara movies_disabled=0 e native_thp=1 insieme a tutto il percorso performance P4.1/P5/P6/P7. La sessione arriva almeno a serial 2100 e attraversa piu Scene Exit senza terminal crash nel tratto acquisito. Tuttavia il log non contiene ancora telemetria THP decode utile: l abilitazione del profilo multimedia e hardware-confermata, ma la riproduzione THP non e ancora dimostrata.

Campioni rappresentativi:

- serial 1200: render ~100.0 ms, DL ~13.3 ms, prebegin ~118.6 ms, p4_mesh=1289/55, resident=12/~59.3 ms, sync EFB ~16.8 ms;
- serial 1500: producer ~226.7 ms, GXDrawDone ~90.9 ms, render ~101.1 ms, resident=12/~58.8 ms, tex_fail=0;
- serial 1800: render ~108.3 ms, resident=12/~59.1 ms, tex_fail=0;
- serial 2100: producer ~554.7 ms, GXDrawDone ~239.1 ms, render ~291.1 ms, tex_fail=1700, transform_fail=32, EFB resident=9, 2 fallback readback e 1 copy failure.

A serial 2100 l EFB non e piu l unico problema: la saturazione texture e il fallback EFB fanno esplodere il frame. Questo diventa il secondo blocker prioritario insieme al resize EFB CPU.

### Stato performance dopo questi test

- P0-P4.1: DONE + hardware-tested.
- P5: implementato + hardware-tested; stabile, ma resize CPU ancora troppo costoso.
- P6: implementato + hardware-tested; safe reuse/capacity passano, texture headroom insufficiente nelle scene pesanti.
- P7: implementato + hardware-tested; producer UI merge efficace.
- P8 selective hot-shard: infrastruttura valida, ma il candidato billboard O2 non porta un vantaggio misurabile.
- full-features: profilo hardware-tested fino a scene avanzate; multimedia abilitato ma THP decode/playback non ancora validato.
- target 60 FPS / 16.67 ms: **non raggiunto**.

### Nuove priorita

1. Hardware-testare il candidato **P5.1-A** gia implementato: native-resolution resident EFB bounded + safe texture retry.
2. Verificare se il native-res trasforma davvero il resize dominante in `GpuSameSize` senza superare il budget EFB; se non basta, passare a uno scaler shader/GXM persistente nearest senza transient FBO.
3. Verificare sulla scena heavy se la retirement/fence bounded riduce `tex_fail=1700` senza introdurre stall eccessivi o use-after-free GPU.
4. Rimisurare GXDrawDone e prebegin_us dopo 1-3: oggi il frame G3D normale resta ~100-118 ms renderer e ~90-106 ms di wait GX.
5. Solo dopo scegliere nuovi hot shard O2/O3 dai profili aggiornati.
6. Correttezza 3D/materiali/xyzw e validazione THP restano task separati; non confondere l avanzamento performance con feature-completeness grafica/multimedia.

## M13.3-P5.1-A — primo hardware run + misura pulita richiesta (2026-09-05)

Stato: **native-res hardware confermato; performance A/B non ancora chiusa**.

Nuovi flag del profilo `full-features-p5_1`:

- `MKW_VITA_EFB_NATIVE_RES_COPY=1`;
- `MKW_VITA_TEXTURE_SAFE_RETRY=1`;
- `MKW_VITA_PERF_LOG=1` per acquisire la nuova telemetria;
- nessun `MKW_TRANSLATED_HOT_SHARDS` e nessun O2/O3 selettivo.

P5.1-A non riabilita il percorso transient-FBO rifiutato. Quando il budget EFB
4 MiB lo consente, una sampled copy che normalmente farebbe 960x544 -> Wii size
mantiene il backing fisico e usa `sceGxmTransferCopy` same-size. Se il backing
fisico non entra nel budget, il percorso torna automaticamente al nearest CPU
resident gia hardware-stabile. `sceGxmTransferDownscale` non viene usato perche
non replica in generale il reticolo nearest corrente.

La texture cache non e stata sostituita: la LRU/pre-eviction P6 e stata estesa
con protezione `useEpoch` e retirement esplicito. Se l'allocazione e bloccata
solo da texture ancora protette, il renderer puo fare al massimo quattro
flush+`glFinish` di emergenza per frame, segnare gli usi precedenti come ritirati,
evictare LRU sicure e ritentare. Le texture usate dopo la fence nello stesso frame
ricevono un nuovo epoch e tornano protette.

Offline PASS:

- ARM32 VitaSDK: `vita_efb.o`, `vita_texture_cache.o`, `aurora_packet_renderer.o`,
  `gx_backend.o` compilati con P4/P5/P5.1/P6/P7 attivi;
- host ASan/UBSan EFB FIFO/resample PASS;
- host texture budget P6 PASS;
- host texture budget P5.1 safe-retry PASS;
- `make -f Makefile.vita -j1 graphics-check` PASS.

Il nuovo log hardware deve contenere `resource_summary` e distinguere almeno:
`efb_path`, native/native-budget fallback, resident reason failures,
`tex_evict_total`, `tex_blocked_total`, `tex_protected`, `tex_retry`,
`retry_wait_us`, oltre a `tex_fail`, `resident`, `wait_gx`, producer e prebegin.
La fase resta aperta finche non esiste questo confronto hardware.

Artefatto P5.1-A da installare su hardware reale:

- `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-features-p5_1.vpk`
- SHA-256 VPK: `c0f6a38b96cf35a03b413b568f99c134cd9d91ac9772f1f0120ffd38a56de732`
- SHA-256 ELF: `92bf1f0bfe1aaefc00e21477add70b5858a18cda9539a203517b87f65fb737bd`
- VPK: 41,281,316 byte; ELF: 218,486,352 byte.

Packaging e ZIP verification passano. Il manifest conferma full-feature,
P4.1/P5/P5.1/P6/P7, queue depth 2, cap EFB 512, nessun hot shard O2/O3 e
translated baseline NEON `-Os`. Questo registra soltanto la riproducibilita della
build: **non e una validazione hardware delle prestazioni P5.1**.

### Primo hardware log P5.1-A

Il run reale del candidato raggiunge serial 981 senza fault nel tratto acquisito.
Il marker iniziale conferma `efb_native_res_copy=1`, `texture_safe_retry=1`,
movies/native THP attivi e transient-FBO disabilitato. A serial 900 il nuovo
`resource_summary` riporta `efb_path=12/0/0/0`, `native=12`,
`native_budget=0`, `resident_fail=0`: tutte le 12 copie del frame sono quindi
GPU same-size e il precedente nearest CPU non viene usato. EFB resident
3.546.816 / 4.194.304 byte, nessun overflow/fallback.

La cache texture non e ancora nel caso heavy: `tex_blocked_total=0`,
`tex_protected=0/0`, `tex_retry=0/0/0`; il run termina prima della scena che nel
baseline produceva `tex_fail=1700`.

I tempi di questo run non sono direttamente confrontabili col baseline:
`full-features-p5_1` forza `MKW_VITA_PERF_LOG=1`, che abilita `render_large` per
ogni frame >=1000 draw, progress ogni 128 draw e producer telemetry molto piu
frequente. Serial 900 misura ~148 ms renderer e intorno a serial 905 il producer
passa ~130 ms in `wait_gx`; serial 981 chiude a ~169 ms. Prima di attribuire
questa regressione al native-res serve quindi un A/B senza tracing intrusivo.

Nuovo profilo dedicato: `full-features-p5_1-measure`, identico al candidato ma
con `MKW_VITA_PERF_LOG=0`. In questa configurazione il `perf_summary` dettagliato
e il `resource_summary` restano disponibili ogni 300 serial, mentre vengono
eliminati i trace per-frame/per-draw che alterano il timing. Questo e il prossimo
artefatto da usare per il confronto 1200/1500/1800/2100 e per raggiungere la
pressione texture heavy.

Artefatto measurement verificato:

- `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-features-p5_1-measure.vpk`
- SHA-256 VPK: `e6eb8dc5be2e0e03ce9dfcfa6e38bfdda518070fec67ba6a43a7341aa0d4832a`
- SHA-256 ELF: `43aa341f6a6a575c9a039741cec1e00d962748a7a446ea6c1203e772fc8b468d`
- VPK: 41.280.337 byte; ELF: 218.461.580 byte.

Packaging, `verify-mkw-firstboot-vpk` e `unzip -t` passano. Il manifest mostra
`perf_log=0`, `efb_native_res_copy=1`, `texture_safe_retry=1`, movies/native THP,
queue depth 2, cap EFB 512 e nessun hot shard. L'ELF contiene sia il detailed
`perf_summary` sia `resource_summary`.
