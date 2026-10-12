# Stato attuale e cause da isolare

Il port presenta tre problemi distinti: una base sorgente non completamente riproducibile, una resa grafica errata e una cadenza estremamente lontana dal budget di 60 FPS. La riduzione delle chiamate GXM è reale in alcune catture, ma lascia attivi produzione GX, decodifica, trasformazioni, cambi di stato, copie EFB e sincronizzazioni. Non c'è una misura che attribuisca tutto il rallentamento alla GPU o allo storage.

L'audit comprende WiiCompiled, l'Aurora incorporata in `aurora-main`, il repository standalone `aurora-vita`, l'archivio `aurora-vita-max-prehardware`, i pacchetti locali e la lettura del dispositivo. Il dettaglio operativo degli interventi è in [PLAN.md](PLAN.md).

## Identità dei sorgenti e dei programmi

| Oggetto | Identità verificata | Implicazione |
| --- | --- | --- |
| WiiCompiled | `90eb1ddc93d545a2b4509600de43904d13400196`, branch `codex/restart-iso-gxm-20261005`, modifiche locali | Il solo commit non descrive l'intero sorgente usato dalle build sperimentali. |
| Aurora incorporata | `7a69a51bf392c62e37aca5926f0dbd27cb0c128b`, branch `wiicompiled-integration-20261006`, modifiche locali | È la copia compilata da WiiCompiled, tramite `vita/CMakeLists.txt`. |
| Aurora standalone | `262e8e1e55ee238decd1d0220db7937c92526edf`, branch `vita-experiment`, modifiche locali | Non viene automaticamente utilizzata da WiiCompiled. |
| Aurora max prehardware | Directory senza metadati Git; `UPSTREAM_COMMIT.txt` indica `f1189541e5d8b97fdf61946377853488d504d9df`, 2 settembre | È un archivio distinto, non il backend corrente. |
| SELF letto dalla Vita | SHA-256 `7e35227b1190bb6cbc1298c12dc3b6386fd492ab104379650fb3d3160c18559c`, 25.212.726 byte | Coincide con il record di installazione verificata dell'11 ottobre alle 09:51:42 UTC. |
| SELF locale e contenuto del VPK | SHA-256 `8c8e4e93c7ed76495c4bbee7fc69f939857623225296b8ed141d0813c69af200` | I due file locali coincidono fra loro; differiscono dal programma installato. |
| VPK locale | SHA-256 `2ed5020c6d123b051e508db5a39f2b75cf3189466c373bc33e101690ba8914f2` | Il manifest locale lo descrive, ma non certifica i sorgenti ora presenti. |

Fra i commit Aurora standalone e incorporato ci sono sette commit e differenze in 60 file, prima delle modifiche locali. Copiare indiscriminatamente una directory sull'altra perderebbe differenze d'integrazione. Le correzioni comuni vanno portate come patch circoscritte, con test in entrambe le copie.

La verifica dello SFO del VPK locale conferma `TITLE_ID=WICMKW001`, `ATTRIBUTE=32768`, `ATTRIBUTE2=12`. Non è una nuova verifica numerica degli attributi del SELF installato.

## La base sorgente non riproduce l'ultimo pacchetto

Il controllo `scripts/install_vita_current.py --check-local` fallisce con:

```text
Local source/generated files differ from DELIVERY.json; rebuild Vita before installing
```

È un rifiuto del controllo d'identità locale, non un problema di permessi. Non va aggirato rigenerando un manifest attorno a un vecchio binario.

Al momento dell'audit mancano la directory `generated/` e `build/vita-data/`. La build host fallisce in `runtime/src/abi_bridge.cpp:13` perché non trova `generated/RuntimeConfig.h`. Il manifest del pacchetto riportava 31.130 file sorgente/generati; una fotografia durante l'audit ne trova 1.291 e un digest diverso. La differenza di conteggio non prova da sola quali file siano stati eliminati: l'assenza degli input generati e l'errore del compilatore sono invece riscontri diretti.

All'inizio falliva anche il target `render-config-tests`: WiiCompiled richiedeva sei campi assenti da `BackendConfig`. Durante l'ispezione modifiche concorrenti hanno reintrodotto i campi e il percorso di snap EFB. Dopo tale modifica:

- `render-config-tests` compila ed esegue con successo;
- `runtime-config-contract` passa;
- `vita/backend.cpp` passa il controllo sintattico VitaSDK con i flag del proprio target;
- la build del target di dispatch continua a fallire per l'header generato mancante.

Il ripristino della dichiarazione non equivale al ripristino dell'implementazione. Nella ricognizione conclusiva, questi cinque campi hanno una dichiarazione nell'header ma nessun utilizzo per attivare la relativa funzione nel sorgente Aurora incorporato:

| Campo | Stato osservato nell'Aurora incorporata |
| --- | --- |
| `native_lyt_packets` | Dichiarato; consumer del percorso nativo non ritrovato. |
| `gxm_native_primitive_min_vertices` | Dichiarato; selezione delle primitive native non ritrovata. |
| `gxm_defer_program_cache_writes` | Dichiarato; coda di scritture differite e file di implementazione non ritrovati. |
| `gxm_precomputed_geometry` | Dichiarato; cache M06 non presente nella copia incorporata, presente invece nel repository standalone. |
| `gxm_deferred_efb_targets` | Dichiarato; gestione differita corrispondente non ritrovata. |

`gxm_efb_copy_snap_1to1` ha invece un collegamento verificabile da backend a `DrawSink` dopo le modifiche concorrenti. Il suo beneficio e l'equivalenza dei pixel non sono provati da questo audit.

Il log del programma installato contiene contatori di replay di display list multiple che non risultano riprodotti dalla versione corrente di `command_processor.cpp`. Anche questo impedisce di considerare il checkout corrente equivalente al SELF installato. I report M02/M06/I/O precedenti descrivono uno stato più ricco di quanto sia ora recuperabile dal solo commit e dalle modifiche presenti.

## Misura su PS Vita reale

L'11 ottobre, alle 21:35–21:36 UTC, l'audit ha letto da FTP SELF, configurazione e ultima sessione del log. Non ha avviato una nuova gara. Il log remoto aveva `mtime=20261011102952`: i risultati seguenti appartengono all'ultima sessione conservata sul dispositivo, non a un benchmark lanciato durante l'audit.

Il filtro più prudente esclude la finestra che attraversa il primo marker osservato di `stage=2` al frame 632. Comprende soltanto gli intervalli successivi, con tutte le pause a zero.

| Metrica | Risultato |
| --- | ---: |
| Frame contati | 651, dal 633 al 1283 |
| Finestre temporali | 45 |
| Tempo complessivo | 471,279 s |
| Cadenza media dei frame completati | **1,38135 frame/s** |
| Tempo medio per frame | **723,931 ms** |
| P95 delle medie di finestra, pesato per i frame | 891,188 ms |
| Budget a 60 FPS | 16,667 ms |
| Rapporto fra tempo attuale e budget | 43,436 volte |
| Riduzione del tempo necessaria rispetto a questa misura | 97,698% |

Il P95 riportato non è il percentile dei singoli frame. `runtime-cadence` misura il ritmo dei frame completati; non dimostra da solo che ogni frame contenga i draw corretti, che l'input risponda o che il display mostri tutti i pixel attesi.

La gara avanza: nel tratto finale `race_frames` cresce, `section_paused`, `controller_paused` e `scene_paused` restano a zero, e il controller registrato/attivo è `0x124`. Questo esclude una semplice pausa come spiegazione del frame rate di quel tratto; non sostituisce una prova di guida e di HUD.

### La configurazione letta non coincide con quella dell'ultimo avvio

Il log di avvio riporta `geometry_min_vertices=65535`; il TOML scaricato contiene `geometry_min_vertices=48`. Il TOML contiene inoltre `efb_copy_snap_1to1=true`, ma il log non ne conferma l'applicazione. Sono confermati dal log batching ON, worker GX avviato, due helper, materiali nativi ON, GPU lighting ON, precomputed geometry dichiarata ON, prepared display lists ON, async EFB OFF e probe OFF.

Non attribuire quindi 1,381 FPS allo snap EFB o alla soglia 48. Il programma installato e la configurazione sono verificati al momento della lettura; il boot marker comune e il record d'installazione sostengono la provenienza della sessione, ma il log non incorpora un proprio hash SELF/config per ogni avvio. Serve una nuova baseline con questa identità registrata esplicitamente.

Il PSARC remoto misura 3.219.564.087 byte, con data di modifica invariata rispetto ai record precedenti. Questo audit non ne ha ricalcolato il digest completo. Il boot conferma l'uso del PSARC: la presenza anche di `dvd_iso` nel TOML non prova un runtime direct-ISO. Se la lettura diretta da `mariokart.iso` resta un requisito, lo stato attuale non lo soddisfa e il percorso dati va deciso prima di cambiare cache o asset.

Il codice richiede 444 MHz CPU e 222 MHz GPU, ma questa cattura non contiene un readback dei clock effettivi. I valori richiesti non vanno presentati come frequenze misurate.

## Perché le ottimizzazioni non stanno portando al target

Le prove disponibili non dimostrano che ogni modifica peggiori le prestazioni. Dimostrano che diversi interventi non hanno prodotto un guadagno hardware sufficiente e che i confronti spesso usano fasi, SELF o configurazioni differenti.

### Il batching arriva dopo molto lavoro già svolto

Le catture PS Vita precedenti contano circa 6.100–6.300 richieste GX per frame. Il batching ne fonde migliaia e porta le chiamate native GXM nell'ordine delle centinaia, senza eliminare il problema di circa 1 FPS. Restano a monte il codice guest che costruisce i comandi, la validazione delle display list, la decodifica dei vertici e dello stato e la preparazione dei materiali.

Nella cattura hardware `m02-2-mainram-on-20261010T204217Z`, sei campioni dal frame 224 danno mediane di 6.226 draw richiesti, 5.827 merge e 487 draw nativi. Il tempo wall del worker è 734,73 ms; i timer nativi riportano circa 1,54 ms per le chiamate draw, 124,26 ms per la cattura EFB e 40,98 ms per le sincronizzazioni di distruzione risorse. Questi intervalli sono annidati e parzialmente sovrapposti: **non si sommano né si sottraggono per ricavare una percentuale CPU/GPU**. Il timer delle chiamate draw non misura tutto il lavoro asincrono della GPU.

Il dato utile è che il conteggio di chiamate GXM non spiega da solo centinaia di millisecondi. Proseguire con altre micro-cache di submission senza profilare il lavoro a monte non ha un beneficio quantitativo dimostrato.

### I percorsi nativi coprono una parte del carico

Nell'ultima sessione hardware, il delta dei contatori fra frame 1216 e 1280 mostra:

| Contatore | Media per frame sui 64 frame |
| --- | ---: |
| Replay di liste multiple | 77 |
| Draw contenuti nei replay | 272 |
| Hit delle prepared display list | 13 |
| Miss delle prepared display list | 169 |
| Liste rifiutate dal percorso prepared | 30 |
| Candidati RAM accettati e mantenuti | 182 |
| Fallback RAM filtrati | 797 |
| Pacchetti LYT nativi | 0 |

Il rapporto hit/(hit+miss) delle prepared list è circa 7,14%. I contatori non sono tutti popolazioni indipendenti e non vanno sommati. I dati giustificano un'analisi delle cause di miss/filtro per famiglia di produttore; non giustificano un incremento arbitrario del budget RAM.

`native_lyt=0` all'avvio spiega i contatori LYT nulli. Non dimostra che l'HUD non venga prodotto dal percorso GX originale.

### Aggiungere thread non elimina automaticamente il lavoro seriale

Esistono già un produttore guest, un consumer GX dedicato e helper CPU. GXM conserva un unico proprietario. Le vecchie tracce Vita3K mostrano job molto piccoli e helper inattivi; sono dati di emulatore, non percentuali di CPU Vita.

Nel sorgente auditato `cpu_parallel_for_min_lanes_impl` acquisisce ancora il flag atomico del pool prima di determinare che un job è troppo piccolo. La micro-correzione documentata in precedenza non è presente in quella fotografia. Recuperarla può evitare overhead, ma non autorizza a promettere un salto da circa 724 a 16,7 ms.

La scelta strutturale da misurare è preparare una volta geometria e materiali statici, mantenendo dinamiche pose, matrici, luce e animazioni. I job di preparazione possono essere raggruppati soltanto con snapshot immutabili, invalidazioni esplicite e ordine GX preservato.

### Copie EFB e sincronizzazioni sono un secondo costo concreto

Le catture mostrano circa 11–13 copie EFB per frame e frequenti aperture/chiusure di scene. Le copie scalate o con conversione possono richiedere un draw su target offscreen; il ritorno al target sorgente riapre una scena. I confini copy/clear sono dipendenze reali.

`GX__CopyTex_8016fd74` mantiene `GXDrawDone()` con async EFB OFF o callback osservabile. Ridurre queste attese richiede di conservare ordine, callback, letture guest e durata delle risorse. Eliminare una finish senza retirement verificato può produrre texture errate o crash.

Il parametro `scenesPerFrame` è per render target. Confrontarlo con il totale globale delle scene può produrre una diagnosi sbagliata. La telemetria per target descritta nel report precedente non risulta interamente presente nella fotografia corrente: recuperarla prima di usare «5 scene» come criterio numerico. Raggiungere cinque sul contatore non garantirebbe comunque 60 FPS.

### I/O e cache shader hanno un ambito più limitato

Prewarm e scritture differite possono ridurre caricamenti e picchi di compilazione. Non c'è una misura hardware che attribuisca loro i circa 724 ms stabili della gara. Il PSARC locale usato dai precedenti test d'integrazione non è più nel percorso atteso; i report precedenti non bastano a validare l'attuale sorgente.

## Texture bianche e HUD

### Evidenza visiva e readback non attendibile

La cattura salvata `build/vita3k-trace/live-20261011/vita3k-game-frame-current.png` mostra Mario grigio/bianco, parti dello scenario bianche o nere, illuminazione scura e nessun normale HUD leggibile. È una cattura Vita3K precedente, non una nuova immagine della PS Vita. Il suo stage non è stato correlato in questa ispezione: da sola non prova che l'HUD debba essere attivo in quel preciso frame.

Il file `synchronized_retry/frame-300.ppm` contiene invece 1.566.720 byte RGB tutti a zero: immagine 960×544 interamente nera. Il log dello stesso esperimento registra una cattura completa e `sceGxmDisplayQueueFinish()=0`; le immagini della finestra documentate nell'esperimento mostrano la pista. È un difetto riprodotto del percorso di lettura o della sua interazione con Vita3K. Non è ancora attribuito a cache CPU, superficie sbagliata o emulatore, né è provata la stessa anomalia su hardware.

`Renderer::readback_rgba8` legge il backing CPU di `surfaces[front]` dopo `finish(Readback)`. Va verificato con un pattern noto, target identificato e confronto con il display realmente presentato. Finché il controllo fallisce, un PPM nero non può essere usato per concludere che il gioco renderizzi nero.

### Il bianco non è spiegato dal fallback della texture

`white_fallback_draws=0` esclude l'uso della specifica texture sostitutiva contata. Non verifica contenuto, sampler, UV, texgen, TEV, lightmap o validità temporale del binding.

I precedenti A/B Vita3K documentano il difetto anche con illuminazione CPU, TEV di riferimento e cache geometrica disabilitata. Escludono una spiegazione semplice basata su una sola di quelle opzioni; non sostituiscono una prova per materiale sul dispositivo.

L'inventario PAL conservato in `build/material-audit/mario-pal.json` descrive i materiali principali di Mario con tre texgen, tre sampler e tre stage TEV, collegati a `lm_0`, `mario_all`/texture degli occhi e `lm_1`. Le analisi salvate delle texture sorgente non le descrivono come interamente bianche. Questo audit ha ricontrollato il JSON e i test sintetici dell'auditor; non ha rieseguito la lettura del PSARC locale assente.

Il vecchio CSV non contiene abbastanza descrittori TEV/provenienza per riconoscere quel materiale con certezza. L'assenza della maschera attesa o un bounding box sovrapposto alla testa sono indizi, non identificazione del draw responsabile. Il test PAL FIFO citato nei report precedenti non è presente nel file dei test frontend della fotografia auditata: va recuperato, non considerato implicitamente incluso nel PASS generale.

La prossima prova utile è correlare un draw realmente responsabile dei pixel con sorgente GX e digest dei byte compressi, TEXMAP/TEXCOORD, tre stage TEV, matrici UV, descriptor GXM e risultato campionato. La diagnostica di provenienza in corso nei sorgenti condivisi è coerente con questo obiettivo, ma non ha ancora prodotto una nuova prova hardware in questo audit.

### L'HUD richiede una diagnosi distinta

La sessione hardware arriva a `stage=2`, con più layer di pagina e nessuna pausa. Questo prova una gara attiva, non l'emissione o la visibilità dei draw LYT.

Occorre separare due casi: l'interfaccia non viene costruita/prodotta dal guest, oppure i suoi draw arrivano ad Aurora e vengono scartati, coperti o campionati male. Registrare creazione/visibilità della pagina, draw LYT, ordine rispetto a `GXCopyDisp`, alpha, depth, viewport e scissor. Catturare un frame dopo l'introduzione e un secondo frame più tardi. Abilitare il percorso LYT nativo senza confronto non è un fix dell'HUD.

### La compatibilità EFB depth è incompleta

`DrawSink::copy_tex` rifiuta formati che non siano copie colore supportate e ritorna `false`, includendo le copie depth. Il messaggio parla di vitaGL, ma il controllo è nel percorso condiviso e riguarda anche GXM. Il runtime menziona copie probe depth per effetti come lens flare.

È una lacuna di compatibilità verificata nel codice. Non c'è una correlazione sufficiente per dichiararla causa del pilota bianco o dell'HUD. Va trattata come ticket separato, con formato, tiling, lettura guest e ordinamento verificati.

Il contatore generico `unsupported` non equivale a draw mancanti: comprende anche approssimazioni come `zCompLocBeforeTex` e LOD. Nella cattura Vita3K si vedono migliaia di eventi, ma senza conteggio per motivo sarebbe scorretto trasformarli in migliaia di draw persi.

## Qualità della misura e dei test

`aurora_gpu_frame_time` restituisce il tempo CPU trascorso in `sceGxmDisplayQueueAddEntry`, come dichiara il commento del codice. Non è un timer GPU. Anche `worker_frame_us` è wall time, non tempo CPU esclusivo. Il piano deve prima separare lavoro attivo del producer, lavoro attivo del consumer, attesa FIFO, preparazione e submission, copie EFB, finish e presentazione.

| Verifica eseguita durante l'audit | Esito e limite |
| --- | --- |
| Build host WiiCompiled | Fallisce per `generated/RuntimeConfig.h` assente. |
| Target render config iniziale | Fallisce per sei campi Aurora mancanti. |
| Target render config dopo modifiche concorrenti | Compila ed esegue; non prova il consumo dei flag nel renderer. |
| Runtime config | PASS dopo ricompilazione del target. |
| Build host Aurora incorporata e CTest | 24/24 PASS nella fotografia testata, backend host VITAGL. Non esercita la GPU GXM. |
| Auditor materiali Python | 8 test PASS, 1 integrazione saltata perché il PSARC non è disponibile nel percorso atteso. |
| Sintassi `vita/backend.cpp`, flag del target corretto | PASS dopo il ripristino dei campi. Non è link o VPK completo. |
| Integrità SELF nel VPK locale e SFO | PASS; il pacchetto è diverso dal SELF installato. |
| Installer `--check-local` | Rifiuta la divergenza sorgenti/manifest. Nessuna scrittura su device. |
| PS Vita | Lettura di SELF/config/log; nessuna nuova prova visiva o di input. |

I precedenti report citavano 28 test Aurora. Ora la suite configurata ne contiene 24; alcuni vecchi eseguibili, ad esempio quello delle scritture GXP differite, restano in `build/` mentre i relativi sorgenti non sono presenti. Eseguire quei vecchi file non validerebbe il checkout attuale.

Una prova sintattica preliminare ha usato i flag del target game su `vita/backend.cpp`, causando anche errori di macro non pertinenti. È stata sostituita dal controllo con i flag di `wiicompiled_backend`, passato. Il log preliminare resta come traccia, ma non è usato per dichiarare un difetto del codice.

## Decisione tecnica

Il prossimo intervento deve produrre una baseline completa e misurabile. Recuperare i sorgenti mancanti, rendere esplicite le funzionalità realmente implementate e riallineare test e manifest viene prima di promuovere altre ottimizzazioni.

La correzione grafica segue due prove separate, materiale e HUD, sostenute da una cattura attendibile. La performance richiede poi un intervento sul tratto dominante misurato: i candidati strutturali sono la produzione/preparazione GX e le dipendenze EFB, non un'altra attivazione simultanea di flag.

**60 FPS resta il target. La sua raggiungibilità su questo port non è dimostrata.** Il divario misurato è di circa 43 volte, quindi una micro-ottimizzazione non basta come piano credibile. Il criterio finale deve includere gara guidabile, HUD e materiali corretti, aggiornamenti reali e frame time di 16,667 ms nelle scene concordate, su PS Vita reale.
