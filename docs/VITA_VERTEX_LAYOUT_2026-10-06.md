# WiiCompiled Vita: triangolo bianco e cache del formato dei vertici

Il test manuale della build `psarc-input-memory-20261006` conferma che i
comandi rispondono e permettono di avanzare nei menu. L'audio è riconoscibile;
la grafica mostra un triangolo bianco su fondo nero. La cattura del framebuffer
riproduce questo difetto. Non è stata verificata una gara completa.

È stata corretta una mancata invalidazione della cache di decodifica dei
vertici in Aurora. La candidata successiva è
**`psarc-vertex-layout-20261006`**. La correzione è verificata sul computer;
la sua efficacia visiva richiede una nuova prova sulla Vita.

Aggiornamento del 7 ottobre: la build è ora installata e verificata sulla
Vita, con 14/14 file identici al VPK. Nessun avvio automatico del gioco.
[Registro della consegna](VITA_VERTEX_LAYOUT_DELIVERY_2026-10-07.md).

## Evidenza del dispositivo

Acquisizione del 6 ottobre, 21:33 UTC, da `192.168.1.79:1337`:
`device-runs/input-memory-20261006T213336Z`.
Sono conservati il log completo, la sessione isolata per marker, lo SFO,
l'eseguibile installato, i metadati appmeta protetti, la configurazione e
le catture/telemetria Aurora. Nessun comando di avvio del gioco è stato inviato.

Il SELF riletto corrisponde esattamente alla candidata input/memoria:
`e8a4fa422beaa08350a12c8634ebd18b7d6a97e37d39b62eeebdc85dccc026d1`.
Lo SFO dell'app contiene `ATTRIBUTE2=12`. I metadati appmeta sono cambiati
rispetto alla precedente acquisizione dell'installazione. La memoria utente
libera prima di inizializzare il renderer è **185.597.952 byte**, contro
71.303.168 byte nella sessione precedente. Questo conferma il cambiamento
dei metadati registrati e del budget disponibile; il file appmeta protetto
non è stato decifrato per leggere direttamente i suoi attributi.

Config.toml è identico alla consegna precedente:
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
Il gioco continua a caricare dal PSARC. Nel campione a frame 900 sono registrate
28 letture, 23.193.237 byte e zero fallimenti.
L'elenco dei dump contiene solo gli otto dump già precedenti alla build
input/memoria; non è comparso un nuovo file nell'acquisizione.

Le transizioni dei tasti sono osservate dal log, compresi Cross → PAD A,
Circle → PAD B, Square → PAD X, Start → PAD Start e direzione sinistra.
L'avanzamento dei menu è una conferma dell'utente. Queste prove non equivalgono
alla validazione di tutti i comandi durante una gara.

Il campione audio ha accepted_samples = consumed_samples = 374.976,
queued_samples = 0, rejected_blocks = 0 e output_errors = 0.
Il contatore underruns è cumulativo e include il periodo iniziale prima della
produzione audio: da quel numero non si deduce la continuità della musica.

## Difetto grafico osservato

La coverage contiene **1.451 errori `vertex decode failed`**.
Nel campione di frame 899 sono stati preparati 49 disegni e completati
13 disegni / 52 vertici / 26 triangoli; 36 disegni sono rifiutati.
Nello stesso campione fallback_tex = 0 e shader_fail = 0.
La mancanza di texture non è quindi indicata come causa di quel campione.
Questi contatori non provano che tutte le texture o tutti gli shader del gioco
siano corretti.

La cattura `frame-589.ppm` è interamente nera. `frame-979.ppm` mostra il
triangolo bianco e pochi pixel residui del menu: 119.187 pixel bianchi,
402.908 neri e 145 di altri colori, su 960×544 pixel. Le copie PNG servono
solo a visualizzare i PPM originali, che sono conservati senza modifiche.

## Causa nel codice e correzione

I registri CP VCD low/high e VAT A/B/C definiscono gli attributi presenti e
il formato dei loro componenti. Quando cambiano, `handle_cp` aggiorna
`pipelineStateGeneration` e chiama `GXState::clearVtxSizeCache()`.
Quella funzione invalidava soltanto il calcolo della dimensione del FIFO.

`DrawSink::submit` può mantenere la decodifica dei vertici anche quando
ritraduce una pipeline: la ricostruisce soltanto se cambia il numero VAT,
`layoutStateGeneration`, o manca una cache iniziale. I cambi VCD/VAT non
aggiornavano quest'ultima generazione. Il lettore FIFO poteva quindi usare
la nuova dimensione mentre il decoder usava attributi, offset e stride
precedenti. Una riduzione del formato provocava il rifiuto del disegno;
un aumento poteva essere accettato leggendo confini di vertice sbagliati.

La correzione in `aurora-main/lib/gx/gx.hpp` invalida anche la generazione
del layout e il dominio Layout insieme alla dimensione FIFO. Tutti e cinque
i gruppi CP passano già per questa funzione. La deduplicazione delle scritture
identiche resta attiva. I cambi della sola pipeline TEV continuano a poter
riutilizzare il layout quando i vertici non cambiano.

Il difetto è riprodotto sul computer e compatibile con gli errori e la
geometria osservati sulla console. Non si attribuisce al difetto ogni
artefatto visivo senza provare la build corretta sul dispositivo.

## Verifica e confronto successivo

Il test in `aurora-main/tests/vita_frontend_translation_test.cpp` usa
pacchetti CP reali, il decoder FIFO reale e il DrawSink reale. Copre i cinque
gruppi di registri, la riduzione e il ripristino del formato sullo stesso VAT,
e le scritture duplicate. Prima della correzione, la prima versione del test
ha prodotto **35 controlli falliti**, inclusi disegni rifiutati in tutti e
cinque i casi. La suite finale e i controlli del binario sono registrati
insieme alla candidata descritta sotto.

La nuova build mantiene il percorso GameCube, `ATTRIBUTE2=12`, il caricamento
PSARC e gli esperimenti prestazionali disattivati. L'unico cambiamento al
rendering è l'invalidazione della decodifica VCD/VAT. Non sono stati cambiati
TEV, ordine FIFO, copie EFB, reset di stato BeginScene o scissor.

Nel prossimo test sul dispositivo confrontare la schermata iniziale e i
menu con le catture precedenti; verificare il conteggio degli errori di
decodifica, dei disegni e delle texture di ripiego. Recuperare log e nuove
catture abbinandoli al SELF installato. Eventuali problemi residui di
proiezione, texture, TEV o EFB devono essere analizzati sul nuovo campione.
Gare complete, effetti, salvataggi, sospensione/ripresa e prestazioni restano
da verificare. Le catture inseriscono sincronizzazioni GPU: questa candidata
diagnostica non fornisce una misura neutra degli FPS.

## Identità della nuova candidata

Build nativa completata e **18/18 test Aurora superati**. L'audit del binario
ha controllato 45.013 simboli eseguibili, trovato disegno/presentazione GXM
e verificato l'assenza di entry point o librerie GL/vitaGL/vita2d.
I controlli ARM confermano ancora i collegamenti reali pthread/TLS/SceFiber
e i confini nativi di input, log e attese. Entrambi i VPK prodotti contengono
il SELF corrispondente e lo SFO corretto con `ATTRIBUTE2=12`.

Archivio della candidata:
`build/vita-psarc/candidate-vertex-layout-20261006T214342Z`.
Include VPK, SELF, ELF, map, VELF, sorgenti della correzione, patch, risultati
dei test, log della build, controlli del binario e riferimenti all'evidenza
del dispositivo. `CHECKSUMS.json` sigilla i file. Alla creazione di questo
archivio la candidata era pronta localmente e non ancora installata.
L'installazione successiva del 7 ottobre è conservata in un archivio separato
e nel [registro della consegna](VITA_VERTEX_LAYOUT_DELIVERY_2026-10-07.md).

| Artefatto | SHA-256 |
| --- | --- |
| VPK gioco | `f5d20bc8de67c59c7e80c5857d2398ada76a31b816b49bfb5b655a6b3a7b449b` |
| SELF gioco | `3bd1e21c1acfb1dec669c699fa194cd4b91393952e560dd4c98b3572f766240e` |
| ELF gioco | `0adb698de8a26e5829c983c72a01d8574323de80234e193013134b1de9288082` |
| Map gioco | `6fa12756b025d36df72e80d398df9e8aee7bf9717d0488244981b15279e62395` |
| VELF gioco | `74dd29ecce606668d1bf24ab080d6418142885d36b092acca9d56280a2d95b22` |

VPK: 22.747.883 byte. SELF: 22.949.579 byte.
Digest sorgenti e input generati:
`8e462f78d247a992f5287dda348340b78d688b236c529171056c6bbbeaa3e49e`.
Il digest comprende anche l'albero di lavoro locale; il solo commit non
identifica la candidata. WiiCompiled HEAD è `279ce8328f17059a4b46172d4ce71d3f403a1aec`,
Aurora HEAD è `28512bd75531508276be1582882145ea021151eb`, entrambi con modifiche
locali registrate nel manifest. La build precedente e gli artefatti abbinati
alle sue catture sono conservati separatamente.
