Agisci come un **senior performance engineer C/C++ low-level specializzato in PS Vita, ARMv7/Cortex-A9, static recompilation PowerPC→ARM32, Wii/GameCube runtime, GX/vitaGL, multithreading e profiling CPU**.

Devi ANALIZZARE lo stato attuale del porting di **Mario Kart Wii PAL RMCP01 → PS Vita** e produrre un **piano tecnico concreto per la prossima fase di ottimizzazione performance, a partire da P6.66**.

Per ora NON implementare modifiche: voglio prima un piano estremamente concreto, ordinato per ROI, rischio e dipendenze.

# REPOSITORY

Repository reale:

`/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`

Target:

**Mario Kart Wii PAL RMCP01 staticamente ricompilato PowerPC → ARM32 PS Vita**

Hardware target:

**PS Vita reale**

La Vita reale è l'unico riferimento valido per performance e correttezza finale.

Renderer:

**direct vitaGL**

VINCOLO ASSOLUTO:

**Aurora renderer NON deve essere reintrodotto.**

Devono restare assenti:

`AuroraPacketRenderer`

`aurora::vita::gfx`

# REGOLE OPERATIVE

- Non creare branch.
- Lavorare sempre sul `main` locale.
- Non fare reset/clean/restore.
- Non fare commit o push.
- Preservare tutte le modifiche locali esistenti.
- Non modificare toolchain condivise.
- Non sacrificare correttezza del gioco per guadagnare FPS senza un kill-switch.
- Ogni ottimizzazione futura deve avere un flag/kill-switch quando ragionevole.
- Evitare trial-and-error cieco: ogni P6.xx deve partire da una evidenza misurata.
- Non aumentare ulteriormente la raw-mesh cache.
- Non tornare su texture/EFB salvo nuova evidenza concreta.
- Non cambiare arbitrariamente timing VI/simulazione per “fingere” FPS più alti.
- Obiettivo finale reale: **30 nuove immagini/s con simulazione corretta, input corretto e stabilità**.

# STATO PERFORMANCE: AUDIT GENERALE COMPLETATO

Considera ormai terminato l'audit performance generale.

Non voglio un'altra fase lunga di profiling ad ampio spettro.

Le aree principali sono già state isolate.

Il lavoro da pianificare adesso deve essere principalmente:

**riduzione del costo CPU guest/producer USER_0**

ed eventualmente spostamento sicuro di lavoro sui core disponibili.

# EVOLUZIONE MISURATA

## P6.59

Marker:

`9ED61D4A`

Prestazioni gara pesante:

- producer circa `421956 us`
- prep circa `90968 us`
- render circa `133774 us`
- raw mesh circa `220 hit / 5883 miss`
- EFB circa `106169 us`

## P6.60

Marker:

`534554F8`

Raw mesh cache ampliata a:

`2048 sets x4 = 8192 entries`

Risultato hardware:

- producer circa `255358 us`
- prep circa `83933 us`
- render circa `64153 us`
- raw mesh circa `4297 hit / 1538 miss`
- hit rate ~73.6%
- EFB circa `40553 us`

Questa è stata una grossa ottimizzazione reale.

## P6.61

Marker:

`8F2B5D4E`

Raw mesh:

`4096 sets x4 = 16384 entries`

Risultato hardware gara pesante:

- producer circa `221104 us`
- prep circa `83142 us`
- render circa `62542 us`
- logical draws ~`5886`
- physical draws ~`1149`
- raw mesh circa `5577 hit / 258 miss`
- hit rate ~`95.6%`
- vertex prep circa `73170 us`
- state prep circa `8776 us`
- EFB circa `39024 us`
- present/new-image circa `242365 us`
- circa `4.1 FPS`

IMPORTANTE:

**NON aumentare più la raw mesh cache.**

P6.61 costa già ~2.81 MiB statici in più rispetto a P6.60.

La cache raw-mesh non è più il collo di bottiglia dominante.

## P6.62

Marker:

`3FEB8E0B`

Introdotto:

`MKW_VITA_DL_TEMPLATE_DEP_SCOPE=1`

Obiettivo:

ridurre il costo delle verifiche ripetute durante il replay di display list.

A questo punto `GX::CallDisplayList` era ancora circa:

`56–74 ms/frame`

di cui template replay circa:

`39–43 ms/frame`

# STATO PIÙ RECENTE: P6.65

Ultima build:

`full-content-p6_65-text-state-dedupe-hot-O3-43724a1f`

Marker:

`3AB4B080`

VPK:

`/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_65-text-state-dedupe-hot-O3-43724a1f.vpk`

SHA-256:

`cc82ad4ce00cffb837ec24d24479e25ad249c4fae851ce4edfaf25fee9cf0c89`

Il nuovo runtime hardware mostra approssimativamente:

- image cadence: **~5.3 FPS**
- producer: **~188–190 ms/frame**
- guest CPU/frame: **~168.5 ms**
- prebegin: **~50.7 ms**
- prep worker: **~23.8 ms**
- render worker: **~18.5 ms**
- texture resolve: **~2.3 ms**
- texture cache: circa **240 hit / 0 miss**
- EFB: **~6–7 ms**
- logical draws: circa **1546**
- physical draws: circa **296**
- draw già fusi dal batching: circa **1250**
- `GX::CallDisplayList`: circa **18.4 ms**

Questi numeri sono fondamentali.

# INTERPRETAZIONE DELLE EVIDENZE

Il renderer non è più il principale responsabile dei ~190 ms/frame.

Texture:

- miss praticamente azzerati nella finestra osservata;
- resolve intorno a 2.3 ms.

EFB:

- sceso da centinaia di ms nelle vecchie build a circa 6–7 ms.

Renderer:

- circa 18.5 ms.

Prep worker:

- circa 23.8 ms.

Batching:

- 1546 draw logici → 296 fisici;
- quindi sta già facendo un lavoro molto efficace.

Display list:

- da decine/centinaia di ms nelle vecchie build a circa 18.4 ms.

Il costo dominante è ora chiaramente:

**guest / producer USER_0**

con circa:

`168.5 ms CPU/frame`

su circa:

`188–190 ms wall/frame`

Questo è il target principale P6.66+.

A 30 FPS abbiamo un budget totale di:

`33.3 ms/frame`

Quindi anche eliminando completamente renderer e prep, il guest attuale da ~168 ms renderebbe impossibile raggiungere 30 FPS.

Serve un cambio di scala nelle ottimizzazioni CPU guest.

# HOT PATH DA TENERE A MENTE

In precedenti profiling F06 erano emerse routine guest importanti.

Una precedente prova di native HLE diretta di:

`PSMTXRotTrig @ 0x8019A204`

aveva causato:

`Stale generated indirect dispatch winner at 0x8019A204`

Quindi NON registrare ingenuamente funzioni native che entrano in conflitto con la generated indirect-dispatch table.

La stessa classe di errore si è verificata con una prima P6.64 su:

`Layout::Draw @ 0x8007A990`

Per qualunque futura HLE/nativeizzazione:

**progettare prima una strategia compatibile con l'indirect dispatch generato.**

Possibili alternative da valutare:

- sostituzione al livello del generator/static recompilation;
- direct-call specialization;
- generated wrapper;
- patch controllata alla dispatch table;
- helper inline nel codice tradotto;
- trasformazione automatica delle routine leaf note;
- altre strategie che NON introducano stale winners.

# MULTITHREADING

La PS Vita offre core disponibili oltre al thread producer principale.

Attualmente esistono già worker come USER_2 per prep.

Valuta seriamente quali parti di USER_0 possano essere:

1. eliminate;
2. memoizzate;
3. nativeizzate;
4. precomputate;
5. spostate su worker;
6. divise in job paralleli.

Ma NON proporre genericamente "parallelizzare il guest".

Per ogni candidato multithread devi specificare:

- dati letti;
- dati scritti;
- dipendenze;
- sincronizzazione;
- barriera necessaria;
- possibilità di race;
- semantica Wii/GameCube da preservare;
- costo teorico della sincronizzazione;
- beneficio atteso.

# POSSIBILE CANDIDATO GIÀ NOTO: VERTEX PREP / HASH

In build precedenti:

`PrepareFrameCpuVertices()`

arrivava intorno a:

`~73 ms`

ed era attivo:

`MKW_VITA_VERTEX_REUSE_CACHE=1`

Esiste il sospetto che venga hashato l'intero payload del frame ogni frame senza abbastanza reuse reale.

Questo è un candidato indipendente da rivalutare.

Ma nelle build più recenti il prep worker complessivo è circa 23.8 ms, quindi devi verificare dai sorgenti e dai contatori correnti se questo problema è già stato ridotto o se rimane rilevante.

Non basarti solo sui vecchi ~73 ms.

# BUG TESTO DOPPIO: DA TENERE SEPARATO

C'è ancora un problema grafico nei menu:

testi apparentemente disegnati due volte.

P6.63/P6.64R hanno dimostrato che esistono molti glyph payload duplicati.

P6.65 ha introdotto una deduplicazione estremamente conservativa basata su:

- stesso owner;
- stessa geometria;
- stessa matrice;
- stesso raster state;
- stesso texture/TEV state.

Ultimo campione P6.65:

`candidates=44`

`suppressed=6`

`geometry_mismatch=27`

`state_mismatch=11`

Quindi la maggioranza delle copie visibili NON è identica a livello renderer.

NON rendere il dedupe più aggressivo.

Il problema del testo va trattato come bug grafico separato:

probabile differenza di transform/matrice/stato GX fra le due copie.

Per il piano performance P6.66 puoi lasciarlo in un workstream secondario.

Priorità primaria:

**FPS / guest CPU.**

# COSA VOGLIO DA TE

Produci un piano P6.66+ estremamente concreto.

Non limitarti a dire "profilare meglio".

Il profiling generale è già fatto.

Voglio una sequenza tipo:

P6.66
P6.67
P6.68
...

con interventi ordinati per rapporto:

**guadagno atteso / rischio / complessità**

Per ogni fase indicare:

1. **ipotesi precisa**
2. **evidenza che la giustifica**
3. **file/funzioni che probabilmente andranno modificati**
4. **tipo di ottimizzazione**
5. **eventuale struttura dati necessaria**
6. **se usa USER_0, USER_1, USER_2 o altro core**
7. **rischi di correttezza**
8. **kill-switch**
9. **telemetria minima necessaria**
10. **criterio hardware PASS/FAIL**
11. **guadagno atteso realistico in ms/frame**
12. **rollback condition**

# PRIORITÀ DA VALUTARE

Voglio che tu valuti esplicitamente almeno questi filoni.

### A. Guest translated hot functions

Identificare le routine PPC tradotte che costituiscono la maggior parte dei ~168 ms/frame.

Proporre un modo sicuro di convertirle in ARM/native helper senza ripetere il problema della indirect-dispatch table.

Valutare:

- SDK math;
- matrix operations;
- memcpy/memset-like loops;
- endian/unaligned helpers;
- paired-single emulation;
- integer conversion;
- container/iterator hot loops;
- virtual dispatch ripetitivo;
- state setters;
- NW4R math.

### B. PPC instruction lowering

Valutare se il costo deriva più dalle funzioni applicative oppure dalla qualità del codice generato.

In particolare:

- PSQ/paired-single;
- load/store endian conversion;
- branch dispatch;
- CR/XER manipulation;
- floating-point conversion;
- indirect calls;
- ABI bridge.

Proporre eventuali peephole/codegen optimization nel translator.

### C. Function-level O3/LTO/specialization

Attualmente esiste già una lista di 16 hot shard compilati O3.

Valutare se:

- ampliare il set O3;
- passare da shard-level a function-level hot compilation;
- usare `-Ofast` solo dove semanticamente sicuro;
- usare LTO selettivo;
- inline di helper PPC;
- clone/specializzazione di funzioni molto chiamate.

Non proporre `-Ofast` globale alla cieca.

### D. Producer prebegin ~50.7 ms

Questa voce è molto grande.

Scomporla concettualmente e proporre come ridurla.

Se è un aggregato, indicare quali sottofasi devono essere isolate con una telemetria di brevissima durata.

Qui accetto nuova telemetria perché è mirata a una voce già identificata, non un nuovo audit generale.

### E. Display-list residual ~18.4 ms

Valutare se il lavoro P6.62 può essere ulteriormente migliorato.

Ma deve avere priorità inferiore rispetto ai ~168 ms guest totali, a meno che dai sorgenti emerga un intervento molto semplice ad alto ROI.

### F. Prep/render overlap

Prep ~23.8 ms e render ~18.5 ms sono ormai sotto il budget individuale di 33 ms.

Valutare soprattutto se possono essere sovrapposti meglio al producer.

Non investire grandi quantità di lavoro per portarli da 20 ms a 10 ms se il producer resta a 170 ms.

### G. Jobifying guest work

Individuare eventuali operazioni deterministicamente parallele:

- animation transforms;
- matrix generation;
- scene graph calculations;
- particle calculations;
- visibility/culling;
- vertex transformation;
- decompression;
- resource update;
- AI;
- collision;
- altri blocchi indipendenti.

Per ciascuno indicare se può realmente essere spostato fuori dal guest execution seriale senza rompere side effects e ordine PPC.

# OBIETTIVO QUANTITATIVO

Stato corrente:

circa `188–190 ms/frame`

circa `5.3 FPS`

Target:

`<=33.3 ms/new image`

Non considero sufficiente un piano che porta solo:

190 → 170 ms.

Voglio capire se esiste realisticamente una strada verso:

- Stage 1: <150 ms
- Stage 2: <100 ms
- Stage 3: <66 ms
- Stage 4: <50 ms
- Stage 5: <=33 ms

Se ritieni che 30 FPS nativi siano irrealistici con questa architettura, devi dirlo chiaramente e spiegare:

- quale componente impone il limite;
- quale redesign sarebbe necessario;
- quale FPS realistico aspettarsi senza redesign;
- quale FPS aspettarsi con le ottimizzazioni proposte.

# OUTPUT RICHIESTO

Restituisci:

## 1. Diagnosi finale

Massimo 10-15 righe.

Spiega quale parte del frame dobbiamo attaccare adesso e perché.

## 2. Budget frame corrente

Tabella con:

- producer
- guest CPU
- prebegin
- display-list
- prep
- render
- EFB
- texture
- present/new-image

e percentuale rispetto al budget 33.3 ms.

## 3. Piano P6.66 → P6.xx

Una tabella ordinata con:

- fase
- intervento
- ms attuali coinvolti
- guadagno stimato
- rischio
- complessità
- dipendenze

## 4. Dettaglio delle prime TRE patch

Le prime tre devono essere implementabili direttamente dopo il tuo piano.

Specificare:

- file
- funzioni
- pseudocodice/approccio
- flag
- contatori
- acceptance gate hardware

## 5. Strategia native-HLE senza stale indirect dispatch

Questa sezione è obbligatoria.

Definisci una strategia robusta che possiamo riutilizzare per nativeizzare future hot function PPC senza causare:

`Stale generated indirect dispatch winner`

## 6. Strategia multicore PS Vita

Indicare cosa può essere realmente spostato fuori USER_0.

Separare:

- SAFE
- PROBABLY SAFE
- HIGH RISK / NOT WORTH IT

## 7. Roadmap verso 30 FPS

Fornisci la tua previsione quantitativa dopo ogni major milestone.

Esempio:

current 190 ms

→ patch A: 150 ms

→ patch B: 110 ms

→ patch C: 75 ms

ecc.

Non inventare guadagni: fornisci range realistici.

## 8. Primo intervento raccomandato

Concludi scegliendo **una sola P6.66**.

Deve essere il miglior intervento successivo sulla base delle evidenze disponibili.

Non implementarla ancora.

Spiega precisamente perché deve venire prima delle altre.