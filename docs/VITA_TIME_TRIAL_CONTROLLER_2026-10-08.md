# Time Trial: registrazione controller e prova della progressione

Aggiornato il 9 ottobre 2026. La correzione del controller è verificata sulla Vita; materiali intermittenti, HUD assente e circa 1 FPS restano aperti.

## Causa e modifica

Il bootstrap registrava il controller del giocatore 0 con ID `0x24`. Nel gioco questo indica il tipo GameCube senza il numero di porta: le routine originali interpretano la porta come `-1`. Il dispositivo collegato alla porta 0 ha ID `0x124`. La selezione del controller deattivato rilevava quindi il giocatore 0 come scollegato e attivava la pausa della sezione/scena durante l'introduzione.

`vita/game_time_trial.cpp` registra ora `0x124` tramite la stessa routine guest `SetID`, senza forzare pause, timer o stato della gara. Il rapporto `[race-progress]` aggiunge ID registrato/attivo, stato e holder del controller. Restano Mario, Kart standard M e Circuito Luigi, selezionati da `[test].time_trial=true`.

## Riproduzione e verifiche

Il test host esegue le routine originali tradotte `CalculateID`, `SetID`, `GetFirstDeactivatedPad`, `GetTypeGCN` e `GetChannel`. Verifica che il controller collegato alla porta 0 produca `0x124`, riproduce il falso scollegamento con `0x24` e verifica che il nuovo ID restituisca nessun giocatore scollegato. La fixture usa il valore di stato effettivamente scritto dal codice tradotto, non una descrizione esterna discordante.

La suite root al momento della consegna controller passa **23/23**. Passano build Vita, audit GXM, contratto pthread/SceFiber, startup asincrono e barriere begin/end. `ATTRIBUTE2=12`; SELF separato e `eboot.bin` nel VPK coincidono. I 57 sorgenti di integrazione/renderer confrontati con la candidata precedente sono identici: questa correzione non modifica il renderer.

| Artefatto controller | SHA-256 |
| --- | --- |
| SELF | `b3bf5f62bfaacbf01d34a3eb700c21d02534dc1679b2d2e5fc4f27537e5e9895` |
| ELF | `069b4e65ca38374b0ae5d3b94695dbdf845ce33f63a8241464dc070660d49375` |
| VPK | `97e86ed2c6c0e035b2afa35fa19a6dba45ad978d8196b318e0af13d20f906897` |

Bundle immutabile: `build/time-trial-controller-20261008/candidate/`. Installer della sola correzione: `build/install-time-trial-controller.py`.

## Risultato sulla Vita

La cattura in `build/render-native-20261009/capture-20261008T222838Z/` contiene il SELF installato con lo stesso hash e il config con hash `f8092058f410ecbf600cddb7efbab63f5c2e9492fb8ce2751e8402e39321bf15`.

- GX worker richiesto e avviato: `1/1`.
- Controller registrato e attivo: entrambi `0x124`.
- Pausa controller, sezione e scena: tutte `0`.
- Introduzione completata, opening pan terminati; `stage=2`, fase di gara effettiva.
- `race_frames=244` al frame 635 e `290` al frame 681.
- Cinque finestre finali: 59 frame in 52.056960 secondi, **1,133 FPS** aggregati, attribuzione pesante disattivata. È la cadenza del loop/presentazione registrata dal port; la raccolta FTP può perturbare il dispositivo e non è un benchmark A/B.

L'utente conferma che la gara avanza, che Mario/materiali alternano bianco e texture visibili, che manca il HUD e che la cadenza resta circa 1 FPS. Non è stata acquisita una nuova immagine della stessa sessione: queste osservazioni visive restano feedback dell'utente. Il log di creazione dei layer UI non dimostra la presenza del HUD nei pixel.

La precedente cattura diagnostica (`fe90c1e3...`) mostrava invece pausa persistente, timer introduttivo 22 e zero frame di gara. È conservata in `build/time-trial-freeze-20261008/capture-20261008T201504Z/` con immagine e draw del frame 300.

## Continuazione

La pausa da falso scollegamento è corretta. Il lavoro su chiamate native, cache, prewarm e prestazioni è nel [report del 9 ottobre](VITA_NATIVE_RENDER_PERFORMANCE_2026-10-09.md). Le nuove candidate prestazionali non hanno ancora un risultato hardware.
