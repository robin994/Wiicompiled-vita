# WiiCompiled M02.3 - Vita3K TRACE: GX multithreading, CPU scheduling, serial bottlenecks

Data analisi: 2026-10-11 (ora locale Italia); trace directory: `build/vita3k-trace/gx-thread-trace-20261010T215938Z/`.

## Identita e ambiente

- Stesso SELF M02.3 in tutte le prove: SHA-256 `6cafee5f7c3269a65b67f2ec5e9d350b8df06a4f0d2b1bd363be46cbc34495f8`.
- Vita3K macOS / backend Vulkan, `log-level: 0` (TRACE), `validation-layer: true`.
- In origine `log-uniforms: true` generava 279.423 messaggi `|D| [draw]` in un precedente trace. Nelle nuove prove temporaneamente `log-uniforms: false` per non inquinare il test con dump dei buffer uniform. `log-active-shaders` rimane attivo.
- Invariate le opzioni rendering: `AURORA_VITA_ASYNC_GX=ON`, `[vita].worker_threads=2`, `renderer_lanes=0`, `geometry_cache=true`, `geometry_min_vertices=48`, `draw_batching=true`, `precomputed_geometry=true`, `prepared_display_lists=true`, `render_budget_probe=true`. `[test].time_trial=true`.
- Due prove indipendenti: (A) diagnostica gioco `enabled=false`, (B) solo diagnostica gioco `enabled=true` per attribuzione host/native CPU e profilo worker. Il secondo profilo perturba il carico: **non** usare gli FPS B come misura prestazionale da confrontare direttamente con A.
- Stato finale: Vita3K chiuso; `config.yml` e `Config.toml` Vita3K ripristinati byte per byte dalla baseline; livello TRACE scelto dall'utente conservato. Nessun cambiamento sulla PS Vita reale, nessuna build Vita o installazione.

## Rendering multithread: codice e verifica runtime

Il renderer e gia multithread, ma non equivale a molteplici thread che inviano comandi GXM:

1. Il thread principale guest `WICMKW001` esegue codice ricompilato e produce comandi GX.
2. `aurora-main/lib/gx/fifo.cpp` avvia un GX consumer dedicato, coda FIFO di 8 job. Il log conferma `[gx-worker] requested=1 running=1`. Nell'emulatore si chiama `melee_gx_frontend` (nome legacy). Il consumer resta l'unico proprietario della preparazione finale e della submission SceGxm.
3. `aurora_cpu_1` e `aurora_cpu_2` sono due worker di preparazione CPU, sincronizzati mediante semafori. La soglia di default di `cpu_parallel_for_vertex` e 512 elementi per lane; nessun worker parte per i piccoli job della scena.
4. La GPU elabora autonomamente i comandi gia presentati; la pipeline grafica non usa contemporaneamente piu thread chiamanti a SceGxm.

## Profilo macOS e TRACE

Un campionamento con `sample 34730 5` durante stage 0 dell'introduzione ha individuato (su 118 campioni per thread):

| Thread | Stack osservato |
| --- | --- |
| `WICMKW001` | 96 campioni in `ThreadState::run_loop()` eseguendo il guest |
| `melee_gx_frontend` | 83 campioni in `sceKernelWaitSema` (consumer in attesa) |
| `aurora_cpu_1` | 118 campioni in `sceKernelWaitSema` |
| `aurora_cpu_2` | 118 campioni in `sceKernelWaitSema` |

Sono campioni di stack host su emulatore, non percentuali CPU hardware e non un profiler ARM guest instruction-by-instruction. L'indicazione e che game/simulazione e preparazione GX seriale costituiscano entrambi limiti rilevanti.

Su Vita3K, dalla diagnostica B per frame >=229:

| Metrica | Risultato |
| --- | ---: |
| FPS ponderati A (TRACE, diagnostica gioco off) | 1,225 |
| FPS ponderati B (TRACE, diagnostica gioco on, non A/B prestazionale) | 1,126 |
| Tempo `fifo_us` per frame in B | 366,0 ms |
| Chiamate totali `cpu_parallel_for_vertex` nelle 15 finestre B | 1.113.131 |
| `dynamic_calls` | 0 |
| Somma `wall_us` di questo percorso sui thread chiamanti in B | 21.005.518 us (annidati) |
| Mediana `native_finish` sui frame campionati | 2 |
| Frame circa 264 (12 frame di finestra), invocazioni `cpu_parallel_for_vertex` | 73.813 |
| Frame circa 264, elementi processati nel chiamante / helper 1 / helper 2 | 418.176 / 0 / 0 |
| Frame circa 264, attesa chiamante `caller_wait_us` | 0 |

Per il frame 229, 11 frame richiedono 10,596 s, di cui `fifo_us=4,432 s`, mentre `native_request_us=12,7 ms`: metriche parzialmente sovrapposte, **non sommabili**. Nei campioni `[wic-render-budget]`, `pipeline_us`, `texture_us`, `draw_us` e le attese di `sceGxmFinish` sono molto piu piccoli del wall medio, ma non coprono l'intero lavoro nel consumer.

La geometria e dominata da circa 6.000 draw GX/frame molto piccoli (4-8 vertici tipici). Abbassare ciecamente la soglia dei worker a 4-8 vertici richiederebbe migliaia di risvegli/semafori, con overhead potenzialmente superiore al lavoro utile. Il confronto Vita3K geometry cache ON/OFF (da circa 1,2 a 6 FPS nella precedente prova) **non** si riproduce sull'hardware: con soglia 65535 la PS Vita resta ~1,27 FPS, contro baseline ~1,32 FPS. Non estrapolare prestazioni da emulatore ad ARM/GXM reale.

Il profilo diagnostico contiene decine di migliaia di operazioni scalar/paired FMA. La build attuale compila `WIC_EXACT_FLOAT_FMA=ON`; modificare la precisione o utilizzare NEON non-fused sarebbe un esperimento di compatibilita numerica, **non** un fix sicuro basato sul solo tracing.

## Intervento conservativo applicato (solo sorgenti Aurora-Vita)

Modificati:
- `aurora-main/platforms/vita/gfx/vita_cpu_workers.cpp`;
- `aurora-main/tests/vita_cpu_workers_test.cpp`.

In `cpu_parallel_for_min_lanes_impl` si normalizza `minItems` immediatamente dopo il controllo di pool inattivo, e quando `count / minItems < 2` si esegue direttamente `runCaller(0,count)`, senza acquisire `g_workers.busy` e senza selezionare affinity/worker per job troppo piccoli. La logica per dispatch di almeno due lane, gestione core3, callback fallite e sincronizzazione resta invariata.

Aggiunti 512 test di lavori a 6 vertici per ciascuna configurazione worker esistente, verificando risultato, lane 0 esclusiva e copertura univoca. Verifiche effettuate **host-only**:

- `cmake --build build/aurora-host --target aurora_vita_cpu_workers_test -j 4`: OK.
- `ctest --test-dir build/aurora-host -R '^vita_cpu_workers$' --output-on-failure`: 1/1 PASS.
- `cmake --build build/aurora-host -j 6`: OK.
- `ctest --test-dir build/aurora-host --output-on-failure -j 6`: 27/27 PASS.
- `git -C aurora-main diff --check`: OK.
- La nuova patch **non e contenuta** nel SELF gia installato. Non e stato misurato alcun incremento FPS sul device reale. L'effetto atteso e solo la riduzione dell'overhead di dispatch nei piccoli job; la maggior parte dei ~366 ms/frame FIFO richiede interventi strutturali.

## Priorita successive

1. Preparazione di forme e materiali **NW4r native ResShp/ResTev** (M02/M03), precompute dell'interpretazione statica e riuso per oggetto, mantenendo le uniform/pose dinamiche aggiornate. Obiettivo eliminare migliaia di traduzioni GX da 4-8 vertici per frame, non moltiplicare i thread SceGxm.
2. Profilare stage0 e poi stage2 su **hardware** con strumenti a basso overhead distinti: tempo game thread, FIFO CPU, completamento coda GX, GXM submit e wait. Mantenere identici SELF/PSARC e scene nelle varianti. Per ora `worker_frame_us` e tempo trascorso tra frame, NON tempo CPU esclusivo del consumer.
3. Valutare il batching dei job di decodifica/trasformazione **solo** con snapshot immutabili, dipendenze state/texture/indici esplicite e barriere prima della submission GXM, per evitare regressioni visive e use-after-free. Non abbassare a 4 la soglia dei worker.
4. Risolvere separatamente il modello Mario bianco e il percorso depth EFB incompleto. Il bianco persiste usando CPU lighting, shader TEV di riferimento e cache geometria disattivata; non e provato un problema di budget CDRAM. Verificare texgen/UV, handle e contenuto delle texture per il draw specifico.
5. Il precedente test hardware di `geometry_min_vertices=65535` non ha aiutato. Non ripeterlo come ottimizzazione senza altri dati; fare un controllo `geometry_cache=false` sul device solo come A/B separato, dopo richiesta utente.

## Evidenze

Le sessioni isolate e i profili sono sotto `build/vita3k-trace/gx-thread-trace-20261010T215938Z/`, in particolare:

- `host-sample-stage0.txt` (stack trace di thread host);
- `light-vita3k-trace.log`, `light-runtime.log`, `light-metrics.json`;
- `diagnostic-vita3k-trace.log`, `diagnostic-runtime.log`, `diagnostic-metrics.json`;
- `vita3k.before.yml`, `Config.before.toml` (baseline ripristinata).

Nessuna modifica a shader, renderer GXM, texture, PSARC o codice generato. Nessun commit o push.

## Follow-up: A/B delle prepared display list e bypass dei draw piccoli

Acquisizione: `build/vita3k-trace/prepared-ab-20261010T221643Z/`. Stesso
SELF M02.3 (SHA-256 `6cafee5f7c3269a65b67f2ec5e9d350b8df06a4f0d2b1bd363be46cbc34495f8`),
stesso PSARC, stesso Time Trial e `[diagnostics].enabled=true` in entrambe
le varianti. Vita3K TRACE con `log-uniforms=false` per non stampare i buffer
uniform per ogni draw. Solo `[vita].prepared_display_lists` cambia.

| Metrica (stage 0, frame 229-281) | ON | OFF |
| --- | ---: | ---: |
| Frame misurati | 53 | 49 |
| Finestrelle di profilo | 5 | 5 |
| FPS ponderati | 1,019 | 0,953 |
| FIFO `process_time_total_us()`, media/frame | 402,64 ms | 448,19 ms |
| `native_request_us`, media/frame | 1,34 ms | 1,83 ms |

**Risultato:** disattivare le prepared display list non migliora il carico
FIFO osservato su Vita3K. Il campione e breve, la fase intro non e perfettamente
identica per ogni frame e alcuni tempi di logging differiscono: non e un
benchmark affidabile per affermare una regressione del 6,5% su PS Vita.
Non promuovere `prepared_display_lists=false` come ottimizzazione.
Il consumo medio FIFO in entrambe le varianti e comunque lontanissimo
dal budget completo di 16,67 ms/frame necessario per 60 FPS.

La diagnostica precedente mostrava, al frame 384, 6433 draw GX richiesti
e 5965 mergiati nel batch nativo (468 draw residui), senza fallback texture
bianche segnalate. Sul campione `frame-300-draws.csv`, 708/773 draw
(91,6%) avevano meno di 48 vertici. Questo e un campione di draw catturati,
non una distribuzione certa di tutti i draw.

### Correzione isolata in Aurora-Vita, non inclusa nel SELF del TRACE

File modificati nel submodule Aurora:

- `platforms/vita/gx/aurora_vita_draw_sink.cpp`
- `tests/vita_frontend_translation_test.cpp`

`DrawSink::submit` verifica adesso una sola volta se ha senso esaminare
la fixed-vertex GPU path:

```cpp
const bool considerGpuVertex=staticGeometry_&&
    (vertexCount>=fixedMinVertices||allowStreamedFixedVertexGpu_);
```

Per i draw sotto soglia, con streaming GPU fixed disattivato, si saltano
aggiornamento del layout/recipe GPU e `supports_fixed_vertex_gpu`. Si
mantiene l'elaborazione CPU completa, compresi matrici, texgen e lighting.
La condizione `allowStreamedFixedVertexGpu_` preserva i consumer che usano
legittimamente quel percorso. Il caso `small -> cacheable large -> small`
e verificato in un test di regressione, controllando pipeline CPU/GPU e
resource accounting.

Verifiche effettuate:

- `cmake --build build/aurora-host --target aurora_vita_frontend_translation_test -j 4`: OK.
- `ctest --test-dir build/aurora-host -R '^vita_frontend_translation$' --output-on-failure`: PASS.
- `cmake --build build/aurora-host -j 6`: OK.
- `ctest --test-dir build/aurora-host --output-on-failure -j 6`: **27/27 PASS**.
- `git -C aurora-main diff --check`: OK.

Questo intervento rimuove lavoro morto dalla hot path dei draw piccoli,
ma **non** quantifica un incremento FPS reale: il SELF dei test A/B resta
M02.3 senza la patch, e il dispositivo non e stato modificato.

### Cleanup e limiti

Vita3K e stato chiuso dopo la seconda acquisizione. Il normale comando
di uscita e poi SIGTERM non hanno fermato il processo di test, per cui si
e reso necessario terminarlo con SIGKILL dopo aver raccolto sufficienti
finestre. `Config.toml` e `config.yml` dell'emulatore sono stati
ripristinati byte per byte dai backup; **nessun** file o INI della PS Vita
reale e stato toccato. Le acquisizioni e il confronto sono in
`prepared_on/`, `prepared_off/` e `COMPARISON.json`.
Nessuna build Vita, installazione, commit o push.

### Prossimo intervento sostanziale

Ridurre le chiamate per draw a `DrawSink::submit` tramite ricette immutabili
per gruppi di primitive compatibili nei modelli NW4r. Qualunque aggregazione
deve preservare ordine, cambi di stato/materiale, texture EFB, barriera/clear,
matrici animate e lifetime degli snapshot. Il TRACE non dimostra che una
nuova coda o un maggiore numero di worker risolverebbe il collo di bottiglia.
