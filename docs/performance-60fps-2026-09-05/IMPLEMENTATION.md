# Implementazione successiva a P4.1 — 5 settembre 2026

Questa integrazione modifica il backend Aurora realmente compilato dal porting,
`aurora-main/platforms/vita/gfx`, e il bridge `vita/`. Il clone separato
`aurora-vita/` non entra nel VPK: non è stato sostituito al backend integrato.
Le modifiche locali precedenti sono conservate. Nessun commit o pubblicazione.

## Risultato e limiti

Implementati: copie EFB in memoria persistente, coda EFB più capiente con
accorpamento conservativo, protezione del riuso dei buffer, budget texture con
margine temporaneo condiviso, accorpamento dei quad UI nel producer e build
selettiva degli shard caldi. Sono disponibili profili separati e manifest con
hash di ELF/VPK e sorgenti. Il target resta 60 FPS, ossia 16,67 ms/frame.

**Queste sono implementazioni e verifiche offline, non nuove misure hardware.**
Non dimostrano 60 FPS, modelli corretti, eliminazione del bianco o stabilità in gara.
Il ridimensionamento EFB arbitrario rimane CPU; la sincronizzazione dei buffer è
un'attesa conservativa, non un sistema di fence asincrone. Le fasi che richiedono
queste proprietà non sono dichiarate concluse.

## Baseline verificata

HEAD `ab7e240cf2584dc0d2c770569f24510eeea62fcf`, worktree con P0–P4.1 già modificato.
P4.1 VPK: SHA-256 `9bbb8f109671a1d7e5beffeda1e70bb80d99cc36029dfa0c060abdfe93b655af`.
Log esaminato: `build/vita/runtime.log`, 2582 righe,
SHA-256 `cd78e526b819bdb37840e7f029382deb98d769a8bea79c73255016711bdbe56f`.
Il log è stato conservato. Ultima riga:

```text
[hle] input Vita state core=0000 classic=0000 stick=(0.00,-0.00)
```

Il campione seriale 900 riporta:

| Dato | P4.1, hardware |
|---|---:|
| Producer | 220,643 ms |
| Attese GXDrawDone | 91,878 ms, 15 chiamate |
| Renderer | 101,851 ms |
| Draw logiche / fisiche | 1619 / 87 |
| Cache mesh hit / miss | 1289 / 213, circa 85,8% hit |
| Layout hit / miss | 1457 / 45 |
| EFB, 12 copie | sync 1,026 / read 42,594 / scale 19,522 / upload 18,008 ms |
| Raw failure / dropped / transform failure | 0 / 0 / 0 |

Le ottimizzazioni P0–P4.1 hanno quindi evidenza utile: non vanno riscritte.
Le copie EFB rappresentano circa l'80% del renderer di questo campione.
Sottrarle idealmente non porterebbe da solo il producer a 16,67 ms.
Non sommare renderer e producer: i tempi includono sovrapposizioni e attese.

## P5 — Copie EFB persistenti

Flag `MKW_VITA_EFB_RESIDENT_COPY=1`, alternativo al vecchio percorso FBO.
`EfbManager::capture_resident`:

- accetta solo il display framebuffer lineare RGBA8 senza MSAA e pool non cached;
- usa descrittori GXM verificati e un adattatore ristretto ai simboli della
  libreria vitaGL già collegata; non modifica né ricompila quell'archivio;
- completa i precedenti lettori/scrittori GPU prima di riutilizzare memoria;
- alloca la texture una volta, con dati iniziali validi, evitando la precedente
  allocazione vuota che aveva dato problemi hardware;
- per dimensioni uguali usa `sceGxmTransferCopy` e attende il trasferimento;
- per dimensioni diverse esegue nearest-neighbour direttamente dal framebuffer
  mappato alla texture persistente, senza buffer di readback/upload intermedi;
- conserva il reticolo di campionamento e l'orientamento della baseline;
- mantiene il percorso precedente per target/layout non supportati o resize.

Il vecchio `MKW_VITA_EFB_GPU_BLIT` rimane a zero. Nessun FBO transitorio,
`glBlitFramebuffer` o cambio del render target nel percorso nuovo.

Telemetria: `efb=GPU/readback`, `resident=scaled_count/copy_us`, `efb_us` con
sincronizzazione separata. `resident` non significa lavoro interamente GPU.
Nella scena 960x544→dimensioni Wii è prevedibile l'uso del ridimensionamento CPU:
il beneficio di evitare gli altri passaggi va misurato, non presunto.

Questo percorso, come la baseline, serve le copie campionate dal renderer:
**non implementa il packing EFB nella RAM guest né l'intercettazione di letture
CPU guest**. Il fallback precedente non va confuso con questa funzionalità.

## P6 — Risorse e capacità

`MKW_VITA_STREAM_SAFE_REUSE=1`: ogni slot viene marcato quando riceve dati;
prima di sovrascrivere uno slot ancora potenzialmente in uso si esegue una
sincronizzazione GPU. Una sincronizzazione ritira tutti gli slot. La stessa
protezione precede la distruzione dell'arena. `reuse_wait_us` misura il costo.
La rotazione di due VBO da sola non garantiva questo: il mapping di vitaGL
non aspetta `last_frame`. Un futuro fence per slot potrà ridurre l'attesa.

`MKW_VITA_EFB_COMMAND_CAPACITY=512`: contatore allargato da 8 a 16 bit, tetto
configurabile fra 128 e 1024, ancora verificato dallo static assert del packet.
Si contano anche i destroy rifiutati, prima silenziosi. Si accorpano solo comandi
immediatamente consecutivi sulla stessa destinazione e allo stesso confine draw;
non si elimina mai una clear né la transizione destroy→copy. Nessun riordinamento.
`efb_coalesced` e `efb_cap` distinguono risparmio reale da overflow.
Il tetto 512 copre il precedente caso documentato di 399 copie più 50 destroy;
non garantisce che ogni scena futura rientri nel limite.

`MKW_VITA_TEXTURE_SHARED_HEADROOM=1`: i 64 KiB temporanei per l'upload non vengono
più addebitati permanentemente a ogni texture. Restano padding, mip estimate e
20% per entry, più il controllo prima di ogni allocazione e il margine condiviso
di 512 KiB durante l'eviction. Il budget globale non viene aumentato.
Il conteggio EFB include ora il pitch arrotondato a otto pixel.

Test host con budget 1 MiB e texture 16x16: vecchio conteggio 15/30 residenti,
nuovo 30/30, senza superare il budget. Verificati anche eviction LRU,
protezione dei riferimenti del frame e invalidazione per intervallo sorgente.
È un risultato del test di contabilità, non una misura della memoria libera Vita.

## P7 — Quad UI nel producer

`MKW_VITA_UI_QUAD_RUNS=1` conserva LYT fedele. Unisce quad adiacenti completi
solo con trasformazione, raster, texture, ordine e intervallo vertici identici,
proiezione ortografica e nessun comando EFB fra i draw. Non unisce strip/fan o
geometria prospettica. PN matrix refs e vertici rimangono nel loro ordine.
Il contatore `producer_merge` distingue questo accorpamento da quello già
presente nel consumer Aurora. Non viene riattivato il vecchio LYT diretto.

## P8 — Shard ARM32 selettivi

La Makefile accetta `MKW_TRANSLATED_HOT_SHARDS`, percorsi espliciti relativi a
`generated/`, e `MKW_TRANSLATED_HOT_OPT=-O2` oppure `-O3`. Rifiuta nomi ignoti.
Gli oggetti selezionati sono separati e identificati dall'hash delle opzioni;
gli altri continuano a usare `mkwii_translated_neon_os`. Nessuna rigenerazione,
nessuna modifica al C++ emesso, nessun `fast-math` o disabilitazione delle eccezioni.

La variante O2 di prova seleziona soltanto
`build_shards/base_common/shard_abb115feb0e46ec86d52cfd1.cpp`, che contiene il
ritorno `0x8003EA94` del percorso billboard osservato in precedenza.
Questa è una candidatura per confronto, non l'attribuzione del costo attuale
del menu né una sostituzione HLE dell'intero sottosistema.

## Riproduzione

Dalla radice del progetto:

```sh
python3 vita/tools/test_performance_helpers.py
python3 vita/tools/build_performance_profile.py p5-resident
python3 vita/tools/build_performance_profile.py p6-resources
python3 vita/tools/build_performance_profile.py p7-ui
python3 vita/tools/build_performance_profile.py p7-ui --hot-shard build_shards/base_common/shard_abb115feb0e46ec86d52cfd1.cpp --hot-opt O2
python3 vita/tools/build_performance_profile.py full-features
```

Ogni profilo ha nomi propri per ELF/VELF/SELF/VPK, configurazione nel manifest,
hash dei sorgenti ed evidenza JSON con `hardware_validated=false`.
`full-features` abilita filmati e THP nativo: è un candidato per test, non una
dichiarazione che tutti i contenuti funzionino. `--dry-run` mostra il lavoro
previsto senza ricompilare. Non lanciare profili contemporaneamente: i native
object e lo stamp di configurazione sono condivisi, mentre gli shard hot sono isolati.

## Verifiche e criteri di chiusura rimasti

I test host confrontano i pixel anche con pitch non compatto, entrambe le
orientazioni e guardie contro accessi fuori buffer. Una simulazione differenziale
di 1.000.000 operazioni EFB verifica ogni osservazione e lo stato finale, con
56.152 comandi accorpati. AddressSanitizer e UndefinedBehaviorSanitizer attivi.
Risultati riproducibili in `build/vita/performance-helper-tests.json`.

La chiusura del programma richiede ancora:

1. **Hardware P5**, stessa scena P4.1: orientamento/colore delle preview, assenza
   di fault, `efb_exec` senza failure, nessun aumento di drop e confronto EFB/producer.
2. **Hardware P7**, stessa scena: testo e UI integri, `producer_merge` effettivo,
   `reuse_wait_us`, cache/allocazioni, gara con centinaia di copie senza `efb_cap`.
3. **Confronto O2**, P7 contro P7-hot: tempi guest, dimensione e memoria. Conservare
   O2 solo se migliora davvero la scena corrispondente.
4. **Multimedia**, profilo full-features: filmati, audio, menu, gara e ritorno,
   quindi sessione prolungata 15–30 minuti. Non avanzare solo perché il VPK si apre.
5. **Correttezza 3D**: il bridge continua a dividere per W sulla CPU e a perdere
   W nel vertice compatto; normali e texgen/materiali non sono tutti rappresentati.
   Serve un percorso xyzw/clipping e la copertura degli attributi necessari,
   con immagini di riferimento. Il bianco non è dimostrato risolto da queste patch.
6. **EFB completo**: ridimensionamento GPU con equivalenza del filtro e readback
   guest su richiesta, dopo definizione e test delle dipendenze di memoria.
7. **Budget 16,67 ms**: nuove misure del guest non-GX/prebegin dopo P5; rimuovere
   GXDrawDone indiscriminatamente o accelerare globalmente il floating point
   cambierebbe la semantica senza risolvere il contratto di sincronizzazione.

Per il prossimo test, conservare/ruotare il log append precedente prima della
nuova esecuzione e associare il nuovo log all'ELF esatto del profilo installato.
La riscrittura generale in GXM, il worker texture e la riduzione di risoluzione
restano interventi condizionati al profiling, non fasi da abilitare alla cieca.

## P5.1 — candidato hardware: native-resolution resident EFB + safe texture retry

Stato: **PERCORSO HARDWARE CONFERMATO; MISURA PERFORMANCE A/B ANCORA APERTA**.

Il nuovo profilo `full-features-p5_1` mantiene P4.1/P5/P6/P7, EFB reale,
materiali reali, billboards, LightTexture, queue depth 2, movies e native THP,
senza hot shard O2/O3. Aggiunge due esperimenti bounded e disattivabili:

- `MKW_VITA_EFB_NATIVE_RES_COPY=1`: quando una `GXCopyTex` sampled con UV
  normalizzate richiederebbe il nearest resize CPU, prova a conservare il backing
  persistente alle dimensioni fisiche della regione sorgente. Se il backing entra
  nel budget EFB invariato da 4 MiB, `capture_resident` usa il percorso GXM
  same-size gia validato; altrimenti ricade sul backing logico + nearest CPU.
  Non usa transient FBO, `glBlitFramebuffer` o `sceGxmTransferDownscale`.
- `MKW_VITA_TEXTURE_SAFE_RETRY=1`: la LRU esistente usa un `useEpoch` monotono
  per non eliminare texture ancora referenziate. Quando un'allocazione e bloccata
  esclusivamente da risorse protette, sono consentiti al massimo quattro retry
  di emergenza per frame: flush dei draw pendenti, `glFinish`, retirement epoch,
  nuova pre-eviction LRU e retry. La fence P6 gia avvenuta viene riutilizzata
  quando disponibile.

Nuova telemetria aggregata `resource_summary`:

- EFB: `efb_path=gpuSame/gpuResize/cpuCopy/cpuResize`, native-res success/fallback,
  resident failure e reason counters;
- texture: eviction count/bytes, blocked pressure, protected bytes/high-water,
  retry attempt/success/fail e `retry_wait_us`.

Test host ASan/UBSan passati:

- resampling EFB strided: 1.000.000 operazioni FIFO, equivalenza pixel invariata;
- texture budget P6: 30/30 texture residenti, LRU/frame pins/invalidation;
- texture budget P5.1: la pressione protetta blocca l'allocazione prima del
  retirement e il retry torna allocabile soltanto dopo `mark_gpu_idle`, senza
  superare il budget.

`graphics-check` passa. La chiusura di P5.1 richiede ancora VPK + log hardware e
confronto numerico con serial 1200/1500/1800/2100; una build riuscita da sola non
costituisce validazione performance.

Artefatto hardware-test candidate prodotto dal source dirty reale:

- VPK: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-features-p5_1.vpk`
- VPK SHA-256: `c0f6a38b96cf35a03b413b568f99c134cd9d91ac9772f1f0120ffd38a56de732`
- VPK bytes: `41281316`
- ELF: `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-features-p5_1.elf`
- ELF SHA-256: `92bf1f0bfe1aaefc00e21477add70b5858a18cda9539a203517b87f65fb737bd`
- ELF bytes: `218486352`
- manifest SHA-256: `8e7ebf99c1a2fabeee1f428e57318334f8e22e588419d192f22ac562001436d1`

`verify-mkw-firstboot-vpk` e `unzip -t` passano. L'ELF contiene i marker
`resource_summary`, `tex_evict_total`, `tex_blocked_total`, `efb_path`,
`efb_native_res_copy` e `texture_safe_retry`. Il manifest conferma
`translated_hot_shards=` vuoto e il translated baseline NEON `-Os`.

### Primo log hardware P5.1-A

Il primo run reale conferma che il meccanismo native-resolution entra davvero
nel percorso voluto. A serial 900: 12/12 EFB sono `GpuSameSize`, `native=12`,
`native_budget=0`, nessun resident failure/reason e nessun CPU resize. Il budget
EFB resta 4 MiB con 3.546.816 byte residenti. La texture cache non raggiunge
ancora la pressione heavy: `tex_blocked_total=0` e `tex_retry=0/0/0`.

Questo log non e pero un A/B prestazionale valido contro il precedente
`full-features`, perche il candidato era stato costruito con
`MKW_VITA_PERF_LOG=1`. Quel flag abilita trace `render_large` su ogni frame da
almeno 1000 draw, progress ogni 128 draw e producer logging molto piu frequente.
Nel run si osservano infatti ~145-169 ms sui frame G3D e ~130 ms di `wait_gx`,
ma questi numeri sono contaminati dal tracing. Il file termina a serial 981,
quindi non raggiunge nemmeno la scena heavy usata per il baseline `tex_fail=1700`.

Per la misura successiva usare `full-features-p5_1-measure`: stessi flag
P4.1/P5/P5.1/P6/P7, movies/native THP e safe texture retry, ma
`MKW_VITA_PERF_LOG=0`. Restano disponibili il `perf_summary` dettagliato ogni
300 serial e `resource_summary`, senza trace per-draw/per-frame ad alto overhead.

Artefatto low-overhead prodotto e verificato:

- VPK: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-features-p5_1-measure.vpk`
- VPK SHA-256: `e6eb8dc5be2e0e03ce9dfcfa6e38bfdda518070fec67ba6a43a7341aa0d4832a`
- VPK bytes: `41280337`
- ELF: `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-features-p5_1-measure.elf`
- ELF SHA-256: `43aa341f6a6a575c9a039741cec1e00d962748a7a446ea6c1203e772fc8b468d`
- ELF bytes: `218461580`

`verify-mkw-firstboot-vpk` e `unzip -t` passano. Il manifest conferma
`perf_log=0`, P5.1/P6/P7 attivi, movies/native THP attivi,
`translated_hot_shards=` vuoto e translated NEON `-Os`. L'ELF conserva sia il
`perf_summary` dettagliato sia `resource_summary`.

## Pacchetti verificati in questa sessione

| Profilo | VPK byte | SHA-256 VPK | SHA-256 ELF |
|---|---:|---|---|
| p5-resident | 40875331 | `8d2f2873b49e8fffd906e61799b77917ce244be8635d5170f15a35f53a70c2fb` | `f8c6b0940d764e81e2f09e335d50eab8cef3e3da67fccb9da553fbcfcd362e89` |
| p7-ui | 40876061 | `5a40dfd924105a1170588edc563fdf4f0d8b9fa640b351d369b1e4d596de6625` | `afcfb0143a3e133e701d2b31256e83a5df1f375a9bcd325db85bd4b2722fb51a` |
| p7-ui-hot-O2-f171ce57 | 41016734 | `af0d6072de223520b53edac319abedad559e7c3ecfeba22932050fb7cc080010` | `22b37d0401dfad4007d49389c4c28dec6c36d0ffee21d8646e6ee2ac3cf3bd91` |
| full-features | 41277088 | `ae4066100ff4646876b11e99f403773d4df7d3cb3054d566bc64bcb2b0f6e811` | `9019cce437afd4ebe4c14a3862e895549ec16b95a6cd2b2a6543b23da9e27e8a` |
| full-features-p5_1 candidate | 41281316 | `c0f6a38b96cf35a03b413b568f99c134cd9d91ac9772f1f0120ffd38a56de732` | `92bf1f0bfe1aaefc00e21477add70b5858a18cda9539a203517b87f65fb737bd` |

Per tutti e quattro: compilazione/link, VELF/FSELF, packaging e verifica ZIP terminati con exit 0.
`graphics-check` e `git diff --check` passano. Il profilo `p6-resources` è riproducibile dallo script; i suoi interventi sono compilati nei VPK P7 e full-features.
La variante O2 sostituisce esattamente un oggetto nel link, con VPK +140.673 byte ed ELF +364.524 byte rispetto a P7.
La Vita al server FTP configurato `192.168.1.217:1337` non risponde (timeout): nessun deploy o test hardware eseguito.
Prossima azione: installare **p5-resident**, acquisire un log nuovo nella stessa scena P4.1 e confrontare `efb_us`, `resident`, `efb_exec` e `producer_frame`, oltre a orientamento e colori. Solo dopo passare a P7, O2 e full-features.


## M13.4 — priorità al contenuto 3D, build full-content-3d (2026-09-06)

La richiesta aggiornata mette correttezza 3D/texture/filmati prima di ulteriori ottimizzazioni. Il log nuovo contiene due sessioni; l'ultima ha già tutti i contenuti abilitati e perf_log=0. Ai seriali 1200/1500/1800 il renderer misura 39,904/47,071/50,245 ms con 12 copie GPU e zero resize CPU; il producer 158,846/175,224/202,473 ms. Il caso heavy termina a serial 1996 con producer 1,492 s e allocazioni texture fallite: manca il resource_summary finale. Nessun successo decode THP osservato.

Implementati W prospettico nel vertice compatto (28 byte, clip_w=1), ammissione delle texture generate dalla posizione anche senza TEX0, controlli e colori materiale non illuminati da API/XF, diagnostica limitata di apertura/preparazione e decode/errori THP. Profilo full-content-3d con contenuti attivi, P4.1/P5.1/P6/P7 conservati e nessun hot shard. PASS ARM32/link/VPK/ZIP, manifest/hash, graphics-check e test host ASan/UBSan. Nuova build non hardware-validata.

**Non è ancora GX completo:** restano TEV multistadio/multitexture, illuminazione completa, altri texgen, pressione texture heavy e prova di riproduzione dei filmati. Non dichiarare risolti tutti i modelli bianchi/capovolti né 60 FPS.

Report e continuazione: `docs/full-content-2026-09-06/REPORT.md`.
Log estratto: `docs/full-content-2026-09-06/hardware-log.json`.
VPK: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.vpk`.
SHA-256 VPK: `84a5c216ad7bcbd0626c79c188a2a108e8c5233f917ec4e8b09b87449795c112`.
ELF: `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.elf`.
SHA-256 ELF: `b5d3a0569d5ddd8d5be7711c2e485ba8fcf6b1c3b5c2a492c307c23b740a91ed`.

## P5.2 — servizio guest timing durante WaitRender Vita

L'audit `docs/performance-audit-2026-09-06/PRODUCER-AUDIT.md` separa la scena
heavy finale in circa 1.495,397 ms producer mediani, 1.132,575 ms di renderer/GX
wait, 358,624 ms residui e 5,504 ms di packet copy. Il queue wait e trascurabile.
Il codice mostrava inoltre un boundary errato: `WaitRender()` contiene gia un
poll ogni 1 ms di `g_waitCallback`, ma la callback VI/allarmi/audio veniva
registrata soltanto dal main desktop escluso dalla build Vita.

Implementazione P5.2:

- `vita/main_vita.cpp` registra `ServiceGuestTimingDuringAuroraFrameWait`;
- ogni invocazione bounded esegue `VI_HLE_ProcessRetracesDeferred(8)`,
  `OS_HLE_ProcessAlarmsDeferred(8)` e `Audio_HLE_PollDeferred()`;
- `MKW_VITA_WAIT_TIMING_SERVICE` e un kill switch compilabile;
- native config/manifest registrano `wait_timing_service`;
- `WaitRender` accumula callback count, tempo totale e massimo;
- `producer_frame` esporta `wait_service=calls/total_us/max_us` senza abilitare
  il tracing invasivo `MKW_VITA_PERF_LOG=1`.

Profilo A/B: `full-content-p5_2-timing-service`. Mantiene il full-content
auditato (`clip_w=1`, movies/native THP), P4.1/P5.1/P6/P7, native-res EFB,
texture safe retry, queue depth 2, EFB cap 512 e translated NEON `-Os`; nessun
hot shard. `perf_log=0` e summary interval 300.

Artefatto verificato:

- VPK: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_2-timing-service.vpk`
- VPK SHA-256: `f4e8a0bcc831364538cb604150fe1b0ac2ccc2b047333b042b4a662824478d47`
- VPK bytes: `41286179`
- ELF: `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_2-timing-service.elf`
- ELF SHA-256: `83f474a5f2fae6e09901825f6bbd5403061bbfcc4f45f59b9a0f03ee2fb1fa7d`
- ELF bytes: `219270448`

PASS: compile ARM32, link, VELF/FSELF, packaging, `verify-mkw-firstboot-vpk`,
`unzip -t` e `git diff --check`. Il manifest conferma `wait_timing_service=1`,
`clip_w=1`, `perf_log=0`, nessun hot shard e tutti i flag P5.1/P6/P7 previsti.

Chiusura hardware P5.2: raggiungere la stessa scena heavy dell'audit, verificare
che `wait_service` sia non nullo nelle attese lunghe, confrontare producer,
`wait_gx`, residual e VI debt, e controllare audio/input/scheduler. Non cambiare
contemporaneamente eviction cache o packet ownership: il primo test deve isolare
il timing service. Dopo l'A/B, misurare i clear globali raw-mesh/DL e solo se
restano un costo reale passare a eviction incrementale; packet swap viene dopo.

## M13.6 / P5.3 — hardware P5.2 + incremental cache eviction (2026-09-06)

Il primo log hardware P5.2 conferma che il timing service Vita viene installato e
che il suo overhead e trascurabile rispetto agli stall misurati. A serial 421 il
producer dura 6.563.918 us, di cui 6.477.089 us in WaitRender; 4.040 invocazioni
del timing service costano complessivamente 6.374 us (max 52 us). A serial 423
953 invocazioni costano 1.604 us durante 1.541.896 us di wait. Il servizio viene
quindi mantenuto.

P5.2 non risolve pero il timing globale: serial 853 dura 6.284.921 us con appena
48 us di WaitRender e wait_service nullo, quindi quasi tutto lo stall e producer/guest
fuori dal renderer. Verso serial 1030 il VI debt e ancora ~12,35 s e il frame
combina 1.752.852 us di GX wait con circa 287 ms residui. Il log termina intorno a
serial 1030 e non raggiunge la vecchia finestra heavy 1655-1691, quindi non chiude
l'A/B prestazionale completo.

La telemetria P5.2 contava ogni invocazione del servizio anche quando
VI_HLE_ProcessRetracesDeferred usciva subito per interrupt guest disabilitati. P5.3
aggiunge quindi a vi_stall deferred=calls/interrupt-disabled/retraces-advanced, senza
cambiare la semantica degli interrupt.

Seguendo il passo successivo del performance audit, P5.3 sostituisce inoltre i clear
globali delle cache con eviction incrementale bounded, mantenendo invariati i budget:

- raw mesh: 4 MiB, LRU globale bounded fino a 64 vittime per store; se non basta,
  viene saltato soltanto il nuovo inserimento;
- DL scan/template: 8 MiB e 8192 entry, eviction LRU bounded fino a 64 record per
  store, senza distruggere il working set completo;
- flag A/B MKW_VITA_INCREMENTAL_CACHE_EVICTION;
- perf_summary: mesh_mem, mesh_evict, mesh_clear, mesh_skip;
- gx_cpu_perf: dl_cache_mem, dl_evict, dl_clear, dl_skip.

Profilo: full-content-p5_3-cache-eviction. Conserva clip_w=1, movies/native THP,
P4.1/P5.1/P6/P7/P5.2, queue depth 2, EFB cap 512, PERF_LOG=0 e nessun hot shard.

Artefatto verificato:

- VPK: build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_3-cache-eviction.vpk
- VPK SHA-256: e49f1e59b5c6c3af925524ee26d44d3aa9ec1b6d81eacf4ad59cb1b3d5108ce4
- VPK bytes: 41.279.756
- ELF: build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_3-cache-eviction.elf
- ELF SHA-256: d911e980b787cca230843c1362ca16cf01df3044034ee40deddd41dc118a9103
- ELF bytes: 219.296.100

PASS: git diff --check, graphics-check ARM32, link, VELF/FSELF, packaging,
verify-mkw-firstboot-vpk e unzip -t. Il manifest conferma incremental_cache_eviction=1,
wait_timing_service=1, perf_log=0 e translated_hot_shards vuoto.

Il prossimo hardware log deve raggiungere almeno la stessa zona di serial 1030 e,
idealmente, 1655-1691/2000. I criteri decisivi sono deferred, mesh_evict/clear/skip,
dl_evict/clear/skip, producer/wait_gx/residual e VI debt. Packet ownership/swap resta
il passo successivo solo se il packet_copy heavy (~5,5 ms storico) rimane misurabile.

## M13.7 / P5.4 — per-fiber guest interrupt state (2026-09-06)

Il log hardware P5.3 ha isolato il motivo per cui il timing service P5.2 non
riesce a recuperare il VI debt: nell'ultima finestra osservata
`deferred=21407/20838/135`. Quindi 20.838 chiamate deferred su 21.407 vengono
skippate per interrupt guest disabilitati e soltanto 135 retrace vengono
effettivamente avanzati. Nello stesso punto il VI e indietro di circa 12,35 s
(`next_retrace_due_us=-12346830`).

P5.3 non mostra pressione sulle due cache interessate dalla nuova eviction: al
serial 1200 raw-mesh usa 705.628 byte su 4 MiB con `mesh_evict=0`,
`mesh_clear=0`, `mesh_skip=0`; la DL cache ha 127 entry / 58.576 byte con
`dl_evict=0`, `dl_clear=0`, `dl_skip=0`. L'incremental eviction resta
abilitata per sicurezza, ma non e il blocker attuale.

L'analisi del scheduler ha trovato il boundary errato: l'emulazione degli
interrupt usa un singolo atomico globale `g_interrupts_enabled`, mentre
message queue, mutex e sleep possono cedere la CPU ad un'altra guest fiber
prima che la funzione chiamante esegua OSRestoreInterrupts. In quel caso la
fiber successiva eredita erroneamente lo stato IRQ disabilitato della fiber
bloccata.

P5.4 non forza callback VI dentro sezioni critiche. Introduce invece uno stato
interrupt-enable per `GuestFiber` e lo salva/ripristina nel context switch:

- la fiber sorgente conserva il proprio IRQ state prima di cedere la CPU;
- la fiber target ripristina il proprio IRQ state prima di eseguire codice guest;
- quando la fiber sorgente riprende, riottiene il suo stato originale, quindi una
  sezione critica sospesa resta correttamente IRQ-off fino al suo OSRestoreInterrupts;
- `OS_HLE_SetInterruptsEnabledForContextSwitch` cambia solo lo stato CPU host-side
  e non simula una chiamata guest OSDisable/RestoreInterrupts;
- kill switch A/B: `MKW_VITA_FIBER_IRQ_STATE=1`; startup marker e manifest
  esportano `fiber_irq_state=1`.

Profilo: `full-content-p5_4-fiber-irq`. Mantiene P5.3/full-content: `clip_w=1`,
movies/native THP, P4.1/P5.1/P6/P7/P5.2, native-res EFB, safe texture retry,
queue depth 2, cap EFB 512, incremental cache eviction e `perf_log=0`; nessun
hot shard.

Artefatto verificato:

- VPK: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_4-fiber-irq.vpk`
- VPK SHA-256: `8066d1a6cbce6ec78611d60dde20db8e3a2cfe697401e424b159e9cb7b650317`
- VPK bytes: 41.280.970
- ELF: `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_4-fiber-irq.elf`
- ELF SHA-256: `253bacb9128f2cef58c6541e6de1a45d6646d0f5abd50f03d6ded0822a48614f`
- ELF bytes: 219.297.272
- manifest SHA-256: `e0f8497a1150bc3e3f606863a3abec958b91df2487ce92407456e178cbf8a9ea`

PASS: `git apply --check` prima del patch, `git diff --check`, compilazione
ARM32, link, VELF/FSELF, packaging, `verify-mkw-firstboot-vpk`, `unzip -t` e
`graphics-check`. L'ELF contiene i marker `fiber_irq_state=%u`,
`deferred=%llu/%llu/%llu`, `mesh_mem` e `dl_cache_mem`.

Criterio hardware P5.4: il nuovo log deve mostrare `fiber_irq_state=1`. Il
rapporto deferred skipped/calls deve ridursi drasticamente rispetto a 20.838 /
21.407 (~97,3%), `deferredRetracesAdvanced` deve continuare a crescere oltre
135 e il VI debt non deve tornare nell'ordine di -12 s. Verificare inoltre
assenza di deadlock/regressioni in message queue, mutex, scheduler, audio e input.
Solo dopo questa A/B tornare a packet ownership/swap o ad altri blocker misurati.


### P5.4 — verifica di ripresa (2026-09-06)

Hash e dimensioni VPK/ELF riconfermati; verifica package PASS. Il log locale resta P5.3 (ultimo boot con incremental_cache_eviction=1, senza fiber_irq_state); manca il test hardware P5.4. Nessuna nuova patch runtime. Baseline numerica, artefatti e prossimo test in `docs/performance-analysis-2026-09-06/P5_4-HANDOFF.md`.


## P5.4 hardware tested / P5.5 wait-service attribution (2026-09-06)

Nuovo log SHA-256 `94a8579e47f83da7176cf62e199c2f0d1d4660b62f1baa9f0ab5f89ecf7df6bc`: terzo boot P5.4, marker fiber_irq_state=1. Ultimo deferred passa da 21407/20838/135 a 13874/0/3844; ultimo VI age 16,369 ms, next due +297 us. Correzione IRQ supportata nel campione, non validazione generale: rimane debt transitorio fino a 2,07 s e stall producer ~6,25 s. Serial 1200 producer 211,991 ms, renderer 52,097 ms; nessuna rivendicazione di guadagno generale.

Wait service ora costa 1400,642 ms su 1642,267 ms di WaitRender nel frame heavy 1098. P5.5 `full-content-p5_5-wait-service-profile` aggiunge solo attribuzione VI/allarmi/audio (totale/max), con MKW_VITA_WAIT_SERVICE_PROFILE=1, stessa semantica e full content P5.4. main_vita.o dipende ora dalla config per garantire il kill switch.

Build ARM32/link/VELF/FSELF/package/verify/unzip PASS, profiler OFF syntax PASS; P5.5 NON hardware tested. VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_5-wait-service-profile.vpk`, 41280700 byte, SHA-256 `8a37812e68729029cb683a3395f71f16db5aeebaf63cbe99b5518b6aa69722c0`. ELF `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_5-wait-service-profile.elf`, 219302096 byte, SHA-256 `4a62a05a9376e8d4fb364fbb25bd80a50a7adccb5fdf4423a48fa6661ab382c7`. Manifest/evidence accanto al VPK.

Report completo: `docs/performance-analysis-2026-09-06/REPORT.md`. Prossimo log: marker wait_service_profile=1, wait_service_parts e producer con stesso serial; identificare fase dominante prima di una modifica funzionale. Packet swap rinviato, nessun aumento budget o hot shard.

P5.5 `graphics-check` PASS con gli stessi flag del profilo (exit 0; warning enum preesistente GXVert.cpp). Nessuna build/test in corso; validazione hardware del nuovo profiler ancora pendente.


## P5.5 hardware / P5.6 audio wait attribution (2026-09-07)

Quarto boot runtime.log, marker fiber_irq_state=1 / wait_service_profile=1, SHA-256 `f1656b83b7bc2b44a0378d2c9f36e68e9347bc36cbd966360873f0562edd5d7a`. Audio domina il servizio: serial 424 2602771/2844471 us (91,5%); serial 861 606954/633078 us (95,9%). Contatori calls coerenti; residuo contabile 0,047–0,143%, non misura completa overhead. Il tempo audio può sovrapporsi al renderer: non prova che causi tutto WaitRender. Transizione producer ~6,257 s ancora aperta, IRQ skipped=0 nei campioni VI, debt transitorio presente.

P5.6 `full-content-p5_6-audio-wait-profile` separa join/sink/AI/AX (calls/totale/max) e conta backlog/blocchi/cap/reentry, senza cambiare ordine, limiti o lifetime. Flag MKW_VITA_AUDIO_WAIT_PROFILE=1, default 0, solo USER_0 durante audio del wait. P5.5 e full content conservati.

ARM32/link/VELF/FSELF/package/verify/unzip PASS, profiler OFF syntax PASS. P5.6 NON hardware tested. VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_6-audio-wait-profile.vpk`, 41284109 byte, SHA-256 `ceb58bf8f76fce9046678b48ef160355cbdf7a86dd23d8a7d119f507de36f148`. ELF `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_6-audio-wait-profile.elf`, 219325048 byte, SHA-256 `4771ed3950d2ab42048b90cd311f52db4b49223778098ad1f42b21cdc71518dd`. Manifest/evidence accanto al VPK.

Report e istruzioni: `docs/performance-analysis-2026-09-07/REPORT.md`. Prossimo log richiede audio_wait_profile=1 e audio_wait_parts correlato per serial. Join dominante → misurare worker/dipendenze; AI/AX dominante → simbolizzare callback e lavoro interno; non ridurre servizi sulla sola base del costo. Packet swap e aumenti budget rinviati.

P5.6 graphics-check PASS con i flag del profilo (exit 0, warning enum preesistente GXVert.cpp). Nessuna build/test in corso. Test offline conclusi senza FAIL; hardware P5.6 pendente.

## P5.6 hardware / P5.7 AI callback attribution (2026-09-07)

Il quinto boot del log hardware SHA-256
`0dca5e3a9d05f0bb5257b61f8225a45e34894f90ef321ba1c542f60f8d8d3d73`
valida P5.6 come profiler: `fiber_irq_state=1`, `wait_service_profile=1`,
`audio_wait_profile=1`, full content, `perf_log=0`. La callback AI osservata nel
render-wait e sempre `0x80551F00` (`THP::AudioMixCallback`). Nei frame misurati
AI assorbe spesso ~97-98% del tempo audio, mentre `JoinMixWorker` pesa centinaia
di ms in alcune finestre. Il backlog arriva a ~23 s nel campione serial 1000;
questo arretrato e lavoro guest da preservare, non una coda da scartare per
alzare artificialmente gli FPS.

P5.6 serial 900 resta molto oltre il budget 16,67 ms: producer `227744 us`,
wait_gx `55282 us`, packet copy `2753 us`, residual stimato `169707 us`, renderer
`61914 us`; EFB native 12/12, nessun texture fail e nessuna pressione raw-mesh/DL.
Lo stall guest multi-secondo resta quindi separato dalla sola attesa renderer.

P5.7 aggiunge soltanto attribution interna al callback AI:

- `AudioAiSubtimer` TLS-gated attivo solo durante il callback AI profilato;
- `DcRangeOp` misura cache maintenance inclusiva senza rimuovere invalidazioni;
- `AXWii::SendMail` misura mail/HandleMail inclusivo senza cambiare locking;
- snapshot bounded THP chain/mode/open/flags quando callback=`0x80551F00`;
- `audio_ai_parts` usa la stessa cadenza/serial di producer/audio_wait_parts;
- flag `MKW_VITA_AUDIO_AI_PROFILE`, default 0; profilo P5.7=1.

P5.7 mantiene P5.6/full-content, `clip_w=1`, faithful LYT, movies/native THP,
P4.1/P5.1/P6/P7/P5.2-P5.6, queue2, EFB cap512/budget4MiB, `PERF_LOG=0`,
translated NEON `-Os` e nessun hot shard.

Artefatto verificato, **non ancora hardware tested**:

- VPK: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_7-audio-ai-profile.vpk`
- bytes: `41282688`
- SHA-256: `45de24a7f4304ed08a071d14dfc07ab47a0547296a933f44b1908c3920c2de6b`
- ELF: `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_7-audio-ai-profile.elf`
- bytes: `219336952`
- SHA-256: `5ded393009e97c9ec5cd87738ae8a6af19ad7b7a5f02c388c81d5b3e8ce0a2dd`
- manifest: `1775` byte, SHA-256
  `49f05a34be329553eb94c8a0b4fd42a3cc9f4dbbf59498d4e2d9809bcd2d7a42`.

Compile/link/VELF/FSELF/package/unzip e `graphics-check` PASS. Gli hash dei
sorgenti P5.7 correnti coincidono con l'evidence della build; `git diff --check`
PASS. Nessuna nuova ottimizzazione funzionale e stata introdotta.

Un file allegato successivamente, SHA-256
`f0b7b8baa65ebe1caa62e71017f4a30acb314990eee8c2be016e8ca68dcbfaa9`,
e stato preservato come `build/vita/runtime-unmatched-f0b7b8ba.log` ma contiene
tre boot storici incompatibili con P5.6/P5.7 (EFB cap128, primo boot perf_log1 e
queue1, altri boot movies/THP off/probe grafici). Non usarlo per confronti P5.7.

Prossimo hardware: P5.7. Richiedere `audio_ai_profile=1` insieme a
`audio_wait_profile=1`, `wait_service_profile=1`, `fiber_irq_state=1` e
`perf_log=0`. `cache_us`/`mail_us` sono inclusivi e gia compresi in `ai_us`.
Se uno domina, profilare quel child; se entrambi sono piccoli, scendere dentro
`THP::MixAudio`/callback indiretta. Non rimuovere join/cache maintenance, non
droppare backlog e non introdurre P5.8 prima di questo A/B.

Dettagli: `docs/performance-analysis-2026-09-07/P5_6-P5_7.md`.

## P5.8 — native SceAudioOut functional baseline (2026-09-07)

Il test hardware ha chiarito un'assunzione precedente: sulla Vita non si sentiva
alcun audio. `vita/audio_backend_vita.cpp` era ancora intenzionalmente un null
sink di first-boot; accettava i DMA e restituiva successo, ma non apriva un port
audio e scartava tutti i campioni. Di conseguenza i tempi P5.5/P5.6 attribuiti ad
"audio" descrivono AI/AX/THP/callback/join HLE, non playback SceAudioOut.

E stato aggiunto un percorso A/B separato, senza cambiare le build storiche:

- flag `MKW_VITA_NATIVE_AUDIOOUT`, default 0;
- profilo `full-content-p5_8-native-audioout`, identico a P5.7 salvo flag=1;
- `SCE_AUDIO_OUT_PORT_TYPE_MAIN`, 48 kHz stereo, chunk 256 frame;
- worker dedicato per il blocking `sceAudioOutOutput`;
- FIFO bounded 8 chunk, drop del solo PCM nuovo su overflow, nessun drop delle
  callback/progresso guest;
- conversione BE16 right/left -> LE/native left/right;
- resampling 32 -> 48 kHz sul normale percorso Wii;
- volume Vita via `sceAudioOutSetVolume`;
- link `-lSceAudio_stub`;
- marker `native_audioout=1`, `vita_audioout opened`, `vita_audioout first_output`.

Il file audio compila con flag off e on. Build P5.8 ARM32/link/VELF/FSELF/package,
verify/unzip e graphics-check PASS; `git diff --check` PASS. Hardware validation
ancora pendente.

- VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_8-native-audioout.vpk`
- 41284474 byte
- SHA-256 `e7b9f225c6f25816dd406c994e57b0af756553d53627a5b057fe731d5cb61658`
- ELF `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_8-native-audioout.elf`
- 219541704 byte
- SHA-256 `2e8f952790e9456bb335c05f3ceca5e960f61ff3e869cc7b57feafdeb53e76c6`.

Prima validare apertura/output e udibilita reale. Se `first_output` e presente ma
non si sente nulla, profilare contenuto PCM (min/max/RMS/checksum) prima di
modificare il device path. La P5.7 null-sink resta la baseline corretta per
confrontare il puro costo HLE; P5.8 e una nuova baseline funzionale e puo avere
un piccolo costo aggiuntivo di conversione/coda. Lo stall guest multi-secondo e
il costo THP/AI restano indipendenti.

## P5.8 hardware validated / P5.9 audio pacing (2026-09-07)

Il test P5.8 ha validato il backend nativo: l'utente sente realmente l'audio e il
log mostra `native_audioout=1`, apertura MAIN 48 kHz e primo
`sceAudioOutOutput` riuscito. Snapshot:
`build/vita/runtime-p5_8-733ec259.log`, SHA-256
`733ec2590f76e2d4610a5a7731e3329ba59188cb0f2a5d3bf17feba02c746274`.

La qualita e pero intermittente/clippata durante i bassi FPS. Il log mostra
`queue_full dropping_new_pcm chunks=8` insieme a stall guest/producer da ~1,3 s
e transizioni ~6,3 s. La FIFO P5.8 contiene soltanto ~43 ms: non puo assorbire
questi stall. Il difetto e quindi un problema di **pacing della produzione guest**
che si manifesta come starvation/burst sull'output host, non un fallimento del
port SceAudioOut.

P5.9 aggiunge un A/B host-only tramite `MKW_VITA_AUDIO_PACING`:

- il worker attende il primo PCM, poi continua a chiamare `sceAudioOutOutput` al
  clock hardware anche se la FIFO e temporaneamente vuota;
- durante l'underrun produce silenzio, non PCM sintetizzato o duplicato;
- una rampa di 64 frame attenua le discontinuita verso/da silenzio;
- in overflow elimina il chunk host piu vecchio e conserva il piu recente;
- non modifica il limite dei servizi HLE, callback AI/AX/THP, backlog guest,
  interrupt o scheduler;
- statistiche ogni 512 chunk riportano real/silence/underrun/drop/high-water.

Profilo: `full-content-p5_9-audio-pacing`, identico a P5.8 salvo
`MKW_VITA_AUDIO_PACING=1`. P5.8 resta disponibile come A/B senza pacing; P5.7
resta il null-sink per misurare il puro costo HLE.

P5.9 offline PASS: compile, link, VELF/FSELF, package, verify/unzip,
`graphics-check`, `git diff --check`.

- VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_9-audio-pacing.vpk`
- 41286477 byte
- SHA-256 `65bbfccbf4905279967d6b5ed9c8963ff57831e36c10c2e377a853b601ca449a`
- ELF 219550236 byte
- SHA-256 `94ae8a1c757c6b81a44ca20e57aa64b805b24f17675ae44d4e2a5d80e3bc4696`.

## P6.7 hardware result / P6.8-MT three-core renderer split (2026-09-08)

P6.7 hardware confirms the THP restuff recovery: standard turbojpeg fails on the
Nintendo entropy stream, while repeated `restuffed_decode` + native decode succeeds
at 608x464 and the final run has no `jpeg_pixels code=11`. Geometry capacity is also
clean. Worker timing, however, proves the THP phase still spends ~0.78-0.94 s on
USER_1 despite only 15-16 physical draws; serial 480 has 111 us VBO upload, 223 us
swap and zero EFB, so render-thread CPU/texture work must be split from submission.

Adopted architectural policy: USER_0 guest/HLE, USER_1 VitaGL/GXM only, USER_2 CPU
graphics preparation + native audio. `MKW_VITA_DIRECT_PREP_WORKER=1` adds a USER_2
frame-prep stage and moves THP YUV420->RGBA conversion off USER_1. The audio output
thread is now explicitly pinned to USER_2 too. Prepared results are generation-
validated, per-frame memory is bounded to two RGBA buffers, and synchronous decode
remains a correctness fallback. No GL/GXM API runs on the prep worker.

Profile: `full-content-p6_8-mt-thp-prep`.

- VPK 41224387 bytes, SHA-256
  `a70dd685c799d7ffd4487b6324205f1d4a92acf217be9cc089386593621cc5ca`;
- ELF 218523256 bytes, SHA-256
  `689d92abfa00c49d55bf9ced153893cbc3bdd71f8e0df6c07dcc4b43840c23ca`.

Compile/link/VELF/FSELF/package/verify/unzip, graphics-check, diff-check and no-Aurora
audit PASS. Prep OFF also graphics-checks, preserving the P6.7 A/B. Next: hardware
measure `direct_prep` and `texprep`; then move vertex transform/texgen and generic GX
texture decode to USER_2. USER_1-only texture allocation/upload needs a persistent
streaming/subimage path if it remains the next dominant cost.

Hardware acceptance: marker `native_audioout=1 audio_pacing=1`; ascoltare se i
click diminuiscono e raccogliere `vita_audioout stats`. La P5.9 non e una fix
FPS: se gli underrun restano alti, tornare immediatamente allo stall guest
TaskThread/THP/scheduler invece di aumentare la FIFO o introdurre time-stretch
speculativo.

## 2026-09-08 — pivot renderer nativo GX HLE -> vitaGL, P6.4a-P6.7

Il target 60 FPS non e piu usato come criterio intermedio: l'obiettivo operativo e
prima una build corretta e giocabile a 30 FPS. Aurora non e piu il renderer della
linea P6; le entry point GX restano come compatibility/HLE ABI del codice PPC
ricompilato, ma il frame viene consumato direttamente dal renderer MKW VitaGL.

L'ultimo log hardware P6.4 conferma che il direct renderer supera il vecchio
overflow 8192/49152 dopo P6.1b e raggiunge G3D/THP senza un fault GXM osservabile.
Rimangono due classi di stall: producer/guest multi-secondo con wait GPU quasi zero,
e `wait_gx` durante il quale USER_0 esegue centinaia di millisecondi di HLE audio.
Per evitare di confondere questi costi sono state implementate le seguenti fasi:

1. **P6.4a worker timing**: `MKW_VITA_DIRECT_WORKER_TIMING=1`, summary 60 frame,
   `worker=serial/render/swap/age` pubblicato da USER_1 e `wait_sleep_us` su USER_0.
2. **P6.5 THP recovery**: fallback bounded di JPEG byte re-stuff dopo il fallimento
   standard turbojpeg; marker `turbojpeg_standard_fail/restuffed_decode/restuffed_fail`.
3. **P6.6 audio wait budget**: un solo blocco AI DMA per iterazione di render-wait,
   senza scartare backlog/callback guest; polling normale resta a quattro blocchi.
4. **P6.7 two-texture TEV**: primo subset esatto a due stage/texture con TEXCOORD0
   condivisa, unit0 MODULATE/REPLACE e unit1 MODULATE. Le altre firme restano fallback
   e sono telemetrizzate; nessuna espansione indiscriminata a TEX0..TEX7.

Build P6.7 offline PASS: compile/link, VELF/FSELF, package, verify/unzip,
`graphics-check`, `git diff --check`. Audit ELF: zero marker del renderer Aurora.

- VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_7-direct-tev-two-texture.vpk`
- bytes `41224294`
- SHA-256 `ed2c3746a1144315dc492be72b61959e553ad01804412336fc9766b6ca039eeb`
- ELF bytes `218442188`
- ELF SHA-256 `b9c092164ecec9ce4eca7077da50389c6112955989b506f26ef9f5747b65f83e`

Hardware P6.7 ancora pendente. Il prossimo log deve decidere tre cose prima di
aggiungere altra GX generica: tempo reale USER_1, successo/fallimento del re-stuff
THP, e copertura `tev_chain`/`tev_draw` del subset nativo. Solo le firme MKW residue
giustificano nuovi shader/multitexture path.
