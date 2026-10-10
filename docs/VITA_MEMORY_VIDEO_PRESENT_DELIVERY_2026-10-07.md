# WiiCompiled Vita: candidata memoria, video e presentazione

La candidata `psarc-memory-video-present-20261007` corregge un difetto
riprodotto nell'ordine delle scritture GPU/CPU e cambia la presentazione
dei frame e la pubblicazione delle texture dei filmati. Contiene
**ATTRIBUTE2=12**. Il PSARC AVC già presente sulla Vita resta compatibile;
non servono una nuova conversione, una ISO sulla console o file estratti.

La build precedente è conservata con ELF, map, VELF, SELF, VPK e sorgenti.
La prova su dispositivo di quella build resta negativa: verde persistente
nelle card, menu molto lenti e ritorno alla LiveArea dopo "1 player".
L'utente segnala inoltre flicker ripetuto nella schermata di avvertenza.
Questa candidata richiede una nuova prova manuale prima di poter dichiarare
corretti questi sintomi. Il target resta 30 FPS.

## Copie EFB differite e scritture della CPU

Il runtime poteva mantenere una copia GPU verso la RAM in attesa della
prima lettura, mentre la CPU scriveva già nuovi dati nello stesso intervallo.
La successiva materializzazione della copia sovrascriveva i dati più recenti.
Le registrazioni separate per gli alias fisico, cached e uncached potevano
inoltre eseguire nuovamente la stessa copia dopo altre scritture della CPU.

La candidata completa una copia precedente prima di una scrittura CPU
sovrapposta. I percorsi scalari e quelli RAM diretti controllano le pagine
che contengono copie differite; il percorso bulk usa il fallback controllato
quando necessario. Una copia EFB materializzata cancella le registrazioni
di tutti e tre gli alias e non viene ripetuta attraverso un altro alias.
Il resto dell'intervallo conserva i dati della copia precedente.

Il test `deferred-write-contract` usa il vero `memory.cpp`, il resolver Vita
e RAM condivisa tra i tre alias. Copre dati di intestazione dell'heap,
scritture intere e floating point, letture attraverso tutti gli alias e una
scrittura a cavallo di due pagine. Compilato contro i sorgenti archiviati
della build precedente, fallisce perché la nuova intestazione scritta dalla
CPU viene persa. Con i sorgenti correnti passa. Il risultato del controllo
è conservato in `build/memory-video-control/RESULT.json`.

Questo dimostra un difetto del runtime e la sua correzione nel test. **Non
dimostra ancora che sia la causa della corruzione osservata sulla Vita.**
L'osservatore dell'heap registra ora le copie differite che toccano
l'intestazione del blocco libero finale e controlla l'heap dopo il readback.
Non ricostruisce la lista libera e non altera i dati per nascondere errori.

## Frame video e texture

Quando il decoder AVC non ha ancora un'immagine, la candidata inizializza
il buffer di destinazione con nero neutro: Y=0, U=128, V=128. Anche questo
buffer passa per la verifica di geometria e intervalli, il tiling e la
pubblicazione di tutti e tre i piani. Non viene riutilizzata un'immagine di
un altro filmato soltanto perché ha le stesse dimensioni.

Dopo che i worker hanno completato tutti i piani, il percorso nativo
pubblica le scritture DMA e invalida le texture GX. L'invalidazione avviene
prima che un draw possa consumare un buffer video riciclato. I log
`[avc-native]` distinguono `picture=0` dalle immagini decodificate e riportano
gli indirizzi Y/U/V e i primi valori dei piani. Il contatore delle
pubblicazioni include i buffer neutri.

Il test del wrapper verifica il nero neutro, l'integrità dei byte esterni
ai buffer, la conservazione dello stato CPU e l'ordine: completamento dei
worker, pubblicazione dei tre piani, invalidazione. Il decoder hardware e
il contenuto dei pixel sulla console restano da verificare. Il verde
persistente non viene attribuito ai soli primi buffer senza output.

## Schermata di avvertenza e presentazione

Nella prova precedente il retrace poteva chiudere e presentare un frame
Aurora anche senza un nuovo `GXCopyDisp`. Le finestre iniziali completavano
circa 60 presentazioni al secondo ma soltanto pochi frame con draw. Questo
percorso poteva presentare un EFB vuoto o incompleto tra due frame del gioco.

Sulla Vita la chiusura del frame ora appartiene a `GXCopyDisp`. Il retrace
continua ad aggiornare il contatore VI, i callback, gli eventi e i thread
in attesa. In assenza di un nuovo frame resta visualizzato l'ultimo buffer
completato. `VISetBlack` viene applicato alla superficie di presentazione
prima della chiusura del frame, senza alterare l'EFB del gioco o l'ordine
del clear dopo la copia. Il marker `[vi-vita-present]` indica proprietario
e stato black. Il flicker deve essere verificato visivamente sulla Vita.

## Verifiche e limiti

- 19/19 test host superati sui sorgenti finali.
- 5/5 test selezionati con AddressSanitizer e UndefinedBehaviorSanitizer
  superati: scritture differite, allocator SDK tradotto, osservatore heap,
  lifecycle AVC e wrapper video.
- Il controllo negativo con i sorgenti della build precedente riproduce
  la perdita della scrittura CPU; il test corrente passa.
- L'allocator SDK completo mantiene le prove di split, memset e costruzione
  delle liste di animazione. Non vengono sostituiti gli allocator del gioco.
- Compilazione Vita e audit ELF, GXM, dispatch, confini nativi e VPK superati,
  associati ai checksum della candidata archiviata. Verificate anche le
  chiamate ARM a invalidazione video, controllo black e osservatore heap;
  il retrace non chiama direttamente la presentazione sulla Vita.
- Nessuna prova di questa candidata è stata eseguita sulla console durante
  la preparazione. Nessun comando di avvio remoto è stato inviato.

Restano aperti il passaggio da "1 player", il Mii mancante, l'output video
sulla console e le prestazioni. Le finestre diagnostiche della build
precedente indicano un forte costo sul percorso CPU principale, ma non
misurano il costo CPU esclusivo di ciascun thread. Queste correzioni non
costituiscono una prova di miglioramento degli FPS né del raggiungimento
dei 30 FPS. I flag matematici e la topologia dei worker restano quelli della
build AVC precedente; async GX resta disattivato.

## Artefatti e prova manuale

Identità della build finale:

| Artefatto | SHA-256 |
| --- | --- |
| VPK, 24.943.596 byte | `e39d81c56d00445175dea132dd218bfaccd88de0862da4873251c831447b1c10` |
| SELF | `c64ee93c399d2b4593e87c58565b2606755e4116ee587e9450dc0a017a088752` |
| ELF | `8571c68df9484d6fe359fc0511d49997e08654b85cb9307eca8c627cc4cabbce` |
| 31.058 file sorgenti e input generati | `3f87bd0c886dc7ab1e542b02d1f910775a9f0569aa74bc18a9fd7a75f3c025e2` |

Il percorso della candidata finale è registrato in
`build/memory-video-present-candidate-path.txt` e `build/latest-build-path.txt`.
La directory contiene VPK, SELF, ELF, map, VELF, manifest dei dati e delle
opzioni, audit, risultati dei test, controllo negativo, sorgenti verificati
e checksum. `REPORT.md` e `VALIDATION.json` identificano esattamente la build.
La raccolta negativa precedente resta distinta in `device-control/`.

Il PSARC associato ha 1.406.613.154 byte e SHA-256
`c92993c58cacc1a90223f9213173e2053e04165ec7115a8cd8867f420ce2f743`.
Questo è l'hash dell'archivio host verificato; il file sulla console non è
stato riletto integralmente in questa sessione.

1. Installare il VPK della candidata e avviare manualmente l'app.
2. Nella schermata di avvertenza osservare se il flicker ripetuto continua.
3. Restare nelle card almeno 15 secondi e verificare se il verde si alterna
   ancora durante la riproduzione.
4. Selezionare "1 player" e verificare se compare il menu successivo, resta
   una schermata nera o l'app torna alla LiveArea.
5. Per la lettura dei log, verificare il marker della candidata e il SELF
   effettivamente installato prima di associare un nuovo risultato.

La prossima diagnosi dovrà usare questa identità e separare le finestre con
cattura del framebuffer dalle misure di fluidità.
