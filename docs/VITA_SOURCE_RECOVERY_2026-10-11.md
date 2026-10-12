# Recupero sorgenti WiiCompiled / Aurora-Vita - 11 ottobre 2026

## Evento

Durante una pulizia delle build sono stati rimossi accidentalmente otto file
tracciati della repository principale e l'intera directory di lavoro del
submodule `aurora-main/`. **Non e stato eseguito alcun reset o clean Git**.

La directory Git interna `.git/modules/aurora-main` era ancora integra:
il branch `wiicompiled-integration-20261006` e il pin della repository
principale indicavano entrambi `7a69a51bf392c62e37aca5926f0dbd27cb0c128b`
(10 ottobre ore 15:32). Il vecchio stash del submodule e stato preservato.

Ripristinati da `HEAD` **solo i file effettivamente mancanti**:
`.gitignore`, `.gitmodules`, `README.md`, `THIRD-PARTY-NOTICES.md`,
`build-vita.sh`, e tre `crashdumps/analysis-*.txt`.
Submodule reidratato senza rete usando il DB oggetti locale e il suo indice.
Nessun file tracciato rimane cancellato, e le modifiche della repository
principale che erano presenti al momento del recupero sono rimaste intatte.

## Cosa e successo alle modifiche Aurora non committate

Le modifiche locali del giorno 11 ottobre dopo `7a69a51` **non erano
presenti nell'object database Git**. Gli snapshot di progetto trovati in
`build/` risalgono al massimo al 9 ottobre; non esistono snapshot APFS
locali e non e stata trovata una cronologia di VS Code per i file
`wiicompiled-vita/aurora-main/`. Non si puo dunque dichiarare una
ricostruzione byte-per-byte del sorgente che ha generato il VPK delle 17:07.

Sono state ricostruite in modo mirato, dai contratti e documenti locali,
solo le funzionalita necessarie per il debugging di fedelta:
- API di cattura con filtri per draw/hash, stage TEV, handle texture.
- Fingerprint FNV1a 64 (massimo 1 MiB) su sorgenti GX encoded, tabella
  texture/handle limitata a 4096 record e attivazione prima del frame 300
  se una cattura esplicita e richiesta.
- Copy EFB 1:1 sperimentale `gxm_efb_copy_snap_1to1`, **OFF per default**,
  che non modifica il rettangolo del `clear` GX.
- Contratto delle opzioni del progetto principale per evitare che i
  precedenti riferimenti C++ risultino privi di membri.

**Non recuperate ne convalidate come funzionanti**: per-target scene budget
telemetry, precomputed geometry, native LYT packet path, delayed GXP disk
persistence, deferred EFB targets e native primitive path. Alcuni campi
del `BackendConfig` esistono per compatibilita di compilazione, ma
**non implementano queste funzionalita**. Non utilizzare gli interruttori
corrispondenti per confronti prestazionali finche non sono implementati e
testati nuovamente. `screen_bounds_valid` della nuova sonda resta
attualmente 0: non attribuire un singolo pixel finale a un draw sulla
base degli handle o della firma TEV.

## Evidenza e test

- Test host `vita_view_draw_capture_test.cpp`: PASS.
- Controlli sintattici VitaSDK `gxm_renderer.cpp`, `game_aurora.cpp`,
  `aurora_vita_draw_sink.cpp`, `aurora_vita_backend.cpp`: PASS.
- Test statici `vita_efb_empty_source_test.cpp`: PASS.
- Test sintattico `vita/tests/render_config_test.cpp`: PASS.
- `git diff --check` root e submodule: PASS.
- **Nessun VPK/SELF costruito**, nessuna installazione PS Vita reale,
  nessun commit/push.

Il SELF gia installato in Vita3K proviene dal VPK completo delle 17:07,
hash `8c8e4e93c7ed76495c4bbee7fc69f939857623225296b8ed141d0813c69af200`,
ed e **precedente alla cancellazione**: puo essere usato come strumento
diagnostico, non come prova che i sorgenti recuperati riproducano lo stesso
binario.

Il diff Aurora ricostruito e conservato anche in
`docs/AURORA_SOURCE_RECOVERY_2026-10-11.patch` (include il nuovo header
`vita_texture_provenance.hpp`). E uno snapshot di emergenza delle sole
modifiche ricostruite rispetto al commit `7a69a51`, NON il diff storico
perduto del giorno 11. Applicarlo a un submodule gia modificato richiede
una revisione manuale.

## Vita3K

Il runner diagnostico e stato interrotto dal collegamento prima del frame
300, senza creare la coppia di CSV aggiornata. Vita3K non e in esecuzione.
`Config.toml` e `config.yml` sono stati ripristinati dai file
`build/vita3k-trace/visual-fidelity-20261011/before-*` dopo confronto
byte-per-byte con le varianti temporanee. I loro SHA256 ripristinati sono
`22fcd8e8443729cb579dc7d9abc373915b8d85a71e538696519ef86a6b04ac2c`
e `ebd4c77db19bf3c64ca36af79b6df557736ec65da3ed8a264ea2df78b88be91a`
rispettivamente. I vecchi frame-300 non sono stati sovrascritti.

## Prossimi passi (prima rendering, poi FPS)

1. Raccogliere tramite il SELF gia installato i CSV sincronizzati
   `frame-300-draws.csv` e `frame-300-textures.csv` e lo screenshot
   della finestra Vita3K; il PPM readback GXM e noto come non affidabile.
2. Confrontare il corpo Mario PAL: TEXMAP 1/0/2, `lm_0`,
   `mario_all` e `lm_1` contro sorgente BRRES.
3. Isolare separatamente TEV/lightmap e depth EFB (depth copy e ancora
   non implementata) rispetto al generale oscuramento della scena.
4. Verificare l'HUD in `stage=2`, non nella sola introduzione.
5. Ricostruire le altre funzioni Aurora-Vita perse prima di una nuova
   build di confronto; mantenere sperimenti disattivati fino al test
   di equivalenza visuale. **Nessuna build Vita automatica**.

## Nuova prova Vita3K alle 23:41 CEST (esito bloccato)

E stata riutilizzata la build **gia installata**, stesso SHA del SELF
`8c8e4e93c7ed76495c4bbee7fc69f939857623225296b8ed141d0813c69af200`.
Il runner con diagnostica frame300, log shader/uniform temporaneamente ridotto,
e terminato dopo 36 secondi **prima del frame 1** (`rc=-5`): nessun nuovo
`frame-300-draws.csv` o `frame-300-textures.csv`.

Crash report macOS `~/Library/Logs/DiagnosticReports/Vita3K-2026-10-11-234155.ips`:
`EXC_BAD_ACCESS / SIGTRAP` sul thread `WICMKW001`, con
`call_import(EmuEnvState&, CPUState&, unsigned int, int)+1108` nello stack.
Il report delle 11:27 mostra **lo stesso stack e lo stesso segnale**.
Questa coincidenza non identifica da sola il motivo primario del fault;
una futura diagnosi deve risalire all'import guest e allo stato CPU.
Il log runtime della nuova sessione termina dopo `EXIUnlock_80169260`.

Il runner ha registrato `RESTORED_CONFIG_AND_DIAGNOSTIC_FILES=TRUE`:
`config.yml` e `Config.toml` sono stati ripristinati byte per byte,
Vita3K e terminato, e i dati originali della precedente cattura sono intatti.
Non ripetere il medesimo run senza cambiare una variabile controllata:
la cattura del frame 300 e attualmente bloccata dal boot Vita3K, non da
una divergenza TEV gia dimostrata.
