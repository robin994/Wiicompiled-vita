# Modelli neri e avvio Time Trial — 8 ottobre 2026

Richiesta: ripristinare i modelli 3D, precedentemente visibili e ora ridotti a sagome nere; aggiungere una flag di configurazione per entrare direttamente in una prova a tempo con Mario, Kart standard M e Circuito Luigi.

**Aggiornamento dopo la prova Vita:** questa candidata è stata installata dall'utente e ha mostrato la schermata iniziale capovolta, seguita da framebuffer nero. La richiesta Time Trial e le copie light map sono confermate dal log; la validazione visiva è fallita. La diagnosi e il pacchetto successivo sono nel [report della regressione di presentazione](VITA_PRESENT_REGRESSION_2026-10-08.md).

## Evidenza e causa corretta nel codice

La cattura FTP in `build/models-timetrial-20261008/capture/CAPTURE.json` identifica il SELF installato `56f944cdd8e02e00004f9e8d7de2627031738e70999c75b8203d100ab1f3c1a2`, marker `psarc-resident-assets-async-vi-fair-20261008`. Il log prosegue almeno fino al frame 696: questa acquisizione non mostra il precedente arresto del produttore a 376. Non è una misura neutra degli FPS.

Le copie GX ripetute in `(0,456)` e `(32,456)`, 32×32, venivano mappate in `(0,544) 51×0` e `(50,544) 52×0`. Il ramo `gx_copy_empty` pubblicava texture nere valide, usate successivamente dal gioco. Il sorgente tradotto `EGG::LightMap::SendToGPU` (`0x8022ed20`) calcola esplicitamente la zona disponibile rispetto a **528 righe EFB**; `EGG::LightTexture::CopyFromEFB` (`0x8022e410`) copia quelle texture. La zona sotto la VI visibile non è una sorgente vuota.

Questo prova la perdita delle light map nel port attuale. È una causa concreta compatibile con le sagome nere segnalate; il ripristino finale dei colori, e l'eventuale presenza di altri difetti di illuminazione/materiali, richiedono la nuova prova Vita.

## Correzione candidata

- Raster visibile 960×544, con la stessa scala della VI 608×456.
- EFB GXM interno **1024×640**, sufficiente per l'EFB logico 640×528 a quella scala (1011×630). Allocazione più grande soltanto nel consumer WiiCompiled; gli altri consumer mantengono il default.
- Viewport e copie mantengono la scala visibile; lo scissor usa i limiti dello storage EFB. Le copie di luce diventano `(0,544) 51×39` e `(50,544) 52×39`, senza texture sintetica nera.
- Le coordinate dei bordi interi vengono moltiplicate prima della divisione in doppia precisione. Questo evita che il bordo visibile diventi 960.00006 e introduca una colonna nella copia display con lo storage più largo.
- La presentazione automatica ritaglia il raster visibile. `GXCopyDisp` conserva il suo crop esplicito; non viene ridimensionata tutta la zona EFB, comprese le righe scratch, sul display.
- Rimangono l'ordine FIFO, i clear/copy, il reset per BeginScene, le cache residenti e la precedente correzione VI dello scheduler. Nessun ricalcolo dei colori del modello o texture bianca sostitutiva.

L'EFB interno aggiunge memoria persistente e una transizione target all'inizio del frame. La build deve essere verificata per allocazione GXM, presentazione e stabilità su Vita; i test host non eseguono il driver GXM.

Marker: `psarc-resident-assets-async-lightmap-tt-20261008`.

## Configurazione del test

Il file esistente è `ux0:data/wiicompiled-vita/Config.toml`, non un secondo file INI.

```toml
[test]
time_trial = true
```

Default `false`, anche quando la chiave manca. La flag viene letta all'avvio.

Il test aspetta il completamento dell'inizializzazione titolo/salvataggi, sceglie la patente già selezionata se valida oppure la prima valida, prepara **Mario (0), Kart standard M (1), Circuito Luigi (8), Time Trial (2), 150cc, tre giri, derapata manuale** e lega il controller GameCube alla porta fisica 0. Un solo giocatore locale, senza CPU o fantasmi. Usa le funzioni tradotte originali per inizializzazione del contesto, controller, metadati Mii e transizione alla sezione `0x1f`; non sostituisce il caricamento di pista e kart.

Su NAND senza patente non ne crea una automaticamente: mantiene il flusso normale e riprova quando una patente esiste. Non riscrive dati di patente per configurare la gara e non simula input durante la guida. Il salto viene richiesto **una sola volta per lancio**, quindi uscire dalla gara non provoca un nuovo salto automatico.

Il hook è nel contratto `ApplyRuntimeCallOptions`, comune a chiamate statiche generate e dispatch indiretto. Non viene aggiunto un override nativo invisibile alle tabelle generate. I registri PPC vengono ripristinati prima che riprenda l'update intercettato.

Marker diagnostici:

```text
[efb-config] visible=960x544 storage=1024x640 lightmap_scratch=1
[aurora-vita] gx_copy_scratch ... captured=1
[test-time-trial] enabled=1 character=0 kart=1 course=8
[test-time-trial] requested=1 ... mode=2 section=0x1f controller=gamecube_port0
[test-time-trial] init_race character=0 kart=1 course=8 mode=2 player_type=0
[test-time-trial] load_section=0x1f
```

## Validazione locale

- Runtime **22/22 PASS**. Il nuovo test esegue il hook reale: disabled, gara già attiva, transizione in corso, save occupato, patente mancante, controller/derapata, conservazione dei registri e avvio singolo. Esegue anche la vera funzione tradotta `RacedataScenario::ComputePlayerCounts`, che conferma un giocatore/uno schermo/uno locale.
- Aurora **19/19 PASS**, compreso il test reale di mapping VI/EFB: immagine visibile invariata, light map non vuote, scissor parziale, coordinate native, ripristino dei limiti standard e rifiuto delle dimensioni invalide.
- Time Trial con AddressSanitizer + UndefinedBehaviorSanitizer: **PASS**.
- Parser: booleano `true`/`false`, chiave assente e tipi/sezioni invalidi: **PASS**.
- Compilazione Vita, audit GXM puro (45.606 simboli), linkage pthread/SceFiber e barrier asincrone: **PASS**. Il binario contiene il nuovo hook Time Trial e tutti i marker della candidata.
- Installer: conservazione delle altre opzioni TOML, aggiornamento idempotente, attivazione con backup e ripristino dei due file dopo un errore simulato: **PASS**. Controllo locale del VPK sigillato: **PASS**.

Questi controlli non provano ancora l'ingresso effettivo in pista, i colori, la risposta dei comandi o la stabilità su hardware della candidata.

## Consegna e prova Vita

Artefatti sigillati in `build/models-timetrial-20261008/candidate/`: VPK, SELF, ELF, VELF, map, opzioni CMake, manifest e risultati. Il SELF installato prima del lavoro e la configurazione sono nella cattura; lo script conserva anche un backup aggiornato al momento dell'installazione.

| Artefatto | SHA-256 |
| --- | --- |
| VPK (24.993.208 byte) | `de8d5e44c85c80247a8809acf45b465a737f627b9abe1efc00c15b5cc6ed9da6` |
| SELF `game-eboot.bin` | `6128156e30b1b7cd27c156762ae20d80d6f4f6b2617e183a85f52b8f3e9add3b` |
| ELF | `2dbeb57cd42cf21e09b5e7c81245c153679899d7267bd9b4ec758c69cb9d82a0` |

`DELIVERY.json` verifica l'identità tra SELF compilato e `eboot.bin` nel VPK e registra `TITLE_ID=WICMKW001`, **SFO `ATTRIBUTE2=12`**. Questo campo è distinto dall'attributo di controllo del SELF. `VALIDATION.json` dichiara esplicitamente che la candidata non è stata installata o avviata da questo lavoro e che la validazione hardware resta aperta. `source-snapshot/` conserva i diff rispetto agli HEAD e i byte esatti dei sorgenti della correzione.

Chiudere WiiCompiled e attivare il server FTP di VitaShell su `192.168.1.79:1337`:

```sh
python3 "/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/install-models-timetrial.py" --time-trial
```

Lo script verifica il pacchetto locale e gli altri file dell'app già registrata, carica il SELF in un file temporaneo, verifica il trasferimento, conserva eseguibile/configurazione precedenti e modifica soltanto la flag richiesta. Non trasferisce PSARC o NAND. Produce `INSTALL.json` con hash e backup; non avvia il gioco.

Lanciare WiiCompiled, controllare ingresso in Time Trial con la combinazione concordata, colori di Mario/kart/pista, HUD integro, acceleratore/sterzo e audio. Conservare eventuali coredump con questo ELF/map/VELF. Per confrontare i menu, impostare `time_trial = false` e rilanciare; lo script accetta anche `--menu`.

## Provenienza tecnica

Branch WiiCompiled `codex/restart-iso-gxm-20261005`, base `279ce8328f17059a4b46172d4ce71d3f403a1aec`. Aurora `wiicompiled-integration-20261006`, HEAD `28512bd75531508276be1582882145ea021151eb`, con modifiche locali preesistenti e quelle di questa candidata. Nessun commit/push eseguito.

I layout sono stati confrontati con costruttori e funzioni in `generated/functions/`, `projects/mkwii/MAP.txt` e, come riferimento secondario, [mkw-sp](https://github.com/mkw-sp/mkw-sp/tree/d2511168b4f102ac871bf0b6e9c089f0f8bfeeb0/payload/game) al commit `d2511168b4f102ac871bf0b6e9c089f0f8bfeeb0`. La build usa le funzioni già tradotte del nostro gioco, non codice del payload mkw-sp.
