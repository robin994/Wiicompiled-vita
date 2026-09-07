# P5.4 hardware e P5.5 attribuzione del timing service — 2026-09-06

## Stato raggiunto
P5.4 hardware tested: nel terzo boot lo stato IRQ non contamina più i tentativi deferred osservati. Correzione mirata supportata dai dati; nessuna validazione generale scheduler/audio/mutex e nessuna rivendicazione 60 FPS. Il log avanza fino a serial 1200, con input ripetuti, senza deadlock terminale osservabile. Rimangono stall di secondi, VI debt transitorio, errori THP jpeg_pixels code=11 (anche nel precedente log). Il sink audio Vita attuale è non-blocking e non apre un dispositivo audio: queste misure non provano output audio udibile.

P5.5 è un candidato diagnostico: stessa semantica di P5.4, misura VI/allarmi/audio durante WaitRender. Non è una correzione prestazionale e il suo overhead va misurato su Vita.

## Evidenza hardware
Il file build/vita/runtime.log è cambiato rispetto al precedente handoff: SHA-256 ora `94a8579e47f83da7176cf62e199c2f0d1d4660b62f1baa9f0ab5f89ecf7df6bc`, tre boot alle righe 1, 484, 983. Marker P5.4 alla riga 1019: fiber_irq_state=1, full content, PERF_LOG=0. Il marker identifica la configurazione; il log non incorpora l'hash ELF, perciò l'associazione all'artefatto fornito è basata sul marker e sul contesto del test, non su un hash runtime.

Snapshot non distruttivi:
- build/vita/runtime-p5_3-94a8579e.log SHA-256 `2ec23856519dc436c28ce8f475795f8f045bf8acac9252d8c25d37d2c1f4577e`.
- build/vita/runtime-p5_4-94a8579e.log SHA-256 `e2e9dce4503ea3c36f11c680e82b0a18a1a0b55e1cc90421482ffa56727d4b9c`.
- P5_4-metrics.json contiene estrazione best-effort dei campioni; il log ha righe interlacciate/duplicate (es. VI a riga 1416), non trattarle come campioni indipendenti.

| Metrica | P5.3 | P5.4 |
|---|---:|---:|
| ultimo deferred calls/skipped/advanced | 21407/20838/135 | 13874/0/3844 |
| skipped/calls cumulativo | 97,342% | 0% |
| ultimo last_retrace_age | 12363,496 ms | 16,369 ms |
| ultimo next_retrace_due | -12346,830 ms | +0,297 ms |
| producer serial 1200 | 202,496 ms | 211,991 ms |
| wait_gx serial 1200 | 35,510 ms | 37,948 ms |
| queue wait serial 1200 | 0,001 ms | 0,002 ms |
| packet copy serial 1200 | 1,909 ms | 2,161 ms |
| residual stimato serial 1200 | 165,076 ms | 171,880 ms |
| wait service serial 1200 | 0,041 ms | 31,001 ms |
| render / submit serial 1200 | 53,130 / 51,183 ms | 52,097 / 50,002 ms |
| endframe / swap serial 1200 | 1,713 / 0,128 ms | 1,881 / 0,127 ms |
| EFB efb_us serial 1200 | 17,075 ms | 16,131 ms |
| EFB native / native_budget / resident_fail | 12 / 0 / 0 | 12 / 0 / 0 |
| texture fail / retry_wait | 0 / 0 | 0 / 0 |
| raw mesh byte | 705628 | 703656 |

Gli ultimi watchdog non sono timestamp equivalenti: questi cumulativi non sono un benchmark normalizzato. Nel P5.4 tutti i campioni VI osservati hanno skipped=0. Tra serial 926 e 927 persiste un debt di 1,60–2,07 s, poi si recupera; non dire VI debt eliminato ovunque.

Scena UI serial 900: producer 124,196→166,621 ms; render 9,174→9,019 ms; residual stimato 120,393→146,783 ms. Non un miglioramento generale. Geometria quasi equivalente (179→178 draws).
Transizione P5.3 925 / P5.4 928: producer 6282,705→6250,828 ms, residual 6281,828→6249,931 ms. Quasi nessuna attesa GX (44→63 us).
Heavy P5.3 1095 / P5.4 1098, stessa geometria 3722 draws/24738 vertici: wait_gx 1717,367→1642,267 ms, wait_service 1,701→1400,642 ms (85,3% del WaitRender P5.4). Residual 280,653→309,717 ms. Serial 445: wait_service 2858,217 ms su wait_gx 6192,334 ms.

Residual = interval - wait_gx - queue_wait - packet_copy è solo una stima: interval è campionato all'ingresso SubmitFrame, mentre copia/queue appartengono alla submission corrente; non una partizione temporale esatta. Wait service è incluso in WaitRender, non sommarlo nuovamente. Manca un campione GX CPU finale P5.4 equivalente al frame=900 P5.3 (DL 22673 us, prebegin 80283 us); non inventare il confronto. P5.4 frame GX=600 è una finestra diversa.

## Root cause e scelta
La correzione per-fiber ha riattivato lavoro guest legittimo che il vecchio flag globale sopprimeva. Il suo costo aggregato è ora grande e non attribuibile dalla vecchia telemetria. Il callback di main_vita chiama VI_HLE_ProcessRetracesDeferred(8), OS_HLE_ProcessAlarmsDeferred(8), Audio_HLE_PollDeferred() in ordine. Il servizio allarmi include anche completamenti network/NAND/DVD (DVD può drenare almeno 64); audio include JoinMixWorker e callback AI/AX. Non sono tre semplici timer: serve la separazione prima di modificare budget/ordine/cadence.

## Modifiche P5.5
- vita/main_vita.cpp: quattro letture del clock attorno ai tre servizi, solo con MKW_VITA_WAIT_SERVICE_PROFILE=1. Nessun cambio a ordine, limiti, IRQ o scheduler.
- vita/include/wiicompiled_vita/gx_backend.h e vita/gx_backend.cpp: accumulo totale/max per fase su USER_0, senza lock/allocazioni/log per callback. Reset a ogni SubmitFrame; emissione wait_service_parts sullo stesso criterio periodico/critico del producer, con serial identico. vi_us, alarm_us, audio_us sono totale/max in microsecondi. Somme confrontabili con wait_service total, con piccolo overhead residuo atteso.
- Makefile.vita: flag kill switch default 0, stamp/config/marker; main_vita.o ora dipende da .native-config per evitare una callback compilata con flag stale nell'A/B.
- vita/tools/build_performance_profile.py: full-content-p5_5-wait-service-profile eredita P5.4 e cambia solo il flag diagnostico; evidence include runtime/src e runtime/include, prima mancavano i sorgenti IRQ/fiber.
- PORTING_STATUS.md / IMPLEMENTATION.md: append-only milestone corrente; P5_4-HANDOFF.md rimane documento storico.

## Prossimo hardware test
Conservare/rinominare il log precedente prima di avviare, ripetere menu/transizione/scena heavy, acquisire log completo. Marker obbligatori fiber_irq_state=1 e wait_service_profile=1, PERF_LOG=0. Cercare wait_service_parts allo stesso serial del producer: calls deve corrispondere a wait_service.calls nelle normali callback completate; somma vi/alarm/audio <= totale esterno con overhead piccolo. I contatori si azzerano anche nei frame non loggati: non sommare campioni sparsi per ricostruire il run intero.

Se domina audio: profilare JoinMixWorker vs callback AI/AX e verificare backlog/lifetime, senza disabilitare audio. Se domina alarm: separare handler e completamenti IOS, applicare eventualmente un drain bounded che conservi ordine e progresso. Se domina VI: misurare postCb 0x8020FCD4 e catch-up, non saltare retrace né forzare IRQ. Conservare P5.4 se i dati restano coerenti. I lunghi residual guest richiedono un ciclo dedicato scheduler/TaskThread/THP con attribution fresca, indipendente dalla misura WaitRender.

## Blocker in ordine
1. Stall transizione producer ~6,25 s: non spiegato dal WaitRender; watchdog snapshot con PC 0x800060A4/LR 0x8021A058 non prova un hotspot preciso. File fiber_manager.cpp, scheduler/TaskThread, translated code. Prossima modifica solo dopo attribuzione del lavoro effettivo.
2. WaitRender ~1,64–6,19 s e servizio fino a 2,86 s: P5.5 attribuisce un costo già misurato; causa interna ancora aperta.
3. Steady heavy producer ~212 ms, renderer ~52 ms/EFB ~16 ms: lontano da 16,67 ms. Nessuna pressione cache osservata. EFB budget blocked=1 compare nella transizione, ma serial 1200 è native 12/12 e zero fail; non aumentare budget.
4. Texture zero fail nel campione; packet copy ~2,16 ms secondario. Nessun packet swap ora.

## Esperimenti esclusi
No transient FBO/glBlitFramebuffer (crash SceGxm), TransferDownscale nearest arbitrario, rimozione cieca glFinish, global O3/fast-math, billboard O2 senza nuova evidenza, budget RAM arbitrari, contenuti ridotti, Vita3K come prova prestazionale. Nessun hot shard.

## Build e test
Comando: `python3 vita/tools/build_performance_profile.py full-content-p5_5-wait-service-profile --jobs 8`.
Log: build/vita/p5_5-wait-service-profile-build.log. Risultati finali e hash aggiunti sotto al completamento.
Config full content P5.4 conservata: faithful LYT, clip_w=1, movies/native THP, P4.1/P5.1/P6/P7/P5.2/P5.3, IRQ state, queue 2, EFB cap 512 e budget 4 MiB, PERF_LOG=0, translated build/vita/mkwii_translated_neon_os con -Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard. Profilo A/B originale full-content-p5_4-fiber-irq; non sovrascrivere gli artefatti storici per confrontarli.

### Artefatti finali P5.5

Build terminata exit 0; ARM32 compile, link, VELF/FSELF, packaging, verify-mkw-firstboot-vpk/unzip PASS. Translated staging riutilizzato, non ricompilato globalmente.

- `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_5-wait-service-profile.vpk`: 41280700 byte; SHA-256 `8a37812e68729029cb683a3395f71f16db5aeebaf63cbe99b5518b6aa69722c0`.
- `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_5-wait-service-profile.manifest.txt`: 1743 byte; SHA-256 `e0e7d73af6f534ede8596bfc207fcc00a822f2027e421e01ae92efc242b782a9`.
- `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_5-wait-service-profile.elf`: 219302096 byte; SHA-256 `4a62a05a9376e8d4fb364fbb25bd80a50a7adccb5fdf4423a48fa6661ab382c7`.

Evidence: stesso basename `.evidence.json`, hardware_validated=false; include gli hash dei sorgenti runtime IRQ/fiber. PASS verifica marker ELF, confronto config P5.4/P5.5 (solo flag diagnostico differente), ARM32 syntax con profiler OFF su main_vita e gx_backend, git diff --check. Nessun test hardware P5.5 eseguito. Due tentativi ausiliari iniziali non conclusi (parser su riga interlacciata senza interval_us, check OFF prima che gx_backend fosse compilato) corretti e rieseguiti; nessun errore compilatore/package.

### Artefatti P5.4 hardware tested conservati

VPK build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_4-fiber-irq.vpk: 41280970 byte, SHA-256 `8066d1a6cbce6ec78611d60dde20db8e3a2cfe697401e424b159e9cb7b650317`. ELF build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_4-fiber-irq.elf: 219297272 byte, SHA-256 `253bacb9128f2cef58c6541e6de1a45d6646d0f5abd50f03d6ded0822a48614f`.

## Worktree finale

HEAD `01f530b500940eb2f64c0aff13e5507d6876dbb5`. Le patch IRQ P5.4 e relative modifiche a Makefile/backend/profili/documenti erano preesistenti; conservate. main_vita.cpp e gx_backend.h sono nuove modifiche di questa sessione, integrate le altre in-place dopo lettura del diff. CLAUDE.md e aurora-vita/ preesistenti, non toccati; aurora-vita internamente pulito, nessun submodule registrato. Nessun reset/clean/restore/revert/commit/push.

```
 M Makefile.vita
 M PORTING_STATUS.md
 M docs/performance-60fps-2026-09-05/IMPLEMENTATION.md
 M runtime/include/fiber_manager.h
 M runtime/include/hle_stubs.h
 M runtime/src/fiber_manager.cpp
 M runtime/src/hle/os/os_interrupt.cpp
 M vita/gx_backend.cpp
 M vita/include/wiicompiled_vita/gx_backend.h
 M vita/main_vita.cpp
 M vita/tools/build_performance_profile.py
?? CLAUDE.md
?? aurora-vita/
?? docs/performance-analysis-2026-09-06/
```

P5.5 `graphics-check` PASS con gli stessi flag del profilo (exit 0; warning enum preesistente GXVert.cpp). Nessuna build/test in corso; validazione hardware del nuovo profiler ancora pendente.
