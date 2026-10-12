# WiiCompiled Vita — P0 GXCopyTex asincrono e ritiro EFB (10 ottobre 2026)

Questa tranche implementa due correzioni native **indipendenti e OFF per default**. Non dichiara risolti 1 FPS, scena scura o modelli bianchi: il guadagno e l'equivalenza dei pixel richiedono il collaudo hardware.

## Evidenza prima della patch

Dal Time Trial hardware post-M06.1 del 10/10 (Vita 192.168.1.79):
- `[wic-render-budget] frame=256 worker_frame_us=745150`; present ~1,3 FPS.
- 6158 draw richiesti, ~5369 fusi, 816 draw GXM. Solo 13 erano precomputed.
- 12 copie EFB GXM e 2 `FinishReason::ResourceDestroy` (~26,6 ms complessivi) per frame.
- Texture bianche esplicite: 0. Non dimostra che materiali, luci o copie depth siano corretti.
- Stage 0; gara attiva e HUD stage 2 non verificati.

I 745 ms del worker comprendono il tempo di **produzione e attesa** nel frame e non misurano il solo tempo GPU. I ~26,6 ms di finish non possono spiegare da soli un frame da ~745 ms.

## Correzioni

1. **GXCopyTex senza attesa ridondante**: `runtime/src/hle/gx/gx_copy.cpp` evita `GXDrawDone` se `[vita] async_efb_copies=true`, il worker GX e attivo e non e registrata una draw-done callback. `GXCopyTex` resta un callback FIFO accodato *dopo* i registri/stati precedenti. Guest readback rimane una barriera, non viene bypassato. La policy ha test a compile-time.
2. **Retire/reuse GPU per EFB**: `aurora-main/platforms/vita/gxm/gxm_renderer.cpp` trattiene gli oggetti `SceGxmRenderTarget`, superfici, sync e allocazioni CPU/GPU dietro il serial di scena notificato da `sceGxmEndScene`. Un vecchio handle diventa invalido immediatamente; il nuovo handle riusa backing solo quando la GPU ha terminato, con dimensioni e presenza depth uguali. Il colore viene azzerato come nella creazione ordinaria; depth/sampler state invalidati.
3. **Casi conservativi**: normali texture, trasferimenti in corso, dipendenze vertex pendenti, errori, assenza del notifier, esaurimento del pool e budget usano `sceGxmFinish` quando richiesto. Nessun uso di puntatori a GPU memory ancora in flight e nessuna variazione alla semantica del gioco, dei callback o delle copie.
4. **Budget del pool**: massimo 24 record, 12 MiB complessivi; backing trattenuto rimane contabilizzato nel budget nativo. Risorse completate possono essere evicted; sotto pressione il renderer esegue la vecchia finish e libera il pool prima di riportare OOM.
5. **Osservabilita corretta**: `[wic-efb-retirement]` campiona retired/reused/pending_bytes, `[wic-draw-health]` associa tempi delle copie EFB allo stesso frame delle misure GXM; `[wic-resource-sync]` stampa le dimensioni in un formato supportato dal logger Vita.

L'esperimento non modifica la configurazione di default di Aurora per gli altri port, ne riabilita direct GPU stream write, ne altera il serial di completamento M06.1 per `SceGxmPrecomputedDraw`.

## Build Vita: solo manuale

```sh
cd /Users/robin994/Documents/Code/PSVita/wiicompiled-vita
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON -DWIC_PRECOMPILED_ASSETS=ON \
  -DWIC_RESIDENT_GEOMETRY=ON -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=OFF -DWIC_NATIVE_DIRECT_STREAM=OFF
python3 scripts/install_vita_current.py --check-local
```

Il controllo `--check-local` verifica anche l'hash **dell'intero albero
sorgente e dei file generati**, non solo i commit Git: con M06.1 ancora
non committato, una vecchia `DELIVERY.json` poteva altrimenti autorizzare
un test su un SELF senza le nuove opzioni. Se il controllo rifiuta
l'eseguibile, ripetere la build manuale prima di installare.

Prima prova: gioco chiuso, VitaShell FTP su 192.168.1.79:1337, stesso PSARC:

```sh
python3 scripts/install_vita_current.py --time-trial --diagnostics quiet \
  --render-batching on --precomputed-geometry on --budget-probe on \
  --async-efb-copies off --deferred-efb-targets off
```

Avviare il Time Trial, fermare il gioco, riaprire FTP, raccogliere:

```sh
python3 scripts/capture_vita_current.py --label efb-reference
```

A/B sullo **stesso SELF**. Tra ogni modifica chiudere WiiCompiled e riaprire VitaShell FTP:

```sh
python3 scripts/install_vita_current.py --efb-only --async-efb-copies on
# Avviare, provare, chiudere; riaprire FTP e catturare come efb-async-on.
python3 scripts/install_vita_current.py --efb-only --deferred-efb-targets on
# Avviare, provare, chiudere; riaprire FTP e catturare come efb-retire-on.
```

Se il risultato grafico peggiora, rollback selettivo:

```sh
python3 scripts/install_vita_current.py --efb-only --deferred-efb-targets off
python3 scripts/install_vita_current.py --efb-only --async-efb-copies off
```

Il baseline FPS va misurato anche con `--probe-only --budget-probe off`, tre run sulla **stessa** scena e dopo prewarm. Per la correttezza verificare ombre, lightmaps, Mii, scena scura, modelli bianchi, camera, HUD con `stage=2`, audio e input. Osservare `[wic-efb-retirement] reused_total`, `finish_destroy`, `worker_frame_us` e il frame effettivamente presentato; zero reuse indica nessuna copertura, non un miglioramento.

## Verifiche non hardware

- Suite host WiiCompiled, parser e bootstrap di configurazione.
- Test Aurora del pool: dimensioni/formato depth, vincoli di serial, pressione memoria, 3.000 scene e fallback conservativo.
- AddressSanitizer/UndefinedBehaviorSanitizer sul pool.
- Test fake-FTP installer: `--efb-only` cambia solo il booleano richiesto, idempotenza A/B/A, nessun download/eseguibile o altra config sostituiti.
- VitaSDK `arm-vita-eabi-g++ -fsyntax-only` sui sorgenti modificati: produce **zero ELF/SELF/VPK**.
- **Nessuna build Vita, installazione o lancio automatico**; M06.1 dirty resta preservato.

## Lavoro successivo

Questo esperimento mira alle sincronizzazioni GX/EFB, non risolve automaticamente tutti i ~745 ms del frame. M02 richiede i producer nativi NW4r per shape/material/pose; M05/M08 richiedono diagnosi e correzioni TEV/lighting e copie depth realmente consumate dal gioco. Non affermare 60 FPS prima di misure su device.
