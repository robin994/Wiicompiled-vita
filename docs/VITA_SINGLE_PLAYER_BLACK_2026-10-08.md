# WiiCompiled Vita: nero dopo "1 Player" nella build con fix GX/audio

## Sessione osservata

- Console: `192.168.1.79:1337`, applicazione `WICMKW001`.
- SELF installato e verificato: SHA-256 `29d99f25106cddd79bbe67046d9764dd1992b6b02fb4df513e26f8e8d65207cb`.
- Marker: `psarc-resident-assets-async-oom-20261008`, GX asincrono attivo (`requested=1 running=1`).
- Diagnostica FTP letta in sola lettura e conservata in `build/diagnostics-oneplayer-black-20261008/`. Nessuna nuova installazione/avvio durante la cattura.
- L'utente segnala nero subito dopo "1 Player" con audio ancora udibile. L'ultima sessione loggata arriva al frame 334; il log non cresce tra 10:43:54 e 10:47:59 UTC. Il solo log non distingue attesa lunga, blocco o uscita senza dump.

## Evidenze

1. Il vecchio errore di handshake sembra eliminato in questa sessione: nessuna riga `invalid begin_frame`, `create offscreen sync (0x805b0017)` o `invalid native texture binding`.
2. Il DMA audio continua dopo un rifiuto temporaneo: `[audio] Audio: output queue full; dropping DMA block without stopping DMA`; al frame 300 risultano 3.042.816 campioni accettati e consumati, 426 blocchi rifiutati e `output_errors=0`. Il dato non dimostra da solo la qualita' percepita dell'audio.
3. Il filmato 512x192 restituisce immagini, dopo alcuni `Pending`; passando al successivo AVC 512x512 il decoder risponde con un frame vecchio da 512x192 e il codice lo rifiuta: `[avc-hardware] rejected output count=1 size=512x192 expected=512x512`. Segue `[video-native] avc-error`.
4. Nello stesso intervallo sono presenti copie EFB con source `0,456 32x32` sulla superficie logica `608x456`; il mapping arriva a `0,544 51x0`, con log `copy-failed`. Queste copie falliscono anche prima della transizione, pertanto non e' dimostrato che siano la causa del nero.
5. `scene_budget_exceeded scenes=29` e poi `40` a fronte di budget 5: il conteggio aggregato non basta per attribuire una saturazione delle risorse driver. Nessun nuovo dump kernel appare nella lista di `ux0:/data` durante la cattura.
6. Sono stati pubblicati frame regolari sino al 334, inclusa una finestra diagnostica a 26,185 Hz da 64 frame; la correttezza dei pixel mostrati non e' dimostrata da `completed=1`.

## Fix locale non ancora provato su Vita

`vita/avc_hardware.cpp`: prima della modifica il decoder memorizzava la geometria soltanto dopo `Pending` e la cancellava dopo ogni `Picture`; un cambio immediatamente successivo a `Picture` lasciava attiva la vecchia sequenza hardware. Il decoder ora conserva `activeWidth/activeHeight` per tutti gli invii e viene ricreato a ogni cambio di risoluzione, anche dopo `Picture`. Un marker `geometry-switch` registra i cambiamenti con output limitato.

`vita/tests/avc_hardware_test.cpp`: il mock simula l'output ritardato della vecchia geometria e include il cambio 672x752 -> 512x192 -> 512x512 subito dopo `Picture`, senza `Pending` intermedio. Il test host `avc-hardware-lifecycle-contract` passa; `git diff --check` e il controllo del submodule Aurora passano.

`aurora-main/platforms/vita/gxm/gxm_renderer.cpp`: soltanto con `MKW_TARGET_VITA` e diagnostica attiva, i nuovi marker `pipeline_start` / `pipeline_ready` delimitano il lavoro del patcher su ogni nuova pipeline durante la transizione. Non cambiano la semantica del renderer ne' aggiungono log alle altre implementazioni Aurora.

## Limiti e prossimo test

Questa e' una correzione di un difetto riprodotto nel flusso AVC, **non la prova di aver risolto la schermata nera**. Il log resta fermo durante la transizione e contiene nuove creazioni di material/shader GXM: se il nero persiste dopo aver corretto la geometria, bisogna diagnosticare separatamente il punto in cui smette di progredire il frame, l'eventuale attesa del consumer GX e le copie EFB fuori viewport. Non aumentare in blocco il numero di scene ne' disabilitare validazioni GXM.

Nessuna build Vita, installazione, avvio, commit o push e' stata eseguita in questo intervento. Il test su hardware richiede rigenerare **anche il SELF** (non solo `wiicompiled-vita-game`), quindi installare manualmente e ripetere il passaggio dopo "1 Player" osservando `geometry-switch`, `avc-native`, `frame-health`, `runtime-profile` e le eventuali prime righe di errore.

## Seconda prova: la geometria AVC e' corretta, il nero persiste

- Il SELF `36f5afe7191a9cfd2f6e8e691240c3608fe7f19161327d390ded7c41078f43dc` e' stato installato e verificato nella consegna successiva.
- L'utente riferisce di nuovo schermo nero dopo `1 Player`, ma audio udibile.
- La nuova sessione e i due log diagnostici sono conservati sotto `build/diagnostics-oneplayer-black-20261008/new-build-36f5afe7/`.
- Il frame 472 completa una finestra da 64 frame in 2.014.809 microsecondi (~31,765 Hz, menu/transizione semplificata, **non gameplay**). Non vengono pubblicati altri `[runtime-profile]` dopo.
- Il decoder ora stampa `geometry-switch n=1 old=512x192 new=512x512 reset=1`; entrambi i primi frame della nuova sequenza sono `Pending` (nero neutro pubblicato). Il vecchio `rejected output size=512x192 expected=512x512` non compare piu'.
- Ogni `pipeline_start` della sessione ha un corrispondente `pipeline_ready` (228/228). Le ultime cinque pipeline vengono registrate completamente, la piu' lenta in circa 20 ms; il blocco non e' **dimostrato** all'interno della loro compilazione o patching.
- Rimangono `copy-failed` su sorgenti logiche `0,456 32x32` e `32,456 32x32`, mappate a `y=544, height=0` in un target 960x544. `GXCopyTex(clear=true)` prima perdeva sia la copia sia il clear; il runtime registrava inoltre letture differite CPU per i medesimi destinatari anche se il backend non creava alcuna texture.
- La dimensione/MDTM di `runtime.log` e' rimasta costante da 13:44:57 a 13:49:20 UTC. Nessun nuovo `fatal`, crash kernel della sessione, errore `invalid begin_frame` o `create offscreen sync` nella cattura. L'assenza di output non distingue da sola un worker GXM bloccato da un main guest fermo.

## Seconda iterazione locale (nessun nuovo VPK installato)

1. Nel solo backend GXM con `MKW_TARGET_VITA`, una sorgente EFB **totalmente** fuori target viene rappresentata da un target RGBA8 zero-inizializzato; il risultato viene conservato nel normale registro delle copie, con revisione, ramPending e trattamento del `clear`. La stessa destinazione sintetica di uguali dimensioni viene riutilizzata senza altre allocazioni GPU. Le sorgenti parzialmente inquadrate, valide o in errore per altri motivi mantengono il percorso precedente. Marker: `[aurora-vita] gx_copy_empty`.
2. `aurora-main/lib/gx/fifo.cpp` espone uno snapshot lock-free del seriale GX inserito/completato, del job corrente (1=FIFO, 2=callback, 3=stop) e del segmento attivo.
3. `aurora-main/platforms/vita/gxm/gxm_renderer.cpp` espone l'operazione GXM attuale con diagnostica attiva: 1=EndScene, 2=BeginScene, 3=Draw, 4=Finish, 5=DisplayQueueAddEntry, 6=TransferFinish, 0=nessuna chiamata tracciata.
4. `vita/game_aurora.cpp` pubblica l'ultimo indice frame completato e i progressi FIFO senza mutex/attese. Il thread audio esistente (`vita/game_audio.cpp`) controlla queste informazioni ogni secondo e, quando i frame sono fermi per almeno 8 secondi, registra al massimo una riga `[gx-stall]` ogni 10 secondi. **Nessun nuovo thread** e nessuna attesa GPU dal thread audio.

### Come interpretare il prossimo `[gx-stall]`

Se `submitted > completed` e `active_type=1`, il consumer e' occupato a processare un job FIFO; `gxm_op=4` identifica in particolare una chiamata bloccata a `sceGxmFinish`, mentre `gxm_op=2` indica `sceGxmBeginScene`. Con `active_type=2` puo' essere attiva una callback, ad esempio il begin/end frame. Se `submitted == completed` e `active_type=0`, il consumer non ha lavoro in sospeso: cercare il blocco nel guest PPC, nel loader o nelle attese lato producer invece di attribuirlo al renderer GPU. Sono segnali diagnostici, non automaticamente la causa ultima.

### Validazione locale

- Test host dei limiti EFB `aurora-main/tests/vita_efb_empty_source_test.cpp` compilato con `c++ -std=c++20 -Wall -Wextra -Werror -pedantic` e passato: gli esatti rettangoli del log sono riconosciuti come totalmente esterni, le sovrapposizioni parziali non lo sono.
- `avc-video-contract` e `avc-hardware-lifecycle-contract`: 2/2 PASS.
- `git diff --check` nel progetto e in `aurora-main`: PASS.
- La correzione EFB e il watchdog **non sono ancora verificati su hardware**. Non e' stata avviata alcuna build Vita, reinstallazione, avvio, commit o push.

Compilazione manuale successiva, comprensiva del SELF aggiornato nel VPK:

```sh
cmake --build build/vita-psarc --target wiicompiled-vita-game.vpk-vpk -j8
```

## Terza prova hardware: GX non e' la sorgente del freeze

- Dopo l'installazione del SELF `f1958f0347ce15a27bb8e964de5585ddfef957a5e9918d8ddafdc7e50c9d0dd4`, l'utente vede ancora schermo nero dopo `1 Player` e sente ancora audio.
- Sessione conservata in `build/diagnostics-oneplayer-black-20261008/new-watchdog-f1958f/runtime-session.log`.
- `GXCopyTex` fuori framebuffer e' ora riconosciuto, con `[aurora-vita] gx_copy_empty n=16 ... clear=1`; non sono piu' presenti i precedenti errori di `copy-failed` per quella regione.
- Decoder AVC: il reset alla geometria 512x512 avviene correttamente; i primi due componenti 512x512 generano `accepted-without-picture` (frame decoder non ancora disponibile). Non si puo' concludere che l'AVC abbia causato il freeze.
- Tutte le nuove pipeline terminano con `pipeline_ready`; il guest ha presentato fino al **frame 349**.
- Tre campioni del watchdog a ~8, ~18 e ~28 secondi dal fermo riportano, identici, **`submitted=56836 completed=56836 active_serial=0 active_type=0 gxm_op=0`**. Il worker GX e' completamente inattivo. Non c'e' un job FIFO pendente ne' una chiamata GXM in corso. Il blocco riguarda il percorso di produzione frame (guest PPC, scheduler e/o worker I/O), non un wait interno al renderer Aurora.
- L'ultimo log non diagnostico prima degli stall e' la chiusura dell'ultima pipeline della transizione. Poco prima, dopo il frame 344, vengono creati due nuovi thread OS guest. Il collegamento con un'attesa THP, di messaggio o di risorsa richiede ancora un riscontro, non e' una causa confermata.

## Terza iterazione diagnostica (solo sorgenti, build Vita manuale)

Il nuovo `runtime/include/guest_stall_probe.h` e' un pannello lock-free di soli atomici, disponibile nei sorgenti OS HLE **soltanto con** `MKW_TARGET_VITA && WIC_CPU_DIAGNOSTICS` e campionato dal thread audio esistente ogni 10 s di stall. Non modifica lo scheduler e non aggiunge thread:

- numero chiamate `SelectThread`, poll dell'idle scheduler, context switch guest, sleep, wakeup e resume;
- OSMessageQueue send/receive/wait, ultima coda, ultimo thread guest coinvolto, ultimo thread creato e relativo indirizzo di entry;
- numero job `TaskThread_run`, callback iniziate/completate e fase dell'eventuale `MovieManager::Prepare` (`task_phase=40` se bloccato dentro il `THP__PlayerPrepare`);
- stato e contatore `runClocks` del **thread principale Vita** tramite `sceKernelGetThreadInfo`: il delta permette di distinguere un host thread in attesa da uno che continua a consumare CPU;
- `vita/psarc_backend.cpp` espone in sola lettura il numero di richieste PSARC inviate/completate, lo stato del worker e l'operazione in corso; evitare falsi sospetti di deadlock I/O se `submitted == completed` e `state == 0`.

I nuovi marker sono `[guest-stall]` e `[psarc-stall]` subito dopo ogni `[gx-stall]`. Con questi dati distinguere: (a) thread guest in idle scheduler che attende un wakeup/queue; (b) task THP bloccato in callback; (c) thread Vita fermo dentro una syscall/lock; (d) guest in ciclo CPU; (e) PSARC worker che non completa una richiesta.

Validazione effettuata senza compilare il VPK: controllo C++ host dello snapshot atomico `guest_stall_probe.h` PASS, cross-compiler Vita `-fsyntax-only` PASS per `vita/game_audio.cpp`, `vita/game_main.cpp` (con i suoi flag sorgente), `vita/psarc_backend.cpp`, `os_scheduler.cpp`, `os_thread.cpp`, `os_sleep.cpp`, `os_message.cpp` e `task_thread.cpp`. Test AVC host 2/2 PASS. Nessuna nuova compilazione ELF/SELF/VPK o installazione hardware eseguita.

**Non viene applicato un workaround che risvegli arbitrariamente i thread guest:** serve la prima cattura `[guest-stall]` per non introdurre corruzione nelle code di scheduling. Per il test successivo eseguire la medesima build completa del VPK mostrata sopra, installare, riprovare `1 Player` e analizzare i nuovi tre marker.

## Quarta prova: regressione temporale dopo la gestione audio QueueFull

Il test hardware con il SELF `29aa4d925a0e57a728c893f2d32b9fb808b3967109fab16914084446771684ba` mostra ancora nero dopo `1 Player`. I log recenti sono in `build/diagnostics-oneplayer-black-20261008/audio-regression-current/runtime-session.log`.

La sessione di riferimento **precedente alle modifiche audio/GX**, archiviata in `device-runs/resident-menu-race-20261008T085716Z/runtime-session.log`, attraversava i menu fino ad almeno il frame 1391 (poi errore renderer in gara). Un blocco audio rifiutato a coda piena era interpretato come errore DMA definitivo; il DMA si fermava a 647.616 campioni consegnati, ma il menu avanzava.

Nella sessione nuova il DMA resta vivo dopo `QueueFull`, come richiesto dalla correzione, ma i frame si fermano a 376. All'ultimo rilevamento periodico l'output aveva accettato e consumato 3.641.472 campioni, con 482 blocchi rifiutati; il watchdog a 38 s registra 65.274 job GX inviati e completati, nessun job GXM pendente, nessuna chiamata GXM attiva. I quattro campioni `[guest-stall]` mostrano il processo Vita in esecuzione e il numero di switch, sleep, wakeup, OSMessageQueue send/recv in continuo aumento. La `[psarc-stall]` riporta operazioni completate, senza attesa del worker. Le 101 callback `TaskThread_run` avviate risultano tutte completate, `task_phase=1` (attesa normale del job successivo).

La regressione e' temporalmente correlata alla correzione che preserva AI DMA e callback dopo l'overflow. **Non e' dimostrato che questa sia l'unica causa**, perche' nello stesso lotto era cambiato anche l'handshake `begin_frame` asincrono. Il controllo corretto e' un A/B con stesso binario e configurazione, variando soltanto la semantica del primo `QueueFull`.

### A/B riproducibile nello stesso VPK

Aggiunta solo su Vita la chiave `audio.legacy_stop_dma_on_queue_full` a `Config.toml`, letta tramite il parser TOML esistente e con default `true` sulle build Vita quando la chiave manca. Non viene modificata automaticamente la configurazione attualmente installata. Le altre piattaforme conservano sempre la gestione `QueueFull` non fatale.

**A (recupero regressione, default sulla Vita)**:

```toml
[audio]
legacy_stop_dma_on_queue_full = true
```

Al primo `QueueFull` disabilita `g_ai.enabled` e non esegue il callback del blocco, esattamente come nella build prima della correzione. Registra `[audio-regression] queue_full_policy=legacy_stop` e `[audio] Audio: queue full; LEGACY stopping DMA for menu regression test`. **L'audio tornera' a interrompersi**: non e' la soluzione definitiva, e' il controllo per isolare la causa del menu nero.

**B (fix audio con callback ancora vivi)**:

```toml
[audio]
legacy_stop_dma_on_queue_full = false
```

Al primo `QueueFull` scarta il blocco PCM ma continua AI DMA, callback e scheduler come nella build che mostra nero; registra `[audio-regression] queue_full_policy=continue`. Non ci sono altre differenze di codice fra A e B.

Confrontare la progressione dopo `1 Player`, massimo frame osservato, e le righe `[gx-stall]`/`[guest-stall]`. Se A ripristina i menu e B no, la regressione e' nella prosecuzione AI DMA/callback dopo overflow. Il fix successivo deve consentire audio continuo **senza** monopolizzare lo scheduler/ritardare i retrace VI; non lasciare `legacy_stop` come soluzione finale. Se entrambi restano neri, verificare separatamente le altre modifiche del lotto originale (handshake frame e foto renderer), sempre isolando un singolo cambiamento per test.

### Verifiche non-hardware

- Test host `runtime/tests/runtime_config_tests.cpp` esteso a `true`, `false` e tipo non valido per la nuova chiave; compilato ed eseguito con successo.
- `runtime/src/hle/audio/audio.cpp` passa la verifica `arm-vita-eabi-g++ -fsyntax-only` con i flag di build Vita.
- `git diff --check` sul progetto principale e su Aurora sono puliti.
- Nessuna build Vita, installazione hardware o cambio della configurazione PS Vita eseguito in questa iterazione.

## Ripresa dell'8 ottobre: starvation del retrace nel ciclo idle

La rilettura FTP del dispositivo conferma ancora il SELF
`29aa4d925a0e57a728c893f2d32b9fb808b3967109fab16914084446771684ba`.
La cattura completa, la configurazione e il SELF sono conservati in
`build/regression-player1-20261008/capture-current/`; il log dell'ultima
sessione coincide con il blocco al frame 376 descritto sopra. Non e' una
nuova prova della candidata. La configurazione letta non contiene la chiave
`audio.legacy_stop_dma_on_queue_full`.

E' stato individuato e riprodotto un difetto concreto in
`runtime/src/hle/os/os_scheduler.cpp`: nel ciclo idle `Audio_HLE_Poll`
precedeva `VI_HLE_PollRetrace`, con un `break` intermedio quando l'audio
rendeva un thread eseguibile. Risvegli audio ripetuti potevano quindi evitare
indefinitamente il servizio VI. Questo spiega un percorso compatibile con
i campioni hardware: aumentano selezioni, switch, messaggi e richieste PSARC,
mentre i frame restano a 376 e `idle_polls` a 609. Il solo valore di
`idle_polls` non misurava i retrace: contava soltanto i cicli che arrivavano
all'attesa in fondo al loop.

### Correzione candidata

- Sulla Vita il ciclo idle chiama `VI_HLE_ProcessRetracesDeferred(8)` prima
  dei timer sleep e dell'audio. Il percorso esistente usa una copia dei
  registri e rinvia i context switch durante il callback; non esegue lavoro
  ricorsivo Aurora. Rimangono la cadenza temporale e il budget dei retrace,
  senza forzare frame o risvegliare thread estranei alle code.
- `legacy_stop_dma_on_queue_full` resta un controllo esplicito per le prove,
  ma il default Vita torna `false`, anche nei nuovi file di configurazione.
  La candidata mantiene DMA/callback attivi e scarta soltanto il PCM che
  non entra nella coda. La configurazione del dispositivo non e' stata
  modificata.
- `[guest-stall]` registra anche `vi_polls` e `vi_retraces`, aggiornati dai
  percorsi VI effettivi. Nessun nuovo thread o attesa GPU.
- Marker della build residente asincrona:
  `psarc-resident-assets-async-vi-fair-20261008`.

### Riproduzione e validazione locale

`runtime/tests/idle_retrace_tests.cpp` compila il vero `SelectThread` e il
vero codice delle code/wakeup. Le sole dipendenze sostituite sono storage
RAM, dispositivi interrupt e cambio di fibra host. L'audio risveglia un
thread a priorita' 4 a ogni ciclo idle; un VI periodico risveglia il
produttore frame a priorita' 16. Una seconda variante aggiunge il risveglio
da sleep timer prima dell'audio.

- Controllo con il vecchio ordine di servizio: **FAIL**, 512 poll audio,
  0 poll VI, 0 progressi del produttore frame.
- Candidata, entrambe le varianti: **PASS**, 410 poll audio, 410 poll VI,
  102 progressi del produttore frame su 512 selezioni scheduler.
- Stesso test con AddressSanitizer e UndefinedBehaviorSanitizer: **PASS**.
- Suite host corrente: **21/21 PASS**, inclusi memoria differita,
  replay heap, input, AVC e wrapper THP.
- Parser della configurazione audio: **PASS**.

Questi risultati provano il difetto dello scheduler e la sua correzione
nel test locale. La scomparsa del nero sul dispositivo deve essere
confermata installando la nuova candidata, passando da `1 Player` ai menu
successivi e verificando immagini, risposta ai comandi e audio continuo.
La precedente build locale e i suoi ELF/map/VELF/SELF/VPK sono conservati
in `build/regression-player1-20261008/before/`.

### Pacchetto pronto per il test

- VPK: `build/vita-psarc/wiicompiled-vita-game.vpk`, 24.992.440 byte.
- SHA-256 VPK:
  `853663e18b64650fd44497d59781a3dc73dbdf9d80a8da4cf7d4c82257d54e75`.
- SHA-256 SELF nel pacchetto:
  `56f944cdd8e02e00004f9e8d7de2627031738e70999c75b8203d100ab1f3c1a2`.
- SHA-256 ELF:
  `c767712dd90d1c051b2f407e75ea9e2a7965d355f4ce938573a5ec2377b918a0`.
- ELF/VELF/SELF/VPK rigenerati con le opzioni della build corrente.
  Controllo GXM su 45.599 simboli, linkage pthread/SceFiber e barriera
  begin/end frame del worker asincrono: **PASS**. Il verificatore async
  accetta ora `--boot-marker` per vincolare il controllo al marker esatto
  della candidata, mantenendo gli stessi controlli ARM.
- `build/vita-psarc/DELIVERY.json` verifica corrispondenza SELF/VPK e SFO
  `TITLE_ID=WICMKW001`, `ATTRIBUTE2=12`; registra opzioni e sorgenti.
- Artefatti e risultati archiviati anche in
  `build/regression-player1-20261008/candidate/`.

Il VPK e' locale, non installato durante questa ripresa. La cattura FTP
e' stata esclusivamente in lettura. Per la prova installare il VPK,
avviare il gioco e ripetere il passaggio `1 Player` osservando che i menu
successivi siano visibili e utilizzabili e che l'audio continui. Con la
configurazione letta oggi non serve modificare `Config.toml`; se in seguito
viene impostato esplicitamente `legacy_stop_dma_on_queue_full = true`,
usare `false` per provare la candidata con DMA attivo.
