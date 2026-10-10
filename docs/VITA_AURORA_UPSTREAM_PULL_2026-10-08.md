# Aggiornamento Aurora `vita-experiment` — 8 ottobre 2026

## Integrazione

La dipendenza `aurora-main` è stata aggiornata dal remoto `https://github.com/robin994/aurora-vita.git`, branch `vita-experiment`.

- Upstream precedente: `2f2259b055a14cc96fc2bb40cd824e05b8017dbc`.
- Upstream recuperato: `6acc6a889d0bec1fb206b1c73cb693a434c132df`.
- HEAD locale precedente: `28512bd75531508276be1582882145ea021151eb`.
- HEAD del merge locale: `27357eb889cd0a13d904c8bc708db9b3d162ed0c`.
- Branch locale: `wiicompiled-integration-20261006`.

Il merge include tutti e tre i nuovi commit upstream. Il puntatore della dipendenza nell'indice WiiCompiled è aggiornato al merge. Le correzioni locali restano nel checkout, senza un commit generale del lavoro precedente. Non è stato eseguito un push.

## Novità rilevanti

| Commit | Modifica | Utilità e stato nel progetto |
| --- | --- | --- |
| `a1d9b66` | Rispetta `exact_bc1` nei caricamenti diretti e negli aggiornamenti delle texture GXM | Corregge la scelta della conversione CMPR/BC1. Nel profilo WiiCompiled attuale i percorsi nativi GX/CMPR sono disattivati; non è una correzione verificata per Mario bianco. |
| `262e8e1` | Stabilizza il test di equivalenza dei materiali nativi | Allinea l'ordine aritmetico del riferimento al codice generato, evitando fallimenti dovuti all'arrotondamento del test host. |
| `6acc6a8` | Cattura dei draw con marker FIFO, hash degli shader/payload e registro di debug dei fragment shader | Permette diagnosi più precise di materiali, draw mancanti e differenze tra submission. Le API sono disponibili nella dipendenza; non vengono attivate automaticamente dal frontend WiiCompiled. |
| `6acc6a8` | Esperimenti XF, uniform e TEV | Nuove opzioni per eliminare scritture di matrice identiche, aggiornare uniform per differenza e specializzare i fragment shader. Rimangono disattivate. |

I nuovi default rimangono `gxm_xf_equal_pos_writes=false`, `gxm_uniform_delta_upload=false` e `gxm_a4_fragment_opt=0`. Cattura dei draw e registro shader restano disattivati. La flag Time Trial e il controllo `[diagnostics].enabled` dell'ultima candidata WiiCompiled sono conservati.

## Conservazione delle correzioni locali

Prima del fetch/merge sono stati salvati i 34 file locali modificati o non tracciati, patch complete/staged/unstaged e stato Git:

`build/aurora-upstream-20261008/before-20261008T183906Z/`

Il branch di recupero è `codex/aurora-before-upstream-20261008T183906Z`; lo stash originale è conservato all'oggetto `0a8392467c6dede7ba2114db66615ad032fe6d82`.

Il merge upstream ha avuto successo. Il ripristino delle modifiche locali ha richiesto la risoluzione di otto conflitti in FIFO, asset nativi, renderer, compilatore asset e test. Sono mantenuti:

- EFB 1024×640 e raster visibile 960×544, incluse le light map sotto il raster visibile.
- Orientamento della copia display e clear limitato al rettangolo sorgente di GXCopyTex.
- Reset GXM a ogni BeginScene e clipping esatto dei clear parziali.
- Acknowledgement del begin asincrono, barriera del frame finale e osservatore degli stalli del worker.
- Cache snapshot, aggiornamento del layout CP e gestione senza duplicazione dei payload RGBA grandi.

Per gli asset nativi, il terzo argomento di `compile_native_asset` conserva il significato locale `allowBc1`; il quarto aggiunge `includeRgba`. I default restano `true/true`, preservando la precompilazione RGBA già usata da WiiCompiled. Il compilatore accetta sia il preesistente `--no-bc1` sia il nuovo `--include-rgba`. I record AVNR v1 esistenti e la PSARC non richiedono rigenerazione per questo merge.

Sedici file locali non toccati upstream risultano byte per byte identici al backup. Diciotto file si sovrappongono alle modifiche upstream e sono stati integrati. Dettagli: `LOCAL_PRESERVATION.json` e `INTEGRATION.json`.

È stato corretto anche il rilevamento delle modifiche Aurora nel marker di build: ora considera modifiche staged e file non tracciati. Il suffisso `-dirty` descrive correttamente il checkout integrato con correzioni locali non committate.

## Verifiche

- Aurora host: **24/24 PASS**, inclusi i cinque nuovi contratti diagnostici/sperimentali.
- WiiCompiled host: **22/22 PASS**.
- ASan/UBSan: **5/5 PASS** su contratto backend/EFB, materiali nativi, frontend/FIFO asincrono, hash dei payload e cattura ordinata dei draw.
- Nessun conflitto Git residuo; controllo whitespace dei file Aurora integrati e dei file WiiCompiled modificati in questa integrazione: PASS. Il controllo globale del checkout contiene segnalazioni preesistenti nei dump e nei report diagnostici, lasciati intatti.
- Build Vita completa: **PASS**, eseguibile ARM, VELF, SELF e VPK prodotti.
- Audit GXM: **PASS**, draw/present GXM presenti, nessuna libreria o entry point GL/vgl/vita2d.
- Audit runtime ARM: **PASS**, implementazioni pthread reali, SceFiber e controlli di avvio collegati.
- Audit GX asincrono ARM: **PASS**, avvio del worker da GXInit e barriere `run_sync` in begin/end frame presenti nell'eseguibile.
- Identità: marker e revisione Aurora `27357eb889cd0a13d904c8bc708db9b3d162ed0c-dirty` verificati nei segmenti caricabili dell'ELF; SELF nel VPK identico al SELF salvato; `TITLE_ID=WICMKW001`, `ATTRIBUTE2=12` verificati nel SFO.
- Installer: controllo locale della candidata **PASS**; conservazione del config, idempotenza e ripristino di entrambi i file dopo un fallimento parziale verificati con FTP simulato.

Queste verifiche non provano modelli corretti, HUD visibile, avanzamento della gara o FPS migliori sulla Vita. Il merge non viene presentato come una soluzione verificata alla regressione grafica.

## Artefatti

Marker della nuova build: `psarc-resident-assets-async-aurora-upstream-20261008`.

Il bundle è salvato in `build/aurora-upstream-20261008/candidate/` con eseguibile, VPK, ELF/VELF/map, snapshot dei sorgenti integrati e patch, opzioni di compilazione, risultati dei controlli e manifest. La precedente candidata resta in `build/white-models-performance-20261008/candidate/`.

| Artefatto | SHA-256 |
| --- | --- |
| SELF `game-eboot.bin` | `7c6409fa3425a0568a7f782911e4db8d305bcb8387daee30c0ea12fef8bcb6f6` |
| ELF `wiicompiled-vita-game` | `5540f02ca1e0f2cd736ad36e8a66047870ec963d4b48a73faa12b6ecb6fd0b8e` |
| VPK `wiicompiled-vita-game.vpk` | `c7ce4b1cb9fd42aa1b2b0f69390887ac095021b34a205ded594f4cacd6bec32d` |

`DELIVERY.json` lega questi file alla revisione Aurora, alle opzioni CMake e al digest di 31.094 sorgenti/input generati. `IDENTITY.json`, `VALIDATION.json`, `ASYNC_LINKAGE.json`, `RUNTIME_LINKAGE.json` e `GXM_AUDIT.txt` conservano i risultati; `SHA256SUMS` copre il bundle.

## Installazione e prossimo test

Chiudere WiiCompiled e attivare l'FTP di VitaShell su `192.168.1.79:1337`. Per aggiornare l'installazione esistente e avviare il prossimo test con Mario, Kart standard M e Circuito Luigi:

```sh
python3 "/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/install-aurora-upstream.py" --time-trial --quiet
```

Lo script verifica l'identità della candidata, salva l'eseguibile e il config precedenti, aggiorna i file con verifica del trasferimento e conserva le altre impostazioni. Imposta `[test].time_trial=true` e `[diagnostics].enabled=false`; la cadenza di presentazione resta registrata. Il gioco si avvia manualmente dopo l'aggiornamento. Se altri file dell'app differiscono da quelli del VPK, lo script si ferma e indica di installare il VPK completo con VitaShell.

Per una successiva acquisizione diagnostica della stessa build, sostituire `--quiet` con `--diagnostic`: abilita la diagnostica WiiCompiled esistente e una cattura del frame completato 300. Le nuove API di cattura dei draw upstream richiedono ancora un collegamento mirato agli eventi del frontend WiiCompiled.

In questa integrazione non sono stati trasferiti file alla Vita né avviato il gioco. Modelli, HUD, avanzamento della gara e framerate della nuova candidata restano da verificare sulla console.

## Test successivo dell'utente

L'utente ha installato e avviato la candidata. La rilettura del SELF/config e il marker di avvio confermano l'eseguibile aggiornato con diagnostica disattivata. Il test non risolve la vista frontale ferma o i materiali bianchi; viene segnalata anche una scena scura. L'osservazione dello stato di gara e il collegamento mirato della nuova cattura dei draw sono ora implementati in una candidata successiva, descritta in [VITA_TIME_TRIAL_FREEZE_2026-10-08.md](VITA_TIME_TRIAL_FREEZE_2026-10-08.md).
