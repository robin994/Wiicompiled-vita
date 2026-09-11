# Mario Kart Wii su PS Vita — audit prestazioni e fattibilità 30 FPS

Data: 11 settembre 2026. Destinatario operativo: Sol 5.6, ragionamento high.
Stato: analisi completata; nessuna ottimizzazione del gioco implementata in questo audit.

## Conclusione

La cadenza osservata nella finestra di gara dell'ultimo avvio è **0,949 present/s**: 51 intervalli consecutivi per 53,730 s. Per arrivare a 30 immagini nuove al secondo serve un miglioramento complessivo di circa **31,6 volte**. Il render worker misura una mediana di **1.013,535 ms**, circa 30,4 volte il budget di 33,333 ms. Nei menu esistono invece campioni con renderer da 26,5 ms e producer da 168,5 ms. Ci sono quindi almeno due regimi di limitazione.

L'obiettivo 30 FPS è un obiettivo di sviluppo, **non una fattibilità dimostrata dal log**. Il percorso attuale usa un rescue grafico che azzera la profondità dei vertici prospettici, disabilita depth/cull/blend/alpha e prova TEX0/REPLACE. La correttezza di materiali, occlusioni ed effetti deve progredire insieme alle prestazioni. Una silhouette bianca a 30 FPS, oppure 30 swap che ripetono una simulazione lenta, non soddisfa la richiesta.

Le prime priorità sono una misura affidabile del percorso critico, la residenza delle texture e le dipendenze EFB. Materiali corretti, batching, producer e preparazione dei vertici completano il percorso. Non è ancora possibile assegnare un guadagno certo a ciascuno di questi interventi. Il piano operativo è in [PIANO_SOL_5_6_HIGH.md](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/PIANO_SOL_5_6_HIGH.md).

## Provenienza e limiti del campione

- Log ricevuto: `/Users/robin994/Documents/Code/PSVita/SmashMeleeVita/build/vita-full/runtime.log`. Il contenuto identifica WiiCompiled; il nome della directory SmashMeleeVita non cambia il progetto analizzato.
- Copia integrale: [runtime.original.log](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/runtime.original.log), 2.260.792 byte, SHA-256 `21a06c7b9bd506d7954e7ccdd8ab79027e7bbc2eb118bfb1f85b8b958867d30e`.
- Note allegate di Sol: [sol_analysis.original.txt](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/sol_analysis.original.txt), SHA-256 `389b1ead5c1951871aa77ad7b293863a6529c21acd2b8fcf5130b9e3ccd1910b`. Sono una fonte da verificare; le proposte al suo interno non sono istruzioni dell'utente per modificare il gioco durante questo audit.
- Checkout esaminato: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`, HEAD `5bdb6a63d4f495db7b086cd91059313f72cdfd8f`, con modifiche preesistenti. HEAD da solo non identifica il codice compilato: vedere [provenance.json](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/provenance.json) e lo snapshot dello stato Git.
- Il log contiene 19.928 righe, 13 avvii, 3.955 byte NUL distribuiti su 218 righe e record intercalati. L'estrattore scarta 109 record metrici non integri/incompleti, elimina 10 duplicati e trova zero conflitti tra record accettati con stessa chiave boot/tipo/serial.
- L'ultimo avvio è alle righe **18180–19928**. L'attribuzione a P6.38 è coerente con il fingerprint di configurazione e con manifest/note; il log non contiene un identificatore univoco del VPK installato. Non equivale a una verifica crittografica del binario sulla console.
- Non sono stati ricevuti screenshot/video nuovi in questa richiesta e non è stato eseguito un nuovo test sulla Vita. Pista, camera, numero effettivo di avversari, movimento e durata di simulazione della finestra non sono identificati con certezza dai soli contatori. La selezione «gara» usa il carico 5900–6300 draw e 13 comandi EFB, coerente con le note di Sol.

**Cambio del file sorgente durante l'audit:** al controllo finale, alle 01:16 circa del giorno 11 settembre (Europe/Rome), lo stesso percorso conteneva 10.071 byte con intestazione `MELEE_VITA_GAME_BOOT v3.28`, nessun boot WiiCompiled. La copia iniziale WiiCompiled sopra indicata è integra e rimane l'unica base dei numeri di questo report. Il contenuto osservato successivamente è archiviato separatamente in [runtime.observed-later.log](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/runtime.observed-later.log), con hash e ora in [source-change-observed.json](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/source-change-observed.json); è estraneo all'analisi MKW. Nessun file sorgente dell'utente è stato sovrascritto da questo audit.

| Avvio | Righe originali | Attribuzione utile, con limiti |
|---|---:|---|
| 1–5 | 1–5414 | Direct precedente; non assegnare un'etichetta P6 precisa senza manifest |
| 6 | 5415–7380 | White probe, coerente con P6.32 |
| 7–8 | 7381–9280 | Textured compat/depth, coerenti con P6.33/P6.34; fingerprint insufficiente a distinguerli da solo |
| 9–10 | 9281–14345 | Rescue storico, coerente con P6.35 e con le note; etichetta dedotta |
| 11 | 14346–16288 | Rescue + FFP prewarm + priority, coerente con P6.36 |
| 12 | 16289–18179 | 480×272 esplicito, coerente con P6.37 |
| 13 | 18180–19928 | 960×544, rescue ON, prewarm OFF, profiler ridotti: coerente con P6.38 |

## Misure dell'ultimo avvio

Le unità qui sono millisecondi, salvo indicazione diversa. Il file JSON mantiene i microsecondi originali. P95/P99 sono nearest-rank sui soli campioni disponibili; non rappresentano una distribuzione di gara completa.

| Metrica | N | Mediana | P95 | Intervallo min–max |
|---|---:|---:|---:|---:|
| Intervallo producer, campioni di gara | 40 | 1072,256 ms | 1103,962 ms | 1022,474–1113,589 ms |
| Render worker, serial deduplicati | 40 | 1013,535 ms | 1052,189 ms | 992,655–1054,061 ms |
| Attesa di disponibilità coda | 40 | 703,656 ms | 745,594 ms | 552,475–753,658 ms |
| Copia del packet | 40 | 6,227 ms | 6,713 ms | 5,784–6,974 ms |
| Draw logici nel packet | 40 | 5984 | 5988 | 5980–5988 |
| Vertici nel packet | 40 | 38124 | 38144 | 38108–38148 |
| Intervallo present, finestra consecutiva | 51 | 1066,778 ms | 1108,819 ms | 953,960–1111,092 ms |
| Chiamata swap nella stessa finestra | 51 | 0,274 ms | 0,318 ms | 0,140–0,332 ms |

La media dei vertici è 38126,5. Il rapporto è circa 6,37 vertici per draw logico. I 40 campioni producer sono ai serial 1248–1299, con buchi: il codice stampa i producer critici sopra un secondo e i periodici, quindi la selezione è **distorta verso i frame lenti**. `1 / mediana(producer)` dà circa 0,933, coerente con il dato delle note, ma il valore preferibile per la cadenza di uscita è **51 / 53,730298 = 0,949185 FPS**. I 51 present consecutivi sono i serial 1248–1298; i loro intervalli coprono il passaggio dal present 1247 al 1298.

L'ultimo `perf_summary` P6.38 è al serial 1200, ancora nel menu. **Non esiste nel run P6.38 un summary dettagliato della gara**: i 79 upload, 77 eviction, 6157 draw fisici e circa 249 ms EFB citati nelle note appartengono a un avvio precedente. Non vanno copiati nella colonna «P6.38 misurato».

### Significato dei timer: cosa non sommare

Verificato nel [renderer](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:7939): `producer_frame=N interval_us` misura l'intervallo tra gli ingressi in `SubmitFrame`. `queue_wait_us` e `packet_copy_us` sono invece misurati **dentro la chiamata corrente**, dopo quell'ingresso. Sottrarli riga per riga dall'intervallo non produce il tempo CPU guest del medesimo frame.

`worker=S/render_us/swap_us/age_us` fotografa l'ultimo worker completato; S può essere N−1 o N−2. Il quarto campo è l'età del completamento al momento della lettura, non una fase da aggiungere. Anche i `wait_service` possono includere callback eseguite mentre si aspetta il renderer. Il tempo trascorso non coincide con il tempo CPU del thread.

`render_us` va da `frameRenderBeginUs` a dopo lo swap. Comprende sincronizzazioni e chiamata swap, ma esclude la preparazione USER_2 e alcuni lavori del worker precedenti all'inizio del timer. `prep`, `render`, `queue_wait`, `efb_us`, `swap_us` non sono una lista di componenti indipendenti da sommare. In una pipeline la latenza dello stesso frame e la cadenza tra frame sono misure differenti; dipendenze e contesa possono ridurre la sovrapposizione.

## Audit delle conclusioni di Sol

### 1. Texture/materiali: indizio forte, attribuzione CPU ancora incompleta

I due record originali esistono:

| Record | Draw/vertici | Render | EFB incluso | Differenza render−EFB |
|---|---:|---:|---:|---:|
| Boot 6, serial 1260, riga 7368, white probe | 6157 / 38660 | 261,945 ms | 239,958 ms | 21,987 ms |
| Boot 9, serial 1200, riga 11264, rescue | 6157 / 38660 | 1119,235 ms | 249,169 ms | 870,066 ms |

La differenza dei residui è **848,079 ms**. È un ottimo motivo per indagare il percorso texture. Non è un A/B controllato sulla stessa build, stesso stato di cache e identico contenuto visivo: appartiene a due avvii e i timer EFB possono raccogliere il completamento della geometria precedente. I 21,987 ms non dimostrano che una gara corretta e completa possa essere renderizzata in 22 ms; inoltre escludono circa 46,8 ms di preparazione USER_2 nel campione white.

Nel record rescue storico: `texprep=8/71` significa 8 hit di texture già preparate e 71 decode sincroni; `texcache=25/79/79/0/14833744` significa hit/miss/upload/fallimenti/byte uploadati; `evict=77/0/14764112` significa eviction per budget/per tabella/byte espulsi. Il limite è 12 MiB e ci sono 256 slot. Quasi 14,8 MB caricati ed espulsi nello stesso frame dimostrano **churn di cache per budget in quel campione**. Non dimostrano da soli il working set univoco necessario: gli stessi pixel possono comparire più volte o con revisioni/varianti differenti.

Il codice possiede già texture persistenti, confronto per generazioni, protezione delle fonti frequenti e preparazione asincrona. Il lavoro richiesto è correggere i miss e il ciclo di vita, non «aggiungere una cache» da zero. [ResolveTexture e le politiche di eviction](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:4837) convertono sul render thread quando manca la preparazione. La chiave pixel include anche colori di bake per `directTevKind==2`; `ApplyDirectTevTextureBake` percorre i pixel. Questo è un candidato misurabile per spostare costanti materiali negli shader, senza invalidare il caching di contenuto.

`tev_draw=151/5793/0` classifica draw, inclusi quelli per cui lo stato viene riusato. **Non misura 5793 costose emulazioni TEV**. Nel rescue la resa viene forzata a REPLACE anche per materiali classificati fallback. Occorre separare decode, bake, verifica generazioni, lookup, upload, sampler, compilazione shader, submission e GPU wait prima di assegnare gli 848 ms al TEV.

### 2. EFB: troppo costoso nei campioni storici, già parzialmente residente

[direct_efb.inc](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/direct_efb.inc:1) ha già 128 entry, budget di 8 MiB e backing RGBA residente. A pari dimensioni usa `sceGxmTransferCopy` seguito da `sceGxmTransferFinish`; il fallback ridimensiona/copia su CPU. Per diversi formati esegue un passaggio CPU di selezione/quantizzazione dei canali. Il chiamante sincronizza con `glFinish` per confine GX e conserva i clear.

Il timer `directEfbUs` include il `glFinish` eseguito prima delle copie. Nella libreria locale collegata, `glFinish` chiude/resetta la scena e chiama `sceGxmFinish`: può aspettare draw precedenti. Anche il sorgente primario [vitaGL, gxm.c](https://github.com/Rinnegatamante/vitaGL/blob/master/source/gxm.c) espone tale percorso; per questa build fa fede la copia locale e il suo archivio hashato.

Quindi «240 ms EFB → tetto aritmetico 4,17 FPS se quel tempo restasse identico» è corretto come conto condizionale. «EFB costa autonomamente 240 ms, indipendentemente dai draw, e potrà essere tolto dal resto» non è provato. Servono misure separate di attesa fonte, allocazione, transfer, attesa transfer, resize, conversione, distruzione e clear.

Il campione rescue storico contiene anche **4 copie fallite su 13** (`efb=9/4`). Nel menu P6.38 serial 1200 sono **10 fallite su 12** (`efb=2/10`). Non è possibile chiamare corretto il rendering a partire dai soli contatori di draw. Identificare formato, dimensioni, destinazione e motivo di ciascun fallimento è parte della roadmap prestazionale: ripristinare operazioni oggi fallite potrebbe aumentare il costo.

### 3. Circa 6000 draw: problema plausibile, batching già presente

Il boot 9 registra 6157 draw fisici e zero merge. Nel P6.38 gara conosciamo i draw logici del packet, non il numero fisico. In menu P6.38 serial 1200 il valore verificato è 2031 fisici su 2038 logici.

Il [batcher attuale](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:4518) unisce draw adiacenti contigui con `renderStateId` uguale e confini EFB rispettati, ma concatena soltanto quads/triangles/lines/points: **strip e fan non entrano nel merge**. Il conteggio per topologia e motivo di interruzione è dunque una prima misura concreta. Non sappiamo ancora quanti dei 6000 siano realmente compatibili.

`renderStateId` dipende da primitive/rasterId/textureId e gli stati sono internati in sequenza. `prepStateRuns` viene incrementato nel ciclo su ciascun draw e non dimostra il numero minimo di batch unici. La costruzione dei suffissi può ripetere scansioni per draw compatibili: misurarla prima di proporre una passata lineare. Il batching deve conservare ordine, winding, triangolazione, stati osservabili e dipendenze; non basta raggruppare per texture.

6000 draw lascerebbero circa 5,56 µs ciascuno se tutto il budget del frame fosse loro. È un controllo di scala, non la prova che ciascuna chiamata GL costi i circa 169 µs ottenuti dividendo il tempo del worker per i draw: quel rapporto include texture, EFB, attese e altro lavoro.

### 4. Preparazione: un ulteriore limite e non soltanto uno stutter storico

Nel **P6.38 stesso**, serial 1244/1245: prep 180,032/185,019 ms; vertici 40,497/43,114 ms; texture 132,552/133,532 ms. Il codice permette al massimo **8 texture e 4 MiB per slot**. Ci sono 69/85 capacity skip, ma zero decode failure in questi due record. I contatori `failures` della preparazione includono skip di capacità/budget: non equivalgono a texture corrotte né a draw persi.

Sempre nel P6.38, serial 322/323: 370,563/374,012 ms in texture preparation, una texture completata e 13 budget skip. È lavoro temporizzato del worker, non una misura esclusiva del decoder di una texture, perché può includere sospensioni/contesa.

Il white storico ha circa 44,8 ms di sola preparazione vertici: anche quel percorso supera 33,3 ms. Spostare lavoro su USER_2 non lo rende gratuito; occorre arrivare al budget con la somma del lavoro dei servizi che condividono il core e misurare la coda. Evitare di limitare il piano a USER_1.

### 5. Transizione EFB e freeze: correlazione, con indicazione del producer

P6.38, riga 19782, producer 1211: intervallo 4255,742 ms, queue wait 0,002 ms, 512 comandi nel packet, 399 copy call, 303 copy registrate, 209 destroy registrati e 169 fallimenti di capacità. Il successivo present dello stesso serial ha intervallo **4951,680 ms**.

Nel record producer, però, `worker=1210/257/130/4255341`: è il frame precedente, già completato da circa 4,255 s, con 0,257 ms di rendering. Quindi quei 4,26 s **non sono una misura dell'esecuzione dei 512 comandi sul render worker**. Il producer ha trascorso molto tempo prima di pubblicare il packet, accumulando anche operazioni EFB. L'intervallo di present comprende il blocco producer e il lavoro successivo.

`efb_cap_fail` conta anche destroy rifiutati; 169 non significa necessariamente 169 copie perse. Esiste già coalescing dell'operazione immediatamente precedente in [ReserveEfbCommand](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:9267). La soluzione deve conservare clear, copie osservate, riuso indirizzi e completamento GPU. Alzare soltanto il limite maschera l'accumulo e può aggravare memoria e latenza.

### 6. Producer dei menu, salvataggio e I/O

P6.38 serial 1200: producer 168,543 ms, queue wait 3 µs, ultimo worker 26,606 ms e summary dello stesso serial 26,484 ms. Il periodo di produzione è circa 5,93 Hz; il renderer di quel menu ha già capacità nominale superiore a 30 Hz. Il tempo rimanente non è automaticamente CPU guest attiva: comprende scheduler, attese, callback, I/O e generazione GX.

La callback `0x80544A5C` da 2557,383 ms è nel **boot 12**, riga 18002. La [mappa simboli](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/projects/mkwii/MAP.txt:12130) la identifica come **`RKSYS::Mgr::SaveCoreTask`**, quindi un task di salvataggio, non una funzione grafica ordinaria. Separare lavoro CPU, attese e accessi NAND; non rimuovere o saltare il salvataggio per migliorare gli FPS.

P6.38 registra BRSAR prefetch da 1063,967 ms, 9.481.536 byte di cache e 10 MiB allocati. Il manifest attiva prefetch cooperativo, lane dedicata e Yaz0 fast-direct; mancano tempi P6.38 di decompress comparabili per attribuire un guadagno a Yaz0. Anche il prefetch cooperativo va verificato per contesa CPU/storage, non solo per la presenza del flag.

### 7. Audio, risoluzione e clock

Alla fine del P6.38: 8263 blocchi reali, 29625 silence/underrun. La frazione cumulativa di silence è **78,19% dell'intero avvio**, inclusi menu/loading. Nel delta tra le righe 19805 e 19928, vicino alla coda di gara, sono 6386 reali e 3854 silence, cioè **37,64%**; i confini non coincidono esattamente con la finestra dei present. Il clamp arriva a 77,583213 s cumulativi scartati, non tutti prodotti in gara. L'audio è già un criterio di giocabilità fallito, ma la sua causalità reciproca con guest/scheduler va misurata.

Il boot P6.37 a 480×272 **non contiene campioni di gara comparabili**: il massimo producer accettato è 2266 draw, contro circa 5984 della gara P6.38. Mostra menu ancora lenti; questo non esclude il costo GPU/fill-rate in gara. Ridurre la risoluzione non è la prima correzione, ma l'esclusione definitiva della GPU riportata nelle note non è sostenuta da questo allegato. Va fatto un confronto controllato in gara dopo aver misurato le attese.

Il clock riportato è 444/222/222/166 MHz dopo il fallimento del tentativo a 500 MHz. Un passaggio CPU ideale 444→500 darebbe +12,61% di throughput, ossia −11,2% di tempo sulla sola parte scalabile. Non risolve un divario di 31,6 volte. Non rendere i 500 MHz un prerequisito della roadmap.

## Stato reale da preservare

Il VPK P6.38 locale esiste, pesa 44.391.665 byte e ha SHA-256 `050448ccb4eabb29779f3fbc374d85e6ac544cf023f6a7fdff69a7868c40b32b`, coerente con il manifest. `unzip -tq` è passato in questo audit. L'archivio vitaGL collegato è `../aurora-vita-max-prehardware/third_party/vitaGL-speedhack-src/libvitaGL-m12_5-custom-heap.a`, SHA-256 `de04978951a09cdc28da1710face48051530be17358df7f50273d7a9a262600d`. Sono verifiche degli artefatti locali; non nuova prova hardware né una ricompilazione.

I file già modificati prima del report sono Makefile.vita, PORTING_STATUS.md, runtime/src/guest_hot_profiler.cpp, runtime/src/hle/egg_decomp.cpp, runtime/src/hle/storage/dvd.cpp, vita/gx_backend.cpp, vita/host_jobs.cpp, vita/include/wiicompiled_vita/host_jobs.h e vita/tools/build_performance_profile.py; CLAUDE.md era già non tracciato. Nessuno è stato modificato da questo audit.

La memoria storica indicava Aurora come riferimento dopo la regressione P6.2. Il checkout attuale e i profili recenti usano direct-vitaGL: mantenere questo percorso per il piano richiesto, e usare un eventuale vecchio riferimento solo se ricostruibile e confrontabile. Non ripristinare automaticamente backend o configurazioni vecchie. Il precedente rischio associato a FBO transitori/`glBlitFramebuffer` resta una ragione per isolare qualsiasi esperimento di quel tipo; non è una misura della causa attuale.

## Riproduzione e prossima decisione

Eseguire dalla root:

```sh
python3 report_performance/extract_evidence.py
```

Produce [metrics.json](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/metrics.json), [key-records.txt](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/key-records.txt) e [latest-boot.numbered.txt](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/latest-boot.numbered.txt). Le righe citate sono quelle originali, conservate nell'estratto numerato. Il parser ha selettori specifici per questo allegato: per log successivi vanno scelti nuovi boot/scena/serial, non riusata ciecamente la finestra 1248–1298.

La prossima build deve rispondere a una domanda operativa: **quanto del secondo del worker è decode/bake/upload texture, quanto attesa GPU precedente, quanto copia/conversione EFB e quanto submission/stato?** Servono anche il tempo CPU attivo del producer e il lavoro di USER_2. Il [piano](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/PIANO_SOL_5_6_HIGH.md) definisce le fasi e i criteri per proseguire; il [prompt di consegna](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/PROMPT_SOL_5_6_HIGH.md) avvia l'implementazione senza perdere queste distinzioni.
