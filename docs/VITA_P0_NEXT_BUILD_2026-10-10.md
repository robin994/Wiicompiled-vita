# WiiCompiled Vita: prossima build e test P0

Data: 10 ottobre 2026. Branch di lavoro:
`codex/restart-iso-gxm-20261005` (WiiCompiled),
`wiicompiled-integration-20261006` (Aurora submodule).

## Motivazione

La precedente build locale `build/vita-psarc` usa un cache CMake
`AURORA_VITA_ASYNC_GX=OFF`: non va installata senza ricompilazione.
Gli installer in `build/render-native-20261009` sono candidate **storiche**,
legate a SHA-256 fissi e non contengono automaticamente le ultime modifiche.

Il primo gate del [piano 60 FPS](VITA_60FPS_PLAN_2026-10-10.md) e
verificare che la correzione `VI_HLE_ProcessRetracesDeferred(8)` nel
ciclo idle (`os_scheduler.cpp`) impedisca ai callback audio di affamare
i retrace VI e che i menu possano avanzare senza disattivare il DMA.

## 1. Build manuale (niente build automatica dell'assistente)

```sh
cd /Users/robin994/Documents/Code/PSVita/wiicompiled-vita

WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON \
  -DWIC_PRECOMPILED_ASSETS=ON \
  -DWIC_RESIDENT_GEOMETRY=ON \
  -DWIC_RESIDENT_TEXTURES=ON \
  -DWIC_NATIVE_DIRECT_SUBMIT=OFF \
  -DWIC_NATIVE_DIRECT_STREAM=OFF

python3 scripts/install_vita_current.py --check-local
```

`build-vita.sh` genera ELF/SELF/VPK e `DELIVERY.json` e avvia gli audit
GXM, runtime, asset, begin/end frame e linkage. Il nuovo installer rifiuta
una `DELIVERY.json` obsoleta o un profilo sync/direct-native inatteso.

## 2. Installazione e prima sessione: menu, DMA continuo

Chiudere WiiCompiled dalla LiveArea. Aprire VitaShell e il suo server FTP
su `192.168.1.79:1337`. Da Mac eseguire:

```sh
python3 scripts/install_vita_current.py \
  --menu --audio-policy continue --diagnostics quiet
```

Il tool confronta gli altri file del VPK con `ux0:/app/WICMKW001` e,
in caso di disallineamento, richiede l'installazione iniziale del VPK
completo via VitaShell prima dell'aggiornamento incrementale. Non sovrascrive
un'app differente. Esegue staging con SHA-256 e backup del SELF e di
`Config.toml`; configura esplicitamente:

```toml
[test]
time_trial = false

[audio]
legacy_stop_dma_on_queue_full = false

[diagnostics]
enabled = false
```

Tutti gli altri parametri, inclusi i path dei dati, gli esperimenti
`[vita]`, il PSARC e i salvataggi rimangono invariati. L'app non viene
avviata dal tool. Chiudere VitaShell e avviare WiiCompiled dalla LiveArea.
Entrare in `1 Player`, verificare immagini, navigazione, audio e risposta
ai tasti, senza interpretare il suono come prova di frame presentati.

Dopo il test chiudere **di nuovo** WiiCompiled, avviare FTP VitaShell,
raccogliere la sessione A, senza scrivere sul dispositivo:

```sh
python3 scripts/capture_vita_current.py --label p0-menu-continue
```

L'output, ignorato da Git, si trova in
`build/vita-psarc/device-runs/p0-menu-continue-<timestamp>/`. Contiene
`runtime-session.log` (solo ultimo boot), la configurazione testata,
l'hash SHA-256 del SELF installato, la dimensione e l'MDTM del PSARC.
Se compare nero mantenere la scena aperta per almeno 20 secondi prima
di raccogliere il log, per includere le sonde dello stall.

## 3. Solo se il nero persiste: A/B dello stesso SELF

Con VitaShell FTP ancora attivo, modificare **esclusivamente** la policy
audio, senza reinstallare, ricompilare o cambiare altri parametri:

```sh
python3 scripts/install_vita_current.py \
  --audio-only --audio-policy legacy
```

Chiudere VitaShell, riaprire WiiCompiled e ripetere identico percorso.
Al primo overflow, il DMA verra fermato dalla policy legacy: **l'audio
potrebbe interrompersi**, come nella build precedente alla fix audio.
Questo e solo un test causale e non il comportamento finale desiderato.

Riaprire VitaShell FTP e catturare la sessione B:

```sh
python3 scripts/capture_vita_current.py --label p0-menu-legacy
```

Ripristinare infine la policy audio corrente (mantiene identico SELF):

```sh
python3 scripts/install_vita_current.py \
  --audio-only --audio-policy continue
```

Interpretazione:

- **A funziona:** VI fairness + callback continui compatibili con i menu;
  procedere al gate Time Trial senza attivare il rollback legacy.
- **A nero, B funziona:** regressione dipendente dalla consegna audio dopo
  l'overflow; isolare thread callbacks, tempo di polling e scheduling.
- **Entrambi neri:** non attribuire la regressione solo all'audio; acquisire
  `[gx-stall]`, `[guest-stall]`, `[psarc-stall]`, immagini e confronto
  fra i percorsi frame begin/end. Non forzare thread e non disattivare VI.

## 4. Gate Time Trial e prestazioni quiet

Dopo che il menu e visibile o dopo aver confermato la stabilita della
scena/controlli Time Trial, con VitaShell FTP:

```sh
python3 scripts/install_vita_current.py \
  --time-trial --audio-policy continue --diagnostics quiet
```

Avviare dalla LiveArea, attendere la fine dell'intro e verificare che
Mario/kart, HUD, texture, camera e comandi siano **effettivamente corretti**;
solo poi usare la cadenza come dato di performance. Dopo la prova, uscire,
attivare VitaShell FTP e acquisire:

```sh
python3 scripts/capture_vita_current.py --label p1-timetrial-quiet
python3 scripts/analyze_vita_frame_budget.py \
  "$(ls -dt build/vita-psarc/device-runs/p1-timetrial-quiet-*/runtime-session.log | head -n 1)" \
  --stage 2 --unpaused
```

Sono necessarie piu catture **della stessa scena** per confrontare baseline
e profili native-submit/native-stream; lo script riporta media, p50/p90/p95
delle medie di finestra, ma non un p95 vero di singoli frame.

## Verifiche locali

```sh
python3 scripts/test_install_vita_current.py
python3 scripts/test_capture_vita_current.py
python3 scripts/test_vita_frame_budget.py
ctest --test-dir build/host --output-on-failure
```

Installer e cattura sono testati con FTP simulato, inclusi staged readback,
rollback doppio, controllo della provenienza del pacchetto e conservazione
delle impostazioni estranee. Il device reale e il rendering GXM non vengono
testati da queste prove host.

Nessuna nuova build Vita o installazione hardware e stata lanciata
automaticamente durante la preparazione di questi strumenti.
