# Tecniche MH3 trasferibili a WiiCompiled e Aurora Vita

Il contributo maggiore di MH3 è la sostituzione dei produttori GX del gioco: modelli, UI ed effetti emettono dati nativi e snapshot, che un thread render prepara e invia a GXM. WiiCompiled dispone già di asset offline, cache, shader specializzati e submission diretta, ma continua a produrre e decodificare il flusso NW4r/GX. Per ridurre questo costo occorre cambiare il contratto fra gioco e renderer, conservando il percorso originale per i casi ancora scoperti.

La repo è analizzata al commit `04092f4e9fe3906212eb27281c66ee72236a91b6`, pubblicato il 10 ottobre 2026. Le 45 tecniche del suo indice sono mappate in [TECHNIQUES.md](TECHNIQUES.md). Le misure MH3 sono dichiarazioni documentate dell'autore, con indicazione di Vita devkit, Raspberry Pi o host; non sono benchmark riprodotti su questo port.

## Identità dei checkout

| Checkout | Revisione osservata prima delle modifiche | Uso |
| --- | --- | --- |
| WiiCompiled | `90eb1ddc93d545a2b4509600de43904d13400196`, `codex/restart-iso-gxm-20261005` | Runtime Mario Kart PAL RMCP01 e integrazione GXM attiva |
| Aurora embedded | `7a69a51bf392c62e37aca5926f0dbd27cb0c128b`, `wiicompiled-integration-20261006` | Renderer effettivamente compilato dal port |
| Aurora standalone | `262e8e1e55ee238decd1d0220db7937c92526edf`, `vita-experiment` | Destinazione delle future modifiche generiche |
| `aurora-vita-max-prehardware` | Snapshot senza metadati Git | Pacchetto storico, non base corrente |

WiiCompiled era pulito salvo `2026-10-08-210330.png`, conservata. Embedded e standalone erano puliti. I file `gxm_renderer.cpp`, `aurora_vita_draw_sink.cpp` e `gxm_shader_gen.cpp` differiscono fra embedded e standalone; texture decoder, fixed snapshots e command stream confrontati coincidono. Trasferire patch per funzione e contratto, senza sovrascrivere interi file.

MH3 ha importato Aurora da uno snapshot WiiCompiled e lo ha trasformato: backend GXM in `aurora-main/lib/gxm`, produttori nativi nel suo runtime, GX compatibility che abortisce. Il nostro GXM è in `platforms/vita/gxm` e mantiene il frontend GX. Gli stessi nomi di progetto non implicano la stessa API. La [provenienza di Aurora in MH3](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/aurora-dependency.md) descrive questa divergenza.

## Evidenza WiiCompiled che determina la priorità

Il [followup locale del 10 ottobre](../VITA_1FPS_DARK_WHITE_FOLLOWUP_2026-10-10.md) lega l'ultima sessione al SELF `f81f6ae6ce5f3aba2eeecaa3935ed37c1377eeecc694581b7d4e6c320f35a8ce` e ad Aurora `1cf5bca`, precedente al HEAD attuale:

- 6.154–6.299 draw richiesti, 5.367–5.748 merge, 424–808 draw GXM osservati, ancora circa 1,04–1,33 FPS.
- 12–13 richieste GXCopyTex, circa 11–12 copie GXM e due `sceGxmFinish` per frame osservato.
- `white_fallback_draws=0`: il bianco visibile non coincide con il fallback esplicito di texture mancante.
- Stage 0 e `race_frames=0` al frame 334: questa cattura non valida l'HUD in gara attiva.
- La prima sonda quiet aveva timer spenti e tempi zero. Il HEAD contiene la correzione, da provare con un nuovo SELF prima di attribuire millisecondi a una fase.

Il precedente campione in stage 2, non in pausa, documenta 1,133 FPS. È una sessione diversa: non va usata come controllo A/B del batching del 10 ottobre. I vecchi tempi di frontend, EFB e submit si sovrappongono e non si sommano.

Questa evidenza non giustifica un altro aumento del batching o del pool CDRAM. La prima priorità è distinguere EFB, distruzione di risorse in uso, shader/materiale, luce GPU e costo di preparazione. Le barriere CPU sono un esperimento separato: non ci si aspetta che da sole chiudano il divario verso 60 FPS.

## Primo trasferimento implementato

L'idea di [togliere barriere ripetute dai percorsi CPU](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/cpu/memory-barriers.md) è adattata in `runtime/include/abi_bridge.h` con `WIC_IMMUTABLE_DISPATCH_FASTPATH`, OFF per default.

Nei due memo `thread_local`, una voce viene riempita soltanto dopo l'acquire della pubblicazione definitiva del registro. Un hit dello stesso thread può quindi essere verificato prima di ripetere quell'acquire. Un miss e il primo accesso continuano ad acquisire la pubblicazione; nessun risultato provvisorio entra nel memo. La cache del solo verdetto `IsBaseTranslatedCpuTargetActive` può leggere il byte con ordine relaxed: il byte è il risultato completo e non pubblica un puntatore o altri dati. Il suo percorso di calcolo acquisisce ancora il registro e il suo store resta release.

L'acquire delle cache di puntatori condivise, la pubblicazione della tabella generata, le guardie GPR/FPR e la scelta del vincitore Native/Base/Mod sono conservati. Non viene modificato il traduttore né introdotta una nuova override a un indirizzo guest. Il contratto fondamentale è che `Finalize` è definitivo e il registro rifiuta registrazioni successive.

I test usano `abi_bridge.cpp` reale, controllano vincitori prima e dopo l'override, miss provvisori, memo che collidono, cache negative, sei lettori concorrenti e sia la tabella generata sia il percorso dinamico. Passano con flag 0 e 1. Il marker `[cpu-dispatch-config] immutable_fastpath=...` permetterà di riconoscere l'opzione nel log di una futura build. [VALIDATION.md](VALIDATION.md) distingue questi risultati dall'audit ARM e dalla prova Vita ancora necessari.

## Inventario GX di Mario Kart

`scripts/inventory_mkw_gx_callers.py` legge le dichiarazioni HLE GX, la mappa simboli e `generated/base_translation_output.json`, senza leggere o modificare disco, asset o dispositivo. [GX_CALLERS.json](GX_CALLERS.json) congela gli hash degli input e contiene:

| Dato statico | Numero |
| --- | ---: |
| Funzioni nel metadata della traduzione | 29.637 |
| Target GX/HLE dichiarati nei sorgenti | 341 |
| Target con chiamanti tradotti diretti | 155 |
| Funzioni tradotte con almeno una dipendenza GX diretta | 473 |
| Collegamenti distinti caller → target | 1.993 |

Sono incluse tutte le alternative del preprocessore, senza decidere quali vengano linkate. Non sono conteggi dinamici, copertura di runtime o prova di tutti gli accessi GX: mancano chiamanti nativi, chiamate indirette e scritture inline nel FIFO. Il numero di collegamenti non dà una priorità prestazionale.

I simboli indicano 21 chiamanti g3d, 31 effetti, 17 UI/testo, tre video, tre EFB/display, 173 SDK GX e 225 non classificati. Queste sono indicazioni da confermare sul codice. Fra i target con più chiamanti statici ci sono `GX::Begin` a `0x8016f0f0` (65), `GX::SetTevOrder` a `0x8017214c` (61), `GX::SetNumTevStages` a `0x801722a8` (58), `GX::SetVtxAttrFmt` a `0x8016dc68` (54) e `GX::SetBlendMode` a `0x8017277c` (53).

Punti di ingresso locali per il lavoro successivo: `nw4r::g3d::ResShp::CallPrePrimitiveDisplayList` a `0x8004f8e0`, `ResTev::CallDisplayList` a `0x80050340`, `nw4r::lyt::Picture::DrawSelf` a `0x8007b220`, `TextBox::DrawSelf` a `0x8007b870`, `nw4r::ef::DrawStrategyImpl::SetupGP` a `0x8003cb60`, `RFLiSetupCopyTex` a `0x800c2550` e `MoviePaneHandler::Draw` a `0x805fbb60`. La condivisione NW4r con MH3 è utile; offset e versioni delle strutture richiedono verifica RMCP01.

## Sostituire NW4r e GX per famiglie

MH3 costruisce draw tipizzati da produttori C, con adapter C++ per leggere il guest. Per i modelli, il game thread cattura record statici e input live; il render thread prepara materiale e primitive senza rileggere memoria guest modificabile. Tabelle per modello risolvono una volta dizionari, materiali, shape e nodi. Gli stati risultanti sono verificati contro un proprietario logico di riferimento prima di eliminare il vecchio percorso. Fonti: [GX replacement](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/gx-replacement.md), [deferred model preparation](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/deferred-model-preparation.md).

Per WiiCompiled serve un'interfaccia nativa Aurora che accetti geometria residente, stato completo di materiale/raster, pose e risorse con identità e revisione. Va mantenuto l'ordine rispetto a copy, clear, target, callback e UI. `DrawSink::submit` diretto attuale riceve pacchetti già preparati dopo il frontend: non implementa questa sostituzione.

Iniziare con una famiglia immutabile misurata, non con la disattivazione globale di GX. I fatal GX di MH3 sono utili per un laboratorio di copertura dopo aver sostituito i chiamanti di quella famiglia; sul nostro percorso di compatibilità avrebbero subito effetto distruttivo. Materiali dinamici, callback, light map, Mii e casi non certificati conservano il percorso originale.

## Stati, pose e uniformi senza copie ripetute

Il nostro Aurora possiede già revisioni GX, snapshot immutabili, `FixedUniformPool`, command stream e cache fragment. Il vantaggio MH3 aggiuntivo è la granularità: struttura statica del materiale distinta da colori animati, illuminazione, texture, pose e view; capture soltanto dopo una revisione utile, confronto dei contenuti prima di copiare, ritiro per sequenza del pacchetto. Il suo materiale compilato può essere riusato direttamente invece di rifondere tutte le pubblicazioni per shape. Fonti: [state versions](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/threading/state-versions.md), [compiled materials](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/compiled-dispatch-materials.md).

Separare revisione della struttura e scritture dei valori live evita invalidazioni inutili. Un tag è valido soltanto se tutti i produttori rilevanti lo aggiornano; una scrittura guest non tracciata deve invalidare o prendere il fallback. Il completamento del pacchetto CPU basta per i dati CPU, non per memoria ancora letta dalla GPU: quest'ultima richiede il fence del frame GXM.

## EFB e ciclo di vita delle risorse

MH3 mantiene i feedback sul GPU come identità più revisione e alloca nuove versioni dei target quando i draw precedenti le leggono ancora. Copie e target vengono riusati dopo il completamento GPU, senza `Finish` ordinari. Il nostro Aurora ha già texture EFB, copie RAM differite e meccanismi di invalidazione: vanno estesi sui casi attribuiti, senza togliere la materializzazione necessaria quando il guest legge o sovrascrive una destinazione. Fonte: [scene feedback](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/scene-feedback.md).

Il passo concreto è associare ogni `[wic-resource-sync]` a handle, tipo, dimensione, producer e ultimo serial GPU. Il ritiro differito può eliminare un'attesa globale solo dopo aver garantito che nessun draw successivo usa l'oggetto vecchio e nessun guest reader richiede i byte. Per RFL/Mii e light map conservare EFB 1024×640, scissor esatto, orientamento e ordine delle copie locali.

Le regole MH3 di load/store del depth e di alpha costante hanno precondizioni esplicite: full clear, certificato del target e assenza di lettori successivi. Non si trasferiscono come flag globali. La configurazione GXM va ristabilita ad ogni BeginScene, compresi suspend/resume e recovery, come richiesto dal contratto Aurora locale.

## Shader fissi e diagnosi dei materiali

MH3 censisce le topologie TEV e le classifica in pochi modelli. Nella sua scena Alatreon, GPU skinning riduce circa 330 draw a 142 e il tempo render da 22,8 a circa 14 ms; i suoi materiali fissi hanno costi shader molto inferiori ai programmi generati iniziali. Sono risultati specifici MH3, utili a scegliere il tipo di intervento, senza una previsione di FPS Mario Kart. Fonti: [fixed shading](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/fixed-shading-models.md), [GPU skinning](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/gpu-skinning.md).

Mario Kart deve avere un proprio censimento di MDL0, UI, effetti e stati runtime. Le percentuali MH3 non descrivono RMCP01. Il nostro generatore già contiene ottimizzazioni TEV e fixed vertex: non occorre duplicarle. Nuove famiglie possono ridurre istruzioni e chiavi solo dopo un confronto indipendente di clamp, rounding, registri, swap, texgen, fog e alpha compare.

MH3 documenta approssimazioni che qui non adottiamo automaticamente: XOR/XNOR dell'alpha ridotti al primo confronto, OR disgiunto ridotto all'inviluppo, fog interpolata dai vertici, direzione delle luci lontane approssimata e inviluppi con oltre quattro pesi troncati e rinormalizzati. Per shape con più influenze conservare il percorso esatto oppure un fallback per quella shape. Gameplay e collisioni continuano a leggere le matrici CPU necessarie.

La [guida GXM](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/libgxm-for-wii-ports.md) offre tre piste utili per il bianco/scuro: una MAD float4 che sul dispositivo perde B/A e richiede due catene float2, conversioni sbagliate dei selettori matrice e attributi letti senza stream valido. Sono ipotesi da provare su uno shader/draw locale identificato, non diagnosi del nostro difetto. Ricontrollare anche palette delle normali, UV, target alpha e contenuto EFB.

## Banca shader e stati GXM precomputati

La banca MH3 enumera il dominio finito delle varianti e compila i programmi sul computer. Il nostro preload e i 477 shader acquisiti in precedenza coprono shader osservati, non dimostrano completezza. Per Aurora conviene un manifest con chiave canonica, sorgente finale, contratto di binding, versione compilatore, opzioni, checksum GXP e fallback. L'enumerazione offline richiede le varianti dinamiche di UI, effetti, fog e illuminazione; un censimento dei soli asset non basta. MH3 richiede `psp2cgc` fornito dall'utente per il suo workflow; non è una dipendenza già disponibile nel nostro checkout. Fonte: [offline shader bank](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/offline-shader-bank.md).

Un intervento GXM più circoscritto è `SceGxmPrecomputedDraw` per mesh residenti: una copia dello stato per ogni slot GPU, aggiornabile soltanto dopo il ritiro di quello slot. Le altre draw API devono ripristinare il percorso normale; budget pieno, shader nuovo, mesh dinamica o errore usano il fallback. Questa API non coincide con le display list preparate GX già presenti. Anche view/projection separate dai dati per draw possono ridurre le uniformi scritte, rispettando reflection, alignment e riserva del buffer. Fonte: [precomputed draws](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/precomputed-draws.md).

## Texture, streaming, audio e dati nativi

Aurora supporta già I8/IA8/RGB565 e un sottoinsieme CMPR→BC1 con policy conservative. MH3 aggiunge palette P4/P8, scelta del formato dal contenuto di tutti i mip, BC3 per texel trasparenti e swizzle arbitrary per dimensioni non POT. La sua riscrittura dei blocchi conserva endpoint e indici, ma l'interpolazione GX 3:5 differisce da BC 1:2: non promettere identità di tutti i texel. BC1 con alpha nero può alterare i bordi filtrati; BC3 va verificato sui sampler reali. Eventuali nuove varianti richiedono versione del sidecar, decoder di riferimento, TLUT/SRT e invalidazione delle risorse dinamiche. Fonte: [compressed textures](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/assets/compressed-textures.md).

MH3 converte mentre carica; noi prepariamo PSARC e sidecar sul computer. Conserviamo questo percorso ed entrambi i backend contenitore supportati, evitando directory estratte sulla Vita. Il trasferimento utile è il prefetch delle geometrie e dei record prima del primo draw, con I/O nativo unbuffered e nessun accesso al disco sul render thread nel percorso certificato. Nuovi formati RVZ/WIA non risolvono il costo del rendering attuale.

MH3 usa due decoder JPEG con read-ahead; il nostro port ha THP nativo e conversione AVC. Prima di aggiungere un altro decoder occorre misurare le card/video e validare ring, cache e YUV. Il mixer AX nativo è invece un possibile miglioramento separato: la macchina delle voci, i comandi per tick e i canali MIX stabilizzati sono riutilizzabili come struttura. Il sound engine Capcom di MH3 non sostituisce NW4r::snd di Mario Kart. I nostri PCM offline non eliminano da soli AX/guest callback. Confrontare mix, loop, panning, pause, reverb e backpressure prima di cambiare runtime audio.

Le copie compatte in formato nativo per collisioni possono essere utili alla KCL di Mario Kart, ma il layout di terreno MH3 non è KCL. Bounds conservativi, ordinamento originale, cache per identità di pista e fallback per NaN/casi limite sono il trasferimento; senza un profilo che mostri collisioni dominanti, il ticket ha priorità inferiore al rendering.

## Traduttore, memoria e toolchain

Sono trasferibili il censimento dei target, contratti per native precise, residency dei nonvolatile, passaggio di argomenti in registri host e flag liveness basata sull'EABI. Nel nostro traduttore esistono già contratti interprocedurali, state-free ABI e flag elision conservativa. Le uscite mantengono tutti i flag e le chiamate indirette sono un confine completo; la versione MH3 restringe questi casi secondo l'EABI. Questo può dare margine, ma richiede audit di ogni lettore CR/XER attraverso chiamate, assembly, thunk, continuation LR e target sostituibili, con rigenerazione di tutto il contratto dispatch. Fonte: [call boundary](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/recompilation/translator-call-boundary.md).

La memoria flat MH3 dipende da kubridge e da una whitelist di accessi hardware ottenuta da trace e scansione. Il nostro backend alloca backing MEM1/MEM2 e mirror controllati, con overlay e hook delle scritture differite/eseguibili. Un puntatore diretto generalizzato che li salta può riprodurre corruzioni di heap/EFB. Portare soltanto fast path dimostrati per RAM ordinaria, con fallback sui casi controllati.

ThinLTO e PGO sono un ticket finale. Il profilo MH3 non si applica a Mario Kart. Serve un profilo del nostro sorgente, raccolto su scene rappresentative, con toolchain Clang/lld compatibile con sysroot Vita e relocation dei veneer. Il dato MH3 di +13% logical FPS corrisponde a un aumento minore degli FPS mostrati, perché quella scena aveva un limite GPU. Non disattivare eccezioni o unwinding sulla sola base del suo preset: questo runtime li usa per errori di memoria e controllo. Fonte: [PGO and ThinLTO](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/cpu/pgo-and-thinlto.md).

## Threading e piattaforma Vita

Sono utili una separazione per ruolo, un core libero per il game thread, job sufficientemente grandi, semafori con ricontrollo della condizione e limiti di coda. I ring CPU e gli slot GPU sono distinti. MH3 impiega quattro pacchetti CPU e tre frame GPU; quei numeri non si trasferiscono come valori obbligatori. Il numero di frame in coda cambia anche la latenza input.

Il draw guest del frame N in parallelo alla logica N+1 richiede snapshot di tutti gli input, stack e scratch propri, trace dei conflitti, frees/destructor differiti e controllo delle scritture disco. MH3 accetta alcuni risultati del frame successivo per materiali ed effetti. Nel nostro port non spostiamo il guest multithread e gli oggetti live sullo stesso schema senza dimostrarne le dipendenze. Il sistema OS/fiber di Mario Kart resta necessario; MH3 può evitare un guest scheduler per caratteristiche specifiche del suo gioco.

Le fix newlib/pthread di MH3, TLS slot `0x89`, stream unbuffered, semafori, stdio locks, reent cleanup e clock sono candidati generici. Prima di adattarle, verificare i layout della VitaSDK installata e tracce di startup, TLS, SceFiber, audio e I/O. Il nostro `thread_local` è usato anche per CpuContext e controlli di rientranza: sostituirlo con stato globale sarebbe scorretto. La selezione mediante stack deve distinguere stack nativo e stack guest delle fiber. Fonti: [Vita TLS](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/platforms/vita-tls-lookups.md), [platform layer](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/platforms/vita-platform-layer.md).

CPU3 e CapUnlocker restano un esperimento distinto. Non copiare affinità, priorità o busy wait di MH3 senza misurare il nostro lavoro e la disponibilità di core; soprattutto non occupare stabilmente CPU3 con spinning. Input GameCube e ID `0x124`, callback VI/audio e packaging `ATTRIBUTE2=12` restano gate del port.

La simulazione a passo variabile MH3 non viene trasferita: riguarda centinaia di siti specifici, timer, input, fisica ed effetti. Mario Kart conserva il suo passo originale, i ghost e le regole della gara. Nemmeno l'approssimazione FMA diventa default per analogia con MH3. Il target è eseguire più velocemente lo stesso gioco.

## Ordine delle prossime integrazioni

1. Nuovo campione valido delle cause di sincronizzazione e confronto CPU lighting/TEV di riferimento sulla stessa scena e stesso SELF; chiudere materiali e HUD in stage 2.
2. Dinamica del censimento GX, copertura per famiglia e primo contratto nativo g3d/UI con oracle.
3. Record di materiale/pose versionati, ritiro delle risorse EFB e prefetch residente, uno per esperimento.
4. Skinning e shader esatti per famiglie certificate; stati GXM precomputati e uniformi per view.
5. Texture, audio, traduttore e SDK secondo i nuovi profili; PGO sul percorso stabile.

L'esperimento dispatch può procedere separatamente con flag OFF/ON e identità del binario. [TASKS.md](TASKS.md) specifica le dipendenze effettive e i gate. Nessuna modifica al renderer Aurora, installazione, rigenerazione di asset o build Vita è eseguita in questa attività.
