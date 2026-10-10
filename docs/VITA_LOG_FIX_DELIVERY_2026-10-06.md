# WiiCompiled Vita: candidata con logging sulla pila nativa

Aggiornamento dopo l'avvio manuale: il nuovo dump conferma il superamento del
precedente punto di crash, con intestazione REL completa e messaggio di
caricamento del modulo nel log. Il nuovo arresto riguarda l'attesa host del
retrace sulla pila libco. Vedere il
[report del quarto crash](VITA_CRASH_RETRACE_2026-10-06.md).
La successiva candidata, con correzione delle attese implementata e installata,
è documentata nella [consegna native wait](VITA_WAIT_FIX_DELIVERY_2026-10-06.md).
Le sezioni sotto conservano implementazione, verifiche e stato alla consegna.

## Correzione del terzo crash

La candidata `psarc-native-log-20261006` corregge il confine individuato nel
[terzo coredump](VITA_CRASH_LOGGING_2026-10-06.md), dove `OSReport` raggiungeva
`sceIoLseek32` dalla pila libco durante l'append a `runtime.log`.

`runtime/src/hle/os/os_report.cpp` ora esegue l'intero ciclo sulla pila nativa
tramite il dispatcher sincrono dello stesso thread: pulizia della cache,
lettura delle stringhe guest, formattazione, filtro delle ripetizioni, output,
flush, diagnostica Warning/Panic e distruzione dei temporanei locali.
Il contesto CPU e gli argomenti variadici PPC rimangono quelli del chiamante,
che riprende subito dopo la richiesta senza rischedulare altri guest.
Il percorso host di produzione mantiene l'implementazione precedente.

`vita/game_stdio.cpp` completa il confine comune dell'output di newlib.
Il collegamento intercetta `_lseek_r` e `_write_r`: vengono eseguiti sulla
pila nativa sia il seek prima delle scritture append sia la scrittura stessa,
includendo gestione del descrittore e dell'errore nella funzione originale.
Questo copre i log iostream e printf che usano il backend stdio, senza riscrivere
i singoli chiamanti. Buffer, descrittore, offset, numero di byte, stato reentrant
e risultato sono inoltrati senza modifiche. I worker nativi senza scheduler
passano direttamente alla funzione originale.

La modalità append e i log rimangono attivi. Le prime otto chiamate `OSReport`
aggiungono il marker `[osreport-native]` con indirizzo dello stack e indirizzo
guest del formato. Rimangono le precedenti correzioni GXM/DVD; questo intervento
non modifica Aurora, l'archivio, le texture o le opzioni sperimentali.

## Verifiche completate

- **6/6 contratti host** superati: PSARC, ISO, indice, EFB, contesti e nuovo
  contratto `OSReport`.
- Il nuovo test compila l'HLE e il formatter reali con il percorso Vita
  abilitato, usando il vero cambio di contesto host. Verifica letture guest,
  output e flush sulla pila nativa, argomenti interi/floating point/stack PPC,
  allineamento a 64 bit, stringhe lunghe, ripetizioni, diagnostica Warning,
  contesto CPU conservato e ripresa della coroutine. Le dipendenze di memoria
  e diagnostica sono sostituite dal test; non esegue il firmware Vita.
- Build Vita completata. Audit dell'ELF/map: **44.947 simboli eseguibili**,
  draw/present GXM presenti, nessun GL/vitaGL/vita2d.
- Test delle istruzioni ARM del binario finale superato: `r4-r11`, `d8-d15`,
  SP e continuazione ARM/Thumb preservati.
- Controllo dell'ELF finale superato: `__swrite` raggiunge i due wrapper nativi;
  wrapper e `OSReport` raggiungono il dispatcher. Il controllo risolve i
  literal dei veneer ARM/Thumb effettivi, senza affidarsi soltanto ai nomi.
- SELF incluso nel VPK identico al SELF prodotto dalla build.

Log, disassemblazione e copie dei sorgenti modificati sono conservati nella
directory di consegna. Il warning VitaSDK di `crtn.o` senza `.note.GNU-stack`
rimane; la build non presenta errori.

## Identità della candidata

| Artefatto | SHA-256 |
| --- | --- |
| VPK | `80a13c13c12f5c1239170e60158ea3d124dbefa6b7fdd5d4cd02c83475a01854` |
| SELF | `9557d79a1f1c14c50d910fc535fcea08d30ec0e6b6c53884a003755be5c35ab2` |
| ELF | `4c4bf7f485678741d50ce090a17d497083b74661213e60aab8406ab0f3f27742` |
| Map | `d7dccaa63975a409b0bccb4949a3bd3bbf928c1149e86b809d1dcf79bc7c58af` |
| VELF | `b7043c110809fb5fa7bff9ef78b7e483cfbb4d468d6b4f9c5a72b7e4e287da79` |
| Digest sorgenti e input | `87b5d50e4e18324a0c26694f8f83ddeb30c1c53f84286de33df6b9c00f63385a` |

VPK: 22.746.066 byte. SELF: 22.942.895 byte.
Pacchetto archiviato:
[wiicompiled-vita-game.vpk](../build/vita-psarc/install-native-log-20261006T193150Z/wiicompiled-vita-game.vpk).
Manifest con identità ISO/PSARC e opzioni:
[DELIVERY.json](../build/vita-psarc/install-native-log-20261006T193150Z/DELIVERY.json).
Async GX, fast math, texture native sperimentali, geometria residente e profiling
guest rimangono OFF; backend GXM.

## Installazione verificata

Console `192.168.1.79:1337`, app registrata `WICMKW001`. È stato aggiornato
il contenuto del pacchetto nell'app esistente: il solo `eboot.bin` differiva.
Il nuovo SELF è stato caricato su un percorso temporaneo, riletto e verificato
prima della sostituzione. Tutti i **14 file** del VPK riletti dopo l'installazione
coincidono con quelli archiviati. SELF installato SHA-256:
`9557d79a1f1c14c50d910fc535fcea08d30ec0e6b6c53884a003755be5c35ab2`.

Il precedente eseguibile, SHA-256
`9448f0a8f3bfe21cac05e915280ae77b52bb365d9239b569c92c6f8be7b3d774`,
rimane conservato localmente e sulla Vita come
`ux0:/app/WICMKW001/eboot.bin.crashed-native-dvd-20261006T193150Z`.
La configurazione è stata riletta e rimane byte per byte identica, SHA-256
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
Il PSARC non è stato modificato o ritrasferito; la dimensione remota è
2.642.224.628 byte. L'hash dell'intero archivio remoto non è stato ricalcolato.
Non è stato inviato alcun comando di avvio.

Evidenze: [INSTALL.json](../build/vita-psarc/install-native-log-20261006T193150Z/INSTALL.json)
e [VALIDATION.json](../build/vita-psarc/install-native-log-20261006T193150Z/VALIDATION.json).
ELF, map, VELF, SELF, VPK e manifest abbinati rimangono indipendenti dai
successivi output di build nella stessa directory di consegna.

## Prova richiesta sul dispositivo

La compilazione e i test dimostrano il percorso implementato e il collegamento
del pacchetto; la risoluzione del crash richiede l'avvio manuale sulla Vita.
Nella nuova sessione verificare:

1. Marker `build=psarc-native-log-20261006` e indirizzo `[osreport-native]`
   dentro la pila nativa registrata all'avvio.
2. Completamento delle righe dell'intestazione di `StaticR.rel`, inclusi
   id, sezioni, offset e versione, e avanzamento del caricatore.
3. Menu e caricamento pista; conservare qualsiasi nuovo dump insieme al SELF
   installato e agli artefatti di questa candidata.

I dump e i pacchetti precedenti rimangono archiviati. Nessuna prova di menu,
gara, correttezza visiva o prestazioni è attribuita a questa nuova build.
