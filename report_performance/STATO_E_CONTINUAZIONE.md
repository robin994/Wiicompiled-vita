# Stato salvato e continuazione

Aggiornato: 11 settembre 2026. Versione piano: 1.0.

## Risultato di questa task

Il piano Sol 5.6 high è ora implementato lato software fino alla build diagnostica integrata P6.48 sulla base upstream riallineata. F01-F09 dispongono dei percorsi, contatori o ottimizzazioni previste dove il piano richiedeva implementazione immediata; i gate che dipendono da frame time, correttezza visiva, simulazione, memoria o audio restano esplicitamente in attesa di PS Vita reale. F10 non è certificabile offline. La build P6.48 compila/linka/impacchetta dopo la correzione del filtro sorgenti Vita nel Makefile, senza reintrodurre Aurora.

| Fase | Stato | Prossimo criterio da soddisfare |
|---|---|---|
| F00 | Audit/provenienza offline completati; benchmark hardware in attesa | Fissare scenario e raccogliere almeno 3 ripetizioni comparabili |
| F01 | Implementata e verificata offline in P6.48 | Misurare overhead ON/OFF e residuo non attribuito su Vita |
| F02 | 16 MiB GPU residency verificata su hardware nel tratto gara P6.51: 0 miss/eviction a caldo; raw CPU cache ancora non accettata | Verificare stabilità/memoria su run più lunghi; non riattivare raw CPU cache senza A/B isolato |
| F03 | EFB lifetime/coalescing verificabili; P6.52 fast CPU resize/convert pronta offline | Misurare su Vita il calo da ~80 ms resize + ~135 ms convert e verificare correttezza visiva |
| F04 | Ripristino staged color/depth/cull/blend/TEV e census signature implementati | Validare resa; shader programmabili restano data-gated |
| F05 | Batching lineare one-pass, break reasons e istogrammi draw implementati | Misurare draw fisici; normalizzare strip/fan solo se l istogramma lo giustifica |
| F06 | Active CPU attribution e guest PC sampling implementati | Risolvere hotspot reali e decidere HLE/O3 solo da misure hardware |
| F07 | Vertex reuse conservativo, packet ownership e host-job profiling implementati | Misurare prep/copy p95 e contesa USER_2 |
| F08 | Timeline VI/present/input implementata; nessun cambio arbitrario di cadenza | Verificare tick/simulazione e 30 immagini nuove/s prima di cambiare scheduling |
| F09 | Yaz0 differential PASS, loading/job/audio delta telemetry implementata | Test save/load e 10 min audio senza underrun/clamp |
| F10 | In attesa di hardware | Matrice finale 30 FPS/correttezza/stabilità |

## Build integrata corrente

- Base Git: `3b4994ce1959eb3ac328d9b1454fa51240c43563` (merge non-squash di upstream attraverso `5722e79`).
- Profilo: `full-content-p6_48-sol-plan-integrated`.
- VPK: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_48-sol-plan-integrated.vpk`.
- Dimensione: 32.191.371 byte.
- SHA-256 VPK: `45a31b1dc657ea7b96ef5d41640e717d2bc7ee112a9ad0e9b11ae5b054f13f7f`.
- SHA-256 ELF: `3914a9a248e088259290e8d7ef1b2f90fe5518730382e44f812e32a55f2aad02`.
- Build/package: PASS; `unzip -t` PASS; audit ELF: zero `AuroraPacketRenderer` e zero `aurora::vita::gfx`.
- Test host: Yaz0 differential, EFB FIFO/coalescing/no-drop e texture budget PASS.
- Correzione build post-upstream: il filtro `MKW_VITA_NATIVE_SRCS` usava anchor regex non escapati per GNU Make; ora `\.cpp$` arriva a grep come `\.cpp# Stato salvato e continuazione

Aggiornato: 11 settembre 2026. Versione piano: 1.0.

## Risultato di questa task

Il piano Sol 5.6 high è ora implementato lato software fino alla build diagnostica integrata P6.48 sulla base upstream riallineata. F01-F09 dispongono dei percorsi, contatori o ottimizzazioni previste dove il piano richiedeva implementazione immediata; i gate che dipendono da frame time, correttezza visiva, simulazione, memoria o audio restano esplicitamente in attesa di PS Vita reale. F10 non è certificabile offline. La build P6.48 compila/linka/impacchetta dopo la correzione del filtro sorgenti Vita nel Makefile, senza reintrodurre Aurora.

| Fase | Stato | Prossimo criterio da soddisfare |
|---|---|---|
| F00 | Audit/provenienza offline completati; benchmark hardware in attesa | Fissare scenario e raccogliere almeno 3 ripetizioni comparabili |
| F01 | Implementata e verificata offline in P6.48 | Misurare overhead ON/OFF e residuo non attribuito su Vita |
| F02 | Identità texture, raw CPU cache, miss taxonomy e budget sperimentali implementati | Misurare churn caldo, working set e memoria prima di accettare 16/20 MiB |
| F03 | Failure taxonomy, generation lifetime, coalescing esteso e transfer finish differito implementati | Verificare zero copy perse/stale e costo EFB hardware |
| F04 | Ripristino staged color/depth/cull/blend/TEV e census signature implementati | Validare resa; shader programmabili restano data-gated |
| F05 | Batching lineare one-pass, break reasons e istogrammi draw implementati | Misurare draw fisici; normalizzare strip/fan solo se l istogramma lo giustifica |
| F06 | Active CPU attribution e guest PC sampling implementati | Risolvere hotspot reali e decidere HLE/O3 solo da misure hardware |
| F07 | Vertex reuse conservativo, packet ownership e host-job profiling implementati | Misurare prep/copy p95 e contesa USER_2 |
| F08 | Timeline VI/present/input implementata; nessun cambio arbitrario di cadenza | Verificare tick/simulazione e 30 immagini nuove/s prima di cambiare scheduling |
| F09 | Yaz0 differential PASS, loading/job/audio delta telemetry implementata | Test save/load e 10 min audio senza underrun/clamp |
| F10 | In attesa di hardware | Matrice finale 30 FPS/correttezza/stabilità |

, escludendo correttamente i moduli desktop/macOS e mantenendo `runtime/src/vita/guest_flat_memory_vita.cpp`.

## Baseline e percorsi

- Root: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`.
- HEAD letto: `5bdb6a63d4f495db7b086cd91059313f72cdfd8f`; 9 file tracciati erano già modificati e CLAUDE.md era non tracciato. Snapshot/hash in `evidence/provenance.json` e `evidence/git-status-at-audit.txt`.
- Profilo: `full-content-p6_38-producer-io-yaz0`.
- VPK: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_38-producer-io-yaz0.vpk`.
- VPK verificato: 44.391.665 byte, SHA-256 `050448ccb4eabb29779f3fbc374d85e6ac544cf023f6a7fdff69a7868c40b32b`.
- ELF indicato dal manifest: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_38-producer-io-yaz0.elf`; SHA-256 riportato nel manifest `ba8f10daa00a2dd0d3ba934ffb1f3d4bb27fcfaed617ecf9d1f315f877656719`. In questo audit il VPK è stato hashato direttamente; non assumere ricompilato o verificato l'ELF dal solo manifest.
- VitaGL locale: `/Users/robin994/Documents/Code/PSVita/aurora-vita-max-prehardware/third_party/vitaGL-speedhack-src/libvitaGL-m12_5-custom-heap.a`, SHA-256 verificato `de04978951a09cdc28da1710face48051530be17358df7f50273d7a9a262600d`.
- Ultimo boot archiviato: righe 18180–19928; configurazione coerente con P6.38, senza identificatore crittografico della build nel log.
- Attenzione alla provenienza: durante questa task il file vivo nel percorso SmashMeleeVita è cambiato ed è diventato un log Melee v3.28 da 10.071 byte. Usare `report_performance/evidence/runtime.original.log` per riprodurre i numeri MKW; il log successivo è isolato in `runtime.observed-later.log` e non entra nelle metriche. Non ripristinare né modificare il file vivo.
- Baseline: 0,949185 present/s su 51 intervalli; worker mediano 1013,535 ms su 40 serial osservati; grafica ancora rescue, audio discontinuo. Nessuna prova dei 30 FPS.

## Verifiche completate

- Copie byte-identiche degli allegati e hash SHA-256 salvati.
- Parser eseguito sul log completo: 13 boot, 40 campioni producer gara, 40 worker univoci, 51 present consecutivi nella finestra; 109 record metrici esclusi, 10 duplicati, zero conflitti accettati.
- Dati numerici principali riscontrati sulle righe originali e significato dei timer/contatori verificato nel codice.
- VPK P6.38 locale: hash conforme al manifest e `unzip -tq` senza errori. Nessuna compilazione eseguita in questa task di pianificazione.
- Collegamenti locali verificati; hash delle fonti del gioco e degli artefatti esaminati invariati rispetto all'inizio dell'audit. La differenza del log vivo è registrata a parte, senza alterare la copia originale.

## Prossima azione concreta

Testare P6.48 su PS Vita reale con la sequenza boot -> selezione personaggio -> selezione kart -> ingresso pista -> countdown/gara, senza cambiare contenuti o clock tra i run. Conservare il `runtime.log` completo. I record prioritari sono `sol_build`, `sol_critical`, `sol_texture`, `sol_prep`, `sol_batch`, `sol_efb`, `sol_draw_hist`, `sol_jobs`, `render_timeline`, `guest_hot_pc` e i delta audio/loading.

P6.48 serve a verificare integrazione e a raccogliere una fotografia completa, non a dimostrare causalità. Dopo il primo boot valido produrre e confrontare la coppia `full-content-p6_39-f01-critical-off` / `full-content-p6_39-f01-critical-on` sullo stesso scenario per misurare l overhead F01. Ordinare quindi F02-F07 in base ai tempi esclusivi. Non cambiare VI/simulazione e non introdurre shader programmabili o conversione strip/fan prima che i dati della P6.48/F01 ne dimostrino la necessità.

## Modello di aggiornamento per la prossima consegna

Registrare data, fase/subfase, ipotesi, file modificati, profilo e flag, hash fonti/VPK/ELF/libreria, comandi e codici di uscita, scenario e serial del nuovo log, misure prima/dopo con N e unità, overhead, risultato visivo/audio/input, regressioni e azione successiva. Per ogni fase usare uno stato preciso: da implementare / in corso / verificata offline / in attesa di hardware / verificata su hardware / scartata per regressione.

Il piano va aggiornato dopo ogni risultato, non solo a fine task. Prima di esaurire il contesto salvare qui la posizione esatta e il prossimo comando; il risultato hardware mancante deve rimanere esplicito.

## Aggiornamento hardware P6.51 e build P6.52

Il log P6.51 (marker `8A7EC461`) conferma che il budget GPU da 16 MiB risolve il churn F02 nel tratto stabile di gara: mediana degli ultimi campioni circa 105 hit / 0 miss per frame, 0 budget eviction e 0 byte espulsi; `resolve_us` scende a ~1.4 ms e `render_us` a ~306 ms. Il collo di bottiglia dominante diventa F03 direct EFB (~244 ms/frame), con ~80 ms di resize CPU e ~135 ms di conversione canali.

È pronta P6.52 `full-content-p6_52-f03-efb-fast-cpu`, marker `BA841AFA`, SHA-256 VPK `440d4e301f9a3de3f59d95c00db9471c0b22e348517f0df3c33a3be0e20e68b6`. Cambia solo i due kernel CPU EFB mantenendo P6.51 invariata per texture cache, rescue, lifetime e transfer. Hardware acceptance ancora pendente.

## Aggiornamento hardware P6.52 / P6.53 visibilita

P6.52 su Vita reale conferma il successo prestazionale F03: nel tratto gara il render worker e circa 22-24 ms e direct EFB circa 8-9 ms, mantenendo la cache texture GPU da 16 MiB senza il precedente churn. La cadenza complessiva osservata resta circa 3-5 FPS perche il collo di bottiglia si e spostato sul guest/producer; non dichiarare 30 FPS.

La correttezza visiva resta aperta: schermo gara bianco, modelli non visibili. Il log mostra texture rescue risolte correttamente ma tipicamente 12 comandi EFB/frame, 2 copy riuscite e 10 fallite con `InvalidDimensions`. Il percorso esistente applicava comunque il clear di GXCopyTex dopo una copy fallita, possibile causa della cancellazione della geometria gia disegnata.

P6.53 `full-content-p6_53-f03-visible-failed-efb-clear` (marker `9C78B93A`) mantiene P6.52, cache 16 MiB e rescue P6.35 invariati e modifica una sola semantica: il clear viene soppresso solo quando la relativa direct-EFB copy e fallita. Aggiunta telemetria `efb_invalid_dims` limitata ai primi 24 casi con coordinate source/mapped/present, scale, dst, formato e clear. VPK SHA-256 `2aedb585f401ca305413e208c2ea139a8fec1f3b2993938f8352f5e38678039f`, 32.191.040 byte; ELF SHA-256 `bdb0027a2a606a4461337c34239fcdb361774ba7d6ea520e643984b08ffa5622`. Build/package/helper tests e anti-Aurora audit PASS.

Prossima azione: test hardware P6.53 fino alla gara e screenshot. Se la scena torna visibile, confermare il clear-after-failed-copy come regressione. Se resta bianca, usare `efb_invalid_dims` per separare le coordinate EFB guest dalla trasformazione del display/present senza introdurre altre modifiche a texture/rescue/performance nello stesso A/B.


## Aggiornamento hardware P6.53 e build P6.54

P6.53 su PS Vita reale elimina il bianco totale: la gara mostra ora geometria del circuito, alberi/prop e HUD, anche se la resa resta molto scura e i materiali/personaggio-kart non sono ancora corretti. Le copy EFB fallite che mappano rettangoli al bordo inferiore a altezza zero non cancellano piu il framebuffer. F03 fast-EFB resta un successo prestazionale: nella finestra gara avanzata il costo EFB e circa 38-40 ms/frame, molto sotto i ~244 ms precedenti.

La stessa finestra rivela pero un secondo regime F02: ~33 MiB di richieste RGBA uniche/frame contro ~16.5 MiB residenti, circa 61-63 budget eviction/frame e ~0.63-0.66 s/frame di resolve/decode texture. Per il prossimo A/B non viene riattivata la raw CPU cache in USER heap: dopo vgl restano solo ~7 MiB USER e quella cache serve il prep worker, non il miss sincrono di ResolveTexture.

E pronta P6.54 `full-content-p6_54-f02-gpu-cache-20m`, marker `B0FC81C4`, VPK SHA-256 `bf141ac33b7687947f57c50194c6d438e2d0eba6adf40aa65cbc1bcb857e9182`. Cambia solo la residency GPU 16 -> 20 MiB; mantiene rescue visibile P6.53, fast EFB, failed-copy clear suppression, prep 8/4 MiB e raw CPU cache OFF. Test hardware richiesto fino alla fase gara avanzata. Se 20 MiB non abbatte il churn, il passo successivo F02 sara ridurre il footprint GPU o introdurre una cache decoded fuori dal limitato USER heap, non continuare ad alzare il budget alla cieca. Dopo stabilizzazione F02, F05 e gia giustificata dai ~5.8k triangle-strip draw/frame non fusi.


## P6.58 -> P6.59 F06
P6.58 su hardware porta il tratto gara campionato a ~184-220 ms/image (~4.5-5.4 FPS). Texture 20 MiB, EFB fast e strip stitching restano stabili. Il nuovo breakdown mostra che GX::CallDisplayList e ormai ~18 ms/frame, quindi non e piu il collo dominante. USER_0 usa ~133-168 ms CPU su ~185 ms wall nei campioni frame 480/600.

Gli hotspot sono stati mappati ai simboli reali; 0x8019A204 e MTX::PSMTXRotTrig, leaf SDK puro ancora tradotto con paired-single/PSQ mentre molte altre PSMTX vicine sono gia native. P6.59 nativeizza solo questa routine e mantiene identici renderer/EFB/cache/F05/F06-DL e gli stessi 16 hot-shard O3 di P6.58. Marker E7A698B3, VPK SHA-256 0f2f3b7f4a25740f5964412e56f24ff1ca029a24c0d70226458b3fc3c05c2912. Hardware gate: stessa scena gara, confronto wall/CPU/cadence e verifica scomparsa/riduzione hotspot 8019A204 senza regressioni visive.


## P6.59 recovery

Il tentativo native PSMTXRotTrig e stato ritirato: su hardware fermava il boot per mismatch con la generated indirect-dispatch table (`0x8019A204`). La nuova P6.59 `full-content-p6_59-f06-sampler-off-recovery` riparte dalla P6.58 hardware-good e disattiva soltanto il guest PC sampler, mantenendo i contatori F06 low-overhead. Marker `9ED61D4A`, VPK SHA-256 `cad17125968b378bf5bc44d39a7d648ca5b0cc9f487fa4db18ed6780e36feb7e`. Gate immediato: confermare boot, menu, selezione e gara; poi confrontare frame wall/CPU con P6.58 per misurare l overhead del sampler.


## P6.59 -> P6.60 F06 raw-mesh cache

P6.59 sampler-off is a negative hardware A/B: heavy race remains ~2.35-2.40 FPS. Raw-mesh telemetry instead shows an entry-capacity problem (about 220 hits / 5883 misses / 5877 stores for ~6103 cacheable draws, only ~45 invalidations and ~1.37 MiB resident payload). P6.60 expands only raw-mesh set count from 512 to 2048 (capacity 2048 -> 8192 entries), preserving the 4 MiB payload budget and all renderer/EFB/texture/rescue settings. Marker 534554F8, VPK SHA-256 aadee5f77169b4e360784611244dde6a61c97cda42d3eee86d78f51870a8e510. Offline validation PASS; metadata costs +2,211,840 ELF data bytes, so real-Vita free-memory and crash behavior are part of the hardware gate.


## P6.60 hardware / P6.61 F06 raw-mesh 16K

P6.60 e un successo hardware: nella gara pesante la raw-mesh cache passa da ~220 hit / ~5883 miss della P6.59 a circa 4.2k-4.7k hit / 1.25k-1.55k miss, senza invalidazioni significative e con ~1.7 MiB di payload su 4 MiB. Il producer scende da ~416-454 ms a ~243-273 ms/frame e il render da ~132-136 ms a ~57-65 ms. Il costo residuo full-race torna concentrato in GX::CallDisplayList (~90-108 ms), soprattutto template replay (~68-75 ms).

P6.61 `full-content-p6_61-f06-rawmesh-16k` cambia soltanto i set raw-mesh 2048 -> 4096, cioe 8192 -> 16384 entry metadata, mantenendo budget payload 4 MiB e tutto il resto identico. Marker `8F2B5D4E`; VPK SHA-256 `896cc7f9ba5bb47cb5d4bd0e6979fd016cdeb5bcdde986682907a5e46470f2e5`; ELF SHA-256 `0f40ad49edc4c9ffae0fde3da4825dbf8a28f4f0bc5201f124a5a01b1b3e7f67`. Offline build/package/helper/audit PASS. Gate hardware: gara completa, hit/miss/store, dl_template, producer/prep/render e memoria libera. Se i miss restano nell ordine di ~1k/frame, non aumentare ulteriormente la cache: passare a validazione dipendenze a livello template / replay piu grossolano.

P6.61 static-data cost relative to P6.60: data 12,582,578 -> 15,531,698 bytes (+2,949,120 bytes, ~2.81 MiB); text and BSS are unchanged. P6.60 reported only 7,340,032 bytes USER free after vitaGL, so P6.61 must be treated as a memory-risk A/B and rejected immediately if hardware shows bad_alloc/crash or materially unsafe USER headroom.

## P6.64R -> P6.65 text dedupe gate (2026-09-12)

`runtime2.txt` confirms that the boot-safe P6.64R path works on real Vita. Actual marker `9E02AE3F`; the earlier P6.64 native Layout/TextBox wrappers are rejected permanently because they triggered stale generated indirect dispatch at `0x8007A990`. In P6.64R, exact text duplication is repeatable and strongly same-owner: frames 120/240/360/480/600 show 18 exact duplicates out of 36 glyphs with all 18 attributed to the same owner; frame 720 shows 370 exact duplicates, 209 same-known-owner, 0 different-known-owner and 161 involving unknown owner; frame 960 shows 17/17 known exact duplicates from the same owner. No sample reports `exact_different>0`.

P6.65 `full-content-p6_65-text-state-dedupe` therefore enables a conservative backend experiment, marker `3AB4B080`. It only suppresses a second same-owner raw glyph quad after decoded vertices/PN references and captured transform+raster+texture/TEV state all compare equal. Unknown-owner and state/geometry-mismatch cases remain untouched. New telemetry: `sol_text_dedupe candidates/suppressed/state_mismatch/geometry_mismatch/table_full`.

Artifact: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_65-text-state-dedupe-hot-O3-43724a1f.vpk`, 31,917,306 bytes, SHA-256 `cc82ad4ce00cffb837ec24d24479e25ad249c4fae851ce4edfaf25fee9cf0c89`. ELF SHA-256 `8ab0cd120c1730b035a393920eb6870941bfe80214ea45b0be3d9ee28bd78c1e`. Dedupe ON/OFF graphics checks, full build/package and anti-Aurora/rejected-wrapper audit PASS.

Next hardware step: reproduce the doubled-text menu on P6.65, visually verify whether the duplicate print is reduced without missing legitimate labels, then provide the complete runtime log. Prioritize `sol_text_dedupe`, `glyph_probe`, `glyph_owner`, producer/prep/render only secondarily. Do not call P6.65 a performance win from timing alone because diagnostic text probes remain enabled.


## P6.75 hardware / P6.76 DL record burst memcpy (2026-09-12)

P6.75 real-hardware marker `8200B3F7` confirms that targeted O3 on `RFLiInitShapeRes` and measured helpers is useful but insufficient. On the comparable heavy RFL frame, wall time moves from 5,893,545 us to 5,409,592 us (~8.2% faster), while the dominant `GXBegin` interval at LR `0x800C23C0` remains 4,919,256 us across 284 calls. The secondary `0x800C4CA4` path is `RFLiDrawQuad` and is only ~68.9 ms total, so it is not the primary target.

The same P6.75 log resolves the EFB diagnostic cleanly: the ten failed EFB copies in steady frames are all `OutsidePresentSource` (`reason=0/0/0/10/...`). The failing 32x32 source rectangles begin at guest/EFB y=456 and are mapped through the 608x456 fullscreen-present transform to Vita y=544, yielding zero mapped height. Do not clamp these copies; internal EFB coordinates must be separated from the final presentation transform in a later graphics-correctness change.

P6.76 `full-content-p6_76-dl-record-burst-memcpy` attacks the remaining RFL recording cost without replacing translated functions. The existing RFL FIFO burst already skips the guest byte loop, but `WriteDisplayListBurst` still performed thousands of `Memory::Write32/16/8` operations, each repeating guest-memory policy/executable-page checks. With `MKW_VITA_DL_RECORD_BURST_MEMCPY=1`, the recorder proves the complete non-wrapping destination as ordinary writable/non-executable guest RAM once via `ResolveRangeHost`, rejects source/destination overlap, then copies the already-encoded FIFO bytes with one `memcpy`. Any failed proof or overlap falls back to the original scalar path unchanged.

P6.76 offline gates: `graphics-check` PASS, full compile/link PASS, VELF/FSELF/VPK package PASS, `unzip -t` PASS, direct-vitaGL renderer audit PASS with zero Aurora renderer symbols. Marker **`37B9EC6B`** (decimal `934865515`). VPK: `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_76-dl-record-burst-memcpy-hot-O3-bcd16295.vpk`, 29,630,350 bytes, SHA-256 `b737df521399e055e94e52c29b61f97d9edbf0a82efc4c5e6570af986ef3a135`. ELF SHA-256 `2cc55cc5df4e3ac0528995564a6252f61e1fbdec1b230b294c3f724d70c923ac`. Hardware validation is pending. Primary gate: compare the same RFL `count=284` interval against P6.75; a meaningful success must sharply reduce `gap_us`, not merely move a few percent.
