# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`wiicompiled-vita` is a **PS Vita port fork of WiiCompiled** — a static recompiler that turns the
PAL `RMCP01` build of Mario Kart Wii into a native executable (no emulator, no interpreter, no JIT,
no PowerPC at runtime). Upstream (`zydezu/Wiicompiled`, forked at `2f98bb7`) is desktop-only
(Windows via LLVM-MinGW, experimental native Linux). This fork adds an ARM32 PS Vita target on top,
built through a separate `Makefile.vita`, with a Vita-specific GX/graphics backend under
`aurora-main/platforms/vita/` and `vita/`.

Everything the game touches at runtime lives in `runtime/`. There is **no Nintendo code, asset, or
game data** in this repo — the translator runs against a disc dump the user owns
(`Assets/main.dol` + `Assets/StaticR.rel`, SHA-256-pinned to PAL RMCP01).

Remotes: `origin` = this fork, `upstream` = `zydezu/Wiicompiled`, `aurora-vita` = the Vita aurora
fork vendored as `aurora-main/`.

## The two build worlds

### 1. Translator (shared, .NET 8) — the only thing with automated tests

The translator (`translator/src/Translator.Cli`) parses the DOL/REL, lifts PowerPC → IR/SSA →
typed C++, driven entirely by `projects/mkwii/recomp.yml` (no game data baked into the tool). It
is platform-independent; the same generated C++ feeds desktop and Vita builds.

```sh
dotnet build translator/Translator.sln -c Release
dotnet test  translator/Translator.sln -c Release        # the entire automated test suite
dotnet test  translator/Translator.sln -c Release --filter <Name>   # single test
```

The default test suite needs **no binaries and no game data** — you can work on the translator
with nothing else installed. CI (`.github/workflows/build.yml`) runs only this: translator build +
test on `windows-latest`. There is no CI for the runtime or the Vita port.

See `translator/README.md` for the four-command translation pipeline
(`translate-recursive` → `generate-data-init` → `emit-build-shards` → compile).

### 2. Desktop runtime build (upstream, not the focus of this fork)

`build.sh` (Windows / LLVM-MinGW, `x86-64-v3`) and `build-linux-native.sh` (experimental native
Linux) drive translation then `runtime/CMakeLists.txt` via CMake + Ninja + Clang. `runtime/`
hard-requires 64-bit Clang and `CMAKE_BUILD_TYPE=Release`. `--retro` on either script also builds
Retro Rewind as a separate static profile (see `profiles.retro-rewind` in `recomp.yml`).

### 3. Vita port build — `Makefile.vita`

Cross-compiles for ARM32 with VitaSDK (`arm-vita-eabi-g++`). **Always pass `-f Makefile.vita`**
(the plain `Makefile` is upstream's). Requires `VITASDK` set (defaults to `/usr/local/vitasdk`).

```sh
# End-to-end: translate the disc dump, then build the bootable vpk.
make -f Makefile.vita mkw-first-boot          # -> build/vita/wiicompiled-vita-mkw-firstboot-<variant>.vpk

# Just (re)run the translator into generated/ (needs Assets/main.dol + Assets/StaticR.rel)
make -f Makefile.vita generate

# Incremental verification targets, cheapest to most complete — use these as the
# feedback loop, since a full first-boot build is slow:
make -f Makefile.vita runtime-check           # core: memory, fiber_manager, ppc/fpu helpers
make -f Makefile.vita runtime-hle-check       # syntax-check non-GX HLE (os/audio/input/net/storage)
make -f Makefile.vita runtime-gx-bridge-check # syntax-check the MKW GX HLE bridge
make -f Makefile.vita graphics-check          # GX/VI bridge link + aurora GX frontend
make -f Makefile.vita runtime-native-check    # top-level native runtime sources
make -f Makefile.vita translator-vita-check   # translated synthetic PPC compiles for ARM32
make -f Makefile.vita full-translated-compile # compile every real MKW translated shard for ARM32
```

**Recursive-make pattern:** targets like `mkw-first-boot`, `recomp-probe`, `full-translated-compile`
depend on `generate` and then re-invoke `$(MAKE) -f Makefile.vita <...>-package`. This is
deliberate — the translator emits content-addressed shard filenames that don't exist until it has
run, so the second parse discovers the real build graph without hard-coding hashes.

`generated/` is gitignored and fully translator-owned; never hand-edit it. `Assets/` and
`PulsarPacks/` are gitignored (user-supplied).

## Runtime architecture

Guest code becomes C++ "shards" (`generated/build_shards/base_*`); `runtime/` supplies everything
around them:

- **`runtime/src/hle/`** — high-level emulation of Wii libraries, one subtree per subsystem:
  `gx/` (GX graphics command stream), `os/` (threads, scheduler, alarms, interrupts, time),
  `audio/` (AX/DSP mixer), `input/` (pad/wpad/kpad), `net/`, `storage/` (DVD, NAND/ISFS,
  Riivolution). `runtime/include/hle/` and `runtime/include/isa/` hold the shared headers.
- **`runtime/src/fiber_manager.cpp`** + `runtime/third_party/libco` — guest OS threads run as
  cooperative fibers (`libco`, `LIBCO_MP`). Guest thread switches are fiber switches.
- **`runtime/src/guest_flat_memory.cpp` / `memory.cpp`** — the flat guest address space
  (`0x80000000`, size `0x01A00000` per `recomp.yml`); `SystemBridge` owns the canonical layout
  and data-section init.
- **`runtime/src/abi_bridge.cpp` / `system_bridge.cpp`** — the calling-convention seam between
  translated PPC and native C++ (`TranslatedFunctionRegistry`, `CpuContext`,
  `InvokeIndirectCpu`). Compiled against the translator-generated `RuntimeConfig.h`.
- **`runtime/src/guest_stall_watchdog.cpp`** (Vita-only, `MKW_TARGET_VITA`) — snapshots guest
  scheduler/thread state to diagnose boot hangs.

### Vita-specific runtime pieces

- **`vita/main_vita.cpp`** — the Vita entrypoint. Deliberately **bypasses `runtime/src/main.cpp`**
  (desktop UI/DVD/CARD): it configures CPU clocks + thread affinity, brings up `SystemBridge`,
  the translated registry, the Vita GX backend, `GxGuestWrite` hooks, and the fiber manager, then
  jumps to the MKW entry `0x800060A4`. Boot progress is logged to
  `ux0:data/wiicompiled-vita/runtime.log`.
- **`vita/gx_backend.cpp`** (`WiiCompiledVita::GxBackend`, ~200 KB) — the Vita GX implementation:
  decodes the guest GX FIFO / display lists, runs XF/geometry/texture, submits through vitaGL.
  Extensive `Stats` counters in `vita/include/wiicompiled_vita/gx_backend.h`. Compiled with
  `-Werror=infinite-recursion`.
- **`vita/aurora_packet_renderer.cpp`** — aurora-packet path variant of the same.
- **`runtime/src/vita/guest_flat_memory_vita.cpp`**, `runtime/src/vita/aurora/` — Vita memory
  backend and aurora glue.
- **`aurora-main/platforms/vita/`** — `gfx/` (33 files: buffer pool, pipeline/texture caches,
  shader gen, vertex/texture decode, EFB, streaming arena, telemetry), `gx/` (aurora GX → Vita
  draw sink bridge), `integration/` (FIFO packet queue, GX capture/replay, frame trace).
- **`vita/tools/`** — `generate_translator_probe.py` (synthetic PPC for `translator-vita-check`),
  `sync_game_assets_from_vita.js`, `decode_gx_begin_hot.py`.

### Vita build variants (`Makefile.vita` variables)

- `MKW_VITA_AURORA_RENDERER` (1 = aurora renderer, 0 = legacy) — default 1.
- `MKW_VITA_VITAGL` (`speedhack` | `stock`) — default `speedhack`, which links a **prebuilt**
  patched vitaGL archive at `AURORA_VITAGL_LIB` (default
  `../aurora-vita-max-prehardware/third_party/vitaGL-speedhack-src/libvitaGL.a`, a sibling
  checkout); `stock` links `-lvitaGL` from VitaSDK.
- `MKW_VITA_LYT_DIRECT` / `MKW_VITA_LYT_FAITHFUL` — layout-rendering path selection.
- Variant-tagged objects live under `build/vita/mkwii_runtime/variants/<renderer>-<vitagl>/`.
- Translated shards build at `-Os -fno-asynchronous-unwind-tables` (`MKW_TRANSLATED_OPT`) — the
  MKW TUs are huge and the Vita loader is tight on loadable text.

## State of the port

Bring-up stage: the goal is a first successful in-game boot of PAL MKW on real Vita / Vita3K.
Rendering, performance and stability are actively in flux. `runtime.log` / `psp2core-*.psp2dmp`
in the repo root are captured hardware traces. `build/vita/hardware/vita-crashdump-tool/` is a
Rust CLI+GUI `.psp2dmp` analyzer (has its own `CLAUDE.md`).

## Conventions

- Match the surrounding code's style; the runtime is C++20 (`-std=gnu++20 -fexceptions`), HLE
  headers rely on exceptions being available even from translated shards.
- Behavior must match real hardware — for anything touching game behavior, be able to show it
  doesn't diverge (`CONTRIBUTING.md`). Retro Rewind intentionally patches some base-game behavior.
- Never commit Nintendo code/assets/game data, and never add links to game files.
- Per `CONTRIBUTING.md`: PR descriptions must be written by a human, not generated.
