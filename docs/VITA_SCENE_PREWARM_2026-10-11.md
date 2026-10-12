# WiiCompiled: prewarm per scena — fase 1 (2026-10-11)

## Scopo e vincoli

Lo shader/program GXM cache preload e il **prewarm dei materiali/pipeline**
sono **già attivi** in Aurora-Vita. L'ultimo TRACE ha registrato
`pipeline_manifest loaded=512`, `pipeline_prewarm warmed=192` e la
ricerca nel GXP binary cache `gxm-cg-gxp-v1`. Questa implementazione
non finge di ricompilare quei materiali: colma il gap di prefetch dei
**sidecar nativi texture e geometria** usati dal gioco.

Il catalogo PSARC corrente ha 5.867 texture e 1.343 mesh; non e sicuro
caricarli tutti nella RAM/CDRAM Vita. Il prewarm nuovo opera
**esclusivamente sulla cache CPU LRU** già esistente, con un budget
massimo prelevato da essa. Non crea handle o texture GXM su un thread
diverso dall'owner e non sostituisce GX/TEV o la geometria dinamica.

## Opzione INI, comportamento normale e rollback

```toml
[vita]
shader_binary_preload = true
shader_preload_limit = 1024
pipeline_prewarm_limit = 192
native_asset_cache_mb = 2
native_asset_prewarm_mb = 1
```

`native_asset_prewarm_mb` ammette **0–8 MB**, e viene poi clampato
a `native_asset_cache_mb` e a un limite interno di 8MB; **0 è il default**
e disabilita manifest, access tracking e I/O di prewarm. Nel caso suggerito
(`native_asset_cache_mb=2`, `native_asset_prewarm_mb=1`) il prefetch
occupa al massimo 1 MiB nella cache preesistente da 2 MiB, **non 1 MiB
aggiuntivo**. Non aumenta automaticamente lo spazio CDRAM.

Per effettuare rollback su uno stesso SELF impostare `native_asset_prewarm_mb = 0`
e riavviare completamente il titolo. Non occorrono modifiche al PSARC.

## Sequenza

1. `game_main.cpp` monta il PSARC e verifica il catalogo. `native_assets::Initialize`
   carica `native/v1/index.bin`, calcola l'impronta FNV-1a dell'intero
   catalogo e prepara i contatori. Non effettua prefetch prima del
   renderer (per evitare che il preload GXP espella i sidecar CPU).
2. `backend::initialize` avvia Aurora-Vita e conserva il comportamento
   esistente: preload binari GXP e 192 pipeline/materiali più frequenti.
3. `native_assets::WarmBoot` carica in modo sincronizzato dalla precedente
   sessione i sidecar CPU hot del bootstrap/menu fino al budget, **prima
   dell'avvio guest**. Non invoca alcuna API GXM.
4. Al confine di un frame presentato, ogni otto frame,
   `NativeAssetSceneTick` controlla in sola lettura l'identificativo
   di sezione/scena; per il test PAL time-trial (`0x1f`) include
   circuito, pilota e kart nella chiave. A una transizione salva il
   precedente manifest e prelegge il manifest della nuova scena.
   La chiamata resta sul thread che coordina la presentazione, non
   sui worker GXM. Un cambiamento di scena può causare un breve stallo
   iniziale per il prefetch CPU, misurato nel log.
5. Ogni 128 frame viene eseguito, solo con prewarm attivo,
   un checkpoint best-effort del manifest (massimo 128 record) così
   la registrazione non dipende esclusivamente dall'uscita pulita.
6. I dati preletti non sono forzati a restare residenti per sempre:
   il normale criterio LRU prevale. Le letture reali aggiornano la
   lista delle risorse hot; i prefetch non si registrano come accessi.

Il primo avvio con prewarm attivo è una **fase di apprendimento**:
un manifest per una scena non ancora visitata non esiste. Al secondo
avvio/visita gli asset hot possono essere già nella cache CPU.

Il manifest è `ux0:data/wiicompiled-vita/native_hot_<scene>.warm`, in
formato `WICWARM1`, max 128 record. Include identificatore
scena, versione, impronta dell'intero catalogo PSARC e checksum.
In lettura vengono inoltre verificati tipo, hash, size e presenza di
ogni record nel catalogo attuale. File mancanti, troncati, obsoleti,
duplicati o corrotti non impediscono il boot e non sostituiscono
il percorso di caricamento originale.

## Integrazione

- `vita/native_prewarm_plan.hpp`: ranking hot record, budget
  conservativo, serializzazione e validazione per file bounded.
- `vita/native_scene_prewarm.hpp`: identità scena stabile e versionata.
- `vita/native_assets.cpp/.hpp`: contatori, manifest I/O, prefetch
  da PSARC, MRU nel cache limitato e fallback.
- `vita/game_main.cpp`: mount PSARC > init catalogue > Aurora shader/pipeline
  warmup > CPU native asset warmup > guest; teardown in ordine di dipendenze.
- `vita/game_aurora.cpp`: tick scene e checkpoint a una frequenza bassa.
- `runtime/include/vita_render_options.h` e
  `runtime/include/runtime_config.h`: opzione opt-in, default 0.
- `vita/tests/native_prewarm_plan_test.cpp` e
  `vita/tests/native_assets_test.cpp`: test unitari e integrazione PSARC.

## Contatori diagnostici

```text
[asset-prewarm] phase=initialized budget=... catalogue=...
[asset-prewarm] phase=boot manifest=0/1 selected=... loaded=... existing=... bytes=... elapsed_us=...
[asset-prewarm] phase=scene frame=... scene=... manifest=0/1 selected=... loaded=... existing=... bytes=... elapsed_us=...
[asset-prewarm] phase=checkpoint scene=... entries=... bytes=...
```

Il programma registra manifest anche durante la prima sessione; il
prewarm effettivo ha `manifest=1` soltanto quando dispone di dati
dalla precedente visita. `selected` è la previsione, `loaded`
i record effettivamente letti dal PSARC, `existing` gli asset già
presenti in cache. Se `loaded=0` non attribuire eventuali scarti FPS
al prefetch.

## Validazioni eseguite

- Compilazione host `native-prewarm-plan-tests`, `native-assets-tests` e
  `runtime-config-tests` riuscita.
- **31/31 test host superati** (inclusi checksum/manifest
  stale/budget, hit PSARC senza seconda lettura, sceneKey, primo
  warm boot, impostazioni INI e fallback su manifest corrotto).
- Cross-compiler VitaSDK verificato con `-fsyntax-only`
  sugli elementi del runtime aggiornati (non genera SELF o VPK).
- Non è stata effettuata una build PS Vita, un'installazione,
  né un nuovo A/B su Vita3K. Non sono dichiarati nuovi FPS.

## Protocollo A/B per una build futura

Su Vita3K usare lo stesso SELF, lo stesso PSARC, cache shader già calda
e lo stesso circuito/personaggio. Prima eseguire due run con
`native_asset_prewarm_mb=1` (il primo registra gli asset, il secondo
esercita il prewarm), poi una baseline con `native_asset_prewarm_mb=0`.
Confrontare avvio, transizione, frame stage0/stage2, FPS, numero
di misses PSARC durante i frame critici, dimensione/eviction cache,
`[asset-prewarm]`, `[resident-assets]`, `[native-assets]`.
Scartare confronti se `manifest=0`, se scena/carattere non coincidono,
o se varia il compilatore GXP/il livello TRACE.
Solo dopo un miglioramento ripetibile e senza regressioni visive
replicare sulla Vita reale.

## Limiti aperti / prossima fase

Questo intervento **non** pre-carica direttamente texture GPU,
mipmap, texture EFB dinamiche, pose animate né tutti i materiali TEV.
Per raggiungere il prewarm GPU per-scena servono un manifest di
`TextureDesc`/binding sicuri con sorgente verificata e barriere di
lifetime, e l'esecuzione in FIFO sul solo GX consumer proprietario
del GXM. Non ricostruire un `TextureDesc` da un hash senza i dati
GX reali; non bloccare in massa la prima gara o aumentare CDRAM
senza telemetria.

Le texture bianche di Mario sono **ancora un difetto di correttezza**
non spiegato da prove di caricamento tardivo; non è stata apportata
alcuna patch speculativa a UV/TEV/decoder. Il readback GXM nero
rimane un difetto diagnostico separato.
