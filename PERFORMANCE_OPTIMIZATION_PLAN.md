# Piano di ottimizzazione delle prestazioni

Data dell'analisi: 4 settembre 2026

Repository analizzati:

- `wiicompiled-vita`
- `aurora-vita-max-prehardware`, inclusa la variante `vitaGL-speedhack`
- `runtime.log` dell'ultima milestone M13.2

## 1. Obiettivo

Portare il rendering di Mario Kart Wii su PS Vita da una pipeline corretta ma fortemente CPU-bound a una pipeline più semplice, compatta e misurabile, senza introdurre una riscrittura generale che aumenti il rischio di regressioni.

La direzione consigliata è:

1. mantenere Aurora per shader, texture e stato grafico già validati;
2. sostituire il percorso generico usato dal bridge WiiCompiled con un percorso compatto specifico per Vita;
3. ridurre il lavoro eseguito per ogni draw GX prima di tentare una riscrittura diretta in GXM;
4. ottimizzare separatamente il producer WiiCompiled, soprattutto display list e UI;
5. validare ogni cambiamento su hardware reale con un solo parametro variato alla volta.

## 2. Stato misurato

Il log analizzato contiene più sessioni concatenate. L'ultima sessione inizia alla riga 119137 ed è una build diagnostica M13.2 con:

```text
renderer=aurora
vitagl=speedhack-custom-heap
efb_gpu_blit=0
movies_disabled=1
native_thp=0
lyt_direct=0
lyt_faithful=1
dl_indexed_raw=1
perf_skip_efb=1
perf_skip_billboards=1
perf_skip_lighttexture=1
perf_force_3d_solid=1
```

Questi dati sono quindi un limite inferiore diagnostico: EFB, billboard, light texture, filmati e THP nativo non rappresentano ancora la configurazione completa finale.

### 2.1 Display list indicizzate

La correzione M13.2 funziona sul percorso osservato:

- circa 3062 draw raw su 3062, 21756 vertici, nessun fallimento nel frame rappresentativo;
- `dl_us` circa 141-143 ms;
- la M13.1 impiegava circa 246 ms sullo stesso carico e falliva tutte le 3062 draw;
- il guadagno misurato è circa 104 ms, ossia circa il 42%, ma il decoder resta troppo costoso.

Alcune righe successive riportano `raw_decode_fail reason=capacity(2)`. Vanno analizzate separatamente: non dimostrano da sole una regressione del percorso rappresentativo, i cui contatori riportano `raw_fail=0` e `raw_cap=0`.

### 2.2 Costo del renderer

Campioni significativi dell'ultima sessione:

| Frame seriale | Draw GX logiche | Vertici | Render Aurora | Submit | End frame | Swap | Draw fisiche |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 600 | 49 | 196 | 1,9 ms | 1,4 ms | n/d | n/d | 14 |
| 720 | 665 | 2660 | 22,8 ms | 18,6 ms | 3,9 ms | 0,2 ms | 110 |
| 840 | 3356 | 22932 | 329,0 ms | 293,4 ms | 21,1 ms | 7,1 ms | 54 |
| 1080 | 4138 | n/d | 401,3 ms | 361,3 ms | 25,6 ms | 7,1 ms | n/d |
| 1440 | 6048 | 38430 | 444,5 ms | 407,6 ms | 27,3 ms | 4,7 ms | 32 |
| 1560 | 6622 | n/d | 613,4 ms | 567,0 ms | 32,1 ms | 7,1 ms | n/d |
| 1800 | 5173 | n/d | 490,7 ms | 450,6 ms | 27,3 ms | 6,3 ms | 31 |

Il dato decisivo è la differenza tra draw logiche e draw fisiche. Aurora riesce già a raggruppare migliaia di draw in poche decine di draw GPU, ma paga comunque il costo di preparazione per ogni draw GX. Il tempo è quasi interamente in `submit_us`, non in `swap` e non nell'esecuzione finale delle draw.

Conclusione: il collo di bottiglia principale del renderer è CPU-side, nel bridge WiiCompiled/Aurora. Non ci sono prove che la rasterizzazione della GPU Vita sia il limite dominante di questi frame.

### 2.3 UI e producer WiiCompiled

Nel frame seriale 720:

- 665 draw logiche e 2660 vertici non costituiscono un carico elevato per la GPU;
- il renderer impiega circa 22,8 ms;
- il producer impiega circa 153 ms;
- `lyt_us` è circa 7,5 ms per 138 chiamate;
- il fast path dei glifi è già attivo, con 526 glifi.

Quindi la lentezza della UI non è spiegata principalmente da vitaGL. Gran parte del tempo è nel codice guest ricompilato, nelle transizioni/HLE e nella generazione dei comandi GX.

Nei frame più pesanti il producer arriva a circa 0,8-1,0 secondi, mentre il renderer richiede circa 0,35-0,61 secondi. Esistono inoltre stall guest di più secondi con attesa renderer trascurabile. Questi stall non possono essere attribuiti ad Aurora.

### 2.4 Logging

La sessione M13.2 contiene, tra gli altri:

- 37967 righe `render_large phase=draw_progress`;
- 3048 avvisi `TEV register out of range`;
- 191 righe `gx_begin_hot`;
- 105 righe `guest_watchdog_stall`.

Il log è aperto in append e stdout/stderr sono line-buffered. Ogni newline nel percorso caldo può quindi causare I/O sincrono verso la memory card. La telemetria corrente altera la misura e può amplificare gli stall. La sua riduzione è la prima ottimizzazione da verificare, non perché risolva tutta la lentezza, ma perché è economica, a basso rischio e necessaria per ottenere dati affidabili.

## 3. Cause strutturali

### 3.1 Espansione e copie dei vertici

Il percorso attuale espande un vertice compatto del bridge in `CanonicalVertex`, una struttura generica molto più grande. Il percorso WiiCompiled usa essenzialmente:

- posizione;
- colore RGBA;
- una coordinata texture.

`CanonicalVertex` conserva invece anche normali, binormali, tangenti, un secondo colore, otto set di coordinate texture e indici matrice. I byte riportati dal log corrispondono a circa 168 byte per vertice, contro i 24 byte del vertice di origine.

Il percorso effettivo esegue più copie:

```text
RenderVertex compatto
    -> conversione per campo in CanonicalVertex
    -> vettore temporaneo
    -> staging buffer CPU
    -> buffer VBO mappato da vitaGL
```

La variante `vitaGL-speedhack` consente già di mappare direttamente il buffer dinamico. Il bridge non sfrutta questa possibilità perché riempie prima uno staging intermedio e poi ne esegue il `memcpy`.

### 3.2 Lavoro ripetuto per ogni draw logica

Per ogni draw GX il renderer attuale esegue ancora:

- costruzione degli indici;
- resize e conversione dei vertici;
- costruzione di una descrizione completa della pipeline;
- calcolo della chiave della pipeline;
- lookup della texture;
- costruzione degli uniform;
- upload di vertici e indici.

Il batching avviene dopo questo lavoro. Riduce le draw fisiche, ma non elimina il costo CPU delle migliaia di draw logiche.

### 3.3 Pacchetto frame sovradimensionato

Ogni `GeometryDraw` copia la proiezione e l'intera palette di dieci matrici di posizione. Molti draw consecutivi condividono lo stesso stato, ma il pacchetto lo duplica.

Il `FramePacket` può raggiungere circa 7,5 MiB. Nei frame pesanti la sola copia del pacchetto costa circa 13-27 ms. Inoltre esiste un solo slot pendente: quando producer e renderer si incontrano, l'attesa li serializza invece di sovrapporli efficacemente.

### 3.4 Display list decodificate di nuovo

La cache corrente evita parte delle scansioni e degli hash, ma non conserva una rappresentazione completamente decodificata dei vertici e dei comandi. Le display list statiche vengono quindi ripercorse e decodificate a ogni frame. Il percorso raw corretto resta a 141-175 ms nei carichi osservati.

### 3.5 Ottimizzazione dei binari ricompilati

I frammenti ARM32 generati sono compilati principalmente con `-Os`, una scelta utile per la dimensione del VELF e il limite del loader, ma non ideale per tutte le funzioni calde. Applicare `-O3` indiscriminatamente aumenterebbe dimensione, pressione sulla cache istruzioni e rischio di superare i limiti di packaging. Serve un profilo selettivo per i soli shard o helper realmente caldi.

## 4. Architettura consigliata

Non conviene sostituire subito Aurora con un renderer completamente nuovo. Conviene restringere il suo ruolo:

```text
WiiCompiled / GX HLE
    -> CompactDraw + ID di stato
    -> VitaRenderQueue a due slot
    -> batch già preparati
    -> Aurora: shader, texture, pipeline e presentazione
    -> vitaGL-speedhack
```

Il nuovo percorso specifico per il gioco dovrebbe vivere sotto `vita/` e avere queste proprietà:

- vertice compatto da 24 o 32 byte;
- range contigui di vertici e indici per frame;
- tabelle immutabili di trasformazioni, materiali, raster state e texture;
- ID piccoli nei draw al posto di copie complete dello stato;
- batching di draw adiacenti compatibili prima dell'upload;
- lookup di texture e pipeline una sola volta per cambio di stato;
- fallback al percorso attuale controllato da flag di build.

Aurora continuerebbe a fornire le parti già funzionanti, ma riceverebbe batch compatti già pronti invece di ricostruirli uno alla volta.

## 5. Piano di lavoro

### Fase 0 - Rendere affidabili le misure

Priorità: immediata.

Interventi:

1. Separare una build `diagnostic` da una build `performance`.
2. Disabilitare nella build performance:
   - progress log ogni 128 draw;
   - warning TEV ripetuti;
   - dump per-draw e per-GXBegin;
   - log watchdog duplicati dello stesso indirizzo.
3. Conservare contatori in memoria e produrre una sola riga ogni 300 frame, oppure al primo errore nuovo.
4. Usare un ring buffer fisso per gli ultimi eventi critici e scaricarlo solo in caso di crash o richiesta esplicita.
5. Aggiungere timer aggregati per:
   - producer guest non-GX;
   - decoder display list;
   - UI/LYT;
   - copia del pacchetto e attesa del renderer;
   - trasformazione dei vertici;
   - risoluzione texture;
   - calcolo/lookup pipeline;
   - packing dei vertici;
   - upload;
   - `vglEndScene` e swap.

Flag proposti:

```make
MKW_VITA_PERF_LOG=0|1
MKW_VITA_PERF_RING=0|1
MKW_VITA_PERF_SUMMARY_INTERVAL=300
```

Criteri di uscita:

- stesso percorso eseguito su hardware con log vecchio e log ridotto;
- nessuna differenza visiva;
- nessun overflow/drop aggiuntivo;
- misure stabili su almeno 300 frame dopo il warm-up;
- differenza misurata di producer, submit e tempo totale.

### Fase 1 - Fast path compatto del renderer

Priorità: massima. È il lavoro con il rapporto beneficio/rischio più favorevole dopo il logging.

Interventi:

1. Introdurre un ingresso frame-wide, per esempio `AuroraPacketRendererSubmitFrame`, invece di una chiamata completa per ogni draw.
2. Definire un `VitaPacketVertex` compatto contenente soltanto posizione, RGBA8 e UV.
3. Aggiungere in Aurora una vertex layout specifica per questo formato, senza conversione in `CanonicalVertex`.
4. Calcolare prima i batch adiacenti compatibili usando un `material_state_id` stabile.
5. Risolvere pipeline e texture una sola volta per batch o cambio di stato.
6. Costruire gli indici direttamente nel buffer finale:
   - pattern precomputato per quad frequenti;
   - copia diretta per triangoli già espansi;
   - eliminazione dei vettori temporanei per-draw.
7. Mappare una volta VBO/IBO per frame e scrivere direttamente nei range assegnati, evitando lo staging duplicato.
8. Conservare il percorso `CanonicalVertex` come fallback per gli attributi non supportati dal fast path.

Flag proposti:

```make
MKW_VITA_COMPACT_VERTEX=0|1
MKW_VITA_FRAME_BATCHER=0|1
MKW_VITA_DIRECT_STREAM_WRITE=0|1
```

Obiettivi misurabili iniziali:

- frame UI seriale 720: `submit_us` da 18,6 ms a meno di 5 ms;
- frame gara simile al seriale 1440: `submit_us` da 407 ms a meno di 80 ms, con obiettivo successivo sotto 50 ms;
- nessun aumento delle draw fisiche;
- stessi pixel per testo, blending, scissor e TEV nei casi supportati;
- fallback esplicito e contato, mai silenzioso.

### Fase 2 - Stato compatto e coda producer/renderer

Priorità: alta, dopo il fast path.

Interventi:

1. Sostituire la copia per-draw della proiezione e delle dieci matrici con tabelle per frame.
2. Assegnare ID generazionali a:
   - proiezione;
   - matrice di posizione;
   - materiale/TEV;
   - raster state;
   - texture binding;
   - viewport e scissor.
3. Memorizzare in `GeometryDraw` solo range e ID, preferibilmente a 16 o 32 bit.
4. Deduplicare lo stato solo quando cambia, senza hash completo per ogni draw.
5. Ridurre la dimensione massima del pacchetto e misurare separatamente i byte realmente copiati.
6. Dopo aver ridotto il pacchetto, introdurre una coda SPSC a due slot tra USER_0 e USER_1.
7. Forzare un punto di sincronizzazione per operazioni con dipendenze CPU/GPU visibili:
   - letture EFB;
   - copie che alimentano immediatamente il guest;
   - presentazione/XFB;
   - cambi di risorsa che richiedono ownership esclusiva.

La profondità deve restare due per evitare latenza e memoria eccessive. Non va introdotto multithreading generico nello stato guest.

Obiettivi:

- copia pacchetto pesante da 13-27 ms a meno di 2 ms;
- nessuna perdita o riordinamento dei frame;
- throughput vicino al massimo tra tempo producer e renderer, invece della loro somma nei punti di contesa;
- assenza di race in EFB, texture cache e presentazione.

### Fase 3 - Producer WiiCompiled e display list

Priorità: alta. È indispensabile per rendere fluide UI e gameplay anche dopo aver accelerato il renderer.

#### 3A. Cache delle display list decodificate

Creare una cache di template decodificati, indicizzata almeno da:

- indirizzo e dimensione della display list;
- VCD/VAT;
- identità e stride degli array;
- generazione delle pagine guest coinvolte;
- stato che modifica il significato dei vertici.

Il template deve conservare vertici in spazio oggetto e comandi essenziali, non coordinate già trasformate. L'invalidazione deve usare il tracciamento delle scritture guest; in caso di dubbio deve ricadere nel decoder normale.

Ottimizzazioni successive:

- decoder bulk per attributi omogenei;
- byte-swap e unpack in blocchi;
- NEON solo sui loop misurati e semanticamente equivalenti;
- eliminazione di callback e allocazioni per vertice;
- aggregazione di più primitive compatibili prima di inviarle al backend.

Obiettivi:

- mantenere `raw_fail=0` nel caso M13.2 rappresentativo;
- portare `dl_us` da 141-175 ms sotto 60 ms, poi sotto 40 ms;
- contatore esplicito di cache hit, miss, invalidazione e fallback;
- test di una display list modificata a runtime per verificare l'invalidazione.

#### 3B. UI fedele ma raggruppata

Il fast path dei glifi è già efficace; non è prioritario riscrivere indiscriminatamente `GlyphDrawer`.

Conviene invece introdurre un writer di quad UI che:

- mantenga il percorso LYT fedele;
- raggruppi solo draw adiacenti con materiale, texture, scissor e blending identici;
- usi lo stesso ordine dei vertici già validato;
- ricada immediatamente nel percorso fedele per casi non equivalenti;
- non riattivi il vecchio percorso LYT diretto finché le sue regressioni visive non sono risolte.

Obiettivo iniziale: portare il producer della UI rappresentativa da circa 153 ms sotto 50 ms, conservando testo leggibile e composizione corretta.

#### 3C. Codice ARM32 ricompilato

Usare il profiler a basso overhead per identificare gli shard e gli helper caldi. Poi:

1. applicare `-O2` o `-O3` soltanto a una lista esplicita di oggetti;
2. valutare `-ffast-math` solo sulle funzioni prive di dipendenze dalla semantica floating-point PPC;
3. inlining mirato degli helper PPC più frequenti;
4. peephole per byte-swap, indirizzamento, matrici e chiamate indirette note;
5. confrontare dimensione di ELF/SELF, memoria e cache istruzioni per ogni profilo.

Funzioni osservate come candidate al profiling approfondito includono `RFLiInitShapeRes`, `RFLiDrawQuad`, `ColorFader`, `Picture::DrawSelf` e `GlyphDrawer`. I tempi aggregati attuali includono anche intervalli tra chiamate e non autorizzano ancora una sostituzione HLE completa.

Una HLE nativa va considerata solo per una funzione con contratto di stato noto, input/output riproducibili e beneficio dimostrato. Non va sostituito un intero sottosistema sulla base del solo indirizzo dell'ultimo watchdog.

### Fase 4 - Texture, EFB e contenuti multimediali

Priorità: media, dopo aver stabilizzato il percorso compatto.

Interventi:

1. Dare al bridge handle stabili per texture e pipeline, evitando hash e lookup ripetuti.
2. Separare cache persistente per UI/font da risorse transitorie di gara, EFB e THP.
3. Usare formati nativi Vita quando supportati, evitando RGBA8 intermedio non necessario.
4. Spostare su un worker soltanto lavori puri e isolati, come una conversione texture senza accesso allo stato guest.
5. Reintrodurre EFB, light texture, billboard, filmati e THP nativo uno alla volta.
6. Per EFB, preferire GPU copy e readback ritardato soltanto dopo aver definito le dipendenze; non ripristinare il vecchio percorso FBO transitorio che ha causato crash.
7. Misurare hit, miss, eviction, allocazioni e memoria libera prima di aumentare il budget texture.

Il contatore di allocazioni fallite presente nel log può includere storia dell'intera sessione e la build forza il 3D solido. Non va usato da solo per aumentare la cache: la memoria Vita è già fragile e serve una misura per-frame pulita.

### Fase 5 - vitaGL e possibile percorso GXM diretto

Priorità: condizionata ai risultati delle fasi precedenti.

La riscrittura diretta in GXM non è il primo intervento raccomandato, perché:

- le draw fisiche sono già poche;
- `swap` costa pochi millisecondi;
- il costo enorme avviene prima dell'esecuzione delle draw;
- una riscrittura completa duplicherebbe gestione di shader, texture, blending, lifetime e sincronizzazione.

Dopo le fasi 1-4:

1. misurare tempo CPU dentro vitaGL e tempo GPU con gli strumenti disponibili;
2. confrontare 960x544 con 720x408 o 640x360 solo se la GPU risulta realmente satura;
3. provare un percorso GXM diretto soltanto per i batch compatti e stabili;
4. mantenere vitaGL per contesto, scene e risorse durante il primo A/B;
5. definire ownership esclusiva di buffer, shader e sincronizzazione prima di mescolare comandi vitaGL/GXM.

Se, dopo il compact batching, `submit` scende sotto il budget ma `vglEndScene` o la GPU restano dominanti, il percorso GXM diventa giustificato. Prima di quel punto sarebbe una riscrittura costosa senza prova del beneficio principale.

### Fase 6 - Ripristino delle funzionalità e stabilizzazione

Ordine suggerito:

1. build performance senza logging caldo;
2. renderer compatto con colore solido diagnostico;
3. texture e TEV;
4. billboard e light texture;
5. EFB;
6. filmati e THP nativo;
7. configurazione fedele completa.

Per ogni passo verificare su hardware reale:

- menu e testo;
- selezione modalità e classe;
- caricamento della gara;
- modelli 3D e texture;
- HUD e trasparenze;
- input e audio;
- transizioni, pause e ritorno ai menu;
- memoria dopo 15-30 minuti;
- assenza di overflow, draw scartate e fallback inattesi.

## 6. Milestone e criteri di accettazione

### P0 - Misura pulita

- log caldo eliminato;
- report aggregato ripetibile;
- VPK e log hardware con hash identificabile;
- baseline aggiornata per UI, selezione classe e gara.

### P1 - Submit compatto

- niente `CanonicalVertex` nel percorso comune;
- niente staging duplicato;
- batch calcolati prima del packing;
- submit pesante ridotto almeno del 70% senza regressioni visive.

### P2 - Pacchetto compatto e overlap

- copia pacchetto sotto 2 ms nei frame pesanti;
- due frame slot stabili;
- nessuna dipendenza EFB/XFB violata;
- attese producer/renderer spiegate dai contatori.

### P3 - Producer accelerato

- display list statiche servite dalla cache decodificata;
- decoder raw sotto 60 ms nel caso di riferimento;
- UI interattiva sotto 50 ms di producer;
- nessuna modifica non invalidata delle display list.

### P4 - Build fedele completa

- tutte le esclusioni diagnostiche rimosse;
- modelli, texture, HUD, EFB e filmati corretti;
- nessun peggioramento di memoria nel test prolungato;
- misure separate di USER_0, USER_1 e GPU.

Il budget finale per 30 fps è 33,3 ms complessivi. Come obiettivo architetturale, USER_0 dovrebbe restare entro circa 20 ms e USER_1 entro circa 12 ms, con copia del pacchetto sotto 1 ms. Non è una promessa per la prima iterazione: il traguardo intermedio realistico è UI sotto 50 ms e frame gara pesanti sotto 100 ms, poi una seconda fase verso 30 fps.

## 7. Metodo di benchmark

Ogni A/B deve variare un solo componente.

Procedura:

1. cancellare o rinominare il vecchio `runtime.log`, perché il file è append-only;
2. usare lo stesso percorso di gioco e gli stessi punti di misura;
3. ignorare il warm-up iniziale e acquisire almeno 300 frame stabili;
4. produrre VPK con nome univoco, flag inclusi e SHA-256;
5. conservare anche ELF/SELF e configurazione di build;
6. registrare mediana, p95 e massimo, non soltanto un singolo frame;
7. confrontare contatori di draw, vertici, fallback, overflow, cache e memoria;
8. annotare separatamente correttezza visiva e prestazioni.

Tabella da compilare per ogni prova:

| Build | Scenario | Producer p50/p95 | DL p50/p95 | Submit p50/p95 | End frame p50/p95 | Swap p50/p95 | Draw logiche/fisiche | Memoria libera | Esito visivo |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| baseline | UI | | | | | | | | |
| variante | UI | | | | | | | | |
| baseline | gara | | | | | | | | |
| variante | gara | | | | | | | | |

## 8. Cosa non fare ora

- Non riscrivere subito l'intero renderer Aurora in GXM.
- Non importare il percorso GX generico di Aurora se il bridge compatto copre già il caso WiiCompiled.
- Non aumentare indiscriminatamente thread e code attorno allo stato guest condiviso.
- Non applicare `-O3` a tutti gli shard senza misurare dimensione e semantica.
- Non ridurre la risoluzione prima di provare che la GPU è il limite.
- Non aumentare il budget texture sulla base di contatori cumulativi sporchi.
- Non riattivare insieme EFB, THP, texture e billboard: impedirebbe di attribuire costi e regressioni.
- Non giudicare la correttezza dal solo build, dal packaging o da Vita3K; il riferimento resta l'hardware reale.

## 9. Ordine operativo raccomandato

1. Riduzione del logging e baseline pulita su hardware.
2. Profilazione interna di `submit` per separare conversione, indici, hash, texture e upload.
3. Vertice compatto e scrittura diretta nel buffer mappato.
4. Batching frame-wide e cache degli ID di stato.
5. Pacchetto compatto con tabelle di stato.
6. Coda SPSC a due slot e sincronizzazioni esplicite.
7. Cache delle display list completamente decodificate.
8. Batching fedele dei quad UI.
9. Ottimizzazione selettiva degli shard ARM32 e degli helper del recompiler.
10. Ripristino controllato delle funzionalità disabilitate.
11. Profilazione GPU e solo allora A/B vitaGL contro un percorso GXM ristretto.

## 10. Decisione tecnica

Sì, il progetto può ottenere prestazioni molto migliori senza aggiungere necessariamente più complessità globale. La semplificazione più utile consiste nell'eliminare le rappresentazioni generiche e il lavoro per-draw dal percorso WiiCompiled, non nel sostituire subito tutta Aurora.

La priorità è ridurre:

1. logging sincrono nel percorso caldo;
2. espansione `RenderVertex -> CanonicalVertex`;
3. copie di buffer e stato;
4. hash e lookup ripetuti per ogni draw;
5. ridecodifica delle display list;
6. overhead del codice guest nelle routine UI e RFL misurate.

Una volta completati questi interventi, i dati diranno se vitaGL rimane un limite. Solo in quel caso una sottile integrazione VitaSDK/GXM per la submission dei batch sarebbe tecnicamente giustificata.
