# WiiCompiled Vita / Mario Kart Wii – Porting Status

> **Snapshot:** 2026-09-03 (session 2 — see section 16 for the milestone log)  
> **Repository:** `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`  
> **Target:** Mario Kart Wii PAL `RMCP01`, static recompile PowerPC → ARM32, PS Vita hardware  
> **Current HEAD observed:** `b554833 checkpoint`

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

1. make the four main-menu cards render correctly and at an acceptable frame rate;
2. make `Single Player` transition to the expected menu (`Grand Prix`, `Time Trials`, `VS Race`, `Battle`) instead of remaining on a black screen;
3. identify and remove the dominant guest-CPU bottlenecks before spending time on lower-resolution rendering;
4. keep improving Aurora-Vita correctness so later 3D scenes, EFB effects, textures and UI do not require a renderer rewrite.

---

## 2. Hardware-confirmed state

The latest hardware-tested renderer/profiling build before the current watchdog work is:

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

`build/vita/wiicompiled-vita-mkw-firstboot-aurora-speedhack-nomovies-schedtrace.vpk`
(M3, this session). Adds `MKW_VITA_DISABLE_MOVIES` plus a scheduler-event ring-buffer
trace dumped at each stall. Pending hardware validation.

Prior builds this session:
- `...-phase-fiber-watchdog-nomovies.vpk` (M1) — SHA-256 `8a7f5401814176556baecd722774dd8509f142f682b7500d8fe2c9811b4f1f69` — movie disable only. Hardware-tested (see section 16).
- `...-nomovies-threaddump.vpk` (M2) — SHA-256 `610561d3947c44ea24d6c5040867c32c7b772698713faae9f7cea83f5c92f38d` — adds the guest-thread-table dump. Hardware-tested (see section 16).

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

## Next

- M3 hardware test -> identify the missing signal.
- M4 (scheduler fix): most likely defer async I/O callbacks + drain them from the idle loop, or
  fix the specific lost-wakeup window. Then a separate pass on the ~230 ms/frame guest CPU on the
  card menu.
- Later: native THP decode (re-enable movies), default `RFL_DB.dat`, texture-cache / EFB /
  `GXInvalidateTexAll`-per-frame cleanups.
