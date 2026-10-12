# Piano di correzione e verifica

I ticket seguenti sono pronti per essere assegnati separatamente, ma non sono stati implementati da questo audit. Ogni risultato deve indicare se è una verifica dei sorgenti, un test host, una build, una prova su emulatore o una prova PS Vita. Il riferimento numerico è la gara conservata sul dispositivo: 1,381 FPS e 723,93 ms/frame. Non è una baseline con cui confrontare indiscriminatamente build future.

L'ordine è **R0 → R1 → C0 → C1/C2**, mentre **P0** si può svolgere dopo R1 su una build diagnostica separata. Le ottimizzazioni **P1/P2** richiedono P0; la promozione di qualunque candidato richiede i controlli grafici. L'integrazione Aurora **A0** accompagna le correzioni comuni senza sostituire in blocco la copia incorporata.

## R0 Recuperare una base sorgente completa

**Priorità:** bloccante. **Dipendenze:** nessuna.

**Ambito e file:** `generated/`, pipeline di preparazione/build, `CMakeLists.txt`, `vita/game.cmake`, `aurora-main/cmake/aurora_vita.cmake`, test e sorgenti M02/M06/I/O citati nei report, script di delivery. Conservare i diff dei due repository, le copie di sorgente disponibili e gli artifact prima di ricostruire gli input.

Recuperare o rigenerare gli input dalla ISO disponibile senza perdere il lavoro locale. Individuare l'implementazione effettivamente usata dal SELF installato attraverso snapshot/diff/build conservati; non dedurla dal suffisso `-dirty`. Ricostruire una coppia WiiCompiled/Aurora esplicita. I campi riaggiunti a `BackendConfig` devono avere un'implementazione oppure essere esposti come non supportati: non basta far compilare la struttura.

**Accettazione di correttezza:** build host completa da directory nuova; nessuna dipendenza implicita da eseguibili vecchi; lista dei test e delle funzionalità recuperate; build Vita riproducibile quando si procede alla compilazione; digest sorgenti prima/dopo coerente; eboot nel VPK identico al SELF; controllo locale dell'installer superato.

**Accettazione prestazionale:** nessuna promessa di guadagno in questo ticket. **Fallback:** snapshot precedente preservato e pacchetto noto conservato. Non rigenerare `DELIVERY.json` soltanto per rendere installabile un binario precedente.

## R1 Congelare la configurazione effettiva e la baseline

**Priorità:** bloccante per gli A/B. **Dipendenze:** R0.

**Ambito e file:** `scripts/record_vita_delivery.py`, `scripts/install_vita_current.py`, `scripts/capture_vita_current.py`, `vita/game_main.cpp`, `vita/backend.cpp`, diagnostica e analizzatori.

Registrare in ogni boot l'identità del pacchetto e della configurazione caricata, le opzioni effettive e i clock letti dal sistema. Rendere distinguibile un'opzione riconosciuta dal parser da una implementata nel renderer. Conservare una configurazione di controllo completa per ogni gruppo sperimentale. Chiarire il requisito direct-ISO rispetto al PSARC attualmente usato prima di cambiare il percorso dati.

**Accettazione di correttezza:** identità installata verificata; nessuna discrepanza fra TOML, marker di boot e capacità; Time Trial `stage=2`, pause zero, simulazione e input verificati. I confronti dichiarano personaggio, kart, pista, intervallo e cache calda/fredda.

**Accettazione prestazionale:** almeno tre run comparabili per controllo e candidato, warmup escluso, diagnostica invasiva spenta. Media aggregata, distribuzione dei singoli frame e dispersione fra run; non attribuire un guadagno inferiore alla variabilità. **Fallback:** ripristino dell'intero profilo sperimentale salvato, senza toccare altre impostazioni o dati.

## C0 Rendere affidabile la cattura grafica

**Priorità:** alta. **Dipendenze:** R1.

**Ambito e file:** Aurora `platforms/vita/gxm/gxm_renderer.cpp`, facade e gestione target; WiiCompiled `vita/game_aurora.cpp`.

Riprodurre con un pattern noto la lettura nera osservata su Vita3K. Tracciare superficie scritta, presentata e letta, stride, formato, sincronizzazione e visibilità dei dati fra CPU e GPU. Confrontare l'immagine del frame mantenuto sul display con il readback. Stabilire se il difetto sia nel renderer o specifico dell'emulatore prima di modificare le copie EFB normali.

**Accettazione di correttezza:** pattern e frame di gioco non neri quando il display non è nero; orientamento e colori corretti; test su Vita3K e PS Vita indicati separatamente; frame di cattura identificato. **Accettazione prestazionale:** le attese aggiuntive per la cattura rimangono limitate alla diagnostica esplicita. **Fallback:** screenshot esterno del display come riferimento, readback interno marcato non affidabile.

## C1 Identificare e correggere il materiale bianco

**Priorità:** alta. **Dipendenze:** R0, R1, C0.

**Ambito e file:** `scripts/audit_mkw_brres_materials.py`, `aurora-main/lib/gx/command_processor.cpp`, `platforms/vita/gx/aurora_gx_bridge.cpp`, `aurora_vita_draw_sink.cpp`, `gfx/vita_vertex_pipeline.cpp`, texture cache, `gxm/gxm_shader_gen.cpp`, `gxm_renderer.cpp`, test frontend.

Recuperare il test PAL del materiale e la diagnostica TEV/provenienza. Identificare i draw del corpo e degli occhi dai byte sorgente delle texture e da una prova dei pixel visibili. Confrontare registri GX e materiale prima della traduzione, `PipelineDesc`, texgen/matrici UV, texture/palette/mip, sampler e descriptor GXM. Usare una sostituzione diagnostica colorata soltanto sul materiale identificato, per distinguere texture non campionata da combinazione TEV errata. Correggere il primo confine in cui i dati divergono.

**Accettazione di correttezza:** corpo, occhi e animazioni PAT0/SRT0 corretti; lightmap e ombre conservate; nessun nuovo difetto su pista e kart; immagini in introduzione, gara e fase successiva. Un test sintetico deve fallire prima della correzione e passare dopo.

**Accettazione prestazionale:** nessuna ricompilazione shader, decodifica texture o finish introdotta per draw nel percorso normale. A/B quiet dopo la verifica grafica. **Fallback:** mantenere il percorso di riferimento e limitare eventuali nuove specializzazioni alle condizioni provate.

## C2 Ricostruire la catena dell'HUD in gara

**Priorità:** alta. **Dipendenze:** R0, R1, C0. È indipendente dalla causa del bianco.

**Ambito e file:** `vita/game_time_trial.cpp`, produttori LYT nel runtime GX, `runtime/src/hle/gx/gx_dl.cpp`, `gx_copy.cpp`, bridge/sink Aurora e gestione viewport/scissor/depth/alpha.

Acquisire dopo `stage=2` un inventario delle pagine/layer attivi e dei draw HUD. Separare mancata produzione dal guest, scarto del draw e mancata visibilità sul display. Verificare texture font/icone, alpha compare, depth, scissor, viewport e ordine rispetto alla copia display. Confrontare il normale ingresso da menu con Time Trial configurato, se il problema si riproduce solo nel percorso di test.

**Accettazione di correttezza:** tempo, giri, posizione, minimappa e indicatori previsti dalla modalità visibili e aggiornati; input e pausa funzionanti; controllo dopo l'introduzione e dopo almeno alcune centinaia di ulteriori frame. Non inserire un HUD sostitutivo per nascondere il problema.

**Accettazione prestazionale:** stesso carico e frame misurati dopo il ripristino; riportare l'eventuale costo dell'HUD prima assente. **Fallback:** LYT originale via GX; il flag nativo è un esperimento separato, non la prova di riparazione.

## C3 Completare le copie EFB depth necessarie

**Priorità:** compatibilità, da dimensionare sui consumer effettivi. **Dipendenze:** R0, P0 per l'attribuzione.

**Ambito e file:** `runtime/src/hle/gx/gx_copy.cpp`, encoder EFB, Aurora `DrawSink::copy_tex`, facade e target depth GXM.

Contare i formati depth richiesti dal gioco e identificare le letture guest o i sampler che li consumano. Implementare prima il sottoinsieme effettivamente usato, con quantizzazione, ordine dei byte e tiling GX corretti. Distinguere le copie depth dai normali color copy e preservare clear/callback.

**Accettazione di correttezza:** fixture indipendenti per pixel depth, rettangoli e lettura guest; nessun ritorno di successo con dati non aggiornati; effetto consumer verificato su hardware. **Accettazione prestazionale:** niente readback a superficie intera per un probe piccolo senza una motivazione misurata. **Fallback:** percorso CPU esatto, contabilizzato esplicitamente; nessuna finta texture bianca o nera.

## P0 Misurare il percorso critico su hardware

**Priorità:** bloccante per scegliere l'ottimizzazione. **Dipendenze:** R1.

**Ambito e file:** `vita/game_diagnostics.cpp`, `game_cpu_diagnostics.cpp`, `aurora-main/lib/gx/fifo.cpp`, `command_processor.cpp`, telemetria frontend/backend, GXM renderer e analizzatori.

Campionare sullo stesso intervallo di gara tempo attivo del guest, attese del guest, lavoro attivo del consumer, attesa FIFO, preparazione dei draw, copie EFB, finish per motivo e attesa della presentazione. Riutilizzare contatori cumulativi per thread quando disponibili; limitare syscall e log sul percorso caldo. Dare nomi che distinguano tempo CPU, wall e GPU: l'hook attuale `aurora_gpu_frame_time` misura attesa CPU di display queue.

La somma dei contatori annidati non è un frame time. Il report deve ricostruire sovrapposizione e dipendenze, indicare ciò che non misura e legare miss/fallback alla famiglia di asset/produttore che li genera.

**Accettazione di correttezza:** contatori monotoni e non nulli per effetto accidentale del gate diagnostico; nessun cambio di ordine o lifetime; sonda OFF equivalente al controllo. **Accettazione prestazionale:** overhead della sonda stimato con OFF/ON; selezione del prossimo intervento sostenuta da un costo dominante misurato. **Fallback:** breve run diagnostico separato, senza usare i suoi FPS come benchmark quiet.

## P1 Ridurre la produzione e la preparazione GX ripetuta

**Priorità:** strutturale, subordinata a P0. **Dipendenze:** R0, P0; C1/C2 per la promozione.

**Ambito e file:** traduttore/runtime dei produttori individuati, `gx_dl.cpp`, FIFO/command processor, cache di geometria e ricette Aurora, pipeline asset nativi.

Se P0 conferma il costo, migrare una famiglia misurata di produttori NW4r per volta, ad esempio geometria ResShp e stato ResTev statico. Preparare packet/ricette immutabili al caricamento o alla prima validazione. Aggiornare soltanto dati dinamici. Conservare invalidazioni per scritture guest, TLUT, EFB, materiale, layout e animazione. Analizzare il 7,14% di hit prepared e i 797 fallback RAM/frame osservati prima di ampliare le cache.

Usare gli helper soltanto per blocchi di lavoro sufficienti e snapshot indipendenti. GXM mantiene un unico proprietario. Recuperare il bypass dei job piccoli come patch circoscritta; trattarlo come riduzione di overhead, senza attribuirgli il guadagno dell'intera migrazione.

**Accettazione di correttezza:** equivalenza dei packet/indici/stato per la famiglia migrata, transizioni e invalidazioni; immagini e gameplay invariati. **Accettazione prestazionale:** riduzione del tempo producer/consumer e del frame time quiet nei run concordati, oltre alla riduzione dei conteggi. **Fallback:** originale GX per ogni caso non coperto o invalidato.

## P2 Ridurre scene EFB e sincronizzazioni dimostrate ridondanti

**Priorità:** alta se P0 conferma il costo. **Dipendenze:** R0, P0, C0.

**Ambito e file:** `gx_copy.cpp`, `DrawSink::copy_tex`, `gxm_facade.cpp`, `gxm_renderer.cpp`, lifetime dei target e contatori per target.

Ripristinare i contatori di scena per target. Correlare copy, clear e consumer. Provare separatamente: transfer 1:1 per rettangoli realmente equivalenti; riuso di target della stessa dimensione/formato; retirement dopo completamento GPU; eliminazione di copie duplicate solo senza scritture EFB intermedie e senza consumer osservabile. Lo snap di un pixel può cambiare filtratura: deve avere una prova di pixel e restare separato dall'esperimento async EFB.

**Accettazione di correttezza:** bordi, ombre, lightmap, HUD, depth, clear e letture guest invariati; nessuna risorsa liberata in uso; nessun errore BeginScene/target. Conservare il reset dello stato a ogni BeginScene.

**Accettazione prestazionale:** meno tempo per copy/finish e migliore frame time hardware, con conteggio per target spiegato. Non saltare copie necessarie per raggiungere cinque scene. **Fallback:** copia e sincronizzazione corrette precedenti, ripristinabili con un solo controllo sperimentale.

## A0 Consegnare le correzioni comuni anche ad Aurora standalone

**Priorità:** obbligatoria per completare la richiesta su Aurora. **Dipendenze:** ogni correzione comune validata.

**Ambito e file:** repository `aurora-vita`, copia `aurora-main`, configurazione CMake, interfacce pubbliche e test di stato/lifetime. L'archivio max prehardware resta un riferimento storico.

Separare le correzioni generali da hook e default WiiCompiled. Applicare le patch comuni a entrambi i repository verificando differenze preesistenti. Testare propagazione completa delle opzioni, uso runtime, invalidazioni, durata dei dati accodati e comportamento degli altri consumer. Un campo dichiarato e stampato al boot non costituisce una funzionalità consegnata.

**Accettazione di correttezza:** suite e manifest coerenti in entrambe le copie, build GXM, nessuna dipendenza da indirizzi PAL nel backend condiviso; prova hardware dei consumer interessati prima di modificare i loro default. **Accettazione prestazionale:** ogni guadagno è attribuito al titolo e alla scena misurati; non trasferire gli FPS di un gioco all'altro. **Fallback:** default preesistenti per i consumer non ancora provati.

## Criterio finale per 60 FPS

Il target è una gara corretta e guidabile con 60 aggiornamenti reali al secondo, non interpolazione, frame duplicati o rendering incompleto. La verifica concorda prima piste, personaggi, modalità e scene da coprire; una singola intro o una pista vuota non dimostra una copertura generale.

Usare tre run ripetibili a clock misurati, stessa build e stessa configurazione. Raccogliere tempi dei singoli frame e durata delle cadute, verificando media e P95 rispetto a 16,667 ms e riportando gli outlier. Per dichiarare 60 FPS stabili, documentare anche il frame pacing e le cadute nelle scene rappresentative; un P95 favorevole da solo non prova assenza di stutter. Controllare materiali, HUD, input, audio, pausa, riavvio gara e frame successivi al warmup.

Un risultato intermedio a 10, 20 o 30 FPS è un avanzamento misurato, non la chiusura del target. Se il nuovo profilo mostra un limite strutturale superiore al budget, rivedere l'architettura o l'ambito con i dati, senza promettere 60 FPS sulla base della sola potenza nominale della Vita.
