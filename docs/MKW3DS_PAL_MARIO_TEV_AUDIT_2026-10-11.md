# MKW-3DS -> WiiCompiled: audit BRRES/TEV PAL di Mario
Data: 2026-10-11. Ambito: **correttezza del rendering GXM**, senza
sostituzione del renderer e senza build/installazioni PS Vita.

## Fonte, licenza e percorso di verifica

- Riferimento esterno: `https://github.com/MiniBandit/MKW-3DS`,
  revision `f0a895136a524b50bfb9ad275808708ba406530b`.
  Il progetto converte BRRES/TEX0 in modelli, texture e materiali 3DS
  prima del runtime, invece di ripetere la traduzione GX.
- `tools/export_brres.ts`, `export_driver.ts`, `retail_lighting.ts`,
  `gx_material.py` sono stati letti **come riferimento tecnico**.
  Gli offset del formato sono stati confrontati con il parser esistente
  `scripts/native_assets/formats.py` e il parser MIT `noclip.website`
  pinned al commit `4cb7471c7631370faf5d526a56876d91ba24aca0`.
- **Nessun codice del port originale MKW-3DS è stato copiato**:
  il suo `NOTICE` riporta che la base originale di dudewhat non ha una
  licenza esplicita; la licenza MIT del fork MiniBandit riguarda le sole
  modifiche successive.
- Si leggono solo i due asset PAL già contenuti nel **PSARC locale**:
  `disc/Race/Kart/ma_kart-mr.szs` e
  `disc/Race/Kart/mdf_kart-mr.szs`.
  Non vengono esportati dump di Nintendo; l'auditor emette solo
  metadati strutturali e SHA-256.
- Codice nuovo: `scripts/audit_mkw_brres_materials.py`;
  test: `scripts/test_audit_mkw_brres_materials.py`.
  L'inventario JSON effettivo è
  `build/material-audit/mario-pal.json` (**intermedio locale,
  non va committato/distribuito con risorse originali**).

## Riscontri oggettivi sul sorgente PAL

L'auditor ha percorso **2 archivi SZS, 4 BRRES, 16 MDL0,
23 materiali e 34 TEX0**. Gli archivi usano la **medesima**
`driver_model.brres` (SHA-256
`26bdd0efd43297d9205dca1afdcce4fc93b3ec3fd74644dde8895a14b8d61a18`).

Il modello primario del pilota espone due materiali
`mario_all_tx` e `mario_eye_tx`, entrambi:
**3 texture generator, 3 sampler, 3 stage TEV**, e nessuno
stage indiretto. La configurazione staticamente descritta nel
BRRES è:

| stage | TEXMAP | TEXCOORD | texture del corpo | texture degli occhi |
|---|---:|---:|---|---|
| 0 | 1 | 1 | `lm_0` | `lm_0` |
| 1 | 0 | 0 | `mario_all` | `mario_eye.0` / PAT0 |
| 2 | 2 | 2 | `lm_1` | `lm_1` |

La firma sorgente TEV tradotta nei valori degli enum GX è:

```text
tev_map_coords = 010100000202
tev_color_args = f8baf804f810
tev_alpha_args = 777774576777
```

La firma è comune ai materiali del corpo/occhi:
**non determina da sola quale texture RGB sia stata caricata**.
Servono TEXMAP, binding, sorgente e CRC/ID asset per distinguere
`mario_all` da `mario_eye.0` e successivi PAT0.

Il modello LOD usa `mario_tx_64` con la stessa famiglia di TEV.

Le texture originali sono formattate **GX CMPR (format 14)**.
Un decoder diagnostico indipendente di livello 0 ha verificato:
- `mario_all` **256x128**, pixel visibili 32.768,
  **30.883 non quasi bianchi**, 1.885 quasi bianchi, 0 trasparenti.
- `mario_eye.0` 32x128, 4.036 non quasi bianchi su 4.096.
- `lm_0` e `lm_1` 64x64, 4.096 non quasi bianchi su 4.096.
- SHA-256 **dei byte sorgente** e SHA-256 **RGBA decodificato**
  nel file JSON consentono future verifiche GX CPU/GXM upload.

Ci sono **8 PAT0** per `mario_eye_tx` (`1st`, `bad`, `bad2`,
`damage`, `good`, `left`, `right`, `wink`). La clip `wink`
usa `mario_eye.0`, `.1`, `.2` in momenti diversi.
Due SRT0 (`left`, `right`) agiscono sulla matrice texture 0
dello stesso materiale.

## Confronto con il vecchio TRACE Vita3K

Riferimento:
`build/vita3k-trace/draw-isolation-20261011/bbox_baseline/frame-300-draws.csv`.
Nelle **770 righe draw** registrate ci sono gruppi con 1, 2 e
3 stage TEV; per i draw a 3 stage si osservano maschere texture
`0x0b`, `0x89`, `0x09`, `0x0f`,
ma **nessuna maschera 0x07**, attesa dal materiale statico
non rimpiazzato del pilota PAL.

Questo è un **indizio**, non ancora la prova che Aurora abbia
perso gli stage di Mario. Il driver potrebbe non essere rappresentato
nei draw selezionati con quella configurazione (animazioni, override,
materiali ricaricati, sprite, LOD, sequenza o capture incompleto).
La sovrapposizione AABB alla testa non attribuisce l'ownership di un
pixel e le precedenti bisezioni GXM hanno escluso lo shader di un
draw sovrapposto senza rimuovere il modello bianco.

Il vecchio CSV **non registra** mappa/coordinate e argomenti TEV per stage:
l'auditor segnala correttamente `matched=false` e
`recapture after GXM diagnostic extension`, non fabbrica un match.

## Oracle: vero FIFO GX -> Aurora PipelineDesc (host)

In `aurora-main/tests/vita_frontend_translation_test.cpp` è stato
aggiunto `pal_mario_material_tev_oracle()`. Il test invia al **vero
decoder FIFO GX** i registri BP del materiale originali
`GEN_MODE`, `RAS1_TREF` (0x28, 0x29), `TEV_COLOR_ENV` e
`TEV_ALPHA_ENV` (0xC0..0xC5), poi le **sei scritture XF**
`0x1040/0x1050`, `0x1041/0x1051`, `0x1042/0x1052`
presenti nel display list del BRRES PAL.

Il test verifica prima `g_gxState.numTevStages=3`,
`g_gxState.numTexGens=3` e i tre texture map/coord; poi invoca
`gxbridge::translate_current_pipeline_and_layout()` e verifica
`PipelineDesc.tev.stageCount=3`, `texgenCount=3`, le tre coppie
TEXMAP/TEXCOORD e tutti gli argomenti RGB/alpha per stage.

**Oracle superato:** con il materiale statico sorgente completo,
la catena di decodifica GX/TEV e traduzione Aurora-Vita sul
backend host **preserva la configurazione originale**. Non abbiamo
dimostrato un bug nella traduzione di questi registri. L'anomalia
dei draw Vita3K potrebbe quindi originare nel caricamento runtime
del materiale, in uno stato GX diverso sul singolo draw, nel
material override o in una fase successiva; non è ancora attribuita.

## Sonda diagnostica introdotta (opt-in, nessun effetto sul gioco normale)

Nel submodule Aurora-Vita:
- `include/aurora_vita_view_draw_capture.h`:
  array `tev_maps`, `tev_coords`, `tev_color_args`,
  `tev_alpha_args` nel record di capture.
- `platforms/vita/gxm/gxm_renderer.cpp`:
  cattura la configurazione **effettiva** di `PipelineDesc::tev`
  esclusivamente nel ramo `if(traceDraw)` (frame singolo),
  senza introdurre copie per-draw nel rendering normale.
- `vita/game_aurora.cpp` aggiunge al CSV
  `tev_map_coords`, `tev_color_args`, `tev_alpha_args`
  come sequenze hex (4 digit/stage).

Lo script correla la configurazione sorgente a ciascun draw nella
regione di interesse solo se i nuovi campi sono disponibili.
Una firma identica non prova il nome del materiale né la correttezza
dei texel.
La struttura del record è verificata anche dal test
`vita_view_draw_capture`, che assicura la conservazione di tutti
i campi TEV durante la copia dei record.

## Riproduzione e prossimo esperimento

Audit sulla ISO PAL già trasformata in PSARC:
```sh
python3 scripts/audit_mkw_brres_materials.py \
  --psarc build/vita-data/mariokart-avc.psarc \
  --trace build/vita3k-trace/draw-isolation-20261011/bbox_baseline/frame-300-draws.csv \
  --output build/material-audit/mario-pal.json
```

Test host con sorgenti sintetici, senza dipendenza da dump Nintendo:
```sh
python3 scripts/test_audit_mkw_brres_materials.py -v
```
Test d'integrazione **opt-in** sul PSARC personale:
```sh
WIC_MKW_PAL_PSARC=build/vita-data/mariokart-avc.psarc \
  python3 scripts/test_audit_mkw_brres_materials.py -v
```

**Per il futuro A/B Vita3K**, dopo una build Vita esplicitamente
richiesta e svolta dall'utente, acquisire un nuovo `frame-300-draws.csv`
su Vita3K e passarne il percorso alla stessa CLI `--trace`.
Cercare la firma `010100000202/f8baf804f810/777774576777`.
Poi verificare presenza e contenuto degli handle TEXMAP 0/1/2,
formati/dimensioni/sorgenti e animazione PAT0 attiva.
Se la firma manca, instrumentare il punto di caricamento GX BP/XF
del materiale prima della traduzione; se esiste e le tre texture
sono corrette, isolare texgen/TEV/illuminazione e debug della
texture effettivamente campionata. Non correggere shader e
texture cache senza questo confronto.

**Limitazione:** prewarm delle risorse, fps e readback GXM nero sono
filoni separati; questa analisi offline non prova un aumento di FPS
né risolve visivamente Mario.

## Fase 2: identità dei pixel GX effettivamente caricati in GXM

L'audit della fase precedente si fermava all'identità dei tre stage
TEV. I tre numeri TEXMAP da soli non identificano la texture né
dimostrano che il contenuto dell'upload sia corretto. Questa fase
introduce una sonda **opt-in** nel submodule Aurora-Vita che conserva
una piccola tabella di provenienza per **handle GPU**:

- `AuroraViewTextureRecord` contiene handle GXM, formato GX,
  dimensioni, numero di mip, indirizzo sorgente/revision e impronte
  della texture e della palette. Le impronte sono **FNV-1a 64 bit dei
  byte GX encoded originali**, prima di conversione CMPR/BC1,
  swizzle, mipmap layout o staging GXM; al massimo il primo **1 MiB**
  viene letto. Per formati con mip differenti l'auditor calcola le
  firme di **tutti i prefissi mip validi**.
- Il codice calcola queste impronte soltanto quando una cattura
  `[diagnostics] enabled = true, capture_frame = N` è configurata
  all'avvio, **mai per il rendering ordinario**. La tabella
  temporanea (max 4096 handle) viene allocata all'avvio del singolo
  frame diagnostico, non durante un draw. La cache GXM, gli shader
  e la politica di residency non sono modificati.
- La tabella è esportata in
  `ux0:data/wiicompiled-vita/aurora/diagnostics/frame-N-textures.csv`.
  `frame-N-draws.csv` mantiene gli 8 `tex_handle*` che collegano
  le singole primitive alle texture della tabella. **Non si esportano
  byte Nintendo**, solo metadati e impronte. Un hash non trovato
  non è una prova di binding errato: la texture può essere dinamica,
  diversa da quelle nei due BRRES selezionati o caricata prima della
  sonda. Uno stesso hash può identificare più copie identiche.

L'auditor `scripts/audit_mkw_brres_materials.py` confronta le due
CSV con i TEX0 originali: formato/dimensioni, *encoded byte count*,
impronta FNV, numero di mip e (se disponibile) firma TEV del draw.
L'output `texture_trace_comparison` separa i match di contenuto
da quelli esclusivamente TEV. Un hash esatto sui dati compressi
permette di identificare, per esempio, `mario_all`, `mario_eye.0`,
`lm_0` o `lm_1` indipendentemente dal numero dell'handle GXM e
dall'ordine delle draw call. Non prova ancora l'ownership dei pixel
finali se altri draw li coprono.

### Test successivo (dopo build manuale richiesta dall'utente)

Mantenere lo stesso PSARC e i medesimi INI Vita3K; abilitare solo:

```toml
[diagnostics]
enabled = true
capture_frame = 300
```

Eseguire Mario + Standard Kart e conservare:
`frame-300-draws.csv`, `frame-300-textures.csv`,
`runtime.log` e schermata finestra effettivamente presentata.

```sh
python3 scripts/audit_mkw_brres_materials.py \
  --psarc build/vita-data/mariokart-avc.psarc \
  --trace /PERCORSO/DEL/TRACE/frame-300-draws.csv \
  --textures /PERCORSO/DEL/TRACE/frame-300-textures.csv \
  --output build/material-audit/mario-pal-new-gxm.json
```

**Interpretazione senza scorciatoie:**

1. `mario_all` riconosciuto su un draw con meno di 3 stage TEV:
   verificare l'override runtime **a monte di GXM** (GX state FIFO,
   ordinamento materiale/draw, transizioni scena, eventuali lod).
2. Sorgenti Mario e firma TEV originale presenti su draw ma il
   modello resta bianco: verificare texgen/UV, lightmap e shader
   GXM effettivamente compilato; il problema può essere **a valle
   della traduzione GX**.
3. Nessun handle del frame riconosciuto dai TEX0 Mario:
   non concludere «mancano le texture». Potrebbe essere una versione
   PAL diversa, un altro pilota/LOD oppure texture già trasformate.
   Registrare solo allora l'asset esatto attivo nella scena.

La vecchia cattura del frame 300 non ha una tabella delle texture.
**Non esiste ancora un nuovo esperimento Vita3K con questa sonda,
non sono stati misurati FPS né validata una correzione cromatica.**
