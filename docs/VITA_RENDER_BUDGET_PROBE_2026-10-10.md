# WiiCompiled Vita: bottleneck dopo A/B batching e sonda budget GXM

10 ottobre 2026. Repo WiiCompiled + submodule Aurora-Vita. Nuova sonda **solo diagnostica**, default OFF e limitata a `MKW_TARGET_VITA` / GXM.

## Verifica hardware del test fallito

Scaricati read-only via VitaShell FTP in `build/diagnostics-3d-batching-failed-20261010/` log e `Config.toml`. Il SELF installato e quello compilato localmente coincidono:

- SHA-256: `2f94e7534f9192889fabe6c0d10b0cbb50cd48ccc669808ef3b8f64905e222e2`.
- Aurora revision `a4f93c30cabfa48cc56368eebee727110e26ab8a`.
- `[native-render-config] draw_batching=1 direct_submit=0 direct_stream=0`; `[gx-worker] requested=1 running=1`.
- In Time Trial, gli ultimi report della sessione arrivano a `frame=314`, `stage=0`, `pans_finished=0`, `race_frames=0`. La gara vera deve ancora cominciare. Circa 1,1–1,4 frame presentati/secondo. I dati non dimostrano un HUD mancante in `stage=2`.
- **Causa della mancata telemetria batching:** `[diagnostics].enabled=false` chiude il gate `runtime_diagnostics_enabled()`, che disattiva `AURORA_VITA_LOG_INFO`. Gli eventi `[wic-draw-health]` erano percio *tutti* silenziati; il successo di `draw_batching=1` non significa che sia stato fuso anche un singolo draw.

## Attribuzione storica: evitare conclusioni errate

Cattura **differente** `build/time-trial-freeze-20261008/capture-20261008T201504Z/current-session.log`, frame diagnostico 299 (prima dell'ultimo batching):

- `total_us=3572637`, `draws=6260`, `draw_frontend_us=357923`, `vertex_decode_us=146624`.
- `efb_copy_us=956628`, *include* il flush di draw prima di `GXCopyTex`.
- `submit_us=857197`, *in parte incluso* in `efb_copy_us`.
- `submit_draw_us=800038`, `submit_pipeline_us=23922`, `submit_texture_us=7775`, `display_queue_us=29`.
- `dl_copy_us=344952`; `dl_pinned_calls=0`; `game_wait_slot_us=1182496` (attesa produttore).
- Il `frame-300-draws.csv` contiene `submit_cpu_us` per un'altra finestra/campionamento con somma 56.347 us: **NON** descrive tutti i tempi GXM del frame 299. Non fare somme fra campi annidati, ne inferire che 56 ms sia un limite per tutti i draw.
- `fallback_tex=0` in quel frame diagnostico: il bianco visibile puo essere un problema di materiale, GX lighting, copie EFB, o altre fonti. Non introdurre workaround cromatici prima di una prova su frame con immagine.

La diagnostica completa (traccia, coverage, lettura stato e timing per draw) **rallenta significativamente** quel frame storico; il tempo quiet piu recente e intorno a 0,8–1,0 s/frame. I valori storici sono indizi di costo, non breakdown definitivo del frame attuale. Il batching non puo eliminare gli overhead di render target, EFB o GPU.

## Intervento: profilo campionato senza full diagnostics

Opzione di sola diagnosi nel port `[vita] render_budget_probe=true`, default **false**.

- Conserva `[diagnostics] enabled=false`: niente draw trace, coverage per-draw, immagini/screenshot o stdio per ogni draw.
- Il renderer GXM raccoglie tempi per **un frame ogni 32**, senza alterare il percorso dei frame restanti. `[wic-render-budget]` da worker: `worker_frame_us`, `native_draws`, `sampled`, `pipeline_us`, `texture_us`, `draw_us`, `display_queue_us`, `native_efb`, `native_efb_scene_us`, `native_efb_transfer_us`, `native_finish`.
- `DrawSink` scrive ogni 32 frame il record `[wic-draw-health]`: richiesta draw, `merged`, fallback bianco/dynamic EFB, `efb_copies`, `efb_flush_us`, `efb_capture_us`, `efb_clear_us`.
- `[wic-texture-fallback]` viene ora scritto anche con diagnostic quiet **solo quando probe=true**, nei primi 12 eventi e poi a conteggio potenza di due.
- Soprattutto, `efb_flush_us` e i tempi GXM possono **sovrapporsi**; non fare somme ingenue. La sonda campionata comporta syscall extra nel trentaduesimo frame: **non** usarlo nei benchmark quiet standard.
- Gli altri Aurora consumer rimangono sulla configurazione originale; nessun cambiamento a render order, lifetime, shader, texture, GX FIFO o thread affinity.

## Prossima build manuale

Da Mac (non e stata eseguita alcuna build Vita dall'assistente):

```sh
cd /Users/robin994/Documents/Code/PSVita/wiicompiled-vita
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON -DWIC_PRECOMPILED_ASSETS=ON \
  -DWIC_RESIDENT_GEOMETRY=ON -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=OFF -DWIC_NATIVE_DIRECT_STREAM=OFF
python3 scripts/install_vita_current.py --check-local
```

Chiudere WiiCompiled, avviare FTP VitaShell sulla Vita e installare con l'**identico PSARC**:

```sh
python3 scripts/install_vita_current.py --time-trial --diagnostics quiet \
  --audio-policy continue --render-batching on --budget-probe on
```

Terminare VitaShell e avviare Time Trial dalla LiveArea. Lasciare correre abbastanza per attraversare frame 32/64/96 e, per il problema HUD, almeno finche `[race-progress] stage=2`. Chiudere WiiCompiled e avviare di nuovo VitaShell FTP, poi:

```sh
python3 scripts/capture_vita_current.py --label race-render-budget --tail-mb 8
python3 scripts/analyze_vita_render_budget.py \
  "$(ls -dt build/vita-psarc/device-runs/race-render-budget-*/runtime-session.log | head -n 1)" --last 16
```

Il riepilogo unisce i report `frame=N` e `completed_frame=N` dello **stesso frame**, assegna lo `stage` noto dall'ultimo `[race-progress]` e mostra se il budget e consumato dal frontend, da emissioni GXM, copie EFB, GPU queue o attese. Non stima il tempo GPU puro dai soli tempi CPU.

Per ripristinare il vero benchmark quiet **senza reinstallare SELF**:

```sh
python3 scripts/install_vita_current.py --probe-only --budget-probe off
```

Eseguire identico Time Trial con batching ON/OFF; gli FPS quiet sono confrontabili solo se effettivamente raggiungono `stage=2` e la stessa scena. Il prossimo intervento su oggetti/materiali nativi sara scelto dai numeri, evitando di aggirare le conversioni dinamiche non equivalenti.

## Test

Gli unit test coprono parser `render_budget_probe`, applicazione al `BackendConfig`, idempotenza A/B installer in FTP simulato, analizzatore e le regole di clipping/memoria/submission gia presenti. Host e sintassi ARM vengono verificati separatamente; `arm-vita-eabi-g++ -fsyntax-only` **non produce eboot** e non e un test FPS.

## Rettifica dopo il test hardware

La sonda dell'implementazione Aurora `1cf5bca` e stata provata sulla console
il 10/10: **misurava effettivamente i merge** (oltre 5.000 draw/frame) ma
**non** i tempi in microsecondi, perche `diagnostic_now_us`,
`telemetry_now_us` e `now_us` si disattivano con `diagnostics=false`.
Gli zeri di quei log **non sono tempi validi**. La correzione dei clock,
il profiling delle cause dei `sceGxmFinish` e l'A/B visivo dei materiali sono
documentati in
[VITA_1FPS_DARK_WHITE_FOLLOWUP_2026-10-10.md](VITA_1FPS_DARK_WHITE_FOLLOWUP_2026-10-10.md).
