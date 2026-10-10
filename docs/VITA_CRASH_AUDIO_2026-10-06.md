# WiiCompiled Vita: quinto crash, audio e difetti del runtime

Aggiornamento successivo all'analisi: la candidata
`psarc-native-context-20261006` integra pthread, backend SceFiber e controlli
iniziali sul firmware. È compilata e installata, senza avvio automatico.
Identità, implementazione e limiti nel
[report di consegna](VITA_CONTEXT_FIX_DELIVERY_2026-10-06.md).
Le sezioni sotto conservano le evidenze e lo stato al termine della diagnosi.

Il test della candidata `psarc-native-wait-20261006` supera il precedente
arresto nell'attesa VI e raggiunge l'inizializzazione audio. Il processo
continua però a terminare prima di un menu verificato. L'analisi identifica
un difetto certo del collegamento C++ dei thread e conferma che il contesto
guest continua a usare una pila esterna a quella registrata per il thread Vita.
Correggere singole chiamate native non ha stabilizzato il runtime nel suo insieme.

## Cattura e identità

Console `192.168.1.79:1337`, applicazione `WICMKW001`. Dump recuperato:
`ux0:/data/psp2core-1791316902-0x0005032711-eboot.bin.psp2dmp`.

Il SELF riletto dalla console è identico alla candidata archiviata in
`build/vita-psarc/install-native-wait-20261006T195636Z`. Config.toml coincide
con quello della consegna. Prima dell'analisi sono stati conservati dump,
log, configurazione, SELF installato, ELF, map, VELF, VPK e manifest abbinati.

| Artefatto | SHA-256 |
| --- | --- |
| Dump originale, 146.192 byte | `db95fa77dd9c0b1058267b0d7d216e8f039e6c34c8fe3e8e77f5de37d72cb9f4` |
| SELF installato, 22.941.335 byte | `98861d6a8649fca0e3f2d4f25c2986e8c7df0458113326f591a3ff16ef4307b3` |
| ELF con simboli | `15b68a76d8a1b885a74e5681966e0bd6f7442d59457c38caaaa03769d85aed16` |
| Map | `93d020cc7e0f939351713279f15c326b310ec110013538770c6cf55dab7b5581` |
| VELF | `7dc86d936962ad26b26230f46e47fa6cedfcb5f9c1a8b71c24204b5a09e1b222` |
| VPK | `aab92d3b8e508fcfa6205ccbc90f460ae88618e7907650c22536cf3a16b8eae1` |
| Config.toml | `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65` |

La [cattura](../crashdumps/psarc-1791316902/CAPTURE.json) registra i file e gli
hash. Il [log della sola sessione](../crashdumps/psarc-1791316902/runtime-session.log)
parte dal marker della candidata; il log completo contiene anche avvii precedenti.
Gli artefatti binari restano esclusi da Git. Una richiesta FTP opzionale della
dimensione PSARC è scaduta dopo il recupero di tutti i file necessari: questa
cattura non costituisce una nuova verifica remota della dimensione dell'archivio.

## Progresso osservato sulla console

| Cattura | Punto raggiunto / arresto |
| --- | --- |
| `1791303648` | Presentazione GXM; arresto durante la chiamata di display sulla pila libco |
| `1791307164` | Lettura DVD del modulo; arresto nell'azzeramento del buffer |
| `1791313305` | Prima stampa dell'intestazione REL; arresto nel seek di append di OSReport |
| `1791315331` | Caricamento REL completato; arresto nell'attesa host VI |
| `1791316902`, attuale | Attese VI native, REL completato, inizializzazione audio e letture BRSAR; arresto nel mixer AX |

L'ultima sessione contiene quattro marker `[wait-native] site=vi` con pila
`0x83700020`, interna all'intervallo nativo. Il messaggio
`[Rel] Load Module[1]: size=4903876Bytes time=768msec` è presente.
Seguono DWC, `__AXOutInitDSP`, NW4R SND e tre letture DVD di
`/sound/revo_kart.brsar`, rispettivamente di 64, 645.856 e 453.344 byte.

L'ultima presentazione numerata nel log resta `512`, come nella precedente
cattura. Il logging delle presentazioni è campionato: questo numero non è
il frame esatto del crash, un conteggio finale o una misura degli FPS.
Le nuove tappe audio dimostrano l'avanzamento del percorso di avvio.
Non c'è ancora prova di menu, gara, immagine corretta o stabilità prolungata.

Il campione PSARC precedente alle letture audio riporta cinque letture,
12.969.569 byte e zero fallimenti. È un campione intermedio, non il totale
dell'esecuzione. La configurazione continua a selezionare il PSARC; il test
non richiede di ripristinare la ISO sulla console.

## Punto di arresto e pile

Il thread fermato è `WICMKW001`, UID `0x40010003`, motivo grezzo `0x10006`.
Il [parser vita-crashdump](https://raw.githubusercontent.com/TibixDev/vita-crashdump/main/src/lib.rs)
interpreta questo codice come **Stack overflow** e distingue i codici di
illegal context e data abort. È la classificazione del parser open source,
non una diagnosi autonoma del meccanismo interno del kernel Sony.

| Dato | Valore |
| --- | --- |
| PC runtime | `0x827d1094` |
| PC ELF dopo la rilocazione RX di `0x4d000` | `0x82784094` |
| Funzione simbolizzata | `AXWii::ProcessPBList`, copia PB inlined |
| Istruzione | `ldrh.w ip, [r3], #2` |
| SP | `0x8a9ebc60` |
| Pila nativa del thread | `[0x836c1000, 0x83701000)`, 256 KiB |
| IFSR / IFAR / DFSR / DFAR | Tutti zero |

SP è fuori dalla pila nativa. Nel punto campionato la sorgente della copia
è `0x8a9ebdc0`, dentro il PB locale di 310 byte. Il segmento attivo ha offset
di struttura 116, lunghezza 194 e avanzamento 124: il prossimo halfword
rispetta questi limiti. La destinazione calcolata è `0x84459aa6`.
Entrambi gli indirizzi sono presenti nei segmenti di memoria del dump.
Non emerge una copia fuori limite nel punto campionato; questo controllo
non certifica la validità semantica di tutte le voci audio o tutti i permessi.

LR è `0x84459a2b`, usato dal compilatore come destinazione della copia più uno.
Non va interpretato come indirizzo di ritorno corrotto. Sono stati invece
verificati due indirizzi salvati contro le chiamate nel binario:
`0x82784432` dopo la chiamata a ProcessPBList e `0x82784e42` dopo quella a
ExecuteCommandList. Il percorso ricostruito è SendMailToDSP,
ExecuteCommandList, ProcessPBList, copia dei PB. La scansione della pila
non è un unwind completo; riferimenti residui alle eccezioni std::thread
non dimostrano che fosse in corso un unwind al momento del crash.

Le quattro catture `1791307164`, `1791313305`, `1791315331` e `1791316902`
condividono motivo `0x10006`, SP esterno alla pila nativa e fault register
zero. Cambiano le funzioni in cui il processo viene fermato.
La pila nativa ha un picco riportato di 15.964 byte in questa cattura;
il consumo corrente zero del record non misura il consumo della pila libco.

Questi dati sostengono un problema generale di gestione dei contesti Vita.
Non dimostrano il punto preciso in cui il kernel controlla la pila, né
provano che sostituire libco con una specifica API risolva già il problema.

Evidenze: [analisi](../crashdumps/psarc-1791316902/analysis.txt),
[disassemblazione audio](../crashdumps/psarc-1791316902/audio-crash-disassembly.txt),
[limiti della copia e ritorni verificati](../crashdumps/psarc-1791316902/audio-copy-evidence.json),
[confronto delle pile](../crashdumps/psarc-1791316902/stack-comparison.json).

## Difetto certo: supporto C++ ai thread disattivato nel binario

Il log registra **50 tentativi e 50 fallimenti** del worker di mixaggio AX.
`wic_audio` è presente nel dump, ma è il distinto thread di uscita audio.
Il worker del mixer non è stato creato. EnsureMixWorker cattura
std::system_error, ripiega sul mixaggio inline e riprova alla richiesta
successiva senza registrare il codice dell'errore o memorizzare il fallimento.

Il binario abbinato mostra una causa deterministica:

- `pthread_cancel` è un simbolo **weak, undefined, indirizzo zero**.
- `pthread_create` è invece presente e definito.
- `__gthread_active_p` carica zero e restituisce sempre zero.
- `std::thread::_M_start_thread` carica lo stesso zero e prende il ramo
  di std::system_error prima di raggiungere pthread_create.
- Il ramo costruisce un errore di categoria generica con valore 1.

Il [supporto POSIX di GCC](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/libgcc/gthr-posix.h)
usa, su questa configurazione, il riferimento weak a pthread_cancel per
stabilire se i thread siano disponibili. La sola presenza di pthread_create
non basta. Il link del gioco include già `-lpthread`, ma il riferimento
debole non estrae l'oggetto pthread_cancel.o dall'archivio statico.
La libreria installata contiene la vera implementazione: non manca dall'SDK.

Lo stesso difetto riguarda **thread_local**. Le istruzioni di
`__emutls_get_address` prendono il ramo che restituisce un singolo puntatore
globale associato all'oggetto. Non raggiungono il lookup per thread.
Questo comportamento coincide con il
[sorgente emutls di GCC](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/libgcc/emutls.c).
HostContext usa thread_local per scheduler nativo, contesto corrente e
richiesta nativa; libco lo usa per il proprio contesto attivo. Questi dati
devono essere isolati tra i thread della console. Il binario attuale non
fornisce tale isolamento. Non è stato dimostrato quale specifico scambio
tra thread abbia contribuito a questo arresto.

La stessa condizione può disattivare percorsi di sincronizzazione che
consultano il gate GCC. La sua correzione attiverà operazioni pthread
oggi saltate: occorre verificarne il contesto di esecuzione, oltre alla TLS.

## Verifica della correzione del link, separata dal gioco

Con GCC Vita 15.2.0 e la stessa libreria pthread sono stati compilati due
piccoli ELF ARM, contenenti std::thread e thread_local:

| Probe | Link | pthread_cancel | Risultato delle istruzioni di __gthread_active_p |
| --- | --- | --- | --- |
| Baseline | `-lpthread` | Weak undefined | `0` |
| Correzione | `-lpthread -Wl,-u,pthread_cancel` | Vera implementazione definita | `1` |

Entrambi compilano senza errori. È stato eseguito sul computer il codice ARM
effettivo del gate dei due ELF. Sul binario del gioco sono stati verificati
anche il salto immediato al fallimento del costruttore di thread e il ramo
TLS con puntatore globale già allocato. Quest'ultimo restituisce lo stesso
puntatore per due SP differenti senza consultare dati pthread per thread.

Questa prova conferma la soluzione al difetto di collegamento. Non crea
thread sul firmware Vita e non prova il funzionamento della TLS dopo la
correzione, la correttezza del mixer o la stabilità del gioco. I probe non
sono stati pacchettizzati, installati o avviati; non è stata cambiata la
build di produzione durante questa analisi.

Evidenze: [threading-evidence.json](../crashdumps/psarc-1791316902/threading-evidence.json),
[verifica delle istruzioni](../crashdumps/psarc-1791316902/check-threading-evidence.py),
[comandi e risultati di compilazione](../crashdumps/psarc-1791316902/pthread-link-probe-build.json),
[TLS disassemblata](../crashdumps/psarc-1791316902/tls-disassembly.txt).

## Perché si susseguono gli errori e cosa cambia nello sviluppo

Le correzioni precedenti hanno superato i rispettivi punti di arresto,
ma hanno coperto progressivamente GXM, DVD, logging e attese. I test host
e ARM verificavano i loro contratti e i registri conservati; non verificavano
la gestione delle pile del firmware o il gate pthread del SELF completo.
Avanzare nel bootstrap esponeva un altro percorso del runtime ancora
incompatibile con Vita. Il difetto del link dimostra inoltre un problema
di base che attraversa scheduler, worker e sincronizzazione.

La prossima sequenza di lavoro deve stabilizzare questi prerequisiti:

1. **Link e TLS.** Includere la vera pthread_cancel nel target Vita e aggiungere
   un controllo del binario finale che rifiuti il gate disattivato. Preparare
   un diagnostico sulla console che verifichi std::thread e isolamento dei
   thread_local, includendo anche thread creati direttamente con le API SceKernel
   come quelli audio/PSARC. La prova deve partire dalla pila nativa e verificare
   che i dispatcher non usino lo scheduler di un altro thread.
2. **Backend dei contesti.** Provare un backend Vita che gestisca correttamente
   le pile dei contesti guest. Le
   [API SceFiber esposte da VitaSDK](https://raw.githubusercontent.com/vitasdk/vita-headers/master/include/psp2/fiber.h)
   offrono inizializzazione, Run, Switch e ReturnToThread e sono una candidata
   da verificare con un diagnostico isolato. Conservare schedulazione guest,
   ritorno al chiamante, allineamento e registri ARM hard-float. Non presumere
   che aumentare la memoria delle pile libco corregga la loro registrazione.
3. **Worker audio.** Avviare il worker dalla pila nativa dopo le verifiche dei
   punti precedenti. Registrare codice e messaggio degli eventuali fallimenti,
   evitare tentativi illimitati e definire un fallback controllato. Le callback
   PPC restano nel percorso guest: non spostare indiscriminatamente l'intero
   SendMailToDSP su un thread nativo.
4. **Nuova build del gioco.** Procedere con una candidata identificabile e
   gate del binario obbligatori. Verificare sulla console creazione del mixer,
   avvio con solo PSARC, menu, gara e durata della sessione. Prestazioni e
   ulteriori ottimizzazioni vengono dopo questi risultati.

Questa analisi ha conservato le prove, compilato i soli probe e aggiornato
la documentazione. Non ha modificato runtime, configurazione della console,
installazione del gioco, PSARC o sorgenti Aurora. Il gioco rimane nello stato
del SELF abbinato al quinto dump.
