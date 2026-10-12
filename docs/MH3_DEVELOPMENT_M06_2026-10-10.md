# MH3 M06.1: record di geometria GXM immutabili

10 ottobre 2026. Dopo l'autorizzazione allo sviluppo è implementato il primo sotto-ticket M06 nei checkout Aurora embedded e standalone. Lo scopo è riusare preparazione e binding della geometria residente. M06.2, stati vertex/fragment precomputati e uniformi per view, rimane aperto; questa modifica non lo dichiara completato.

Il [bundle di analisi](MH3_REUSE_2026-10-10/README.md) conserva identità e risultati della fase precedente. I suoi hash dei sorgenti descrivono quel momento e non il nuovo albero di lavoro. L'identità delle modifiche correnti e i nuovi risultati sono in [MH3_M06_EVIDENCE_2026-10-10.json](MH3_M06_EVIDENCE_2026-10-10.json).

## Comportamento implementato

Aurora può usare `SceGxmPrecomputedDraw` per draw con vertici e indici residenti. La chiave contiene pipeline attiva, programma vertex, handle dei due buffer, base dei vertici, offset degli indici, numero di indici e tipo di primitiva. Shader, uniformi, texture, blend/depth/cull, viewport e scissor continuano a essere impostati per il draw con il percorso context corrente.

Un record pubblicato e il suo backing sono immutabili. Un hit può quindi essere usato di nuovo mentre un draw precedente è ancora pendente. Soltanto l'eviction o la re-inizializzazione richiede il completamento dell'ultima scena che lo ha usato. È un adattamento più circoscritto rispetto alle copie per slot degli stati shader di [MH3 precomputed draws](https://github.com/Grarak/mh3-wii/blob/04092f4e9fe3906212eb27281c66ee72236a91b6/docs/optimizations/rendering/precomputed-draws.md). Nomi e tipi delle chiamate sono verificati con l'[API GXM VitaSDK](https://docs.vitasdk.org/group__SceGxmUser.html) e l'header della SDK installata.

Quando l'opzione è attiva, ogni `EndScene` pubblica una notifica fragment nel primo word della regione GXM, riservato a questo renderer. Il renderer è l'unico proprietario del context e della notifica. Il contatore GPU a 32 bit viene esteso alla sequenza CPU a 64 bit; i lease non sono ricavati dal numero di frame presentati. Più scene EFB nello stesso frame restano distinte.

La cache ha al massimo 512 record, con un backing GPU-mapped di 256 KiB allocato soltanto al primo miss supportato, contabilizzato nel budget risorse. Le strutture e l'indice dei record occupano anche heap CPU ordinario; vengono creati solo con opzione attiva. Non aumentano il pool CDRAM configurato. Uno stato GXM che supera i 512 byte riservati al singolo slot conserva il percorso ordinario.

## Fallback e transizioni

- Opzione assente/false: percorso ordinario e nessuna cache o notifica aggiuntiva.
- Geometria dinamica o utility pass corrente: percorso ordinario. Nel checkout embedded il requisito `gameDraw` esclude esplicitamente i pass di clear/copy/present; nel standalone i loro buffer correnti non sono marcati residenti.
- Nessuno slot ritirato: draw ordinario, senza finish o wait per liberare spazio.
- Budget, allocazione o inizializzazione GXM falliti: esperimento disattivato per la sessione e fallback prima della submission. Un formato troppo grande ricade sul percorso ordinario per quel draw.
- Errore di `DrawPrecomputed`: stessa gestione di errore della submission ordinaria, nessun retry che possa duplicare un draw già consumato, esperimento disattivato.
- Dopo un draw precomputato la cache del vertex stream context viene invalidata: il successivo draw ordinario ripristina il proprio stream.
- Ogni `BeginScene` ristabilisce completamente il contesto e disattiva gli eventuali stati shader precomputati. Le prenotazioni uniformi e lo scissor parziale restano nel percorso corrente.
- Aggiornamento/distruzione buffer e rilascio del vertex program rimuovono le chiavi corrispondenti. Una chiave invalidata non rende riutilizzabile un payload ancora letto dalla GPU.
- Il teardown libera backing e record dopo il vero `sceGxmFinish`. Le risorse sono marcate in uso prima di entrambe le API draw, così anche una submission fallita conserva il fence necessario alla loro distruzione.

La notifica non sostituisce i fence già presenti per le altre risorse. M04, eliminazione delle attese dovute a distruzione/EFB, resta aperto. Non vengono riordinati draw, copie o callback.

## Configurazione e osservabilità

WiiCompiled espone un booleano in `Config.toml`:

```toml
[vita]
precomputed_geometry = false
```

`true` seleziona la candidata separata. Default e valori di tipo errato conservano `false`. La configurazione attraversa `VitaRenderOptions`, `BackendConfig`, `RendererConfig` e `gxm::Config`; nessuna override guest o rigenerazione del traduttore è richiesta.

Aurora standalone espone `BackendConfig::gxm_precomputed_geometry`; il probe può usare direttamente `gxm::Config::precomputedGeometry`. La differenza di API draw tra i due checkout è conservata, senza copiare interi renderer.

WiiCompiled stampa `[precomputed-geometry-config] enabled=...` all'avvio. Con `render_budget_probe=true`, `[wic-precomputed-geometry]` campiona i contatori del frame ogni 32 frame: `draws`, `hits`, `misses`, `fallbacks`. I quattro contatori sono anche nella telemetria Aurora `NATIVE_STATE`. Un flag richiesto non dimostra copertura: occorrono draw effettivamente precomputati e hit dopo warmup. Le inizializzazioni, l'indice della cache e il ramo di selezione hanno un costo CPU da misurare.

## Verifiche eseguite

| Prova | Risultato | Ambito |
| --- | --- | --- |
| WiiCompiled host | 30/30 PASS | Include parser del nuovo booleano e propagazione al backend, oltre ai test precedenti |
| Aurora embedded host | 25/25 PASS | Suite attuale e nuovo test di cache/lifetime |
| Aurora standalone host | 19/19 PASS | Suite della sua revisione, con il medesimo nuovo test |
| AddressSanitizer + UndefinedBehaviorSanitizer | PASS | Test cache, incluso stress da 100.000 scene |
| VitaSDK `-fsyntax-only` | PASS, entrambi i checkout | `gxm_renderer.cpp`, `gxm_facade.cpp`, `aurora_vita_backend.cpp`, con macro del frontend supportato |
| Integrità del diff | PASS | Entrambi i checkout e WiiCompiled; helper/test condivisi identici |

Il test usa la classe di cache e la conversione dei serial realmente inclusi nel renderer. Verifica hit immutabili ancora pendenti, più scene, key/range diversi, invalidazione pendente, pressione a capacità due/otto, osservazioni vecchie/future, rollover a 32 bit e un oracle separato dei payload di GPU simulate. Non sostituisce GXM e non prova pixel sul dispositivo.

Il primo controllo standalone senza `MKW_TARGET_VITA` ha incontrato una dipendenza FIFO preesistente. Il controllo finale usa le macro che `aurora_vita_frontend.cmake` pubblica per il frontend GX supportato. Nessuna modifica estranea è stata fatta per supportare quella combinazione.

Log completi, ignorati da Git: `build/mh3-m06-host-*.log`, `build/mh3-m06-aurora-*.log`, `build/mh3-m06-standalone-*.log`, `build/mh3-m06-sanitizers.log`, `build/mh3-m06-*-sdk-syntax.log`. Il controllo SDK non genera object, ELF, SELF o VPK. Non è stata eseguita una build Vita o un'installazione e non sono stati modificati asset, salvataggi o config sul dispositivo.

## Confronto hardware necessario

La build manuale parte dal [runbook P0 corrente](VITA_P0_NEXT_BUILD_2026-10-10.md), ricompilando tutti i target toccati dai nuovi header Aurora. Prima chiudere il gate menu/DMA/input e confermare Time Trial in stage 2 con `race_frames` crescente e HUD visibile.

Confrontare lo stesso SELF e gli stessi asset con `precomputed_geometry=false`, poi `true`, quindi tornare a `false`. Il bootstrap rilegge il TOML: riavviare l'app a ogni variante. Conservare hash SELF/config e marker di avvio. Lasciare OFF l'esperimento dispatch CPU e gli altri esperimenti nuovi durante questo confronto.

Una sessione diagnostica identifica copertura, errori GXM e pressione della cache. Per FPS usare tre run quiet per variante, stessa pista/personaggio/camera/clocks, medesimo warmup, probe OFF. Verificare passaggi menu→gara→menu, Mii/light map, materiali, HUD, audio/input, suspend/resume e chiusura. Zero draw precomputati indica una prova senza copertura di questo intervento, non un risultato prestazionale negativo.

Il rollback del solo esperimento è `precomputed_geometry=false` e riavvio. In caso di errore conservare log/coredump e binario corrispondente. Il gate visuale/FPS è aperto: la modifica non viene presentata come correzione del bianco/scuro o come raggiungimento di 60 FPS.

## Sviluppo successivo

M06.2 richiede copie distinte degli stati shader per ogni uso ancora pendente, binding delle uniformi con lifecycle verificato e fallback context esplicito. M02 rimane il lavoro architetturale principale: producer nativi per famiglie NW4r, che eliminino la produzione GX prima della submission. I nuovi serial di scena non autorizzano da soli quel bypass o la rimozione dei finish EFB.
