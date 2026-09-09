# P6.8-MT handoff — three-core direct VitaGL pipeline

Date: 2026-09-08

Repository: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`

Manifest HEAD: `dd3385bae06c27bac426196d141eea7f5cb8dfb1`

## Safety

Preserve the dirty worktree. No reset/restore/clean/destructive checkout. Do not
commit or push unless explicitly requested.

## Architecture requirement

The active renderer is no-Aurora GX HLE -> native VitaGL. Treat the three available
Vita user cores as a pipeline:

1. USER_0: statically recompiled guest PPC + HLE producer.
2. USER_2: CPU graphics preprocessing and native audio output.
3. USER_1: sole VitaGL/GXM owner: upload/bind/draw/EFB/swap.

Never move VitaGL/GXM calls to helper threads without a proven shared-context design.
Instead move pure CPU work away from USER_1.

## P6.7 hardware evidence

- THP restuff is hardware validated: repeated 608x464 `restuffed_decode` succeeds;
  the final run has no `jpeg_pixels code=11`.
- final run has no frame upload/capacity failure, dropped geometry or GXM/fatal marker.
- serial 480: 16 logical / 15 physical draws, 200 vertices, zero EFB,
  `direct_upload_us=111`, `swap_us=223`, but `render_us=808221`.
- adjacent frames report USER_1 ~779-939 ms.
- serial 480 TEV: `tev_chain=0/12/0/0`, `tev_draw=0/15/0`.

Thus the current THP-phase USER_1 blocker is not draw count, VBO upload, swap or EFB.
CPU texture processing and/or USER_1 texture allocation/upload are the prime suspects.

## P6.8-MT implementation

Flag: `MKW_VITA_DIRECT_PREP_WORKER=1`.

- Added `HostThreadRole::GraphicsPrep -> USER_2`.
- Native SceAudioOut worker now explicitly applies `HostThreadRole::Audio -> USER_2`.
- Frame queue adds `PrepQueued` and `Prepping` between Packing and Ready.
- A dedicated USER_2 prep worker processes CPU-only resources before USER_1 sees the frame.
- First offload: THP YUV420 -> RGBA conversion via existing `ConvertTextureLevel0`.
- No VitaGL/GXM API is called on USER_2.
- Prepared textures are matched by pointer/revision/global epoch/generation/dimensions/chroma snapshot.
- Guest generation is checked before and after prep; USER_1 still revalidates before upload.
- At most two prepared RGBA textures are retained per frame slot; extras use synchronous fallback.
- `ResolveTexture` consumes prepared RGBA on a cache miss, otherwise preserves old behavior.

Telemetry:

`direct_prep worker_started affinity=USER_2`

`direct_prep serial=N affinity=USER_2 prep_us=X texture_us=Y textures=C failures=F`

Direct summary adds:

`prep=total_us/texture_us/count/fail texprep=prepared_hits/synchronous_decodes`

Startup marker adds `direct_prep_worker=1`.

## Artifact

Profile: `full-content-p6_8-mt-thp-prep`

VPK:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_8-mt-thp-prep.vpk`

Bytes: `41224387`

SHA-256: `a70dd685c799d7ffd4487b6324205f1d4a92acf217be9cc089386593621cc5ca`

ELF:
`build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_8-mt-thp-prep.elf`

Bytes: `218523256`

SHA-256: `689d92abfa00c49d55bf9ced153893cbc3bdd71f8e0df6c07dcc4b43840c23ca`

Offline PASS: compile/link, VELF/FSELF, package/verify/unzip, graphics-check,
git diff --check, zero AuroraPacketRenderer / aurora::vita::gfx. The direct backend
also graphics-checks with PREP_WORKER=0.

## Next hardware test

Use P6.8-MT and reproduce the P6.7 THP scene. Verify:

1. startup has `direct_prep_worker=1`;
2. `direct_prep worker_started affinity=USER_2`;
3. THP still has `restuffed_decode` and no decode_error;
4. `direct_prep ... textures>0`, failures remain low/zero;
5. `perf_summary texprep` has prepared_hits > 0;
6. compare USER_1 worker render time with the P6.7 0.78-0.94 s baseline;
7. no audio regression from sharing USER_2 and no geometry/GXM regression.

If prep succeeds but USER_1 remains hundreds of milliseconds, the next P6.9 action
is not another CPU decoder tweak: instrument and convert dynamic THP texture upload
from delete/gen/glTexImage-per-frame to a persistent streaming texture updated with
subimage/staging, while retaining the USER_2 conversion. In parallel, move vertex
transform/material/texgen and then generic GX texture decoders onto USER_2 so USER_1
converges toward bind/upload/draw/present only.
