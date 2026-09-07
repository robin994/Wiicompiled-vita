# Handoff P5.7 — AI callback attribution / next hardware build

## Workspace e target

- Repo: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`
- Target: Mario Kart Wii PAL RMCP01, static PPC -> ARM32, PS Vita reale.
- Renderer: Aurora -> vitaGL -> SceGxm.
- HEAD: `01f530b500940eb2f64c0aff13e5507d6876dbb5`.
- Worktree intenzionalmente dirty: leggere sempre file e diff prima di modificare.
- Vietati `git reset`, `git restore`, `git checkout --`, `git clean`, revert distruttivi.
- Commit/push non autorizzati.

## Configurazione da preservare

Full content, faithful LYT, `clip_w=1`, movies enabled, native THP, P4.1/P5.1/P6/P7/P5.2/P5.3/P5.4/P5.5/P5.6, queue depth 2, EFB command cap 512, EFB resident budget 4 MiB, native-resolution EFB, texture safe retry, `PERF_LOG=0`, nessun hot shard.

Translated staging:

`build/vita/mkwii_translated_neon_os`

Flags:

`-Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard`

Non introdurre global O3/fast-math, RAM arbitraria, transient FBO/glBlitFramebuffer, `sceGxmTransferDownscale` come nearest generico, rimozione cieca `glFinish`/join, contenuti ridotti o Vita3K come prova performance.

## Stato validato fino a P5.6

P5.4 per-fiber IRQ state ha eliminato gli skip IRQ anomali nei campioni: il precedente P5.3 `deferred=21407/20838/135` e passato in P5.4 a campioni con skipped=0. Mantenere `MKW_VITA_FIBER_IRQ_STATE=1`.

P5.5 ha separato VI/allarmi/audio dentro il timing service. Hardware: audio rappresenta ~91,5-96,4% del servizio nei campioni completi. Il tempo audio e incluso nell'attesa e puo sovrapporsi al renderer: non trattarlo come risparmio diretto.

P5.6 e hardware tested. Log originale SHA-256:

`0dca5e3a9d05f0bb5257b61f8225a45e34894f90ef321ba1c542f60f8d8d3d73`

Snapshot:

`build/vita/runtime-p5_6-0dca5e3a.log`

SHA snapshot:

`ac48598d605d20b6f81a1f674543f1beca4dbf1c54500c5f468ba3b1b6989ffa`

Marker valido: `fiber_irq_state=1`, `wait_service_profile=1`, `audio_wait_profile=1`, full content, `perf_log=0`.

P5.6 attribution:

| serial | blocks | AI us | join us | AI/audio | backlog max/last us |
|---|---:|---:|---:|---:|---|
| 448 | 2238 | 2617180 | 13960 | 97,02% | 487731/1520 |
| 450 | 528 | 660490 | 3308 | 97,16% | 54394/2115 |
| 749 | 332 | 572288 | 488151 | 53,64% | 6780856/6768856 |
| 896 | 504 | 589888 | 2982 | 97,95% | 14307677/13612057 |
| 900 | 28 | 41064 | 174 | 98,31% | 13899193/13862114 |
| 1000 | 532 | 921359 | 452572 | 66,56% | 23046612/23034612 |

Callback AI costante: `0x80551F00`, simbolizzata in `projects/mkwii/MAP.txt` come `THP::AudioMixCallback`; `0x8055206C` e `THP::MixAudio`. DMA 384 byte / 32000 Hz = 96 campioni stereo 16-bit, ~3 ms/blocco. A 749/896/900/1000 tutti i tick misurati raggiungono cap 4 e il backlog resta alto. Non droppare/coalescere callback o blocchi per migliorare il benchmark.

P5.6 serial 900: producer `227744 us`, wait_gx `55282`, wait_service `43391`, packet_copy `2753`, queue_wait `2`, residual stimato `169707`, renderer `61914`, submit `60654`, endframe `1005`, swap `132`. 3193 draw / 23036 vertici / 90 physical. EFB native 12/12, `efb_us=16804/0/0/0`, no resident failure, no texture fail/blocked, no mesh pressure.

Serial 1000: producer `1978352 us`, wait_gx `1662705`, wait_service `1415495`, audio `1384252`, join `452572`, AI `921359`. Join/AI sono gia inclusi nell'audio e non vanno sommati a WaitRender.

Ultimo VI P5.6: `deferred=13312/0/3786`, age ~20 ms, next due ~-3,36 ms. Restano debt transitori ma nessun ritorno al vecchio 97% skipped.

Separato dall'audio: una transizione guest multi-secondo ~6,25 s con wait_gx quasi nullo resta aperta e va attribuita a TaskThread/THP/scheduler se torna dominante.

## P5.7 implementata — NON hardware tested

Profilo:

`full-content-p5_7-audio-ai-profile`

Unico nuovo flag diagnostico rispetto a P5.6:

`MKW_VITA_AUDIO_AI_PROFILE=1`

P5.7 non cambia semantica audio. Implementazione:

- `runtime/include/audio_wait_profile.h`: child counters cache/mail e snapshot THP.
- `runtime/src/hle/audio/audio.cpp`: TLS gate solo durante AI callback nel render-wait; snapshot THP solo quando callback `0x80551F00`.
- `runtime/src/hle/os/os_cache.cpp`: `AudioAiSubtimer(0)` inclusivo attorno a `DcRangeOp`; invalidazioni restano intatte.
- `runtime/src/hle/audio/ax_mix.cpp`: `AudioAiSubtimer(1)` inclusivo in `SendMail`; lock e `HandleMail` invariati.
- `vita/gx_backend.cpp`: marker `audio_ai_profile` e `audio_ai_parts` stessa cadenza/serial di producer.
- Makefile/helper: flag default 0; profilo P5.7 eredita P5.6 e abilita solo AI profile.

`audio_ai_parts`:

- `cache_us=calls/total/max`
- `mail_us=calls/total/max`
- `thp_chain`, `thp_mode`, `thp_open`, `thp_flags`
- `snapshots=count/errors`

Cache/mail sono tempi INCLUSIVI dentro `ai_us`; non sommarli ad AI. I campi THP sono l'ultimo snapshot del frame, non una traccia completa. Interpretarli solo se `snapshots>0` e gli errori non invalidano la lettura.

Base THP usata dal profiler: `0x809BEB00` (`0x809C0000 - 5376`). Offset snapshot: chain 1444, mode 1456, open 160, flags 164. Stato byte165 = `(flags >> 16) & 255`, byte167 = `flags & 255`.

## Artefatto P5.7 da testare

VPK:

`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_7-audio-ai-profile.vpk`

- bytes: `41282688`
- SHA-256: `45de24a7f4304ed08a071d14dfc07ab47a0547296a933f44b1908c3920c2de6b`

ELF:

`build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_7-audio-ai-profile.elf`

- bytes: `219336952`
- SHA-256: `5ded393009e97c9ec5cd87738ae8a6af19ad7b7a5f02c388c81d5b3e8ce0a2dd`

Manifest:

`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_7-audio-ai-profile.manifest.txt`

- bytes: `1775`
- SHA-256: `49f05a34be329553eb94c8a0b4fd42a3cc9f4dbbf59498d4e2d9809bcd2d7a42`

Evidence accanto al VPK, `hardware_validated=false`.

Build ARM32/link/VELF/FSELF/package/unzip PASS. `build/vita/p5_7-graphics-check.log` PASS, solo warning enum preesistente GXVert.cpp. `git diff --check` PASS. Gli hash dei sorgenti chiave correnti coincidono con l'evidence della build, quindi il VPK non e stale rispetto al source dirty corrente. Nessuna build in corso.

Build command di riferimento:

`python3 vita/tools/build_performance_profile.py full-content-p5_7-audio-ai-profile --jobs 8`

## Log da NON usare

Il file allegato nella sessione corrente e stato preservato come:

`build/vita/runtime-unmatched-f0b7b8ba.log`

- bytes: `548979`
- SHA-256: `f0b7b8baa65ebe1caa62e71017f4a30acb314990eee8c2be016e8ca68dcbfaa9`
- 5349 righe / 3 boot.

NON e P5.6/P5.7. Boot1: EFB cap128, `perf_log=1`, compact path off, queue1. Boot2/3: movies/native THP off, skip EFB/billboards/lighttexture e solid forced. Non contiene i marker P5.4-P5.7. Tenerlo solo come storico P0/P1.

## Prossimo test hardware

1. Conservare/rinominare il runtime.log precedente prima di avviare P5.7.
2. Installare esattamente il VPK P5.7 con SHA sopra.
3. Ripetere la stessa sequenza menu -> transizione -> heavy, idealmente oltre serial 1000.
4. Il boot valido deve contenere nello stesso marker:
   - `fiber_irq_state=1`
   - `wait_service_profile=1`
   - `audio_wait_profile=1`
   - `audio_ai_profile=1`
   - `perf_log=0`
   - movies enabled / native THP
   - queue depth 2
   - EFB cap 512.
5. Deduplicare righe interlacciate e correlare per serial:
   - `producer_frame`
   - `wait_service_parts`
   - `audio_wait_parts`
   - `audio_ai_parts`
   - `perf_summary` / `resource_summary`
   - `vi_stall` / watchdog.

Decisione successiva guidata dai dati:

- Se `cache_us` domina AI: separare quali range/invalidation path costano; valutare fast reject solo per range realmente non sovrapposti con prova semantica. Non rimuovere invalidazioni alla cieca.
- Se `mail_us` domina AI: profilare `HandleMail`, lavoro inline, attese lock/worker e pubblicazione. Non rimuovere join/mail.
- Se cache e mail sono piccoli rispetto ad AI: profilare internamente `THP::MixAudio` e la callback indiretta selezionando il ramo tramite snapshot THP. Non abilitare hot shard solo perche il simbolo esterno e noto.
- Se torna dominante lo stall guest ~6,25 s fuori WaitRender: spostare priorita su TaskThread/THP/scheduler attribution; non confonderlo con il costo audio dentro WaitRender.
- Packet ownership/swap resta secondario (~2,7-5,5 ms storico) finche esistono blocker da decine/centinaia/migliaia di ms.

## Documenti

- `docs/performance-analysis-2026-09-07/P5_6-P5_7.md`
- `docs/performance-analysis-2026-09-07/REPORT.md`
- `PORTING_STATUS.md`
- `docs/performance-60fps-2026-09-05/IMPLEMENTATION.md`

## Prompt autosufficiente per la prossima sessione

Continua autonomamente in `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita` il port di Mario Kart Wii PAL RMCP01 PPC->ARM32 per PS Vita reale, Aurora->vitaGL->SceGxm. HEAD `01f530b500940eb2f64c0aff13e5507d6876dbb5`; worktree dirty: leggi file/diff e preserva tutto. Vietati reset/restore/checkout/clean/revert; niente commit/push. Mantieni full content, faithful LYT, clip_w=1, movies/native THP, P4.1/P5.1/P6/P7/P5.2-P5.6, queue2, EFB cap512/budget4MiB, PERF_LOG=0, no hot shard, translated NEON `-Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard`.

P5.6 e hardware tested: log SHA `0dca5e3a9d05f0bb5257b61f8225a45e34894f90ef321ba1c542f60f8d8d3d73`, snapshot `build/vita/runtime-p5_6-0dca5e3a.log`. La callback AI `0x80551F00` = `THP::AudioMixCallback` domina spesso il tempo audio; join e significativo in alcune finestre; backlog arriva a ~23s. Serial900 P5.6: producer227744us, wait_gx55282, wait_service43391, packet_copy2753, residual~169707, renderer61914; EFB native12/12, niente texture/cache pressure. Stall guest ~6,25s fuori WaitRender resta separato.

P5.7 e gia implementata/offline tested e misura cache DC + AXWii::SendMail inclusivi dentro AI e snapshot THP, senza cambiare semantica. Testa `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_7-audio-ai-profile.vpk`, 41282688 byte, SHA `45de24a7f4304ed08a071d14dfc07ab47a0547296a933f44b1908c3920c2de6b`; ELF SHA `5ded393009e97c9ec5cd87738ae8a6af19ad7b7a5f02c388c81d5b3e8ce0a2dd`. Richiedi marker fiber_irq_state=1, wait_service_profile=1, audio_wait_profile=1, audio_ai_profile=1, perf_log=0, full content/THP, queue2, cap512. Correlare per serial producer/wait_service/audio_wait/audio_ai. cache_us/mail_us sono inclusi in ai_us e non vanno sommati. Se cache domina, profilare invalidation range; se mail domina, profilare HandleMail/worker; se entrambi piccoli, scendere dentro THP::MixAudio/callback indiretta. Non rimuovere join/cache maintenance, non droppare backlog, non fare P5.8 speculativa.

Il file `build/vita/runtime-unmatched-f0b7b8ba.log` SHA `f0b7b8baa65ebe1caa62e71017f4a30acb314990eee8c2be016e8ca68dcbfaa9` e un log storico P0/P1, NON P5.6/P5.7: ignorarlo per l'A/B. Aggiorna report/status dopo il nuovo hardware log. Quando stai finendo il contesto, lascia un report persistente con stato, log, modifiche, build/hash/test, performance, blocker, esperimenti esclusi e worktree, piu un nuovo prompt autosufficiente per continuare.
