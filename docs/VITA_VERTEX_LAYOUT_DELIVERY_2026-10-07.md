# WiiCompiled Vita: build VCD/VAT installata, 7 ottobre 2026

La build **`psarc-vertex-layout-20261006`** è installata sulla PS Vita
`192.168.1.79:1337`, nell'app `WICMKW001`. Tutti i **14 file** dell'app
sono stati riletti integralmente e coincidono per dimensione e SHA-256
con i contenuti del VPK. Il gioco non è stato avviato automaticamente.

La build corregge la mancata invalidazione della cache del formato dei
vertici dopo le scritture CP VCD/VAT. La diagnosi, i 18/18 test Aurora
e i controlli del binario sono nel [report della correzione](VITA_VERTEX_LAYOUT_2026-10-06.md).
La successiva prova manuale del 7 ottobre conferma i menu iniziali corretti
e zero errori di decodifica dei vertici. Restano FPS bassi e rendering incompleto
della creazione della licenza, con Mii non visibili.
[Risultato e prossima candidata diagnostica](VITA_MII_RENDER_STATUS_2026-10-07.md).

## Installazione e conservazione

Rispetto alla build input/memoria cambia soltanto `eboot.bin`. Lo SFO
del VPK è byte per byte identico a quello già installato e contiene
`ATTRIBUTE2=12`; i metadati registrati appmeta sono rimasti identici
prima e dopo l'aggiornamento. Non è necessaria una nuova registrazione
per cambiare gli attributi del pacchetto.

Il nuovo eseguibile è stato caricato con un nome temporaneo e riletto
prima della sostituzione. La sostituzione usa rename, conservando il
precedente eseguibile, anch'esso riletto e verificato. Non è stato
sovrascritto in posizione l'eseguibile precedente.

Il comando mirato di chiusura `kill WICMKW001` ha restituito un errore
generico. Il [codice di Vita Companion 1.06](https://github.com/devnoname120/vitacompanion/blob/1.06/src/cmd_definitions.c)
usa quella risposta per ogni risultato negativo di
`sceAppMgrDestroyAppByName`; non distingue un'app inattiva da un altro
fallimento. La chiusura del processo non è quindi dichiarata confermata.
L'aggiornamento dei file è completato e verificato. Per provare il nuovo
eseguibile, chiudere l'eventuale sessione precedente e riaprire il gioco
dalla LiveArea. Non sono stati inviati comandi di avvio o di chiusura di
tutte le app.

Backup remoto verificato:
`ux0:/app/WICMKW001/eboot.bin.before-vertex-layout-20261007T065917Z`.
I 14 file precedenti dell'app, il log e la telemetria precedenti sono
conservati anche nell'archivio locale della consegna.

| Artefatto | SHA-256 |
| --- | --- |
| SELF installato e riletto | `3bd1e21c1acfb1dec669c699fa194cd4b91393952e560dd4c98b3572f766240e` |
| VPK sorgente | `f5d20bc8de67c59c7e80c5857d2398ada76a31b816b49bfb5b655a6b3a7b449b` |
| SELF precedente conservato | `e8a4fa422beaa08350a12c8634ebd18b7d6a97e37d39b62eeebdc85dccc026d1` |
| Config.toml prima e dopo | `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65` |

Il PSARC non è stato scritto. Dimensione **2.642.224.628 byte** e metadati
della directory sono invariati; il contenuto completo dell'archivio non è
stato riletto. La ISO continua a essere assente dalla console.

Archivio:
`build/vita-psarc/install-vertex-layout-20261007T065647Z`.
`INSTALL.json` e `VALIDATION.json` distinguono l'installazione verificata
dall'esecuzione e dalla correttezza visiva ancora da provare.
`CHECKSUMS.json` sigilla gli artefatti e i registri. I puntatori
`build/latest-install-path.txt` e `build/vertex-layout-install-path.txt`
identificano questa consegna.
