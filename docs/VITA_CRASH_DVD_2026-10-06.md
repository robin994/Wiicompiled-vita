# WiiCompiled Vita: secondo crash PSARC, buffer DVD sulla pila nativa

Aggiornamento dopo il successivo avvio manuale: il terzo dump mostra che la
candidata raggiunge la stampa dell'intestazione REL dopo la lettura e supera
il punto di arresto descritto qui. Il nuovo crash riguarda `OSReport` e il
seek di append ai log dalla pila libco. Il SELF installato è stato nuovamente
verificato. Vedere il [report del terzo crash](VITA_CRASH_LOGGING_2026-10-06.md).
Le sezioni sotto conservano diagnosi, correzione e stato alla consegna precedente.

## Cattura e identità

Console `192.168.1.79:1337`, app `WICMKW001`. Nuovo dump:
`ux0:/data/psp2core-1791307164-0x0004132601-eboot.bin.psp2dmp`.
La copia locale è [crash.psp2dmp](../crashdumps/psarc-1791307164/crash.psp2dmp),
146.848 byte, SHA-256
`897401a4d4c5e49bb6a5a92e1e80418607594cc0f28e06f57baa2671ee796148`.

Il SELF riletto dalla console coincide con la candidata del precedente intervento,
SHA-256 `69f304c002fdb06155b013ccafc9d20149b68c101fba25f3af5ae4b5c3bdd101`.
ELF, map, VELF, VPK, SELF e manifest corrispondenti sono stati copiati dalla
directory dell'installazione in `crashdumps/psarc-1791307164` prima delle modifiche.
[CAPTURE.json](../crashdumps/psarc-1791307164/CAPTURE.json) registra gli hash.

Il log completo contiene anche esecuzioni precedenti. La diagnosi usa la sola
ultima sessione `build=psarc-native-stack-20261006`, salvata in
[runtime-session.log](../crashdumps/psarc-1791307164/runtime-session.log).
Config.toml è identico alla precedente cattura:
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.

## Risultato della precedente correzione sul dispositivo

La pila nativa è `[0x836c1000, 0x83701000)`. Le prime quattro presentazioni
registrano `stack=0x8370043c`, dentro questo intervallo. Il log completa le
presentazioni da 1 a 8, prosegue nell'inizializzazione del sistema del gioco
e registra una copia display. Il nuovo crash avviene nel thread di caricamento.

Questo è progresso hardware rispetto al crash GXM della
[prima cattura](VITA_CRASH_PSARC_2026-10-06.md). Non dimostra ancora menu,
gara, correttezza visiva o prestazioni stabili.

## Nuovo punto del crash

| Campo | Valore |
| --- | --- |
| Thread fermato | `WICMKW001`, UID `0x40010003` |
| Stop reason | `0x10006`, mantenuto come codice grezzo |
| PC | `0xe0003258`, `SceLibKernel@1+0x138` |
| SP | `0x8a4ea7a0`, fuori dalla pila nativa |
| Modulo app RX caricato | `0x8100a000` |
| RX nell'ELF abbinato | `0x81000000` |
| Dimensione del buffer | `0x4ad3c4`, 4.903.876 byte |

I valori di ritorno nello stack riconducono a `ReadDiscExact`, `DVDReadPrio`,
`TaskThread_run_HLE` e `FiberProc`. La scansione conservata in
[analysis.txt](../crashdumps/psarc-1791307164/analysis.txt) e
[analysis.json](../crashdumps/psarc-1791307164/analysis.json) è una raccolta di
indirizzi candidati nello stack, non un unwinding completo.

Il ritorno caricato `0x827acfd7`, corretto per la rilocazione e lo stato Thumb,
corrisponde a `0x827a2fd6` nell'ELF. L'istruzione precedente, `0x827a2fd2`,
è `bl memset` nel costruttore del vector. La routine `memset` a `0x829516b0`
salta al veneer di `sceClibMemset`. La disassemblazione è conservata in
[vector-zero-disassembly.txt](../crashdumps/psarc-1791307164/vector-zero-disassembly.txt)
e [libc-disassembly.txt](../crashdumps/psarc-1791307164/libc-disassembly.txt).

Il buffer è già allocato: nel vector catturato l'inizio è `0x8a4eb1f8` e
il limite è `0x8a9985bc`, la cui differenza è esattamente `0x4ad3c4`.
Il crash si colloca nell'azzeramento del buffer, prima della lettura PSARC.
Gli indirizzi associati a `operator new` sono ritorni precedenti ancora presenti
nello stack; non identificano da soli la chiamata attiva.

Il worker PSARC aveva già isolato le operazioni I/O del container su una pila
nativa, ma il chiamante DVD continuava a creare/azzerare il vector e copiare
i dati sulla pila libco. La nuova correzione completa questo confine.
La conferma che elimini il nuovo crash richiede un'altra esecuzione sulla Vita.

## Correzione implementata

- `HostContext::CallOnNativeStack` trasporta callable, argomenti e risultato
  attraverso il dispatcher sincrono esistente, con propagazione delle eccezioni.
- `ReadDiscToGuest` esegue l'intero ciclo del buffer sulla pila nativa dello stesso
  thread: allocazione, azzeramento, lettura, copia in RAM guest e distruzione.
- Anche letture assolute, low DVD e raw ISO eseguono il ciclo completo sulla pila
  nativa. Le copie DMA del modulo usano lo stesso confine.
- La copia alla RAM guest avviene dopo il successo della lettura, con la stessa
  notifica DMA. Gli esiti, gli aggiornamenti del command block e le chiamate ai
  callback guest rimangono dopo il ritorno dell'operazione sincrona.
- ISO e PSARC continuano a usare i rispettivi backend; non vengono richiesti file
  estratti e l'archivio non viene riconvertito.

Marker della candidata: `build=psarc-native-dvd-20261006`. Le prime otto letture
per nome stampano `[dvd-native]` con indirizzo dello stack, byte e percorso.
Async GX, fast math, texture native sperimentali, geometria residente e profiling
guest restano OFF. Nessuna ulteriore modifica al renderer Aurora in questo intervento.

## Verifiche

- 5/5 contratti host superati. Il test dei contesti usa anche un buffer da
  4.903.876 byte, ne verifica tutti i byte dopo la copia, il trasporto del risultato,
  il ritorno alla coroutine e la distruzione del contesto locale sulla pila nativa
  sia in caso di successo sia in caso di eccezione.
- I test host non eseguono `sceClibMemset` del firmware né avviano il gioco.
- Build Vita completata; controllo sulle istruzioni ARM dell'ELF finale superato:
  `r4-r11`, `d8-d15`, SP e continuazione ARM/Thumb preservati.
- Audit ELF/map: 44.937 simboli eseguibili controllati; draw/present GXM presenti;
  nessun entry point o libreria GL/vitaGL/vita2d.
- SELF contenuto nel VPK identico a `game-eboot.bin`. Il manifest include gli hash
  di ISO e PSARC e il digest dei sorgenti effettivi.

Log: [contratti host](../build/host-native-dvd-validation.log),
[build e audit](../build/vita-native-dvd-build.log),
[contratto ARM](../build/vita-native-dvd-abi.log).

## Candidata e installazione

| Artefatto | SHA-256 |
| --- | --- |
| VPK | `87e0f391aba2ee00b77933fb1bc4cb3b1fd6476cee677d2e102c2c7cc2c815c4` |
| SELF | `9448f0a8f3bfe21cac05e915280ae77b52bb365d9239b569c92c6f8be7b3d774` |
| ELF | `b6666b9b0f730194fad7a0ef76ce07991118ddb58b1f21f6ca3086807350a6c7` |
| Digest sorgenti e input generati | `d3e89d5534f98d447f392669f02ec62e26ab6c49697345921619ab3d85af0b41` |

Copie indipendenti di ELF, map, VELF, VPK, SELF e manifest sono conservate in
`build/vita-psarc/install-native-dvd-20261006T173104Z`.
Pacchetto: [wiicompiled-vita-game.vpk](../build/vita-psarc/install-native-dvd-20261006T173104Z/wiicompiled-vita-game.vpk).
Installazione completata alle 17:32:12 UTC (19:32:12 CEST), senza avvio automatico.
È stato aggiornato il solo `eboot.bin` dell'app registrata `WICMKW001`.
Tutti i 14 file riletti dalla console coincidono con il pacchetto della candidata.
Il SELF installato ha SHA-256
`9448f0a8f3bfe21cac05e915280ae77b52bb365d9239b569c92c6f8be7b3d774`.

Il precedente SELF è conservato anche sulla console come
`ux0:/app/WICMKW001/eboot.bin.crashed-native-stack-20261006T173104Z`.
Config.toml è stato riletto e rimane identico alla cattura. Il PSARC non è stato
modificato o ritrasferito; dimensione remota 2.642.224.628 byte. Non è stato
calcolato l'hash dell'intero PSARC remoto.

Evidenza: [INSTALL.json](../build/vita-psarc/install-native-dvd-20261006T173104Z/INSTALL.json)
e [VALIDATION.json](../build/vita-psarc/install-native-dvd-20261006T173104Z/VALIDATION.json).
La candidata non ha ancora una prova di esecuzione hardware.

## Prossima verifica sul dispositivo

Dopo l'installazione verificata, avviare manualmente la nuova candidata.
Controllare il marker, che gli indirizzi `[dvd-native]` appartengano all'intervallo
nativo registrato all'avvio, e che la lettura del buffer grande completi.
Poi verificare menu e caricamento pista. Se compare un nuovo dump, abbinarlo al
SELF installato e all'ELF archiviato di questa candidata prima di modificare i sorgenti.
