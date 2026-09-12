**Raccomando P6.66: specializzazione di `PSMTXRotTrig` dentro la funzione generata, mantenendo invariati simbolo, registrazione e dispatch.** È un intervento circoscritto su un hotspot già individuato e prepara una strategia riutilizzabile per le successive nativeizzazioni.

Ho analizzato sorgenti, configurazione, log locale e disassemblato l’ELF P6.65. Nessun file modificato; workspace rimasto su `main`.

## 1. Diagnosi finale

Il vincolo attuale è il lavoro seriale su USER_0: circa **168,5 ms CPU per immagine**, contro un budget complessivo di 33,3 ms.
Questo contatore comprende codice tradotto, HLE, callback e runtime sul thread producer: **non misura esclusivamente istruzioni PPC tradotte**.
`prebegin` e display-list sono sottoinsiemi del lavoro producer; non vanno sommati ai 168,5 ms.
Prep e render possono sovrapporsi al producer e oggi non giustificano un altro redesign grafico.
Le opportunità principali sono eliminare attraversamenti ABI, controlli memoria ripetuti e conversioni PSQ nei blocchi frequenti.
O3 è già attivo sui cinque candidati F06 esaminati: ampliarli indiscriminatamente non affronta questi costi strutturali.
Il codice contiene già NEON, caching registri, GQR hoisting, flag elision e specializzazioni ABI: bisogna estenderne la copertura, non riproporli come novità.
**30 FPS non sono sostenuti dalle evidenze attuali:** richiedono oltre 5× di accelerazione del solo USER_0, con ulteriore margine per sincronizzazione e runtime.
Una sequenza di piccole leaf nativeizzate può aprire la strada, ma difficilmente basta senza nativeizzare regioni complete.
Renderer sempre direct vitaGL; nessuna reintroduzione di Aurora.

**Provenienza:** il VPK P6.65 ha lo SHA-256 indicato. Tuttavia [runtime2.txt](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime2.txt:37) identifica **P6.64R, marker `9E02AE3F`**. I valori P6.65 seguenti sono quindi quelli forniti nella richiesta, non una misura P6.65 indipendentemente ricostruita dal log locale. L’ELF P6.65 verificato non definisce i simboli renderer vietati.

## 2. Budget frame corrente

Percentuale calcolata rispetto a **33,3 ms**, non rispetto alla durata attuale.

| Voce | Tempo corrente | Budget 33,3 ms | Interpretazione |
|---|---:|---:|---|
| Producer | 188–190 ms | 565–571% | Intervallo wall tra submit |
| Guest CPU / USER_0 | 168,5 ms | 506% | CPU dell’intero thread, inclusi HLE/runtime |
| Prebegin | 50,7 ms | 152% | Intervallo prima del primo begin registrato |
| Display-list | 18,4 ms | 55% | Incluso nel producer; può includere altro lavoro HLE |
| Prep | 23,8 ms | 71% | Worker USER_2 |
| Render | 18,5 ms | 56% | Worker USER_1, include attese |
| EFB | 6–7 ms | 18–21% | Incluso nel render |
| Texture resolve | 2,3 ms | 7% | Incluso nel render |
| Present / nuova immagine | ≈188,7 ms, 5,3 FPS | 567% | Risultato end-to-end |

**Non sommare le righe.** Anche `producer − guest CPU ≈20 ms` non rappresenta automaticamente un’attesa eliminabile: servono finestre temporali coincidenti.

Due precisazioni dai sorgenti:

- `prebegin` parte dall’ultimo `GX_HLE_TakeCpuPerfSnapshot()`, dopo `GXCopyDisp`, e termina al primo `GX_HLE_RecordBeginCaller()`. Può contenere simulazione, scheduler/audio, aggiornamento scena e preparazione iniziale GX. Non significa “50 ms di animazioni”.
- `gx_cpu_perf frame` e `sol_critical serial` utilizzano contatori differenti. Nel log locale, `frame=960` e `serial=960` descrivono momenti diversi.

Riferimenti: [contabilità CPU e prebegin](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/gx/gx_dl.cpp:1857), [SubmitFrame](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:9357).

## 3. Piano P6.66 → P6.75

Le stime sono **intervalli di lavoro da verificare**, non guadagni già misurati. Includo lo zero perché manca il self-time corrente dei candidati. Non si possono sommare i massimi: molte patch colpiscono gli stessi costi.

### Ordine proposto

| Fase | Intervento | Tempo attuale coinvolto | Risparmio ipotizzato | Rischio | Complessità | Dipendenze |
|---|---|---|---|---|---|---|
| **P6.66** | `PSMTXRotTrig`: fast path nell’entry generata | Quota non quantificata dei 168,5 ms | **0–5 ms USER_0** | Medio, circoscritto | Media | Contratto e fallback originali |
| **P6.67** | Risoluzione memoria una volta per leaf/blocco, inclusi store sicuri | Accessi ripetuti nei candidati F06 | **0–12 ms USER_0** | Medio | Media | Guardie memoria verificate |
| **P6.68** | Lowering PSQ/FP mirato: meno conversioni, GQR check e accessi TLS | Quota math residua | **0–20 ms USER_0** | Medio-alto | Alta | P6.66–67, oracle numerico |
| **P6.69** | Regioni con ABI specializzata e native helper più grandi | Traversal/setter/getter e bridge residui | **0–25 ms USER_0** | Medio-alto | Alta | Costi esclusivi e contratti |
| **P6.70** | Eliminazione/memoizzazione mirata nel prebegin | Fino a 50,7 ms wall; quota eleggibile ignota | **0–15 ms USER_0** | Alto | Alta | Breve breakdown prebegin |
| **P6.71** | Compilazione hot per funzione; LTO su un gruppo delimitato | Funzioni ancora costose dopo P6.69 | **0–8 ms USER_0** | Medio | Media-alta | Elenco per indirizzo, budget text |
| **P6.72** | Job di math/animazione su snapshot immutabili | Solo kernel indipendenti già nativeizzati | **0–20 ms sul percorso critico** | Alto | Alta | Contratti, fence, slack core misurato |
| **P6.73** | Ridurre doppio lookup nel replay template | Parte dei 18,4 ms DL | **0–4 ms USER_0** | Medio | Media | Breakdown DL e preflight stabili |
| **P6.74** | A/B senza hash completo vertex-reuse | Parte dei 23,8 ms prep | **0–10 ms USER_2; spesso ≈0 sulla cadenza** | Basso | Bassa | Hit rate per finestra |
| **P6.75** | Sovrapposizione e backpressure mirate | Solo attese effettive producer/worker | **0–5 ms oggi** | Medio-alto | Media | Timeline correlata; USER_0 ridotto |

P6.74 è un esperimento economico eseguibile anche prima, ma **non deve interrompere il lavoro principale su USER_0**.

### Gate comune a tutte le fasi

Ogni patch funzionale deve avere una variante OFF con la stessa baseline:

- Stesso percorso menu → gara, camera, giocatori, contenuti, clock e configurazione grafica.
- Confronto A/B/A di almeno tre finestre omogenee da 120 nuove immagini; separare caricamento, warm-up e gara stabile.
- Misurare CPU USER_0, intervallo nuove immagini, p50/p95, attese e contatori specifici della patch.
- PASS prestazionale solo se il beneficio supera la variabilità A/A e non peggiora sensibilmente la coda p95.
- Nessun aumento di draw scartati, errori EFB, fallback, problemi audio, memoria o crash.
- Correttezza: input, timer gara, progressione, callback e sequenza di simulazione. Il conteggio VI da solo non dimostra simulazione corretta.
- Rebuild degli artefatti interessati, verifica manifest/VPK, dispatch e assenza dei renderer vietati.

Per ogni fase: **mismatch di correttezza ⇒ flag OFF immediatamente**. Nessun reset del workspace.

### A. Quali hot function affrontare

I candidati storici F06 sono localizzati, ma **non esiste nei materiali correnti un ranking affidabile che attribuisca loro la maggioranza dei 168,5 ms**.

| Funzione | Evidenza corrente | Decisione |
|---|---|---|
| `PSMTXRotTrig @ 8019A204` | Tradotta, O3, 2.260 byte ARM; PSQ e TLS visibili nell’ELF | Prima specializzazione |
| `Pane::GetVtxPos @ 800797D0` | O3, 3.256 byte ARM; stack guest temporaneo, conversioni e accessi ripetuti | Range memoria, poi eliminazione temporanei provata |
| `TexMap::Get @ 800822F0` | O3, 4.434 byte ARM; chiamate a setter GX e bridge | Specializzare la regione, preservando gli effetti GX |
| `ConvertColorS10ToUT @ 805E7B40` | O3, 1.418 byte ARM per clamp/conversione di quattro componenti | Kernel intero esatto |
| `EGG::Thread::SwitchThreadCallback @ 802435DC` | Accessi guest e callback indirette | Ridurre lookup; mantenere seriale |
| `__AXDSPResumeCallback` e wrapper DL | Il token può restare visibile durante lavoro host sottostante | Non interpretarli come self-time |

Le dimensioni dell’ELF indicano espansione, **non tempo dinamico**. Le numerose chiamate ai fallback nel disassemblato non dimostrano che quei fallback siano eseguiti.

Molte SDK math sono già native in [mtx.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/math/mtx.cpp:95): identity, copy, concat, inverse, scale, transform e varie operazioni vettoriali. Nessun guadagno va attribuito alla loro “futura nativeizzazione”.

Per gli altri filoni:

- **Memcpy/memset-like:** riconoscimento di loop su RAM ordinaria; conservare overlap, ordine e side effect. Niente sostituzione generalizzata con `memcpy`.
- **Container/iterator:** specializzare traversal completi, evitando attraversamenti ABI per ogni nodo.
- **Virtual dispatch:** guardia sul target osservato, ramo diretto e fallback indiretto; mai assumere globalmente una vtable immutabile.
- **State setter:** eliminare lavoro soltanto con equivalenza completa dello stato e delle invalidazioni.
- **NW4R math:** preferire blocchi array/matrici a una successione di micro-HLE.

## 4. Dettaglio delle prime TRE patch

### P6.66 — `PSMTXRotTrig` specializzata nell’entry generata

**Ipotesi.** Una routine che costruisce una matrice da seno/coseno già disponibili paga molto più dell’aritmetica necessaria: rappresentazione paired-single, controlli GQR/memoria e materializzazione del contesto.

**Evidenza.** L’implementazione PPC occupa 176 byte; il simbolo nell’ELF P6.65 occupa 2.260 byte ed è già O3. Contiene anche una chiamata a `__emutls_get_address`. Il precedente tentativo è stato fermato dal dispatch, non da un confronto numerico fallito.

**File e funzioni:**

- [CxxLinearCodeGenerator.cs](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/translator/src/Translator.Core/CodeGen/CxxLinearCodeGenerator.cs:541): emissione dell’entry pubblica e del corpo di riferimento.
- [TranslatedBuildShardEmitter.cs](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/translator/src/Translator.Core/Build/TranslatedBuildShardEmitter.cs:620): dipendenze, fingerprint e composizione shard.
- Nuovo helper locale al runtime, per esempio `runtime/include/vita_guest_leaf_specializations.h`.
- [Makefile.vita](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/Makefile.vita:600) e [build_performance_profile.py](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/tools/build_performance_profile.py:545): flag, identità artefatti e profilo.

**Approccio:**

```cpp
// Codice emesso dal generator; nessuna nuova registrazione.
extern "C" void func_8019A204(CpuContext* ctx) {
#if MKW_VITA_ROTRIG_GENERATED_FAST
    if (RotTrigPreconditionsHold(ctx)) {
        RotTrigExactFast(ctx);
        return;
    }
#endif
    RotTrigTranslatedReference(ctx);
}
```

Il fast path deve:

1. Accettare soltanto il GQR store mode supportato.
2. Provare l’intero output RAM di 48 byte prima di scrivere.
3. Leggere seno, coseno e costanti guest prima degli store.
4. Preservare rounding, segno dello zero e rappresentazione paired.
5. Riprodurre **anche GPR/FPR/CR/LR e gli altri effetti osservabili del corpo originale**, non soltanto la matrice.
6. Usare il riferimento per condizioni non dimostrate, inclusi casi numerici inizialmente esclusi.

Asse non valido: inizialmente fallback originale, che conserva anche gli effetti sui registri.

**Struttura dati:** descrittore statico `{PC, fingerprint, contratto, helper}`; nessuna cache persistente o allocazione.

**Core:** USER_0.

**Flag:** `MKW_VITA_ROTRIG_GENERATED_FAST=0/1`; oracle separato e limitato.

**Contatori minimi:** chiamate, fast hit, fallback per causa, tempo aggregato della leaf su una breve finestra, mismatch oracle.

**Acceptance gate:**

- Boot completo attraverso registrazione e validazione dispatch.
- Confronto differenziale su memoria e `CpuContext`, inclusi assi X/Y/Z, NI, GQR, ±0 e casi limite.
- Sul device: almeno 25% di riduzione del costo della leaf e beneficio USER_0 distinguibile dal rumore; per promuoverla come ottimizzazione FPS, obiettivo minimo **1 ms/frame**.
- Menu, gara e transizioni invariati.

**Guadagno:** ipotesi **0–5 ms/frame**, subordinata al numero reale di chiamate.  
**Rollback:** mismatch, fallback dominante, regressione p95 o beneficio non rilevabile.

Questa patch apre una strada; **non le attribuirei decine di millisecondi senza misurarli**.

---

### P6.67 — Range memoria risolti una volta nei kernel hot

**Ipotesi.** I kernel piccoli ripetono controllo indirizzo, lookup pagina ed endian conversion per ciascun campo. La risoluzione di un range può ammortizzarli.

**Evidenza.** Su Vita, `ResolveRangeHost(... needsWrite=true)` restituisce attualmente sempre `nullptr`. Esiste invece `TryGetWritablePointerFast`, con gestione delle pagine scrivibili e guardie executable. Il generator dispone già di `GuestMemoryRangeLowering`.

Riferimento: [memory_access.h](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/include/memory_access.h:231).

**File/funzioni:**

- `MemoryInline::ResolveRangeHost`, `TryGetWritablePointerFast`, `WriteResolved*`.
- [GuestMemoryRangeLowering.cs](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/translator/src/Translator.Core/CodeGen/GuestMemoryRangeLowering.cs).
- [CxxLinearCodeGenerator.FlatGuestMemory.cs](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/translator/src/Translator.Core/CodeGen/CxxLinearCodeGenerator.FlatGuestMemory.cs).
- Allowlist iniziale: `GetVtxPos`, `ConvertColorS10ToUT` e altri blocchi call-free verificati; evitare l’intero `TexMap::Get`, che chiama GX.

**Approccio:**

```cpp
if (ProveAllRangesBeforeSideEffects(ranges)) {
    // Puntatori validi soltanto per questo blocco.
    ExecuteExactKernelWithResolvedPointers();
} else {
    ExecuteOriginalScalarAccesses();
}
```

La prova deve coprire:

- Overflow, limiti, contiguità e pagine attraversate.
- Accessi deferred/MMIO e protezione del codice.
- Alias fra sorgente e destinazione.
- Assenza di callback, yield, remapping o pubblicazione intermedia.
- Stessa semantica di write tracking/cache visibility del percorso originale.

**Non basta rimuovere `if (needsWrite) return nullptr`.** Serve un contratto di durata del puntatore e una guardia completa.

**Struttura dati:** `ResolvedGuestRange {guestStart, host, length, accessKind}` locale; niente cache globale di puntatori.

**Core:** USER_0.

**Flag:** `MKW_VITA_HOT_RESOLVED_RANGES`; selezione per PC e fingerprint.

**Contatori:** blocchi fast/fallback, accessi ammortizzati, byte, cause di fallimento, tempo aggregato dei kernel.

**Acceptance gate:**

- Test di confine pagina, indirizzi non allineati, alias e guardie executable.
- Memoria finale e contesto equivalenti.
- Nessuna nuova incoerenza delle generazioni GX.
- Obiettivo di promozione: **≥2 ms USER_0/frame** oltre il rumore.

**Guadagno:** **0–12 ms/frame**; i ms di P6.66 già eliminati non si ricontano.  
**Rollback:** pointer lifetime non dimostrabile, divergenza di memoria o beneficio inferiore al costo delle guardie.

---

### P6.68 — PSQ/FP lowering su regioni delimitate

**Ipotesi.** Il residuo math perde tempo nel passaggio fra scalar double, float e paired payload, oltre a ripetere informazioni GQR/NI già stabili.

**Evidenza.** Il codice generato usa sequenze `PPC_PsFromScalarInline` / `PpcSetPairedFprInline` / store PSQ. L’ELF mostra conversioni e trasferimenti di registri. Gli helper FP leggono stato TLS. NEON e GQR hoisting sono però **già presenti**.

**File/funzioni:**

- [ppc_isa_quantized.h](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/include/isa/ppc_isa_quantized.h:1220).
- [ppc_isa_float.h](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/include/isa/ppc_isa_float.h:201).
- [ppc_isa_fpenv.h](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/include/isa/ppc_isa_fpenv.h:22).
- `CxxLinearCodeGenerator.GqrHoisting.cs`, `RegisterResidency.cs`, `Expressions.cs` e analisi della rappresentazione.

**Approccio:**

```text
entry del blocco:
    acquisisci GQR e stato NI coerente
    verifica precondizioni del kernel
    risolvi i range necessari

blocco call-free:
    conserva paired lanes senza pack/unpack ridondanti
    applica gli stessi punti di rounding
    usa store endian equivalenti

uscita:
    materializza soltanto lo stato richiesto dal contratto
```

NI può essere passato esplicitamente agli helper del blocco per evitare accessi TLS ripetuti. Va riacquisito dopo qualsiasi evento che possa cambiarlo: callback, cambio contesto/fiber, scrittura FPSCR.

**Niente flag globale al posto del TLS.** I worker hanno stato FP distinto.

**Struttura dati:** fatti IR per lane, GQR, NI e liveness; nessuna cache frame.

**Core:** USER_0; gli helper riutilizzabili sui worker ricevono stato esplicito.

**Flag:** `MKW_VITA_PSQ_REGION_LOWERING`, con allowlist iniziale.

**Contatori:** esecuzioni regione, fallback GQR/FP, conversioni eliminate staticamente, tempo kernel misurato. Non contatori per istruzione.

**Acceptance gate:**

- Equivalenza per lane ordering, NaN, subnormal, signed zero, saturazione e rounding.
- Nessuna sostituzione approssimata di FMA/reciprocal/rsqrt.
- Confronto ARM dell’ELF per verificare che le conversioni siano realmente scomparse.
- Obiettivo di promozione: **≥3 ms USER_0/frame** e nessuna divergenza.

**Guadagno:** **0–20 ms/frame** sui kernel effettivamente raggiunti.  
**Rollback:** qualsiasi mismatch numerico oppure aumento di spill/code size che annulla il beneficio.

### Telemetria breve incorporata nelle prime patch

Una finestra di **32–64 frame stabili**, disattivata automaticamente, è sufficiente per scegliere i successivi interventi. Nessun nuovo audit generale.

Per `prebegin`, delimitare questi sottogruppi:

| Sottogruppo | Confini candidati |
|---|---|
| Scheduler/timing | `SelectThread`, switch callback, servizi VI/alarm |
| Audio sul producer | callback AX e servizi audio già identificati |
| Aggiornamento animazione | `ScnRoot::UpdateFrame @ 8006F590`, `CalcAnmScn @ 8006F830` |
| Matrici/scena | `CalcWorld @ 8006F9C0`, `CalcView @ 8006FA50` |
| Materiali/vertici | `CalcMaterial @ 8006FA10`, `CalcVtx @ 8006FA30` |
| Resto | Residuo esplicito, senza attribuirlo automaticamente alla simulazione |

Gli indirizzi sono verificati nella mappa locale. Occorre intersecare gli intervalli con la finestra prebegin e distinguere tempi inclusivi/esclusivi.

Per le leaf molto frequenti, usare chiamate aggregate e timing campionato; niente syscall di timing ad ogni invocazione. Overhead diagnostico da tenere sotto **1%**, verificato ON/OFF.

Il vecchio sampler non va riacceso ingenuamente: conserva token di ingresso senza rappresentare uno stack completo e in `EmitWindow()` contiene ancora la divisione `runClocks / MHz`, già corretta altrove.

## 5. Strategia native-HLE senza stale indirect dispatch

Il controllo in [abi_bridge.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/abi_bridge.cpp:200) rifiuta una voce statica quando:

- Il winner del registry è diventato Native.
- Il puntatore non coincide.
- Le maschere di preservazione non coincidono.

**Il controllo va mantenuto.**

### Strategia ordinaria: entry tradotta stabile, corpo specializzato

Per ogni funzione candidata:

1. Conservare l’entry pubblica `func_ADDRESS(CpuContext*)`.
2. Conservare `FunctionKind::BaseTranslated` e il contratto originale.
3. Inserire il fast path nel corpo generato.
4. Conservare il riferimento tradotto come fallback privo di registrazione autonoma.
5. Fare convergere chiamate dirette, indirette e tail-call sulla stessa entry.
6. Gestire separatamente eventuali entry interne/resume point: inizialmente restano tradotte.
7. Includere helper, contratto, flag e fingerprint nell’identità della build.

La categoria “translated” descrive qui l’entry e il suo contratto: il corpo può contenere C++ specializzato se ne preserva gli effetti.

**Attenzione agli inline:** eventuali copie già inline nei caller devono essere rigenerate o esplicitamente escluse dalla specializzazione; altrimenti il contatore della leaf sottostima il lavoro raggiunto.

### Quando serve una vera registrazione Native

È possibile, ma richiede rigenerazione coerente di:

- Indice dei native override.
- Esclusioni delle traduzioni.
- Trait per chiamate dirette.
- Bulk registration.
- Dispatch statico e relativi caller.

Il [generator del dispatch](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/translator/src/Translator.Core/Build/TranslatedBuildShardEmitter.cs:940) esclude già i winner Native. Il problema è mischiare quel mondo rigenerato con tabelle precedenti.

**Non raccomando una patch manuale della tabella:** dovrebbe aggiornare anche lookup, cache pubblicate e guardie ABI.

### Verifica obbligatoria

Un test host deve esercitare:

```text
registrazione → pubblicazione → validazione → InvokeIndirectCpu
```

oltre alla chiamata diretta. Chiamare soltanto l’helper non intercetta il problema che ha fermato P6.59/P6.64.

## 6. Strategia multicore PS Vita

Il budget disponibile è quello dei **tre core user**. `USER_ALL` comprende USER_0/1/2; non considero il core SYSTEM disponibile. [VitaSDK](https://docs.vitasdk.org/group__SceCpuUser.html).

L’affinità corrente in [host_thread.h](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/include/wiicompiled_vita/host_thread.h:18) è:

| Core | Lavoro corrente |
|---|---|
| USER_0 | Guest, scheduler Wii, producer/HLE |
| USER_1 | Render |
| USER_2 | Prep, audio, I/O; THP usa il ruolo GraphicsPrep |
| USER_1 oppure USER_2 | Background jobs |

### SAFE — lavoro host con ownership già separata

| Candidato | Letture / scritture | Dipendenze e barriera | Beneficio |
|---|---|---|---|
| Prep di un packet sigillato | Legge geometria/stato del packet; scrive buffer del proprio slot | `Prepping → Ready`; nessun accesso concorrente allo stesso output | Già implementato; nessun nuovo guadagno attribuito |
| Decompressione pura | Input compresso immutabile; output host privato | Join prima della pubblicazione guest; callback su USER_0 | Principalmente caricamenti, non FPS gara senza nuova evidenza |
| Conversioni array già native | Snapshot di matrici/vettori; output disgiunti | Una fence per batch; commit ordinato USER_0 | Solo se il batch è abbastanza grande |

Per la decompressione non si deve parallelizzare arbitrariamente un singolo stream con dipendenze back-reference: i job devono corrispondere a stream indipendenti.

### PROBABLY SAFE — dopo estrazione del kernel

| Candidato | Dati letti | Dati scritti | Sincronizzazione e semantica |
|---|---|---|---|
| Matrici animazione | Pose locali, risorse, tempo animazione fissato | Matrici host per oggetto | Parent prima dei child; batch di alberi indipendenti |
| Scene graph | Snapshot gerarchia e trasformazioni | World/view matrix separate | Ordine topologico; callback fuori dai job |
| Culling | Camera e bounding volume immutabili | Bitset/risultati indicizzati | Merge stabile nell’ordine originale |
| Particelle, solo matematica | Stato particella già determinato, parametri | Stato privato per particella | RNG, spawn/delete, callback e lista draw seriali |

Per ogni batch:

- Nessun `CpuContext` condiviso.
- Nessun GX, registry, scheduler o guest callback sul worker.
- Nessuna scrittura diretta alla memoria guest mentre USER_0 continua.
- Output pubblicato soltanto dopo completamento, nel punto originale della simulazione.
- Memoria scratch limitata e riutilizzata.
- Job non interrompibili lunghi evitati sui core audio/render.

La stima corretta è:

\[
\Delta T=C-\left(S+\max(C_0,C_1,\ldots)+B+K\right)
\]

dove `C` è il kernel seriale, `S` snapshot/submit, `B` barriera e `K` commit. Con due worker, il limite ideale è `C/2`, prima di contesa e sincronizzazione.

**Gate iniziale:** batch da almeno circa 1 ms, costo snapshot/sync/commit inferiore al 10% del kernel, nessuna regressione audio/render. Sono soglie di progetto; la latenza delle primitive Vita va misurata, non presunta.

**Prerequisito concreto:** rivedere `HostJobFence::wait/signal` in [host_jobs.cpp](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/host_jobs.cpp:20). Il predicato atomico viene modificato/notificato senza il mutex usato dal waiter: la sequenza attuale ammette una lost wake-up. Prima di introdurre join obbligatori, usare un protocollo che la escluda.

### HIGH RISK / NOT WORTH IT inizialmente

- Esecuzione concorrente di normali funzioni PPC sullo stesso stato guest.
- AI completa: RNG, ordine degli aggiornamenti, interazioni fra kart.
- Collisione e risoluzione dei contatti: dipendenze fra oggetti e ordine numerico.
- Scheduler Wii, switch callback, interrupt e callback audio.
- Traversal che mutano liste, materiali o risorse condivise.
- Job per singola matrice o singolo getter: sincronizzazione sproporzionata.

AI e collisioni potranno avere sottokernel paralleli, ma non sono candidati iniziali giustificati dal profilo disponibile.

### Dettaglio operativo delle fasi successive

| Fase | Implementazione, strutture, core e flag | Telemetria e PASS specifico | Rollback |
|---|---|---|---|
| **P6.69** | Estendere `StateFreeAbi.cs`, `RegisterResidency.cs` e contratti delle regioni getter/setter. Argomenti/risultati tipizzati, stato materializzato ai confini. USER_0. `MKW_VITA_HOT_REGION_ABI` | Chiamate ABI evitate, spill, tempo esclusivo; ≥5 ms USER_0 e contesto equivalente | Effetti non modellati, callback inattesa, peggioramento I-cache |
| **P6.70** | Nei kernel NW4R identificati, cache per versione oggetto/parent/animazione/camera. USER_0. `MKW_VITA_SCENE_DERIVED_REUSE` | Recompute evitati, hit, invalidazioni, byte; ≥5 ms e output identico | Invalidazioni incomplete o hit rate insufficiente |
| **P6.71** | `TranslatedBuildShardEmitter` separa funzioni hot per indirizzo, non hash shard storico; Makefile compila un’isola O3/LTO. USER_0. `MKW_VITA_HOT_FUNCTIONS`, `MKW_VITA_HOT_LTO` | Text loadable, spill/stack, tempo; ≥2 ms senza pressione memoria | Crescita text o runtime peggiore |
| **P6.72** | Kernel delle fasi precedenti + `HostJobSystem`, fence corretta, snapshot/output privati. USER_1/2 secondo slack. `MKW_VITA_SCENE_JOBS` | Snapshot, queue, execution, join, commit; ≥5 ms critici e audio invariato | Contesa, join troppo presto, race, memoria |
| **P6.73** | `GX__CallDisplayList`, preflight raw-mesh in `gx_backend.cpp`: passare handle già validati al replay, evitando lookup duplicati. Scratch limitato alla singola DL. USER_0. `MKW_VITA_DL_PREFLIGHT_HANDLES` | Lookup/validation risparmiati, fallback, `dl_other_us`; ≥1 ms | Handle invalidato da eviction/mutazione o fallback frequenti |
| **P6.74** | `PreparedVertexFrameKey` / `PrepareFrameCpuVertices`; A/B con `MKW_VITA_VERTEX_REUSE_CACHE=0`. USER_2 | `hash_us`, byte hashati, tentativi/hit per finestra; riduzione netta prep ≥1 ms | Il reuse risparmia più del costo hash |
| **P6.75** | `SubmitFrame`, prep/render worker: risolvere soltanto attese dimostrate, mantenendo ownership e ordine EFB. `MKW_VITA_PIPELINE_OVERLAP_V2` | Queue wait, idle, slot occupancy, serial completati; ≥1 ms di cadenza | Nuovi drop, letture anticipate, aumento latenza input |

### O3, LTO e `-Ofast`

La [configurazione hot corrente](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/Makefile.vita:600) usa 16 shard O3. Il problema è che gli shard sono grandi e i nomi dipendono dal contenuto.

Passerei a una lista stabile di **indirizzi guest + fingerprint**, facendo generare il gruppo hot. LTO va provato su quel gruppo e i suoi helper; i file non compilati con LTO non diventano automaticamente ottimizzabili attraverso il linker.

Non partirei da `-Ofast`: abilita assunzioni FP e altre trasformazioni più permissive. Per kernel interi o bitwise O3 è la prima scelta; eventuali rilassamenti FP devono essere singoli, documentati e confinati a domini dimostrati. [GCC: opzioni di ottimizzazione](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html).

### Vertex hash: problema ancora presente, priorità ridotta

`PreparedVertexFrameKey()` esegue ancora FNV-1a **byte per byte con accumulatore a 64 bit** su vertici, riferimenti matrice, draw, trasformazioni e texture.

Il contatore `vertex_reuse` è cumulativo per slot; non dimostra l’hit rate della finestra gara corrente. Inoltre il campo `reuse` di `direct_prep` riguarda le texture.

La prima prova deve quindi essere **disabilitare l’hash/reuse completo**, ricomputando gli stessi vertici. Nessuna cache più grande e nessun nuovo hash sofisticato prima di questo A/B.

### Testo doppio

Rimane separato. Nessun dedupe più aggressivo.

C’è però un dettaglio pratico: in `gx_text.cpp`, `ownerKey` è dichiarato sotto `MKW_VITA_TEXT_OVERDRAW_PROBE` ma usato dal dedupe. Per spegnere la scansione diagnostica mantenendo il comportamento P6.65, occorre separare l’estrazione owner dalla sonda. Non basta cambiare il flag.

Con 44 glyph non attribuirei a questa scansione decine di millisecondi; nei menu ricchi di testo va esclusa dalla baseline prestazionale con un A/B separato.

## 7. Roadmap verso 30 FPS

**Non c’è oggi una previsione credibile “190 → 150 → 100 → 66 → 33” ottenibile sommando le patch.** Manca la copertura dinamica dei kernel sostituibili.

Il vincolo quantitativo è comunque chiaro:

| Stage | Cadenza | Accelerazione minima dei 168,5 ms USER_0* |
|---|---:|---:|
| <150 ms | >6,7 FPS | >1,12× |
| <100 ms | >10 FPS | >1,69× |
| <66 ms | >15,2 FPS | >2,55× |
| <50 ms | >20 FPS | >3,37× |
| ≤33,3 ms | ≥30 FPS | ≥5,06× |

\* Limite ottimistico: concede al solo USER_0 l’intero budget, senza margine aggiuntivo.

Con la legge di Amdahl:

- Accelerare **l’80% del lavoro di 3×** lascia circa **78,6 ms CPU**.
- Accelerare **l’80% di 5×** lascia circa **60,7 ms CPU**.
- Accelerare **il 90% di 8×** lascia ancora **35,8 ms CPU**: già oltre il budget totale.

Quindi 30 FPS richiedono una copertura molto ampia, oppure parallelismo efficace sul residuo, oppure entrambi.

### Previsione condizionata per milestone

| Milestone | Intervallo di pianificazione | Valutazione |
|---|---:|---|
| Stato dichiarato P6.65 | 188–190 ms, ≈5,3 FPS | Baseline |
| P6.66–68, se i kernel hanno peso sufficiente | **160–185 ms**, ≈5,4–6,3 FPS | Riduzione locale; <150 non garantito |
| P6.69–71 con buona copertura dei traversal | **110–160 ms**, ≈6,3–9,1 FPS | Stage 1 plausibile; Stage 2 ancora ambizioso |
| Regioni più ampie nativeizzate + job utili | **70–110 ms**, ≈9–14 FPS | Richiede redesign del guest hot path |
| Copertura dominante della scena/simulazione | **40–66 ms**, ≈15–25 FPS | Scenario strutturale, non promessa delle patch elencate |
| Producer ≤28–30 ms, worker e sincronizzazione entro budget | **≤33,3 ms**, 30 FPS | Obiettivo non ancora dimostrato |

Questi intervalli sono **scenari tecnici**, non intervalli statistici derivati da benchmark.

La mia previsione operativa è:

- **Senza redesign delle regioni guest:** pianificare circa **6–9 FPS**, non 30.
- **Con nativeizzazione consistente di regioni e multicore selettivo:** **10–15 FPS** è un obiettivo intermedio sensato da verificare.
- **20–30 FPS:** richiedono un’ulteriore riduzione strutturale del lavoro seriale; le evidenze attuali non consentono di prometterla.

Il redesign necessario riguarda **granularità dell’esecuzione guest, rappresentazione numerica e confini ABI**, mantenendo direct vitaGL. Non richiede tornare ad Aurora.

Prep e render oggi sotto 33 ms non sono una garanzia per il futuro: quando il producer accelera, diminuisce lo slack dei core e possono emergere contesa, backpressure e frame pesanti.

## 8. Primo intervento raccomandato

**Una sola P6.66: `PSMTXRotTrig @ 0x8019A204`, specializzazione esatta nell’entry generata con fallback originale.**

Viene prima perché:

1. È un candidato F06 già identificato e verificato nel codice corrente.
2. È già O3: l’intervento affronta il costo della traduzione.
3. È call-free e circoscritto, quindi consente una verifica differenziale completa.
4. Elimina la causa del precedente stale winner senza indebolire la validazione.
5. Fornisce il meccanismo riutilizzabile per nativeizzare le successive regioni.

**Non la considero la patch che porta a 150 ms.** La considero il primo passo controllato per verificare quanto lavoro USER_0 possiamo eliminare con questa strategia. Se il guadagno è sotto soglia, il fast path resta OFF e si procede al candidato successivo usando il contratto e l’infrastruttura verificati.

Nessuna implementazione eseguita.

<oai-mem-citation>
<citation_entries>
MEMORY.md:295-297|note=[preserve local changes and distinguish real hardware evidence from artifacts]
</citation_entries>
<rollout_ids>
</rollout_ids>
</oai-mem-citation>