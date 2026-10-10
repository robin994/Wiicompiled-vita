# WiiCompiled Vita: stato e ripresa dello sviluppo, 6 ottobre 2026

**Aggiornamento del 7 ottobre:** la prova manuale di
`psarc-vertex-layout-20261006` conferma i menu iniziali renderizzati correttamente
e zero errori di decodifica VCD/VAT. FPS bassi, creazione della licenza incompleta
e Mii invisibili restano aperti. La candidata `psarc-mii-diagnostics-20261007`
aggiunge la diagnostica necessaria a distinguere copie EFB, stato RFL,
chiamate native e attese. È installata e verificata su 14/14 file dell'app;
non è stata avviata automaticamente e la nuova prova manuale resta da eseguire.
[Consegna e prova](VITA_MII_DIAGNOSTICS_DELIVERY_2026-10-07.md).
[Evidenza e limiti](VITA_MII_RENDER_STATUS_2026-10-07.md).
Le descrizioni successive sono storiche.

Ultimo test manuale: nella build `psarc-input-memory-20261006` i comandi
rispondono e permettono di avanzare nei menu. L'audio è riconoscibile;
la grafica mostra un triangolo bianco su fondo nero, confermato dalla cattura
del framebuffer. Il SELF installato coincide con la candidata, lo SFO
contiene `ATTRIBUTE2=12`, i metadati appmeta sono cambiati e la memoria libera
prima del renderer è salita a 185.597.952 byte. Nessun nuovo dump nel campione.

La coverage registra 1.451 errori di decodifica dei vertici. È stato
riprodotto e corretto sul computer un mancato aggiornamento della cache
VCD/VAT in Aurora. La build `psarc-vertex-layout-20261006` è stata installata
il 7 ottobre e verificata rileggendo 14/14 file dell'app, con `ATTRIBUTE2=12`,
configurazione e PSARC invariati. Il gioco non è stato avviato automaticamente;
la correzione visiva richiede una nuova prova sulla Vita.
[Consegna del 7 ottobre](VITA_VERTEX_LAYOUT_DELIVERY_2026-10-07.md).
[Diagnosi, correzione e identità della candidata](VITA_VERTEX_LAYOUT_2026-10-06.md).
Le consegne e diagnosi sotto descrivono le sessioni precedenti.

Consegna precedente: **`psarc-pthread-complete-20261006`**, installata sulla
PS Vita `192.168.1.79:1337` e verificata rileggendo tutti i 14 file dell'app.
SELF `eca6ac41e1b4…`; solo eboot.bin è cambiato. Config.toml è identico,
PSARC e relativa dimensione/metadati invariati, ISO assente. Il precedente
SELF è conservato. Il gioco non è stato avviato automaticamente.
Identità e verifica: [consegna pthread completa](VITA_PTHREAD_FIX_DELIVERY_2026-10-06.md).

Ultimo crash, precedente al test attuale: il dump **1791319267**, abbinato al SELF `14c43f7a71cc…`,
conferma una regressione prima di main. Il gate GCC è attivo ma pthread_once
e quattro primitive condition restano irrisolte; il guard C++ chiama
pthread_mutex_lock con un puntatore nullo durante i costruttori statici.
Le verifiche SceFiber/TLS non sono state eseguite. Il log scaricato è quello
della sessione precedente e non va attribuito a questa build.
Diagnosi e riproduzione ARM: [report del crash iniziale](VITA_CRASH_STARTUP_2026-10-06.md).

La correzione **`psarc-pthread-complete-20261006`** include l'archivio pthread completo, con
27 primitive definite e nessuna irrisolta. Il nuovo audit rifiuta il binario
arrestato e controlla anche le chiamate reali dei guard C++.
Passano gli audit GXM e dispatcher nativo; il SELF è identico a quello nel VPK.
L'archivio della consegna è
`build/vita-psarc/install-pthread-complete-20261006T205755Z`.
I controlli firmware sono ora passati nella nuova cattura; menu e gara restano da verificare.

## Consegna arrestata e limiti dell'audit precedente

Consegna precedente: **`psarc-native-context-20261006`**, installata sulla Vita
e verificata su tutti i 14 file dell'app; solo eboot.bin è cambiato.
SELF `14c43f7a71cc…`. La build collega la vera pthread_cancel, usa SceFiber
al posto dello switch libco sulla Vita e crea il mixer dalla pila nativa.
Il bootstrap include verifiche di thread, TLS, switch/ritorni nativi e d8-d15.
7/7 contratti host e il precedente audit limitato del binario erano passati;
il test manuale ha esposto il collegamento incompleto sopra descritto.
I controlli firmware restano da raggiungere. Configurazione e PSARC invariati.
Vedere la [consegna SceFiber/pthread](VITA_CONTEXT_FIX_DELIVERY_2026-10-06.md).

## Diagnosi precedente e motivazione della candidata

Aggiornamento dopo il test: il quinto dump `1791316902` è abbinato al SELF
installato `98861d6a8649…`. La candidata supera l'attesa VI e raggiunge AX/NW4R
con letture di `revo_kart.brsar` dal PSARC, poi si arresta nel mixer.
Il gate C++ dei thread nel binario restituisce sempre zero perché
`pthread_cancel` rimane weak undefined: sono confermati 50 fallimenti del
worker e il ramo TLS globale. Un probe ARM separato verifica che includere
la vera implementazione pthread corregge il gate. SP continua a essere
esterno alla pila nativa; menu e gara non sono verificati.
Diagnosi, prove e ordine dei prerequisiti prima di altre build del gioco:
[report audio e runtime](VITA_CRASH_AUDIO_2026-10-06.md).
Il runtime di produzione non è stato modificato in questa analisi.

Alla consegna precedente, la candidata `psarc-native-wait-20261006` implementava il confine
nativo delle attese VI/idle, GX retry, GKI e pausa panic. Sono passati 7/7
contratti host e i controlli ARM, collegamenti delle attese e GXM sul binario
finale. La build è installata e verificata sulla console; tutti i 14 file
dell'app coincidono con il pacchetto. Non è stato inviato alcun comando di avvio.
Identità, implementazione e verifiche sono nel
[report di consegna delle attese](VITA_WAIT_FIX_DELIVERY_2026-10-06.md).
Il test successivo ha superato quel punto di crash ed esposto i problemi
descritti nel nuovo report; menu, gara e correttezza visiva restano da verificare.

La precedente candidata `psarc-native-log-20261006` superava il crash
`OSReport`, stampava l'intestazione completa di `StaticR.rel` e il messaggio
di caricamento del modulo. Il suo log registrava la presentazione numero 512
e cinque letture PSARC senza fallimenti. Il quarto dump fermava
`sceKernelDelayThread` nel percorso VI/idle sulla pila libco: diagnosi e
binario abbinato sono nel
[report del crash retrace](VITA_CRASH_RETRACE_2026-10-06.md).
Le catture precedenti sono nei report [log](VITA_CRASH_LOGGING_2026-10-06.md),
[DVD](VITA_CRASH_DVD_2026-10-06.md) e [GXM](VITA_CRASH_PSARC_2026-10-06.md).
Il resto di questo documento descrive la build precedente alle correzioni
dei confini nativi.

Sorgente dati attuale sulla Vita: il solo `mariokart.psarc`. L'utente ha rimosso
la ISO; la verifica della directory dati conferma il PSARC e l'assenza di
`mariokart.iso`. La configurazione seleziona `paths.dvd_psarc`; il percorso ISO
ancora scritto in `paths.dvd_iso` è ignorato. La ISO sul computer serve alla
preparazione e ai controlli del pacchetto, senza occupare spazio sulla console.

## Stato verificato del checkout

Branch principale `codex/restart-iso-gxm-20261005`, HEAD WiiCompiled
`279ce8328f17059a4b46172d4ce71d3f403a1aec`, con un ampio insieme di modifiche
locali già presenti e parzialmente staged prima di questa ripresa.
Non sono state annullate né committate automaticamente.

Aurora è ora un submodule, da `robin994/aurora-vita`, HEAD
`28512bd75531508276be1582882145ea021151eb`, sopra la base upstream
`2f2259b055a14cc96fc2bb40cd824e05b8017dbc`. Il submodule era pulito e non è
stato modificato in questa ripresa. Il pin `39fb53e` del primo report è storico.

Il checkout contiene nuove correzioni a thread/fiber, interrupt, NAND, GX e
servizi Vita, oltre a una bozza PSARC. Il lettore ISO aveva già ricevuto un
worker nativo per tenere le syscall I/O fuori dagli stack libco.
La bozza PSARC usava FIOS2 ed era priva di un pacchetto reale verificato.

Ci sono nuovi dump kernel, incluso `crashdumps/psp2core-1791277863.raw`.
L'analisi di base riporta il thread `WICMKW001` fermato con codice `0x10006`
in `SceLibKernel`, e thread GXM/audio/Aurora presenti. Questo dato non è una
prova di menu o gara funzionante. La correzione del problema va dimostrata
contro dump e binario abbinati. Il file `runtime.log` nella radice contiene
marker vitaGL di una precedente integrazione e non valida il nuovo GXM.

Il precedente manifest del gioco è datato 6 ottobre, 09:01 UTC, con SELF
`e58b738ac14d9c642e4cd5c3791b0809390a28fb03d376520e13bb575e88d71f`.
La nuova build vive in `build/vita-psarc`; quella precedente non è stata
sovrascritta. Nessun nuovo pacchetto è stato installato automaticamente.

## Implementazione PSARC completata

Il packer Python e il lettore C riusano la pipeline Strikers. La provenienza
è in [PSARC_UPSTREAM.md](../scripts/PSARC_UPSTREAM.md), con licenza CC0 del
container e licenza GPL del lettore locale. Il lettore non dipende da FIOS2.

La preparazione host legge `mariokart.iso`, decifra i cluster e controlla H0.
Archivia tutti i file FST e le viste di sistema già esposte dal core, un indice
degli offset originali e i primi `0x50000` byte raw per identità e metadati del
disco. Non rimuove lingue, filmati o contenuti di gioco. La ISO sorgente resta
intatta. I file temporanei sono eliminati solo dalla directory creata dalla
specifica invocazione del tool.

L'archivio conserva i payload originali non compressi, con blocchi logici da
64 KiB e deduplicazione SHA-256. Il vantaggio viene dall'eliminazione di padding,
overhead cifrato e contenuti esterni alla partizione di gioco indicizzata,
insieme alla condivisione dei file identici. Non viene introdotta decompressione
dei payload di gioco sulla Vita. Non è una conversione di texture o geometria.

Il runtime apre il PSARC tramite un worker nativo `wic_psarc_io` e usa letture
posizionali: il numero di syscall non cresce con i confini dei blocchi stored.
I chiamanti guest pubblicano una richiesta e attendono la risposta senza
entrare nel kernel dal proprio stack libco. L'attesa attuale è attiva; il worker
rimane disponibile fino alla fine del processo e il suo handle archivio viene
chiuso da `Shutdown`. Il costo CPU di questa attesa richiede misura hardware.

L'indice è controllato per dimensioni, percorsi, duplicati e limiti della
partizione. Al mount DVD sono verificate presenza e dimensione di ogni file.
Le letture assolute e low DVD convertono gli offset Wii da parole a byte a
64 bit, possono attraversare file adiacenti e mantengono callback ed esito
di completamento del runtime. Le copie in RAM guest mantengono la notifica DMA.
Il disco ID viene letto dall'header reale conservato, senza sintetizzarlo.

Il formato è un archivio di file di gioco, non una copia reversibile dell'intera
immagine cifrata. Letture di padding scartato e settori raw fuori dal segmento
conservato falliscono esplicitamente. Se un percorso reale del gioco richiede
quei byte, occorre aggiungere gli intervalli necessari e riconvertire il
pacchetto; non inventare padding zero. Le normali letture file sono limitate
alla loro dimensione logica come nel contratto DVD già presente.

`paths.dvd_psarc` seleziona esplicitamente il backend. Un archivio scelto ma
mancante o invalido è un errore; non viene usata automaticamente una ISO
diversa. Con `dvd_psarc = ""` rimane il percorso ISO. La build corrente verifica
gli eseguibili e avvia automaticamente il guest, come il codice già presente
prima di questa ripresa; il vecchio launcher a due pressioni non è più il flusso
attuale. I log riportano il marker `build=psarc-native-20261006`, la revisione
Aurora effettiva e contatori PSARC ogni 300 frame.

## Spazio e identità misurati

| Dato | Byte |
| --- | ---: |
| ISO originale | 4.699.979.776 |
| Payload logici inclusi, prima della deduplicazione | 2.685.952.816 |
| Payload identici condivisi | 43.946.716 |
| PSARC completo | 2.642.224.628 |
| Risparmio rispetto alla ISO | 2.057.755.148 |

Riduzione **43,78%**, da circa 4,70 GB a 2,64 GB decimali. 2.038 file
nell'archivio, 1.986 payload unici. I 2.036 file indicizzati del disco sono
accompagnati da `index.bin` e `raw/header.bin`.

- ISO SHA-256: `2fc548bad45f373bad953d410a32e042e7efeef86a90f9ff63ddc5b62f9e6971`.
- PSARC SHA-256: `95f9b1151ccd10e697ffd5e62b8c1eff9b9bd328a3c9a26128a8d1fc47883f1c`.
- DOL SHA-256: `80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05`.
- REL SHA-256: `16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d`.

Pacchetto locale: [mariokart.psarc](../build/vita-data/mariokart.psarc).
Inventario con SHA-256 di ogni file e verifiche:
[mariokart.json](../build/vita-data/mariokart.json).

La sola riduzione dell'archivio non prova un aumento degli FPS. La rimozione
della decifratura e dei controlli H0 durante le letture Vita può ridurre il costo
di caricamento, ma il risultato deve essere misurato sul dispositivo.

## Verifiche eseguite

- 4/4 contratti core: ISO, indice disco, PSARC e copie EFB colore.
- Stessi 4/4 contratti con AddressSanitizer e UndefinedBehaviorSanitizer.
- 648/648 test del traduttore e 18/18 test host della revisione Aurora corrente.
- PSARC: deduplicazione, output deterministico, case-insensitive lookup,
  blocchi completi/parziali, file vuoti, EOF, symlink, TOC/codec malformati,
  troncamento e rilevazione di payload che differiscono dalla sorgente.
- Indice: letture a offset sopra 2 GiB, attraversamento di file adiacenti,
  rifiuto dei buchi prima di emettere letture, metadata troncati e percorsi
  invalidi.
- Tutti i payload archiviati confrontati via SHA-256 e tramite il lettore C
  del runtime con i file temporanei ricavati dalla ISO.
- Confronto indipendente del PSARC con la ISO: identità dell'indice, di ogni
  file tramite letture per offset e dell'header raw. 2.685.529.784 byte di file
  confrontati, oltre a indice e header. Nessuna differenza.

Log delle verifiche: `build/status-tests-20261006.log`,
`build/psarc-sanitize.log`, `build/psarc-preparation.log`,
`build/psarc-iso-compare.log`.

## Pacchetto Vita verificato

Build nativa completata: ELF, map, VELF, SELF e VPK. Audit finale di **44.924**
simboli eseguibili: GXM draw/present presenti, nessun GL/vitaGL/vita2d.
I SELF contenuti nei VPK coincidono con quelli prodotti dalla build.
La build segnala il warning VitaSDK di `crtn.o` senza `.note.GNU-stack`
e warning `always_inline` del codice generato; non ci sono errori di build.

Tutte le opzioni WIC sperimentali e GX asincrono sono OFF; backend GXM.
Il manifest include sorgenti, input generated, strumenti host, pin Aurora,
identità ISO/PSARC e hash di ELF/map/VELF/SELF/VPK.

| Artifact | Byte | SHA-256 |
| --- | ---: | --- |
| [wiicompiled-vita-iso-check.vpk](../build/vita-psarc/wiicompiled-vita-iso-check.vpk) | 704,424 | `d842552aa9873d52c2e27efdf045a1ed17998118264c501debad8fc59df07698` |
| [wiicompiled-vita-game.vpk](../build/vita-psarc/wiicompiled-vita-game.vpk) | 22,741,998 | `933204fb601eebaa8fe966f0e660588faa0f23ece69f19ade7cbd05afe4c5567` |
| [game-eboot.bin](../build/vita-psarc/game-eboot.bin) | 22,940,187 | `dbdd3541d79ff5ce88f310eca9efe9c0970d8317332398a5c90b88985a4e8aaa` |

SHA-256 cumulativo dei 31,007 input sorgenti/generated/tooling: `9a2f978feb7c2d24cc53a2b4f8472883dd1509b975af02c8eff4478c768e18d4`.

Manifest: [build/vita-psarc/DELIVERY.json](../build/vita-psarc/DELIVERY.json).
Log finale: `build/vita-psarc-verification.log`. Questa build non è stata
eseguita sulla console: non prova che il precedente crash sia risolto,
né che menu, gara, effetti e prestazioni siano corretti.

## Riprendere e usare il pacchetto

```sh
./build-vita.sh data
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc ./build-vita.sh game
```

Il primo comando rifiuta un output esistente: usare `data --replace` per
sostituirlo dopo nuova verifica. Il secondo presuppone che i sorgenti generated
corrispondano al profilo PAL già validato.

Copiare il solo PSARC in `ux0:data/wiicompiled-vita/mariokart.psarc` e impostare
la chiave nel blocco `[paths]` del `Config.toml` esistente, senza creare blocchi
o chiavi duplicati:

```toml
dvd_psarc = "ux0:data/wiicompiled-vita/mariokart.psarc"
```

La ISO rimane sul computer come sorgente del convertitore; sulla console basta
il PSARC. I salvataggi continuano a usare `paths.nand_root`.

## Priorità residue

1. Provare il nuovo SELF sul dispositivo con il solo PSARC, conservando
   log e hash installato. Verificare ingresso nel guest, menu e caricamento
   pista. Una build host non risolve il dump kernel per definizione.
2. Analizzare qualunque nuovo dump con ELF/map/VELF abbinati; confermare che
   l'I/O archivio passi dal worker nativo e cercare altre syscall su stack libco.
3. Verificare una gara completa, effetti EFB/profondità, input, audio, NAND e
   sospensione/ripresa. Il supporto EFB depth resta incompleto.
4. Misurare tempo e CPU dei caricamenti PSARC, attesa del worker, RAM e
   cache. Se l'attesa attiva domina, sostituirla mediante un percorso cooperativo
   del scheduler guest che ritorni sullo stack nativo, preservando i callback.
5. Stabilire la baseline di gara e profilare le funzioni PPC calde. Proseguire
   poi con ABI, materiali/shader GXM, texture native e vita della geometria
   residente per pista. Le opzioni sperimentali restano disabilitate nel VPK
   iniziale per questo confronto.
