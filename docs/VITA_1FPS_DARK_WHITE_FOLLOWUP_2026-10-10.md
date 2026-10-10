# WiiCompiled Vita, 10 ottobre 2026: 1 FPS, scena scura, modelli bianchi

## Evidenza: ultima esecuzione reale

Sessione acquisita **in sola lettura** dalla PS Vita (192.168.1.79:1337):

`build/vita-psarc/device-runs/darkwhite-1fps-20261010-20261010T131915Z/`.

- SELF installato == SELF compilato in locale: SHA-256 `f81f6ae6ce5f3aba2eeecaa3935ed37c1377eeecc694581b7d4e6c320f35a8ce`; Aurora `1cf5bcac011498f1f4c2bf5f707448d7e6e088b5`.
- `draw_batching=1`, GX worker asincrono avviato, `direct_submit=0`, `direct_stream=0`. Stesso PSARC da 3.219.564.087 byte.
- Tra frame 224 e 320: da 6.154 a 6.299 draw richiesti, **5.367-5.748 merge**; 424-808 native draw GXM osservati. Il batching elimina molti draw ma **non cambia** la cadenza: circa 1,04-1,33 FPS.
- Ogni frame osservato contiene 12-13 richieste GXCopyTex, circa 11-12 copie GXM e **2 `sceGxmFinish`**.
- `white_fallback_draws=0`, nessun `[wic-texture-fallback]`; il bianco visibile **non e un fallback esplicito per texture mancante**. Non prova la correttezza di TEV, texgen, normali, luce GPU, texture data o copie EFB.
- A frame 334 il gioco e ancora in `stage=0`, camera intro non terminata, `race_frames=0`: questa sessione non puo confermare l'HUD in gara vera.

### Cosa dice la diagnostica storica (non confrontabile come FPS)

Nel campione completo precedente del frame 299 (strumentazione **pesante**): `draws=6260`, `draw_frontend_us=357923`, `efb_copy_us=956628`, `submit_us=857197`, `nativeDrawUs=800038`, `dl_copy_us=344952`. Questi tempi **si sovrappongono** (copie EFB includono flush e submission), non sono sommabili e non descrivono la build quiet attuale. Lo stesso log registra `finish_resource_destroy_calls_total=358` e `finish_resource_destroy_us_total=2179373` al frame 299: risorse GPU in uso venivano frequentemente distrutte. Non disattivare le sincronizzazioni prima di identificare risorsa, motivo e durata.

### Bug della prima sonda: tutti i tempi uguali a zero

La precedente build Aurora `1cf5bca` riportava `sampled=1` ma `worker_frame_us=0`, `draw_us=0` ed `efb_*_us=0` in tutte le finestre. Non era un frame gratuito: i tre timer `now_us`, `diagnostic_now_us` e `telemetry_now_us` **ritornano zero** quando `[diagnostics].enabled=false`. La sonda campionata controllava il flag ma continuava a utilizzare questi timer spenti.

## Correzioni mirate introdotte (non modificano il rendering)

1. In Aurora GXM, il probe ora usa `sceKernelGetProcessTimeWide` soltanto sulle fasi campionate (uno ogni 32 frame, campioni introduttivi). `worker_frame_us`, tempi native pipeline/texture/draw, EFB flush/capture/clear, present queue e durata delle finish reason non possono piu restare a zero per il solo gate `diagnostics=false`. Quando il probe e OFF, il percorso normale resta identico.
2. Il probe riporta `finish_destroy`, `finish_destroy_us`, `finish_buffer`, `finish_texture`, `finish_readback`, `finish_target`, `finish_explicit`, `finish_stream`, `finish_discard`, conteggi **del singolo frame**. Un campionamento logaritmico `[wic-resource-sync]` identifica *buffer/pipeline/texture/efb_target*, dimensioni e handle **prima** della distruzione in-flight. Nessuna chiamata `sceGxmFinish` rimossa o aggiunta.
3. I clock EFB del `DrawSink` sono limitati al frame campionato, non fanno syscall a ogni copia di ogni frame quiet.
4. `scripts/analyze_vita_render_budget.py` ora mostra le ragioni delle sincronizzazioni e segnala esplicitamente i log vecchi con tutti i tempi zero, senza spacciare quei valori per misure valide.
5. `scripts/install_vita_current.py` aggiunge `--visual-only`: cambia **una sola** opzione fra `gpu_lighting` e `native_materials` sullo **stesso SELF**, senza toccare altre sezioni, modalità Time Trial, NAND, PSARC, audio, texture, salvataggi o altre opzioni. Le modifiche usano lo staging FTP, backup e rollback già collaudati.

### Perche non ho introdotto una modifica grafica speculativa

I modelli bianchi e la scena scura potrebbero derivare da vertex lighting, shader TEV, UV/texgen, depth/EFB, sampler o contenuto asset. Non risultano errori di fallback e la screenshot storica non isola il draw incriminato. Disattivare in blocco la luce o inventare un colore rischia di mascherare le cause o introdurre regressioni. La prova CPU lighting/reference TEV ha valore diagnostico **prima** di correggere il ramo preciso.

## Comandi prossima build manuale

**L'assistente non deve compilare o installare la build Vita automaticamente.**

```sh
cd /Users/robin994/Documents/Code/PSVita/wiicompiled-vita
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON -DWIC_PRECOMPILED_ASSETS=ON \
  -DWIC_RESIDENT_GEOMETRY=ON -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=OFF -DWIC_NATIVE_DIRECT_STREAM=OFF
python3 scripts/install_vita_current.py --check-local
```

Chiudere WiiCompiled, avviare FTP VitaShell e installare il nuovo SELF:

```sh
python3 scripts/install_vita_current.py \
  --time-trial --diagnostics quiet --audio-policy continue \
  --render-batching on --budget-probe on \
  --gpu-lighting gpu --native-materials native
```

Rilanciare dalla LiveArea, attendere i frame 224/256/288/320; chiudere e riaprire VitaShell FTP:

```sh
python3 scripts/capture_vita_current.py --label render-budget-clockfix --tail-mb 16
python3 scripts/analyze_vita_render_budget.py \
  "$(ls -dt build/vita-psarc/device-runs/render-budget-clockfix-*/runtime-session.log | head -n 1)"
```

Controllare `finish_destroy`, `finish_destroy_ms`, `[wic-resource-sync]`, `efb_flush_ms`, `efb_capture_ms`, `native_draw_ms`, `worker_ms`. Non sommare scope annidati. Se il codice del campionamento continua a riportare tutti zero, rifiutare il risultato (forse SELF non aggiornato).

### Confronto visivo ordinato, stesso SELF

Con il gioco **chiuso** e FTP attivo (riavviare il gioco fra le configurazioni):

```sh
# A: illuminazione CPU, stesso TEV nativo e stessi asset.
python3 scripts/install_vita_current.py --visual-only --gpu-lighting cpu

# Ripristino A prima di provare B.
python3 scripts/install_vita_current.py --visual-only --gpu-lighting gpu

# B: shader materiale di riferimento, stessa luce GPU.
python3 scripts/install_vita_current.py --visual-only --native-materials reference

# Ripristino riferimento originale.
python3 scripts/install_vita_current.py --visual-only --native-materials native
```

Ogni variante deve essere giudicata **alla stessa scena e nello stesso stage**; non attribuire un diverso shader compile/prewarm a miglioramenti FPS. Se la luce CPU corregge Mario, indagare il path fixed-vertex GPU (normali/palette/matrici); se il riferimento TEV corregge il colore, isolare un bug del generatore native material. Se nessuna delle due cambia, catturare draw/pixel/UV/EFB in diagnostica full, una sola scena. HUD va misurato in `stage=2` con `race_frames>0`.

I benchmark FPS vanno ripetuti con `--probe-only --budget-probe off`: il frame campionato introduce inevitabilmente qualche syscall.
