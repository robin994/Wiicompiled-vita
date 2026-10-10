# WiiCompiled Vita: draw batching nativo asincrono e diagnosi texture/HUD

Data: 10 ottobre 2026. Renderer **Aurora-Vita GXM**, senza VitaGL e senza riabilitare il percorso direct GPU vertex streaming.

## Evidenza hardware recuperata (read-only)

Ultima sessione su `192.168.1.79` archiviata in `build/diagnostics-3d-native-20261010/`:

- `[gx-worker] requested=1 running=1`: la coda GX **e gia asincrona**; questa patch non aggiunge inutilmente altri thread.
- `[native-render-config] ... direct_submit=0 direct_stream=0 ...`: i percorsi GXM compile-time diretti rimangono disattivati per la prima prova, per evitare di sommare esperimenti.
- `[test-time-trial] enabled=1` e `[race-progress]` fino al frame 389: si osservano `stage=0`, `scene_id=2`, `pans_finished=0`, `race_frames=0`. Questa sessione e ancora **nell'introduzione**: non e una prova che l'HUD manchi durante la gara. L'utente aveva segnalato HUD mancante anche su run precedenti, da verificare con un'immagine di `stage=2`.
- 13 finestre quiet dopo frame 240: 161 frame in circa 135 s = **1,191 FPS**, media **839,86 ms/frame**. La cadenza non misura separatamente CPU, GPU e FIFO.
- Nessun nuovo errore fatale. Continua un avviso: `[gx] depth EFB copies require native implementation` durante il caricamento, possibile causa di effetti mancanti ma non attribuibile con certezza ai modelli bianchi.
- Il PSARC montato dichiara 7.210 voci precompilate, 5.867 texture e 1.343 geometrie; questa cifra e un catalogo, **non** prova che quei draw abbiano evitato la decodifica o usato tutte le risorse.

Cattura precedente con `frame-300-draws.csv`:

- **6.260** draw in un frame diagnostico in pausa; 3.622 hanno 4 vertici, in genere quad.
- 4.669 draw usano lo shader/materiale nativo; 1.593 no. Questo non misura la presenza/assenza delle singole texture.
- Nell'ordine catturato, 6.105 coppie adiacenti di draw su 6.260 mostrano uguale `pipeline_active`, `uniform_payload` e `draw_state` (97,5%). **E solo un limite superiore teorico**: non contiene tutte le barriere FIFO, il lifetime dei render target, il layout di memoria e la verifica completa di sampler/state.
- Con un ring che allinea ogni allocation a 16 byte, un quad con 6 indici U16 usa 12 byte ma la slice seguente inizia dopo il padding: `local_draws_mergeable` richiedeva adiacenza e non poteva unirlo.

## Implementazione candidata (isolata a WiiCompiled)

Nel submodule `aurora-main`, con l'opzione runtime `[vita].draw_batching = true`:

1. `gfx/vita_draw_batch.hpp`: separa l'equivalenza di stato completa dalla contiguita dei buffer. Sono ancora confrontati pipeline, uniformi, sampler, viewport, scissor, texture, geometria e fixed-vertex state. Il vecchio predicato con indirizzi strettamente adiacenti mantiene identica semantica per gli altri port.
2. `gfx/vita_streaming_arena.cpp`: introduce `compact_pending_draw()`, che valida prima di scrivere **tutti** i buffer/offset, i limiti 16-bit e gli indici, poi muove esclusivamente i vertici/indici del draw piu recente negli spazi di padding di uno slot **non ancora flushato**. Gli indici sono ribasati alla posizione del primo draw senza cambiare l'ordine dei triangoli. Nessuna riallocazione per draw, nessuna readback GPU e nessuna nuova barriera `sceGxmFinish`.
3. `gfx/vita_draw_adapter.cpp`: se il ring usa il percorso GXM WiiCompiled, unisce il draw solo dopo equivalenza completa. La verifica rimane dietro `MKW_TARGET_VITA && AURORA_VITA_RENDERER_GXM`; tutte le altre configurazioni seguono il vecchio percorso.
4. Il percorso **direct stream write GPU** continua a rifiutare le rilocazioni: non scriviamo mai sopra memoria GPU in uso. `WIC_NATIVE_DIRECT_SUBMIT`/`WIC_NATIVE_DIRECT_STREAM` restano OFF nella prima prova.
5. `DrawSink` emette ogni 64 frame `[wic-draw-health]` con richieste, draw submit, `merged`, draw con white fallback, fallback copie EFB e opzione di batching. Il primo evento `[wic-texture-fallback]` espone slot, validita, formato, dimensioni, identita e `dynamic_copy`; campionamento logaritmico successivo. Gli eventi sono attivi anche in modalita quiet, senza tracce per ogni draw.

### Cosa non e ancora stato fatto

- **Non** esiste ancora un contratto nativo completo per oggetti NW4r, skinning, pose e shader/TEV dinamici. Le geometrie statiche gia possono essere caricate da sidecar PSARC e mantenute in buffer GPU, ma i miss/draw animati hanno ancora packing e trasformazioni ARM. Raggruppare le draw riduce chiamate GXM, **non** elimina da solo quelle conversioni.
- Le texture bianche possono derivare da fallback mancanti, copie EFB, shader/color lighting o invalidazione GPU. Finche non abbiamo log delle nuove sonde e un'immagine alla **stessa fase** non forziamo colore o texture al posto della sorgente.
- L'HUD non viene inventato in overlay: deve essere disegnato dalla pipeline originale e verificato dopo l'introduzione (`stage=2`, `race_frames>0`).
- **Nessun risultato FPS hardware** e attribuito al batching prima del confronto controllato con lo stesso SELF.

## Test e primi gate

- Aurora host: suite completa 24/24 PASS; test di submission e backend contract PASS.
- `tests/vita_submission_test.cpp`: test con 128 quad di 4 vertici e 6 indici, compattazione ricorsiva, ordine dei byte vertice e degli indici preservato; test indici invalidi, slice gia flushate e cambio di pipeline/scissor/materiale.
- `arm-vita-eabi-g++ -fsyntax-only` PASS per adapter, ring buffer e DrawSink con i **veri flag** `MKW_TARGET_VITA`/`AURORA_VITA_RENDERER_GXM` del port.
- Nessuna build Vita, installazione o avvio console automatico.

## A/B su un unico SELF: prima correctness, poi FPS

Usare il Time Trial automatico, con il medesimo PSARC, shader cache e clock e con rendering GX asincrono. Compilare manualmente con `WIC_NATIVE_DIRECT_SUBMIT=OFF` e `WIC_NATIVE_DIRECT_STREAM=OFF`, perche questo test misura il batching e non la submission diretta.

```sh
cd /Users/robin994/Documents/Code/PSVita/wiicompiled-vita
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON \
  -DWIC_PRECOMPILED_ASSETS=ON -DWIC_RESIDENT_GEOMETRY=ON -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=OFF -DWIC_NATIVE_DIRECT_STREAM=OFF
python3 scripts/install_vita_current.py --check-local
```

Dopo aver **chiuso il gioco**, con VitaShell FTP attivo su `192.168.1.79:1337`:

```sh
# A: vecchio percorso, stesso SELF, stessa gara.
python3 scripts/install_vita_current.py --time-trial --audio-policy continue --diagnostics quiet --render-batching off
# Avviare/giocare fino a stage=2 (non misurare intro), chiudere e riattivare VitaShell FTP.
python3 scripts/capture_vita_current.py --label race-batching-off

# B: solo opzione batching, nessun nuovo eseguibile.
python3 scripts/install_vita_current.py --batch-only --render-batching on
# Stesso Time Trial/stessa scena; chiudere e riattivare VitaShell FTP.
python3 scripts/capture_vita_current.py --label race-batching-on

# Ripristino conservativo.
python3 scripts/install_vita_current.py --batch-only --render-batching off
```

Il nuovo `scripts/install_vita_current.py` permette `--render-batching on/off` e `--batch-only`, preservando tutti gli altri campi; l'opzione batch-only richiede che il SELF corrente sia gia installato. Nei log confrontare `[wic-draw-health] merged=...`, `white_fallback_draws=...`, lo stato race (`stage=2`), frametime e assenza di regressioni di pixel.

Per distinguere **correttezza immagine da performance**, separare una cattura diagnostica:

```sh
python3 scripts/install_vita_current.py --time-trial --audio-policy continue \
  --render-batching off --diagnostics enabled --capture-frame 680
# Avviare e raggiungere almeno il frame 680 con stage=2; poi VitaShell FTP.
python3 scripts/capture_vita_current.py --label race-visual-baseline --frame-ppm 680
```

Questa cattura introduce readback e **non** va usata per calcolare gli FPS. Se stage=2 non e ancora attivo al frame 680, spostare il frame diagnostico piu avanti (prima di eseguire una nuova prova). Confrontare immagini A/B, fallback maschere e scene. Solo dopo un output equivalente si puo misurare un guadagno FPS e procedere al percorso nativo per oggetti/materiali piu complessi.
