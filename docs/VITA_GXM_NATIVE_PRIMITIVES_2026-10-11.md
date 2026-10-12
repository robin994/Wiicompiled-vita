# GX Native Primitive — Aurora-Vita / WiiCompiled (fase 1)
Data: 2026-10-11. Implementazione sorgente, senza build né installazione PS Vita.

## Obiettivo e supporto hardware

Gli header VitaSDK `/usr/local/vitasdk/arm-vita-eabi/include/psp2/gxm.h` confermano:
`SCE_GXM_PRIMITIVE_TRIANGLES`, `TRIANGLE_STRIP`, `TRIANGLE_FAN`,
`LINES`, `POINTS`. Il backend GXM in Aurora gestiva già triangoli,
strip e fan a livello di chiamata `sceGxmDraw`, ma il frontend GX convertiva
di norma ogni strip/fan in una lista di triangoli e allocava indici per ogni draw.

Questa prima fase abilita **solo** l'emissione diretta di
`GX_TRIANGLESTRIP -> SCE_GXM_PRIMITIVE_TRIANGLE_STRIP` e
`GX_TRIANGLEFAN -> SCE_GXM_PRIMITIVE_TRIANGLE_FAN` nei percorsi
*streamed CPU* e *CPU fallback* del frontend. Se il port non opta per
l'esperimento, il precedente comportamento non cambia.

- `GX_TRIANGLES`: già supportato direttamente.
- `GX_QUADS`: conserva l'espansione in 2 triangoli per quad.
- `GX_LINES`, `GX_LINESTRIP`, `GX_POINTS`: rimangono nella traduzione
  a sprite/quad, perché lo spessore delle linee, la dimensione del punto e le
  UV GX non coincidono con le primitive hardware raw della Vita.
- Le primitive inferiori alla soglia rimangono triangoli, per non perdere il
  batching dei numerosi draw piccoli. Due strip/fan indipendenti **non**
  vengono concatenate senza primitive restart. Ordine, winding pari/dispari,
  materiali, scissor, matrici, EFB e lifetime dell'input rimangono invariati.
- `StaticGeometryCache` e le geometrie NW4r/PSARC precaricate mantengono
  il percorso e il formato precedenti: non sono ancora state convertite le
  mesh residenti a un formato nativo completo. Il parametro non forza
  conversioni in presenza di geometry cache hit o di streaming GPU fixed.

## Ottimizzazione: identity index buffer condiviso

Quando l'opzione è attiva sul renderer GXM si tenta di allocare una
sola volta un buffer GPU **immutabile** di 64.000 indici U16
(128.000 byte). Contiene la sequenza `0,1,...,63999` e può essere
condiviso fra draw strip/fan di qualunque lunghezza fino a 64.000 vertici.
Per questi draw:
- il frontend continua a decodificare/trasformare e caricare i vertici
  nell'arena, usando lo stesso stato GX;
- non genera `(N-2)*3` indici triangolari;
- non genera **né carica** N indici sequenziali a ogni draw;
- usa il buffer condiviso con `indexCount=N`, preservando il culling nativo.
- se la buffer allocation iniziale fallisce, utilizza la sequenza locale
  nell'arena: fallback deterministico senza OOM fatal per questo indice.
- il buffer viene distrutto dalla stessa istanza `DrawSink` solo dopo
  l'arresto del consumer GX; il renderer applica la propria protezione
  in-flight ai buffer GXM.

Se la diagnostica split-phase usa `PreparedDraw` anziché lo streamed path,
il frontend produce gli indici sequenziali e il tipo primitiva corretti.
Questo percorso non tenta di fondere due strip/fan.

## Configurazione e rollback

La feature è opt-in e **spenta di default** in tutti i port Aurora:

```toml
[vita]
native_primitive_min_vertices = 0
```

Per un primo test conservativo, impostare **128**:

```toml
[vita]
native_primitive_min_vertices = 128
```

Valori ammessi: 0 (OFF), da 3 a 64000. Input in altre sezioni, stringhe,
valori fuori intervallo e negativi non attivano il percorso. Il parametro si
applica all'avvio; riavviare completamente il gioco tra gli A/B.
Per provare più aggressivamente usare 64; non usare inizialmente 3-8,
perché la perdita di batch GXM potrebbe peggiorare gli FPS.

Con `render_budget_probe=true` il log periodico `[wic-draw-health]`
aggiunge `native_primitives=<draw>` e `shared_index_draws=<draw>`.
Sulla Vita, confrontare allo **stesso stage e stessi clock**:
- FPS/present_hz e durata FIFO del worker;
- draw nativi GXM e draw mergeati;
- numero di primitive native e numero di volte che viene riutilizzato
  l'IBO condiviso;
- UI/HUD, modelli bianchi, texture/UV e render target, per evitare
  regressioni visive.

Per il rollback basta riportare il parametro a 0 **con lo stesso SELF**.
Nessuna modifica dell'ISO/PSARC e nessun nuovo formato dati necessario.

## Codice

Aurora submodule:
- `platforms/vita/gfx/vita_draw_adapter.hpp/.cpp`: opzione topologia,
  footprint, indici, preparazione CPU/streamed, binding indice condiviso.
- `platforms/vita/gx/aurora_vita_draw_sink.hpp/.cpp`: selezione sopra soglia
  solo se GPU geometry/streamed GPU non hanno già gestito il draw, proprietà
  del buffer U16, metriche, fallback CPU e lifetime.
- `platforms/vita/aurora_vita_backend.hpp/.cpp`: propagation opt-in.
- `tests/vita_submission_test.cpp`: winding completo vs vecchia espansione,
  footprint, cache di indici condivisa senza index arena writes, fallback,
  divieto di merging di strip/fan anche nel CPU fallback.

WiiCompiled parent:
- `runtime/include/vita_render_options.h`,
  `runtime/include/runtime_config.h`: parametro e primo INI.
- `vita/render_config.hpp`, `vita/backend.cpp`: forwarding e startup log.
- `runtime/tests/runtime_config_tests.cpp`,
  `vita/tests/render_config_test.cpp`: opt-in e limiti.

## Validazione

- Aurora host: `cmake --build build/aurora-host -j 6` +
  `ctest --test-dir build/aurora-host --output-on-failure -j 6`:
  **27/27 test superati**, inclusi primitive/winding/batching.
- WiiCompiled host: `render-config-tests` e `runtime-config-tests`
  compilati; tutti i 30 test di `ctest --test-dir build/host` superati.
- GXM VitaSDK: `arm-vita-eabi-g++ -fsyntax-only` sui sorgenti modificati
  con i flag reali dal checkout `build/vita-psarc`: verifica sintattica,
  **non** una build eseguibile.
- Non è ancora stato misurato un guadagno di FPS Vita o Vita3K del nuovo
  codice; i vecchi TRACE eseguivano il SELF M02.3 precedente. Un'analisi
  su Vita3K dell'hardware GXM non è sufficiente senza A/B sulla PS Vita reale.

## Build Vita manuale (NON eseguita da Codex)

```sh
cd /Users/robin994/Documents/Code/PSVita/wiicompiled-vita
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON \
  -DWIC_PRECOMPILED_ASSETS=ON \
  -DWIC_RESIDENT_GEOMETRY=ON \
  -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=OFF \
  -DWIC_NATIVE_DIRECT_STREAM=OFF
```

Dopo la compilazione, installare il nuovo SELF. La sequenza di prova
sull'**identico SELF** è `native_primitive_min_vertices=0`, poi 128
e solo in assenza di regressioni 64. Non interpretare una riduzione
degli indici come un risparmio equivalente sui 400+ ms/frame di GX FIFO:
rimangono parsing, stati GX e vertex transform per ogni primitiva.

Nessun commit/push, nessun auto-build/deploy.
