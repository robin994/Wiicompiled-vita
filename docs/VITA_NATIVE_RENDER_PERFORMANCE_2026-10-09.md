# WiiCompiled Vita: rendering nativo, cache e prestazioni

9 ottobre 2026. Implementazione locale sul branch `codex/restart-iso-gxm-20261005`, HEAD WiiCompiled `279ce8328f17059a4b46172d4ce71d3f403a1aec`, Aurora embedded `27357eb889cd0a13d904c8bc708db9b3d162ed0c`. Entrambi i checkout contengono modifiche locali preesistenti. Nessun reset, commit o push in questa attività.

## Richiesta e risultato attuale

L'utente conferma che il Time Trial avanza, Mario e alcuni materiali alternano bianco e texture visibili, il HUD manca e il gioco resta circa a 1 FPS. Richiede rendering asincrono, distribuzione del lavoro sui thread, chiamate native per gli asset, prewarm e cache shader/vertici, trasferendo le soluzioni applicabili dagli altri port.

È stata verificata sulla Vita la correzione dell'ID controller descritta nel [report dedicato](VITA_TIME_TRIAL_CONTROLLER_2026-10-08.md). Per le prestazioni sono implementati configurazione, avvio delle cache prima del prewarm, collegamento dei percorsi diretti già presenti in Aurora, import degli shader e candidate separate. **Non è ancora dimostrato un aumento degli FPS né una correzione di materiali/HUD nelle nuove candidate.** Non è ancora implementato un percorso nativo completo per animazioni, pose e tutti i materiali NW4r del gioco.

## Evidenza hardware disponibile

### Sessione della correzione controller

`build/render-native-20261009/capture-20261008T222838Z/` lega log/config al SELF installato `b3bf5f62bfaacbf01d34a3eb700c21d02534dc1679b2d2e5fc4f27537e5e9895`.

- GX worker richiesto e avviato: `1/1`. Il rendering asincrono era già attivo nella build provata.
- Controller registrato/attivo `0x124`; pausa controller/sezione/scena zero.
- `stage=2`, opening pan terminato, `race_frames=244 -> 290` tra frame 635 e 681.
- Ultime cinque finestre: 59 frame in 52.056960 s, **1,133 FPS** aggregati. Attribuzione renderer/CPU disattivata; una finestra scende a 0,694 FPS. È una misura della cadenza registrata, raccolta anche via FTP, non un confronto A/B controllato.
- Tre layer UI creati non provano che il HUD arrivi ai pixel. Nessuna nuova immagine della stessa sessione è stata acquisita; l'alternanza dei materiali e il HUD assente sono feedback dell'utente.

### Precedente sessione diagnostica

`build/time-trial-freeze-20261008/capture-20261008T201504Z/`, SELF `fe90c1e3...`, mostrava una gara in pausa. I dati restano utili per attribuire quella scena, senza trasferirli automaticamente alla gara ora attiva.

| Dato diagnostico | Evidenza e limite |
| --- | --- |
| Pool CDRAM | 64 MiB, picco 39.587.840 byte = 37,75 MiB, zero allocation fallback. Nessuna evidenza di esaurimento di questo pool. |
| Draw del frame 300 | 6.260 draw, 54 shader fragment effettivi; cattura completa senza record persi. Frame della scena in pausa. |
| Texture | Oltre un milione di hit cumulativi, 135 snapshot, upload stabili e nessuna eviction tardiva nei campioni. I contenuti/stati effettivi restano da confrontare. |
| Geometria residente | 30 entry, circa 339 KiB, 13 hit GPU nel campione rispetto a migliaia di draw. Copertura limitata. |
| Materiali nativi | 4.669 dei 6.260 draw del campione usano la specializzazione nativa; la copertura non è completa. |
| Helper | Nei campioni piccoli il lavoro sui helper risulta zero. Creare altri thread non garantisce una ripartizione utile. |
| Prewarm | 192 pipeline già riscaldate dalla precedente build. Il costo basso degli hit shader non elimina da solo il costo di migliaia di draw per frame. |

Non viene aumentato il pool e non viene compensata artificialmente la luminosità.

## Modifiche implementate

1. **Configurazione applicata al backend reale.** `[vita]` in `Config.toml` controlla pre-caricamento GXP, limite prewarm, budget CPU degli asset, helper e lane renderer, cache geometria, illuminazione GPU, materiali nativi, display list preparate, batching, viste immediate, cache della preparazione fragment e pool uniformi. I valori numerici sono limitati; i booleani non validi mantengono i default. Le opzioni sperimentali restano disattivate finché non sono selezionate. EFB 1024×640/display 960×544 restano quelli della correzione delle light map.
2. **Ordine di avvio corretto.** Mount/verifica PSARC e cataloghi grafici/audio avvengono prima dell'inizializzazione GXM/prewarm. Prima il lettore nativo poteva ricevere richieste quando l'archivio non era ancora montato. I progressi del prewarm sono registrati, senza bloccare i draw sconosciuti con una cache sigillata.
3. **Submission nativa diretta.** `WIC_NATIVE_DIRECT_SUBMIT=ON` collega il percorso Aurora già esistente che invia i pacchetti preparati al renderer GXM sul thread proprietario, scaricando i comandi precedenti nell'ordine corretto. L'audit ARM verifica la chiamata da `DrawSink::submit` a `gfx::Renderer::draw` nel binario effettivo.
4. **Streaming diretto dei vertici.** `WIC_NATIVE_DIRECT_STREAM=ON` risolve i puntatori ai buffer persistenti del ring durante la loro creazione. I dati vengono scritti nelle pagine gestite dal ring; restano le regole di ritiro/sincronizzazione già presenti in Aurora. L'audit ARM verifica le chiamate a `BufferPool::writable` da `StreamingArena::initialize`. Questa opzione è separata dalla submission.
5. **Cache shader native.** Il lettore PSARC accetta esattamente `native/v1/shaders/gxm-cg-gxp-v1/[vf]-[16 hex].gxp`. Il consumer Aurora verifica identità/stage, estensione e checksum; GXM verifica il programma prima della registrazione. Il tool host importa solo record validi e rifiuta collisioni. Sono acquisiti dalla Vita **477 record, 404.484 byte**, validi e stabili tra le letture. Il pre-caricamento dei binari persistenti è ora configurabile prima della creazione delle pipeline.
6. **Cache CPU degli asset limitata.** Il budget del lettore nativo è configurabile da 0 a 32 MiB; default 2 MiB. Il catalogo mantiene i miss grafici senza I/O e le entry sono riutilizzate con LRU. Le cache GPU di geometria/texture già esistenti conservano identità e invalidazione.
7. **Geometrie piccole.** `geometry_min_vertices` può scendere da 48 a 3 per le geometrie immutabili ammesse dal renderer; il preparatore host espone lo stesso limite. Il PSARC attualmente installato contiene sidecar generati con soglia 48: abbassare il limite runtime non crea nuovi sidecar offline. I draw ammessi senza sidecar conservano preparazione CPU/cache runtime. I limiti di sicurezza per fonti dinamiche/non tracciate restano nel renderer.
8. **Core distinti.** Con `distinct_cpu_dispatch=true` il game thread viene assegnato a CPU0; il GX worker resta sul core già previsto dal motore e il dispatcher evita di inviare lavoro a un helper sullo stesso core del proprietario renderer. I default hanno due helper. CPU3 e CapUnlocker non vengono abilitati in questa integrazione.

I sorgenti Aurora confrontati con la candidata controller sono invariati. Questa attività collega/configura capacità presenti nel motore embedded e modifica il bootstrap/lettore del port; conserva le correzioni locali di copie EFB, scissor, stato delle scene e presentazione.

## Che cosa significa eliminare le traduzioni

Texture, geometrie statiche e PCM preparati nel PSARC sono consumati da routine ARM native. I pacchetti preparati possono arrivare direttamente a GXM con la nuova variante. Il frontend GX/GXM di Aurora è già codice nativo: non è un interprete PPC.

Resta però codice del gioco ricompilato che aggiorna oggetti, animazioni e stato NW4r/GX e produce i draw. Eliminare anche questi passaggi richiede un contratto nativo completo per materiali, pose/skinning, matrici e comandi di modello, con snapshot immutabili. Attivare la submission diretta non realizza quel contratto. Texture EFB, light map, video e risorse dinamiche devono continuare a rispettare invalidazione e ordine; non possono essere trattate tutte come asset statici.

## Configurazione

Esempio conservativo con pre-caricamento; le opzioni non riportate mantengono i default:

```toml
[test]
time_trial = true

[diagnostics]
enabled = false

[vita]
shader_binary_preload = true
shader_preload_limit = 1024
pipeline_prewarm_limit = 192
native_asset_cache_mb = 2
worker_threads = 2
renderer_lanes = 0
geometry_min_vertices = 48
prepared_display_lists = false
draw_batching = false
immediate_draw_view = false
fragment_prepare_cache = false
fixed_uniform_pool = false
geometry_preflight = false
distinct_cpu_dispatch = false
native_materials = true
geometry_cache = true
gpu_lighting = true
```

`geometry_cache` e `gpu_lighting`, se assenti, mantengono la politica compilata originale. Limiti: shader 0..1024, pipeline 0..512, asset CPU 0..32 MiB, helper 0..2, lane renderer 0..3, minimo vertici 3..65535. Per confronti isolati l'installer imposta esplicitamente l'intero gruppo `[vita]` e conserva valori sconosciuti, disco, audio, input e altre sezioni. Non sommare manualmente esperimenti prima del confronto.

## Asset shader e rigenerazione offline

Cattura originale: `build/render-native-20261009/shader-capture-20261008T224525Z/`; copia importata: `build/render-native-20261009/shader-sidecars/`. Manifest: `SHADER_IMPORT.json`. La directory `shader-sidecars` contiene solo i nuovi sidecar e **non è un archivio completo avviabile**.

Le candidate usano anche i record già presenti nella cache persistente della Vita. L'import nell'archivio evita dipendenza da quella cache per i programmi incontrati, ma il pre-caricamento di tutta la directory richiede ancora la cache persistente: il preload Aurora enumera quella directory e l'archivio viene consultato per hash durante il caricamento delle pipeline.

Per rigenerare un archivio completo, senza sostituire quello attuale:

```sh
python3 scripts/prepare_mkw_native_assets.py \
  --source build/vita-data/mariokart-avc.psarc \
  --output build/vita-data/mariokart-native-render-20261009.psarc \
  --shader-cache build/render-native-20261009/shader-capture-20261008T224525Z \
  --geometry-min-vertices 3
```

Il tool verifica tutti i file originali per SHA-256 nel nuovo PSARC e rifiuta output già esistenti. È una rigenerazione completa, non eseguita né installata durante questa attività. Per usare sidecar con soglia 3, selezionare anche `geometry_min_vertices=3` nel config. Il contenitore resta l'unica sorgente runtime; non serve una directory di asset estratti sulla Vita. Gli eseguibili guest originali mantengono i loro hash.

## Candidate e installazione

Il bundle è `build/render-native-20261009/`. `reference`, `native-submit` e `native-stream` hanno GX asincrono attivo e gli stessi sorgenti del port/renderer; differiscono per le due opzioni di compilazione diretta. Il controllo `synchronous` serve ad attribuire le regressioni e non è una soluzione prestazionale.

Il marker delle candidate asincrone è `psarc-resident-assets-async-native-render-20261009`; le opzioni effettive sono riportate da `[native-render-config]`. L'hash SELF distingue le varianti con lo stesso marker.

Con WiiCompiled chiuso e FTP VitaShell su `192.168.1.79:1337`, primo test della submission:

```sh
python3 "/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/install-native-render.py" --profile native-submit --time-trial --quiet
```

L'installer verifica SELF/VPK, la compatibilità degli altri file dell'app, il trasferimento temporaneo e il risultato installato. Crea backup SELF/config, ripristina dopo attivazione parziale e salva un record locale con hash. Non avvia il gioco. Se gli altri file dell'app differiscono, richiede di installare il VPK completo in VitaShell prima di ripetere il comando.

| Profilo | Differenza rispetto al riferimento |
| --- | --- |
| `reference` | Controllo asincrono; percorsi diretti disattivati; pre-caricamento binari disattivato; prewarm 192. |
| `native-submit` | Solo submission diretta. |
| `native-stream` | Submission diretta e streaming diretto; confrontare con `native-submit`. |
| `prewarm` | Solo pre-caricamento binari shader, fino a 1024. |
| `immediate-view` | Vista immediata; confrontare con `native-submit`. |
| `prepared-dl` | Display list preparate. |
| `small-geometry` | Soglia cache geometria 3. |
| `batching` | Batching locale del renderer, senza riordinare draw. |
| `fragment-cache` | Cache preparazione fragment. |
| `uniform-pool` | Pool uniformi fisse. |
| `geometry-preflight` | Preflight geometria. |
| `cores` | Game thread CPU0 e dispatch verso core distinti. |
| `cpu-lighting` | Illuminazione GPU disattivata, fallback CPU. |
| `cpu-geometry` | Geometria residente disattivata. |
| `reference-material` | Specializzazione materiali nativi disattivata. |
| `synchronous` | Worker GX disattivato nella compilazione; confronto di correttezza con `reference`. |

Per attribuire l'intermittenza, usare `reference` e poi `synchronous` con la stessa scena; se serve, `cpu-lighting` e `reference-material`, uno per volta. Per prestazioni, confrontare `reference -> native-submit -> native-stream` a diagnostica disattivata. Il prewarm si misura separando tempo di avvio e regime della gara.

## Verifiche e limiti

- Root host: **24/24 PASS**, inclusi configurazione applicata al vero `BackendConfig`, controller con routine tradotte originali, namespace/cache PSARC e import shader.
- Aurora host: **24/24 PASS**, inclusi cache, packet/snapshot e percorsi di fallback disponibili su host.
- Build ARM/Vita e VPK delle candidate: verificate separatamente, con hash SELF/ELF e SFO `ATTRIBUTE2=12`.
- Audit ARM: GXM senza renderer vitaGL, runtime/thread, native asset, barriere asincrone e chiamate dirette nelle varianti previste. Il controllo sincrono ha un controllo distinto dell'opzione disattivata.
- Installer: preservazione del config, idempotenza per tutti i profili, attivazione e rollback FTP simulato. Queste prove non inviano file alla Vita.
- Nessuna delle nuove candidate prestazionali è stata installata o avviata dall'agente. I test host non eseguono GXM reale e non dimostrano FPS o correttezza dei pixel.
- I 477 record shader passano i controlli host del contenitore; la registrazione GXP delle nuove candidate richiede la Vita.

## Lavoro residuo ordinato

### R1 — Materiali intermittenti e HUD

Scope: renderer GX/GXM e stato dei draw. Usare i controlli sincrono/CPU lighting/materiale originale per distinguere lifetime asincrono, pipeline nativa e illuminazione. Conservare stessa configurazione e immagini in fase di gara attiva, poi associare le variazioni a serial FIFO, frame consumer, shader attivo, target, texture, matrici e scissor. I layer UI possono esistere anche se i draw vengono persi.

File principali: `aurora-main/lib/gx/fifo.cpp`, `aurora-main/platforms/vita/gx/aurora_vita_draw_sink.cpp`, `aurora-main/platforms/vita/gxm/gxm_renderer.cpp`, `runtime/src/hle/gx/gx_dl.cpp`, `vita/game_diagnostics.cpp`.

Gate: Mario e scenario stabili per più frame, HUD presente nella gara e menu corretti; nessuna regressione EFB/light map/copie display. Una singola immagine corretta non chiude l'intermittenza. Non eliminare dirty state, sincronizzazioni necessarie o alpha test per mascherare il difetto.

### R2 — Costi per draw e vertici

Scope: misurare submission, decode/transform, matrice, texture/materiale, attese guest/consumer senza sommare timer annidati. Isolare le opzioni già collegate prima di una riscrittura. Estendere cache a piccoli draw immutabili e riutilizzo delle display list solo se il numero di preparazioni/copie si riduce davvero.

File principali: draw sink, `vita_draw_adapter.cpp`, `vita_streaming_arena.cpp`, `vita_persistent_geometry.*`, `vita_cpu_workers.cpp`, configurazione port.

Gate: stessa geometria e ordine, fallback su miss o risorse dinamiche, misure quiet ripetute dopo warmup. Costo CPU di submit non equivale a durata GPU. Target 30 FPS ancora aperto.

### R3 — Comandi nativi completi per modelli/materiali

Scope: descrittori precompilati per geometria/materiale e comandi diretti che evitino l'emissione ripetuta NW4r/GX; snapshot per matrici e pose dinamiche, con invalidazione di revisioni delle risorse. Mantenere il percorso originale per casi non coperti. Le override guest devono rispettare registro/ABI/dispatch e relativa generazione, non introdurre un vincitore isolato incompatibile con i caller.

File principali: preparatore `scripts/native_assets/formats.py`, compilatore Aurora, lettore `vita/native_assets.*`, adapter native/display list, runtime NW4r/entry tradotte interessate.

Dipendenze: R1 corretto e attribuzione R2. Gate: comparazione indipendente di vertex payload, material state e immagini; copertura misurata per modello/UI, zero richieste guest per dati già nativi nel percorso coperto. Non dichiarare tutti gli asset diretti finché animazioni/materiali dinamici non sono implementati e confrontati.

### R4 — Prewarm di gara e lavoro parallelo utile

Scope: raccogliere gli hash effettivi delle pipeline per Mario/Kart/Luigi, pre-caricare shader validi e preparare buffer immutabili prima della gara, mantenendo fallback per UI ed effetti sconosciuti. Aggregare compiti sufficientemente grandi; partizionare solo lavoro CPU puro, con copertura esatta dei range e senza spostare le chiamate GXM dal proprietario renderer.

File principali: `gxm_program_cache.cpp`, `gxm_renderer.cpp`, `vita_cpu_workers.cpp`, `vita/game_main.cpp`, preparatore/manifest asset.

Gate: distinguere avvio freddo/caldo e FPS a regime; lane realmente utilizzate, niente race o stalli aggiunti. Nessun core extra abilitato sulla base di una capacità storica di un altro port.

Le lezioni trasferite sono cache persistenti validate, asset offline, submission diretta, opzioni reversibili, attribuzione e identità dei binari. Risultati prestazionali e patch specifiche di Strikers/vitaGL non vengono attribuiti automaticamente a questo port GXM.

## Identità delle candidate

| Candidata | SELF SHA-256 |
| --- | --- |
| `reference` | `e8647fee69ccc9d6f47eba86f851f9a21e3f92ae1f9867a2729e4799016b2d43` |
| `native-submit` | `b7a8123ab0f50357a77a0d375b24ea9c57e55dfc8218e20ead8882c16d323ac5` |
| `native-stream` | `c59cbdb5e12a1167e8644a9d76e9fd1ff95fe2ea1ac76c2bd2881c50f3407586` |
| `synchronous` | `d078eef92c8dc368f2f9047b3d44a2fe04138eee8742554cd9024c43b9e13017` |

`CANDIDATES.json` e `IDENTITY.json` dei singoli bundle riportano anche ELF, VELF, map e VPK. Gli installer verificano hash SELF letterali, senza ricavarli dalla build modificabile. Ogni bundle conserva sorgenti, flag, manifest, audit e `SHA256SUMS`. Le candidate asincrone condividono gli stessi sorgenti C/C++ verificati; i percorsi diretti sono selezionati con i flag.

## Build riproducibile dopo il controllo

`build/vita-psarc` contiene l'ultima compilazione di controllo **sincrona**. Le candidate asincrone da installare sono i bundle separati, non quel prodotto modificabile. Per ricompilare con submission nativa e rendering asincrono, reimpostare esplicitamente le opzioni:

```sh
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON \
  -DWIC_PRECOMPILED_ASSETS=ON -DWIC_RESIDENT_GEOMETRY=ON -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=ON -DWIC_NATIVE_DIRECT_STREAM=OFF
```

Il resto delle opzioni già presenti nel cache CMake resta quello della build corrente; questa istruzione ricostruisce con gli stessi asset e senza rigenerare le traduzioni. Per riprodurre esattamente una candidata da un'altra directory, usare anche tutte le opzioni registrate nel suo `CMakeCache.txt`/`DELIVERY.json`. Gli audit diretti e del modo asincrono/sincrono sono ora eseguiti anche da `build-vita.sh`.
