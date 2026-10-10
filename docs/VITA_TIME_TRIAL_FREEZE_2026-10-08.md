# Time Trial fermo e materiali bianchi — 8 ottobre 2026

## Esito del test dopo il pull

L'utente conferma che la build dopo l'aggiornamento Aurora non cambia il comportamento: la vista frontale resta ferma, Mario e alcuni elementi conservano il difetto delle texture e la scena appare scura. Il HUD risultava assente nel precedente screenshot. La regressione non è risolta.

È stata verificata in sola lettura l'installazione presente sulla Vita, distinta dal precedente screenshot:

- SELF installato: `7c6409fa3425a0568a7f782911e4db8d305bcb8387daee30c0ea12fef8bcb6f6`.
- Marker: `psarc-resident-assets-async-aurora-upstream-20261008`.
- Aurora: `27357eb889cd0a13d904c8bc708db9b3d162ed0c-dirty`.
- Config: `f8092058f410ecbf600cddb7efbab63f5c2e9492fb8ce2751e8402e39321bf15`; Time Trial attivo, diagnostica disattivata.
- Il log conferma Mario, Kart standard M, Circuito Luigi, modalità 2 e sezione `0x1f`.
- Dopo il caricamento, la cadenza osservata resta circa **1,13–1,55 presentazioni/s**. Il contatore avanza fino al frame completato 362 nella sessione acquisita.

Evidenza: `build/time-trial-freeze-20261008/capture-20261008T193309Z/`, con eseguibile installato, config, coda del log, sessione corrente separata e `CAPTURE.json`. In questa acquisizione non sono stati scritti file della Vita.

Le presentazioni completate indicano che il percorso di submission/present continua a lavorare. Non stabiliscono che la logica di gara, la camera o la scena visibile avanzino. Non escludono un'attesa GPU costosa e non sono una misura della durata della sola GPU.

La misura precedente del pool CDRAM, legata al SELF prima del pull, era 37,707 MiB di picco su 64 MiB e zero allocazioni di ripiego. Non dimostra esaurimento; non è stata riclassificata come una misura della nuova sessione quiet. Il pool rimane a 64 MiB.

## Controllo del percorso originale

Il codice tradotto PAL distingue il disegno dagli aggiornamenti della gara:

- `GameScene::calc`, `8051B3C8`, aggiorna lo stato di pausa.
- `RaceScene::isPaused`, `80554E14`, legge HomeMenuMgr `+0x4c` e lo stato della sezione `+0x38b`.
- `RaceScene::OnCalc`, `80554E6C`, può saltare gli aggiornamenti della gara quando `GameScene +0x2540` segnala pausa.
- `RaceCameraMgr::HasEveryOpeningPanEnded`, `805A8FBC`, controlla il bit 8 dell'opening pan di ogni camera.

Il campo `+0x2540` è uno stato di pausa; non è un flag di inizializzazione. Una pausa permanente è un'ipotesi da misurare, non una diagnosi già provata. Anche una sequenza introduttiva estremamente lenta o una presentazione che ripete uno stato precedente restano possibili.

Le strutture sono state confrontate con [RaceInfo di Pulsar](https://github.com/MelgMKW/Pulsar/blob/820ad929c3c7141a0396692d8b0896d1546240fd/GameSource/MarioKartWii/Race/RaceInfo/RaceInfo.hpp), [Section](https://github.com/MelgMKW/Pulsar/blob/820ad929c3c7141a0396692d8b0896d1546240fd/GameSource/MarioKartWii/UI/Section/Section.hpp), [RaceCamera](https://github.com/MelgMKW/Pulsar/blob/820ad929c3c7141a0396692d8b0896d1546240fd/GameSource/MarioKartWii/3D/Camera/RaceCamera.hpp) e [RaceScene di MKW-SP](https://github.com/mkw-sp/mkw-sp/blob/d2511168b4f102ac871bf0b6e9c089f0f8bfeeb0/payload/game/scene/RaceScene.cc). Gli indirizzi effettivi sono verificati nel codice tradotto locale. Le copie consultate sono conservate in `build/time-trial-freeze-20261008/reference/`.

## Candidata diagnostica

È stata preparata una candidata distinta: `psarc-resident-assets-async-race-progress-20261008`.

1. Quando `[test].time_trial=true`, aggiunge `[race-progress]` al rapporto periodico già esistente, anche con diagnostica pesante disattivata. Legge sezione, transizione, layer UI, pausa del controller/sezione/scena, Home menu, fase di gara, timer introduttivo, contatore della gara e completamento degli opening pan. I bit `valid` distinguono oggetti non ancora creati da stati reali; `4294967295` indica un campo non disponibile. Non richiama routine guest e non modifica memoria o registri guest.
2. Con diagnostica abilitata e `capture_frame` esplicito, cattura tutti i draw GXM di quel solo frame, fino a 16.384 record. Registra shader richiesti/attivi, target, maschere texture/texgen, TEV, blend/profondità/alpha e hash dei payload. Usa un marker nell'ordine FIFO e legge i record soltanto dopo la barriera finale del consumer. Nessun `sceGxmFinish` viene aggiunto dalla cattura dei draw.
3. Conserva la cattura immagine esistente dello stesso frame. Questa lettura dei pixel inserisce una sincronizzazione GPU; la sessione diagnostica non serve a confrontare FPS.

Il file nuovo è `ux0:data/wiicompiled-vita/aurora/diagnostics/frame-300-draws.csv`; l'immagine resta `frame-300.ppm`. Il log `[draw-capture]` deve riportare `saved=1 complete=1 lost=0` prima di considerare completa la cattura. I record hanno vista 255 perché non è ancora collegata una classificazione specifica delle viste del gioco. Gli hash identificano shader e variazioni dei payload; non equivalgono a un dump dei pixel di tutte le texture o a una prova della loro correttezza.

## Interpretazione del prossimo test

| Dato osservato | Prossimo percorso da verificare |
| --- | --- |
| `home_active=1` o pausa sezione/scena persistente | Origine della pausa, input e transizione della sezione. |
| Introduzione con timer che cambia e pan che avanza | Durata effettiva dell'introduzione rispetto alla cadenza molto bassa. |
| Countdown senza pan completati | Aggiornamento della camera e condizione che permette il countdown. |
| Fase gara attiva e `race_frames` che cambia, immagine ferma | Aggiornamento dei draw, copie EFB e presentazione. |
| Layer UI assenti | Attivazione delle pagine della sezione prima di attribuire tutto al renderer UI. |
| Draw/materiali con maschere o shader inattesi | Texture effettive, texgen, TEV e illuminazione; il bianco da solo non individua una di queste cause. |

L'oscuramento resta un difetto osservato dall'utente, senza causa attribuita. Non viene applicata una compensazione di luminosità.

## Verifiche e identità

- WiiCompiled host: **22/22 PASS**. I nuovi controlli confrontano l'osservatore con le routine originali tradotte `GetCurrent`, `isPaused` e `HasEveryOpeningPanEnded`, verificano che non modifichi memoria/registri e respinga oggetti/puntatori/count non validi.
- Aurora host: **24/24 PASS**, inclusi cattura ordinata e hash dei payload.
- Build Vita, audit GXM, collegamento pthread/SceFiber e barriere begin/end asincrone: **PASS**.
- SFO: `TITLE_ID=WICMKW001`, `ATTRIBUTE2=12`. Il SELF nel VPK coincide con quello separato.
- Installer: identità locale, conservazione/idempotenza del config, backup e ripristino dopo attivazione parziale verificati con FTP simulato.
- I 55 sorgenti Aurora archiviati nella precedente candidata sono byte per byte identici nel checkout attuale (`RENDERER_UNCHANGED.json`). Nessuna modifica al renderer in questa attività.

| Artefatto della candidata diagnostica | SHA-256 |
| --- | --- |
| SELF | `fe90c1e3c4c90baefd9f0bc65a44919dd1b7775f2f0d9eab4a0e1f05dfa61ee0` |
| ELF | `99d6a82323f0233813ad86e9640bee802b5959d2d905046cc6f1cdccea0bcf38` |
| VPK | `8edc9381e8f134d5296f83c3126e23b53b53507960a04ff0fffe930a7336a426` |

Bundle: `build/time-trial-freeze-20261008/candidate/`, con ELF/VELF/map, SELF/VPK, snapshot dei sorgenti e patch complete, configurazione, verifiche, manifest e installer. La precedente candidata resta in `build/aurora-upstream-20261008/candidate/`.

Questi risultati validano la diagnostica a livello host/statico e il pacchetto. **La nuova candidata non è ancora stata avviata sulla Vita e non contiene una correzione verificata del blocco o dei materiali.**

## Installazione

Chiudere WiiCompiled e attivare FTP in VitaShell su `192.168.1.79:1337`:

```sh
python3 "/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/install-race-progress.py" --time-trial --diagnostic
```

L'installer aggiorna la candidata esatta, verifica i trasferimenti, conserva gli altri valori del config e crea backup del precedente SELF/config. Attiva Mario, Kart standard M, Circuito Luigi e la cattura del frame 300. Avviare manualmente il gioco e lasciarlo almeno tre minuti dopo l'apparizione della vista frontale; poi chiuderlo e riattivare FTP per raccogliere log, immagine e draw. La disponibilità di `frame-300-draws.csv` e dei marker nel log determina se la cattura è arrivata al frame richiesto.

Per leggere soltanto lo stato della gara e la cadenza senza catture, usare lo stesso installer con `--time-trial --quiet`.

## Esito del test e seguito del 9 ottobre

La candidata diagnostica è stata eseguita dall'utente. La cattura del SELF `fe90c1e3...` identifica una pausa da controller scollegato: dal frame 249 al 670 controller/sezione/scena sono in pausa, il timer introduttivo resta 22 e i frame di gara restano zero. La registrazione iniziale `0x24` è stata corretta in `0x124`, ID del GameCube collegato alla porta 0. La successiva build controller è stata eseguita: pausa zero, pan completato e contatore della gara da 244 a 290.

La diagnosi e la prova della correzione sono nel [report controller](VITA_TIME_TRIAL_CONTROLLER_2026-10-08.md). Restano materiali bianchi intermittenti, HUD assente e circa 1 FPS. La preparazione delle candidate native è descritta nel [report prestazioni del 9 ottobre](VITA_NATIVE_RENDER_PERFORMANCE_2026-10-09.md). L'indicazione precedente di candidata non ancora avviata descriveva lo stato al momento della sua consegna, superato da queste prove.
