# WiiCompiled Vita: correzione pthread installata

La candidata **`psarc-pthread-complete-20261006`** è installata sulla
PS Vita `192.168.1.79:1337`, nell'applicazione già registrata `WICMKW001`.
Tutti i **14 file** dell'app sono stati riletti dalla console e coincidono
con i contenuti del VPK. Rispetto alla build precedente cambia solo eboot.bin.
Nessun comando di avvio è stato inviato.

La build corregge il collegamento parziale pthread identificato nel
[dump 1791319267](VITA_CRASH_STARTUP_2026-10-06.md). L'ELF risolve tutte
le primitive pthread referenziate e i guard C++ chiamano le implementazioni
reali di pthread_once, mutex e condition variable. L'audit aggiornato
rifiuta il binario precedente. Il backend SceFiber e i controlli iniziali
restano quelli della candidata precedente.

## Trasferimento e identità

Il trasferimento aggiorna i contenuti del VPK nell'app già registrata.
Il nuovo eseguibile è stato caricato con un nome temporaneo e riletto
integralmente: dimensione e SHA-256 coincidevano prima della sostituzione.
Il precedente eseguibile è conservato nell'archivio locale e sulla console
come `ux0:/app/WICMKW001/eboot.bin.crashed-native-context-20261006T205755Z`.

| Artefatto | SHA-256 |
| --- | --- |
| SELF riletto dopo l'installazione | `eca6ac41e1b490842d6c2ec698a1cdc1a575cfec00047e6a341ca0da1d9fe679` |
| VPK sorgente | `7791d311f43c397c5b345786868aa45553fdc42868305867f2e08d7ade7ed0c1` |
| ELF abbinato | `8f4f9f7480c6e1f0d2d18ba5d78488c76beb653f4b0a1f6aff1f344e70a91ea8` |
| SELF precedente conservato | `14c43f7a71cccd188bb5a43735fa35acd45eb2ed2f9028f0ecad4845e6250453` |

Config.toml è identico prima e dopo l'installazione, SHA-256
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
Il PSARC non è stato scritto: dimensione di 2.642.224.628 byte e metadati
della directory sono invariati. Il contenuto completo dell'archivio non è
stato riletto durante questa consegna. La ISO rimane assente dalla console.

Archivio della consegna:
`build/vita-psarc/install-pthread-complete-20261006T205755Z`.
`build/latest-install-path.txt` e `build/pthread-complete-install-path.txt`
identificano questa installazione. Manifest e verifiche:
[INSTALL.json](../build/vita-psarc/install-pthread-complete-20261006T205755Z/INSTALL.json),
[VALIDATION.json](../build/vita-psarc/install-pthread-complete-20261006T205755Z/VALIDATION.json),
[RUNTIME_LINKAGE.json](../build/vita-psarc/install-pthread-complete-20261006T205755Z/RUNTIME_LINKAGE.json).

## Risultato del test manuale successivo

La sessione acquisita alle 21:04 UTC contiene il marker di questa build
e conferma sulla Vita il successo di std::thread/TLS, thread SceKernel/TLS
e SceFiber/switch/ritorno nativo/d8-d15. Il mixer AX/DSP parte e il PSARC
raggiunge 22 letture senza fallimenti. Non c'è un nuovo dump nell'elenco
controllato. SELF e configurazione riletti coincidono con questa consegna.

Restano riquadro bianco immobile, musica ripetuta e input senza risposta
visibile. Il test non prova menu o gara corretti. Evidenza acquisita e
nuova candidata input/ATTRIBUTE2 sono nel
[report input e rendering](VITA_INPUT_RENDER_STATUS_2026-10-06.md).
