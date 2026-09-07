# Handoff P5.8 — native Vita AudioOut

## Stato

Repository: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`

HEAD: `01f530b500940eb2f64c0aff13e5507d6876dbb5`

Target: Mario Kart Wii PAL RMCP01, PPC -> ARM32, Aurora -> vitaGL -> SceGxm, PS Vita reale.

Worktree intenzionalmente dirty. Preservare tutto. Non usare reset/restore/checkout/clean/revert. Nessun commit/push senza richiesta.

P5.6 e hardware tested. P5.7 e offline tested ma non hardware tested. P5.8 e una nuova baseline funzionale audio, offline tested e non hardware tested.

## Nuova root cause audio

L'utente ha confermato che sulle build precedenti non si sentiva alcun audio dalla Vita. L'ispezione di `vita/audio_backend_vita.cpp` ha dimostrato che non era un problema di volume: il backend era ancora il first-boot null sink.

Prima di P5.8:

- `ApplyGainLocked()` non faceva nulla;
- `PushWiiAiSamplesBE16()` restituiva successo ma scartava il PCM;
- `PushSamplesLE16()` restituiva successo ma scartava il PCM;
- nessun `sceAudioOutOpenPort`/`sceAudioOutOutput` era presente.

Quindi i grandi tempi P5.5/P5.6 denominati audio misurano principalmente emulazione/HLE Wii AI/AX/THP, callback guest e join del mix worker. Non misuravano playback hardware Vita.

## P5.8 implementata

Flag nuovo: `MKW_VITA_NATIVE_AUDIOOUT`, default `0`.

Profilo: `full-content-p5_8-native-audioout`, identico a P5.7 salvo `MKW_VITA_NATIVE_AUDIOOUT=1`.

File principali modificati:

- `Makefile.vita`: flag/config/COMMON_FLAGS, dipendenza audio object dal config stamp, link `-lSceAudio_stub`;
- `vita/tools/build_performance_profile.py`: BASE flag0 e profilo P5.8 flag1;
- `vita/gx_backend.cpp`: marker `native_audioout=%u`;
- `vita/audio_backend_vita.cpp`: SceAudioOut reale con kill switch;
- documentazione P5.8.

Backend P5.8:

- `SCE_AUDIO_OUT_PORT_TYPE_MAIN`;
- output fisso 48000 Hz stereo come richiesto dal MAIN port;
- 256 frame per `sceAudioOutOutput`, multiplo di 64;
- worker `std::thread` dedicato per tenere la chiamata bloccante fuori da USER_0;
- coda bounded 8 chunk, circa 43 ms;
- overflow: drop del solo nuovo PCM, mantenendo callback/progresso guest;
- Wii AI BE16 right,left -> PCM host left,right;
- 32 kHz -> 48 kHz con resampler lineare block-local; il DMA osservato 96 frame/32 kHz produce 144 frame/48 kHz;
- 48 kHz passthrough;
- volume/mute via `sceAudioOutSetVolume`;
- `Shutdown()` ferma il worker e rilascia il port.

Marker runtime:

- `native_audioout=1`
- `vita_audioout opened port=... type=main frames=256 rate=48000 queue_chunks=8`
- `vita_audioout first_output port=... frames=256 rate=48000`
- errori possibili: `open_failed`, `output_failed`, `queue_full`.

## Build P5.8

VPK:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_8-native-audioout.vpk`

Bytes: `41284474`

SHA-256:
`e7b9f225c6f25816dd406c994e57b0af756553d53627a5b057fe731d5cb61658`

ELF:
`build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_8-native-audioout.elf`

Bytes: `219541704`

SHA-256:
`2e8f952790e9456bb335c05f3ceca5e960f61ff3e869cc7b57feafdeb53e76c6`

Manifest:
`build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_8-native-audioout.manifest.txt`

Manifest confermato: full content, faithful LYT, movies/native THP, clip_w=1, P4.1/P5.1/P6/P7/P5.2-P5.7, queue2, EFB native/resident cap512, PERF_LOG0, no hot shard, native_audioout=1.

## Test offline

PASS:

- compile `vita/audio_backend_vita.cpp` con native_audioout=1;
- compile dello stesso file con kill switch native_audioout=0;
- full ARM32 compile;
- link con `-lSceAudio_stub`;
- VELF/FSELF;
- package VPK;
- verify/unzip;
- graphics-check con flag P5.8;
- git diff --check.

Warning rimasti: warning/ABI note GCC gia noti, incluso enum GXVert; nessun nuovo errore.

P5.8 NON hardware tested.

## Prossimo test PS Vita

Installare P5.8 e ripetere almeno boot/menu fino a dove normalmente parte l'audio.

Verificare nel marker:

- `fiber_irq_state=1`
- `wait_service_profile=1`
- `audio_wait_profile=1`
- `audio_ai_profile=1`
- `native_audioout=1`
- `perf_log=0`
- movies/native THP on
- queue2
- EFB cap512.

Verificare log audio nell'ordine:

1. `vita_audioout opened`;
2. `vita_audioout first_output`;
3. assenza di `open_failed`/`output_failed`;
4. frequenza di `queue_full` se presente.

Verifica fisica obbligatoria:

- si sente qualcosa dagli speaker/cuffie Vita?
- pitch/velocita sono plausibili?
- stereo/channel order e plausibile?
- ci sono crackle, burst o pause lunghe?

Se `opened` manca: il backend non e stato inizializzato o il profilo non e P5.8.

Se `open_failed`: interpretare il return code SceAudioOut prima di cambiare architettura.

Se `opened` c'e ma `first_output` manca: tracciare `PushWiiAiSamplesBE16`/AI DMA e verificare che arrivino abbastanza PCM per un chunk.

Se `first_output` c'e ma resta completamente muto: aggiungere telemetria PCM bounded (min/max, RMS, checksum, non dump continuo) prima di accusare il device. Verificare che i campioni non siano tutti zero e che volume/mute non siano zero.

Se si sente ma e distorto: verificare prima endian/channel order, sample rate e continuita del resampler; non toccare AI/AX callback per correggere il sink.

## Performance

Non confondere P5.8 con una performance optimization. La P5.7 null-sink resta la baseline migliore per misurare il puro costo HLE. P5.8 introduce un piccolo costo di conversione/resampling/queue ma sposta il blocking `sceAudioOutOutput` sul worker dedicato.

I blocker performance restano:

1. stall guest ~6,25 s TaskThread/THP/scheduler ancora non attribuito completamente;
2. AI/THP callback estremamente costosa nei render-wait; P5.7 deve ancora essere hardware-testata per cache/mail attribution;
3. heavy producer ~228 ms e renderer ~62 ms nella finestra P5.6;
4. packet copy ~2,7 ms secondario.

Dopo aver validato che l'audio nativo funziona, si puo tornare al ramo performance. Per confronti numerici puri, conservare P5.7 null-sink; per una baseline funzionale completa usare P5.8.

## Divieti da preservare

No transient FBO/glBlitFramebuffer; no TransferDownscale nearest; no blind glFinish/join removal; no global O3/fast-math; no hot shard senza profiling; no aumento RAM arbitrario; no contenuti ridotti per gonfiare FPS; Vita3K non e validazione prestazionale.

## Prompt di continuazione

Continua autonomamente in `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`. Target Mario Kart Wii PAL RMCP01 PPC->ARM32, Aurora->vitaGL->SceGxm, PS Vita reale. HEAD `01f530b500940eb2f64c0aff13e5507d6876dbb5`; worktree dirty da preservare, nessun reset/restore/checkout/clean/revert, commit/push non autorizzati. P5.8 native AudioOut e offline-tested e NON hardware-tested. Le build fino a P5.7 erano mute per definizione: `vita/audio_backend_vita.cpp` era un null sink e scartava il PCM. P5.8 aggiunge `MKW_VITA_NATIVE_AUDIOOUT=1`, MAIN SceAudioOut 48 kHz stereo, worker dedicato blocking-output, queue 8x256 frame, conversione BE right/left e resampling 32->48 kHz, senza cambiare AI/AX callback guest. Testare VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_8-native-audioout.vpk`, SHA256 `e7b9f225c6f25816dd406c994e57b0af756553d53627a5b057fe731d5cb61658`, 41284474 byte. ELF SHA256 `2e8f952790e9456bb335c05f3ceca5e960f61ff3e869cc7b57feafdeb53e76c6`, 219541704 byte. Nel prossimo log richiedere `native_audioout=1`, `vita_audioout opened` e `vita_audioout first_output`; chiedere anche conferma uditiva, pitch/crackle. Se first_output success ma silenzio, implementare telemetria PCM min/max/RMS/checksum bounded; se open/output fallisce, analizzare rc; se audio funziona, non attribuire automaticamente i costi P5.5/P5.6 al sink: erano soprattutto HLE AI/AX/THP. Conservare P5.7 come null-sink perf baseline e P5.8 come functional baseline. Poi riprendere P5.7 child attribution e lo stall guest ~6,25s. Aggiornare PORTING_STATUS, IMPLEMENTATION e report dopo ogni hardware run e lasciare sempre handoff autosufficiente prima di esaurire il contesto.
