# WiiCompiled Vita direct renderer handoff — P6.7

Date: 2026-09-08

Repository: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`

Manifest HEAD: `dd3385bae06c27bac426196d141eea7f5cb8dfb1`

## Safety

The working tree may be dirty. Preserve every existing user change. Do not use
`git reset`, `git reset --hard`, `git checkout --`, `git restore`, `git clean` or
other destructive rollback. Do not commit/push unless explicitly requested.

## Architecture

Active target:

`statically recompiled PPC -> GX HLE compatibility/state -> MKW native Vita renderer -> vitaGL -> SceGxm`

Aurora renderer is not part of the final direct path. Shared Aurora/Dolphin GX
headers and compatibility entry points may remain because translated guest code
uses that ABI. Do not reintroduce `vita/aurora_packet_renderer.cpp` or
`aurora-main/platforms/vita/gfx/*` into a direct profile.

## Hardware evidence before P6.7

The latest P6.4 hardware run reaches G3D/THP with the direct renderer. P6.1b's
12,288 draw / 73,728 vertex bounds remove the previous packet/upload capacity
failure in the observed scene. No fatal/SceGxm renderer marker was observed.

Remaining evidence:

- guest/producer stalls can exceed one second with nearly zero renderer wait;
- other `wait_gx` intervals contain 0.5-0.9 s of USER_0 HLE audio service, so old
  wait totals cannot be treated as renderer cost;
- native THP still produced `decode_error phase=jpeg_pixels code=11`;
- old 300-frame summary interval missed TEV counters during the short interesting
  scene window.

## P6.4a — real direct worker timing

Flag: `MKW_VITA_DIRECT_WORKER_TIMING=1`.

USER_1 publishes timing independently after each completed direct frame. USER_0
producer logs expose:

`worker=<serial>/<render_us>/<swap_us>/<age_us>`

`WaitRender` also records `wait_sleep_us = total wait - wait callback service`, so
HLE work can no longer be mistaken for VitaGL/GPU time. P6.4a sets
`MKW_VITA_PERF_SUMMARY_INTERVAL=60`.

## P6.5 — THP JPEG recovery

Flag: `MKW_VITA_THP_UNESCAPED_FIX=1`.

Standard `tjDecompressToYUVPlanes` is tried first. On failure, the decoder finds
SOS and re-stuffs entropy `0xFF` bytes before bounded candidate EOI attempts.
Markers:

- `thp: turbojpeg_standard_fail`
- `thp: restuffed_decode`
- `thp: restuffed_fail`

Do not disable movies as a workaround. If hardware still reports
`restuffed_fail`, inspect the actual turbojpeg error and THP entropy/restart-marker
handling before changing the movie state machine.

## P6.6 — bounded render-wait audio service

Flag: `MKW_VITA_AUDIO_WAIT_BLOCK_BUDGET=1` in the P6.6+ profile.

Only the render-wait pump is limited to one AI DMA block per service iteration.
Normal audio polling remains four blocks. The accumulator/backlog and every guest
callback remain pending, not dropped. `audio_wait_parts` reports `budget=1`.

## P6.7 — first native two-texture TEV subset

Flag: `MKW_VITA_DIRECT_TEV_TWO_TEXTURE=1`.

Recognized exact subset:

- exactly two sampled/classified TEV stages;
- both use `GX_TEXCOORD0`;
- first stage is `MODULATE` or `REPLACE`;
- second stage is `MODULATE`;
- both GX texture maps are bound.

The second GX texture is snapshotted with revision/global epoch/guest generation
and submitted through VitaGL texture unit 1 using the existing TEX0 vertex stream.
Unsupported chains remain fallback. Telemetry:

`tev_chain=<collapsed>/<unsupported>/<multitexture>/<two_texture_native>`

`tev_draw=<simple>/<fallback>/<two_texture>`

Do not expand every vertex to TEX0..TEX7 without hardware evidence; implement the
remaining MKW signatures observed in fresh logs.

## Final offline artifact

Profile: `full-content-p6_7-direct-tev-two-texture`

VPK:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_7-direct-tev-two-texture.vpk`

Bytes: `41224294`

SHA-256: `ed2c3746a1144315dc492be72b61959e553ad01804412336fc9766b6ca039eeb`

ELF:
`build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_7-direct-tev-two-texture.elf`

Bytes: `218442188`

SHA-256: `b9c092164ecec9ce4eca7077da50389c6112955989b506f26ef9f5747b65f83e`

Manifest:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_7-direct-tev-two-texture.manifest.txt`

Offline checks PASS:

- ARM32 compile/link;
- VELF/FSELF;
- package + verify + `unzip -t`;
- `graphics-check` (only pre-existing GXVert enum warning);
- `git diff --check`;
- ELF string audit: zero `AuroraPacketRenderer`, zero `aurora::vita::gfx`.

P6.7 is **not hardware validated yet**.

## Next hardware test

Use P6.7 and collect a fresh `runtime.log`. Required observations:

1. Startup marker contains direct renderer plus `direct_tev_two_texture=1`,
   `direct_worker_timing=1`, `thp_unescaped_fix=1`, `audio_wait_block_budget=1`.
2. No `frame_upload_failed`, `begin_cap`, `raw_cap`, dropped geometry or GXM crash.
3. Compare `worker` render/swap time against `prior_wait_us`, `wait_sleep_us` and
   `wait_service_parts`; only the worker number is renderer cost.
4. `audio_wait_parts` must show `budget=1`; verify backlog is preserved and service
   no longer monopolizes one wait iteration.
5. Inspect `perf_summary` every 60 frames for physical/merged/state-skip/upload/EFB
   and `tev_chain`/`tev_draw` coverage.
6. THP: success is `restuffed_decode` followed by native decode/movie progress.
   `restuffed_fail` or continued `jpeg_pixels code=11` remains a blocker.

Near-term target is visual correctness and playable 30 FPS. Do not claim 60 FPS
until measured full-content frames are consistently below 16.67 ms with guest,
renderer, THP and audio all functioning.
