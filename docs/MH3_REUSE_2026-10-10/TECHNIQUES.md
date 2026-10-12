# Mappa completa delle 45 tecniche MH3

Ogni link punta al commit congelato `04092f4e9fe3906212eb27281c66ee72236a91b6`. La colonna applicazione descrive il trasferimento proposto, non una funzione già implementata. Solo M12 e la parte statica di M01 sono consegnati in questa attività; i restanti [ticket](TASKS.md) hanno gate separati di correttezza e prestazioni.

**WIC** indica runtime, traduttore e adattamenti RMCP01. **Aurora** indica il renderer generico, da integrare separatamente nei checkout embedded e standalone. Le tecniche della stessa riga possono coinvolgere entrambi. Le misure MH3 sono specifiche del suo gioco e dell'hardware dichiarato dall'autore.

## Ricompilazione — 5 tecniche

| Tecnica MH3 | Destinazione e stato locale | Applicazione e vincolo |
| --- | --- | --- |
| [Function discovery](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/recompilation/function-discovery.md) | WIC; scoperta e metadata già presenti | Riutilizzare controlli di completezza, target indiretti e riferimenti dati; M01 produce una mappa statica GX, senza garantire reachability. Nuovi target richiedono rigenerazione e oracle. |
| [Static RSO overlays](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/recompilation/static-rso-overlays.md) | WIC; DOL/REL e overlay già gestiti | Conservare identità, rilocazioni e lifecycle dei moduli locali. Le RSO MH3 e i loro indirizzi non descrivono il nostro StaticR.rel; nessuna sostituzione automatica del loader. |
| [Translator call boundary](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/recompilation/translator-call-boundary.md) | WIC; analisi interprocedurale e ABI già presenti; M11 | Contratti native precisi, argomenti host e residency nonvolatile; conservare guardie su target non certificati. Rigenerare bodies, traits, header e vincitori come un unico contratto. |
| [Flag elision](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/recompilation/flag-elision.md) | WIC; elision conservativa già presente; M11 | Audit CR/XER attraverso chiamate e ritorni prima di restringere le barriere secondo EABI. Indiretti, assembly, varargs e continuation mantengono il fallback. |
| [Cortex-A9 floating point](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/recompilation/cortex-a9-floating-point.md) | WIC; FMA esatta e opzione fast già esistenti | Riusare il metodo di profiling/disassembly ARM; FMA non fused cambia rounding e resta opt-in. Nessuna approssimazione globale della fisica o dei ghost. |

## CPU — 5 tecniche

| Tecnica MH3 | Destinazione e stato locale | Applicazione e vincolo |
| --- | --- | --- |
| [Native function ports](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/cpu/native-function-ports.md) | WIC; override native già presenti; M11/M16 | Specializzare leaf realmente calde con test differenziali. Le circa 1.650 native MH3 includono comportamento specifico: non importare registry, indirizzi o ABI del suo gioco. |
| [Flat guest memory](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/cpu/flat-guest-memory.md) | WIC; backing e range RAM ottimizzati già presenti | Valutare alias fixed/kubridge solo con trace e whitelist nostre. Conservare hook EFB/executable/overlay e fallback checked; evitare accessi hardware trasformati in RAM ordinaria. |
| [Compact native data](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/cpu/compact-native-data.md) | WIC; M16 | Cache native KCL per identità e revisione, bounds conservativi e ordine dei candidati invariato. La struttura terreno MH3 non è il formato collisioni di Mario Kart. |
| [Barriers on ARM32](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/cpu/memory-barriers.md) | WIC; **M12 implementato, OFF** | Hit memo TLS prima dell'acquire ripetuto e verdict scalare relaxed, dopo freeze immutabile. Pubblicazioni e cache pointer condivise conservano acquire/release; guadagno device ancora aperto. |
| [PGO and ThinLTO](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/cpu/pgo-and-thinlto.md) | WIC/Aurora; M14 | Build Clang equivalente, poi ThinLTO e profilo RMCP01 rappresentativo. Verificare veneer/relocation, eccezioni e ABI; non usare profdata MH3 o equiparare logical FPS e flip. |

## Threading — 5 tecniche

| Tecnica MH3 | Destinazione e stato locale | Applicazione e vincolo |
| --- | --- | --- |
| [Render thread and frame packets](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/threading/render-thread.md) | Aurora/WIC; consumer async già presente; M02/M03/M17 | Pacchetti immutabili per produttori nativi e proprietario GXM unico. CPU packet retirement e GPU resource retirement sono eventi distinti; dimensionare code e latenza. |
| [Pipelined draw](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/threading/pipelined-draw.md) | WIC; M17, successivo agli snapshot | Sovrapporre soltanto lavoro con dipendenze provate. Scratch separato, frees e distruttori differiti, input catturati; non accettare implicitamente risultati del frame successivo. |
| [Guest worker](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/threading/guest-worker.md) | WIC; worker nativi già presenti; M17 | Partizionare effetti e matrici dopo trace dei conflitti e callback. Non duplicare l'intero guest su un secondo thread senza CpuContext, stack e proprietà della memoria definite. |
| [State versions](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/threading/state-versions.md) | Aurora/WIC; revisioni/snapshot già presenti; M03 | Revisioni separate di pose, view e campi materiale; evitare copie identiche e catturare una sola volta per versione. Una scrittura non tracciata invalida la cache. |
| [Core layout](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/threading/core-layout.md) | WIC/Aurora; affinità sperimentali presenti; M17 | Separare ruoli e misurare lavoro/join per lane; CPU3 e CapUnlocker in esperimento distinto. Nessun busy wait permanente sul core di sistema. |

## Rendering — 16 tecniche

| Tecnica MH3 | Destinazione e stato locale | Applicazione e vincolo |
| --- | --- | --- |
| [libgxm for Wii ports](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/libgxm-for-wii-ports.md) | Aurora; backend GXM esistente; M00/M05 | Guida per matrici, attributi, shader, target e notifiche. RG/BA MAD, selettori e stream sono piste diagnostiche da verificare su un draw locale, non una diagnosi già provata. |
| [GX replacement](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/gx-replacement.md) | WIC/Aurora; principale gap; M01/M02 | Bypass per famiglie g3d/lyt/ef con API nativa completa, oracle e fallback GX. La submission diretta attuale non elimina il lavoro del produttore NW4r. |
| [Fixed shading models](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/fixed-shading-models.md) | Aurora/WIC; ottimizzazioni TEV già presenti; M05 | Censimento RMCP01 e famiglie esatte. I tre modelli/92,5% MH3 non dimostrano copertura Mario Kart; conservare combinazioni, clamp, registri e compare originali. |
| [Material uniform blocks](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/material-uniform-blocks.md) | Aurora; uniform pool presente; M03/M06 | Distinguere campi per materiale, view e draw, senza rifare risoluzione costanti invarianti. Reflection, alignment, slot e indirizzi restano parte del contratto. |
| [Offline shader bank](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/offline-shader-bank.md) | Aurora; cache import/prewarm presenti; M05 | Dominio finito enumerato e manifest sorgente/opzioni/binding/GXP. I 477 programmi osservati non sono prova di completezza; includere varianti dinamiche e fallback su miss. |
| [Shader specializations](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/shader-specializations.md) | Aurora; TEV optimizer presente; M05 | Specializzare condizioni provate: stage attivi, luce, alpha e target. Fog ai vertici e luci approssimate richiedono esperimenti separati; vietato confonderle con equivalenza esatta. |
| [Deferred model preparation](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/deferred-model-preparation.md) | WIC/Aurora; M02/M03 | Catturare static/live sul game thread, preparare primitive/materiali sul consumer da snapshot. Il consumer non legge oggetti guest modificabili, stack o puntatori invalidati. |
| [Compiled dispatch materials](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/compiled-dispatch-materials.md) | WIC/Aurora; M02/M03 | Compilare struttura una volta e patchare soltanto colori, luci, animazioni e risorse live. Identità/revisioni coprono tutte le scritture e callback del proprietario originale. |
| [Precomputed draws](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/precomputed-draws.md) | Aurora; M06 | Stato GXM precomputato per mesh/pipeline/slot; view separata e uniformi minime. Ripristinare il percorso context e mantenere fallback su miss, errore o budget pieno. |
| [GPU skinning](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/gpu-skinning.md) | WIC/Aurora; M07 | Palette per mesh e NodeMix eliminano split da dieci matrici. Conservare matrici CPU necessarie e fallback per troppi pesi; non troncare le influenze a quattro per analogia. |
| [Culling](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/culling.md) | WIC/Aurora; M07 | Bounds conservativi per tutte le matrici influenti. Eseguire callback necessarie anche sugli oggetti scartati e tenere visibili i casi con bounds sconosciuti. |
| [Scene feedback](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/scene-feedback.md) | Aurora/WIC; EFB e readback differito presenti; M04/M08 | Feedback GPU versionato e target ritirati dopo fence. Materializzare per veri lettori CPU e conservare FIFO, scissor, Mii/light map e precondizioni depth/load-store. |
| [Particle batching](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/particle-batching.md) | WIC/Aurora; batching draw presente; M09 | Producer ef compatto, vertici minimi e uniformi per draw. Merge soltanto adiacenti, stessi stati/range contigui, senza attraversare copy/clear/target/barriere. |
| [HUD and debug font](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/hud-and-text.md) | WIC/Aurora; M09 | Producer lyt/testo nativo e glyph map se misurata calda. Conservare blend/scissor/layout; il gate HUD richiede gara stage 2 con race_frames che avanzano. |
| [Presentation](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/presentation.md) | Aurora/WIC; present/async esistenti; M04/M17 | Notifiche GPU e coda flip dimensionata; misurare FPS mostrati e latenza input. Tre slot GPU e quattro pacchetti CPU MH3 non sono parametri obbligatori. |
| [Widescreen](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/widescreen.md) | WIC/Aurora; layout/viewport presenti; priorità bassa | Correggere aspect solo dopo output valido e budget GPU; 960×544 aumenta lavoro pixel. Conservare coordinate EFB fuori schermo e proporzioni HUD/RFL. |

## Asset — 4 tecniche

| Tecnica MH3 | Destinazione e stato locale | Applicazione e vincolo |
| --- | --- | --- |
| [Disc access](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/assets/disc-access.md) | WIC; ISO/PSARC e I/O nativo presenti; M10 | Prefetch metadata e sidecar, richieste async e I/O unbuffered misurato. Conservare backend e hash originali; nuovi formati disco non ottimizzano il frame corrente. |
| [Load-time asset conversion](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/assets/load-time-asset-conversion.md) | WIC/Aurora; conversione offline già presente; M10 | Rendere mesh/materiali/texture pronti prima del primo draw, con revisioni e fallback dinamico. Conservare il sidecar offline locale e nessuna directory estratta sulla Vita. |
| [Compressed textures](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/assets/compressed-textures.md) | Aurora/WIC; formati packed e BC1 parziale; M10 | P4/P8, BC3 e swizzle arbitrary dopo oracle di mip/TLUT/filtri. Endpoint e indici conservati non eliminano la differenza di interpolazione GX 3:5 vs BC 1:2. |
| [Hardware movie decoding](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/assets/hardware-movie-decoding.md) | WIC; THP nativo/AVC presenti | Confrontare pipeline JPEG/read-ahead/ring/cache con il percorso corrente solo se il profilo video lo richiede; doppio decoder non risolve automaticamente verde o corruzione. |

## Sistema — 4 tecniche

| Tecnica MH3 | Destinazione e stato locale | Applicazione e vincolo |
| --- | --- | --- |
| [System layer](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/system/system-layer.md) | WIC; OS/VI/audio/fiber presenti | Riutilizzare design per tick e callback native, conservando semantica locale. MH3 evita il guest scheduler; Mario Kart dipende da thread guest e attese. |
| [Saves on host files](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/system/saves-nand.md) | WIC; backend file/salvataggi presente | Confrontare error mapping, ordine e commit atomico senza cambiare il formato dei salvataggi. Non importare percorsi/title ID del gioco MH3. |
| [Native audio](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/system/native-audio.md) | WIC; PCM e AX native parziali; M15 | Voci host, delta per tick e MIX settled riducono lavoro ripetuto. Sound engine Capcom non è NW4r::snd; confrontare mix, loop, reverb e backpressure. |
| [Controller input](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/system/controller-input.md) | WIC; input GameCube verificato storicamente | Usare code input e timestamp coerenti al passo originale; preservare ID port 0 `0x124`, callback e menu. Mappatura Classic Controller MH3 non è quella locale. |

## Simulazione — 1 tecnica

| Tecnica MH3 | Destinazione e stato locale | Applicazione e vincolo |
| --- | --- | --- |
| [Variable step](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/simulation/variable-step.md) | WIC; **non adottare** | Patch MH3 specifiche a timer, input, fisica ed effetti. Mario Kart conserva tick originale, ghost e regole gara; il trasferimento utile è la misura separata di logica/presentazione. |

## Piattaforme — 5 tecniche

| Tecnica MH3 | Destinazione e stato locale | Applicazione e vincolo |
| --- | --- | --- |
| [Vita platform layer](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/platforms/vita-platform-layer.md) | WIC/Aurora; port SDK presente; M13 | Fix mirate per semafori, clock, stdio, pthread/reent e cleanup. Congelare VitaSDK e misurare prima di sostituire wrapper locali, inclusi suspend/resume. |
| [Vita TLS lookups](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/platforms/vita-tls-lookups.md) | WIC/Aurora; TLS per thread/fiber; M13 | Adapter emutls/reent basato su layout verificato; slot privato `0x89` non è API stabile. Nessuna globalizzazione del CpuContext o confusione fra stack nativo e fiber. |
| [Vita CDRAM budget](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/platforms/vita-cdram-budget.md) | Aurora; pool/counter presenti; M04/M10 | Budget per categoria, risorse residenti e ritiro fenced. Il campione locale non indica esaurimento; aumentare il pool senza attribuzione non chiude 1 FPS. |
| [Linux GLES and ARM32](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/platforms/linux-gles-and-arm32.md) | WIC/Aurora; harness host da valutare | Backend oracle e benchmark ARMv7 utili ai contratti e ai costi relativi. Raspberry Pi e renderer desktop non dimostrano pixel, throughput o FPS Vita. |
| [Web build](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/platforms/web-build.md) | WIC/Aurora; fuori dalla priorità Vita | Harness portabile per regressioni/scene se conveniente; nessuna migrazione Emscripten richiesta dal problema corrente. Il browser non è un benchmark SGX543. |

## Confini del riuso

Il fork Aurora di MH3 ha API e layout differenti. Le righe Aurora sono adattamenti per contratto, non file da sovrascrivere. Il codice WIC upstream è GPLv3, Aurora è MIT: prima di incorporare implementation verificare la licenza dei singoli file e conservarne notice/provenienza. Qui sono salvati analisi, metadati di dipendenza e una modifica locale del runtime; non sono copiati shader, asset o implementazioni del gioco MH3.
