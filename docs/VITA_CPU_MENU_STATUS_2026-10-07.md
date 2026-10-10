# WiiCompiled Vita: menu a 1 FPS, CPU e uscita dopo 1 player

Aggiornamento dopo la prova THP: l'esito dell'utente resta negativo. La nuova
attribuzione matematica è `PSMTXConcat`; la lista libera dell'heap contiene
un collegamento incoerente. È pronta la conversione dei video in H.264 con
lettore hardware sperimentale. Evidenza e lavoro residuo nel
[rapporto AVC aggiornato](VITA_HARDWARE_AVC_2026-10-07.md). Le sezioni seguenti
conservano la diagnosi delle build CPU precedenti.

Obiettivo del port: **30 FPS**, con un budget di **33,33 ms per frame**.
Il rallentamento segnalato è confermato dai log della console. L'uscita è un
arresto del gioco dopo un'asserzione durante la costruzione di un menu.
Il port non ha ancora raggiunto il target e l'asserzione non è ancora corretta.

## Prova FMA/layout: risultato negativo e causa immediata verificata

L'utente conferma menu a 1 FPS, Mii assente e ritorno alla LiveArea dopo
"1 player". La cattura `device-runs/cpu-fma-layout-20261007T133104Z` contiene
il SELF riletto `2e1aa8c341520c0e619b734a626a287ec660a4ca310114da1c94451968610269`,
identico alla candidata installata. Si analizza solo `runtime-session.log`:
il log completo contiene anche sessioni precedenti di altre build.

Le ultime quattro finestre misurano **0,938–0,946 frame con draw al secondo**,
circa 1.057–1.066 ms/frame. Il percorso FMA non ha dimostrato un beneficio
hardware; le stime matematiche sono circa il 39–40% del tempo di parete.
La diagnostica aggiunta impedisce di trattare il confronto come un A/B quiet.
I due helper hanno ancora zero item nelle finestre dei menu.

I riferimenti al codice, registrati dal programma e verificati contro due
simboli dell'ELF corrispondente, danno uno spostamento di `0x26000`. Tutti gli
otto siti paired dominanti delle finestre lente appartengono a
**`0x801B4DA8`, `__THPInverseDCTNoYPos`**, il decoder video THP. Questo distingue
il calcolo video da GX/GXM e rende il decoder un intervento mirato.

Il BRLYT salvato dalla RAM guest è ora confrontato integralmente con quello
originale nel PSARC: 3.092 byte e SHA-256
`0ff7833de156feb637328ac43896d8dbdf0ed668b2fd9161af5bc783688c963e`, identici.
Durante la sua costruzione falliscono **15/15 allocazioni**, 3.356 byte richiesti.
Allocator `0x80afd9d0`, callback `0x80199b58`, percorso
`MEMAllocFromExpHeapEx`. Il layout restituisce successo con root/groups nulli;
`MainLayout::Build` attraversa la lista e termina con l'asserzione
`LinkList.h:573` / `PPCHalt`. Non è presente un nuovo dump kernel.

È verificata la causa immediata dell'arresto, non ancora il motivo del
fallimento dell'heap. La candidata successiva legge, senza modificarli,
heap, alignment, limiti e lista dei blocchi liberi al fallimento. La
contabilità dei pane tramite wrapping non è completa: l'allocatore viene
intercettato, ma alcune chiamate pane restano interne alla stessa unità di
traduzione. Il conteggio `panes=0` non prova assenza di blocchi pane nel file.

Confronto delle opzioni Aurora e implementazione successiva:
[THP nativo e confronto Strikers](VITA_NATIVE_THP_AURORA_2026-10-07.md).

## Prova della candidata CPU: risultato negativo

L'utente ha provato `psarc-cpu-ranges-20261007`: **nessun miglioramento
percepito, menu ancora a 1 FPS e uscita dopo 1 player**. La cattura successiva
è `device-runs/cpu-ranges-20261007T093024Z`. Il SELF riletto è
`5e401a20b3f31e523aee6448c3102f135b73328ed7491699bd4deb2802851a13`,
identico a `build/vita-psarc/candidate-cpu-ranges-20261007T091558Z`.
Config invariata: `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
La prova comprende diagnostica e non è un A/B senza strumentazione.
L'ottimizzazione dei blocchi RAM resta sperimentale, senza beneficio hardware
dimostrato.

| Fine finestra | FPS con draw | ms/frame | FMA float32 | Tempo FMA stimato / parete | Item sui due helper |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1017 | 0,878 | 1.138,4 | 4.362.412 | 31,7% | 0 |
| 1024 | 0,648 | 1.543,8 | 3.504.746 | 24,2% | 0 |
| 1034 | 0,963 | 1.038,8 | 4.991.254 | 36,0% | 0 |
| 1044 | 0,973 | 1.028,2 | 4.964.004 | 36,2% | 0 |
| 1054 | 0,966 | 1.035,6 | 5.025.516 | 36,2% | 0 |

Nella finestra 1034 le stime FMA totalizzano 3.742.067 us su 10.387.935 us.
Sono tempi campionati dentro libm, comprensivi delle letture del timer: non
misurano tutti i costi dei wrapper e non spiegano l'intero frame. Le sei
posizioni ARM hanno un unico spostamento comune di `0x24000` rispetto alle
chiamate BL/BLX nell'ELF conservato. Questo è **dedotto dai siti di chiamata**,
non letto dai moduli. I due percorsi dominanti sono `PpcFmaddPairInline` e
`PpcFmsubPairInline`; il dato non identifica ancora la funzione PPC più costosa.

Nella finestra 1054, 6.645 chiamate al pool elaborano 26.580 item: **quattro
vertici per chiamata**, tutti sul chiamante. I due helper hanno zero item,
chunk e tempo di lavoro. Distribuire ogni quad introdurrebbe migliaia di
sincronizzazioni. Servono gruppi di calcolo indipendenti abbastanza grandi
da assorbire quel costo. La variante separata con assegnazione CPU0/1/2
è compilata e conservata, ma non è stata installata né misurata.

Prima dell'asserzione, `Layout::Build` restituisce 1 con `root=0`, `groups=0`,
header RLYT, BOM FEFF e versione 10. Non è comparso un nuovo dump kernel;
restano gli otto del 6 ottobre. La risorsa originale referenziata da
`BattleSettingRadioBase`, `common_w023_rule_menu.brlyt`, è presente nel PSARC
host: 3.092 byte, 32 blocchi, pane principale e gruppi, SHA-256
`0ff7833de156feb637328ac43896d8dbdf0ed668b2fd9161af5bc783688c963e`.
Questo prova la struttura sul computer, **non l'identità del corpo nella
RAM guest né il successo delle allocazioni**. L'analisi host non introduce
file estratti necessari al port: sulla Vita resta il solo PSARC.

## Candidata successiva: FMA e diagnosi della costruzione

`psarc-cpu-fma-layout-20261007` aggiunge un solo percorso matematico
sperimentale, `WIC_EXACT_FLOAT_FMA`, OFF per default. Il prodotto di due
significandi float32 richiede al massimo 48 bit ed è esatto in float64. La
somma in float64 può essere ristretta a float32 nei casi ordinari; se cade su
un punto medio float32, usa la funzione libm originale. Anche NaN, infiniti,
subnormali, zero risultante, limiti di underflow/overflow e modalità diverse
da nearest mantengono il fallback. Su ARM la modalità è letta da FPSCR.
`WIC_FAST_PPC_MATH` resta OFF: non viene attivata la VMLA approssimata float32.
Il percorso può essere compilato anche senza la diagnostica per un futuro A/B.

La diagnosi aggiunge conteggi dei pane e delle allocazioni SDK, con i primi
errori, dimensioni, allocator e callback. Al fallimento salva il BRLYT caricato,
con un limite di due file da massimo 2 MiB. Registra inoltre un riferimento al
codice caricato per simbolizzare i prossimi siti ARM senza dedurre lo
spostamento. ABI, asserzioni e valori di ritorno originali sono conservati;
i BRLYT salvati sono diagnostica e non una nuova dipendenza del gioco.

Sono passati **11/11 test core/runtime/input/CPU**, inclusi quattro milioni
di confronti float32 bit per bit con FMA, casi speciali e fallback sui punti
medi. Questa è validazione sul computer. La successiva prova sulla Vita
non mostra un miglioramento degli FPS né corregge l'uscita. La
configurazione dei worker rimane quella della prova precedente per isolare
il cambiamento matematico. Compilazione, audit ARM, pacchetto e SELF installato
sono registrati nel report di consegna.

La candidata campiona anche i chiamanti ARM dei due helper paired: una posizione
casuale ogni gruppo di 128 chiamate, così un ciclo periodico non seleziona sempre
lo stesso sito. `[cpu-pair-caller]` permette l'attribuzione alle funzioni PPC
con l'ELF corrispondente. Chiamate e tempi per sito sono stime; il tempo paired
include il tempo FMA e **non va sommato** a `[cpu-math-site]`. Questa strumentazione
è esclusa nella compilazione senza `WIC_CPU_DIAGNOSTICS`.

## Evidenza legata alla build installata

Cattura: `device-runs/mii-cpu-20261007T080526Z`. I log completi, la configurazione,
il SELF installato e le immagini sono conservati. `CAPTURE.json` registra gli
hash; `ANALYSIS.json` contiene le finestre misurate e `MATCHING_ARTIFACTS.json`
indica ELF, map e VELF corrispondenti.

- Marcatore: `psarc-mii-diagnostics-20261007`.
- SELF installato, riletto integralmente:
  `5c37a4b082071b81231d7fea4d8c690540b5f4ebc83ba196f770875a01e1eb68`.
- Configurazione:
  `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
- SFO: `ATTRIBUTE2=12`, `ATTRIBUTE=32768`.
- CPU richiesta dal programma: 444 MHz; GPU: 222 MHz.
- Contenitore sulla Vita: PSARC. La ISO resta assente.

| Finestra che termina al frame | Frame completati | Tempo trascorso | Presentazioni/s | Richieste native | Fuori dalle richieste native |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1069 | 13 | 10,401 s | 1,250 | 0,953 s | 90,84% |
| 1079 | 10 | 10,092 s | **0,991** | 0,723 s | **92,84%** |
| 1094 | 15 | 10,435 s | 1,438 | 1,068 s | 89,77% |
| 1109 | 15 | 10,461 s | 1,434 | 1,056 s | 89,91% |

Nella finestra peggiore, 10 frame richiedono 10.091.583 us: circa **1.009 ms
per frame**, contro il budget di 33,33 ms. Sono contati 6.740 richieste native
ma soltanto 22 effettivi passaggi sulla pila originale. Molte richieste sono
eseguite direttamente perché il chiamante si trova già sulla pila nativa.
Le attese VI/GX/GKI/panic misurate in questa finestra sono zero.

Dividendo per i dieci frame, le richieste native occupano circa **72,3 ms/frame**,
di cui **42,8 ms/frame** sono FIFO. Anche questa parte supera già il budget
di 33,33 ms: dopo aver ridotto il costo dominante esterno alle richieste,
servirà ancora ridurre il costo continuo delle chiamate e del frontend.
Il piccolo peso percentuale del renderer nel campione a 1 FPS non implica
che il renderer sia già abbastanza veloce per il target.

Queste misure sono diagnostiche, con logging attivo. Il tempo FIFO (428.347 us)
è incluso nei callback nativi (616.549 us), a loro volta inclusi nelle richieste
(722.941 us). **Non si sommano.** Il restante 92,84% comprende codice ricompilato,
altri percorsi non misurati e scheduling: non è una misura esclusiva dei cicli
CPU del gioco. Il core al 100% è un'osservazione dell'utente, non una nuova misura
di utilizzo totale per core fatta da questa cattura.

Nella fase iniziale si vedono circa 60 presentazioni/s, spesso con soli 6–7
frame/s contenenti draw. Quei contatori non dimostrano che un menu completo
fosse renderizzato a 60 FPS sulla build corrente. La riduzione a 30 FPS del
target non elimina il lavoro che oggi richiede circa un secondo per frame.

## Uscita dopo la scelta di un player

Il log termina con `LinkList.h:573 Panic`, seguito da `PPCHalt`. La backchain
mostra `MainLayout::Build` → `ControlLoader::Load` → `RadioButtonControl::Load`
→ `Pages::BattleSettings::CreateControl` → inizializzazione della sezione.
Il ritorno dall'asserzione sarebbe a `0x805E8990`.

Il codice di `MainLayout::Build` trova un link nullo durante l'iterazione della
lista dei figli del pane. Il messaggio nella rodata originale, a `0x808B9554`,
è `NW4R:Pointer must not be NULL (p)`. Nei riferimenti conservati sulla pila
compare `BattleSettingRadioBase`. Questi dati identificano la condizione di
arresto; non distinguono ancora un layout rifiutato da un errore di costruzione,
allocazione, lettura o ricompilazione.

Non è stato trovato un nuovo coredump kernel del 7 ottobre: la lista remota
contiene soltanto gli otto dump precedenti del 6 ottobre. `crash.txt` appartiene
all'arresto guest. Il `pc=0` del contesto è obsoleto e non va interpretato come
un salto ARM all'indirizzo zero.

La diagnostica nuova intercetta la funzione SDK `Layout::Build` (`8007A170`)
tramite linker wrapping. Registra header RLYT, byte order/versione, dimensioni,
risorsa/accessor, valore di ritorno, root pane e link dei figli. Conserva il
tipo di funzione, l'ABI e le tabelle generate. Le prime 32 costruzioni e fino
a 16 ulteriori anomalie sono registrate; la condizione di errore resta attiva.

## Copie dei Mii

Il manager RFL risulta leggibile, `working=0`, `flags=0x01`, `reason=0`.
Le otto copie fallite hanno sorgente logica `y=456`, altezza 32, e sorgente
fisica `y=544`, **altezza zero**. Il mapping usa la regione visibile 608×456
e il target 960×544: la zona temporanea EFB sotto l'area visibile viene tagliata.

Questo è un difetto concreto delle copie, coerente con i Mii mancanti. La
correzione richiede una superficie EFB che conservi anche la zona temporanea
fuori dal display, mantenendo viewport, scissor, copie e presentazione coerenti.
La candidata CPU non implementa ancora questa superficie. Aumentare soltanto
l'altezza logica globale cambierebbe la scala dei menu senza conservare il
contenuto richiesto. La cattura della sessione mostra [Select Licence](../device-runs/mii-cpu-20261007T080526Z/diagnostics/frame-979.png).

## Sviluppo CPU implementato

### Riduzione delle chiamate alla memoria

`WIC_FAST_RAM_RANGES`, OFF per default, introduce un resolver verificato per
blocchi di RAM in scrittura sulla Vita. In precedenza `ResolveRangeHost` restituiva
sempre null perché ARM32 non dispone della vista virtuale da 4 GiB. I salvataggi
dei registri finivano quindi nei fallback separati per ogni parola.

Il nuovo percorso controlla mapping autorevoli, limiti, continuità delle pagine,
copie EFB differite e protezioni delle pagine eseguibili. Mantiene MMIO/FIFO sul
percorso con policy. Se tutto il blocco è valido, usa direttamente i codec bulk
esistenti; alias, ordine dei byte e accessi non allineati sono preservati.
Il controllo del blocco è condiviso per limitare la crescita del codice ARM.
La durata dei puntatori resta limitata dalle barriere del traduttore. Le letture
mantengono il percorso precedente: una scrittura FIFO intermedia può creare
una copia EFB differita mentre il puntatore del blocco resta in vita. Il primo
intervento non memorizza puntatori in lettura che aggirerebbero questa policy.

I test coprono alias MEM1/MEM2, overlay Owned, mapping parziali e non contigui,
wrap a 32 bit, guardie eseguibili interne al blocco, transizioni delle policy,
copie differite ai due estremi e codec bulk non allineati. La semantica PPC
delle operazioni matematiche resta quella del controllo.

### Attribuzione delle operazioni matematiche

`WIC_CPU_DIAGNOSTICS`, OFF per default, conserva `fma`/`fmaf` originali e conta
le chiamate del thread OS del gioco, incluse le sue fiber. Campiona un'operazione
ogni 128 chiamate per sito, senza allocazioni per chiamata. Registra gli otto
siti con maggiore tempo stimato e il loro indirizzo di ritorno ARM, da simbolizzare
con l'ELF esatto. Le stime includono la perturbazione delle letture del timer;
i siti senza campioni non hanno una misura di durata. I contatori dei worker
sono esclusi e le operazioni libm annidate non vengono contate due volte.

I test verificano identità bit per bit con libm per float/double, inclusi NaN,
infinito, zero con segno e valori molto piccoli/grandi, campionamento limitato,
esclusione degli altri thread e conservazione del contesto nel wrapper Layout.

### Multithreading

La build installata ha già **due helper Aurora**, verificati nel log:
CPU2/priorità `0x40` e CPU1/priorità `0x41`, oltre al chiamante. Ha anche thread
nativi per audio e I/O PSARC. La cattura successiva misura quattro vertici
per chiamata nei menu lenti, tutti elaborati sul chiamante.

La nuova diagnostica registra per finestra chiamate al pool, tempo di attesa
del chiamante e item/chunk/tempo per lane. `WIC_DISTINCT_CPU_CORES`, OFF per
default, prepara una variante separata con il thread del gioco su CPU0 e
dispatch che evita helper sul core del chiamante. La candidata principale
mantiene questa opzione OFF per isolare l'ottimizzazione dei blocchi RAM.

Il core PPC e la FIFO restano ordinati. Per estendere il lavoro parallelo
servono funzioni calde misurate, input immutabili, output privati e commit
ordinati: eseguire contemporaneamente i thread Wii sul contesto globale
attuale introdurrebbe corse sui registri, sulla RAM e sui callback.

## Validazione e prossime verifiche

- **10/10** test per la prima candidata CPU; **11/11** per la candidata FMA/layout.
- **18/18** contratti Aurora sul computer, inclusi worker e dispatch.
- VPK, ELF, map, VELF e identità: vedere il manifest della candidata e il
  successivo report di consegna. La compilazione non prova prestazioni sulla Vita.
- La prova della candidata FMA/layout deve ripetere licenza → 1 player. Il log
  deve contenere `[cpu-code]`, `[cpu-math-site]`, `[cpu-pair-caller]` e, al
  fallimento, `[layout-result]` e i file `layout-failure-*.brlyt`.
- Priorità: correggere la causa della lista nulla; verificare la riduzione
  delle chiamate RAM; individuare i siti matematici costosi; misurare l'utilizzo
  degli helper; spostare soltanto il lavoro preparatorio indipendente; correggere
  la superficie EFB temporanea dei Mii.
- Dopo la stabilizzazione: prove A/B senza diagnostica, stessa scena e cache
  calda, mediane/p95/p99 e percentuale di frame entro 33,33 ms. Conservare una
  variante di controllo e cambiare una sola ottimizzazione per confronto.

Nessun test automatico ha avviato il gioco sulla console. PSARC e configurazione
non sono stati alterati dalla raccolta della diagnosi.
