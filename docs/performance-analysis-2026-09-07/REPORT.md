# P5.5 hardware: audio domina WaitRender; P5.6 attribution — 2026-09-07

## Stato
P5.5 hardware tested, non un'ottimizzazione funzionale. P5.4 mantiene skipped IRQ=0 nei campioni VI del quarto boot. Nessuna validazione generale audio/scheduler o 60 FPS. Il log raggiunge present=960 e termina dopo xf_indexed n=16384; non prova deadlock, uscita pulita o completamento della gara. P5.6 è un candidato diagnostico che separa join/sink/AI/AX e backlog senza modificare comportamento. Nessuna correzione del mix o rimozione di servizi è ancora giustificata dai tempi aggregati.

## Log hardware
File runtime.log append-mode con quarto boot alla riga 1484, marker alla 1520: fiber_irq_state=1, wait_service_profile=1, full content/PERF_LOG=0. SHA-256 `f1656b83b7bc2b44a0378d2c9f36e68e9347bc36cbd966360873f0562edd5d7a`.
Snapshot preservato: `build/vita/runtime-p5_5-f1656b83.log`, SHA-256 `c59e0dd1e8f131f0b940e8ff15bfa5567316b45ff0c0d7fe4a20069749c52fff`. Nessuna modifica al log originale.
P5_5-metrics.json separa frammenti interlacciati e deduplica per serial. Serial 654 ha una riga parts ripetuta e nessuna producer completa: non ricostruire il totale esterno o contare il duplicato. Il marker è compatibile con P5.5; il log non incorpora hash ELF.

| serial | service totale us | VI us | alarm us | audio us | audio/service | residuo contabile us |
|---|---:|---:|---:|---:|---:|---:|
| 424 | 2844471 | 42780 | 194844 | 2602771 | 91.50% | 4076 |
| 426 | 734743 | 10107 | 50103 | 673555 | 91.67% | 978 |
| 654 | n/d | 4769 | 15356 | 1041151 | n/d | n/d |
| 861 | 633078 | 4184 | 21632 | 606954 | 95.87% | 308 |
| 900 | 42644 | 290 | 1233 | 41101 | 96.38% | 20 |

Nei quattro campioni completi calls coincide tra producer e parts. Residuo contabile 0,047–0,143%: verifica coerenza dell'instrumentazione, NON una misura dell'overhead totale del profiler. Non è possibile stimare un guadagno FPS da questi tempi inclusivi: audio può attendere il worker, e il renderer avanza contemporaneamente.

## Performance e confronto
| metrica | P5.5 |
|---|---:|
| producer serial 900 | 223381 us |
| wait_gx / wait_service serial 900 | 53188 / 42644 us |
| queue_wait / packet_copy serial 900 | 2 / 2812 us |
| residual stimato serial 900 | 167379 us |
| render / submit serial 900 | 62856 / 61694 us |
| endframe / swap serial 900 | 906 / 129 us |
| draws / physical / vertices | 3192 / 89 / 23032 |
| EFB efb_us | 17838/0/0/0 |
| EFB path / native / failures | 12/0/0/0 / 12 / 0 |
| EFB byte / budget | 3546816 / 4194304 |
| raw mesh byte / eviction/clear/skip | 515823 / 0/0/0 |
| texture fail / blocked / retry_wait | 0 / 0 / 0 |
| reuse_wait | 55 us |

P5.4 serial 1200 aveva geometria diversa (2304 draws): non confrontarlo come scena equivalente con il 900 P5.5. Finestra simile 3193 draws/23036 vertici: P5.4 frame 994 producer 1071968, wait_gx 818662, wait_service 628525 us; P5.5 frame 861 producer 1080234, wait_gx 824708, wait_service 633078 us. Variazione ~0,8% producer nel singolo campione, non benchmark statistico.
Transizione 533 draws/3624 vertici: P5.4 frame 928 producer 6250828 us; P5.5 frame 795 6256811 us, wait_gx 65 us, residual stimato 6255893 us. Blocker guest invariato.
GX CPU P5.5 frame=600 (contatore distinto dal serial producer): DL=14101 us, prebegin=85777, tail=4007, copydisp=1302; DL cache 69 entry/28540 byte senza eviction/clear/skip. Non allineare automaticamente al serial renderer 900.
Ultimo VI sample: deferred=10804/0/2259, age=2108162 us e next due=-2091496 us durante transizione. Prima age=14364 us, next due=2302. IRQ skip resta corretto; debt transitorio permane e manca un watchdog finale equivalente al P5.4.

Residual = interval - wait_gx - queue_wait - packet_copy è una stima, perché i confini di interval e copia corrente differiscono. Non sottrarre wait_service due volte: è incluso in wait_gx.

## Root cause delimitata e codice
L'audio costituisce 91,5–96,4% del wait service nei campioni completi; serial 654 mostra audio 1041151 us contro VI+alarm 20125 us. Causa interna non ancora attribuita.
Audio_HLE_Tick in runtime/src/hle/audio/audio.cpp esegue fino a 4 blocchi per tick, ognuno con JoinMixWorker, sink, callback AI e ServiceDeferredCallbacks AX. JoinMix in ax_mix.cpp attende m_mixPending/m_mixBusy e pubblica gli aux output; i tempi AI possono includere ulteriori join/mix invocati dal guest. ServiceDeferredCallbacks drena callback resume; non modificarne il ciclo senza evidenza.
Il sink vita/audio_backend_vita.cpp è non-blocking e non apre SceAudioOut: output audio udibile non validato. Nessun bypass audio introdotto.

## Modifiche P5.6
- runtime/include/audio_wait_profile.h: nuovo snapshot POD dei contatori, API wrapper wait e take/reset, compilati solo Vita+flag.
- runtime/src/hle/audio/audio.cpp: scope thread_local del solo audio chiamato da WaitRender; timer RAII join/sink/AI/AX (clock solo se scope attivo), contatori polls/ticks/reentries/blocks/capped, backlog max/ultimo, ultimo callback e formato DMA. Snapshot azzerato a ogni SubmitFrame. Il guard scope si ripristina anche su eccezione. Statistiche solo USER_0; nessun accesso nuovo del worker.
- vita/main_vita.cpp: seleziona wrapper profilato con MKW_VITA_AUDIO_WAIT_PROFILE; mantiene ordine VI/alarm/audio e gli stessi limiti.
- vita/gx_backend.cpp: nuovo marker audio_wait_profile e riga audio_wait_parts sullo stesso serial/cadenza del producer. Nessun log per blocco/tick, nessun buffer dinamico nuovo.
- Makefile.vita: flag default 0, COMMON_FLAGS, config stamp/manifest.
- vita/tools/build_performance_profile.py: full-content-p5_6-audio-wait-profile eredita P5.5, unico cambio AUDIO_WAIT_PROFILE=1.
- Documenti di stato aggiornati append-only. Patch P5.4/P5.5 preesistenti preservate.

## Come leggere il prossimo test
Marker fiber_irq_state=1, wait_service_profile=1, audio_wait_profile=1, PERF_LOG=0. Conservare/rinominare il log precedente, ripetere menu/transizione/heavy. P5.6 ancora NON hardware tested.
- wait_service_parts audio_us = totale/max per callback wait.
- audio_wait_parts join_us/sink_us/ai_us/ax_us = calls/totale/max (tre valori, non due); AI include bookkeeping busy e callback, AX comprende il drain intero, non singola callback resume.
- polls dovrebbe coincidere con wait_service_parts.calls. ticks può differire per IRQ-off/reentry; reentries sono tick rientrati che non drenano nuovi blocchi.
- blocks conta blocchi completati; capped conta tick arrivati al limite 4, NON implica da solo backlog residuo.
- backlog_us = massimo/ultimo tempo accumulato in microsecondi, osservato nei tick misurati; non è dimensione buffer hardware né totale del run. dma=byte/rate ultimo; callback ultimo indirizzo AI osservato. Zeri sono possibili in frame senza tick attivo.
- Somma dei tempi di fase deve rientrare nel tempo audio esterno a parte clock/bookkeeping. Reset anche nei frame non loggati. Per il costo per blocco usare totale/calls e osservare max; confrontare backlog crescente e capped.

Se domina join: misurare il lavoro worker e il punto di pubblicazione, verificare backlog e dipendenze prima di una modifica. Se domina AI/AX: simbolizzare callback e profilare il lavoro interno, non applicare hot shard senza dati. Se domina sink: controllare fallback Memory::Read16/conversione e buffer sorgente. Una proposta di budget/drain o ownership deve preservare ordine, progresso e lifetime ed avere kill switch A/B; non ridurre servizi solo perché costosi.

## Blocker ordinati
1. Transizione guest ~6,25 s, non spiegata dai wait; ancora servono dati TaskThread/THP/scheduler.
2. Audio nei wait fino a 2,60 s cumulativi; P5.6 separa join e callback, prossimo intervento dipende dal risultato hardware.
3. Heavy producer ~223 ms/renderer ~63 ms; prebegin alto nella finestra GX, EFB ~18 ms. Nessuna cache pressure nuova: niente budget aumentati.
4. Packet copy ~2,8 ms secondario, swap ownership rinviato.

## Esperimenti esclusi
No transient FBO/glBlitFramebuffer, TransferDownscale nearest, rimozione cieca glFinish, global O3/fast-math, hot shard senza profiling, aumento RAM indiscriminato, contenuti ridotti o Vita3K per prestazioni. Mantenere P4.1/P5.1/P6/P7/P5.2/P5.3/P5.4, full content/faithful LYT/clip_w=1/movies/native THP, queue 2, EFB cap512/budget4MiB, PERF_LOG=0, no hot shard.

## Build/test
Comando in esecuzione: `python3 vita/tools/build_performance_profile.py full-content-p5_6-audio-wait-profile --jobs 8`.
Log build/vita/p5_6-audio-wait-profile-build.log. Staging NEON -Os conservato: build/vita/mkwii_translated_neon_os, -Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard. Artefatti e risultati finali aggiunti sotto.

## Worktree
HEAD 01f530b500940eb2f64c0aff13e5507d6876dbb5. Tutte le modifiche elencate all'inizio erano preesistenti e sono state preservate. Nuovi file header profilo audio e report 2026-09-07. Nessun reset/clean/restore/revert/commit/push.

### Build finale P5.6

Wrapper terminato exit 0: ARM32 compile/link/VELF/FSELF/package/verify-mkw-firstboot-vpk/unzip PASS. Nessuna ricompilazione globale translated; staging NEON conservato.

- `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_6-audio-wait-profile.vpk`: 41284109 byte, SHA-256 `ceb58bf8f76fce9046678b48ef160355cbdf7a86dd23d8a7d119f507de36f148`.
- `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_6-audio-wait-profile.manifest.txt`: 1760 byte, SHA-256 `cc0639386f5b9ab4d78d0521314c7e4fddde0553098ada3ff4d05f73660a5dbe`.
- `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_6-audio-wait-profile.elf`: 219325048 byte, SHA-256 `4771ed3950d2ab42048b90cd311f52db4b49223778098ad1f42b21cdc71518dd`.

Evidence JSON accanto al VPK, hardware_validated=false. Hash artefatti e sorgenti audio/main/GX verificati sul filesystem; marker audio_wait_profile e audio_wait_parts presenti nell'ELF. Config P5.5/P5.6 differisce solo per AUDIO_WAIT_PROFILE. ARM32 syntax profiler OFF su audio/main/GX PASS. git diff --check PASS. Nessun errore build/test finora. Test hardware P5.6 non eseguito; necessario nuovo log. Esito graphics-check aggiunto al termine.

Artefatto storico P5.5 preservato: VPK build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_5-wait-service-profile.vpk 41280700 byte SHA-256 `8a37812e68729029cb683a3395f71f16db5aeebaf63cbe99b5518b6aa69722c0`; ELF build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_5-wait-service-profile.elf 219302096 byte SHA-256 `4a62a05a9376e8d4fb364fbb25bd80a50a7adccb5fdf4423a48fa6661ab382c7`.

Stato worktree finale (aurora-vita internamente pulito e submodule status vuoto):
```
 M Makefile.vita
 M PORTING_STATUS.md
 M docs/performance-60fps-2026-09-05/IMPLEMENTATION.md
 M runtime/include/fiber_manager.h
 M runtime/include/hle_stubs.h
 M runtime/src/fiber_manager.cpp
 M runtime/src/hle/audio/audio.cpp
 M runtime/src/hle/os/os_interrupt.cpp
 M vita/gx_backend.cpp
 M vita/include/wiicompiled_vita/gx_backend.h
 M vita/main_vita.cpp
 M vita/tools/build_performance_profile.py
?? CLAUDE.md
?? aurora-vita/
?? docs/performance-analysis-2026-09-06/
?? docs/performance-analysis-2026-09-07/
?? runtime/include/audio_wait_profile.h
```

P5.6 graphics-check PASS con i flag del profilo (exit 0, warning enum preesistente GXVert.cpp). Nessuna build/test in corso. Test offline conclusi senza FAIL; hardware P5.6 pendente.

## Aggiornamento successivo: P5.6 hardware tested / P5.7 pronta

Questa sezione supera lo stato "hardware P5.6 pendente" riportato sopra. Una
sessione successiva ha hardware-testato P5.6 e isolato la callback AI
`0x80551F00` (`THP::AudioMixCallback`) come fase dominante dell'audio nel
render-wait, con `JoinMixWorker` significativo in alcune finestre e backlog fino
a ~23 s. L'analisi completa e i numeri deduplicati sono in
`docs/performance-analysis-2026-09-07/P5_6-P5_7.md`.

Da quell'evidenza e stata implementata P5.7
`full-content-p5_7-audio-ai-profile`, diagnostica soltanto: misura cache
maintenance e `AXWii::SendMail` inclusivi dentro il callback AI e registra uno
snapshot THP bounded. Non modifica callback, join, cap audio, invalidazioni,
ordine dei servizi o lifetime.

Artefatto P5.7 da hardware-testare:

- VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_7-audio-ai-profile.vpk`
- bytes `41282688`
- SHA-256 `45de24a7f4304ed08a071d14dfc07ab47a0547296a933f44b1908c3920c2de6b`
- ELF SHA-256 `5ded393009e97c9ec5cd87738ae8a6af19ad7b7a5f02c388c81d5b3e8ce0a2dd`.

Compile/link/package/unzip e graphics-check PASS; evidence
`hardware_validated=false`. Gli hash dei sorgenti correnti coincidono con
l'evidence della build.

Il `runtime.log` allegato nella sessione corrente, SHA-256
`f0b7b8baa65ebe1caa62e71017f4a30acb314990eee8c2be016e8ca68dcbfaa9`,
non e P5.6/P5.7 ed e stato preservato come
`build/vita/runtime-unmatched-f0b7b8ba.log`. Contiene boot storici cap128/probe e
non deve essere usato per l'A/B corrente.

Handoff autosufficiente: `docs/performance-analysis-2026-09-07/HANDOFF_P5_7.md`.

## Aggiornamento funzionale: P5.8 native AudioOut

L'utente ha confermato che le build precedenti erano completamente mute su PS
Vita. Il backend Vita ha confermato la causa: era ancora un null sink e non
chiamava mai SceAudioOut. Questo significa che l'attribution P5.5-P5.7 resta
valida per il **lavoro HLE guest audio**, ma non misura un dispositivo di output
Vita che fino a P5.7 non esisteva.

P5.8 aggiunge un output nativo isolato dal profiler storico tramite
`MKW_VITA_NATIVE_AUDIOOUT=1`: MAIN port 48 kHz stereo, worker dedicato per
`sceAudioOutOutput`, coda 8x256 frame, conversione canali/endian e resampling del
Wii 32 kHz a 48 kHz. La coda non blocca USER_0; un overflow scarta solo PCM nuovo
e non altera AI/AX/callback guest.

Build offline PASS:

- VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_8-native-audioout.vpk`
  41284474 byte, SHA-256
  `e7b9f225c6f25816dd406c994e57b0af756553d53627a5b057fe731d5cb61658`;
- ELF 219541704 byte, SHA-256
  `2e8f952790e9456bb335c05f3ceca5e960f61ff3e869cc7b57feafdeb53e76c6`;
- compile con kill switch 0/1, link, VELF/FSELF, package, verify/unzip,
  graphics-check e git diff check PASS.

Hardware ancora pendente. Il prossimo log deve mostrare `native_audioout=1` e,
idealmente, `vita_audioout opened` + `vita_audioout first_output`; il test deve
anche riportare se l'audio e realmente udibile, pitch plausibile, crackle e
overflow. Se l'API accetta i chunk ma resta silenzio, il prossimo profiler deve
misurare il PCM prodotto, non ridurre o bypassare la pipeline AI/AX/THP.

Handoff successivo: `docs/performance-analysis-2026-09-07/HANDOFF_P5_8.md`.

## P5.8 hardware validated e P5.9 audio pacing

Nuovo hardware log preservato:
`build/vita/runtime-p5_8-733ec259.log`, SHA-256
`733ec2590f76e2d4610a5a7731e3329ba59188cb0f2a5d3bf17feba02c746274`.
L'utente conferma che l'audio P5.8 e udibile. Il log conferma il marker completo
P5.8, `vita_audioout opened`, `vita_audioout first_output` e almeno un
`queue_full dropping_new_pcm chunks=8`. P5.8 passa quindi da offline-only a
**hardware validated per output funzionale**, ma non per qualita/pacing.

Il crackle e coerente con il frame pacing del guest: sono ancora presenti
producer frame ~1,3-1,4 s e transizioni ~6,3 s. Il buffer host da ~43 ms si
svuota durante gli stall e viene riempito a raffiche durante il catch-up.
L'overflow non deve essere corretto aumentando indiscriminatamente la FIFO.

P5.9 introduce soltanto continuita dell'output host:

- flag `MKW_VITA_AUDIO_PACING=1` nel nuovo profilo;
- SceAudioOut resta clockato dopo il primo PCM, con silenzio in underrun;
- fade bounded 64 frame alle discontinuita;
- overflow host: drop oldest, keep newest;
- contatori real/silence/underrun/drop/high-water;
- nessuna modifica a AI/AX/THP/backlog/callback guest.

Build P5.9 offline PASS:

- VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_9-audio-pacing.vpk`
  41286477 byte, SHA-256
  `65bbfccbf4905279967d6b5ed9c8963ff57831e36c10c2e377a853b601ca449a`;
- ELF 219550236 byte, SHA-256
  `94ae8a1c757c6b81a44ca20e57aa64b805b24f17675ae44d4e2a5d80e3bc4696`;
- compile/link/VELF/FSELF/package/verify/unzip/graphics-check/diff-check PASS.

P5.9 e **NON hardware tested**. Nel prossimo run, alta `silence`/`underrun`
dimostra starvation; alta `drop_oldest` dimostra burst/catch-up. Entrambi alti
confermano che il vero blocker resta il guest multi-secondo. Dopo il test P5.9,
la priorita performance torna all'attribution TaskThread/THP/scheduler e alla
P5.7 AI-child attribution; packet swap rimane secondario.
