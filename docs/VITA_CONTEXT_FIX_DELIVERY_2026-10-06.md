# WiiCompiled Vita: candidata SceFiber/pthread installata

**Esito del test manuale:** il dump `1791319267` conferma un arresto prima
di main dovuto al collegamento pthread incompleto. Forzare pthread_cancel
ha riattivato il gate GCC lasciando pthread_once e quattro primitive condition
weak undefined. I controlli runtime descritti sotto non sono stati eseguiti.
Il precedente audit passava perché verificava il gate senza tutte le primitive.
La diagnosi e la candidata corretta pronta localmente, ancora non installata,
sono nel [report del crash iniziale](VITA_CRASH_STARTUP_2026-10-06.md).
Il resto di questo documento conserva la consegna e le verifiche eseguite
prima del test; non dimostra il funzionamento del firmware.

La candidata **`psarc-native-context-20261006`** è compilata e installata
sulla console `192.168.1.79:1337`, nella stessa applicazione `WICMKW001`.
Tutti i 14 file del pacchetto riletti dalla console coincidono con il VPK.
È cambiato solo eboot.bin. Il gioco non è stato avviato automaticamente.
Il PSARC e Config.toml non sono stati modificati.

L'intervento segue la [diagnosi del quinto crash](VITA_CRASH_AUDIO_2026-10-06.md).
Corregge il collegamento dei thread e sostituisce sulla Vita il cambio manuale
di pila libco con il backend SceFiber. Il successo della compilazione e
dell'installazione non prova ancora l'esito sul firmware, il menu o la gara.

## Implementazione

Il target del gioco forza l'inclusione della vera funzione pthread_cancel
della libreria Vita con `-Wl,-u,pthread_cancel`. Le istruzioni dei gate GCC
e std::thread ora contengono il suo indirizzo, invece dello zero del SELF
precedente. Questo riattiva i percorsi pthread e la TLS per thread.
Il controllo [test_vita_runtime_linkage.py](../scripts/test_vita_runtime_linkage.py)
è obbligatorio nella procedura `build-vita.sh game`: rifiuta la funzione
mancante, i gate non risolti, l'assenza delle API native o un vecchio switch
libco ancora collegato. È stato controllato anche contro il vecchio ELF,
che viene rifiutato per il difetto noto.

Il [backend HostContext](../runtime/src/host_context.cpp) usa le
[API SceFiber esposte da VitaSDK](https://raw.githubusercontent.com/vitasdk/vita-headers/master/include/psp2/fiber.h)
solo sul target Vita effettivo. Inizializza il modulo, alloca i contesti e
le pile allineate e usa Run dalla pila nativa, Switch tra contesti guest
e ReturnToThread per tornare al dispatcher. Verifica che i contesti
appartengano allo stesso thread nativo. Finalize precede il rilascio di
contesto e pila; una mancata inizializzazione o un errore API non viene
trattato come un handle valido. Gli altri backend host restano quelli
precedenti. Il vecchio codice ARM libco non è collegato al SELF del gioco.

Il dispatcher conserva il ritorno al richiedente effettivo anche dopo uno
switch diretto tra guest. Le correzioni precedenti di GXM, DVD, logging e
attese native restano attive. Questa implementazione richiede il test sul
firmware per verificare il problema di pila emerso nei dump.

La creazione del worker AX viene ora eseguita tramite il dispatcher sulla
pila nativa. Un fallimento std::system_error registra valore, categoria e
messaggio. Sulla Vita il fallimento viene memorizzato per evitare tentativi
illimitati; il fallback inline rimane disponibile. Un successo scrive
`[audio] AX/DSP mix worker started`. Le callback guest non sono spostate
indiscriminatamente sul worker audio.

## Verifica iniziale sulla console

Prima dell'inizializzazione GXM e del caricamento del gioco,
[CheckRuntimeContracts](../vita/runtime_checks.cpp) esegue:

1. Una verifica che il punto di partenza sia sulla pila nativa.
2. La creazione e il join di un vero std::thread, controllando valore iniziale,
   indirizzo e isolamento della variabile thread_local rispetto al main.
3. Lo stesso controllo con un thread creato direttamente da SceKernel,
   come quelli già usati da audio e PSARC.
4. La verifica che un worker nativo esegua il dispatcher nel proprio thread,
   senza utilizzare lo scheduler del main.
5. Due contesti SceFiber con ritorni nativi e switch diretto tra guest.
   La traccia attesa è `A N B M b a`: prova il ritorno al richiedente corretto.
6. La conservazione di TLS e degli otto registri VFP d8-d15, mantenuti
   effettivamente live nel primo contesto e sovrascritti nel secondo.
7. La finalizzazione dei contesti e il ritorno al normale bootstrap del gioco.

In caso di fallimento il bootstrap si interrompe con una diagnostica,
prima di eseguire il codice PPC. I marker attesi sono:

```text
[boot] build=psarc-native-context-20261006 ...
[runtime-check] start backend=scefiber pthread=linked
[runtime-check] std-thread=ok tls=isolated dispatch=local
[runtime-check] kernel-thread=ok tls=isolated dispatch=local
[runtime-check] scefiber=ok peer-switch=ok native-return=ok requester=ok d8-d15=ok
```

Questi controlli sono **compilati nella candidata e ancora da eseguire
sulla Vita**. Non sono risultati hardware già ottenuti. Dopo il loro successo
la build prosegue automaticamente con la verifica del PSARC e l'avvio guest.

## Verifiche prima della consegna

- 7/7 contratti host passati: PSARC, ISO, indice, EFB, contesti host,
  OSReport nativo e attese native. Non eseguono SceFiber sul firmware.
- Build Vita completata, con il warning VitaSDK preesistente su crtn.o.
- Audit dell'ELF finale: gate GCC e std::thread risolti sulla vera
  pthread_cancel; API SceFiber e verifiche iniziali presenti; nessun
  co_switch/co_swap_function libco collegato.
- Controllo negativo: l'audit nuovo rifiuta l'ELF abbinato al quinto dump
  proprio perché pthread_cancel è irrisolta.
- Collegamenti stdio, OSReport e tutte le attese VI/GX/GKI/panic al
  dispatcher nativo verificati, inclusi i veneer ARM/Thumb.
- Audit GXM dell'ELF/map passato: 44.984 simboli eseguibili, draw/present
  nativi presenti, nessun GL/vitaGL/vita2d.
- Disassemblazione dei due probe conservata: esercitano davvero d8-d15.
  Il vecchio test delle istruzioni di switch ARM libco non si applica al
  nuovo backend; la verifica dei registri SceFiber attende il primo avvio.
- SELF dentro il VPK identico al SELF di build e a quello riletto dalla Vita.

Async GX, fast math, texture native sperimentali, geometria residente e
profiling guest rimangono OFF. Renderer GXM. Nessuna variazione a sorgenti
Aurora, PSARC, salvataggi o configurazione della console.

## Identità e installazione

Archivio indipendente della candidata:
`build/vita-psarc/install-native-context-20261006T203514Z`.
I puntatori `build/native-context-install-path.txt` e
`build/latest-install-path.txt` identificano questa consegna.

| Artefatto | Byte | SHA-256 |
| --- | ---: | --- |
| VPK | 22.744.926 | `b8724b69bb15187758536d8b5243074e3a85917ce91e3511c76ee7fc9d284c4a` |
| SELF installato | 22.947.578 | `14c43f7a71cccd188bb5a43735fa35acd45eb2ed2f9028f0ecad4845e6250453` |
| ELF | 69.634.480 | `b38ba017df1bd0607e4f7b9d46d8dea741fe39c8e6709ce68bd8f377c3e71617` |
| Map | 43.762.414 | `9e94b9e280df4aa6bcf606d3a4e0933b14bcd433a8d04facbb723ae25a057fb1` |
| VELF | 85.703.378 | `d8383eace9eff55092a1e64d007e5bbda8f04a71a702714fbd4f283947174e54` |

L'installazione aggiorna i contenuti del pacchetto dell'app già registrata.
L'upload temporaneo è stato riletto e verificato prima della sostituzione.
Il precedente SELF è conservato localmente e in un backup remoto identificato
da INSTALL.json. Config.toml è identico, SHA-256
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
La dimensione remota del PSARC è 2.642.224.628 byte. Il file non è stato
riscritto e non è stata ripristinata la ISO sulla console.

Manifest, prove e installazione:
[DELIVERY.json](../build/vita-psarc/install-native-context-20261006T203514Z/DELIVERY.json),
[VALIDATION.json](../build/vita-psarc/install-native-context-20261006T203514Z/VALIDATION.json),
[RUNTIME_LINKAGE.json](../build/vita-psarc/install-native-context-20261006T203514Z/RUNTIME_LINKAGE.json),
[INSTALL.json](../build/vita-psarc/install-native-context-20261006T203514Z/INSTALL.json).

Il prossimo test deve prima verificare i marker runtime-check, poi creazione
del mixer, menu, gara e durata della sessione. Un nuovo dump va analizzato
contro l'ELF di questa consegna, non contro quello del quinto crash.
