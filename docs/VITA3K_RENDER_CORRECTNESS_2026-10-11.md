# Vita3K - localizzazione del rendering bianco (WiiCompiled / Aurora-Vita)

Data: 2026-10-11. Il port WiiCompiled resta su renderer GXM nativo e GX consumer asincrono.
Tutti gli esperimenti di bisezione sono opt-in e solo per Vita3K. Nessun deploy PS Vita reale.

## Evidenze gia verificate

- Mario e grigio/bianco nella schermata Vita3K. Kart, pista e alcuni elementi
  dello scenario mantengono texture e colori. Non e un black screen generale.
- La condizione persiste indipendentemente con `gpu_lighting=false`,
  `native_materials=false` (TEV riferimento) e `geometry_cache=false`.
  Pertanto non esiste prova che GPU lighting, ottimizzazione TEV o residency
  geometria siano l'unica causa.
- `white_fallback_draws=0` in tutte le varianti osservate: e solo il
  contatore della texture sostitutiva quando un handle richiesto non viene
  creato, non una verifica dei texel campionati o dei dati UV.
- Nel vecchio `frame-300-draws.csv`: 785 draw GXM, 774 con texture_mask
  diverso da zero. Otto draw `700-707` hanno `texture_mask=0` e shader
  frammento `0a75eafeec1ea448`. Nove draw `40,41,61,62,71,72,73,81,82`
  hanno `texture_mask=2` e `texgen_mask=0`, shader
  `0bb8dfac2ce27780`. Questi sono **candidati da identificare
  graficamente**, non ancora dimostrati come Mario.
- Il `frame-300.ppm` del vecchio `Renderer::readback_rgba8` e completamente
  nero (960x544), mentre una cattura indipendente della finestra Vita3K mostra
  piste e kart: non usare questo PPM come ground truth del framebuffer.
  `gxm_renderer.cpp::readback_rgba8` legge direttamente il backing CPU di
  `d.surfaces[d.front]` dopo `sceGxmFinish`; serve un controllo separato
  dell'ownership/cache-coherence/target prima di considerarlo affidabile.
- A/B di primitive GX native nello **stesso SELF**
  `4811785d1d6e3aaecedc460928625377520d6c014754f27fbfabc835fa3313d8`:
  soglia 0/128/64: 1,214/1,174/1,186 FPS, contatore
  `native_primitives=0` in tutte. Non e una soluzione per il bianco
  ne una prova di aumento prestazionale; nessuna installazione hardware.

## Confini della pipeline da correlare

1. `aurora-main/lib/gx/command_processor.cpp`: FIFO BP/CP/XF, stato GX,
   `numTexGens`, slot TEV/texMapId. Lo stato guest corretto non e ancora
   comparato con un riferimento Dolphin per questi draw.
2. `aurora-main/platforms/vita/gx/aurora_gx_bridge.cpp`:
   `translate_current_pipeline_and_layout` e `translate_texture` leggono
   lo stato GX e creano `PipelineDesc` / `TextureDesc` (puntatore dati,
   formato, palette, versione, sampler). In presenza di un mapping sbagliato
   lo shader puo essere valido ma il risultato bianco.
3. `aurora-main/platforms/vita/gx/aurora_vita_draw_sink.cpp`:
   texture mask TEV, handle texture, ricetta/CPU vertex transform, uniform,
   binding; il contatore di fallback non verifica la correttezza dei pixel.
4. `aurora-main/platforms/vita/gfx/vita_vertex_pipeline.cpp` e
   `gxm/gxm_shader_gen.cpp`: TexGen XF vs input UV e generazione Cg
   del TEV. Stage che non campionano per argomenti TEV emettono letteralmente
   `raw_tex=float4(1.0)` (puo essere corretto, va correlato).
5. `aurora-main/platforms/vita/gxm/gxm_renderer.cpp`:
   `Renderer::create_texture` decodifica/compact/swizzle, `bind_texture`
   e `sceGxmSetFragmentTexture`, oltre a `sceGxmDraw`.
   Un handle valido non certifica descriptor, bytes o UV.

## Diagnostica selettiva implementata

Lo strumento `ViewDrawCapture` puo sopprimere la chiamata GPU per
un intervallo di draw del solo frame catturato, lasciando invariato
il resto del gioco e registrando comunque `diagnostic_suppressed`
nel CSV. La configurazione e volontariamente separata dall'uso normale:

```toml
[diagnostics]
enabled = true
capture_frame = 300
draw_suppress_first = 694
draw_suppress_last = 728
```

`0/0` fornisce il controllo completo. Gli input malformati sono rifiutati.
Nuova API `aurora_vita_view_draw_start_ex_range`, con test della sequenza
e dei range invalidi. Gli shader non sono soppressi globalmente e nessun
draw fuori dal capture frame e omesso.

Prove host dedicate completate: `vita_view_draw_capture`,
`vita_shader_debug_registry`, `vita_frontend_translation` e
`runtime-config-contract` PASS.

## Stato sperimentale

Le nuove acquisizioni sono previste in
`build/vita3k-trace/draw-isolation-20261011/`.
Il runner conserva hash e copie originali di `Config.toml`,
`config.yml`, `eboot.bin`, CSV e PPM, lavora sul nuovo SELF solo
nell'installazione Vita3K, fotografa **la finestra dell'emulatore**
con `screencapture` e ripristina i file con controllo SHA-256.

Il confronto baseline vs skip `694-728` e **in corso di preparazione**.
Non attribuire ancora la geometria bianca a uno di questi shader:
prima identificare il gruppo di draw sullo schermo, poi per esso
verificare l'intera catena GX/TEV/UV/texture fino al GXM binding.

## Risultati successivi: isolamento per hash e coordinate GXM

Le catture sono state completate e conservate sotto
`build/vita3k-trace/draw-isolation-20261011/`. Questa sezione aggiorna
lo stato provvisorio precedente.

- Il test per ordinali `694-728` ha omesso 35 draw e prodotto una
  schermata visivamente piu luminosa; **non** e prova che quelle mesh siano
  responsabili del colore bianco. L'ordine dei draw cambia fra avvii.
- Il test per ordinali `685-690` ha omesso 6 draw, ma soltanto uno
  apparteneva alla famiglia shader cercata: usare gli ordinali non e
  un identificatore affidabile fra esecuzioni.
- Una nuova traccia GXM per draw registra l'hash del fragment shader,
  gli handle texture per slot e un bounding box 2D calcolato dagli indici,
  dai vertici e dalle uniformi. Questa AABB e **approssimativa** in
  presenza di GPU skinning, clipping, viewport o full-screen overlay,
  e non e un test di proprieta finale del pixel. In una cattura sono
  disponibili 168/770 bounding box.
- Lo shader `99288ddaca3b104f` (nel controllo draw 696, 1.058 vertici,
  texture slot 1/handle 489 e bounds circa x448..545, y306..436)
  interseca la testa; sopprimerlo per **hash** elimina tutti e tre
  i draw di questa famiglia, ma nella cattura GUI il pilota resta
  sostanzialmente bianco. Intersezione di bbox non implica pixel visibili.
- Lo shader `0a75eafeec1ea448` non usa texture nel TEV e comprende
  sei draw. Sopprimendoli per hash si modifica l'ombra del kart,
  senza risolvere il modello bianco; non e un corretto candidato per
  il materiale principale del pilota.
- **Attenzione al tempo di presentazione:** `sceGxmDisplayQueueAddEntry`
  e asincrona. In un capture immediato alla fine del frame, l'immagine
  della finestra puo rappresentare il frame precedente a quello
  marcato nel CSV. I due risultati visuali sono percio indicativi;
  l'esclusione rigorosa di uno shader richiede un frame presentato
  e mantenuto visibile con `sceGxmDisplayQueueFinish` in modalità
  diagnostica. Questa sincronizzazione non deve essere attiva durante
  l'esecuzione normale.
- `readback_rgba8` di GXM scrive un PPM nero per un frame in cui
  la finestra di Vita3K non e nera: il PPM interno non e una sorgente
  utilizzabile per verifica di pixel senza indagine separata.
- Il warning Vita3K *Render Target is using more scenes per frame than
  what was planned!* e separato dal difetto cromatico.
  `nativeSceneCount=23` con budget configurato a 5 conta scene globali,
  non prova da solo che un singolo render target ne abbia 23;
  il warning comunque giustifica una sonda per-target.

Il filtro robusto `aurora_vita_view_draw_start_ex_hash` e unicamente
diagnostico, viene eseguito durante un frame di cattura e ha test host
`vita_view_draw_capture` superato. Resta intatto il percorso GXM
normale; non fare deploy di questa build di isolamento su hardware.
La prossima prova e ottenere una GUI del **frame esatto**, poi ricondurre
  il materiale visibile a GX source/palette/UV, shader TEV e relativo
  descriptor GXM. Non si propone alcun fix cromatico non verificato.

## Confronto finale con presentazione sincronizzata

E' stata aggiunta **solo al percorso `capture_frame` esplicito**
una chiamata `sceGxmDisplayQueueFinish`, con risultato 0, e una
pausa di 4 secondi dopo il salvataggio per fotografare il frame
effettivamente presentato. Non esistono attese equivalenti nel
rendering normale.

Le sessioni `synchronized_retry` (baseline) e
`synchronized_skip_9928` (filtro per hash) hanno acquisito
rispettivamente 765 e 771 draw e tutti i 3 draw della famiglia
`99288ddaca3b104f` sono stati soppressi nel secondo run, senza
perdite di cattura. La GUI mostra il corpo/testa bianchi in entrambi.
Il confronto e documentato in
`build/vita3k-trace/draw-isolation-20261011/synchronized-character-comparison.jpg`.
Lo shader 9928 e escluso come causa principale, **ora anche sul
frame presentato e sincronizzato**.

Inoltre `synchronized_retry/frame-300.ppm` e **completamente nero**:
960x544, min=max=0, nonzero=0 anche dopo
`sceGxmDisplayQueueFinish()=0`. La corrispondente finestra mostra
correttamente la pista. E una riproduzione distinta di readback errato
in `gxm_renderer.cpp::Renderer::readback_rgba8` o nella mappatura
CPU/GPU della superficie; non puo essere attribuito soltanto al
ritardo di presentazione.

Un primo tentativo `synchronized_baseline` si e fermato durante
inizializzazione guest, prima della cattura, senza un errore GXM
attribuibile al test. Il riavvio pulito e riuscito. Il runner ha
verificato con SHA-256 il ripristino dei cinque file originali
Vita3K a ogni esecuzione conclusa, e non e stata installata
alcuna build sulla PS Vita reale.

### Stato aperto

La texture/TEV responsabile del **bianco del pilota** non e stata
ancora identificata. Prossima sonda circoscritta:
registrare per i draw effettivamente visibili sopra la testa e il
corpo sorgente GX, formato/dimensioni, palette e CRC prima e dopo
decodifica, min/max UV, argomenti TEV e binding GXM.
Un A/B diagnostico con una texture-test colorata su un materiale
identificato distinguerà campionamento erroneo da colore
costante TEV/lighting. Non alterare in blocco shader o
texture cache sulla base dei soli hash/bounds.
