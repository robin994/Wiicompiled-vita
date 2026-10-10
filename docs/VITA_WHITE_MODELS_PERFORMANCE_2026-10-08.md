# Mario bianco, HUD assente e circa 1 FPS — 8 ottobre 2026

## Richiesta e conclusione attuale

L'utente riferisce Mario e alcuni elementi dello scenario bianchi, UI non renderizzata e prova a tempo visivamente ferma a 1 FPS. La domanda iniziale era se aumentare il pool CDRAM.

**I dati acquisiti non giustificano un aumento del pool.** La sessione usa un pool da 64 MiB, con picco di 37,77 MiB e zero allocazioni di ripiego. I modelli bianchi e il HUD assente restano problemi aperti; non vengono dichiarati corretti da questa candidata.

## Identità dell'eseguibile effettivamente installato

- SELF riletto via FTP: `7e62624d0f9488a1cb7373f69ed59ec1e2c17d9c903296fd7295013fed9a1fe6`.
- Marker: `psarc-resident-assets-async-efb-present-fix-20261008`.
- Config.toml: `4bec11b8100a6d813f86d89e7e95c424622cdc7aa91beaa13330786fcf5d07fa`.
- `[test].time_trial = true`: Mario, Kart standard M, Circuito Luigi.
- Acquisizione in sola lettura: `build/white-models-performance-20261008/capture-20261008T181958Z/`.
- Distinte: `CAPTURE.json`, `FRAMEBUFFER_CAPTURE.json`; analisi: `build/white-models-performance-20261008/ANALYSIS.json`.

Sono stati letti eseguibile, configurazione, coda del log, memoria, coverage, trace e due immagini già esistenti. Nessun file della Vita è stato modificato e nessun lancio è stato inviato da questa attività.

## Memoria e texture

Il file della memoria contiene più avvii: i seguenti valori appartengono soltanto all'ultimo blocco `before-init` e ai suoi campioni successivi.

| Misura | Valore |
| --- | ---: |
| Pool CDRAM | 64 MiB |
| Uso nell'ultimo campione | 35,78 MiB |
| Picco del pool | 37,77 MiB |
| Margine rispetto al picco | 26,23 MiB |
| CDRAM libera fuori dal pool | 38 MiB |
| Allocazioni di ripiego | 0 |

Nel frame completato 300 ci sono 6.260 draw e 38.901 vertici, **zero fallback di texture**, zero miss/upload texture e zero overflow dell'arena. Nei campioni successivi le snapshot delle texture restano a 135 entry, circa 0,883 MiB, con upload e miss invariati e zero eviction. Il budget da 8 MiB delle snapshot è una cache specifica, distinta dal pool CDRAM totale.

Non compaiono errori espliciti di allocazione/copia GXM nella sessione acquisita. Questi dati non provano che ogni materiale sia corretto: indicano che il bianco osservato non è accompagnato dal fallback di texture mancante contato dal renderer. Il prossimo controllo visivo riguarda texture effettivamente campionate, TEV, illuminazione e copie EFB. Il contatore `unsupported=3826` include funzionalità con resa approssimata, soprattutto il confronto di profondità prima del campionamento: non equivale a 3.826 texture mancanti.

## Cadenza e limiti della diagnosi

I cinque ultimi intervalli, dal frame completato 303 al 365, contano 62 presentazioni in 51,999874 secondi: **1,192 Hz**, senza catture in quegli intervalli. Confermano la lentezza riferita, ma la build ha attribuzione grafica/CPU attiva. Non costituiscono un benchmark senza diagnostica.

Il frame campionato 299/300 riporta 23 scene, 12 copie EFB e circa 367,7 ms nel percorso FIFO. Le 1.124 submission di display list, per circa 244 KiB, riportano 338,2 ms. **Quel timer include `drain()` e l'attesa di spazio nella coda**: non misura soltanto `memcpy`, e non si somma al tempo FIFO o agli altri tempi annidati. Nessuna lista del campione proviene da CDRAM. L'invio alla display queue dura 30 µs; non è una misura del tempo di esecuzione GPU.

Il contatore dei frame prosegue, ma non prova che la logica della gara o l'immagine cambino correttamente. I frame 30 e 180 acquisiti sono neri e precedono il rendering della gara; il frame 600 remoto è del precedente eseguibile e viene escluso. La conferma visiva di Mario bianco e HUD assente viene dall'utente, non da una cattura di gara già disponibile.

## Modifica consegnata per isolare il problema

La candidata aggiunge due opzioni al **Config.toml esistente**:

```toml
[diagnostics]
enabled = false
```

`enabled = false` disattiva attribuzione del renderer, coverage, trace, diagnosi delle decodifiche, contatori CPU/native-call/heap e catture framebuffer automatiche. Rimane un report di cadenza a bassa frequenza (`[runtime-cadence]`), contato dopo il completamento delle presentazioni. I wrapper diagnostici sono ancora compilati, ma non attivano i contatori; i log essenziali e i controlli di stallo rimangono. Il confronto misura l'effetto delle funzioni diagnostiche abilitate, senza promettere overhead nullo.

Per ottenere un'immagine della gara:

```toml
[diagnostics]
enabled = true
capture_frame = 300
```

Un `capture_frame` esplicito sostituisce le catture automatiche 30/180/600 e si riferisce al numero di **frame completato**, indipendentemente dal numero di frame con draw. Zero disattiva le catture. L'assenza delle opzioni conserva il comportamento precedente; `enabled = false` prevale sulla richiesta di cattura.

Il pool resta a 64 MiB. Le impostazioni di rendering, materiali, shader, cache, clock e Time Trial restano quelle della build acquisita. I sette sorgenti nativi della precedente correzione EFB sono byte per byte identici: `RENDERER_UNCHANGED.json`. La patch limitata a questa attività è in `candidate/source-snapshot/diagnostics-only.patch`.

## Validazione e identità della candidata

- Build Vita: PASS.
- Suite host esistente: 22/22 PASS.
- Test del parser Config.toml, incluse nuove opzioni, tipo errato e sezione errata: PASS.
- Audit GXM puro: 45.608 simboli, nessuna dipendenza GL/vitaGL/vita2d.
- Audit runtime pthread/SceFiber e barriere asincrone begin/end: PASS.
- SELF compilato identico a quello nel VPK; SFO `ATTRIBUTE2=12` verificato.
- Installer: conservazione delle opzioni estranee/commenti, idempotenza e ripristino di entrambi i file dopo errore simulato nell'attivazione della configurazione: PASS.

Marker: `psarc-resident-assets-async-race-diagnostics-20261008`.

| Artefatto | SHA-256 |
| --- | --- |
| SELF | `96dcfa4a4f46575add3693596ea1b9ce2454bda51c2a304e9ce00b40d4d70bb1` |
| ELF | `b7c7beb0df141b488937449b1c8422ce369991f765e0f888445961983d06458d` |
| VPK | `2de7cde46072da06ec6d3e47c6bf927fa06f3b7677a84ed887873f8567ef92dc` |

Bundle: `build/white-models-performance-20261008/candidate/`. Al termine della consegna iniziale questa candidata non era stata installata o provata sulla Vita. La verifica successiva dell'eseguibile installato, della schermata e dei log è riportata in fondo a questo documento. Build e test, da soli, non stabiliscono FPS migliori, modelli corretti, HUD visibile o gara giocabile.

## Installazione e confronto sul dispositivo

Chiudere WiiCompiled e attivare FTP VitaShell su `192.168.1.79:1337`. Primo avvio con Time Trial e diagnostica disattivata:

```sh
python3 "/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/install-race-diagnostics.py" --time-trial --quiet
```

Avviare il gioco e osservare se cambia la cadenza e se la gara progredisce. Un incremento stabilisce una parte del costo diagnostico; non prova che i modelli siano corretti. Se resta circa 1 FPS, il costo continuo di gioco/rendering richiede attribuzione separata.

Secondo avvio, dopo aver richiuso il gioco e riattivato FTP, per la cattura dei modelli e del HUD:

```sh
python3 "/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/install-race-diagnostics.py" --time-trial --diagnostic
```

Al frame completato 300 viene scritto `ux0:data/wiicompiled-vita/aurora/diagnostics/frame-300.ppm`; il log riporta l'esito. I dati di questa seconda esecuzione sono diagnostici. Per tornare al confronto della cadenza ripetere il comando con `--quiet`.

Lo script verifica il pacchetto e gli altri file dell'app registrata, salva eseguibile/configurazione precedenti, trasferisce su nomi temporanei e verifica i byte prima dell'attivazione. Mantiene backup `before-race-diagnostics-*` e ripristina entrambi i file se l'attivazione fallisce. Non modifica PSARC/NAND e non avvia l'app.

## Evidenza successiva: schermata precedente al pull Aurora

L'utente ha fornito `2026-10-08-210330.png` specificando che proviene dalla build precedente all'allineamento di Aurora. Una nuova acquisizione in sola lettura conferma l'identità e conserva i dati in:

`build/white-models-performance-20261008/screenshot-evidence-20261008T191623Z/`

- SELF installato: `96dcfa4a4f46575add3693596ea1b9ce2454bda51c2a304e9ce00b40d4d70bb1`.
- Marker: `psarc-resident-assets-async-race-diagnostics-20261008`.
- Revisione Aurora registrata all'avvio: `28512bd75531508276be1582882145ea021151eb-dirty`.
- Config della sessione diagnostica: `038fb313f05409c556aa233e04daf33d86ea1d8881ef0b9e20f1de83a1a5682d`, con `time_trial=true`, `diagnostics.enabled=true`, `capture_frame=300`.
- Il PNG coincide **pixel per pixel** con `frame-300.ppm` riletto dalla Vita; il log della stessa sessione conferma la cattura del frame completato 300. SHA-256 dei pixel RGB: `6dc3393d979666dfd0a8ff6cd22bf20f32b0d5fc1829f7b73693c939e96675c9`.
- L'eseguibile della candidata dopo il pull ha SHA-256 `7c6409fa3425a0568a7f782911e4db8d305bcb8387daee30c0ea12fef8bcb6f6`: questa immagine è il riferimento precedente all'aggiornamento.

La scena è orientata correttamente in questa cattura. Il kart e molti elementi del circuito hanno colori e texture; Mario è prevalentemente chiaro/bianco e alcuni elementi dello scenario appaiono bianchi. Non è visibile un HUD. La vista frontale può appartenere all'introduzione della gara: una singola immagine non determina la fase di gioco, né prova che il HUD non venga disegnato durante una gara attiva.

Il frame 300 contiene 6.260 draw, 38.901 vertici, zero fallback di texture, 23 scene e 12 copie EFB. Nell'ultima sessione diagnostica il pool CDRAM è 64 MiB, il picco è **37,707 MiB** e le allocazioni di ripiego sono zero. Il contatore di fallback non esclude texture errate, texgen/UV errati, TEV, illuminazione o copie EFB non corrette; non indica esaurimento del pool.

Il log conserva anche una precedente esecuzione dello **stesso SELF**, con `[diagnostics-config] enabled=0` e `attribution=0`. Negli ultimi quattro intervalli registrati, dal frame completato 237 al 298, conta **61 presentazioni in 41,480787 secondi: 1,47056 Hz**. Il problema di cadenza persiste anche disattivando la diagnostica pesante. Questa è una misura aggregata di una singola esecuzione esistente, senza un protocollo ripetuto di riscaldamento; non è un confronto con la nuova candidata, una distribuzione dei tempi frame o una prova di avanzamento della logica di gara.

Le sessioni quiet e diagnostica sono state separate in `quiet-session.log` e `diagnostic-session.log`; identità, confronto dei pixel, contatori e limiti sono in `CAPTURE.json` e `ANALYSIS.json`. In questa acquisizione non sono stati modificati file della Vita.

Il prossimo confronto usa la candidata dopo il pull, mantenendo il pool a 64 MiB e gli esperimenti Aurora disattivati. Se i difetti visivi persistono, occorre identificare i draw/materiali coinvolti e verificare separatamente fase della gara, HUD, texture effettivamente campionate, texgen, TEV/illuminazione e copie EFB.

## Riscontro dopo il pull e diagnostica dello stato gara

Il successivo test dell'utente conferma vista frontale ferma, materiali ancora bianchi e scena scura. Una nuova lettura del SELF installato conferma la candidata `7c6409fa…`, con diagnostica disattivata; le presentazioni continuano a circa 1,13–1,55 Hz. La fase e i contatori della logica di gara non erano ancora registrati.

È stata preparata una candidata distinta con osservazione dello stato originale di gara e cattura completa dei draw di un frame. Evidenza, limiti, hash, verifiche e comando di installazione sono in [VITA_TIME_TRIAL_FREEZE_2026-10-08.md](VITA_TIME_TRIAL_FREEZE_2026-10-08.md). È una candidata diagnostica, senza una correzione del blocco o dei materiali già verificata sulla console.
