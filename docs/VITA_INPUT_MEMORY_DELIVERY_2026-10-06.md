# WiiCompiled Vita: aggiornamento input e memoria sulla console

Aggiornamento dopo il test manuale, acquisizione del 6 ottobre 21:33 UTC:
il SELF coincide con la candidata, i metadati appmeta sono ora diversi da
quelli precedenti all'installazione e la memoria libera prima del renderer
è salita da 71.303.168 a 185.597.952 byte. I comandi rispondono e i menu
avanzano. La registrazione risulta quindi aggiornata dopo la consegna qui
descritta; il file appmeta protetto non è stato decifrato per verificarne
direttamente gli attributi. Resta un triangolo bianco su fondo nero.
Evidenza e correzione successiva: [report VCD/VAT](VITA_VERTEX_LAYOUT_2026-10-06.md).
Il registro sigillato dell'installazione conserva lo stato precedente,
quando la promozione nativa era ancora da completare.

## Stato alla consegna precedente

I contenuti della candidata **`psarc-input-memory-20261006`** sono aggiornati
sulla PS Vita `192.168.1.79:1337`, nell'app `WICMKW001`. Tutti i **14 file**
del VPK sono stati riletti dalla console e verificati tramite dimensione
e SHA-256. Cambiano eboot.bin e sce_sys/param.sfo. Il nuovo SFO contiene
`ATTRIBUTE2=12` e il corretto TITLE_ID.

**La registrazione nativa del nuovo pacchetto non è completata.**
Il servizio remoto Vita Companion 1.06 non espone un comando di installazione;
i metadati protetti in ux0:appmeta/WICMKW001/param.sfo risultano byte per byte
identici prima e dopo l'aggiornamento dei file. Il valore 12 nel file dell'app
non è quindi una prova che il sistema abbia già applicato la nuova modalità
di memoria. Non sono stati modificati direttamente appmeta o il database app.

Il VPK completo è stato caricato e riletto integralmente in:

```text
ux0:data/wiicompiled-vita-input-memory.vpk
```

È stata aperta VitaShell per completare l'installazione. Non è stato inviato
alcun comando di avvio del gioco. Aprire il VPK in VitaShell e confermare
l'installazione prima di provare la build. L'installer VitaShell usa la
[promozione nativa del pacchetto](https://github.com/TheOfficialFloW/VitaShell/blob/master/package_installer.c)
per completare questo passaggio.

## Identità e conservazione

| Artefatto | SHA-256 |
| --- | --- |
| SELF riletto dalla Vita | `e8a4fa422beaa08350a12c8634ebd18b7d6a97e37d39b62eeebdc85dccc026d1` |
| VPK caricato e riletto | `c5a8ac0e52682d864ecb53e4d7178e6f9edf61df5215758c4cc8f861c22186d4` |
| SELF precedente conservato | `eca6ac41e1b490842d6c2ec698a1cdc1a575cfec00047e6a341ca0da1d9fe679` |
| Config.toml prima e dopo | `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65` |

Entrambi i file modificati sono stati caricati con nomi temporanei e riletti
prima della sostituzione. I precedenti eseguibile e SFO sono conservati sulla
console con suffisso `before-input-memory-<timestamp>` e nell'archivio locale.
Tutti i payload precedenti dell'app sono conservati in `previous-app/`.

PSARC: nessuna scrittura; dimensione **2.642.224.628 byte** e metadati della
directory invariati. Il contenuto completo dell'archivio non è stato riletto
durante questa consegna. La ISO continua a essere assente dalla console.
Config.toml è identico prima e dopo l'aggiornamento.

Archivio della consegna:
`build/vita-psarc/install-input-memory-20261006T212635Z`.
Comprende ELF, map, VELF, SELF, VPK, verifiche locali, file precedenti,
SFO ed eseguibile riletti, configurazione e registro delle operazioni.
`INSTALL.json` distingue l'aggiornamento verificato dei file dalla
registrazione nativa ancora necessaria. `CHECKSUMS.json` sigilla l'archivio.
`build/latest-install-path.txt` e `build/input-memory-install-path.txt`
indicano questa consegna; la candidata locale originale resta intatta.

## Test successivo

Dopo l'installazione completa in VitaShell, avviare manualmente il gioco.
Premere X, rilasciarlo e provare croce direzionale e Start. Sono attesi il
marker `build=psarc-input-memory-20261006`, i marker `[input-vita]`,
`[frame-health]` e `[audio-health]`, e tre catture del framebuffer nella
directory `ux0:data/wiicompiled-vita/aurora/diagnostics`.

Riquadro bianco, risposta dei comandi, audio e modalità di memoria effettiva
restano da verificare su questa build. Implementazione e verifiche locali:
[report input e rendering](VITA_INPUT_RENDER_STATUS_2026-10-06.md).
