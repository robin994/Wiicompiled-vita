# MH3 reuse — implementazione M02/M05/M09, prima tranche 2026-10-10

Il lavoro applica parti **reali** delle 45 tecniche MH3 analizzate in `docs/MH3_REUSE_2026-10-10/`. NON dichiara completati tutti i ticket M00–M17; i ticket richiedono più fasi con oracoli di compatibilità e prove sul dispositivo. L'obiettivo rimane: Mario Kart Wii su PS Vita, Aurora-Vita **GXM nativo**, 60 FPS, scene/illuminazione/texture/HUD corretti. Manteniamo il tick fisico originale (no variable-step MH3).

## Implementato in questa tranche

### M02.1 — ricette native di display list geometriche NW4r

Sorgenti `aurora-main/lib/gx/prepared_display_list.hpp`, `command_processor.cpp/.hpp`:

- `NativeDrawRecipe`: massimo 16 comandi draw, 256 KiB/liste, con range tipizzati (primitive, VTXFMT, stride, count, offset). Il compilatore accetta soltanto draw e NOP, verifica limiti e overflow, respinge CP/XF/BP, chiamate annidate e codici non riconosciuti; per questi ultimi il vecchio parser GX rimane il **fallback integrale**.
- Si attiva mediante il preesistente `[vita] prepared_display_lists=true`, default OFF. La ricetta e conservata dal consumer GX per identità del pin immutable + bytes + epoca del layout CP; VCD/VAT e reset invalidano l'epoca. Gli offset si riferiscono a una copia GPU-safe/CPU-owned finché il worker non ha consumato la lista, **mai** a puntatori guest vivi non certificati.
- Replay sul consumer GX nell'ordine originale, tramite `DrawSink::submit` con lo stato shader/materiale **attuale**. Bypass della decodifica opcodes per più draw; non pretende di aver compilato materiali TEV o shape skinned in asset nativi.
- Contatori a frequenza bassa `[wic-native-producers] multi_compiled/multi_replayed/multi_draws`; zero indica assenza di copertura, non un'ottimizzazione riuscita.
- Test unit: opcode consentiti, CP/XF/BP che impongono fallback, payload malformati, stride per VTXFMT misti, massimo 16 draw; test FIFO reale: compile una volta, replay multipli e invalidazione epoca dopo write CP.

### M02.2 — trasporto immutabile per display list nella RAM Wii (implementato, verificato su hardware)

La prova hardware del 10/10 sul SELF `b8b7b74f...` con M02.1 attivo ha confermato
`multi_compiled=0, multi_replayed=0, multi_draws=0` nei campioni disponibili,
`stage=0` e circa 1–1,3 FPS. Al frame 224: 745,844 ms rispetto ai
753,982 ms della build precedente, **non** un A/B sullo stesso SELF. Non c'e
evidenza di accelerazione. La causa plausibile e stata localizzata nel
trasporto: su Vita `display_list_shadow_pin()` accettava soltanto puntatori
CDRAM `0x60000000–0x6fffffff`, quindi liste NW4r in RAM cached non
raggiungevano `submit_prepared_display_list()`.

- File `aurora-main/lib/gx/fifo.cpp/.hpp`,
  `lib/gx/display_list_shadow.hpp` e
  `platforms/vita/aurora_vita_backend.cpp`. Con
  `[vita].prepared_display_lists=true` e GX worker attivo, le liste
  non-CDRAM con <=256 KiB e prefisso draw/NOP plausibile usano ora un
  `PinnedDisplayList` e arrivano al compilatore di ricette M02.1.
  Senza flag, il comportamento della RAM cached rimane quello precedente.
- Cache **separata dalla CDRAM**, budget 2 MiB e al massimo 1.024 record;
  riuso solo dopo `byte_spans_equal` con l'originale guest. Tracked write
  o modifica guest **non notificata** generano una nuova identita immutabile;
  le copie gia accodate restano vive con `shared_ptr` fino al consumo FIFO.
  Le identita RAM usano un namespace distinto dalle identita CDRAM. Una
  nuova admission dopo eviction pubblica anche la revisione dell'indirizzo,
  per non riutilizzare geometrie GPU residenti basate su puntatori precedenti.
- Non sposta CP/XF/BP o materiali: liste state-only, codice GX non supportato,
  payload troppo grandi e assenza worker continuano nel parser/copy FIFO
  esistente. La validazione dei pacchetti rimane **sul consumer**; il controllo
  prefisso evita solo snapshot inutili delle liste di stato.
- Nuovi contatori campionati con `[wic-native-producers]`:
  `ram_candidates`, `ram_pinned`, `ram_fallback`, `ram_filtered`,
  `ram_untracked`, `prepared_hits/misses/rejected`, oltre ai vecchi
  `multi_*`. Se RAM e pinned sono >0 ma `prepared_rejected` prevale,
  il prossimo blocco richiede riconoscere davvero i producer ResShp/ResTev
  e le sequenze mixed state+draw, non un altro profiler.
- La toolchain GX VitaSDK usa **`-fno-exceptions`**: il catch per allocazioni
  e disponibile solo nell'host build. Su hardware i limiti di record e byte
  evitano una cache illimitata, ma **non** costituiscono una garanzia di
  recupero da OOM globale. In condizioni di non eleggibilita il vecchio FIFO
  resta il fallback.
- Test: route OFF/CDRAM/RAM e limiti inclusi edge-case overflow;
  verifiche esatte dopo scritture non notificate, riutilizzo indirizzi dopo
  eviction e lifetime di snapshot pending mentre il worker e bloccato.
  `vita_main_ram_snapshot_test` ripete 4.096 riutilizzi; passa sotto
  AddressSanitizer e UBSan. Il test integrato di `vita_frontend_translation`
  controlla anche il fallback opcode, il replay FIFO e la nuova identita.

**Convalida M02.2**: Aurora host **27/27**, WiiCompiled host **30/30**,
`arm-vita-eabi-g++ -fsyntax-only` per FIFO e backend **PASS**,
ASan+UBSan sul nuovo test **PASS**. Su Vita il SELF
`554ad061efccd409177a9256b457e716a89e5b635418f2b5c4e0b23ebe4743dc`
ha superato l'introduzione e raggiunto `stage=1`, `race_frames=20`,
senza fatal. A frame 384 sono stati misurati:
`ram_candidates=32371`, `ram_pinned=32371`,
`multi_compiled=13389`, `multi_replayed=14252`,
`multi_draws=55517`. Il frame 224 ha 555 draw GXM contro 800 della
build M02.1, ma il worker impiega 732,026 ms contro 745,844 ms.
Le finestre wall disponibili restano circa **1,16 FPS**, senza guadagno
statisticamente dimostrato. Riscontro riproducibile nel log:
`build/vita-psarc/device-runs/m02-2-mainram-on-20261010T204217Z/`.

### M02.3 — template di ricette strutturali attraverso snapshot RAM (implementato, hardware pending)

I log M02.2 mostravano il problema successivo: al frame 384,
`prepared_hits=2412` su 32371 lookup e `multi_compiled=13389`
contro 14252 replay. Una nuova identita immutable e una mutazione
del layout CP richiedevano di riparsare quasi ogni lista anche se
la **struttura delle draw** era invariata.

- `aurora-main/lib/gx/prepared_display_list.hpp` definisce una cache
  template, fissa e set-associativa (**128 bucket × 4 vie**), che memorizza
  **solo** offset, primitive, count e stride di massimo 16 draw;
  non contiene **mai** dati vertice, puntatori guest, GX material, pose o
  texture. La chiave (lunghezza, prefisso, primo opcode/count) e solo un
  hint di bucket: prima di ogni hit sono confrontati esattamente tutti gli
  opcode/header, tutti i NOP, i byte delimitanti e gli stride del layout
  CP **attuale**. Header e VTXFMT difformi, comandi di stato, padding non
  nullo e payload troppo corto invalidano il template e attivano il
  compilatore/fallback precedente. Hash collision ≠ successo.
- La cache opera **solo sul thread consumer GX** e viene resettata assieme
  al preesistente `prepared_display_lists`. Su un hit si inviano solo
  **i byte dello snapshot immutable della chiamata corrente**; la
  struttura puo essere condivisa fra identita RAM diverse, i dati e
  lo stato GPU no. I risultati sono verificati anche quando si cambia
  il CP layout epoch: se gli stride effettivi coincidono non serve
  ricompilare. Resolver memoizzato al massimo una volta per VTXFMT
  per ciascuna display list; nessuna alterazione di shader/TEV.
- Nuovi contatori cumulativi:
  `[wic-native-producers] template_lookups/template_hits/`
  `template_inserts/template_evictions` per attribuire la copertura
  **senza** un nuovo profiler per-draw.
- Materiali misti CP/XF/BP non sono stati accelerati con questo sotto-ticket:
  WiiCompiled gia utilizza `DlScanCache` e
  `SyncAppliedVtxStateFromHleReal` per le liste ResTev di stato. Aggiungere
  una seconda macchina di stato GX senza test di effetti collaterali
  produrrebbe regressioni (texture/luce/callback/ordine FIFO). Il producer
  nativo completo ResShp/ResTev e ancora un task separato M02/M03.
- M02.3 sfrutta **lo stesso** `[vita].prepared_display_lists=true`.
  Se l'opzione e OFF il percorso GX originale rimane invariato.

**Convalida M02.3 (host):** Aurora **27/27 PASS**, WiiCompiled
**30/30 PASS** con `ctest -j2`, `vita_prepared_display_list_test`
e `vita_frontend_translation_test` PASS. ASan+UBSan sul template
cache test PASS. `arm-vita-eabi-g++ -fsyntax-only` del consumer
`command_processor.cpp` e backend `aurora_vita_backend.cpp` PASS
con le flag reali VitaSDK `-fno-exceptions`. In un run host
`-j8` concorrente e fallito `host-wait-native-contract` (test
native sleep/timing non modificato da M02.3); isolato e nel successivo
`-j2` e passato. Il test Vita resta indispensabile per stabilire
`template_hits/lookup`, tempi e correttezza grafica.

**INI sulla Vita, 10/10:** eseguito
`python3 scripts/install_vita_current.py --probe-only --budget-probe off`
con successo (`installed_verified`),
`build/vita-psarc/device-installs/20261010T204751Z/`. Modifica
esattamente `render_budget_probe=true→false` per consentire un
confronto FPS quiet della **build M02.2 già installata**;
M02.2 resta ON, M09/EFB OFF, SELF e PSARC invariati. Il nuovo M02.3
non e ancora incluso sul dispositivo: richiede build Vita **manuale**
e successivo test prima di attribuire guadagni.


### M09.1 — UI NW4r::lyt quad come pacchetto FIFO native

Sorgenti `aurora-main/lib/gx/fifo.cpp/.hpp`, `command_processor.cpp`, `runtime/src/hle/gx/gx_dl.cpp`:

- Il produttore `SubmitLytDrawDirect` può accodare un `NativeLytQuad` tipizzato dietro tutti i comandi GX precedenti, **senza invocare DrawSink sul game thread**. I vertici vengono copiati nei buffer già posseduti dal FIFO e nessun callback/GXM viene riordinato. Il consumer prova il percorso tipizzato e, se fallisce la validazione prima dell'invio, passa i byte GX originali al parser.
- Se il worker non e attivo, e in una display list registrata o i dati sono troppo grandi, rimane la submission originale.
- `[vita] native_lyt_packets=true` opt-in, default OFF. `[wic-native-producers]` riporta `lyt_queued/lyt_native/lyt_fallback`.
- Test di integrazione con worker host realmente avviato: snapshot dei byte proprietari del producer, blocker sul worker, callback dopo il pacchetto, verifica che la draw venga consumata prima dell'evento successivo.

### M05.1 — correzione indice matrice GXM (non shader TEV completo)

File `aurora-main/platforms/vita/gxm/gxm_shader_gen.cpp`:

- Nel solo build **MKW_TARGET_VITA + AURORA_VITA_RENDERER_GXM**, evita `floor(selector+0.5)` per gli attributi integrali di matrice (packed F32 di interi <=24 bit e PN U8). Sul compilatore SGX543 questa operazione puo arrotondare a uno slot vicino. E' una specifica anomalia descritta da MH3 nel documento `libgxm-for-wii-ports.md`.
- Il resto delle trasformazioni e della logica TEV rimane invariato. Nessun adattamento automatico per altri port: per loro il generatore mantiene il percorso precedente.
- Il contratto host viene compilato anche con i macro WiiCompiled/GXM e verifica le espressioni emesse.

## Stato M00–M17 dopo questa tranche

| Ticket | Stato verificato / lavoro residuo |
|---|---|
| M00 | Avvio e Time Trial automatico; **gate FPS/pixel aperto** (ultima Vita ~1.16 FPS, stage1 raggiunto; stage2/HUD non validato) |
| M01 | Inventario statico completato; copertura dinamica dei percorsi GX ancora incompleta |
| M02 | **M02.1/M02.2/M02.3 implementati** (ricette multi-draw, pinning RAM, template strutturali cross-snapshot). M02.2 verificato Vita ma non accelera sensibilmente FPS; hardware M02.3 pending, producer NW4r shape/material/pose ancora da completare |
| M03 | Snapshot/revision e uniform pool preesistenti; material compiled/live patch non completo |
| M04 | Async EFB e retirement sperimentali preesistenti, default OFF; async EFB non migliora sensibilmente gli FPS |
| M05 | **M05.1 indice matrice** implementato per GXM WiiCompiled; TEV classifier, shader bank e correzione bianco/scuro non chiusi |
| M06 | Geometrie `SceGxmPrecomputedDraw` M06.1 disponibili ma poca copertura (13/816 draw); stati shader e view uniforms M06.2 aperti |
| M07 | GPU skinning e bounds conservativi da implementare dopo contratti M02/M03/M05 |
| M08 | Depth EFB, GPU feedback versionato e readback certificati da implementare |
| M09 | **M09.1 LYT quad FIFO** implementato; testo/effetti e oracle HUD stage2 aperti |
| M10 | P4/P8, BC3 e prefetch sidecar da completare, solo con decoder reference e revisione formati |
| M11 | Traduttore/ABI native precisa, flag CR/XER e rigenerazione shards non completati |
| M12 | Immutable dispatch fastpath presente, OFF per default; necessita prova device dell'ELF ARM |
| M13 | TLS/stdio/pthread Vita, ottimizzazioni SDK da implementare solo con ABI verificata |
| M14 | Clang ThinLTO/PGO rappresentativo non implementati |
| M15 | Mixer e voci NW4r::snd/AX nativi non completati |
| M16 | KCL native SoA/collision math non completato |
| M17 | Ribilanciamento worker/CPU3 e scheduler senza side effects da validare su hardware |

Le tecniche MH3 web e variable-step di simulazione **non devono essere copiate**: sono incompatibili/non pertinenti al nostro contratto Vita e alle regole di gara/ghost.

## Test e regressioni

- Aurora host 27/27 e WiiCompiled host 30/30 aggiornati M02.2
  (vedi `build/m02-mainram-aurora-ctest.log` e
  `build/m02-mainram-wic-host-ctest.log`).
- Unit e integration test delle ricette/worker/ordering in `vita_prepared_display_list_test`, `vita_frontend_translation_test`.
- Test fake FTP installer, rollback opzioni e schema TOML.
- AddressSanitizer + UndefinedBehaviorSanitizer su `vita_prepared_display_list_test`.
- AddressSanitizer + UndefinedBehaviorSanitizer su
  `vita_main_ram_snapshot_test` (nuovo, validazione della cache e del budget).
- Test shader Cg M05 compilato separatamente con macro WiiCompiled/GXM, 55.161 controlli source+CPU.
- Check `arm-vita-eabi-g++ -fsyntax-only` per unit di GX consumer, FIFO, runtime e generatore GXM.
- M02.3: `vita_prepared_display_list_test` verifica payload mutati,
  collisioni/esaurimento bucket, modifica opcode/count/NOP/VTXFMT/stride e
  layout CP; `vita_frontend_translation_test` verifica riuso template
  cross-identity, *valori dei vertici differenti* e incremento epoch in FIFO async.
  Il nuovo percorso passa
  il test mirato e AddressSanitizer+UndefinedBehaviorSanitizer.
  Suite complete in `build/mh3-m02-3-aurora-ctest.log` (27/27) e
  `build/mh3-m02-3-wic-final-ctest.log` (30/30).
- Build/deploy **della tranche M02.3** non eseguiti; la verifica hardware
  menzionata sopra e relativa a M02.2. Non affermare miglioramenti FPS o
  shader/pixel senza prova sulla console.

## Build/test A/B sulla console, manuale

Eseguire la build solo su richiesta esplicita dell'utente. Comando per l'utente:

```sh
cd /Users/robin994/Documents/Code/PSVita/wiicompiled-vita
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON \
  -DWIC_PRECOMPILED_ASSETS=ON -DWIC_RESIDENT_GEOMETRY=ON -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=OFF -DWIC_NATIVE_DIRECT_STREAM=OFF
python3 scripts/install_vita_current.py --check-local
```

Alla successiva installazione **manuale** con VitaShell FTP, verificare per prima cosa il Time Trial con `prepared_display_lists=off` e `native_lyt_packets=off`. Poi usare una opzione alla volta sullo **stesso SELF**:

```sh
python3 scripts/install_vita_current.py --recipe-only --prepared-display-lists on
# Lanciare, verificare, chiudere WiiCompiled, riaprire VitaShell FTP.
python3 scripts/install_vita_current.py --recipe-only --prepared-display-lists off

python3 scripts/install_vita_current.py --lyt-only --native-lyt-packets on
# Lanciare, verificare, chiudere WiiCompiled, riaprire VitaShell FTP.
python3 scripts/install_vita_current.py --lyt-only --native-lyt-packets off
```

Non attivare contemporaneamente altre novita per attribuire gli effetti. Le opzioni `--recipe-only`/`--lyt-only` rifiutano SELF non corrispondenti. I nuovi log `[wic-native-producers]` devono mostrare una copertura reale; senza hit non misurano un guadagno. Confrontare frame quiet, materiali/luce, copie EFB e HUD **solo in stage2**. M05.1 cambia la variante shader WiiCompiled e richiede controllo pixel rispetto alla versione precedente.

Il prossimo blocco di sviluppo ad alto impatto deve introdurre il **producer ResShp/ResTev completo (M02/M03)**, non soltanto l'ottimizzazione finale di GXM, quindi M08 depth feedback/materiali e M07 skinning. Tenere il rollback GX per qualunque scena non certificata.
