# WiiCompiled Vita: primo avvio senza dump, input e memoria

Aggiornamento dopo la prova della candidata input/memoria: i comandi
rispondono e i menu avanzano, l'audio è riconoscibile e il framebuffer
mostra un triangolo bianco. I metadati registrati sono cambiati e il budget
di memoria è aumentato. È stata corretta una mancata invalidazione del
layout VCD/VAT: [nuova diagnosi e candidata](VITA_VERTEX_LAYOUT_2026-10-06.md).
Le sezioni seguenti conservano la diagnosi e la consegna precedenti al test.

La build installata `psarc-pthread-complete-20261006` supera ora sulla Vita
i controlli iniziali di thread, TLS e SceFiber e avvia il mixer AX/DSP.
Il gioco non è ancora utilizzabile: l'utente vede un riquadro bianco immobile
su fondo nero, sente musica riconoscibile ripetersi e non ottiene risposta
visibile dai tasti. Non è comparso un nuovo dump nella directory controllata.

È pronta la candidata **`psarc-input-memory-20261006`**. Corregge il percorso
dei comandi, imposta **`ATTRIBUTE2=12`** nel pacchetto e aggiunge diagnostica
per il rendering. I 14 file dell'app sono ora aggiornati e verificati sulla
console, compreso lo SFO con il valore 12. Il VPK completo è caricato;
la registrazione nativa dei metadati richiede ancora l'installazione in
VitaShell. Il gioco non è stato avviato. Dettagli:
[consegna input e memoria](VITA_INPUT_MEMORY_DELIVERY_2026-10-06.md).
La correzione del riquadro bianco e il funzionamento dei comandi nel gioco
richiedono il prossimo test sul dispositivo.

## Evidenza della sessione attuale

Acquisizione FTP del 6 ottobre, 21:04 UTC:
`device-runs/pthread-complete-20261006T210413Z`. Sono conservati log completo,
ultima sessione isolata tramite marker di build, configurazione, SFO ed eboot
riletti dalla Vita, elenco dei dump e gli ELF/map/VELF/VPK abbinati.
`CAPTURE.json`, `OBSERVATIONS.json` e `CHECKSUMS.json` registrano la provenienza.

SELF installato verificato:
`eca6ac41e1b490842d6c2ec698a1cdc1a575cfec00047e6a341ca0da1d9fe679`.
Config.toml:
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
La corrispondenza con la consegna pthread è esatta.

Il log della sessione contiene:

```text
[runtime-check] std-thread=ok tls=isolated dispatch=local
[runtime-check] kernel-thread=ok tls=isolated dispatch=local
[runtime-check] scefiber=ok peer-switch=ok native-return=ok requester=ok d8-d15=ok
[audio] AX/DSP mix worker started
[psarc] reads=22 bytes=18371320 native_io_us=2101956 failures=0 stored_blocks=1
```

Sono raggiunti StaticR, NW4R SND/LYT/G3D e il caricamento di HomeButton.arc.
Vengono compilati shader e caricati altri dati grafici. Il budget scene
superato è una segnalazione diagnostica: quel controllo non interrompe
i draw. La musica riconoscibile è un'osservazione dell'utente, non una
verifica della coda audio o della temporizzazione del gioco.

Il dump più recente nell'elenco resta `1791319267`, appartenente alla build
precedente arrestata prima di main. Non va riutilizzato come diagnosi di
questa esecuzione. Menu, gara e immagine corretta non sono verificati.

## Correzione dei comandi

Il vecchio backend conteneva già una mappa di tasti Classic Controller.
Tuttavia dichiarava un Classic collegato tramite WPADProbe mentre altre
risposte WPAD dichiaravano il controller assente; PADRead riportava tutte
le porte GameCube scollegate. La lettura SceCtrl ignorava inoltre l'esito
e avveniva direttamente dal chiamante guest. Queste sono incoerenze
confermate nel sorgente; non dimostrano da sole il punto esatto in cui il
gioco ignorava l'input nell'esecuzione osservata.

La candidata presenta la Vita come un GameCube pad sulla porta 0, attraverso
il PAD HLE già esistente. WPAD/KPAD non annunciano lo stesso dispositivo.
La lettura SceCtrl passa dal dispatcher sulla pila nativa e un campione
fallito o assente non mantiene tasti premuti. Le altre tre porte restano
scollegate. Il ritorno PADRead resta zero perché la Vita non offre il motore
rumble; la connessione è indicata dal campo `PADStatus.err`.

| Vita | GameCube |
| --- | --- |
| X / Croce | A |
| Cerchio | B |
| Quadrato | X |
| Triangolo | Y |
| Start | Start |
| Select | Z |
| L / R | L / R, pressione analogica completa |
| Croce direzionale | D-pad |
| Analogico sinistro | Stick principale |
| Analogico destro | C-stick |

I marker `[input-vita]` registrano esito della lettura, tasti Vita e tasti
GameCube. Sono limitati ai primi campioni, alle prime 32 transizioni e a
un riepilogo ogni 300 letture, per non scrivere a ogni frame.

## ATTRIBUTE2=12

Il parametro VitaSDK che seleziona la modalità di memoria da 109 MiB è
`ATTRIBUTE2=12` nel param.sfo. `ATTRIBUTE` è un campo distinto e conserva
il valore homebrew `0x8000`. Le definizioni sono nel
[sorgente ufficiale di vita-mksfoex](https://github.com/vitasdk/vita-toolchain/blob/master/src/vita-mksfoex/vita-mksfoex.c).
Anche la configurazione corrente del port Strikers usa `ATTRIBUTE2=12`.

Lo SFO riletto nella cattura precedente aveva `ATTRIBUTE2=0`. Entrambi i nuovi VPK
hanno il valore 12 verificato direttamente dentro l'archivio, con TITLE_ID
corretto. Il manifest di consegna ora controlla questi campi e rifiuta un
pacchetto con un valore diverso. Il vecchio SFO è usato come controllo negativo.

La prossima installazione deve includere i metadati del VPK completo:
sostituire il solo eboot.bin lascerebbe lo SFO precedente. La selezione di
memoria non prova, da sola, che il riquadro bianco fosse dovuto a poca RAM.

## Diagnostica del riquadro bianco e dell'audio

La candidata mantiene rendering GXM, GX sincrono, TEV, ordine FIFO,
copie EFB e reset/scissor di Aurora. Non introduce nuove ottimizzazioni.
Aggiunge:

- `[frame-health]`: frame completato, draw, vertici, texture fallback,
  funzionalità non supportate, scene e copie EFB.
- Un trace limitato a 256 draw, con maschere di fallback e warning;
  fino a 128 record sono salvati periodicamente in
  `ux0:data/wiicompiled-vita/aurora/diagnostics/wic-draw-trace.log`.
- Un report coverage `wic-coverage.log` nella stessa directory e
  diagnostica di decodifica texture già fornita da Aurora.
- Tre catture PPM del framebuffer, dopo 30, 180 e 600 frame con draw.
  I nomi `frame-<numero>.ppm` contengono il numero di presentazione.
- `[audio-health]`: campioni accettati, consumati e in coda, underrun,
  blocchi rifiutati ed errori di output.

Le catture usano il readback sincronizzato esistente del renderer e
inseriscono tre attese GPU. Questa build serve a verificare correttezza;
non va usata per confrontare gli FPS con una build priva di diagnostica.
Il log attuale non contiene dati sufficienti per attribuire il bianco a
texture mancanti, materiale, copie EFB o avanzamento della scena.

## Pacchetto e verifiche locali

Archivio conservato:
`build/vita-psarc/candidate-input-memory-20261006T211714Z`.
Il puntatore `build/latest-build-path.txt` identifica la candidata nuova;
`build/latest-install-path.txt` identifica ora l'aggiornamento dei file sulla
console, con registrazione nativa ancora da completare.

| Artefatto | SHA-256 |
| --- | --- |
| VPK | `c5a8ac0e52682d864ecb53e4d7178e6f9edf61df5215758c4cc8f861c22186d4` |
| SELF | `e8a4fa422beaa08350a12c8634ebd18b7d6a97e37d39b62eeebdc85dccc026d1` |
| ELF | `74a1eac6e24008916c2913eda390d4fcd48c708ad7650c8dbcb9993ccf2c2439` |
| Map | `14d99cc8b1a3f1416995764bd7e36740b1c7a90b6f86bb23a9d1b886a27e83eb` |
| VELF | `c961841ee54e3b6911133777a6511334fa1a6925c673822107f111ee6d4c6dab` |

VPK: 22.757.429 byte. SELF incorporato identico al SELF esterno.
Rispetto al VPK installato cambiano esattamente eboot.bin e sce_sys/param.sfo;
tutti gli altri payload del pacchetto sono identici.

- 8/8 contratti host passati, compreso il nuovo test sul backend input reale:
  lettura dalla pila nativa anche da una coroutine, tutti i pulsanti,
  combinazioni, segno e limiti degli stick, trigger, status guest big-endian,
  letture fallite e assenza di una seconda identità WPAD.
- Audit di 45.013 simboli eseguibili: draw/present GXM presenti, nessun
  GL/vitaGL/vita2d.
- Audit pthread/SceFiber passato; guard C++ collegati alle primitive reali.
- Audit delle istruzioni ARM e dei veneer: stdio, attese e PADRead raggiungono
  il dispatcher nativo; il callback del controller chiama SceCtrl.
- SFO dei due VPK controllati; SFO installato precedente rifiutato.
- Digest di 31.021 sorgenti/input/tooling:
  `accaaf4e9c8d6584f6590a67e1d43643adf2151506baf5db31af0038be9914bc`.

PSARC e configurazione della console non sono stati modificati. Sulla Vita
continua a servire il solo PSARC. Nessun comando di avvio è stato inviato.

## Prossima verifica sul dispositivo

Installare il VPK completo e avviare manualmente. Premere X, rilasciarlo,
poi provare direzioni e Start. Recuperare il log e la directory diagnostics,
insieme a eboot e SFO riletti. Il marker atteso è
`[boot] build=psarc-input-memory-20261006`.

Se i tasti risultano ricevuti ma l'immagine resta identica, confrontare
le tre catture e il trace: prima determinare se esistono draw di scena
validi e texture fallback, poi correggere il percorso preciso. Anche un
menu raggiunto richiederà successivamente una gara completa, effetti EFB,
input, audio, salvataggi e sospensione/ripresa.
