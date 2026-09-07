# Handoff P5.9 — native audio pacing

Date: 2026-09-07

## Workspace / target

Repository:
`/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`

Target: Mario Kart Wii PAL RMCP01, static recomp PPC -> ARM32, Aurora -> vitaGL -> SceGxm, real PS Vita.

HEAD at this milestone:
`01f530b500940eb2f64c0aff13e5507d6876dbb5`

The worktree is intentionally dirty. Preserve every existing change. Do not use `git reset`, `git restore`, `git checkout --`, `git clean`, `git revert` or equivalent destructive operations. Do not commit/push unless explicitly requested.

Translated staging:
`build/vita/mkwii_translated_neon_os`

Translated flags:
`-Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard`

Keep full content, faithful LYT, movies/native THP, clip_w=1, P4.1/P5.1/P6/P7/P5.2-P5.8, frame queue 2, EFB cap 512, resident/native EFB, PERF_LOG=0 and no hot shard.

## P5.8 hardware result

P5.8 fixed a real functional bug: before it, `vita/audio_backend_vita.cpp` was a first-boot null sink and discarded all PCM. P5.8 added `MKW_VITA_NATIVE_AUDIOOUT=1`, MAIN SceAudioOut at 48 kHz stereo, 256-frame chunks, an 8-chunk bounded queue, a dedicated blocking-output worker, Wii AI BE right/left conversion, 32 -> 48 kHz resampling, and `-lSceAudio_stub`.

The new hardware run is preserved at:
`build/vita/runtime-p5_8-733ec259.log`

SHA-256:
`733ec2590f76e2d4610a5a7731e3329ba59188cb0f2a5d3bf17feba02c746274`

The user confirms that audio is audible on the real Vita, but clips/crackles badly when FPS are low.

The log confirms:

- full-content P5.8 startup, `native_audioout=1`;
- `vita_audioout opened port=7 type=main frames=256 rate=48000 queue_chunks=8`;
- `vita_audioout first_output port=7 frames=256 rate=48000`;
- at least one `vita_audioout queue_full dropping_new_pcm chunks=8`.

Therefore P5.8 is **hardware validated for real audio output**, not for clean playback/pacing.

The crackle is not evidence that SceAudioOut itself is slow. The same run still contains severe guest/producer stalls around ~1.3-1.4 s and transitions around ~6.3 s. An 8x256-frame queue is only ~43 ms at 48 kHz, so it cannot hide multi-second stalls. The observed pattern is device starvation during guest stalls followed by burst catch-up and host FIFO overflow.

Do not increase the audio FIFO as the main fix: it would add latency and still cannot absorb 1-6 second stalls.

## P5.9 implemented

New profile:
`full-content-p5_9-audio-pacing`

New flag:
`MKW_VITA_AUDIO_PACING=1`
Default remains 0 so P5.8 remains an A/B baseline.

Files changed for P5.9:

- `Makefile.vita`
  - adds `MKW_VITA_AUDIO_PACING ?= 0`;
  - adds the flag to native config and compile defines.
- `vita/tools/build_performance_profile.py`
  - BASE flag 0;
  - adds `full-content-p5_9-audio-pacing` = P5.8 + pacing=1.
- `vita/gx_backend.cpp`
  - startup marker adds `audio_pacing=%u`.
- `vita/audio_backend_vita.cpp`
  - pacing behavior and bounded telemetry.

P5.9 changes only host output behavior. It does **not** alter AI/AX/THP callbacks, guest interrupt state, service cadence, backlog semantics, scheduler behavior, or game simulation timing.

### P5.9 host behavior

After the first real PCM chunk:

1. the AudioOut worker no longer sleeps when the host queue becomes empty;
2. it keeps `sceAudioOutOutput` continuously clocked at 256-frame hardware cadence;
3. on underrun it submits a silence chunk;
4. the first transition into silence fades from the previous stereo sample to zero over 64 frames;
5. the first real PCM after silence/discontinuity fades back in over 64 frames;
6. on host FIFO overflow, P5.9 removes the oldest queued host chunk and keeps the newest PCM, reducing stale playback latency;
7. dropping a host chunk does not skip or coalesce guest AI/AX/THP work that has already happened.

New bounded markers:

- first underrun:
  `vita_audioout underrun inserting_silence frames=256`
- first overflow:
  `vita_audioout queue_full dropping_oldest_pcm chunks=8`
- every 512 output chunks:
  `vita_audioout stats real=<n> silence=<n> underrun=<n> drop_oldest=<n> high_water=<n> queued=<n>`

Interpretation:

- high `silence` / `underrun` = guest is not supplying audio continuously in real time;
- high `drop_oldest` = guest catches up in bursts faster than the hardware can play the recovered PCM;
- both high = strong starvation/catch-up oscillation caused by guest stalls.

P5.9 is a concealment/telemetry improvement, not an FPS optimization.

## P5.9 build

VPK:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_9-audio-pacing.vpk`

Bytes:
`41286477`

SHA-256:
`65bbfccbf4905279967d6b5ed9c8963ff57831e36c10c2e377a853b601ca449a`

ELF:
`build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_9-audio-pacing.elf`

Bytes:
`219550236`

SHA-256:
`94ae8a1c757c6b81a44ca20e57aa64b805b24f17675ae44d4e2a5d80e3bc4696`

Manifest:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_9-audio-pacing.manifest.txt`

Manifest confirms:

- movies enabled;
- native THP=1;
- PERF_LOG=0;
- queue depth=2;
- EFB cap=512;
- resident/native EFB=1;
- P5.2-P5.8 flags active;
- `native_audioout=1`;
- `audio_pacing=1`;
- no hot shards;
- translated NEON `-Os` staging.

Offline checks PASS:

- ARM32 compile;
- link with `-lSceAudio_stub`;
- VELF;
- FSELF;
- package;
- verify/unzip;
- `graphics-check`;
- `git diff --check`.

Only known pre-existing GCC/linker/GX enum warnings remain.

P5.9 is **NOT hardware tested**.

## Next hardware test

Install the P5.9 VPK and repeat at least boot/menu plus the transitions that currently produce audible crackle.

Require startup marker:

- `native_audioout=1`
- `audio_pacing=1`
- `fiber_irq_state=1`
- `wait_service_profile=1`
- `audio_wait_profile=1`
- `audio_ai_profile=1`
- `perf_log=0`
- movies/native THP on
- frame queue 2
- EFB cap 512.

Ask for two outputs:

1. subjective: is clicking/crackling reduced, unchanged or worse? Is pitch still plausible?
2. runtime log containing the new `vita_audioout` markers/stats.

Do not conclude that P5.9 improves performance merely because it sounds smoother. Compare producer/renderer metrics separately.

## Main performance blocker after P5.9

The main unresolved blocker is still guest execution with multi-second stalls, not the native device sink.

Prior/current evidence includes producer intervals around ~1.3-1.5 s and transitions around ~6.3 s. Some transitions are renderer waits, others have very little renderer wait and therefore require separate guest attribution.

Next performance work after the P5.9 hardware A/B:

1. attribute the ~1-6 s guest stalls to TaskThread / THP / scheduler / long guest fiber slices;
2. retain P5.7 AI-child attribution for `THP::AudioMixCallback` if AI remains a measured HLE cost;
3. do not reduce service count, drop guest backlog, remove mixer joins or bypass THP callbacks merely to improve FPS;
4. packet ownership/swap remains secondary until packet-copy time becomes material relative to the dominant stall.

Do not re-enable transient FBO/glBlitFramebuffer, do not use TransferDownscale as arbitrary nearest scaling, do not blindly remove glFinish, do not enable global O3/fast-math, do not increase memory budgets arbitrarily and do not select hot shards without profiling.

## Continuation prompt

Continue autonomously in `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`. Target Mario Kart Wii PAL RMCP01 PPC->ARM32, Aurora->vitaGL->SceGxm, real PS Vita. HEAD `01f530b500940eb2f64c0aff13e5507d6876dbb5`; preserve the intentionally dirty worktree and never use reset/restore/checkout/clean/revert; no commit/push unless requested. P5.8 native AudioOut is now hardware validated: user hears audio, log `build/vita/runtime-p5_8-733ec259.log` SHA256 `733ec2590f76e2d4610a5a7731e3329ba59188cb0f2a5d3bf17feba02c746274` shows `native_audioout=1`, `vita_audioout opened`, `first_output` and `queue_full dropping_new_pcm`; crackle occurs because multi-second guest stalls starve the ~43ms host FIFO and catch-up then overflows it. P5.9 `full-content-p5_9-audio-pacing` is implemented/offline-tested but NOT hardware-tested. It adds `MKW_VITA_AUDIO_PACING=1`: after first PCM, continuously clock SceAudioOut, insert silence on underrun, 64-frame anti-click fades, drop oldest host PCM on overflow to keep freshest samples, and bounded stats `real/silence/underrun/drop_oldest/high_water/queued`; it does not change AI/AX/THP or guest timing. Test VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_9-audio-pacing.vpk`, 41286477 bytes, SHA256 `65bbfccbf4905279967d6b5ed9c8963ff57831e36c10c2e377a853b601ca449a`; ELF SHA256 `94ae8a1c757c6b81a44ca20e57aa64b805b24f17675ae44d4e2a5d80e3bc4696`, 219550236 bytes. Next log must show `audio_pacing=1`; analyze `vita_audioout stats` and compare subjective crackle. High underrun/silence proves starvation; high drop_oldest proves burst catch-up. Do not increase FIFO as a fix. Regardless of audio smoothness, return performance priority to attribution of ~1-6s guest stalls (TaskThread/THP/scheduler/fiber slices), keeping P5.7 AI child profiler available. Preserve full content, faithful LYT, movies/native THP, clip_w1, queue2, EFB cap512/native resident, P4.1/P5.1/P6/P7/P5.2-P5.9, PERF_LOG0, no hot shard. Update PORTING_STATUS, IMPLEMENTATION and report after hardware results, and leave a new self-contained handoff before context exhaustion.
