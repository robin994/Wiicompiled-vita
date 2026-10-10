# WiiCompiled Vita: crash PSARC e correzione degli stack nativi

Aggiornamento dopo il test della candidata: le presentazioni passano il punto
di questo crash, ma il caricamento DVD produce un nuovo dump. La nuova cattura
e l'intervento sui buffer sono nel
[report del secondo crash](VITA_CRASH_DVD_2026-10-06.md).

## Evidenza recuperata dalla console

Console: `192.168.1.79:1337`, app `WICMKW001`. Il nuovo dump è
`ux0:/data/psp2core-1791303648-0x00040e27c1-eboot.bin.psp2dmp`.
La copia locale è [crash.psp2dmp](../crashdumps/psarc-1791303648/crash.psp2dmp):
131.424 byte, SHA-256
`dbb13584da787d2a7f8b227c78c6a8da34df7001a6991f770eb5bf87bfd0aaf7`.

Il SELF scaricato da `ux0:/app/WICMKW001/eboot.bin` coincide byte per byte con
quello del VPK installato: SHA-256
`dbdd3541d79ff5ce88f310eca9efe9c0970d8317332398a5c90b88985a4e8aaa`.
Prima di modificare i sorgenti sono stati conservati ELF, map, VELF, VPK,
SELF, manifest, Config.toml e log nella stessa directory locale.
[CAPTURE.json](../crashdumps/psarc-1791303648/CAPTURE.json) registra gli hash.
Gli artefatti binari della cattura sono esclusi da Git.

Il log contiene più esecuzioni storiche. Per questa diagnosi si usa solo
[runtime-session.log](../crashdumps/psarc-1791303648/runtime-session.log),
a partire dal marker `build=psarc-native-20261006`, Aurora `28512bd7`.
`crash-historical.txt` è precedente a questa esecuzione e non identifica il nuovo crash.

Questa esecuzione verifica i dati di avvio del PSARC, carica l'indice RMCP01,
pubblica la FST, inizializza GX/VI e crea i thread guest. Non dimostra ancora
il funzionamento del menu o della gara. Il PSARC non è stato modificato o ricopiato.

## Punto del crash e limiti della diagnosi

Il thread `WICMKW001` è fermato con `stop_reason=0x10006`:

| Campo | Valore |
| --- | --- |
| PC | `0xe0054508`, `SceGpuEs4User@1+0x1768` |
| LR | `0xe00543a5`, `SceGpuEs4User@1+0x1605` |
| SP corrente | `0x8a2e93a0` |
| Stack nativo dichiarato dal thread | base `0x836c1000`, dimensione `0x40000` |
| Intervallo dello stack nativo | `[0x836c1000, 0x83701000)` |

Lo SP corrente è fuori dallo stack nativo ed è sullo stack libco della
coroutine guest. I valori di ritorno nello stack puntano a `Renderer::end_frame`,
`gfx::Renderer::present`, `gfx::Renderer::end_frame`, `end_frame_now`.
[analysis.txt](../crashdumps/psarc-1791303648/analysis.txt) conserva registri,
moduli e riferimenti simbolizzati nello stack; questa scansione non è un
unwinding completo delle chiamate.

Il ritorno `0x828e53eb`, corretto per la rilocazione del modulo, corrisponde
a `0x828c23ea` nel matching ELF. L'istruzione precedente è
`blx sceGxmDisplayQueueAddEntry` a `0x828c23e6`.
La chiamata GXM dallo stack della coroutine è quindi verificata; il codice
`0x10006` non viene rinominato in un'eccezione specifica senza documentazione.
La correlazione indica un problema nel confine coroutine/driver. La sua
risoluzione definitiva richiede una nuova esecuzione sulla console.

## Correzione implementata

- `HostContext::RunOnNativeStack` sospende il solo contesto chiamante, esegue
  il callback sullo stack originale dello stesso thread e riprende quel
  contesto. Non cambia lo stato dei thread PPC e non avvia un worker GX.
- Il dispatcher gestisce richieste da coroutine raggiunte attraverso altri
  contesti, callback annidati e propagazione delle eccezioni al chiamante.
- Il bridge Vita applica il dispatcher a begin/end frame, attese del renderer,
  materializzazione delle copie EFB e shutdown.
- Aurora ha un hook opzionale nella decodifica GX: i pacchetti vengono elaborati
  sullo stack nativo prima di restituire il controllo al produttore. I buffer
  conservano il loro ordine e restano validi fino al ritorno. Il percorso
  predefinito di Aurora rimane diretto quando l'hook non è installato.
- La routine libco ARM della Vita ora salva e ripristina `d8-d15`, oltre a
  `r4-r11`, SP e continuazione ARM/Thumb. Il nuovo stack iniziale contiene
  anche il frame VFP di 64 byte. La vecchia routine perdeva questi registri.
  La conservazione è richiesta dalla
  [specifica Arm AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst#6121-vfp-register-usage-conventions).
- I log di avvio usano formati numerici supportati dal printf Vita: il precedente
  `%zu` produceva `zu` e poteva spostare gli argomenti successivi nella stampa.
  Questa era una limitazione della diagnostica, non la prova di un heap da 24 MB.

Marker della candidata: `build=psarc-native-stack-20261006`,
`native-stack-dispatch=1`. Il log registra base/dimensione dello stack nativo
e lo SP delle prime quattro presentazioni come `[gx-native] present=... stack=...`.

Async GX, fast math, texture native sperimentali, geometria residente e profiling
guest restano OFF. Aurora ha due file modificati localmente sopra `28512bd7`;
il marker aggiunge `-dirty` e il manifest registra sia il pin sia lo stato del
submodule, oltre al digest dei sorgenti effettivi.

## Validazione

- 5/5 contratti host: PSARC, ISO, indice disco, copie EFB e contesti cooperativi.
  Il test dei contesti verifica che i callback girino sullo stack del scheduler,
  l'ordine A/B/scheduler, gli argomenti sulla coroutine e le eccezioni.
- 18/18 test host Aurora.
- Test ARM sulle istruzioni dell'ELF con Unicorn: la build del crash fallisce
  il controllo `d8-d15 were not restored`, riproducendo il difetto ABI.
- Controllo ARM della candidata superato: le istruzioni effettive dell'ELF
  preservano `r4-r11`, `d8-d15`, SP e continuazione ARM/Thumb, incluso il ritorno
  al contesto originale dopo la modifica dei registri da parte del contesto target.
- Build Vita completata. Audit dell'ELF/map: 44.935 simboli eseguibili controllati,
  draw/present GXM presenti, nessun entry point o libreria GL/vitaGL/vita2d.
- Il SELF incluso nel VPK coincide con `game-eboot.bin`; il manifest finale
  include ISO, PSARC e digest dei sorgenti effettivi.

Nessuna prova di stabilità hardware o di FPS viene dedotta da queste verifiche.
Log: [contratti host](../build/host-native-stack-validation.log),
[Aurora](../build/aurora-host/Testing/Temporary/LastTest.log),
[build/audit](../build/vita-native-stack-build.log),
[contratto ARM](../build/vita-native-stack-abi.log).

Comando del controllo ABI, con le dipendenze locali in `build/arm-contract-deps`:

```sh
PYTHONPATH="$PWD/build/arm-contract-deps" python3 scripts/test_vita_context_abi.py build/vita-psarc/wiicompiled-vita-game
```

## Identità della candidata

| Artefatto | SHA-256 |
| --- | --- |
| VPK | `78f2568345234f3dc7a8fb9f7fe5d45e30a5f093c1af5ac26e12cf47d79a8054` |
| SELF | `69f304c002fdb06155b013ccafc9d20149b68c101fba25f3af5ae4b5c3bdd101` |
| ELF | `30500b3548244165436676625d42d197008d4936cc1481a7831e79581bbe6115` |
| Digest sorgenti e input generati | `bab89a9c4d1832dbfef52b5853dc260e16d7ad2ff37cf54384b4db51a08c574b` |

VPK: 22.744.659 byte; SELF: 22.943.538 byte. Sono conservate copie indipendenti
di ELF, map, VELF, VPK, SELF e manifest nella directory
`build/vita-psarc/install-native-stack-20261006T165133Z`, per analizzare eventuali
nuovi dump anche dopo build successive. Il VPK archiviato è
[wiicompiled-vita-game.vpk](../build/vita-psarc/install-native-stack-20261006T165133Z/wiicompiled-vita-game.vpk).

Installazione completata il 6 ottobre alle 16:53:54 UTC (18:53:54 CEST), senza
avvio automatico. È stato aggiornato il contenuto dell'app già registrata
`WICMKW001`: il solo `eboot.bin` differiva, gli altri 13 file erano identici.
La rilettura di tutti i 14 file coincide con il nuovo pacchetto e il SELF
installato ha SHA-256 `69f304c002fdb06155b013ccafc9d20149b68c101fba25f3af5ae4b5c3bdd101`.

Il precedente SELF è conservato anche sulla console come
`ux0:/app/WICMKW001/eboot.bin.crashed-psarc-20261006T165133Z`.
Config.toml è stato riletto e rimane identico alla cattura del crash, SHA-256
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
Il PSARC non è stato modificato o trasferito; la dimensione remota è
2.642.224.628 byte. Non è stato calcolato l'hash dell'intero PSARC remoto.
La prova dell'installazione è in
[INSTALL.json](../build/vita-psarc/install-native-stack-20261006T165133Z/INSTALL.json).

## Prossima verifica sulla Vita

Avviare la candidata solo dopo installazione verificata. Controllare il nuovo
marker e che lo SP dei messaggi `[gx-native]` appartenga all'intervallo dello
stack nativo. Verificare prima avvio/menu, poi caricamento pista e gara. Se
compare un nuovo dump, preservarlo con il SELF/ELF della candidata prima di
intervenire. EFB depth, sospensione/ripresa e prestazioni restano verifiche successive.
