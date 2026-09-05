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
