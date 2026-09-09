# P6.12 handoff — direct texture anti-thrash

Date: 2026-09-09

Repository: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`

Source HEAD recorded by manifest: `dd3385bae06c27bac426196d141eea7f5cb8dfb1`.
The worktree is intentionally dirty. Preserve all pre-existing changes; do not use
reset/restore/clean or destructive checkout operations.

## Hardware evidence from P6.11

User-supplied `runtime.log` SHA-256:
`2262870c3b36e2107f15a2352a7af0a44acb25edb19fa02f3d5fc11ddb6eb511`.

P6.11 hardware confirms:

- direct-vitaGL renderer, no Aurora packet renderer;
- USER_2 prep worker active;
- P6.9 vertex prep active and cheap;
- P6.10 generic texture prep active but capped at 8 sources;
- P6.11 state/run classification active and cheap;
- THP restuffed/native decode succeeds repeatedly.

Heavy repeated scene:

- 178 logical draw, 2656 vertices;
- ~162 physical draw, 16 merges;
- USER_2 total ~182 ms: vertex ~1.9 ms, texture ~180 ms, state ~65 us;
- only 8 prepared textures, 154 preparation failures/skips;
- USER_1 still performs up to 138 synchronous texture decodes;
- `render_us` reaches ~3.90 s;
- producer settles around ~4.25 s/frame and audio catch-up/backlog grows as a
  consequence.

This makes texture churn the immediate renderer blocker. Do not optimize vertex or
state classification again before testing the texture fix.

## Root cause fixed in P6.12

The direct texture cache matched tracked guest sources using all of
`dataRevision`, `textureGlobalEpoch`, and `sourceGeneration`. `GXInvalidateTexAll`
increments the global epoch, so unchanged tracked pixels could miss every time.

Aurora already had the correct policy in `FoldTextureRevision`: when guest memory
is tracked, the guest-write generation itself is the content revision. Object
rebuilds / GX cache invalidation do not imply changed pixel bytes.

P6.12 adds kill switch:

`MKW_VITA_DIRECT_TEXTURE_CACHE_ANTITHRASH=1`

When enabled:

1. tracked main/THP plane sources compare by guest-write generation;
2. untracked sources keep conservative revision + epoch matching;
3. direct cache metadata capacity is 256 entries instead of 32;
4. GPU texture byte budget remains exactly 12 MiB;
5. recycled frame-slot RGBA results are reused when source generation still
   matches; current active prep stays max 8 textures / 4 MiB;
6. telemetry separates reuse/cap/budget/decode failures and budget/entry eviction.

P6.12 OFF preserves the old P6.11 comparison semantics and passes graphics-check.

## Artifact

VPK:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_12-texture-antithrash.vpk`

- bytes: `41225841`
- SHA-256: `95cb1cb123b74d30eda905709f211c489d149f1ea38ebb05e830a975db8ab6da`

ELF:
`build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_12-texture-antithrash.elf`

- bytes: `218662296`
- SHA-256: `b19538bea6eb06be4e553d90121b693dba36dff6fa100e53617f189859dc843d`

Manifest:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_12-texture-antithrash.manifest.txt`

- bytes: `2100`
- `direct_texture_cache_antithrash=1`
- translated hot shards empty;
- full content, THP, audio pacing, direct EFB/state/TEV and P6.9-P6.11 remain enabled.

Validation PASS:

- P6.12 ON graphics-check;
- P6.12 OFF/P6.11 A/B graphics-check;
- ARM32 compile + link;
- VELF + FSELF;
- VPK package + verify;
- `unzip -t`;
- `git diff --check`;
- ELF string audit: zero `AuroraPacketRenderer`, zero `aurora::vita::gfx`.

## Next hardware test

Startup must contain:

`direct_texture_cache_antithrash=1 texture_cache_cap=256 texture_cache_budget=12582912`

Compare the repeated 96-run and 178-run scenes with P6.11. Primary acceptance:

- `direct_prep ... reuse=` becomes non-zero after frame-slot warm-up;
- `texture_us` drops substantially below ~180 ms on repeated static sources;
- `perf_summary ... texprep=prepared_hits/synchronous_decodes` shows synchronous
  decode far below the old 138 on the 178-draw scene;
- cache hits dominate misses;
- `entryEvictions` should be near zero with the 256-entry metadata table;
- if `budgetEvictions` is high, 12 MiB is the real GPU byte-pressure limiter;
- no source-race, upload-failure, GXM or frame-upload regression.

Do not raise the texture byte budget merely because misses remain. If misses stay
high while both eviction counters stay low, instrument guest-write generation
granularity and identify which source ranges are genuinely changing. If cache
behavior is fixed but USER_1 remains hundreds of milliseconds, profile
`glTexImage2D`/allocation versus draw/state submission next.
