# WiiCompiled Vita: costo dei menu, perdita audio e gara nera

## Esito

La build corrente mostra un costo prevalente nel percorso CPU che produce e
traduce GX nei menu osservati. La gara nera coincide con errori del backend
GXM e con un contratto di avvio del frame asincrono che può innescare retry
incoerenti dopo un errore. Non è una misura della capacità della GPU di
renderizzare una gara corretta, né dimostra che Mario Kart sia irrealizzabile
sulla Vita. I 30 FPS non sono attualmente dimostrati o garantiti.

L'audio non mostra la morte del thread di uscita: il contatore degli underrun
continua a salire, senza errori di `sceAudioOutOutput`. Il runtime disabilita
il DMA dopo un invio fallito. Un blocco rifiutato dalla coda e il percorso
sorgente indicano fortemente che un rifiuto temporaneo viene trattato come
errore definitivo; il risultato booleano non registra la causa del singolo
invio, quindi la correlazione non sostituisce una nuova diagnostica tipizzata.

## Identità ed evidenza conservata

- Dispositivo: `192.168.1.79:1337`, applicazione `WICMKW001`.
- Marker: `psarc-resident-assets-async-oom-20261008`.
- SELF riletto integralmente dalla Vita:
  `6742917608521bef5f9f57be62ab2e7153c93b2cb53e2fd33980850dd6b49f6c`.
- Configurazione riletta:
  `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
- Installazione: `build/vita-psarc/install-resident-assets-async-oom-20261008T084146Z`.
- Candidata e binari per simbolizzazione:
  `build/vita-psarc/candidate-resident-assets-async-oom-20261008T082910Z`.
- Cattura: `device-runs/resident-menu-race-20261008T085716Z`.
  Contiene SELF, Config.toml, log della sessione corrente completo dal marker,
  coda del log cumulativo con offset e metadati, log memoria, coverage,
  draw trace, manifest e analisi numerica.
- Nessun nuovo coredump nella lista letta durante questa cattura. Il dump delle
  08:00 del dispositivo appartiene alla precedente build, non a questa prova.
- GX asincrono realmente avviato: `[gx-worker] requested=1 running=1`.
- Geometria residente e snapshot texture attivi; SFO della consegna con
  `ATTRIBUTE2=12`. Il PSARC è lo stesso dell'installazione precedente.

La cattura è in sola lettura: nessuna installazione, modifica della
configurazione, richiesta di avvio o arresto del gioco. L'utente ha attraversato
i menu manualmente. La cattura FTP finale avviene mentre il gioco è ancora
aperto; non costituisce una misura quiet o un confronto A/B ripetuto. I log
sono di una build con diagnostiche attive. L'associazione esatta di ogni
intervallo al nome di una schermata non è automatizzata.

## Costo continuativo prima degli errori in gara

Cinque finestre con endpoint 932, 953, 974, 995 e 1016 contengono 105 frame in
52,089322 secondi. Non contengono le catture one-off ai frame 30, 180 e 600,
né il successivo ingresso nella gara.

| Misura | Risultato | Significato |
| --- | ---: | --- |
| Cadenza interna osservata | 2,016 Hz | Circa 496,09 ms per frame nella build diagnostica |
| Tempo trascorso nella lavorazione FIFO | 202,35 ms/frame | Decode/render preparation, invio e possibili attese incluse; non tempo GPU puro |
| Percorso dei job sui vertici | 71,04 ms/frame | Parte annidata del lavoro grafico |
| Lavoro della lane chiamante sui vertici | 59,29 ms/frame | Timer trascorso, non misura dell'utilizzo hardware del core |
| Job sui vertici | 267.438 / 105 frame | Circa 2.547 job per frame |
| Elementi lavorati dagli helper 1/2/3 | 0 / 0 / 0 | Il pool esiste ma non parallelizza questi piccoli job |
| Upload degli snapshot a inizio/fine finestra | 64 / 64 | Nessun nuovo upload nel sottoinsieme delle texture piccole residenti |

I tempi sono annidati e possono sovrapporsi: **non si sommano**. La differenza
fra tempo frame e tempo FIFO non va attribuita interamente al guest PPC: include
il resto del runtime, scheduling e attese non separate da questi contatori.

In finestre precedenti alla gara, il renderer riporta medie della coda display
di 29–31 microsecondi e durata del frame di circa 394–491 millisecondi, con
`blocked_pct=0`. La presentazione finale non spiega il rallentamento continuativo
osservato. Altri intervalli registrano attese sulla coda: questo non esclude
costi GPU o sincronizzazioni in altre fasi. La classificazione automatica
`gpu_backpressure` scatta con una soglia di 500 microsecondi su almeno il 10%
dei frame: da sola non misura né dimostra un limite di fill rate della GPU.

Il frame 900 contiene 2.503 draw e 15.400 vertici. La soglia del pool è 512
elementi per job: distribuire separatamente migliaia di draw di pochi vertici
aggiungerebbe costi di scheduling. Serve ridurre/aggregare il lavoro prima di
distribuirlo. `gxm_local_draw_batching` è attualmente disabilitato.

L'intervallo 932→1016 registra 187.151 nuovi hit degli snapshot, senza nuovi
upload. Il risultato dimostra il riuso di quel sottoinsieme. Non dimostra
che tutte le texture siano residenti: il limite sorgente è 4.096 byte e i video,
gli aggiornamenti dinamici, i target EFB e le texture più grandi seguono altri
percorsi. La cache non elimina traduzione dello stato, draw, uniformi, lavoro
del guest e preparazione dei vertici.

Rispetto a 33,33 ms per frame, i 496,09 ms osservati corrispondono a circa
15 volte il budget. È il rapporto di questa esecuzione diagnostica, non una
stima garantita del guadagno possibile.

## Audio: DMA disabilitato dopo un rifiuto

`runtime-session.log:737` registra:

```text
[audio] Audio: failed to read DMA buffer; disabling audio DMA
```

I successivi contatori restano a 647.616 campioni accettati e consumati, con
`rejected_blocks=1`, coda vuota e `output_errors=0`. Gli underrun salgono da
6.496 a 70.658: il thread di uscita continua a produrre silenzio.

`vita/game_audio.cpp::enqueue` restituisce `false` se la coda non ha spazio.
`runtime/src/hle/audio/audio.cpp::PushAudioBlock` propaga quello stesso booleano,
oltre a usarlo per errori di accesso alla RAM guest e altri rifiuti del backend.
`Audio_HLE_Tick` interpreta qualsiasi `false` come lettura DMA fallita e imposta
`g_ai.enabled=false`, prima di eseguire il callback del blocco. Il messaggio
attuale non distingue le cause. Non risultano errori di uscita Vita o una
terminazione del thread in questa sessione.

Correzione da implementare: risultati distinti per invio riuscito, coda piena,
buffer invalido e backend non disponibile; preservare callback/stato DMA quando
un blocco valido deve essere scartato per saturazione. Verificare il recupero
con una coda deliberatamente piena, senza nascondere gli errori reali di memoria.

## Ingresso in gara: compilazione e fallimento del renderer

Il frame 1122 dura 41,291 secondi, di cui 40,825 nel timer FIFO; il 1123 dura
21,154 secondi, di cui 20,524 nel FIFO. Sono stall di caricamento/prima
preparazione, non la prestazione di una gara stabile.

La cache preriscalda 192 pipeline all'avvio ma la sessione compila almeno
128 ulteriori shader stage. I campioni pubblicati costano 368,5–1.018,8 ms
ciascuno. Il log è limitato ai primi otto e alle potenze di due: questi valori
non sono il totale di tutte le compilazioni. Le texture precalcolate nel PSARC
non eliminano i miss della cache dei programmi GXP.

Prima del nero compaiono `create offscreen sync (0x805b0017)` e
`invalid native texture binding`; seguono ripetuti `invalid begin_frame`.
Il VitaSDK locale identifica `0x805b0017` come `SCE_GXM_ERROR_DRIVER`, non
come il codice specifico di memoria esaurita. La causa interna del driver non
è determinabile da quel codice soltanto.

Il conteggio massimo è 451 scene in un frame, contro il valore configurato 5.
Questo è un conteggio aggregato: non dimostra da solo il superamento delle
scene del singolo render target. Serve tracciare scene per target, target/sync
vivi, picchi, riuso e distruzione. La memoria libera o il margine nel pool CDRAM
non escludono limiti distinti delle risorse del driver.

Il percorso asincrono aggiunge una criticità verificabile nel sorgente:

1. `aurora_vita_backend.cpp::begin_frame` accoda il nuovo begin ma restituisce
   immediatamente il risultato del frame precedente.
2. Dopo un fallimento precedente, il chiamante può ricevere `false` anche se
   il worker apre correttamente il frame appena accodato.
3. Il chiamante ritenta `BeginAuroraFrame`, accodando un secondo begin sul
   frame già aperto; `gxm_renderer.cpp::begin_frame` lo rifiuta.

Questo interleaving è consentito dal contratto attuale. I log sono compatibili
con esso, ma non contengono ID degli ack sufficienti per provarne ogni passaggio
sulla console. Correzione proposta: un unico begin per frame e conferma del
worker prima di comunicare il risultato al chiamante, mantenendo decode e
rendering asincroni durante il frame. Servono prove di errore→recupero e retry.

Dopo un frame fallito, `end_frame_now` non pubblica una nuova performance
snapshot. Il wrapper legge comunque l'ultima snapshot valida; per questo
`drawn_frames`, `completed=1` e alcuni contatori congelati possono continuare
ad apparire nel log della schermata nera. Non vanno usati per misurare i frame
realmente renderizzati in gara. Anche questa validità va resa esplicita.

## Ordine del lavoro successivo

1. Riparare la gestione del rifiuto audio, con risultati distinti e prova di
   saturazione/recupero. Il thread di uscita esistente è già avviato.
2. Correggere ack/retry del begin asincrono e pubblicazione delle snapshot
   fallite; diagnosticare e limitare il ciclo di vita di target/sync EFB.
3. Preriscaldare/consegnare i programmi GXP necessari alla gara e misurare
   separatamente caricamento freddo, cache calda e funzionamento continuativo.
4. Sul renderer stabile, attribuire guest PPC, frontend GX, job sui vertici,
   uniformi/invio, finish per causa e GPU. La telemetria esiste ma le righe
   periodiche complete non sono presenti nella cattura corrente; non
   sostituire la misura con la classificazione della sola coda display.
5. Ridurre draw e lavoro ripetuto, aggregare solo chiamate compatibili senza
   attraversare clear/copy/cambi target/barriere, specializzare i percorsi
   caldi. Abilitare una variante alla volta con fallback recuperabile.
6. Misurare controlli/candidate senza diagnostiche invasive sulla stessa
   schermata/gara, con identità, configurazione e clock confermati. Solo
   questa prova può stabilire quanto manca ai 30 FPS e il limite GPU reale.

In questa analisi non sono state implementate o installate nuove correzioni.
