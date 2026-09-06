# Audit producer GX, cache e scheduling — 2026-09-06

Analisi in sola lettura del codice e dell'ultima sessione di `build/vita/runtime.log`, avvio con `clip_w=1` a riga 5128 e ultimo producer a riga 5784. Nessuna modifica al runtime effettuata per questo audit. Le misure sono del log hardware fornito; il codice suggerisce ulteriori cause da misurare, non risultati già ottenuti.

## Il tempo perso non è soltanto GPU

| Campione dell'ultima sessione | Intervallo producer | Attesa renderer registrata | Intervallo meno attesa | Copia packet corrente | Renderer |
|---|---:|---:|---:|---:|---:|
| serial 600, 96 draw / 716 vertici | 62,373 ms | 0,006 ms | 62,367 ms | 0,135 ms | 4,345 ms, riga parzialmente corrotta |
| serial 900, 1431 draw / 10452 vertici | 154,481 ms | 32,353 ms | 122,128 ms | 1,202 ms | 41,592 ms |
| serial 1200, 2304 draw / 15188 vertici | 210,252 ms | 36,488 ms | 173,764 ms | 1,881 ms | 52,648 ms |
| serial 1500, 2272 draw / 15608 vertici | 213,089 ms | 39,448 ms | 173,641 ms | 1,853 ms | 52,043 ms |
| mediana serial 1655–1691, 37 campioni | 1495,397 ms | 1132,575 ms | 358,624 ms | 5,504 ms | nessun riepilogo disponibile dopo 1500 |

Per l'ultima riga le mediane sono calcolate indipendentemente sulle colonne, quindi non vanno sottratte fra loro. Intervalli finali: minimo 1472,002 ms, massimo 1672,568 ms; draw 6149–6161, vertici 38636–38716. La media dell'intervallo è 1497,751 ms, pari a circa **0,668 frame prodotti/s**. La coda richiede solo 1–2 microsecondi in tutti questi 37 campioni.

`SubmitFrame()` misura l'intervallo fra gli ingressi successivi, non un tempo CPU esclusivo (`vita/gx_backend.cpp:5056`). La differenza con `prior_wait_us` comprende codice guest, HLE, scheduling, I/O e lavoro del submit precedente. La copia del packet corrente avviene dopo l'ingresso e non va sottratta come se fosse una fase dello stesso intervallo. Anche `prebegin_us` è tempo trascorso prima del primo GXBegin, non profilazione esclusiva di una funzione (`runtime/src/hle/gx/gx_dl.cpp:1674`).

Con questi limiti, il dato resta decisivo: eliminare tutta l'attesa del renderer lascerebbe comunque centinaia di millisecondi non spiegati nel carico finale. Ottimizzare soltanto il formato dei vertici o il numero delle chiamate GPU non può dimostrare 60 FPS. Occorre misurare separatamente tempo CPU guest, tempo CPU render e attesa GPU.

Non sono confronti controllati della stessa schermata: i draw richiesti passano da 179 a circa 6165. Il log dimostra scene più costose e saturazioni, non una perdita di memoria per ogni ingresso nel menu. Servono identità della scena e un percorso A→B→A ripetuto per distinguere nuovi contenuti da risorse non ritirate.

## Difetto concreto: servizio VI/audio durante WaitRender non collegato su Vita

`vita/gx_backend.cpp:5243` attende il completamento del serial precedente. Ogni timeout di 1 ms richiama `g_waitCallback`, **solo se impostata**. Il puntatore parte nullo (`:1002`).

L'unico chiamante di `aurora_set_frame_worker_wait_callback(...)` nel runtime è `runtime/src/main.cpp:1542`; la callback (`:76`) esegue `VI_HLE_ProcessRetracesDeferred(8)`, allarmi e audio. Quel main desktop è escluso da `Makefile.vita:39`. L'entry Vita (`vita/main_vita.cpp:180`) inizializza il renderer e installa gli hook delle scritture guest (`:192`), ma non la callback di attesa. Il commento a `:187` documenta espressamente il bypass del main desktop.

Quindi durante l'attesa GX finale di circa 1,13 secondi/frame il percorso previsto per servire VI/allarmi/audio non viene eseguito. Nel log `vi_stall` l'asse temporale VI accumula un ritardo: circa 12,46 secondi al serial 978, 55,92 secondi a 1559 e 121,81 secondi a 1685. `advance_enter == advance_complete`, quindi non è evidenza di un callback VI bloccato. Il servizio esistente recupera al massimo otto retrace per chiamata (`runtime/src/hle/vi.cpp:422`), e numerosi frame registrano esattamente otto retrace elapsed.

**Intervento prioritario:** collegare nel main Vita una callback equivalente, sul thread guest e con il medesimo isolamento dei registri/deferimento dei cambi di fiber; coprire anche i percorsi raw-DL che bypassano GXBegin. Il servizio durante GXBegin esiste (`runtime/src/hle/gx/gx_vertex.cpp:242`), ma non rappresenta tutti i draw ora accelerati. Misurare durata/numero dei callback e debito VI prima/dopo, verificando input, audio, scene e stato guest. Non eseguire callback guest sul thread GPU e non recuperare un arretrato arbitrario con un ciclo senza limite.

Il mancato collegamento è dimostrato dal codice. Quanto contribuisca agli FPS, al costo dei callback successivi e alla simulazione resta da misurare. Ripristinare il servizio può aumentare temporaneamente il lavoro guest dentro l'attesa: non è di per sé un'accelerazione del renderer.

Il dimezzamento osservato non è spiegato da un attesa vblank esplicita: nell'ultima sessione i campioni `vi_perf` mostrano `pace=0`, `pace_us` dell'ordine di 7–13 µs; la politica presenta subito quando il frame è in ritardo (`runtime/src/hle/vi.cpp:645`).

## Le ottimizzazioni già efficaci non sono la priorità principale

Ai serial 900 / 1200 / 1500:

- Cache layout: 1457/45, 2183/43, 2362/59 hit/miss, circa 97–98% hit.
- Cache raw mesh: 1289/213, 2010/216, 2010/411 hit/miss, circa 85,8% / 90,3% / 83,0%; invalidazioni osservate **zero** in tutti e tre i campioni.
- Trasformazioni/raster/texture riusati per circa l'88–93% delle catture di stato.
- Draw GPU fisici 93 / 151 / 132 a fronte di 1431 / 2304 / 2272 draw registrati.
- Copia packet nell'ordine di 1–2 ms in questi menu; circa 5,5 ms nel carico finale. È migliorabile, ma non rappresenta il secondo di attesa.

La coda a due slot non produce l'attesa misurata: `queue_wait_us` è quasi nullo. La serializzazione avviene prima tramite `GXDrawDone()` → `WaitRender()` (`vita/gx_backend.cpp:5508`). Anche i frame con EFB richiedono una barriera del frame (`:5129`). Aumentare gli slot senza correggere proprietà e dipendenze delle risorse non elimina questi vincoli.

I contatori sono azzerati e gli array delle catture di stato svuotati in ogni `SubmitFrame()` (`:5171`). Non emerge un accumulo illimitato dei packet lato producer fra un menu e il successivo. Capacità trattenuta dai vettori e cache globali vanno contabilizzate, ma non equivalgono automaticamente a una perdita.

## Due soglie delle cache possono creare una discontinuità evitabile

1. `StoreRawMeshCache()` (`vita/gx_backend.cpp:2908`) svuota **tutta** la cache quando la nuova allocazione oltrepassa 4 MiB. La cache ha 2048 record in 512 set a quattro vie e LRU solo all'interno del set (`:2723`). Un overflow di budget può quindi cancellare molti mesh ancora utili e riattivare decodifica/allocazioni. La politica è certa; che sia stata attivata nella coda finale non è dimostrato: mancano contatori di svuotamento e il successivo summary sarebbe al serial 1800, mai raggiunto.
2. `StoreDlScanCache()` (`runtime/src/hle/gx/gx_dl.cpp:762`) svuota tutte le entry al limite di 8192 record o 8 MiB di payload. Anche qui può esserci un calo dopo cambi scena senza una perdita di memoria. Manca una misura delle soglie raggiunte e dei costi di ricostruzione.

**Intervento:** aggiungere occupazione, picco, motivo miss, espulsioni e svuotamenti; sostituire gli svuotamenti globali con espulsione incrementale delle entry meno recenti fino al rientro nel budget, con limite di lavoro per frame. Non aumentare ciecamente il budget della Vita. Conservare tutte le verifiche della validità dei dati guest.

Le dipendenze raw mesh sono oggi controllate sull'intero span dell'array, non sui soli indici effettivamente letti (`vita/gx_backend.cpp:2842`, `:2891`). Il tracker usa granuli da 64 KiB (`runtime/include/gx_guest_write.h:42`), quindi scritture indipendenti possono causare invalidazioni conservative. Possibile intervento successivo: dipendenze min/max degli indici o intervalli più precisi. **Priorità inferiore:** i tre summary disponibili hanno zero invalidazioni raw mesh, quindi questo non è il collo di bottiglia provato nei menu campionati.

## Misure necessarie prima di altri interventi CPU

Il flag `perf_log=0` riduce l'output, ma non elimina tutta la profilazione: `GX_HLE_RecordBeginCaller()` esegue un clock e ricerca il chiamante a ogni GXBegin (`runtime/src/hle/gx/gx_dl.cpp:1674`), e il servizio temporale consulta un secondo clock (`runtime/src/hle/gx/gx_vertex.cpp:247`). Nei frame oltre un secondo `producerCritical` stampa ogni frame anche con logging ridotto (`vita/gx_backend.cpp:5205`). Questo lavoro non è cronometrato separatamente; non attribuirgli il collasso senza A/B. La corruzione/interleaving di alcune righe dimostra inoltre che il log testuale non è un tracciato affidabile di tutti gli eventi.

Serve un ring binario a costo limitato, aggiornato ogni frame e scaricato dopo il test o su errore, con identificativo di scena e serial comune. Includere: delta `runClocks` dei thread guest/render (API presente nello SDK locale: `sceKernelGetThreadRunStatus`), tempo ed esito del servizio VI/audio, chiamate/tempo di decode DL e trasformazioni, wait GX con serial atteso, cache clear/miss/allocazioni, limiti dei packet. I tempi delle callback e I/O vanno distinti dalle funzioni tradotte.

Campionare i chiamanti guest con LR affidabile; poi intervenire sui pochi hotspot dominanti con implementazioni native equivalenti e NEON dove il carico è davvero vettoriale. La ricompilazione generale con ottimizzazioni più aggressive o fast-math non fornisce una previsione credibile del guadagno necessario. Anche eliminare integralmente i ~22 ms di `dl_us` osservati non risolverebbe i 174–359 ms residui degli ultimi carichi.

## Ordine raccomandato per questo sottosistema

1. Collegare e verificare il servizio temporale mancante e raccogliere campioni completi anche dopo il serial 1650, senza aspettare altri 300 frame.
2. Dopo la correzione delle risorse EFB/texture e del 3D/THP, misurare lo stesso percorso di menu A→B→A e distinguere warm-up, steady state e caricamenti. Vincolo di correttezza: nessun draw/copy perso per capacità; il serial 1585 supera già vertici e comandi EFB.
3. Eliminarne le espulsioni globali, se osservate; poi profilare il residuo producer con tempo CPU e hotspot reali.
4. Valutare packet ownership tramite scambio di slot/buffer per evitare la copia da ~5,5 ms, mantenendo validità degli snapshot, RAM guest e ordine EFB. Prima verificarne il guadagno rispetto ai costi di attesa e dei thread.
5. Solo con contenuto completo corretto e carichi misurati sotto 16,67 ms per il collo di bottiglia si può discutere di 60 FPS sostenuti. I dati attuali non permettono di prometterli né di dichiarare le fasi precedenti convalidate end-to-end.
