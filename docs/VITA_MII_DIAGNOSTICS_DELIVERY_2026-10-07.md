# WiiCompiled Vita: diagnostica Mii/FPS installata, 7 ottobre 2026

La build **`psarc-mii-diagnostics-20261007`** è installata sulla PS Vita
`192.168.1.79:1337`, app `WICMKW001`. Tutti i **14 file** dell'app sono stati
riletti integralmente: dimensioni e SHA-256 coincidono con i contenuti del VPK.
Il gioco non è stato avviato automaticamente.

Questa build raccoglie parametri delle copie EFB fallite, stato RFL, campioni
di geometria illuminata e tempi delle chiamate native e delle attese. La
correzione dei Mii e il miglioramento degli FPS restano da individuare e provare.
[Diagnosi, implementazione e validazione locale](VITA_MII_RENDER_STATUS_2026-10-07.md).

## Verifica dell'installazione

È cambiato soltanto `eboot.bin`. Il nuovo eseguibile è stato caricato con un
nome temporaneo e riletto per verificarne l'intero hash prima della sostituzione.
Il precedente è conservato con un rename; la sostituzione non lo sovrascrive
in posizione. Anche il backup remoto è stato riletto e verificato.

Il `param.sfo` del VPK è byte per byte identico a quello già installato:
`ATTRIBUTE2=12`, `ATTRIBUTE=32768`. I metadati registrati in appmeta sono
identici prima e dopo. Non è stata necessaria una nuova registrazione dell'app.
`Config.toml` è stato conservato e riletto byte per byte.

Il PSARC non è stato scritto. La dimensione di **2.642.224.628 byte** e i
metadati della directory sono invariati; il contenuto completo non è stato
riletto. La ISO continua a essere assente dalla console.

| Artefatto | SHA-256 |
| --- | --- |
| SELF installato e riletto | `5c37a4b082071b81231d7fea4d8c690540b5f4ebc83ba196f770875a01e1eb68` |
| VPK sorgente | `f1bd53931fcbc8a3808346897194a7af74e94a4a2e01ba0013d4270ccbb3d95d` |
| SELF precedente conservato | `3bd1e21c1acfb1dec669c699fa194cd4b91393952e560dd4c98b3572f766240e` |
| Config.toml prima e dopo | `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65` |

Backup remoto verificato:
`ux0:/app/WICMKW001/eboot.bin.before-mii-diagnostics-20261007T075857Z`.
La precedente app, configurazione e sessione diagnostica sono conservate
anche nell'archivio locale dell'installazione.

## Prossima prova manuale

Il comando mirato `kill WICMKW001` ha restituito
`Error: cannot kill the app. Is the TITLEID correct?`. Vita Companion 1.06
restituisce questo messaggio per ogni risultato negativo della funzione
di chiusura; non distingue un'app inattiva da un altro errore. La chiusura
del processo non è dichiarata confermata. Sono stati aggiornati e verificati
i file dell'app; per usare il nuovo eseguibile, chiudere l'eventuale sessione
precedente e riaprire il gioco dalla LiveArea. Non sono stati inviati comandi
di avvio, riavvio della console o chiusura di tutte le applicazioni.

Restare circa **15 secondi nel menu iniziale**, poi almeno **15 secondi nella
creazione della licenza/selezione Mii**. I nuovi log devono contenere il
marcatore `psarc-mii-diagnostics-20261007`, `[runtime-profile]`, `[rfl-health]`
e, quando si verificano copie fallite, `[aurora-vita][copy-failed]`.
La prova manuale resta necessaria per stabilità, rendering e prestazioni.

## Artefatti conservati

Archivio della candidata, immutato:
`build/vita-psarc/candidate-mii-diagnostics-20261007T075306Z`.

Archivio dell'installazione:
`build/vita-psarc/install-mii-diagnostics-20261007T075857Z`.
Include VPK, SELF, ELF, map, VELF, sorgenti e log della build, i file precedenti,
i file installati riletti e i registri dell'aggiornamento. `INSTALL.json` e
`VALIDATION.json` distinguono l'installazione verificata dalla prova visiva
ancora da eseguire. `CHECKSUMS.json` conserva gli hash dell'archivio.
I puntatori `build/latest-install-path.txt` e
`build/mii-diagnostics-install-path.txt` identificano questa consegna.
