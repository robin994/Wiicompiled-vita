# WiiCompiled Vita — piano verificabile per il target 60 FPS

Data: 10 ottobre 2026. Port PAL RMCP01, runtime ARM nativo e backend Aurora-Vita GXM.
Branch WiiCompiled: `codex/restart-iso-gxm-20261005`; Aurora: `wiicompiled-integration-20261006`.

## Confronto 3DS: potenziale, non equivalenza prestazionale

Il 10 ottobre 2026 sono apparsi nuovi filmati e testimonianze relative a una versione
3DS di Mario Kart Wii/WiiCompiled. Diversi utenti descrivono circa 30 FPS su Old 3DS
e prestazioni prossime a 60 FPS su New 3DS, con limitazioni a VS, difetti audio/animazione
e crash. I post non forniscono **un repository sorgente verificato, una cattura FPS
strumentale o una comparazione della stessa scena alla stessa risoluzione**.
Il file CIA condiviso potrebbe incorporare dati di gioco: non viene scaricato,
decompilato o incluso in questo repository.

Fonti da verificare in futuro:
- https://www.reddit.com/r/3dshomebrew/comments/1wxjjrd/mario_kart_wiicompiled_3ds_testers_wanted/
- https://www.reddit.com/r/MarioKartWii/comments/1x24gu9/mario_kart_wii_for_3ds_first_impressions/
- https://www.reddit.com/r/3dsqrcodes/comments/1x27jat/mario_kart_wii_unofficial_3ds_port/
- https://github.com/patchzyy/Wiicompiled (base di ricompilazione originale)

Il confronto "Vita superiore a 3DS" non implica una equivalenza 1:1:
la superficie 960x544 della Vita richiede 522.240 pixel per immagine, contro
96.000 pixel per la schermata 400x240 del 3DS (circa **5,44 volte**).
Inoltre le GPU e i percorsi GX/PICA200/GXM, l'overhead delle state transitions,
la compatibilita dei materiali e le frequenze realmente usate non sono equivalenti.
Sono necessarie misure sullo **stesso workload**, con resa corretta, per usare il
risultato 3DS come obiettivo quantitativo.

I 60 FPS **reali** richiedono una presentazione completa entro **16,667 ms**
senza duplicare frame; l'interpolazione non basta a dimostrare un gameplay
nativo a 60 FPS. L'obiettivo resta valido, non ancora dimostrato.

## Baseline locale: separare gara attiva da diagnostica e pausa

Strumento: `scripts/analyze_vita_frame_budget.py`.

| Cattura e situazione | Frames | Finestra | Cadenza | Media frame | Diagnostica | FIFO nel frame |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| `build/render-native-20261009/capture-20261008T222838Z/current-session.log`, Time Trial stage=2, non in pausa | 59 | 52,06 s | **1,133 FPS** | **882,32 ms** | No; `runtime-cadence` | Non misurato |
| `build/time-trial-freeze-20261008/capture-20261008T201504Z/runtime-tail.log`, frame >=600, Time Trial in pausa | 71 | 60,76 s | **1,168 FPS** | **855,81 ms** | Si; `runtime-profile` | **375,76 ms/frame** elapsed annidato |

Le due catture sono differenti (build, scena, pausa e livello diagnostico);
**non sono un confronto A/B**. L'indice di rallentamento rispetto ai 16,667 ms
e circa 52,9x per la gara attiva e 51,3x per la scena in pausa. Non e una
previsione lineare del lavoro necessario, non identifica singolarmente CPU e GPU.

Le finestre `runtime-profile` hanno `fifo_us` e `native_request_us`, entrambi
compresi nel tempo wall: non si sommano. Le finestre `runtime-cadence` sono
piu leggere, ma **non** misurano i costi interni; lo strumento li restituisce
come `null`, anziche inventare valori zero. Il report espone una percentuale
p95 **delle medie di finestra, pesata per i frame**: non e il p95 dei tempi dei
singoli frame. `performance_proven_in_game=false` finche immagini, input,
simulazione e presentazione non sono validati nella stessa sessione.

Per riprodurre:

```sh
python3 scripts/test_vita_frame_budget.py

# Gara in corso, diagnostica disattivata.
python3 scripts/analyze_vita_frame_budget.py \
  build/render-native-20261009/capture-20261008T222838Z/current-session.log \
  --stage 2 --unpaused --json build/bench-time-trial-quiet.json

# La pausa e esplicitamente visibile e segnalata nel report.
python3 scripts/analyze_vita_frame_budget.py \
  build/time-trial-freeze-20261008/capture-20261008T201504Z/runtime-tail.log \
  --min-frame 600
```

## Sequenza di lavoro — gate prima della velocita

**P0: correctness e stabilita, senza scorciatoie.**
- Menu `1 Player` funzionante, filmati/THP/AVC stabili, audio e callback
  recuperano dopo una coda temporaneamente piena. Il vecchio comportamento
  `[audio] legacy_stop_dma_on_queue_full` e **solo un test A/B**, non una
  soluzione audio. Fare due sessioni con stesso SELF e sole due opzioni TOML,
  isolando questo fattore.
- Gare Time Trial guidabili, scena dinamica, Mario/kart correttamente texturizzati,
  HUD presente e light map/EFB/clear/scene lifetime corretti.
- Non disattivare correzioni di alpha, depth o memorie solo per guadagnare FPS.
  Copiare frame in video senza aggiornare il gioco non conta.

**P1: baseline ripetibile.**
- Ogni prova deve identificare SHA256 SELF, SHA256 PSARC, cache shader usata,
  clocks/CPU affinity, configurazione, scena, personaggio/kart/pista, intro
  conclusa, diagnostica e flag, numero di frame misurati dopo warmup.
- Tre acquisizioni minime della stessa scena attiva (piu cattura pixel
  **separata**, non nella finestra performance), riportando media e variazione.
- Usare `runtime-cadence` per FPS quiet, `runtime-profile` per attribuzione,
  e `[race-progress]` per fase/pause; associare ogni finestra alla stessa
  identita di build. Non confondere i momenti di preriscaldamento con il steady state.

**P2: ridurre il carico per draw.**
- In una scena precedente sono stati osservati **6.260 draw/frame**,
  di cui **4.669** con specializzazione materiali nativi ma soltanto **13 hit**
  sulle geometrie GPU nel campione. Questo suggerisce che il costo
  dell'adapter GX/state/CPU puo essere maggiore del costo del singolo triangle.
- Confrontare `reference` -> `native-submit` -> `native-stream` a pari
  workload usando le candidate gia preparate in `build/render-native-20261009`.
  Le prime due candidate differiscono un solo flag. Il percorso stream deve
  conservare ring lifetime, draw ordering, indici e GPU memory barriers.
- Misurare al netto delle callback/attese, raggruppare draw solo se compatibili:
  **mai attraverso copy EFB, clear, cambio render target, barriera o cambio
  stato**. Restano disattivate di default `draw_batching`,
  `prepared_display_lists`, `immediate_draw_view` finche la verifica pixel
  non supera i test.
- Prima candidata con batching GXM a memoria staging non ancora flushata,
  protezione per gli altri consumer Aurora e diagnosi white fallback/HUD:
  [VITA_GXM_PENDING_BATCH_2026-10-10.md](VITA_GXM_PENDING_BATCH_2026-10-10.md).
  Non attribuire un guadagno alla compattazione prima dell'A/B su hardware.
- **Risultato hardware 10/10:** la build Aurora `a4f93c3` era realmente
  installata e `draw_batching=1`, ma l'utente non osserva alcun miglioramento:
  **non ripetere questo esperimento alla cieca**. In quiet i messaggi
  `[wic-draw-health]` erano soppressi. La nuova sonda campionata e il runbook
  sono in
  [VITA_RENDER_BUDGET_PROBE_2026-10-10.md](VITA_RENDER_BUDGET_PROBE_2026-10-10.md);
  separare GXM draw, copie EFB, preparazione GX e backpressure prima di
  scegliere un'altra ottimizzazione. I 6.260 draw/frame sono di una scena
  precedente, non un conteggio della nuova build.

**P3: GPU fixed vertex e material preparation.**
- Asset statici e shading precompilabili nel PSARC; animazioni e skinning
  dinamici richiedono dati sincronizzati e invalidazione della cache.
- Ottimizzare l'upload uniforme per revisione e il reuse texture soltanto
  con prove di equivalenza del payload e della visualizzazione su piu frame.
  Shader binary preload/prewarm rimuove compilation spikes, non 6.260
  submission per frame.

**P4: multithreading utile.**
- Non distribuire 2.547 job piccoli di vertici per frame su helper se il
  guadagno e inferiore al costo di dispatch/sync. Le metriche precedenti
  riportavano lane helper inattive. Usare chunk piu grandi e verificare
  laneItems/laneWorkUs effettivi, lasciando sceGxm sul thread proprietario.
- Non abilitare CPU3 o modificare le policy di altri Aurora consumer senza
  test e senza gate su WiiCompiled.

**P5: 60 FPS.**
- Gate di performance: **tempo medio e p95 di finestra sotto 16,667 ms**
  in tre run non diagnostici, stessi output e gara giocabile senza regressioni.
  Dopo questa soglia raccogliere i tempi dei singoli frame per un vero p95
  e distinguere 60 Hz simulazione, presentazione e input.
- Un 30 FPS stabile con gameplay corretto e step intermedio, **non**
  autorizza dichiarare raggiunto l'obiettivo 60.

## Stato modifica e pubblicazione

Il runbook operativo del prossimo test P0, con comandi build/deploy VitaShell,
backup, policy audio A/B, acquisizioni SHA-256 e successivo Time Trial quiet,
e [VITA_P0_NEXT_BUILD_2026-10-10.md](VITA_P0_NEXT_BUILD_2026-10-10.md).

Lo strumento e i test sono sorgenti host, non generano VPK e non richiedono
caricare contenuti Nintendo. Le due catture e i report JSON restano sotto
`build/`, esclusi dal versionamento. Non sono state eseguite build Vita
automatiche o installazioni in questo intervento.
