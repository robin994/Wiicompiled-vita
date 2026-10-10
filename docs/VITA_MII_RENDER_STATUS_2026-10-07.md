# WiiCompiled Vita: menu iniziali, creazione della licenza e Mii, 7 ottobre 2026

La prova della build `psarc-vertex-layout-20261006` conferma un avanzamento:
i menu iniziali renderizzano, l'input permette di navigare e gli errori di
decodifica VCD/VAT non ricompaiono. La creazione della licenza resta incompleta:
la cattura di **New Licence** mostra titolo, sfondo e pulsante Back, ma il
centro è quasi vuoto e un elemento giallo è tagliato sul bordo sinistro.
L'utente conferma che i Mii non sono visibili e gli FPS sono bassi.

Il problema dei Mii **non è ancora risolto**. La nuova candidata
`psarc-mii-diagnostics-20261007` raccoglie i dati mancanti per distinguere
un errore nelle copie EFB da un errore nello stato RFL o nei passaggi grafici.
Non modifica viewport, clipping, TEV, conversione delle texture, ordine FIFO
o comportamento delle attese. Non contiene una nuova ottimizzazione degli FPS.

## Evidenza della sessione sulla Vita

Dispositivo: `192.168.1.79:1337`, app `WICMKW001`.
Cattura: `device-runs/vertex-layout-20261007T071612Z`.
`CAPTURE.json` conserva identità e metadati dei file scaricati;
`ANALYSIS.json` contiene i conteggi e le limitazioni della diagnosi.

| Dato | Risultato |
| --- | --- |
| Marcatore dell'ultima sessione | `psarc-vertex-layout-20261006` |
| SHA-256 del SELF installato e riletto | `3bd1e21c1acfb1dec669c699fa194cd4b91393952e560dd4c98b3572f766240e` |
| Identità con la candidata precedente | Confermata |
| SFO installato | `ATTRIBUTE2=12`, `ATTRIBUTE=32768` |
| Config.toml | `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65` |
| Coverage | 405.877 osservazioni, 206 chiavi, 1.221 fallback, 0 unsupported |
| Errori `vertex decode failed` | **0** |
| Copie EFB fallite | **18**, su quattro destinazioni |
| Nuovi dump del 7 ottobre nella lista remota | Nessuno; sono elencati otto dump del 6 ottobre |

Le immagini `frame-274.ppm`, `frame-985.ppm` e `frame-1405.ppm` sono presenti
nei marcatori dell'ultima sessione. Le PNG sono conversioni per consultazione;
le PPM originali sono conservate. `frame-589.ppm` e `frame-979.ppm` appartengono
alle prove precedenti e **non** descrivono questa build.

- [Schermata iniziale, frame 985](../device-runs/vertex-layout-20261007T071612Z/diagnostics/frame-985.png).
- [New Licence incompleta, frame 1405](../device-runs/vertex-layout-20261007T071612Z/diagnostics/frame-1405.png).

Le destinazioni delle copie fallite, espresse come puntatori host nel log,
sono `0x84c94240` (7), `0x84c94360` (2), `0x84c95420` (7) e
`0x84c95540` (2). Il log precedente non include rettangolo, formato e
dimensioni delle copie fallite: il collegamento con i Mii è una **ipotesi**,
non una causa dimostrata. Altre copie EFB della sessione riescono.

Gli altri 1.203 fallback riguardano l'approssimazione `zCompLocBeforeTex`.
Il messaggio contiene il nome storico vitaGL, ma questa app usa il backend
GXM nativo. Non sono 1.203 errori di caricamento delle texture.

All'avvio RFL tenta di leggere
`nand/shared2/menu/FaceLib/RFL_DB.dat`, che non esiste. Il file è il database
personale dei Mii: la sua assenza, da sola, non dimostra che manchino le risorse
grafiche o i Mii predefiniti. Non sono state create risorse o database fittizi.

## Cosa sappiamo dei rallentamenti

| Campione diagnostico | Frame 1199 | Frame 1499 |
| --- | ---: | ---: |
| Intervallo totale Aurora | 248.774 us | 1.265.490 us |
| Elaborazione FIFO | 8.421 us | 47.292 us |
| Frontend draw, incluso lavoro annidato | 6.812 us | 38.914 us |
| Present | 6.480 us | 29.835 us |
| Draw / vertici | 110 / 440 | 665 / 2.660 |
| Texture di fallback / overflow / shader falliti | 0 / 0 / 0 | 0 / 0 / 0 |

Sono campioni di tempo trascorso, non misure della sola CPU o della GPU e
non un benchmark stabile di FPS. Il frontend draw è incluso nel lavoro FIFO;
non si sommano le colonne. La diagnosi contiene tre readback del framebuffer
e logging, che influenzano il tempo. Anche la compilazione iniziale di nuovi
shader produce pause e va distinta dai rallentamenti a cache calda.

La coda display nei campioni di menu impiega circa 30 us. La maggior parte
dell'intervallo non è attribuita dai contatori del renderer: restano da misurare
il codice ricompilato, le chiamate native e le attese. Ridurre la risoluzione
o cambiare TEV senza questa attribuzione non ha un beneficio dimostrato.

## Nuova diagnostica implementata

`WIC_NATIVE_CALL_PROFILE` è un'opzione di build **OFF per default**.
La candidata diagnostica la abilita soltanto sul thread del gioco; gli altri
thread mantengono il default OFF. `WIC_PROFILE_GUEST` resta OFF: non viene
aggiunta strumentazione a ogni funzione ricompilata.

- `[runtime-profile]`: finestre di almeno due secondi e 60 frame, oppure
  dieci secondi al primo frame completato successivo nei menu lenti. Registra
  presentazioni e frame con draw, costo delle richieste native, costo dei
  callback, passaggi di pila, lavoro FIFO e attese VI/GX/GKI/panic.
- `[rfl-health]`: legge il puntatore del manager RFL e i campi working,
  flags e reason, con indirizzi e offset verificati sui sorgenti tradotti PAL.
  Non richiama la libreria RFL e non modifica la memoria del gioco.
- `[aurora-vita][copy-failed]`: conserva destinazione, rettangolo logico e
  fisico, dimensioni, formato, viewport e scissor delle copie fallite.
  Il campo `backend_last_error` può essere una condizione precedente se
  il rifiuto avviene prima di chiamare GXM; va interpretato insieme ai parametri.
- Le prime 16 copie EFB e le successive potenze di due sono registrate al
  livello Info. Anche i dettagli dei fallimenti sono limitati alle prime 16
  occorrenze e alle potenze di due. Non si abilita il logging Debug completo.
- `[lit-mesh]` e `[lit-mesh-v]`: nei primi 16 disegni triangolari che richiedono
  normali vengono registrati viewport/scissor, profondità, culling e tre
  campioni trasformati con coordinate proiettate e colore. È una diagnostica
  della geometria illuminata, non un'identificazione certa dei draw dei Mii.
  Usa copie locali dei vertici e non modifica i dati inviati alla GPU.

La misura delle richieste include i callback; le attese e il lavoro FIFO sono
contenuti nei callback. La strumentazione evita di contare due volte i tempi
annidati, include gli intervalli ancora attivi negli snapshot e mantiene la
corretta chiusura degli scope in caso di eccezione. Le frequenze riportate
sono diagnostiche e non costituiscono una misura neutra delle prestazioni.

## Validazione e prossima prova

Passano **8/8** test del core/runtime e **18/18** test Aurora. I test del contesto
coprono richieste dirette, coroutine diverse, callback annidati, snapshot
durante una chiamata, propagazione delle eccezioni e disattivazione del profiler.
I test delle attese verificano la pila nativa, i deadline, il ritorno al chiamante
e la corrispondenza tra conteggi e tempi registrati per ciascun sito.

La build Vita finale passa gli audit sul binario ARM: 45.022 simboli controllati,
draw/present GXM nativi, assenza di GL/vitaGL/vita2d, primitive pthread risolte,
guard C++ collegati, TLS e SceFiber nativi, stdio, attese e input instradati
sulla pila nativa. Il SELF contenuto nel VPK coincide con quello generato.
Questi controlli non certificano il rendering o le prestazioni sulla console.

La candidata mantiene `ATTRIBUTE2=12`, GXM nativo, PSARC come sorgente degli
asset e le ottimizzazioni sperimentali precedenti disabilitate. Non richiede
la ISO sulla console o una directory di dati estratti.

La prossima prova deve restare per circa 15 secondi nella schermata iniziale,
poi per almeno 15 secondi nella creazione della licenza/selezione Mii. Va
registrato se gli elementi dell'interfaccia sono mancanti, tagliati o fermi.
Il confronto dei nuovi log chiarirà quali copie falliscono e se il tempo
dominante è nelle richieste native, nelle attese oppure fuori dal renderer.
La correzione dei Mii e il miglioramento degli FPS restano da verificare
sul dispositivo. Non sono stati inviati comandi di avvio automatico.

La candidata è **installata e verificata sulla Vita**: 14/14 file coincidono
con il VPK, con `ATTRIBUTE2=12`, configurazione e PSARC conservati.
Il gioco non è stato avviato automaticamente; la prossima prova di rendering
e prestazioni resta da eseguire. L'installazione è identificata da
`build/latest-install-path.txt`.
[Verifica dell'installazione e istruzioni per la prova](VITA_MII_DIAGNOSTICS_DELIVERY_2026-10-07.md).

Archivio della candidata: `build/vita-psarc/candidate-mii-diagnostics-20261007T075306Z`.
Il VPK è `wiicompiled-vita-game.vpk`; SELF, ELF, map e VELF sono conservati
insieme a sorgenti modificati, configurazione, log degli audit e dei test.
`VALIDATION.json` distingue la diagnosi dalla correzione ancora da realizzare;
`CHECKSUMS.json` conserva l'identità dei file archiviati.
I puntatori `build/latest-build-path.txt` e
`build/mii-diagnostics-candidate-path.txt` identificano questa candidata.

| Artefatto della candidata | SHA-256 |
| --- | --- |
| VPK | `f1bd53931fcbc8a3808346897194a7af74e94a4a2e01ba0013d4270ccbb3d95d` |
| SELF | `5c37a4b082071b81231d7fea4d8c690540b5f4ebc83ba196f770875a01e1eb68` |
| ELF | `c7540475a1844808ea0aaa2eabc4bed98d480d10284b08e68a13c65d30be8427` |
| Map | `8d1d315602e980d2e0ae0c7ad109043946039d2bc723a90c2ec7e48702f13d1b` |
| VELF | `944768f9c6984e736dc73b011a3f8be88e893754e24fb6998358c71b72a973cc` |
| Sorgenti e input generati, 31.024 file | `eeb8dde6f7b496dabfdf5b03e4ce4dee16aecf38bf45b1d75db1e3bbdf8aa0a8` |

Build eseguita:

```sh
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game -DWIC_NATIVE_CALL_PROFILE=ON
```

Per tornare a una build senza questo profiler nello stesso build directory,
passare esplicitamente `-DWIC_NATIVE_CALL_PROFILE=OFF`: CMake conserva
l'opzione della precedente configurazione nella propria cache. Anche i
readback diagnostici vanno esclusi da un futuro confronto neutro degli FPS.
