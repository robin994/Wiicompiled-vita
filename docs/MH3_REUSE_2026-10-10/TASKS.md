# Ticket per integrare le tecniche MH3

Ogni ticket produce una modifica separata con fallback, sorgenti e opzioni registrati. Una prova host può chiudere il contratto; il gate di prestazioni richiede il dispositivo. Nessun ticket deve annullare le correzioni locali di EFB, scissor, BeginScene, input, audio o gestione della memoria. Le destinazioni Aurora embedded e standalone si integrano separatamente.

## M00 Attribuzione e correttezza del rendering

**Stato:** prerequisito hardware aperto; gli strumenti esistono già.

**File:** `vita/game_diagnostics.cpp`, `scripts/analyze_vita_render_budget.py`, `scripts/install_vita_current.py`, `aurora-main/platforms/vita/gxm/gxm_renderer.cpp`, `platforms/vita/gx/aurora_vita_draw_sink.cpp`.

**Scope:** usare la sonda con clock corretto per attribuire finish, EFB flush/capture, GXM draw e attese. Confrontare CPU lighting e riferimento TEV separatamente sulla stessa scena. Identificare un draw bianco per shader, texture, UV, palette e target.

**Dipendenze:** nessuna; il runbook corrente richiede build Vita manuale. **Fallback:** profilo attuale e stessa configurazione, probe OFF.

**Correttezza:** materiale stabile su più frame, HUD in stage 2 con `race_frames>0`, input e audio continui, nessun timer campionato nullo per gate diagnostico. **Prestazioni:** tre run quiet della stessa scena; i tempi diagnostici non sono il baseline FPS e non si sommano.

## M01 Censimento delle famiglie GX

**Stato:** inventario statico implementato, misura dinamica da aggiungere.

**File:** `scripts/inventory_mkw_gx_callers.py`, `GX_CALLERS.json`, `runtime/src/hle/gx/`, `projects/mkwii/MAP.txt`, metadata della traduzione e diagnostica guest.

**Scope:** partire dai 473 chiamanti; verificare famiglie e versione NW4r, aggiungere copertura dinamica opt-in per caller/target/famiglia, comprese emissioni FIFO inline e chiamate indirette. Censire stati TEV per draw di model/UI/effects, senza dump asset.

**Dipendenze:** M00 per priorità temporali. **Fallback:** contatori disattivati e inventario statico, con limiti espliciti.

**Correttezza:** contatori non cambiano il guest; sample completo per scene di riferimento; non classificare automaticamente una funzione anonima. **Prestazioni:** i conteggi stabiliscono copertura e quota di costo, non un guadagno.

## M02 Contratto Aurora per modelli nativi

**Stato:** progetto di interfaccia e implementazione da eseguire.

**File:** `aurora-main/include/aurora/`, `platforms/vita/gfx/vita_gfx_types.hpp`, `vita_draw_adapter.*`, `platforms/vita/gx/aurora_vita_draw_sink.*`, `scripts/native_assets/formats.py`, `vita/native_assets.*`, native overrides RMCP01 individuate da M01.

**Scope:** record versionato per shape/materiale/pose, risorse con identità e revisione e draw order completo. Tabelle per MDL0 eliminano dizionari e setup ripetuti della famiglia certificata. Il consumer non deve leggere guest mutable. Prima eseguire il percorso nativo in modalità oracle senza presentare i suoi pixel, poi selezionarlo per famiglia.

**Dipendenze:** M00, M01. **Fallback:** frontend GX originale per famiglia, callback, risorsa dinamica o record sconosciuto.

**Correttezza:** confrontare geometria, matrici, stato materiale, texture/TLUT, callback e immagini; contare tutti i draw coperti; zero letture guest tardive. **Prestazioni:** riduzione misurata di emissione/decode e tempo per famiglia; shader e asset invariati nel primo confronto.

## M03 Pose e materiali con versioni immutabili

**File:** `vita_fixed_snapshots.hpp`, `vita_fixed_uniform_pool.hpp`, `vita_command_stream.hpp`, `vita_texture_snapshots.hpp`, `aurora_vita_draw_sink.cpp`, produttori g3d di M02.

**Scope:** separare struttura, pose, view, colori, luce e texture. Revisioni aggiornate solo per cambiamenti effettivi; confronto esatto prima di una copia; materiale compilato per struttura e patch del solo input live. Eliminare reset/copie duplicati quando il candidato è completo.

**Dipendenze:** M02 per revisioni dei produttori nativi; il sotto-ticket di riuso dei blocchi già completi può usare l'interfaccia esistente. **Fallback:** snapshot completo attuale.

**Correttezza:** cambiamenti alternati A/B/A, scritture non tracciate, clear/reset, copie guest, TLUT, camera e pose; nessuno snapshot mutato prima dell'execute. **Prestazioni:** conteggio copie, byte e invalidazioni per frame più confronto quiet; evitare un cache lookup più costoso della copia rimossa.

## M04 Ritiro di risorse e target dopo completamento GPU

**File:** `gxm_renderer.cpp`, `gxm_memory.*`, `vita_buffer_pool.*`, `vita_texture_cache.*`, `vita_persistent_geometry.*`, `vita_streaming_arena.*`, copie EFB nel draw sink.

**Scope:** attribuire le risorse che causano `finish_destroy`, conservare la vecchia versione fino all'ultimo serial CPU/GPU pertinente, pool di target per formato/dimensioni. Distinguere risorse ancora referenziate dal command stream da quelle già inviate a GXM.

**Dipendenze:** M00 e audit lifetime locale. **Fallback:** finish attuale per caso non certificato, shutdown, errore o pressione di memoria.

**Correttezza:** più scene nello stesso frame, resize, target switch, scena fallita, suspend/resume, eviction e low-memory; nessun handle riusato troppo presto. **Prestazioni:** riduzione attribuita di count/durata finish e tail latency, senza aumento illimitato di memoria.

## M05 Materiali fissi esatti e banca shader

**File:** `gxm_shader_gen.*`, `gxm_tev_opt.hpp`, `vita_pipeline_key.*`, `gxm_program_cache.*`, `vita_program_binary_cache.hpp`, `scripts/native_assets/shader_cache.py`, `scripts/import_vita_shader_cache.py`.

**Scope:** censimento RMCP01; classifier di famiglie equivalenti, canonicalizzazione delle chiavi ignorate dal singolo stage, dominio finite enumerabile e manifest GXP con compilatore/opzioni/binding. Diagnosi isolata RG/BA e selettori matrice prima di cambiare il programma generale.

**Dipendenze:** M00, M01; M02 per bypass completo del setup. **Fallback:** TEV originale e cache/compiler attuale per miss. Uno shader osservato non rende completo il dominio.

**Correttezza:** oracle indipendente TEV, alpha XOR/XNOR/OR disgiunto, clamp/wrap, signed registri, texture indirette, fog, UI e late-loaded effects. Nessuna semplificazione approssimata introdotta implicitamente. **Prestazioni:** GPU/CPU reali, cold/warm separati; conteggio cicli del compilatore non basta.

## M06 Stati GXM precomputati e uniformi per view

**File:** `gxm_renderer.cpp`, `gxm_shader_gen.*`, `vita_pipeline_cache.*`, `vita_streaming_arena.*`, `vita_persistent_geometry.*`.

**Scope:** cache `SceGxmPrecomputedDraw` per mesh/pipeline con stati distinti per slot; View separata da Draw, scrittura limitata ai campi realmente letti. Deselezionare gli stati precomputati prima di ogni altro percorso.

**Dipendenze:** M00, lifetime slot verificato; M02 aumenta la copertura, senza essere requisito per una mesh residente già supportata. **Fallback:** chiamate context GXM correnti su miss/errore/budget pieno.

**Correttezza:** riuso con texture, blend, target, scissor, programma e indirizzi nuovi, passaggi precomputed↔context e slot wrap; uniformi conformi alla reflection e `BeginScene`. **Prestazioni:** ridurre CPU submit per mesh; una diminuzione di questa fase non prova FPS se domina EFB/GPU.

## M07 GPU skinning e culling conservativo

**File:** convertitore MDL0 in `scripts/native_assets/formats.py`, `vita/native_assets.*`, `vita_fixed_vertex.*`, `vita_vertex_pipeline.*`, `gxm_shader_gen.*`, produttori g3d.

**Scope:** leggere NodeMix e matrici inverse bind, palette per mesh, layout con influenze; ridurre le draw spezzate dai dieci slot GX. Bounds per tutte le matrici usate, culling solo dopo callback necessarie.

**Dipendenze:** M02, M03, M05. **Fallback:** forma a palette GX per troppi pesi, uniforme insufficiente, callback non certificata o bounds ignoti.

**Correttezza:** non troncare a quattro influenze per analogia con MH3; normali, envelope, reflection map, scaling non uniforme, NaN e bordi scissor. Conservare matrici CPU lette da collisioni, effetti e camera. **Prestazioni:** draw/frame e costo skin/view CPU/GPU; la palette più grande non deve peggiorare la scena.

## M08 Feedback EFB e certificati di depth e alpha

**File:** `runtime/src/hle/gx/gx_copy.cpp`, `gx_guest_write_hooks.cpp`, `aurora_vita_draw_sink.cpp`, `vita_efb*`, `gxm_renderer.cpp`, `gxm_efb_ops.hpp`.

**Scope:** individuare destinazioni lette soltanto dalla GPU e tenere il feedback come risorsa versionata. Readback per lettore CPU reale. Full clear e lettori depth successivi guidano load/store; certificato alpha 1 invalidato su clear parziale/copia/upload/scrittura non provata.

**Dipendenze:** M00, M04, trace dei lettori. **Fallback:** materializzazione/copertura/load-store generali.

**Correttezza:** RFL/Mii, light map, copie colore/profondità, CPU writes sovrapposti, scene multiple e callback; ordine FIFO e pixel esatti. **Prestazioni:** costo copie/scene/readback attribuito; non eliminare una copia solo perché MH3 non la legge sul CPU.

## M09 UI ed effetti nativi compatti

**File:** `runtime/src/hle/gx/gx_dl.cpp`, producer `nw4r::lyt`/`nw4r::ef` individuati da M01, native geometry API, `vita_draw_batch.hpp`, streaming arena.

**Scope:** quads nativi con soli campi per vertice variabili, uniformi per draw e indici statici; glyph map se la ricerca font domina. Unire soltanto draw adiacenti con stessi stati e range contigui, senza attraversare barriere.

**Dipendenze:** M00, M01, M02 per il contratto condiviso. **Fallback:** draw originale per strategy/shape non coperta.

**Correttezza:** HUD corretto, testo completo, blend/alpha/depth/scissor, UI dopo cambio viewport, ordine degli effetti e limite U16. **Prestazioni:** ridurre producer CPU, byte copiati e draw; il batching attuale da solo non ha migliorato 1 FPS.

## M10 Formati texture e prefetch dei sidecar

**File:** `vita_texture_decode.*`, `vita_native_assets.*`, `gxm_renderer.cpp`, `scripts/prepare_mkw_native_assets.py`, `scripts/native_assets/formats.py`, `vita/native_assets.*`, `vita/psarc_backend.*`.

**Scope:** palette P4/P8, BC3 quando utile, swizzle arbitrary non POT e formato scelto da tutti i mip. Precaricare metadata/geometria/texture durante il load, senza I/O al primo draw certificato. Conservare PSARC/ISO selezionato e hash originali.

**Dipendenze:** census degli asset e M00; lifecycle M04. **Fallback:** formato attuale e converter/versione precedenti, archivio intatto.

**Correttezza:** round-trip di ogni mip/palette, TLUT live, sampler lineare/trilineare, wrap/bordi alpha e decoder GX indipendente; dichiarare differenza CMPR 3:5 vs BC 1:2. **Prestazioni:** memoria CDRAM, miss/upload e frame più lento di caricamento; misura separata dal tempo di conversione offline.

## M11 Confini ABI e flag del traduttore

**File:** `translator/src/Translator.Core/Analysis/GuestFlagLivenessAnalyzer.cs`, `GuestAbiContractAnalyzer.cs`, analisi residency/SSA, `CodeGen/CxxLinearCodeGenerator.FlagElision.cs`, `StateFreeAbi.cs`, emettitore shard/dispatch, `runtime/include/abi_bridge.h`.

**Scope:** audit lettori CR/XER attraverso chiamate, eventuale restrizione EABI dei volatile, residency nonvolatile e argomenti host. Native precise solo con effetti dimostrati. Rigenerare traduzioni, traits, header statici e tabella dei vincitori come un unico contratto.

**Dipendenze:** profilo CPU valido, oracle ISA e corpus RMCP01. **Fallback:** confini conservativi e guardie attuali.

**Correttezza:** assembly non EABI, varargs CR1, thunk save/restore, `mfcr`, continuation LR, indiretti, override Native/Mod e registrazioni post bootstrap. **Prestazioni:** contare istruzioni/barriere nel vero ELF e ms CPU Vita; codice più grande va misurato per instruction cache.

## M12 Memo del dispatch immutabile

**Stato:** implementato, OFF per default; host verificato.

**File:** `runtime/include/abi_bridge.h`, `runtime/tests/immutable_dispatch_tests.cpp`, `vita/CMakeLists.txt`, `vita/game.cmake`, `vita/game_main.cpp`.

**Scope:** memo TLS prima dell'acquire ripetuto, verdetto scalare relaxed; niente rimozione delle pubblicazioni o delle guardie registri. **Dipendenze:** registro congelato per tutta la vita del processo. **Fallback:** `WIC_IMMUTABLE_DISPATCH_FASTPATH=OFF`.

**Correttezza:** test con flag 0/1, override prima del freeze, miss pre-freeze, memo negativo/collisione, lookup multithread e tabella generata. **Prestazioni:** resta da compilare/auditare il vero ELF ARM e confrontare la stessa scena Vita; nessun guadagno FPS dichiarato.

## M13 TLS e fix SDK Vita

**File:** `vita/game_stdio.cpp`, `vita/runtime_checks.cpp`, `runtime/src/host_context.cpp`, `fiber_manager.cpp`, `host_wait.cpp`, VitaSDK newlib/pthread installate e futuro adapter specifico.

**Scope:** identificare costo emutls/reent, mutex/sema, clock e stdio concorrente nel profilo; adattare singole wrap con layout SDK congelato, fallback cold e cleanup corretto. Conservare TLS per thread e contesto delle fiber.

**Dipendenze:** identità VitaSDK e audit librerie; non basta copiare `fast_tls.c`. **Fallback:** SDK originale e wrapper OFF.

**Correttezza:** creazione/distruzione thread, TLS keys, errno, native/guest stack, once prima del main, file aperti in parallelo, descrittori standard e wait/cancel. **Prestazioni:** riduzione dei campioni kernel/TLS e stalli, senza busy wait aggiuntivo.

## M14 Clang ThinLTO e PGO rappresentativo

**File:** `build-vita.sh`, `vita/game.cmake`, CMake runtime/Aurora, nuovo preset/toolchain sperimentale, linker map e script profili.

**Scope:** prima build Clang/lld equivalente senza PGO; poi ThinLTO; infine profilo nostro del medesimo sorgente. Scene: menu/video/Mii/selezione, Time Trial e altre piste, effetti e audio. Nessun profilo MH3 importato.

**Dipendenze:** percorso corretto e stabile M00, corpus di test, ambiente Clang e relocation Vita verificate. **Fallback:** toolchain GCC attuale e flag invariati.

**Correttezza:** PIC veneer/relocation, eccezioni usate dal runtime, traits generati, runtime ABI, attributi SELF/SFO e avvio device. **Prestazioni:** tempo CPU, FPS mostrati, dimensione codice, RSS e tempi build; logical FPS non equivale a flip.

## M15 Voci e mixer audio nativi

**File:** `runtime/src/hle/audio/ax_*`, `audio.cpp`, `vita/game_audio.cpp`, `vita/native_audio.*`, adapter NW4r::snd del gioco.

**Scope:** record host per voci e delta dei campi, tick e command ring, canali MIX lasciati settled su setter identico. Il motore Capcom MH3 non viene copiato come sostituto NW4r::snd.

**Dipendenze:** callback/backpressure corretti e profilo audio. **Fallback:** mixer AX corrente, stessa configurazione e salvataggi.

**Correttezza:** ADPCM/PCM, loop e stream, panning/envelope/resampling, reverb/aux, pause, starvation transitoria e recupero coda. **Prestazioni:** game-thread ms/s, lavoro mixer, underrun e cadenza sul medesimo percorso.

## M16 Dati KCL compatti e matematica nativa

**File:** reader/lookup KCL RMCP01 individuati dal profilo, guest memory helpers, native compiler per dati statici e test differenziali.

**Scope:** copie native in SoA dei campi letti dai test caldi; bounds conservativi con ordinamento dei candidati preservato. Math leaf native senza FMA approssimata implicita.

**Dipendenze:** hotspot confermato e invarianti KCL, non strutture MH3. **Fallback:** query originale su miss, modifica, NaN, thin triangle o input non certificato.

**Correttezza:** collisioni/bordi/rampe/oggetti, ghost e risultato ordine-dipendente; oracolo da funzione guest con passo originale. **Prestazioni:** ms query e cache misses, senza cambiare fisica.

## M17 Thread per lavoro indipendente e benchmark ripetibile

**File:** `vita_cpu_workers.*`, `vita_cpu_dispatch_plan.hpp`, `host_context.cpp`, g3d/effects native producer, installer/capture/frame-budget scripts.

**Scope:** chunk sufficienti, affinità per ruolo e trace dei conflitti prima di overlap guest. Contatori separati logica/draw CPU/consumer/GPU/present, input riproducibile con il passo originale. Non trasferire la simulazione variabile MH3.

**Dipendenze:** M02/M03 per dati condivisi, profilo per distribuire lavoro; CPU3 in esperimento distinto. **Fallback:** scheduling corrente, draw serializzato e guest scheduler originale.

**Correttezza:** range coperti una volta, callback, memory frees, distruttori, side effects e output del frame; thread GXM proprietario unico. **Prestazioni:** lane realmente occupate, join/dispatch meno costosi del lavoro tolto, tre run quiet con identità SELF/config/asset e immagini corrette. Nessuna promessa 60 FPS fino a prova di simulazione e presentazione.
