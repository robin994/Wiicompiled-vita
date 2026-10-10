# WiiCompiled Vita: ripartenza da upstream e ISO

Questo documento conserva anche i risultati e gli artifact del 5 ottobre. Per lo stato corrente, il submodule Aurora e la pipeline PSARC consultare [VITA_STATUS_2026-10-06.md](VITA_STATUS_2026-10-06.md).

## Identità della base

- WiiCompiled: `patchzyy/wiicompiled`, commit `279ce8328f17059a4b46172d4ce71d3f403a1aec`.
- Aurora: submodule `aurora-main/` da `robin994/aurora-vita`, branch `vita-experiment`; base upstream corrente `2f2259b055a14cc96fc2bb40cd824e05b8017dbc`. Le modifiche Aurora specifiche del port devono vivere nel repository/submodule Aurora, non essere vendorizzate nel tree WiiCompiled.
- Le ottimizzazioni Aurora devono preservare le altre implementazioni/backend: modifiche Vita-specifiche vanno isolate o gated e devono mantenere i relativi contract/regression test prima di essere considerate upstream-ready.
- Branch di lavoro: `codex/restart-iso-gxm-20261005`.
- Il vecchio port non viene utilizzato dalla nuova build. È conservato in `../wiicompiled-vita-archive-20261005`, nel branch `archive/pre-restart-20261005` e nello stash denominato `Archive before clean ISO/GXM restart 2026-10-05`.
- L'archivio conserva sorgenti, vecchi build/generated/Assets/local e il precedente dump GPU. Non si tratta di un nuovo dump di questa build.

Questa è una nuova base compilabile da validare su hardware. Compilazione, test host e audit ELF non dimostrano l'avvio del gioco, una gara completa, stabilità con il tasto PS o un aumento degli FPS.

## Architettura implementata

`core/` possiede il disco e il contratto della memoria condivisa. Il lettore apre una ISO Wii raw cifrata, seleziona la partizione dati, ricava la title key dal ticket, decifra i cluster e verifica i 31 hash H0 del payload prima di pubblicarne i dati nella cache. Gli offset ISO sono a 64 bit anche su Vita, tramite `sceIoLseek`. La cache LRU contiene al massimo 16 cluster decifrati per impostazione iniziale, circa 496 KiB, e serializza accesso al file e cache. Non è una verifica crittografica completa della catena H1/H2/H3/TMD.

La FST viene letta dalla ISO e controllata prima dell'uso: limiti, nomi terminati, gerarchie, duplicati e intervalli dei file. Le viste `/sys/main.dol`, `/sys/fst.bin`, `/sys/boot.bin` e `/sys/bi2.bin` sono intervalli virtuali del disco. Non vengono scritte come file estratti. I percorsi DVD si risolvono nella FST; le letture basse usano direttamente la partizione decifrata o l'immagine raw, senza indovinare un file in base all'intervallo richiesto.

Il traduttore accetta `iso:mariokart.iso#/sys/main.dol` e `iso:mariokart.iso#/rel/StaticR.rel`. Il processo `wiicompiled-disc` legge i binari usando lo stesso core e li passa in memoria al traduttore. La build produce codice C++, initializer e payload di sezioni in `generated/`, ignorato da Git. Il runtime inizializza RAM e REL relocata dalle sezioni incorporate nel proprio eseguibile e legge gli asset di gioco dalla ISO su Vita. Non è un ricompilatore dinamico di qualunque ISO: il codice resta specifico per i due eseguibili validati.

`vita/guest_memory.cpp` alloca un backing per MEM1 e uno per MEM2. Le viste fisiche, cached e uncached restituiscono lo stesso puntatore più l'offset. I load/store RAM tradotti utilizzano questo percorso diretto; MMIO, FIFO, scritture eseguibili e letture EFB differite mantengono i controlli del runtime. Su ARM32 non viene riservato uno spazio virtuale da 4 GiB. Il target Vita usa MEM1 da 24 MiB e MEM2 retail da 64 MiB; la configurazione NDEV da 128 MiB resta propria dell'upstream desktop.

`vita/backend.cpp` presenta tramite Aurora GXM nativo e raccoglie i rettangoli del frontend in batch di quad. Il frontend di gioco verifica disco e hash, poi avvia il guest da `0x800060A4`. Controller Classic su porta 0 tramite `SceCtrl`, audio stereo tramite `SceAudioOut`, log e salvataggi risiedono nella directory dati Vita. Il frontend diagnostico separato verifica ISO/GXM e non esegue il gioco.

Il runtime conserva il loop guest e i callback HLE. Il ponte per i pacchetti draw raw emette header e vertici big endian nel FIFO Aurora. Le copie EFB colore restano texture GPU finché una lettura CPU tradotta chiede i byte: allora vengono sincronizzate, convertite e codificate nel layout tiled GX della RAM. Le registrazioni differite coprono tutte le viste RAM e vengono ritirate quando l'allocazione è riutilizzata. Orientamento GPU, tempi e comportamento reale di questo percorso richiedono confronto su Vita. Le copie EFB di profondità restano un limite del backend: il log lo segnala e la conversione colore rifiuta i formati Z.

## Stato dell'approccio richiesto

| Intervento | Stato concreto |
| --- | --- |
| FMA → VMLA | `WIC_FAST_PPC_MATH=ON` usa `vmla_f32` per le operazioni paired single ARM32. Verificata l'istruzione nel disassemblato. OFF nel pacchetto iniziale. Le operazioni scalari double/single mantengono le helper accurate dell'upstream. |
| Memoria mirrored diretta | Implementata per le viste RAM. Conservati i controlli di MMIO/FIFO, codice eseguibile e EFB; nessuna falsa mappatura virtuale da 4 GiB. |
| Riduzione dei salvataggi PPC | Riutilizzata l'analisi ABI/liveness upstream: 301 interfacce state free e 71 specializzazioni callsite derivate nella traduzione. Non sono state cancellate indiscriminatamente tutte le convenzioni guest. |
| Profilazione delle funzioni calde | `WIC_PROFILE_GUEST=ON` strumenta solo le unità tradotte. Ogni 300 frame registra indirizzi ARM, chiamate, tempo inclusivo e tempo esclusi i figli strumentati. Overhead diagnostico: non usare questi FPS per un confronto prestazionale. Contatori per thread; i tempi includono attese/native HLE sotto il chiamante guest. |
| Profilazione ISO | Contatori richieste, byte raw, hit/miss, tempo I/O e tempo AES/H0 nel log ogni 300 frame. Valori cumulativi del processo. |
| Rimuovere TEV dal motore di Mario Kart | Non implementato. Aurora può classificare/specializzare materiali supportati, ma il motore di Mario Kart non è stato riscritto come renderer autonomo. Serve evidenza dei materiali e degli effetti reali. |
| Texture compresse GXM | Aurora aggiornata include upload packed e CMPR → BC1 per il sottoinsieme compatibile. `WIC_NATIVE_TEXTURES=ON` abilita le opzioni, inizialmente OFF. Nessuna conversione permanente dei file della ISO. |
| Oggetti immutabili residenti in CDRAM | Cache geometria Aurora disponibile, budget 8 MiB con `WIC_RESIDENT_GEOMETRY=ON`, altrimenti 0. Non equivale ancora a una gestione della vita degli oggetti per pista/area. |
| GX asincrono | OFF: si mantiene un solo consumatore sincrono. Preservato il reset dello stato GXM per scena. |

La riduzione delle FMA modifica il rounding: non è dimostrata l'equivalenza di fisica, replay o determinismo. Anche con fast math OFF, il nuovo supporto ARM32 deve essere confrontato con il gioco su hardware; non viene dichiarata equivalenza completa bit per bit della FPU PPC.

## Disco locale verificato

- Formato supportato in questa base: ISO Wii raw cifrata, retail common-key index 0, PAL `RMCP01`, revisione 0.
- ISO: 4.699.979.776 byte, SHA-256 `2fc548bad45f373bad953d410a32e042e7efeef86a90f9ff63ddc5b62f9e6971`.
- Partizione logica: 4.255.886.336 byte; 2.099 voci nel catalogo incluse le viste di sistema.
- DOL SHA-256: `80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05`.
- REL SHA-256: `16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d`.
- DOL e REL letti direttamente dalla ISO confrontati byte per byte con il precedente riferimento locale.
- Tradotte 29.637 funzioni; 72 shard base comuni. Dati incorporati: 7.701.072 byte.

ISO, binari guest, file generated e VPK non sono sorgenti pubblici del repository. Il VPK di gioco è un prodotto locale legato a questi dati.

## Verifiche e limiti

- Core: test di ISO cifrata sintetica con seek sopra 2 GiB, cluster attraversati, corruzione H0, bounds, cache e letture concorrenti; alias RAM; test separato di codifica tiled EFB colore, padding e rifiuto del formato profondità.
- Traduttore: 648 test passati dopo l'aggiunta degli input ISO e della compatibilità GCC dei risultati state free.
- Aurora: 16 test host passati. Sono test CPU/contratti, non rendering GXM su console.
- Header runtime ARM32 compilati con fast math OFF e ON; profiler ARM32 compilato separatamente. Il profilo ON non è ancora un campione di gioco su console.
- Assembly dati ora sceglie ELF ARM, Mach-O o COFF mediante il preprocessore del compilatore target; non dipende dal sistema su cui gira il traduttore.
- Core: 2/2 test passati nella verifica finale.
- Audit del target diagnostico: 2.463 simboli eseguibili controllati; GXM draw/present presenti, assenza di GL/vitaGL/vita2d.
- Audit del gioco: 43.678 simboli eseguibili controllati; GXM draw/present presenti, assenza di GL/vitaGL/vita2d.
- Build nativa del gioco completata fino a ELF, VELF, SELF e VPK. Il linker segnala che `crtn.o` del VitaSDK non contiene `.note.GNU-stack`; la build si conclude con successo. Il warning non è stato risolto modificando il runtime.

Gli hash degli artifact finali sono registrati in [build/vita-game/DELIVERY.json](../build/vita-game/DELIVERY.json) per il gioco e in [build/vita/DELIVERY.json](../build/vita/DELIVERY.json) per il diagnostico. Entrambi verificano che l'`eboot.bin` contenuto nel VPK sia identico al SELF della rispettiva build. L'audit finale del gioco è in `build/vita-game-audit.log`. Conservare ELF, map e VELF corrispondenti se viene catturato un dump.

## Artifact consegnati

Pacchetto iniziale: GXM nativo, GX asincrono OFF, fast math OFF, texture native sperimentali OFF, cache geometria residente OFF e profilazione guest OFF. Il manifest è stato registrato il 5 ottobre 2026 alle 16:15 UTC. I sorgenti e gli input generati hanno SHA-256 cumulativo `da1aac4fd8cd9de2ab050a7fc936461696e71fd45f76eba541f73d2e43eed64a` su 30.981 file; il checkout contiene modifiche locali non ancora committate.

| Artifact | Byte | SHA-256 |
| --- | ---: | --- |
| [Gioco VPK](../build/vita-game/wiicompiled-vita-game.vpk) | 20.361.677 | `e967c747335cacd5bae2d8947852027b6f87b356446124b401d0a6f079f554fa` |
| [Gioco SELF](../build/vita-game/game-eboot.bin) | 20.552.512 | `bd2c4e1b8a0e30a177e0a9c9d6368a3a3a4434a400280f299a13d513f7373a08` |
| [Diagnostico VPK](../build/vita/wiicompiled-vita-iso-check.vpk) | 689.877 | `d23d5eb834204342919c66e0fb144451cc80107f8ef321f5fcd9186ee81e4360` |
| [Diagnostico SELF](../build/vita/eboot.bin) | 695.851 | `10048ff807c0072392b401c102e9a71c538367c229066d20893aa1c6a42ab9ae` |

Mancano prove hardware di launcher, avvio guest, menu, gara, audio, input, EFB depth, salvataggi, ritorno dai menu, sospensione e consumo di memoria. Il profilo iniziale è offline e base game; la nuova integrazione non include frontend Retro Rewind, overlay estratti, controller SDL o integrazione con servizi desktop.

## Passi successivi riprendibili

1. Installare il diagnostico e verificare ISO e display nativo. Conservare `iso-check.log`.
2. Installare il VPK del gioco, verificare dal launcher e avviare. Conservare `runtime.log`, eventuale `crash.txt` e dump kernel insieme agli artifact di `DELIVERY.json`.
3. Correggere prima i problemi di avvio, materiali/effetti, EFB profondità e memoria. Una prova host non chiude questi punti.
4. Catturare baseline per scena con identità del binario e impostazioni; profilare i guest caldi in una build separata.
5. Implementare sostituzioni native delle sole funzioni/materiali identificati. Confrontare texture e geometria residenti una opzione alla volta, poi estendere il ciclo di vita della cache alla pista/area.

Fonti tecniche: [WiiCompiled upstream](https://github.com/patchzyy/wiicompiled), [Dolphin VolumeWii](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/DiscIO/VolumeWii.cpp) e [Dolphin DI](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/IOS/DI/DI.cpp) per partizioni e offset delle richieste disco.
