# WiiCompiled: piano scene GXM verso 5 per frame (11 ottobre 2026)

## Numeri verificati e distinzione importante

- `gxm_scenes_per_frame=5` e gia il parametro attivo di WiiCompiled in
  `aurora-main/platforms/vita/aurora_vita_backend.hpp`. Il parametro viene
  inoltrato a `SceGxmRenderTargetParams.scenesPerFrame` sia per il render
  target di display sia per ciascun target EFB offscreen.
- La cattura Vita3K del giorno 11/10 riporta **23 `nativeSceneCount` aggregati**
  in un frame; il report hardware dell'08/10 aveva osservato 29 e 40.
  Nessuno di questi numeri certifica quante scene colpiscano un *singolo*
  `SceGxmRenderTarget`. La sessione Vita3K corrente, con diagnostica gioco
  disabilitata, non esponeva ancora il conteggio nel suo render budget probe.
- Il run di Time Trial dell'11/10 vede circa 12 copie EFB per frame,
  delle quali 11 native in un campione. Il `worker_frame_us` mediano e circa
  751 ms e la cadenza ponderata nella fase di introduzione circa 1.225 Hz.
  Il tempo worker e wall-clock, non CPU GXM/GPU esclusiva.
- Obiettivo funzionale: **<= 5 aperture `sceGxmBeginScene` in un frame
  rappresentativo di gameplay**, con HUD, ombre, lightmap, effetto clear,
  readback e copia EFB corretti. Non ridurre artificiosamente il numero
  saltando scene necessarie: perderebbe la correttezza GX.

## Perche le scene si moltiplicano

In `gxm_renderer.cpp::copy_current_to_target`:

1. Il ramo `gpuFixup` (`scaled || flipX || flipY || RGB565`) chiama
   `bind_target(destination)`, che chiude la scena sorgente se aperta.
2. Un draw di copia GPU nel target offscreen apre una nuova scena GXM.
3. `bind_target(originalTarget)` chiude quella offscreen. Il successivo draw
   della scena principale ne riapre una: un solo GXCopyTex puo cosi creare
   circa due scene per copia, se tra copie ci sono draw sorgente.
4. Il ramo di trasferimento 1:1 chiude la scena sorgente e usa
   `sceGxmTransferCopy`; non aggiunge una scena offscreen con shader.
   `sceGxmTransferDownscale` e presente ma il ramo GPU fixup lo evita quando
   deve ridimensionare, per precedenti errori di transfer su Vita.
5. `GXCopyTex(clear=true)` e una vera dipendenza: la EFB deve essere copiata
   **prima** del clear e i draw successivi vedono i pixel azzerati. Non e
   corretto differire arbitrariamente, riordinare o eliminare tali copie.

## Intervento applicato: sola telemetria, niente semantica GPU modificata

Con `[vita] render_budget_probe=true`, il nuovo marker
`[wic-scene-budget]` viene prodotto insieme a `[wic-render-budget]` nei
primi quattro frame e ogni 32 frame:

```
[wic-scene-budget] frame=N scenes=S display=D offscreen=O max_per_target=P target_switch_ends=T transfer_source_ends=R shader_copies=F transfer_copies=X depth_load=L depth_store=Z depthless=Q budget=5
```

- `S = D + O` misura le scene aperte dal renderer in quel frame.
- `P` e il massimo delle aperture su **uno stesso render target fisico**
  nel frame. Le texture EFB tengono un contatore con stamp per-frame per
  evitare scansioni e allocazioni della cache sul percorso di draw.
- `T` conta chiusure per cambio target; `R` chiusure per transfer EFB.
- `F` e `X` distinguono copie shader dalle copie via transfer; `L/Z/Q`
  aiutano a separare il traffico depth dai casi color-only.
- La logica di visualizzazione del budget per WiiCompiled con diagnostica
  attiva usa ora `max_per_target`, non il totale aggregato. Gli altri
  consumatori Aurora mantengono il precedente comportamento.

La sonda non e ancora contenuta nel VPK gia in esecuzione. Non sono stati
fatti build, installazione, riavvii, modifiche all'INI o prove PS Vita.

## Roadmap per scendere davvero a 5 (A/B in ordine)

1. **Distinguere target display/offscreen:** se `P > 5`, isolare quante
   scene appartengono al display e quante ai singoli target EFB. Evitare di
   aumentare il parametro globale per nascondere il warning.
2. **Trasferimenti per le copie guest 1:1 con differenza di un pixel:**
   il run corrente mostra scratch copy `51x39 -> 51x38` e `52x39 -> 51x38`
   (sorgente logica 32x32). Una futura normalizzazione controllata del
   mapping GX potrebbe rendere queste copie transfer 1:1 e risparmiare
   una scena offscreen ciascuna. Validare i texel e i bordi prima di
   abilitarla, con flag WiiCompiled opt-in e rollback INI.
3. **Copy 2:1:** `152x114 -> 76x57` usa un fixup shader. Un esperimento
   transfer-engine deve accertare formato, allineamenti, orientamento,
   campionamento e dipendenze GPU; non riattivare incondizionatamente il
   downscale che in passato falliva sulle dimensioni Vita.
4. **Coalescing delle copie senza nuove scritture EFB:** solo se la EFB
   non ha ricevuto draw/clear dal precedente snapshot e nessun consumer
   osserva un risultato intermedio. Per `clear=true` vale sempre l'ordine
   esatto GX; tenere una fallback sincronizzata.
5. **Eliminare render-to-texture intermedi dalla pipeline materiale/ombra**
   solo dove un produttore nativo NW4r o una pass-equivalence dimostrata
   riproduce gli stessi risultati. Non sopprimere arbitrariamente le copie
   di ombre, lightmap e HUD per raggiungere 5 sul contatore.

Anche 5 scene/frame non risolvono da sole 1.2 FPS: il costo CPU di
preparazione GX/FIFO resta una priorita parallela.

## Validazione manuale futura

- Costruire manualmente un nuovo VPK completo (SELF incluso) e verificare
  che contenga `[wic-scene-budget]`. Non usare le prove correnti per
  attribuire alla nuova sonda numeri di scena non ancora misurati.
- Conservare SELF, PSARC, scena e clock identici; con logging Vita3K
  `log-uniforms=false`, `log-active-shaders=false`, raccogliere
  `wic-scene-budget` nei frame 256, 288, 320 ... di `stage=1` e `stage=2`.
- Controllare `S`, `P`, `T`, `F`, `X`, EFB, HUD, Mario, lightmap/ombre,
  e mediana frame/worker. Ripetere quiet con `render_budget_probe=false`
  per un confronto FPS non inquinato dall'instrumentazione.
- Nessuna build Vita automatica e nessuna modifica ai file di gioco.

## Prima ottimizzazione implementata: snap 1:1 opt-in (11 ottobre)

La pipeline e ora predisposta per la nuova opzione INI:

```toml
[vita]
efb_copy_snap_1to1 = false
```

Per avviare l'A/B sperimentale, portare soltanto questa opzione a `true`
su una build contenente la modifica. La vecchia build Vita3K resta immutata.
La connessione e `RuntimeConfigFile -> VitaRenderOptions -> apply_render_options
-> BackendConfig -> DrawSinkConfig -> DrawSink::copy_tex`, esclusivamente su
`MKW_TARGET_VITA && AURORA_VITA_RENDERER_GXM`. Gli altri giochi Aurora-Vita
non abilitano il percorso.

La funzione `efb_snap_one_to_one_capture` in `vita_efb_copy.hpp` accetta
soltanto rettangoli completamente interni all'EFB fisica, con dimensioni
guest sorgente/destinazione uguali, in cui il raster scissor fisico differisce
dal target per **zero o un pixel aggiuntivo su ciascun asse** (almeno uno
deve differire). Non supporta qui downscale 2:1, crop, rectangle parzialmente
fuori EFB, né formati depth; `DrawSink` la applica soltanto alle copie
colore `EfbCopyFormat::Passthrough`.

**Separazione critica del clear:** `captureSrc` e ritagliato solo per la
copia; `src` conserva la superficie originale per
`renderer_->clear_current_region(src,...)`. La sequenza
`flush -> capture -> clear` e invariata. Se la cattura ritagliata fallisce,
il codice tenta nuovamente il rettangolo originario e il percorso shader/CPU.

Le differenze di un pixel possono cambiare la filtratura: il percorso shader
scalato usa campionamento *lineare*, il transfer 1:1 usa copia pixel. La
correttezza bit-per-bit delle immagini **non e dimostrata**: per questo
la flag resta OFF e richiede uno screenshot A/B del frame presentato (non
solo il readback PPM noto come difettoso nel report correctness).

Nuovi indicatori nel probe `[wic-draw-health]`:
`efb_copy_snaps=N` e `efb_snap_retries=N`. La frequenza di
`[wic-scene-budget] offscreen`, `shader_copies`, `transfer_copies`,
`scenes` e `max_per_target` dice se il transfer nativo e stato
effettivamente usato. La sola presenza di `efb_copy_snaps>0` non dimostra
il guadagno: `capture_current` potrebbe avere applicato un fallback CPU.
Al boot il marker `[wic-efb-snap-config] enabled=0|1 probe=0|1`
conferma i flag effettivamente applicati (se probe o feature e abilitata).

In condizioni ideali si evita **una scena offscreen per ciascuna copia
ricondotta a transfer**; l'apertura successiva della scena sorgente resta
necessaria. Passare da 23 a 5 non e ottenibile attribuendo a questo
intervento un guadagno non misurato. La maggior parte degli EFB boundary
resta da eliminare a livello di produzione o da dimostrare accorpabile.

### Verifica automatica eseguita, senza build Vita

- Test host `runtime_config_tests.cpp`: PASS, opzione tipizzata e opt-in.
- Test host `vita/tests/render_config_test.cpp`: PASS, propagazione e default.
- Test host `aurora-main/tests/vita_efb_empty_source_test.cpp`: PASS, inclusi
  i rettangoli reali `51x39 -> 51x38` e `52x39 -> 51x38`, con clear intatto
  e rigetto di rettangoli clipped, delta > 1 e vera riduzione 2:1.
- VitaSDK `arm-vita-eabi-g++ -fsyntax-only`: OK su
  `aurora_vita_draw_sink.cpp` e `aurora_vita_backend.cpp`.
- Nessun VPK/SELF prodotto, nessun file Vita3K modificato, nessun test GPU
  o visivo sulla nuova build effettuato.

### A/B da eseguire sulla prossima build manuale

Usare il checkout attuale e, se la preparazione dei dati e gia valida,
il comando **manuale** seguente (non eseguito dall'assistente):

```sh
cd /Users/robin994/Documents/Code/PSVita/wiicompiled-vita
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON -DWIC_PRECOMPILED_ASSETS=ON \
  -DWIC_RESIDENT_GEOMETRY=ON -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=OFF -DWIC_NATIVE_DIRECT_STREAM=OFF
```

1. Chiudere ordinatamente la sessione precedente, installare il nuovo SELF
   `WICMKW001` su Vita3K e mantenere PSARC e scene identici.
2. Primo run: `efb_copy_snap_1to1=false`,
   `render_budget_probe=true`, `[diagnostics] enabled=false`;
   ridurre solo il log Vita3K (uniform/shader dump OFF).
3. Secondo run: stessa build, `efb_copy_snap_1to1=true`.
4. Confrontare le medesime finestre di `stage=1` e `stage=2`:
   `offscreen`, `max_per_target`, `efb_copy_snaps`,
   `shader_copies`, `transfer_copies`, `runtime-cadence`.
5. Verificare pixel di lightmap, ombre, Mario, HUD, viewport, clear, audio
   e assenza di nuovi blocchi. Per disabilitare basta riportare l'opzione
   a `false` e riavviare il gioco; non serve cambiare SELF o PSARC.
