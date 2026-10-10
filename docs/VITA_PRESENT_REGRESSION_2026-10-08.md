# Schermata capovolta e nero dopo l'avvio Time Trial — 8 ottobre 2026

Segnalazione dopo l'installazione della candidata light map/Time Trial: la schermata iniziale «premere Start» è apparsa capovolta, poi l'immagine è scomparsa. La cattura hardware conferma il nero; il messaggio dell'utente non specifica ancora se l'audio continua.

## Identità ed evidenza hardware della build precedente

- SELF installato e riletto via FTP: `6128156e30b1b7cd27c156762ae20d80d6f4f6b2617e183a85f52b8f3e9add3b`.
- Configurazione: `4bec11b8100a6d813f86d89e7e95c424622cdc7aa91beaa13330786fcf5d07fa`, `[test].time_trial = true`.
- Marker: `psarc-resident-assets-async-lightmap-tt-20261008`.
- Acquisizione: `build/present-regression-20261008/capture-20261008T174625Z/`.

Il log registra `requested=1`, `init_race character=0 kart=1 course=8 mode=2 player_type=0` e `load_section=0x1f`. Questo verifica la richiesta e il caricamento della sezione, senza provare una gara visibile o guidabile. Le copie light map `(0,456)` e `(32,456)` sono diventate catture reali 51×39 e 52×39; non vengono più sostituite dalla texture sintetica nera.

La VI passa da `black=1` a `black=0` alla presentazione 100. Il frame 180, acquisito dalla superficie effettivamente presentata e scritto dalla stessa sessione, contiene **522.240 pixel RGB tutti `(0,0,0)`**. SHA-256 PPM: `96ddc219ce63c7731496e0f09f86160017b127527574a6bd594f5dcb5154ed38`. Il log continua almeno fino al frame 290 senza errori espliciti di begin-frame, allocazione/copia GXM o panic. Ci sono avvisi di superamento del budget scene; questo run diagnostico non misura gli FPS neutri.

Il file `frame-600.ppm` remoto è precedente a questo lancio (15:52 UTC) ed è escluso dall'evidenza della candidata. La distinta delle catture e i tempi remoti sono in `DIAGNOSTICS.json`.

## Difetti corretti nel codice

### Orientamento della copia display

Il viewport GXM usa scala verticale `-height/2`: la parte alta del display corrisponde a clip-space `Y=+1`. Il triangolo di `blit_to_default` campionava `V=1` in quel punto e `V=0` in basso. Il nuovo EFB più grande dello schermo ha attivato quel percorso anche con raster visibile 960×544, esponendo l'inversione.

La copia display ora usa la stessa geometria delle altre copie native: alto → `V=0`, basso → `V=1`. Il crop 960×544 dell'EFB 1024×640 continua a escludere le righe scratch. `gxm_efb_ops.hpp` contiene la geometria effettivamente caricata nel buffer GPU e condivisa dai tre percorsi di copia; non vengono ruotati camera, modelli o input.

### Clear dopo GXCopyTex

Il clear successivo a una copia parziale chiamava `clear_current`, che cancellava l'intero target. Anche una copia di luce sotto l'immagine visibile eliminava colore e profondità della scena già disegnata.

Sul percorso GXM, `GXCopyTex(clear=true)` ora cancella soltanto il **rettangolo sorgente fisico**. Il clear parziale mantiene il clipping esatto nel fragment shader e conserva il caricamento della profondità esterna al rettangolo. L'ottimizzazione che evita di caricare la vecchia profondità resta limitata ai clear dell'intero target. Una sorgente interamente fuori dall'EFB non cancella alcun pixel.

Rimangono l'ordine copy → clear, i reset per BeginScene e il ciclo di presentazione già esistente. La flag Time Trial, la combinazione Mario/Kart standard M/Circuito Luigi, lo storage EFB e le opzioni di build sono quelli della precedente candidata. Il percorso VitaGL conserva il comportamento precedente.

L'inversione è riprodotta dal controllo matematico della vecchia geometria. Il clear totale è un difetto concreto compatibile con il framebuffer nero osservato; la nuova cattura hardware deve verificare che non restino altre cause.

## Validazione della candidata corretta

- Build Vita: **PASS**.
- Runtime host: **22/22 PASS**.
- Aurora host: **19/19 PASS**.
- Nuovo contratto: sorgente asimmetrica senza inversione, crop visibile senza righe scratch, clear parziale che conserva tutti i pixel visibili e quelli esterni alla light map, clipping ai bordi, sorgenti vuote, distinzione clear intero/parziale. Usa la geometria e il pacchetto di clear utilizzati dal renderer nativo.
- Audit del pacchetto, GXM puro (45.608 simboli) e linkage runtime/barriere: **PASS**. SELF compilato identico a quello nel VPK, SFO `ATTRIBUTE2=12`.
- L'audit ARM verifica che `DrawSink::copy_tex` chiami il clear regionale nel binario consegnato.
- Installer: conservazione delle opzioni TOML, idempotenza, backup e ripristino dei due file dopo un errore simulato: **PASS**. Controllo locale della candidata: **PASS**.

I test host verificano matematica e descrittori; non eseguono il driver GXM e non provano colori, comandi o stabilità Vita della correzione.

## Pacchetto e installazione

Marker: `psarc-resident-assets-async-efb-present-fix-20261008`.

| Artefatto | SHA-256 |
| --- | --- |
| SELF | `7e62624d0f9488a1cb7373f69ed59ec1e2c17d9c903296fd7295013fed9a1fe6` |
| VPK | `34bed69d78b4dac5ac7af2e01f5e52754c4489d3d21141c902eba1758df54521` |
| ELF | `260223061e6cb35009f802b3261ef44055d5fcebd17610283f11504c024c89c2` |

Bundle: `build/present-regression-20261008/candidate/`. Comprende VPK, SELF, ELF, VELF, map, opzioni CMake, diff/sorgenti della correzione, log di validazione e manifest. Il pacchetto e gli artefatti della candidata fallita restano in `build/models-timetrial-20261008/candidate/` per confronto e simbolizzazione.

Chiudere WiiCompiled e attivare FTP VitaShell su `192.168.1.79:1337`, quindi:

```sh
python3 "/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/install-present-fix.py" --time-trial
```

Lo script verifica la candidata, gli altri file dell'app registrata e il trasferimento, conserva backup aggiornati di eseguibile/configurazione e ripristina i file se l'attivazione fallisce. Il test Time Trial era già abilitato nella configurazione catturata. Non avvia automaticamente l'app.

Verifica Vita ancora aperta: schermata iniziale orientata correttamente, passaggio dalla transizione alla pista visibile, Mario/kart con colori, HUD, acceleratore/sterzo e audio. Per un confronto dai menu usare `--menu`; l'eseguibile resta lo stesso.
