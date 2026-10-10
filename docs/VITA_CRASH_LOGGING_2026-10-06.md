# WiiCompiled Vita: terzo crash PSARC, OSReport e scrittura dei log

Aggiornamento successivo all'analisi: la correzione è ora implementata nella
candidata `psarc-native-log-20261006`, compilata e installata sulla Vita con
verifica dei contenuti. Dettagli e identità sono nel
[report di consegna](VITA_LOG_FIX_DELIVERY_2026-10-06.md).
Il gioco non è stato avviato automaticamente; l'esito hardware resta da provare.
Le sezioni sotto conservano la diagnosi e lo stato del precedente intervento
di sola analisi.

## Esito dell'analisi

Il nuovo dump ferma `WICMKW001` nello stub importato di `sceIoLseek32`.
La chiamata arriva dalla scrittura di `OSReport` su `runtime.log`, aperto in
append, mentre il thread usa ancora la pila della coroutine libco.
Il confine con i servizi nativi resta incompleto per il logging.

Il percorso raggiunto è la prima stampa dell'intestazione di `StaticR.rel`,
successiva alla lettura DVD e alla manutenzione della cache nel caricatore.
La candidata supera quindi il precedente punto di crash nell'azzeramento del
buffer da 4.903.876 byte in questa esecuzione. Menu, rilocazione completa del
modulo, gara e prestazioni restano da verificare.

Questo intervento recupera e analizza il dump e aggiorna la documentazione.
La correzione dei log descritta sotto è proposta: nessuna modifica al runtime,
nuova build, installazione o avvio automatico è stato eseguito.

## Cattura e identità

Console `192.168.1.79:1337`, app `WICMKW001`. Dump remoto:
`ux0:/data/psp2core-1791313305-0x0004e42af1-eboot.bin.psp2dmp`.
Cattura completata il 6 ottobre 2026 alle 19:02:28 UTC.

| Artefatto | Byte | SHA-256 |
| --- | ---: | --- |
| Dump | 146.816 | `06ca82b9747113f5a0a7ba5785b2aa110b48578400a83f143cf8c6cd36af3c53` |
| SELF riletto dalla console | 22.943.708 | `9448f0a8f3bfe21cac05e915280ae77b52bb365d9239b569c92c6f8be7b3d774` |
| ELF abbinato | 69.626.356 | `b6666b9b0f730194fad7a0ef76ce07991118ddb58b1f21f6ca3086807350a6c7` |
| Map | 43.735.766 | `b939ac44968307c9d52eea30b2e6802792ab87aaef60a412256027a1862909c7` |
| VELF | 85.681.806 | `265e4b4dc5f434884161a40225e19680149e9fef4c0e45560adc5ef03eafed03` |

Il SELF recuperato coincide byte per byte con quello della candidata
`psarc-native-dvd-20261006`, archiviata in
`build/vita-psarc/install-native-dvd-20261006T173104Z`. ELF, map, VELF, VPK,
SELF e manifest sono copiati nella nuova cartella di cattura. La configurazione
mantiene SHA-256 `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.

Evidenze: [CAPTURE.json](../crashdumps/psarc-1791313305/CAPTURE.json),
[dump originale](../crashdumps/psarc-1791313305/crash.psp2dmp),
[analysis.txt](../crashdumps/psarc-1791313305/analysis.txt) e
[analysis.json](../crashdumps/psarc-1791313305/analysis.json).
Il manifest di cattura include anche dimensioni e hash degli output derivati.

## Punto di arresto e chiamata attiva

| Campo | Valore |
| --- | --- |
| Thread | `WICMKW001`, UID `0x40010003` |
| Stop reason | `0x10006`, conservato come codice grezzo |
| PC caricato | `0x82980348` |
| PC nell'ELF | `0x82965348`, `sceIoLseek32+8` |
| LR caricato | `0x82973ac7` |
| Ritorno nell'ELF, Thumb corretto | `0x82958ac6`, `_lseek_r` |
| SP | `0x8a3ea6f8`, fuori dalla pila nativa |
| Pila nativa del thread | `[0x83481000, 0x834c1000)`, 262.144 byte |
| RX app caricato / RX ELF | `0x8101b000` / `0x81000000` |

I registri della chiamata sono `r0=0x400200b9`, `r1=0`, `r2=2`:
handle Vita, offset zero, `SEEK_END`. La disassemblazione di `_lseek_r`
mostra `blx sceIoLseek32` a `0x82958ac2`, con ritorno `0x82958ac6`.
In `__swrite`, il ramo append imposta proprio offset zero e `SEEK_END`
prima di chiamare `_lseek_r`.

Il file ELF conserva gli stub importati originali, che il loader modifica
al caricamento. Le parole NID a `sceIoLseek32+8` non sono interpretate come
istruzioni realmente eseguite; il dump identifica il PC nel relativo stub,
ma non conserva qui il codice RX modificato. Il significato preciso del
codice `0x10006` non è dedotto dalla sola posizione del PC.

I ritorni coerenti nello stack e le chiamate controllate nell'ELF riconducono a:

```text
func_8000A188 -> func_8000A440 -> OS__Report_801a25d0
  -> std::__ostream_insert / std::__ostream_write
  -> fwrite -> _fwrite_r -> __sfvwrite_r -> __sflush_r
  -> __swrite -> _lseek_r -> sceIoLseek32
```

Questa è una ricostruzione del percorso con indirizzi verificati, non un
unwinding completo. La scansione contiene anche valori residui e puntatori a
funzioni, che non sono trattati come frame attivi.
La [disassemblazione dei log](../crashdumps/psarc-1791313305/logging-disassembly.txt)
conserva gli intervalli usati per il controllo.

## Progresso dopo la correzione DVD

La diagnosi usa soltanto l'ultima sessione
`build=psarc-native-dvd-20261006`, estratta dal log completo in
[runtime-session.log](../crashdumps/psarc-1791313305/runtime-session.log).
Le presentazioni da 1 a 8 sono registrate; gli indirizzi delle prime quattro
appartengono all'intervallo nativo del thread.

Le due letture registrano:

```text
[dvd-native] read=1 stack=0x834bfda8 bytes=299969 path=/Boot/Strap/eu/English.szs
[dvd-native] read=2 stack=0x834bff10 bytes=4903876 path=/rel/StaticR.rel
```

Entrambe partono sulla pila nativa. Il marker `read=2`, da solo, documenta
l'ingresso nella lettura, non il suo completamento. L'evidenza successiva viene
dal dump: il ritorno salvato `0x81ed56e9` corrisponde a `0x81eba6e8`, dopo
la prima chiamata a `OSReport` di `func_8000A440`. Il ritorno del chiamante
`0x812f77ef` corrisponde a `0x812dc7ee`, dopo la chiamata a questa funzione.

Nel caricatore `func_8000A188`, quel percorso segue la lettura DVD, il controllo
contro una lettura corta e `DCFlushRange`. Il ramo di fallimento della lettura
non raggiunge questa stampa. Il formato della prima riga, ricavato dal rodata
dell'ELF abbinato all'indirizzo guest `0x80245ba3`, è `id                %d\n`.
Il gioco sta quindi tentando di stampare l'id del modulo REL dopo la lettura.
Il valore effettivo dell'id e l'intero buffer guest non sono recuperati dal dump.
Questa evidenza non certifica l'hash di tutti i dati letti sulla console.

I controlli sono conservati nella
[disassemblazione della chiamata](../crashdumps/psarc-1791313305/rel-report-disassembly.txt),
nella [traccia del caricatore](../crashdumps/psarc-1791313305/rel-loader-disassembly.txt),
negli [estratti del codice generato](../crashdumps/psarc-1791313305/rel-loader-source.txt)
e in [rel-report-evidence.json](../crashdumps/psarc-1791313305/rel-report-evidence.json).

## Confine da correggere

`vita/game_main.cpp` apre stdout e stderr sullo stesso `runtime.log` in modalità
append. `runtime/src/hle/os/os_report.cpp` formatta i messaggi e li invia a
`std::cout` con flush, senza passare dal dispatcher della pila nativa.
La chiamata corrente inserisce proprio il messaggio formattato: il ritorno
`0x827b945b` coincide con l'istruzione dopo `__ostream_insert` nell'ELF.

Il crash in una syscall eseguita dalla pila libco è coerente con il problema
di confine dei due dump precedenti. L'arresto in seek è accertato; il meccanismo
preciso con cui il firmware rifiuta o interrompe la chiamata richiede una nuova
prova dopo la correzione. Il dump non dimostra un errore nel contenuto PSARC.

La prossima modifica dovrebbe eseguire l'intero ciclo di `OSReport` attraverso
`HostContext::CallOnNativeStack`: cache delle stringhe guest, formattazione,
scrittura, flush e distruzione dei temporanei. Vanno mantenuti gli argomenti
variadici PPC, il contesto CPU, il filtro delle ripetizioni e la diagnostica
Warning/Panic. Spostare soltanto la write lascerebbe altre chiamate alle librerie
native nel ciclo di formattazione e rilascio dei buffer.

Anche `RT_LOG`, `RT_LOGF` e `LogMemoryError` scrivono direttamente su stdio.
Serve un confine comune per l'output dei log raggiungibile dalle coroutine,
con seek/write/flush sulla pila nativa e comportamento host conservato.
La correzione dovrà essere misurabile con marker e indirizzi di stack, senza
disabilitare la diagnostica né cambiare la modalità append per nascondere seek.

## Verifica richiesta per la prossima candidata

1. Controllare su host il passaggio sincrono, il ritorno alla coroutine e il
   ciclo completo dei temporanei; verificare formato e argomenti dei log.
2. Compilare e conservare ELF, map, VELF, SELF e manifest della nuova candidata.
3. Verificare sulla Vita che `OSReport` usi la pila nativa e che la stampa
   dell'intestazione REL completi. Poi verificare rilocazione, menu e pista.
4. Abbinarne qualsiasi nuovo dump al SELF riletto dalla console.

La sequenza delle catture resta consultabile nei report del
[primo crash GXM](VITA_CRASH_PSARC_2026-10-06.md) e del
[secondo crash DVD](VITA_CRASH_DVD_2026-10-06.md).
