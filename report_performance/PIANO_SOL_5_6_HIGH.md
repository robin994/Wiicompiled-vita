# Piano operativo — Mario Kart Wii giocabile a 30 FPS su PS Vita

Versione 1.0, 11 settembre 2026. Esecutore previsto: Sol 5.6 high.
Base: audit P6.38 in [REPORT.md](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/REPORT.md).
Stato: F00 audit offline completato; preparazione benchmark hardware e F01 da implementare. Tutte le successive fasi sono aperte.

## Obiettivo e regole di avanzamento

L'obiettivo è una gara single-player completa a **30 immagini nuove/s, velocità di gioco corretta, controlli responsivi, audio continuo e grafica funzionale**. La prima pista di benchmark va identificata sul dispositivo; dal log non si deve inferire una pista certa. Dopo il primo risultato ripetere su piste/effetti più pesanti e sulle transizioni. Nessuna stima di questo piano garantisce che la PS Vita raggiungerà il risultato.

Un solo intervento causale per build di confronto, con flag o profilo separato e VPK distinto. Non aggiungere contemporaneamente cache, shader, batching e sincronizzazione per poi attribuire il risultato a uno solo. Mantenere il miglior profilo verificato, inclusi i miglioramenti recenti del worktree. Una fase può richiedere più build e più turni; non saltare la prova hardware e non dichiararla completata perché il pacchetto è valido.

Se i dati della F01 cambiano il collo di bottiglia, aggiornare l'ordine delle fasi, motivandolo con serial, tempi e hash. Le dipendenze funzionali restano: niente riordino dei draw prima di conoscere le dipendenze; niente rimozione di fence senza lifetime corretto; niente benchmark finale con rescue o effetti mancanti. Gli obiettivi numerici sotto sono **criteri proposti di lavoro**, non guadagni previsti o tempi già raggiunti.

Non sono richiesti ora commit, push, tag, release o reset. Leggere le istruzioni applicabili e lo stato Git, preservare file sporchi e dipendenze. Restare sul direct-vitaGL attuale; un backend alternativo richiederebbe una proposta separata sostenuta da misure, non una sostituzione automatica basata su vecchi report.

## Budget e milestone

Oggi: 0,949 present/s osservati, worker mediano 1013,535 ms. Per 30 FPS occorrono 33,333 ms tra immagini nuove, con margine e senza crescita delle code. Una riduzione del worker non basta se producer o USER_2 diventano più lenti.

| Budget finale proposto | Obiettivo indicativo | Come interpretarlo |
|---|---:|---|
| Producer utile, con semantica guest verificata | ≤25 ms per immagine finale | Se la simulazione richiede due tick per immagine, includere entrambi; misurare anche il budget per tick |
| Prep CPU totale USER_2 per immagine | ≤10–12 ms | Comprende i job necessari; tenere conto di I/O/THP concorrenti sullo stesso core |
| Percorso render prima del pacing | ≤28 ms | Somma esclusiva e attese effettivamente sul percorso critico, senza doppi conteggi |
| EFB incrementale nel budget renderer | obiettivo finale ≤4–6 ms | Separare attesa della GPU precedente da transfer/resize/conversioni |
| Lookup/decode/bake/upload texture caldo | obiettivo ≤2–3 ms | Solo contenuto statico invariato; texture dinamiche misurate a parte |
| Packet copy/pubblicazione | obiettivo ≤1–2 ms | Oggi ~6,2 ms; migliorare ownership senza perdere isolamento |
| Margine render | ~5 ms | Rispetto al deadline 33,333 ms; non equivale a garantire FPS |

I budget di thread diversi non si sommano come tempi seriali, ma la loro contesa e le dipendenze vanno comprese. Il limite utile è l'intero periodo della pipeline misurato; `max(producer, prep, render)` è soltanto una prima approssimazione quando lavorano realmente in parallelo.

| Milestone di uscita | Criterio cumulativo proposto, stessa scena | Uso |
|---|---|---|
| G0 | Baseline ripetibile, tempi attribuiti, errori censiti | Evitare ottimizzazioni speculative |
| G1 | ≤200 ms per immagine, nessun overflow/errori nuovi | Primo salto verso ≥5 FPS |
| G2 | ≤66,7 ms per immagine, resa funzionale | Soglia ~15 FPS; rivalutare fattibilità del resto |
| G3 | ≤41,7 ms per immagine, simulazione a velocità corretta | Soglia ~24 FPS |
| G4 | Criteri F10 per 30 FPS superati su hardware | Obiettivo soddisfatto per la matrice testata |

Ogni milestone usa percentili e distribuzione oltre alla media; una scena vuota o il white probe non può superare G2–G4 al posto del gioco completo.

## F00 — Congelare la baseline e definire il benchmark

**Stato:** audit offline fatto. Mancano cattura visiva, identificazione scenario e ripetizioni hardware.

1. Leggere report, manifest, `provenance.json` e stato Git. Verificare che i file non siano cambiati rispetto agli hash; se sono cambiati aggiornare la base senza sovrascrivere il lavoro. Conservare il VPK P6.38 con nome/hash originale.
2. Definire una sequenza ripetibile: boot → selezione personaggio/kart → ingresso pista → countdown → 60 s di gara iniziale → giro completo → uscita/menu → reingresso. Registrare pista, modalità, avversari, camera, save/config, input e versione degli asset. Finché si è intorno a 1 FPS, la prima finestra può essere 60–120 s di tempo host; non spacciarla per 60 s di simulazione.
3. Raccogliere almeno tre ripetizioni per A/B, distinguendo cache fredde e calde. Usare lo stesso clock verificato, condizioni termiche simili e nessun cambiamento di contenuto. La sequenza va svolta su Vita reale; se serve l'interazione dell'utente, fornire VPK e istruzioni concrete e segnare «in attesa di hardware».
4. Separare fin da ora present, immagini realmente nuove, tick guest e tempo di gara. Il flag `render_target_hz=30` è già ON: non è una nuova ottimizzazione.
5. Aggiungere nella build diagnostica identificativo univoco di build/config e fonti (hash nel manifest e marker corrispondente nel log). Chiavi minime dei record: boot/run ID, scenario ID, frame serial, stage/thread, timestamp.

**Accettazione:** stessa scena riconoscibile, stessi flag e contenuti, nessun mix tra avvii, baseline numerica e visiva ripetibile. I 30 FPS restano «non verificati».

**Consegna:** scenario scritto, log originali, screenshot/video o riscontro visivo documentato, VPK/ELF/manifest e hash, aggiornamento `STATO_E_CONTINUAZIONE.md`.

## F01 — Profilo del percorso critico, esclusivo e a basso overhead

**Prerequisito:** F00. Prima implementazione da assegnare a Sol. Nessuna modifica alla resa, agli algoritmi o ai limiti dei buffer in questa fase.

**File principali:** [gx_backend.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp), [direct_efb.inc](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/direct_efb.inc), [host_jobs.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/host_jobs.cpp), [build_performance_profile.py](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/tools/build_performance_profile.py), [Makefile.vita](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/Makefile.vita). Aprire il logger esistente prima di aggiungere un altro canale di logging.

**Implementazione:**

- Nuovo profilo derivato da `full-content-p6_38-producer-io-yaz0`, con nome univoco scelto dopo aver verificato i profili presenti. Evitare di occupare automaticamente P6.39 se è già usato. Due varianti uguali salvo telemetria ON/OFF.
- Timestamp coerenti per ingresso SubmitFrame, queue begin/end, packet ready, prep begin/end, render begin/end, submit GPU e completamento effettivo. Registrare il serial di ciascun dato; separare gli intervalli tra ingressi da quelli tra pubblicazioni.
- Tempi CPU per thread se la runtime/API disponibile lo permette, insieme al wall time; distinguere esecuzione, sleep, mutex, callback di servizio e attese GPU. Non usare `worker_age` come tempo di rendering o sottrarre il queue wait della riga corrente dal producer interval.
- Renderer: lookup e validazione fonte texture; decode; bake; allocazione; upload; sampler/material state; shader cold creation; draw submission; attese GXM. Distinguere porzioni esclusive e timer inclusivi. Includere anche bookkeeping fuori da `frameRenderBeginUs`.
- EFB: motivo di fallimento, tipo/formato/dimensioni, destinazione+generazione, boundary, source wait, destination wait, alloc, transfer submit/finish, CPU resize, conversione canali, destroy, clear. Identificare se il wait sta drenando geometria precedente; non inserire `glFinish` per ogni draw soltanto per misurare.
- Texture: miss reason separati (mai vista, eviction budget, slot, generazione cambiata, bake variante, fonte non più valida), byte unici richiesti nel frame e in una finestra, decoded bytes, resident bytes e memoria ancora trattenuta da risorse in volo. Rendere osservabili `sourceRaceDraws`, fallback bianchi e formati non supportati.
- Draw: istogramma per topologia, distribuzione vertici/draw, draw fisici, candidati di merge, motivi che rompono un batch e numero effettivo di run. Correggere/precisare la semantica di `prepStateRuns` senza cambiare il batcher.
- Prep: tempi decode/riuso per formato, costo delle scansioni/ranking, job queue delay e pressione di DVD/THP/prefetch su USER_2. Producer: top PC campionati o zone misurate, distinguendo SaveCoreTask e I/O dal lavoro di gara.
- Usare contatori locali e aggregati ogni ~1 s host; burst dettagliato limitato su frame lenti e cambio scena. A 1 FPS non attendere 120 frame per ottenere un summary. Scrivere record completi per evitare nuove righe intercalate; ring/flush devono avere memoria limitata e contatori di perdita.

**Accettazione:** ogni millisecondo grande è attribuito o marcato «non attribuito»; obiettivo residuo non attribuito <10% del worker; almeno 30 frame di gara utili o una finestra completa equivalente. Overhead ON/OFF proposto ≤3% sullo stesso scenario, verificato con ripetizioni; se la misura è rumorosa usare finestre più lunghe. Tutte le copie EFB fallite hanno una categoria. Nessun nuovo crash o cambiamento visivo.

**Decisione:** ordinare le F02–F07 in base ai tempi esclusivi e al percorso critico. Se il grosso è GPU wait, investigare prima draw/effetti GPU; se è decode/upload, iniziare dalla F02. Non affermare «CPU-only» dalla sola durata di `glFinish`.

## F02 — Eliminare decode/upload ripetuti di contenuto invariato

**Prerequisito:** F01 conferma miss/decode/upload rilevanti. Questa fase deve essere utile mantenendo la resa attuale costante; non è ancora il completamento dei materiali.

**Punti del codice:** `SameTexturePixelSource`, `TextureSourceStillMatches`, `ResolveTexture`, `OldestValidTextureEntry`, `PrepareFrameCpuTextures`, `FindPreparedTextureCpu` in gx_backend.cpp; decoder in [direct_texture_decode.h](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/include/wiicompiled_vita/direct_texture_decode.h); [gx_guest_write_hooks.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/gx/gx_guest_write_hooks.cpp).

**F02a — Identità e riuso:** separare contenuto pixel, variante decodificata/baked e stato sampler/materiale. La chiave deve includere tutti gli input che cambiano davvero i pixel, comprese palette/plane/generazioni quando applicabili. Non togliere i colori di bake dalla chiave se il bake resta attivo. Verificare generazioni per fonte univoca nel periodo consentito dalla ownership, senza disabilitare i controlli sulle scritture guest. Le texture EFB usano il loro percorso di lifetime.

**F02b — Cache e preparazione:** costruire una lista di richieste uniche per frame, confrontarla con contenuti già pronti, decodificare solo miss utili. Valutare un riuso CPU tra slot basato su ownership immutabile; oggi 8 texture/4 MiB per slot non coprono tutti i miss. Incrementare i limiti solo dopo inventario memoria. I puntatori guest letti da worker devono restare coerenti fino al completamento; se cambiano durante il frame, usare versioni/snapshot corretti, non accettare silenziosamente texture stale.

**F02c — Residenza GPU:** inventariare memoria effettiva, allocazioni GL, pending free, EFB, vertex pool, heap guest, cache CPU, THP e BRSAR. Misurare working set univoco e distanza di riuso. Se 12 MiB sono insufficienti e c'è margine dimostrato, confrontare due budget limitati, ad esempio 16 e 20 MiB; sono esperimenti, non default prescritti. Tenere un margine per allocazioni transitorie e non consumare riserve a caso. Proteggere tutte le risorse in volo fino al completamento GPU, che non coincide necessariamente con la fine del packet o dello swap.

Se il set non entra, ridurre duplicazioni e byte reali con formati GPU equivalenti supportati e verificati, oppure ammissione basata sul riuso. Conversione CMPR/compressione è opzionale e richiede prova di equivalenza di layout/alpha; niente riduzione arbitraria di qualità o mancata risoluzione di texture per far sparire i miss. LRU con tabella più grande non risolve automaticamente un budget di byte insufficiente.

**Test:** confronto pixel col decoder precedente su formati usati, cambio generazione, riuso indirizzo, due materiali sulla stessa texture, palette/THP se coinvolti, modifica guest durante due frame in volo. Stress allocazioni e transizioni ripetute su Vita.

**Accettazione proposta:** scena calda statica senza cambi di contenuto → decode/upload/eviction ricorrenti dei medesimi pixel tendono a zero; primo gate ≥90% di riduzione del churn rispetto alla nuova baseline F01, senza errori visivi o memoria crescente. Separare animazioni ed EFB, che possono legittimamente aggiornarsi. Documentare il guadagno sul periodo finale, non solo il numero di hit.

## F03 — EFB corretto, copie efficienti e lifetime limitato

**Prerequisito:** F01. Anticipare prima della F02 se predominano i suoi costi o i fallimenti bloccano la resa.

**File:** [direct_efb.inc](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/direct_efb.inc), `ExecuteDirectEfbAt`, `ReserveEfbCommand`, `GXCopyTex`, `GXDestroyCopyTex` in gx_backend.cpp; [frame_optimization_policy.h](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/include/wiicompiled_vita/frame_optimization_policy.h); test [performance_helpers.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/tests/performance_helpers.cpp).

**F03a — Errori prima della velocità:** classificare le 4/13 failure storiche e 10/12 del menu P6.38. Implementare i casi richiesti dalla scena oppure dimostrare che un'operazione non ha consumatori/effetti osservabili. Distinguere formati unsupported, depth, budget, dimensioni, source layout e indirizzo stale. Non fare diventare «successo» una copia saltata.

**F03b — Lifetime e accumulo:** mantenere superfici compatibili con generazioni distinte dell'indirizzo guest e pool limitato. Rilasciare o riusare soltanto dopo l'ultimo lettore GPU. Allargare il coalescing oltre la sola operazione precedente solo dove il tracciamento dei consumatori dimostra equivalenza. Una copy sovrascritta può sparire solo se nessun draw/lettore CPU/copy intermedia la osserva e se non ha clear o altri effetti. Un clear va sempre preservato. Per overflow adottare drenaggio/segmentazione/backpressure corretti, non perdita silenziosa di copy o destroy.

**F03c — Copia e sincronizzazione:** il backing residente e il transfer esistono già. Ridurre wait ripetuti usando dipendenze/fence realmente supportati dalla versione locale GXM. Mantenere source e destination vivi e coerenti fino a transfer e sampling completati. Passare da finish per copia a completamenti raggruppati soltanto nei confini equivalenti; un clear o nuovo draw può richiedere un'altra dipendenza. Per CPU resize/channel conversion misurati dominanti, spostare il lavoro in un pass GPU equivalente o ottimizzare il fallback CPU con risultati identici. Prevedere readback esplicita quando il guest legge davvero i pixel, senza lasciare RAM obsoleta.

Le API di transfer sono dichiarate negli [header primari VitaSDK](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/gxm.h). La presenza dell'API non prova la corretta combinazione di sync/notification nella libreria custom: verificarla localmente. Non chiamare GL/GXM da USER_2. Tenere disabilitato il percorso FBO transitorio/`glBlitFramebuffer` associato ai crash precedenti; un eventuale nuovo pass deve essere isolato e provato su hardware prima di diventare default.

**Test:** sequenze copy→sample, copy→clear→copy, copy→overwrite, destroy→riuso indirizzo, frame multipli in volo, resize, conversioni canali, ritorno menu, carico che supera il vecchio limite 512. Test differenziali confrontano osservazioni/pixel, non la sola lista di comandi.

**Accettazione proposta:** zero `efb_cap_fail`, zero copie necessarie perse, nessuna texture stale; memoria torna al plateau dopo 10 transizioni. Prima soglia per il percorso EFB attribuito ≤15 ms, obiettivo finale incrementale ≤4–6 ms. Riportare separatamente la parte GPU precedente inclusa nelle attese. Se il frame peggiora mentre si correggono effetti mancanti, mantenere la correttezza e aggiornare il budget con trasparenza.

## F04 — Materiali e stato 3D corretti, con specializzazioni misurate

**Prerequisiti:** diagnostica F01 e texture/EFB sufficientemente corretti per fare confronti visivi. Questa fase è un requisito di giocabilità, non un optional a fine progetto.

**File:** capture/classificazione TEV, `ApplyDirectTevTextureBake`, `ApplyDirectTevMode`, `TransformVertex`, `TransformTexCoord` in gx_backend.cpp; [gx_tev.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/gx/gx_tev.cpp). Studiare eventuali helper shader già presenti senza ricollegare l'intero vecchio backend.

1. Congelare un profilo rescue solo diagnostico. Su un profilo corretto ripristinare gradualmente profondità, culling, alpha/blend, colore e texgen, una variabile per confronto. Verificare matrici e convenzioni clip/depth con dati guest, scene sintetiche e geometria reale; non attivare tutti i vecchi flag assumendo corretta la trasformazione Z dalle sole note.
2. Censire signature TEV più usate e più costose. Coprire prima quelle necessarie a pista, kart, HUD e trasparenze. Non confondere classifica «fallback» con costo per draw.
3. Dove la misura giustifica shader programmabili, costruire una chiave shader canonica per struttura TEV e stati che richiedono codice diverso; costanti materiali e matrici diventano uniform, evitando varianti/texture baked duplicate. Cache shader limitata, invalidazione e compatibilità con la versione del renderer esplicite. Evitare compilazione sincrona in frame caldi dopo il primo incontro.
4. Per catene non coperte mantenere un fallback corretto, oppure segnare chiaramente la feature ancora incompleta. `REPLACE`, white fallback, depth OFF o drop di effetti non sono fallback accettabili per il benchmark finale. Il prewarm FFP già tentato non va riproposto come soluzione principale senza tempi di shader creation misurati.

**Test:** confronti pixel/immagine delle equazioni TEV dove esiste un oracle; scene reali con occlusione kart/pista, alpha cutout, blending, effetti EFB, HUD e testo. Verificare anche texgen multipli e color/alpha separati se usati; catture hardware per ciascuna riattivazione.

**Accettazione:** per la matrice target nessuna geometria diagnostica, missing texture o equazione visibilmente errata; zero nuovi errori di trasformazione e copy. Obiettivo cold shader miss vicino a zero dopo warmup sulla scena ripetuta. Se non si completa una feature necessaria, G4 resta aperto anche con 30 FPS.

## F05 — Ridurre le submission fisiche senza alterare l'ordine

**Prerequisiti:** F01, semantica degli stati F04 definita e confini EFB F03 affidabili. Può iniziare con contatori e conversione topologica indipendenti, mantenendo profili separati.

**File:** `CaptureDrawState`, `PrepareFrameCpuState`, loop direct draw in [gx_backend.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:4518); percorso draw della libreria locale soltanto se il profiling prova un costo lì.

- Usare l'istogramma reale per distinguere fragmentazione di stato da strip/fan non concatenabili. Non limitarsi ad alzare `MKW_VITA_DIRECT_BATCHER`: è già ON.
- Canonicalizzare lo stato effettivo necessario per il draw. Escludere dalla chiave soltanto valori già incorporati nei vertici o non osservabili; includere raster, shader/uniform non baked, texture+versione, sampler, viewport/scissor, mask, depth/cull/blend/alpha, texgen e tipo di proiezione quando ancora rilevante. Non indebolire la chiave soltanto perché il rescue non usa depth.
- Normalizzare strip/fan/quads in liste indicizzate se necessario, conservando winding alternato, triangolazione e attributi flat/provoking vertex. Unire solo run consecutivi compatibili, rispettando ordine di primitive ed EFB, senza ordinamento globale per materiale.
- Calcolare i run in una passata; evitare di ricostruire tutti i suffissi compatibili per ogni draw. Upload contiguo o segmentato con ownership esplicita; nessun riuso di VBO/index buffer ancora letto dalla GPU.
- Il limite corrente può arrivare a 73728 vertici: con indici a 16 bit spezzare i batch in segmenti sicuri, oppure validare un formato di indice diverso supportato. Misurare anche l'aumento di indici/vertici dovuto alla triangolazione.

**Test:** oracle delle primitive prima/dopo, strip dispari/pari, fan corti, triangoli degeneri, proiezioni diverse, transparenze sovrapposte, texture e uniform differenti, confini EFB/clear. Confronto screenshot e packet/frame osservabile.

**Accettazione proposta:** primo obiettivo <2000 draw fisici nella scena attuale, poi <1000 **solo se la compatibilità misurata lo permette**. Nessun cap che scarta geometria. Se gli stati reali impediscono quel numero, riportare il minimo ottenibile e il costo API invece di forzare merge errati. Gate vero: meno tempo di submission e frame più rapido, senza danni visivi.

## F06 — Ridurre il lavoro guest e i blocchi del producer

**Prerequisito:** F01. Anticipare quando il renderer non è più la parte dominante o per un blocco funzionale del menu.

**File:** [guest_hot_profiler.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/guest_hot_profiler.cpp), [gx_dl.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/gx/gx_dl.cpp), [gx_egg.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/gx/gx_egg.cpp), [arc_fast.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/arc_fast.cpp), [task_thread.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/task_thread.cpp), [MAP.txt](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/projects/mkwii/MAP.txt).

1. Separare campioni CPU realmente in esecuzione da attese scheduler/I/O. Risolvere PC/LR nella mappa della revisione guest corretta. Per `0x80544A5C` investigare SaveCoreTask e figli/NAND; non attribuirlo genericamente al rendering o all'audio.
2. Ottimizzare le poche funzioni con quota maggiore del tempo esclusivo: decoder display list, layout, resource lookup, RFL, trasformazioni o guest tradotto secondo evidenza. Cache display list e mesh già presenti vanno validate per hit/invalidation e lifetime, non ricreate alla cieca.
3. HLE nativa soltanto con comportamento osservabile equivalente: confronti con funzione tradotta su input registrati, scritture memoria, valore di ritorno, ordine callback ed effetti collaterali. Evitare caching per solo puntatore quando memoria o contenuto cambiano.
4. O3 solo sui nuovi hotspot dimostrati; base tradotta rimane `-Os -fno-asynchronous-unwind-tables` e conserva eccezioni/semantica PPC. Nessun fast-math globale o ricompilazione integrale non necessaria. Non migrare il CpuContext guest mutabile su worker senza un progetto di ownership.
5. Misurare salvataggio/letture storage su una lane appropriata, mantenendo completamenti guest su USER_0 e semantica/durabilità previste. Non eliminare salvataggi, callback AI/AX/VI o workload di gameplay per gonfiare gli FPS.

**Accettazione proposta:** menu inizialmente <66,7 ms e poi entro il budget finale; CPU utile del producer per immagine target ≤25 ms, precisata dalla F08 per i tick. Nessuna regressione di input, timer, save/load o gara. L'obiettivo non si verifica con `producer_interval - queue_wait` della stessa riga: usare i nuovi timestamp.

## F07 — Prep, copie e contesa dei core

**Prerequisiti:** F01; F02/F05 chiariscono proprietà di texture e stream. Diventa prioritaria quando i 40–45 ms di vertex prep impediscono il passo successivo.

**File:** `PrepareFrameCpuVertices`, `PrepareFrameCpuTextures`, pipeline dei frame in gx_backend.cpp, [host_jobs.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/host_jobs.cpp), [host_jobs.h](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/include/wiicompiled_vita/host_jobs.h).

- Misurare calcolo per vertice, fetch matrice, texgen e scansioni; accumulare le operazioni per blocchi coerenti e usare NEON dove l'equivalenza numerica/visiva è verificabile.
- Evitare transform ripetute di mesh statiche solo quando matrice, palette ossea, attributi, texgen e dati sorgente lo consentono. Se si spostano trasformazioni in vertex shader, mantenere PN matrices, homogeneous W, clipping, lighting e texgen; profilo separato e attenzione all'interazione con batching/uniform.
- Packet ownership tramite slot immutabili da riciclare dopo tutti i consumatori: eliminare copie ridondanti, non introdurre puntatori a memoria guest che cambia. Misurare prima/dopo packet copy e byte trasferiti.
- Separare priorità dei job CPU grafici da I/O/prefetch e THP. Una nuova lane/thread su USER_2 non crea un altro core: registrare CPU time totale, queue delay, starvation e lock. Preservare affinity GL/GXM su USER_1 e callback guest su USER_0.
- Mantenere le code limitate. Aumentare profondità per nascondere stall peggiora memoria e latenza input quando il consumo resta più lento della produzione.

**Accettazione proposta:** prep totale p95 ≤10–12 ms sulla scena calda, copia packet ≤1–2 ms se ottenibile senza regressioni; coda senza crescita e riduzione del periodo finale. Test con video/audio/loading attivi oltre alla gara stabile.

## F08 — 30 FPS con simulazione e input alla velocità corretta

**Prerequisito:** contratto temporale definito in F00 e frame utili vicini al budget; verifiche di correttezza F04. Misurare i tick fin dalla F01, implementare cambi alla cadenza soltanto dopo aver capito il modello guest.

**File:** [vi.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/vi.cpp), gx_egg.cpp, SubmitFrame/present in gx_backend.cpp, scheduler/clock guest e input.

1. Confrontare cronometro di gara, numero tick di simulazione e secondi host su percorso ripetibile. Determinare se il guest è frame-locked e a quale cadenza nella modalità/revisione usata; non assumere che limitare lo swap dimezzi correttamente il lavoro.
2. Se occorrono 60 tick di simulazione/s, mantenere quei tick e produrre 30 immagini/s, con budgeting esplicito di due tick per immagine. Prima misurare se il guest può sostenerli. Non dimezzare VI, audio callback o velocità fisica per far coincidere un contatore con 30.
3. Saltare eventuale lavoro puramente visivo solo se separato da readback, EFB, callback e side effect necessari al guest. Scartare un FramePacket arbitrario può perdere copy/clear e alterare il gioco; prima stabilire quali effetti devono comunque essere eseguiti.
4. Usare timestamp/pacing coerenti per immagini nuove, limitando il lavoro in volo. Test input durante carico, menu e countdown; misurare input-to-visible con ripresa o strumento disponibile, senza dedurlo solo dalla frequenza di polling.

**Accettazione:** velocità simulazione coerente col riferimento entro tolleranza proposta ±1% su almeno 60 s di gara esclusi pause/loading; 30 immagini nuove/s sostenute, senza duplicati usati per mascherare il ritardo; coda stabile e controlli verificati. Se il guest non sostiene il tick necessario, tornare a F06: G4 resta aperto.

## F09 — Loading, salvataggi e audio continui

**Prerequisiti:** F01, con avanzamento anticipato quando un blocco impedisce i test delle altre fasi. Non aspettare la fine per correggere salvataggi o crash.

**File:** [egg_decomp.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/egg_decomp.cpp), [dvd.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/storage/dvd.cpp), storage NAND effettivamente chiamato, [audio_backend.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/audio_backend.cpp), [audio.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/audio/audio.cpp), AX/THP e host jobs.

- Verificare Yaz0 fast-direct con output byte-identico al decoder di riferimento, inclusi overlap, fine buffer e input troncati; misurare la stessa SZS prima/dopo. Non considerare vecchi tempi di 620 ms come baseline P6.38 senza rifare la misura.
- Prefetch BRSAR: misurare latenza foreground, byte letti inutilmente, contesa e hit effettivi; mantenere budget e backoff. Non aumentare buffer/audio/cache in concorrenza senza bilancio memoria comune.
- Audio: misurare delta per scena di real/silence/underrun/clamp, profondità coda, tempo CPU mixer/guest callback e latenza. I timer inclusivi di callback non sono risparmio eliminabile. Conservare la cadenza richiesta dal guest; qualunque cambiamento dei cap/backlog deve avere una prova temporale.
- Stabilizzare il sink e il pacing dopo aver misurato le cause di starvation. Evitare di far sparire underrun accumulando secondi di audio o inventando blocchi silenziosi classificati «reali».

**Accettazione proposta:** zero underrun e nessun clamp nella gara calda di 10 minuti; avvio/transizioni riportati separatamente. Save/load funzionali, nessuna perdita di dati, loading misurato p50/p95 e niente freeze non attribuito. Se l'audio rimane discontinuo, il gioco non supera G4 anche con grafica a 30 FPS.

## F10 — Verifica finale e decisione sull'obiettivo

**Prerequisiti:** G3, F04 e F08. Nessun nuovo cambio algoritmico nella build usata per certificare il risultato.

**Matrice minima:** pista benchmark con gara completa e contenuto previsto; almeno due piste aggiuntive scelte per effetti/trasparenze/varietà; countdown, oggetti, collisioni, arrivo, replay se disponibile nel flusso, ritorno menu e reingresso. Un test continuo di almeno 10 minuti e 10 cicli di ingresso/uscita per lifetime. Non estendere automaticamente il risultato a split-screen o tutti i contenuti non provati.

**Criteri proposti per «giocabile a 30 FPS»:**

- 29,5–30,5 immagini nuove/s in ogni finestra stabile da 60 s del benchmark, con target/pacing 30; non solo la media aggregata.
- Intervallo p95 ≤35 ms, p99 ≤40 ms; nessun hitch >100 ms in gara calda. Riportare anche frame mancati, latenze max e 1% low con formula esplicita. Se l'API misura soltanto swap return, aggiungere una verifica della cadenza visibile su pannello.
- Simulazione a velocità corretta secondo F08; input-to-visible obiettivo p95 <100 ms se misurabile, con metodo dichiarato e verifica manuale dei controlli.
- Materiali, profondità, trasparenze, HUD, effetti EFB e geometria necessari presenti. Nessun force-white/rescue/depth-off globale, content skip o limiti che perdono draw/copy.
- Nessun crash, overflow, OOM, leak crescente, lettura stale, race nota o underrun nella finestra di gara. Le transizioni sono parte della prova funzionale e hanno tempi separati.

Confrontare una build senza telemetria dettagliata e una equivalente con metriche aggregate: registrare l'overhead residuo. Se una soglia fallisce, aggiornare il piano con il nuovo collo di bottiglia e una sola prossima azione. Se manca un percorso credibile per rientrare nel budget senza compromettere il gioco, dichiarare il limite misurato e la parte mancante; non presentare il target come raggiunto.

## Build, controlli e consegna di ogni incremento

Comando già esistente per ricostruire la baseline (solo quando serve; in questo audit non è stato eseguito):

```sh
python3 vita/tools/build_performance_profile.py full-content-p6_38-producer-io-yaz0 --jobs 8
```

Il tool configura VitaSDK/PATH, esegue package + verifica VPK e per il direct controlla l'assenza di simboli renderer Aurora. Per una nuova fase usare il suo nuovo profilo, non sovrascrivere P6.38. Il builder scrive `.evidence.json` con config, hash fonti/artefatti e `hardware_validated=false`: il risultato hardware deve essere documentato a parte con log e hash corrispondenti.

Per modifiche runtime/GX usare i target applicabili letti dal Makefile, tra cui:

```sh
PATH="/usr/local/vitasdk/bin:$PATH" make -f Makefile.vita graphics-check
git diff --check
```

`runtime-native-check` è pertinente per HLE/ABI quando coinvolti. `python3 vita/tools/test_performance_helpers.py` esegue test host esistenti per helper; parte del codice testato è storico Aurora, quindi non sostituisce test mirati del nuovo percorso direct. Aggiungere test differenziali sostanziali per cache, primitive e EFB quando cambiano gli algoritmi. Verificare anche compilazione della variante profiler OFF e la configurazione realmente impacchettata.

Per ogni VPK controllare exit code di compilazione/link/package, manifest, `unzip -tq` sul file esatto e SHA-256 di VPK/ELF/libreria. Riutilizzare oggetti generati compatibili; se i flag cambiano, invalidare o separare solo gli oggetti interessati con un meccanismo verificato. Non fare clean globale o ricostruire tutta la traduzione senza motivo.

Ogni consegna deve includere: problema concreto, modifica, baseline e variante, test offline con esito, VPK/hash, prova Vita con scenario/serial, tabella prima/dopo, limiti e prossima fase. Aggiornare `STATO_E_CONTINUAZIONE.md` dopo ogni risultato, anche negativo. Prima dell'esaurimento del contesto salvare i tre documenti principali e lasciare un messaggio di continuazione con ultimo profilo verificato, file modificati e prossimo comando. Non attendere l'ultimo token per scrivere il report.
