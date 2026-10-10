# WiiCompiled Vita: arresto prima di main, collegamento pthread incompleto

Il dump **1791319267** identifica una regressione della candidata
`psarc-native-context-20261006`: il processo si arresta durante i costruttori
statici C++, prima di `main`, in `pthread_mutex_lock` con un puntatore nullo.
La causa è il collegamento parziale di libpthread. La correzione precedente
forzava pthread_cancel e riattivava il gate dei thread GCC, ma lasciava
pthread_once e quattro funzioni di condition variable weak undefined.
L'audit precedente controllava il gate senza verificare queste primitive.

Non è una nuova dimostrazione del difetto di pila guest, né un risultato
dei controlli SceFiber. Questa sessione non ha raggiunto il bootstrap del
renderer, il PSARC, il mixer o il codice PPC. Menu e gara restano da verificare.

## Identità della cattura

Cattura indipendente: `crashdumps/psarc-1791319267`.
Dump remoto: `ux0:/data/psp2core-1791319267-0x0007192559-eboot.bin.psp2dmp`.
Il SELF riletto dalla Vita coincide con quello nell'archivio della consegna
`build/vita-psarc/install-native-context-20261006T203514Z` e con quello nel VPK.

| Artefatto della build arrestata | Byte | SHA-256 |
| --- | ---: | --- |
| Dump | 70.544 | `7d7236b55edf02af9f4148c2bfcc0a2691bc4cbc743bdfaebb9ece22883fe4f6` |
| SELF installato | 22.947.578 | `14c43f7a71cccd188bb5a43735fa35acd45eb2ed2f9028f0ecad4845e6250453` |
| ELF abbinato | 69.634.480 | `b38ba017df1bd0607e4f7b9d46d8dea741fe39c8e6709ce68bd8f377c3e71617` |
| Map | 43.762.414 | `9e94b9e280df4aa6bcf606d3a4e0933b14bcd433a8d04facbb723ae25a057fb1` |
| VELF | 85.703.378 | `d8383eace9eff55092a1e64d007e5bbda8f04a71a702714fbd4f283947174e54` |

Config.toml coincide con quello della consegna. Il runtime.log scaricato è
identico byte per byte a quello della cattura precedente, SHA-256
`c58509db71988c501c8e89acb70bc1656e613f2dbca33e6850898ef4494250ff`.
Non contiene il marker `build=psarc-native-context-20261006` e non è evidenza
di questa esecuzione. `runtime-session.log` è intenzionalmente vuoto.

Manifest della cattura: [CAPTURE.json](../crashdumps/psarc-1791319267/CAPTURE.json).
Analisi: [analysis.txt](../crashdumps/psarc-1791319267/analysis.txt).

## Fault e percorso di chiamata

Il thread WICMKW001 ha stop reason `0x30004`, data abort. Il segmento RX
è caricato a `0x81030000`, contro `0x81000000` dell'ELF: delta `0x30000`.

| Dato | Valore |
| --- | --- |
| PC sul dispositivo | `0x8291cf76` |
| PC nell'ELF | `0x828ecf76`, pthread_mutex_lock + 2 |
| Istruzione | `ldr r4, [r0, #0]` |
| r0 / DFAR | `0x00000000` / `0x00000000` |
| DFSR | `0x000000c7` |
| SP | `0x83700f00` |
| Intervallo della pila nativa | `[0x836c1000, 0x83701000)`, 256 KiB |
| LR convertito nell'ELF | `0x829039b0`, ritorno al guard C++ |

SP è interno alla pila nativa. Il dump contiene soltanto il main arrestato
e SceCommonDialogWorker. I worker del gioco non risultano ancora creati.

Gli indirizzi salvati sulla pila e le relative istruzioni di chiamata
ricostruiscono questo percorso coerente:

```text
_start
  __libc_init_array
    _GLOBAL__sub_I_indirect_dispatch_a0a39a429c344b0a.cpp
      RegisterStaticIndirectDispatchTable
        RegistryMutex
          __cxa_guard_acquire
            pthread_mutex_lock(NULL)
```

Non è un unwind completo: l'analizzatore conserva candidati di indirizzi
salvati; le disassemblazioni verificano i siti di chiamata e il LR attivo.
RegistryMutex richiede un guard C++ per costruire il suo std::mutex locale
statico. L'arresto è nel mutex interno del guard, prima della costruzione
del mutex del registro.

## Causa verificata nel binario

Nell'ELF arrestato sono weak undefined e valgono zero:

- pthread_once;
- pthread_cond_wait;
- pthread_cond_destroy;
- pthread_cond_broadcast;
- pthread_cond_signal.

Le implementazioni reali sono presenti in VitaSDK/libpthread.a, ma le
referenze weak di libstdc++ non ne estraggono gli oggetti dall'archivio.
Forzare la sola pthread_cancel abilita il percorso concorrente senza
completarne il collegamento.

Il guard prepara il callback di inizializzazione e l'oggetto once, ma a
`0x8290399c` la chiamata mancante a pthread_once è diventata `nop.w`.
Poi carica static_mutex e chiama pthread_mutex_lock. Il callback init(),
che assegna il buffer del mutex al puntatore static_mutex, non è eseguito.
La [implementazione dei guard C++ di GCC 15.2](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/libstdc%2B%2B-v3/libsupc%2B%2B/guard.cc)
conferma che get_static_mutex deve eseguire init attraverso __gthread_once.
Il compilatore VitaSDK locale è GCC 15.2.0.

Prove conservate:

- [guard arrestato](../crashdumps/psarc-1791319267/pthread-guard.disassembly.txt);
- [istruzione del fault](../crashdumps/psarc-1791319267/mutex-fault.disassembly.txt);
- [RegistryMutex](../crashdumps/psarc-1791319267/registry-static-constructor.disassembly.txt);
- [simboli pthread dell'ELF](../crashdumps/psarc-1791319267/pthread-symbols.json);
- [implementazioni disponibili nello SDK](../crashdumps/psarc-1791319267/sdk-pthread-definitions.txt).

La posizione globale static_mutex non rientra nei segmenti di memoria
effettivamente catturati dal dump: non è stata letta direttamente dal core.
Il puntatore nullo è stabilito da r0/DFAR e dal percorso delle istruzioni.

## Riproduzione e controllo della correzione

Una riproduzione ARM host carica i segmenti dell'ELF esatto, parte da
__cxa_guard_acquire con un guard ancora da inizializzare ed esegue le
istruzioni reali. Dopo **22 istruzioni** produce la stessa lettura nulla
in pthread_mutex_lock + 2. Non è simulazione del firmware e non esegue
syscall Vita. Traccia: [guard-arm-reproduction.json](../crashdumps/psarc-1791319267/guard-arm-reproduction.json).

Un relink controllato degli stessi oggetti del gioco, cambiando soltanto
l'inclusione di libpthread, risolve 27 primitive pthread e nessuna referenza
pthread resta undefined. Il guard ora chiama le implementazioni reali di
pthread_once, pthread_mutex_lock e pthread_cond_wait. Non sono stati
disabilitati i thread o i guard C++ per aggirare il difetto.

Il [nuovo audit](../scripts/test_vita_runtime_linkage.py) rifiuta qualsiasi
primitiva pthread referenziata ma irrisolta e controlla le chiamate effettive
del guard, oltre ai gate, SceFiber e assenza di libco già verificati.
Il controllo negativo rifiuta proprio l'ELF abbinato a questo dump, elencando
tutte e cinque le primitive mancanti. Il relink corretto passa.

Prove: [rifiuto della candidata arrestata](../crashdumps/psarc-1791319267/crashed-candidate-audit.log),
[audit del relink corretto](../crashdumps/psarc-1791319267/complete-pthread-proof-audit.json),
[guard corretto](../crashdumps/psarc-1791319267/corrected-pthread-guard.disassembly.txt).

## Prossima candidata e verifica sul dispositivo

La correzione in [game.cmake](../vita/game.cmake) include l'archivio pthread
completo con whole-archive limitato a quella sola libreria. La garbage
collection delle sezioni resta attiva. L'unico altro cambiamento nel gioco
è il marker `psarc-pthread-complete-20261006` per distinguere la nuova build.
Il backend SceFiber, le verifiche iniziali, Aurora, PSARC e configurazione
mantengono l'implementazione della candidata arrestata.

La nuova candidata **`psarc-pthread-complete-20261006`** è compilata e pronta
localmente, nell'archivio indipendente
`build/vita-psarc/candidate-pthread-complete-20261006T205318Z`.
L'audit sul suo ELF finale conferma 27 primitive pthread definite, nessuna
irrisolta e le chiamate reali del guard. Passano anche i collegamenti del
dispatcher nativo stdio/attese e l'audit GXM: 45.006 simboli eseguibili,
draw/present nativi, nessun GL/vitaGL/vita2d. Solo eboot.bin differisce tra
i 14 file del VPK nuovo e quelli del VPK arrestato.

| Artefatto della candidata pronta | Byte | SHA-256 |
| --- | ---: | --- |
| VPK | 22.751.868 | `7791d311f43c397c5b345786868aa45553fdc42868305867f2e08d7ade7ed0c1` |
| SELF | 22.952.534 | `eca6ac41e1b490842d6c2ec698a1cdc1a575cfec00047e6a341ca0da1d9fe679` |
| ELF | 69.641.568 | `8f4f9f7480c6e1f0d2d18ba5d78488c76beb653f4b0a1f6aff1f344e70a91ea8` |
| Map | 43.846.306 | `4ce3308bdf294ae453db0e33e354632ee6bbc9c4b4debb310b943013794e4b2d` |
| VELF | 85.713.154 | `79f50ad623b480703e15ea0095753f16931b450928744c6bf585de8e264cef2e` |

Il digest dei 31.018 file sorgente/input è
`1d3dac13d5d2d4ec78d85d451024cdfb110ca6ccecd654a8ec95e6d73695f5e5`.
Manifest e verifiche:
[DELIVERY.json](../build/vita-psarc/candidate-pthread-complete-20261006T205318Z/DELIVERY.json),
[VALIDATION.json](../build/vita-psarc/candidate-pthread-complete-20261006T205318Z/VALIDATION.json),
[RUNTIME_LINKAGE.json](../build/vita-psarc/candidate-pthread-complete-20261006T205318Z/RUNTIME_LINKAGE.json).

**Aggiornamento della consegna: la candidata è stata installata e verificata
sulla Vita, senza avvio automatico.** Tutti i 14 file dell'app coincidono
con il VPK e il SELF riletto ha SHA-256 `eca6ac41e1b4…`.
`build/latest-install-path.txt` identifica ora
`build/vita-psarc/install-pthread-complete-20261006T205755Z`;
`build/latest-build-path.txt` conserva la candidata locale abbinata.
Dettagli e manifest: [consegna pthread completa](VITA_PTHREAD_FIX_DELIVERY_2026-10-06.md).
PSARC e configurazione della Vita non sono stati modificati.
Renderer GXM e opzioni sperimentali OFF; esecuzione sul firmware ancora da verificare.

La prova hardware successiva deve prima raggiungere il nuovo marker boot
e i controlli std::thread, TLS, thread SceKernel e SceFiber/d8-d15. Solo dopo
il loro successo si potranno valutare l'avvio guest, il mixer, menu e gara.
Un nuovo dump va abbinato al SELF realmente riletto dalla console.
