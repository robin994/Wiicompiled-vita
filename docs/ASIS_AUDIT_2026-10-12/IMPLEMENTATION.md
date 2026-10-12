# Implementazione e verifica del recupero

12 ottobre 2026. La prima implementazione ripristina una build completa e rende verificabili configurazione e funzionalità recuperate. La seconda corregge difetti riprodotti nella selezione delle texture BP e nella proprietà dello stato GX asincrono. La correzione visiva di Mario/HUD e il target 60 FPS restano da dimostrare sul dispositivo.

## Modifiche applicate

La preparazione ora compila prima il solo lettore ISO necessario al traduttore. Questo elimina la dipendenza circolare fra bootstrap e test che richiedevano `generated/RuntimeConfig.h`. Gli input sono stati rigenerati dalla ISO PAL già disponibile: 29.637 funzioni tradotte, organizzate in 72 shard. Non sono stati copiati file di una build vecchia nella nuova generazione.

La geometria precomputata GXM M06 è nuovamente implementata nell'Aurora incorporata. Il recupero usa l'helper e il test ancora conservati nell'Aurora standalone, adattando la submission alle differenze del renderer WiiCompiled. La cache contiene soltanto geometria residente e conserva i record fino al completamento delle scene che li utilizzano. Uniformi, texture e stato continuano a essere aggiornati sul contesto. Sono conservati invalidazione di buffer/programmi, fallback per pressione o errori e reset dello stato a ogni BeginScene. La cattura diagnostica e i pass interni non entrano nel percorso precomputato. Il default rimane OFF.

Le scritture GXP differite sono state ricostruite in entrambe le copie di Aurora: massimo 2 MiB, 256 record, 256 KiB per record, chiave sorgente/stage, riuso dei record in RAM e scarico dopo il prewarm iniziale e alla chiusura. L'errore di persistenza non invalida uno shader già compilato. La pubblicazione usa rename senza cancellare preventivamente il file precedente. Aurora generale mantiene il default OFF; WiiCompiled applica la propria opzione. Il reader degli shader nativi da PSARC resta specifico dell'integrazione incorporata.

Il percorso dei job CPU piccoli ora evita atomica e scansione del pool quando non può usare almeno due lane. La patch e la verifica di copertura, risultato e lane del chiamante sono presenti in entrambe le copie Aurora.

Il boot ora registra SHA-256 del SELF, SHA-256 dei byte effettivamente caricati dal TOML e frequenze CPU/GPU lette dal sistema. Il renderer registra le capacità recuperate. Le richieste per LYT nativo, primitive native e retirement EFB differito vengono rese esplicitamente inattive, poiché le loro implementazioni sono ancora mancanti. Il percorso LYT originale conserva il packet raw ordinato sul FIFO.

Lo script di cattura hardware confronta ora questi marker con SELF e TOML letti dal dispositivo. Rifiuta un log appartenente al programma precedente, una configurazione cambiata dopo il boot e identità assenti o ambigue. L'opzione esplicita `--allow-legacy-identity` consente di archiviare log storici privi di marker, segnalandoli come non verificati; non ignora identità discordanti. Questa verifica impedisce di associare il vecchio log della Vita al candidato appena installato.

È stata ripristinata la query della callback DrawDone usata dalla policy async EFB, conservando l'invocazione e l'ordine delle callback. Il test PAL invia registri BP e uno stato XF completo al decoder FIFO, poi controlla i tre stage TEV e gli argomenti del materiale. Questo verifica il contratto di traduzione; non identifica ancora il draw responsabile del modello bianco in gara.

## Correzione del percorso BP raw

Un oracle sul FIFO reale ha riprodotto tre errori prima della patch: un comando `IMAGE3` raw continuava a usare il puntatore e l'identità della precedente texture nativa, mentre una dimensione codificata di 1024 veniva decodificata come zero. Un secondo oracle, con il consumer asincrono fermo su una barriera controllata, ha dimostrato che `GXApplyBPReg` poteva cambiare `numTevStages` dal thread produttore prima del consumo del comando. Non sono ipotesi ricavate dalle PPM nere dell'emulatore: i test fallivano sui sorgenti precedenti e passano con la correzione.

Aurora incorporata e standalone distinguono ora le sorgenti native dai binding raw BP per ciascuna delle otto unità. Un `IMAGE3` completo seleziona la sorgente raw anche quando il valore del registro è identico al precedente; una maschera nulla conserva la sorgente, e una scrittura parziale che non permette di ricostruire un puntatore nativo viene rifiutata. Il resolver registrato da WiiCompiled traduce l'indirizzo fisico attraverso gli alias stabili MEM1/MEM2 e valida l'intero intervallo della texture, inclusi i mip. Non esegue fence né rientra nel FIFO. I binding EFB vengono cercati con l'alias effettivamente risolto. Le dimensioni 1024 non subiscono più il wrap della maschera a dieci bit.

Il percorso conserva il fallback per indirizzi non validi, resolver assente e sorgenti raw indicizzate che richiedono una traduzione TMEM/TLUT non ancora validata. La patch non dichiara supporto generale alle palette raw. La cache dei binding risolti include anche la provenienza raw/nativa, così una transizione non riusa una texture precedente.

Nel runtime WiiCompiled i display list mantengono, nella scansione già esistente, la maschera delle unità texture toccate. Prima dell'invio raw viene invalidato il solo binding nativo interessato: `texture A → BP raw B → texture A` deve emettere nuovamente A. Le unità estranee restano riutilizzabili. Un bind registrato in un display list non può essere soppresso usando lo stato corrente e non viene pubblicato come bind già eseguito. I parser BP immediati applicano la stessa invalidazione.

`GXApplyBPReg` conserva il comando nel display list oppure lo accoda quando il worker asincrono possiede lo stato GX. La mutazione avviene sul consumer nell'ordine FIFO; il percorso sincrono continua a essere immediato. Non è stata aggiunta un'attesa per registro. I test verificano le transizioni raw/native, le maschere BP, le otto unità, l'alias EFB, l'ordine asincrono e i limiti degli intervalli MEM1/MEM2.

## Verifica locale corrente

| Verifica | Esito |
| --- | --- |
| WiiCompiled host | 34/34 PASS |
| Aurora incorporata host | 26/26 PASS |
| Aurora standalone host | 21/21 PASS |
| WiiCompiled Vita GXM | ELF, SELF e VPK completi |
| Aurora standalone Vita GXM | Librerie e probe completi |
| Audits del nuovo WiiCompiled | Collegamento GXM/runtime/CPU/assets/FIFO, integrità SELF nel VPK e SFO superati |
| Installer con manifest nuovo | Controllo locale superato |
| Precomputed geometry e coda GXP con ASan/UBSan | PASS |
| Installer Python | 21 test PASS |
| Cattura hardware Python | 12 test PASS |

Gli eseguibili host sono stati ricompilati dai sorgenti recuperati. Il primo oracle PAL era incompleto perché specificava il numero dei texgen senza inizializzarli; la versione finale contiene anche i generatori XF e passa. Questo errore di fixture non è presentato come bug del renderer.

SELF corrente con correzione BP: `73b644bd59e1acdead673ddf51ff2af53f087f8a51b2958c9d318a32f68fa258`, 24.408.161 byte. VPK: `b42a57e294b84265f96c1b09b0b05c56b6e4ebd2b88debd01344a9af36ef3f8b`. Digest sorgenti/input/tooling: `972aeeddb9fb86d7af24574c8a6b3ccf185f2c64d33c0d570adbf87978dbe7e4`. Il manifest completo è in `build/implementation-20261012/vita/DELIVERY.json`; log e diff sono in `build/implementation-20261012/`. Il controllo d'identità non è stato aggirato.

La prima tranche usava il SELF `e627e927cf8234f906fa4d602c8b36f3aae3fa9938159c9286f736af7f574a8d`, 24.403.061 byte. Dopo la modifica al solo tooling di cattura quel SELF era rimasto identico, con digest sorgenti/tooling `bd596624a8cf9543d4484cb69f21eabfbfe85d01017fcf2305ec540f4ee0b5ef` e VPK `c0461564f31bfc0b3d01baa0b3c300015e72a1b6c76494959ec6215aeb1d8803`. La coppia precedente ELF/SELF/map/VELF/VPK/manifest è conservata in `sealed-candidate-e627/`; le sue prove restano associate a quella versione.

## Stato della verifica grafica

Il candidato è stato avviato su Vita3K, verificando dal boot il SELF e i byte TOML caricati. La cattura dell'introduzione al frame 300 contiene 794 record draw e 78 texture; quella di gara al frame 700 contiene 674 record draw e 83 texture. Entrambe sono complete, con zero record persi. Prima della seconda cattura, il frame 697 registra `stage=2`, le tre pause a zero, 306 frame di gara e controller registrato/attivo `0x124`. Questo dimostra progressione sull'emulatore, senza dimostrare pixel, input fisico o FPS Vita.

I contatori confermano l'uso del percorso M06 sull'emulatore: 13 draw precomputati nella finestra di introduzione e 17 in gara, con hit e senza miss/fallback nelle finestre registrate. Non esiste ancora un A/B quiet hardware per attribuirgli un guadagno.

Il runner iniziale si fermava appena salvati i CSV, mentre il readback era ancora in corso, e poteva copiare una PPM precedente. La prova ripetuta attende il marker `frame=N saved=1` della sessione corrente. Le PPM così confermate restano interamente nere, tutte e 522.240 le terne RGB a zero. La cattura della finestra tramite macOS fallisce; non è stata presentata un'immagine della finestra launcher come prova dei pixel del gioco.

Nel sorgente locale Vita3K la sincronizzazione delle superfici Vulkan verso memoria guest dipende dal mapping. Il ramo `__APPLE__` forza Vulkan e disabilita il mapping per MoltenVK, anche quando il YML chiede `double-buffer`. I log delle prove annunciano effettivamente `Disabled`; la variante YML che chiedeva OpenGL eseguiva ancora Vulkan. Questi esperimenti non costituiscono confronti fra backend o mapping differenti. Il probe Aurora con cubo/colori noti riproduce il limite del readback; il suo contatore vicino a 60 FPS è un risultato dell'emulatore su due draw, senza valore probatorio per Mario Kart sulla Vita. Il checkout locale Vita3K è `ca2daeae8e949a9bd7eba56b4c2b639afe5b1a0e`, mentre l'app installata dichiara 4153/741b19ac: l'attribuzione al ramo Apple è coerente con i log, non una prova di identità fra quei sorgenti e l'eseguibile.

L'auditor non trova nei binding catturati la firma TEV o le impronte sorgente dei materiali Mario inventariati, neppure nella cattura di gara. L'estensione offline a 32 archivi `ma_kart-*` non identifica il corpo nei binding della cattura di introduzione. Questo restringe il prossimo controllo a produzione/materiale runtime, draw scartati e provenienza prima del binding; non prova un bug del decoder né la proprietà dei pixel bianchi.

Configurazione, file diagnostici e SELF preesistenti dell'emulatore sono stati ripristinati e verificati dopo ciascuna prova. Evidenze: `vita3k-candidate-complete/VERIFIED_CAPTURE.json`, `vita3k-active-race/VERIFIED_CAPTURE.json` e rispettivi CSV/log sotto `build/implementation-20261012/`.

## Prima prova PS Vita e ripristino

Il candidato è stato installato atomicamente sul dispositivo `192.168.1.79:1337`, con readback del SELF uguale a `e627e927…574a8d`. Profilo diagnostico Time Trial, cattura richiesta al frame 900, precomputed geometry OFF, async EFB OFF e deferred targets OFF; SHA-256 TOML temporaneo `c1ee5c718615aaecff4926b593d882a13b7909615ca5a9834209b53185778803`.

Companion ha risposto al comando di avvio, ma in 1.804,8 secondi il runtime è rimasto esattamente a 91.871.516 byte, senza un nuovo boot registrato. Il successivo riscontro corretto dell'utente conferma che **la partita del tentativo del candidato era entrata ed era visibile**, con **texture bianche, HUD assente e forte lentezza**. L'assenza del marker non dimostra quindi un mancato avvio. L'associazione al candidato è fondata sull'installazione verificata e sulla cronologia confermata dall'utente; manca ancora l'identità runtime. Non sono stati acquisiti frame 900 o un benchmark quiet hardware. Resta da spiegare perché quella esecuzione non abbia prodotto un nuovo log nella posizione monitorata.

Terminato il tentativo con `kill WICMKW001`, sono stati ripristinati atomicamente e riletti sia il SELF precedente sia il TOML originale. Stato finale verificato:

- SELF: `7e35227b1190bb6cbc1298c12dc3b6386fd492ab104379650fb3d3160c18559c`, 25.212.726 byte.
- Configurazione: `2f683047cd5a10666585b483af872fae10c68b1c2bb196cd504402fddf7e8e83`.
- Primo candidato non promosso sul dispositivo; ELF/SELF/VPK, backup e receipt conservati localmente.

I record dell'installazione sono in `vita/device-installs/20261012T153016Z/`; `DEVICE_LAUNCH.json`, `DEVICE_MONITOR.json`, `DEVICE_STOP.json` e `DEVICE_FINAL_STATE.json` conservano separatamente lancio, assenza di progressione e ripristino. Nessuna misura del vecchio runtime viene attribuita al candidato.

Dopo la correzione dell'utente è stata acquisita anche una sessione arrivata più tardi nel log. Essa annuncia `fast_ram_ranges=1` e `native_call_profile=1`, mentre il primo candidato è compilato con entrambe le opzioni a zero; manca inoltre il marker d'identità. Questa sessione appartiene all'eseguibile precedente e non viene usata per quantificare il candidato. `DEVICE_LATE_LOG_ATTRIBUTION.json` conserva la distinzione rispetto al riscontro qualitativo dell'utente.

## Seconda prova PS Vita: identità verificata

Il candidato con correzione BP è stato installato e riletto con successo, receipt `vita/device-installs/20261012T174009Z/`. L'utente ha confermato il gioco chiuso; l'errore Companion nel tentativo di chiusura era coerente con l'assenza di un'app da terminare. Il successivo avvio ha prodotto un nuovo boot e il marker runtime conferma esattamente SELF `73b644bd…fa258`, TOML `79a18293aa4b10355ac2cd558bf6a9841782a75462bc3b5d1c079d44748928a7`, CPU 444 MHz e GPU 222 MHz.

Il profilo è Time Trial diagnostico, cattura al frame 700, batching ON, precomputed geometry OFF, async EFB OFF, deferred targets OFF e LYT nativo OFF. L'identità è in `device-raw-bp/BOOT_IDENTITY.json`; la verifica grafica e la raccolta della cattura sono in corso. I contatori di questa prova diagnostica non costituiscono un benchmark quiet né una dimostrazione del target.

## Lavoro ancora necessario

Restano aperti l'identità e la raccolta del log dell'esecuzione hardware, l'identificazione del materiale bianco, la produzione/visibilità dell'HUD, il readback attendibile, le copie depth necessarie e l'attribuzione del costo CPU/GX. L'ingresso in partita è confermato dall'utente, ma non c'è un miglioramento grafico o prestazionale dimostrato. Non è stato ricostruito byte per byte il sorgente perso delle vecchie build. I percorsi M02 multi-draw/template, LYT nativo, primitive native e retirement EFB differito richiedono implementazione e test prima di riabilitare i loro controlli.

La consegna locale è una base di sviluppo nuovamente compilabile. Non costituisce una dichiarazione di raggiungimento dei 60 FPS.
