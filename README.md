# WiiCompiled Vita

Ripartenza del port da [WiiCompiled upstream](https://github.com/patchzyy/wiicompiled), con core disco, runtime ARM32 e backend Aurora GXM nativo. Il gioco legge `mariokart.iso` oppure un singolo `mariokart.psarc` preparato dalla ISO; sul dispositivo non serve una directory di file estratti.

Sono prodotti un VPK diagnostico ISO/GXM e un VPK locale con il gioco ricompilato. Sulla Vita il bootstrap thread/TLS/SceFiber passa, i comandi rispondono e i menu avanzano. Le prove CPU RAM, FMA e THP del 7 ottobre restano negative per il port: l'utente riferisce menu a 1 FPS, video errati nella candidata THP e uscita dopo un player. Il target resta **30 FPS**. Con il decoder JPEG attivo, i siti matematici più caldi appartengono a `PSMTXConcat`; il frontend GX e il resto del codice tradotto restano costi aperti. Falliscono tutte le 15 allocazioni SDK del layout: la lista dei blocchi liberi dell'heap presenta un collegamento incoerente. L'uscita e il Mii mancante non sono ancora corretti.

È implementata la conversione dei 27 filmati in H.264 e il lettore hardware Vita sperimentale. Il nuovo PSARC passa da 2,64 a 1,41 GB, conserva l'audio originale e non richiede asset estratti sulla console. Tutti i 55.143 fotogrammi sono verificati su host. La candidata AVC/buffering è stata provata: i video sono visibili ma alternano fotogrammi verdi per tutta la permanenza nelle card, e il gioco esce dopo "1 player". Il log raggiunge 120 immagini decodificate, con circa 8 ms nei campioni; le finestre lente completano invece un frame del gioco in circa 302–574 ms. I worker sui piccoli lotti di vertici non ricevono lavoro, mentre quelli del tiling video sono attivi. L'heap perde circa 20 MB di capacità perché l'intestazione libera viene azzerata; la richiesta da 32 byte fallisce senza aggiungere un blocco usato. La scrittura responsabile non è ancora identificata. Progressione, target 30 FPS e Mii restano da validare. Gare ed effetti non sono verificati; copie EFB di profondità e area fuori schermo dei Mii restano limiti. Il motore non è ancora stato riscritto per eliminare TEV.

È preparata la candidata `psarc-memory-video-present-20261007`: corregge l'ordine tra copie EFB differite e scritture CPU, invalida le texture dopo l'aggiornamento completo dei piani video e affida la presentazione Vita a `GXCopyDisp` per evitare frame vuoti tra due frame del gioco. Il difetto di scrittura è riprodotto sui sorgenti della build precedente e il test passa con la correzione; passano 19 test host e 5 prove con i controlli sulla memoria. Il flicker dell'avvertenza, il verde persistente e l'uscita dopo "1 player" richiedono una nuova prova sulla console. Il VPK usa ATTRIBUTE2=12 e conserva il PSARC AVC corrente.

## Base

La candidata memoria/video/presentazione ha consentito all'utente di superare "1 player" e raggiungere la selezione personaggi: i modelli 3D renderizzano, ma le animazioni restano molto lente. I log della build verificata misurano circa 0,15–0,20 FPS nelle finestre finali e 72.958 creazioni/caricamenti di texture native, soprattutto I8/IA8 di piccole dimensioni. Il costo dominante registrato è nel percorso GX/FIFO; la saturazione GPU e il costo esclusivo delle texture restano da attribuire. GX asincrono è disattivato. La gara è ancora da verificare.

- WiiCompiled: `279ce8328f17059a4b46172d4ce71d3f403a1aec`.
- Aurora Vita: submodule [`aurora-main/`](aurora-main) da `robin994/aurora-vita`, branch `vita-experiment`. Base upstream corrente: `2f2259b055a14cc96fc2bb40cd824e05b8017dbc`; gli adattamenti WiiCompiled restano commit separati nel repository Aurora.
- Nuovo branch: `codex/restart-iso-gxm-20261005`.
- Dettagli, evidenza e lavoro residuo: [report della ripartenza](docs/VITA_RESTART_2026-10-05.md).
- Stato aggiornato e risultati PSARC: [report del 6 ottobre](docs/VITA_STATUS_2026-10-06.md).
- Nero dopo 1 Player con audio attivo, catture e correzione del servizio retrace: [regressione dell'8 ottobre](docs/VITA_SINGLE_PLAYER_BLACK_2026-10-08.md).
- Modelli diventati sagome nere, conservazione delle light map nell'EFB e avvio rapido Time Trial: [candidata dell'8 ottobre](docs/VITA_MODELS_TIME_TRIAL_2026-10-08.md).
- Prova Vita della candidata: schermata capovolta e nero; [correzione della copia display e dei clear parziali](docs/VITA_PRESENT_REGRESSION_2026-10-08.md).
- Mario bianco, HUD assente e circa 1 FPS: [verifica CDRAM e confronto con diagnostica disattivata](docs/VITA_WHITE_MODELS_PERFORMANCE_2026-10-08.md).
- Dipendenza Aurora aggiornata a `vita-experiment`: [merge dell'8 ottobre e conservazione delle correzioni WiiCompiled](docs/VITA_AURORA_UPSTREAM_PULL_2026-10-08.md).
- Vista frontale ancora ferma e scena scura dopo il pull: [stato della gara e cattura completa dei draw](docs/VITA_TIME_TRIAL_FREEZE_2026-10-08.md).
- Time Trial che avanza dopo la correzione dell'ID controller, ancora circa 1 FPS e HUD assente: [correzione e prova hardware](docs/VITA_TIME_TRIAL_CONTROLLER_2026-10-08.md).
- Submission e streaming GXM diretti, prewarm, cache shader/vertici e opzioni dei worker: [candidate native e lavoro residuo del 9 ottobre](docs/VITA_NATIVE_RENDER_PERFORMANCE_2026-10-09.md).
- Menu a 1 FPS, uscita dopo 1 player, riduzione delle chiamate RAM e diagnostica CPU: [stato CPU del 7 ottobre](docs/VITA_CPU_MENU_STATUS_2026-10-07.md).
- Prova FMA/layout senza miglioramenti: [consegna CPU FMA del 7 ottobre](docs/VITA_CPU_FMA_DELIVERY_2026-10-07.md).
- Decoder THP nativo, causa immediata dell'uscita e confronto delle opzioni Strikers: [stato THP/Aurora del 7 ottobre](docs/VITA_NATIVE_THP_AURORA_2026-10-07.md).
- Prova THP negativa, video H.264 nel PSARC, nuova attribuzione CPU e heap incoerente: [stato AVC del 7 ottobre](docs/VITA_HARDWARE_AVC_2026-10-07.md).
- Prova H.264 negativa, decoder senza output e heap completo corrotto: [diagnosi corrente e candidata AVC/heap](docs/VITA_AVC_HEAP_FAILURE_2026-10-07.md).
- Video visibili con verde persistente, limite sul percorso CPU e prima corruzione dell'heap osservata: [diagnosi della prova AVC/buffering](docs/VITA_AVC_GREEN_CPU_HEAP_2026-10-07.md).
- Correzioni all'ordine delle copie RAM, alle texture video e alla presentazione dell'avvertenza: [candidata memoria/video/presentazione](docs/VITA_MEMORY_VIDEO_PRESENT_DELIVERY_2026-10-07.md).
- Progressione fino alla selezione personaggi, texture ripetute e priorità di ottimizzazione: [stato texture e tempi GX](docs/VITA_TEXTURE_COST_STATUS_2026-10-07.md).
- Texture, geometria e PCM preparati sul computer, nello stesso PSARC: [pipeline asset nativi](docs/VITA_NATIVE_ASSET_PIPELINE_2026-10-07.md).
- Ultimo crash e correzione pthread: [diagnosi startup/pthread](docs/VITA_CRASH_STARTUP_2026-10-06.md).
- Crash precedente e prerequisiti del runtime: [diagnosi audio, thread e pile](docs/VITA_CRASH_AUDIO_2026-10-06.md).
- Triangolo bianco, errore VCD/VAT riprodotto e candidata corretta: [diagnosi rendering](docs/VITA_VERTEX_LAYOUT_2026-10-06.md).
- Build VCD/VAT installata e verificata il 7 ottobre: [consegna sulla Vita](docs/VITA_VERTEX_LAYOUT_DELIVERY_2026-10-07.md).
- Menu iniziali confermati, creazione della licenza incompleta e nuova diagnostica Mii/FPS: [prova del 7 ottobre](docs/VITA_MII_RENDER_STATUS_2026-10-07.md).
- Build diagnostica Mii/FPS installata e verificata, pronta per la prova manuale: [consegna del 7 ottobre](docs/VITA_MII_DIAGNOSTICS_DELIVERY_2026-10-07.md).
- Aggiornamento input/ATTRIBUTE2 e successiva prova sulla Vita: [consegna input/memoria](docs/VITA_INPUT_MEMORY_DELIVERY_2026-10-06.md).
- Comandi GameCube e diagnostica: [input e rendering](docs/VITA_INPUT_RENDER_STATUS_2026-10-06.md).
- Consegna precedente verificata all'avvio: [pthread completa](docs/VITA_PTHREAD_FIX_DELIVERY_2026-10-06.md).
- Consegna precedente arrestata: [SceFiber/pthread](docs/VITA_CONTEXT_FIX_DELIVERY_2026-10-06.md).
- Correzione delle attese e prossima candidata: [consegna native wait](docs/VITA_WAIT_FIX_DELIVERY_2026-10-06.md).

Il precedente lavoro è archiviato fuori dal checkout attivo, in `../wiicompiled-vita-archive-20261005`, con branch e stash recuperabili. La nuova build utilizza i sorgenti upstream e le nuove directory `core/` e `vita/`.

## Disco supportato

La preparazione sul computer richiede una ISO Wii raw cifrata di Mario Kart Wii PAL, `RMCP01`, revisione 0, con retail common-key index 0. Sulla Vita basta il PSARC preparato da questa sorgente. Il frontend verifica gli hash degli eseguibili nel contenitore selezionato prima di permettere l'avvio:

| Input | SHA-256 |
| --- | --- |
| `/sys/main.dol` | `80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05` |
| `/rel/StaticR.rel` | `16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d` |

RVZ, WBFS, NKit, altre regioni/revisioni e overlay Retro Rewind non fanno parte di questo profilo. La ricompilazione resta specifica per questi eseguibili, mentre gli asset vengono letti dal contenitore selezionato durante il gioco: PSARC nel percorso attualmente usato sulla Vita.

## Preparare il PSARC e risparmiare spazio

```sh
./build-vita.sh data
```

Il tool legge la ISO originale, verifica H0 durante la decifratura sul computer, mantiene tutti i file del gioco, elimina il padding del disco e condivide i payload identici. Usa il packer e il lettore portabile della pipeline Strikers. I blocchi originali restano non compressi, senza decompressione durante il gioco. Non cambia texture, modelli, suoni o filmati.

La ISO locale da 4.699.979.776 byte produce `build/vita-data/mariokart.psarc` da 2.642.224.628 byte: 2.057.755.148 byte risparmiati, **43,78%**. Il confronto completo verifica ogni file tramite SHA-256 e tramite il lettore C usato dal runtime. `build/vita-data/mariokart.json` registra identità, inventario e risultati. I file estratti esistono solo nella directory temporanea del convertitore sul computer, rimossa dopo la verifica; la ISO non viene cancellata né riscritta.

Il convertitore rifiuta output già esistenti. Per sostituire un precedente PSARC dopo una nuova verifica:

```sh
./build-vita.sh data --replace
```

Il lettore Vita usa I/O posizionale su un thread nativo dedicato, mantenendo le syscall fuori dagli stack guest libco. L'indice conserva gli offset originali della partizione per le richieste DVD. Le richieste a padding scartato o settori cifrati non conservati falliscono esplicitamente; non vengono sintetizzati byte. L'header raw conservato copre i primi `0x50000` byte della ISO.

## Build

Prerequisiti: CMake 3.25 o successivo, Python 3, SDK .NET 8 o runtime successivo con roll forward, VitaSDK con GCC ARM32 e libreria OpenSSL `crypto`. Su host serve OpenSSL; su macOS il percorso Homebrew standard viene rilevato dallo script. Le dipendenze runtime vendorizzate appartengono all'upstream; CMake scarica le dipendenze Aurora/Mbed TLS con i relativi pin.

Mettere la propria ISO nella radice del checkout, con il nome `mariokart.iso`, quindi:

```sh
./build-vita.sh test
./build-vita.sh game
```

Il secondo comando costruisce il lettore host, legge DOL/REL dalla ISO in memoria, verifica gli hash, genera initializer e traduzioni, compila il runtime Vita, produce il VPK e verifica l'ELF GXM. Il VPK locale è `build/vita-game/wiicompiled-vita-game.vpk`. I dati generati non vanno pubblicati nel repository.

Per il solo controllo iniziale ISO/GXM:

```sh
./build-vita.sh check
```

Produce `build/vita/wiicompiled-vita-iso-check.vpk`, che non avvia il gioco. Comandi intermedi: `host` e `prepare`. `WIC_JOBS` controlla il parallelismo; `VITASDK` può indicare un SDK differente. `WIC_SKIP_PREPARE=1` salta la traduzione solo quando `generated/` corrisponde già a input e sorgenti correnti.

## Uso su Vita

1. Per usare PSARC, copiare `build/vita-data/mariokart.psarc` in `ux0:data/wiicompiled-vita/mariokart.psarc` e impostare `paths.dvd_psarc` nel file di configurazione come sotto.
2. Installare il VPK del gioco compilato con questo lettore. La build corrente verifica DOL/REL e avvia automaticamente il guest. Un archivio esplicitamente configurato ma mancante o invalido causa un errore; non cambia automaticamente sorgente dati.
3. Per il percorso ISO, impostare `dvd_psarc = ""` e copiare la ISO in `ux0:data/wiicompiled-vita/mariokart.iso` su un filesystem che supporti file oltre 4 GiB. Il diagnostico resta dedicato a ISO/GXM, con log `iso-check.log` e uscita tramite Cerchio.

```toml
[paths]
dvd_psarc = "ux0:data/wiicompiled-vita/mariokart.psarc"
dvd_iso = ""
```

Quando PSARC è selezionato, sulla Vita serve il solo `mariokart.psarc`. Conservare la ISO sul computer come sorgente e controllo. Un eventuale percorso `dvd_iso` rimasto nella configurazione è ignorato durante l'uso del PSARC; l'esempio sopra lo lascia vuoto. Le diciture DVD e `[dvd-native]` indicano l'interfaccia disco del gioco, che legge anche dal PSARC, e non richiedono la ISO.

La prossima candidata presenta un GameCube pad su porta 0: Croce=A, Cerchio=B, Quadrato=X, Triangolo=Y, analogico sinistro=stick, destro=C-stick, D-pad, Start=Start, Select=Z e L/R. Il backend è verificato con test host; la risposta nel gioco richiede il test Vita. Audio stereo nativo a 32/48 kHz. Profilo iniziale offline. Il programma richiede CPU 444 MHz e GPU 222 MHz; il risultato sulla console non è stato misurato.

Il runtime crea `ux0:data/wiicompiled-vita/Config.toml`; `paths.dvd_psarc` seleziona l'archivio, `paths.dvd_iso` la ISO, `paths.nand_root` i salvataggi. `runtime.log` contiene avvio, identità Aurora e contatori del disco. La build installata `psarc-pthread-complete-20261006` ha superato i marker `[runtime-check]` sul dispositivo. La prossima `psarc-input-memory-20261006` aggiunge `[input-vita]`, `[frame-health]`, `[audio-health]` e tre catture del framebuffer nella directory Aurora diagnostics. Entrambi i VPK nuovi contengono ATTRIBUTE2=12 verificato dal manifest. I marker `[wait-native]` permettono di confrontare la pila delle attese con l'intervallo nativo registrato all'avvio. `crash.txt` conserva lo stato guest per errori intercettati; un crash kernel richiede il dump della console.

Per i test 3D, la candidata `psarc-resident-assets-async-lightmap-tt-20261008` legge una flag nello stesso file:

```toml
[test]
time_trial = true
```

Avvia una sola prova a tempo per lancio, con Mario, Kart standard M, Circuito Luigi, 150cc e derapata manuale. Usa una patente esistente; su NAND nuova resta nei menu finché viene creata una patente. `false` o chiave assente conserva l'avvio normale. Non forza pulsanti durante la guida. L'ingresso in gara e i colori dei modelli della candidata restano da verificare sul dispositivo.

## Opzioni sperimentali

La build iniziale mantiene GX sincrono e tutte le seguenti opzioni OFF:

- `WIC_FAST_PPC_MATH`: VMLA nelle operazioni paired single ARM32. Cambia il rounding; le helper scalari accurate restano attive.
- `WIC_NATIVE_TEXTURES`: upload packed e subset CMPR → BC1 supportati da GXM.
- `WIC_RESIDENT_GEOMETRY`: cache geometria immutabile Aurora, budget 8 MiB. Il ciclo di vita per pista/area resta da implementare.
- `WIC_PROFILE_GUEST`: profilazione strumentata delle unità tradotte. Contatori per thread; campioni ogni 300 frame. Aggiunge overhead e non produce una misura neutra degli FPS.

Esempio di build separata per una singola opzione:

```sh
WIC_BUILD_DIR=build/vita-fast ./build-vita.sh game -DWIC_FAST_PPC_MATH=ON
```

Confrontare una opzione alla volta con la build iniziale usando lo stesso percorso di gioco e conservando gli hash del SELF installato, i log, ELF, map e VELF. Il report distingue quanto implementato dalle ottimizzazioni ancora da dimostrare.

Licenza del progetto e dipendenze: [LICENSE](LICENSE), [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
