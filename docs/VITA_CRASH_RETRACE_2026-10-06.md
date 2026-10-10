# WiiCompiled Vita: quarto crash PSARC, attesa del retrace sulla pila libco

## Esito

Il nuovo coredump ferma `WICMKW001` nello stub importato di
`sceKernelDelayThread`, chiamato da `usleep` durante l'attesa host del retrace.
Il thread usa la pila della coroutine, fuori dall'intervallo nativo registrato.
Il nuovo confine da correggere è la sospensione host del percorso VI/idle.

La candidata `psarc-native-log-20261006` supera il precedente crash dei log:
l'intestazione completa di `StaticR.rel` è scritta, compare il messaggio
`[Rel] Load Module[1]` e il log registra la presentazione numero 512.
Questo è progresso del percorso hardware; non certifica menu, gara o
correttezza visiva. Il numero 512 è l'ultima presentazione numerata nel log,
non il frame esatto in cui è avvenuto il crash.

Alla cattura questo intervento conservava e analizzava il nuovo dump; la
correzione delle attese descritta sotto era proposta. È ora implementata nella
candidata `psarc-native-wait-20261006`, con test host e controlli del binario
finale superati. Implementazione, identità e stato di consegna sono nel
[report delle attese native](VITA_WAIT_FIX_DELIVERY_2026-10-06.md).
Le evidenze del dump nelle sezioni seguenti riguardano la precedente candidata.

## Cattura abbinata al binario

Console `192.168.1.79:1337`, app `WICMKW001`. Dump remoto:
`ux0:/data/psp2core-1791315331-0x0004fd28b1-eboot.bin.psp2dmp`.
Cattura iniziata il 6 ottobre 2026 alle 19:36:29 UTC.

| Artefatto | Byte | SHA-256 |
| --- | ---: | --- |
| Dump | 139.664 | `e34d56f6f03e41256fa09aff19879c71ea1c1c03f3fd2269172ecd756fe09cd2` |
| SELF riletto dalla Vita | 22.942.895 | `9557d79a1f1c14c50d910fc535fcea08d30ec0e6b6c53884a003755be5c35ab2` |
| ELF abbinato | 69.627.824 | `4c4bf7f485678741d50ce090a17d497083b74661213e60aab8406ab0f3f27742` |
| Map | 43.739.483 | `d7dccaa63975a409b0bccb4949a3bd3bbf928c1149e86b809d1dcf79bc7c58af` |
| VELF | 85.683.526 | `b7043c110809fb5fa7bff9ef78b7e483cfbb4d468d6b4f9c5a72b7e4e287da79` |

Il SELF recuperato coincide byte per byte con la candidata consegnata in
`build/vita-psarc/install-native-log-20261006T193150Z`.
ELF, map, VELF, VPK, SELF e manifest sono copiati nella nuova directory di
cattura. Config.toml rimane identico, SHA-256
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.

La successiva verifica della directory dati sulla console conferma il solo
`mariokart.psarc`, dopo la rimozione della ISO da parte dell'utente. Il percorso
`dvd_psarc` è selezionato e il percorso `dvd_iso` ancora presente nella
configurazione è ignorato. Le diciture DVD descrivono l'interfaccia del disco
del gioco, anche quando i dati sono letti dal PSARC.

Evidenze: [CAPTURE.json](../crashdumps/psarc-1791315331/CAPTURE.json),
[dump originale](../crashdumps/psarc-1791315331/crash.psp2dmp),
[analysis.txt](../crashdumps/psarc-1791315331/analysis.txt) e
[analysis.json](../crashdumps/psarc-1791315331/analysis.json).

## Punto di arresto

| Campo | Valore |
| --- | --- |
| Thread | `WICMKW001`, UID `0x40010003` |
| Stop reason | `0x10006`, conservato come codice grezzo |
| PC caricato | `0x8298fda8` |
| PC nell'ELF | `0x82965da8`, `sceKernelDelayThread+8` |
| LR caricato | `0x82983a89` |
| Ritorno nell'ELF, Thumb corretto | `0x82959a88`, `usleep` |
| SP | `0x8a3eaa18` |
| Pila nativa del thread | `[0x836c1000, 0x83701000)`, 262.144 byte |
| RX app caricato / RX ELF | `0x8102a000` / `0x81000000` |
| Argomento della chiamata | `r0=0x3db`, 987 microsecondi |

In `usleep`, l'istruzione ELF `0x82959a84` è `blx sceKernelDelayThread`;
il ritorno è esattamente `0x82959a88`, coerente con LR. Il valore richiesto
è positivo e inferiore a un millisecondo.

Il PC è nello stub di import, che il loader Vita modifica al caricamento.
Le parole NID conservate nell'ELF identificano la funzione e non vengono
interpretate come istruzioni eseguite. Il codice RX modificato non è qui
conservato dal dump; il significato preciso di `0x10006` non è dedotto dalla
sola posizione del PC.
Lo [stub originale](../crashdumps/psarc-1791315331/delay-import-stub.txt) e la
[disassemblazione dell'attesa](../crashdumps/psarc-1791315331/sleep-disassembly.txt)
conservano questi controlli.

## Percorso delle chiamate

I ritorni coerenti nello stack, verificati contro le istruzioni dell'ELF,
ricostruiscono il percorso seguente:

```text
func_80008D18 -> VIWaitForRetrace_HLE_801b99ec
  -> OSSleepThread_HLE_801aa9b8 -> SelectThread_801a9c08
  -> VI_HLE_WaitForNextRetracePoll -> SleepPreciselyUntil
  -> std::this_thread::sleep_until -> std::this_thread::__sleep_for
  -> usleep -> sceKernelDelayThread
```

La scansione è una raccolta di indirizzi salvati candidati, non un unwinding
completo. La relazione attiva `usleep -> sceKernelDelayThread` è confermata
anche da LR; i ritorni precedenti sono coerenti con le chiamate controllate.

Il ritorno `0x827e0945` corrisponde a `0x827b6944`, immediatamente dopo la
chiamata `sleep_until` a `0x827b6940` nel ramo senza spin finale di
`SleepPreciselyUntil`. Il ritorno `0x827e09db` corrisponde a `0x827b69da`,
dopo la chiamata a quel helper. Il ritorno `0x827c904d` corrisponde a
`0x8279f04c`, dopo `VI_HLE_WaitForNextRetracePoll` nel ciclo idle dello scheduler.

Il sorgente attende fino al prossimo retrace o fino a un millisecondo da ora,
in modo da tornare periodicamente al servizio di timer, audio e allarmi.
Il ramo corrente usa l'attesa senza spin, coerente con i 987 microsecondi
richiesti dopo il costo di preparazione. Non c'è un errore di timeout nel log.

Evidenze: [retrace-disassembly.txt](../crashdumps/psarc-1791315331/retrace-disassembly.txt),
[wait-source-evidence.txt](../crashdumps/psarc-1791315331/wait-source-evidence.txt)
e [wait-evidence.json](../crashdumps/psarc-1791315331/wait-evidence.json).

## Confronto fra thread

Il dump registra anche il worker PSARC nella stessa funzione importata:

| Thread | Stop reason | SP nella propria pila nativa | Attesa |
| --- | --- | --- | ---: |
| `WICMKW001` | `0x10006` | No, SP `0x8a3eaa18` | 987 us |
| `wic_psarc_io` | `0` | Sì, SP `0x83760fd8` in `[0x83751000, 0x83761000)` | 100 us |

Il worker effettua la normale pausa quando non ci sono richieste.
Questo confronto sostiene la diagnosi di confine incompleto per le attese
host del thread guest. Il punto di arresto e la pila sono accertati; il
meccanismo preciso dell'errore firmware e la sua eliminazione richiedono la
prova della prossima candidata. Il thread della display queue GXM non
riporta uno stop reason di errore.

## Progresso della candidata dei log

L'analisi usa soltanto l'ultima sessione `build=psarc-native-log-20261006`,
estratta in [runtime-session.log](../crashdumps/psarc-1791315331/runtime-session.log).
Il log completo contiene anche esecuzioni precedenti.

Le prime otto righe `[osreport-native]` registrano indirizzi dentro la pila
nativa. Le letture DVD di `English.szs` e `StaticR.rel` registrano anch'esse
indirizzi nativi. L'intestazione del REL contiene ora id `1`, 17 sezioni e
versione `3`, seguita dalle righe di offset, prolog, epilog, allineamenti e:

```text
[OSReport] [Rel] Load Module[1]: size=4903876Bytes time=789msec
```

Il caricatore del gioco segnala quindi il completamento del caricamento;
questa riga non prova da sola correttezza di ogni rilocazione o contenuto.
Il percorso prosegue con copie display e la presentazione numerata `512`.
L'ultimo contatore PSARC registra cinque letture, 12.969.569 byte e zero
fallimenti. Sono contatori del backend, non una nuova verifica SHA-256 di
tutti i payload remoti.

I tempi della display queue descrivono attese CPU nella submission. La
classificazione `gpu_backpressure` nel log è una diagnostica del backend,
non una misura diretta del tempo GPU né una prova di FPS del gioco.

## Prossima correzione proposta

1. Eseguire le attese host usate da `SleepPreciselyUntil` sulla pila nativa,
   tramite il dispatcher sincrono esistente. Mantenere deadline assolute,
   finestra finale di spin e ritorno alla stessa coroutine.
2. Lasciare callback PPC, servizio audio/allarmi, aggiornamenti delle code e
   cambi di thread guest nei loro contesti attuali. Il trasferimento riguarda
   l'operazione host di attesa.
3. Usare un confine comune anche per le altre attese HLE raggiungibili dalle
   coroutine: retry del frame GX, `GKI_delay` e pausa di Panic. Sono punti
   trovati nell'audit del sorgente, non cause attive di questo dump. Le pause
   dei worker già nativi mantengono il passaggio diretto.
4. Verificare con un test il ritorno al chiamante e la gestione delle deadline;
   controllare il percorso nel nuovo ELF e aggiungere marker di stack limitati.
   Poi verificare sulla Vita la prosecuzione oltre questa attesa e l'ingresso
   in menu/pista, conservando l'identità della candidata.

Non serve riconvertire il PSARC per correggere la chiamata individuata.
La candidata installata e gli artefatti del crash rimangono preservati.
