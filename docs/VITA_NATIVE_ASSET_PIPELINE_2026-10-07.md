# WiiCompiled Vita: preparazione degli asset nativi nel PSARC

Aggiornamento del 2026-10-08: la candidata con risorse residenti è descritta
nell'ultima sezione. Le identità e le opzioni delle sezioni precedenti
documentano la build precalcolata conservata come controllo.

La richiesta è spostare sul computer la conversione di texture, audio e
geometria statica, mantenendo un solo archivio sulla Vita. La candidata
`psarc-precompiled-assets-async-20261008` aggiunge il produttore offline e i
consumatori nel renderer GXM e nell'AX del gioco. Il dispositivo usa
`mariokart.psarc`, senza ISO o albero di file estratti.

La conversione è implementata per gli asset supportati descritti sotto.
Il miglioramento degli FPS e la copertura effettiva dei percorsi della
Vita richiedono ancora una prova. Preparare un record sul computer non
dimostra che ogni draw o voce audio lo utilizzi durante il gioco.

## Base conservata

La build di controllo è `psarc-memory-video-present-20261007`, con SELF
`c64ee93c399d2b4593e87c58565b2606755e4116ee587e9450dc0a017a088752`.
L'utente ha raggiunto la selezione personaggi e confermato modelli visibili,
con animazioni lente. Le finestre finali del log associato misurano circa
0,15–0,20 FPS; non sono una misura GPU esclusiva.

La nuova candidata conserva le correzioni di memoria, video e presentazione,
FMA esatta, intervalli RAM veloci, input e ATTRIBUTE2=12. Su richiesta
esplicita la build finale attiva GX asincrono. Restano disattivati geometria
residente, il profilo BC1 sperimentale e la nuova topologia dei core. Il
passaggio modifica il consumo degli asset precalcolati e abilita il lettore
zlib già esistente per i relativi payload freddi.

## GX asincrono

L'opzione `AURORA_VITA_ASYNC_GX=ON` ora è rispettata dal progetto Vita;
prima il suo CMake la forzava a OFF. `GXInit` avvia il consumer FIFO Aurora,
che decodifica GX e prepara/invia il rendering GXM. La richiesta di creazione
del thread usa la maschera CPU2; il core realmente usato va confermato sulla
console. I due helper CPU esistenti mantengono la configurazione precedente.

La fine del frame conserva `run_sync`: il producer aspetta il completamento
del lavoro del consumer prima di riciclare le risorse guest. Rimangono anche
le barriere sulle scritture e gli snapshot immutabili delle display list.
L'async opera all'interno del frame; non autorizza il riuso anticipato dei
buffer del frame precedente.

Il flag di avvio è privato al target Aurora GXM. Le unità PPC/runtime non
usano quel macro e mantengono la vecchia definizione `ASYNC_GX=0`; il loro
valore non seleziona lo scheduler. Il marker usa `WIC_GX_ASYNC_BUILD=1`.
L'audit verifica il codice ARM effettivo: chiamata da `GXInit` a
`start_worker`, creazione/avvio del thread e chiamata da `end_frame` a
`run_sync`. Il log `[gx-worker] requested=1 running=1` conferma l'avvio reale
solo dopo una prova sulla Vita.

I contatori dei nuovi asset grafici sono atomici, perché il consumer li
aggiorna mentre il gioco campiona le diagnostiche. Un test con quattro
thread verifica le letture concorrenti e il conteggio dei caricamenti.
La configurazione del reader avviene prima dell'esecuzione guest e resta
fissa per la durata dei worker.

La candidata sincrona preparata prima di questa richiesta resta archiviata
in `build/vita-psarc/candidate-precompiled-assets-sync-20261007T220701Z`:
usa gli stessi dati ma precede questi aggiustamenti per l'async. È un
controllo recuperabile, non una coppia A/B a sorgenti identici. La versione
installata sulla Vita non è stata cambiata durante questa preparazione.

## Compilazione riproducibile

La candidata usa la seguente selezione esplicita; la preparazione del guest
è stata eseguita prima di questo passaggio:

```sh
WIC_SKIP_PREPARE=1 WIC_BUILD_DIR=build/vita-psarc WIC_JOBS=8 \
./build-vita.sh game \
  -DAURORA_VITA_ASYNC_GX=ON \
  -DWIC_PRECOMPILED_ASSETS=ON -DWIC_ASSET_ZLIB=ON \
  -DWIC_EXACT_FLOAT_FMA=ON -DWIC_FAST_RAM_RANGES=ON \
  -DWIC_CPU_DIAGNOSTICS=ON -DWIC_NATIVE_CALL_PROFILE=ON \
  -DWIC_HARDWARE_AVC=ON -DWIC_NATIVE_THP=OFF \
  -DWIC_NATIVE_TEXTURES=OFF -DWIC_DISTINCT_CPU_CORES=OFF
```

`build-vita.sh` mantiene `WIC_FAST_PPC_MATH`, `WIC_RESIDENT_GEOMETRY` e
`WIC_PROFILE_GUEST` a OFF. Le diagnostiche attive non rendono questa build
un benchmark neutrale: una successiva misura senza diagnostiche deve
conservare le altre opzioni e la stessa identità dei dati.

## Contenuto preparato

| Categoria | Scansione e risultato |
| --- | --- |
| Texture TEX0/TPL | 32.909 occorrenze; 5.867 record unici, comprese le varianti dei prefissi mip |
| Modelli MDL0 | 11.169 occorrenze; 6.486 draw idonei, deduplicati in 1.343 record geometrici |
| Musica BRSTM | 111 file: 87 DSP ADPCM precalcolati, 24 già PCM |
| Banche audio BRSAR | RWAV della banca principale preparati; il vecchio RBNK/WAVE di HomeButtonSe conserva il decoder originale |
| Audio preparato | 37.960 blocchi PCM unici da 35.697 richieste sorgenti |
| Filmati | I 27 video H.264 del PSARC AVC restano invariati; decoder hardware Vita già presente |
| File originali | Tutti i 2.039 file del PSARC AVC conservati senza modifiche |

Il parser apre Yaz0 e U8 sul computer e percorre BRRES, TEX0, PLT0, TPL e
MDL0 revisioni 8/9/11. Il pacchetto contiene i file originali del disco e
`native/v1/`, con indici e record verificabili; queste directory sono
interne al PSARC. La directory temporanea estratta esiste solo sul computer.

Le texture sono preparate nei layout e formati accettati dal renderer
attuale: I8, IA8, RGB565 o RGBA8. I sette record che potevano diventare BC1
sono ricompilati senza BC1, per essere consumabili con il flag esistente
disattivato. Non viene applicata una nuova compressione con perdita.

La geometria preparata contiene vertici canonici nello spazio del modello
e indici dei triangoli. Il percorso collegato è quello degli upload
streaming. Matrici correnti, illuminazione CPU e texgen continuano a usare
il worker originale: una posa precalcolata non può sostituire l'animazione
del personaggio. I draw indicizzati con almeno 48 vertici possono usare il
record; draw più piccoli, linee, punti e draw reindicizzati esplicitamente
mantengono il percorso normale. Gli oggetti non sono ancora mantenuti
residenti in CDRAM per area.

L'audio è PCM16 little endian per blocchi di massimo 1.024 frame DSP.
Ogni blocco conserva byte ADPCM, coefficienti e storie dei frame, necessari
per riconoscere correttamente i buffer caricati dal gioco. L'AX usa il PCM
soltanto quando frame sorgente, coefficienti, predictor e storia coincidono.
Loop, indirizzi, stato della voce e resampling restano quelli del gioco.
Un buffer modificato o una storia diversa riattivano la decodifica originale.

## Correttezza e memoria

Un hash seleziona il record grafico; il consumatore confronta esattamente
descrittore, sorgente e palette e controlla il checksum del payload.
L'invalidazione GX globale non obbliga più a riconvertire pixel statici
identici. Il fallback conserva il contratto di invalidazione e non altera
gli snapshot GPU ancora in uso. Le ricerche grafiche assenti terminano
nell'indice senza I/O né una cache negativa che cresce con i frame video.

Le cache dei record grafici e audio sono limitate a 2 MiB ciascuna. L'indice
audio conserva digest binari, espandendo il percorso solo su una ricerca
che carica un blocco, per evitare decine di migliaia di stringhe permanenti.
Gli indici del PSARC e degli asset occupano comunque memoria aggiuntiva:
il consumo complessivo e la capacità di proseguire fino alla gara restano
da verificare sulla console. Il limite delle cache non è un limite di tutta
la RAM necessaria al pacchetto.

Il PSARC conserva i file guest senza nuova compressione; applica zlib solo
ai payload audio/texture/geometria precalcolati. La lettura e la decompressione
fredda avvengono sul worker I/O nativo esistente. Rimangono costi di lookup,
verifica, copie, upload e possibili ricaricamenti alla saturazione delle cache.
Questa implementazione non costituisce un percorso zero copy.

## Verifica audio

Il convertitore riproduce la formula AX del port, inclusi arrotondamento
`+0x400` e saturazione `[-32767,32767]`. La prima versione del produttore
usava un limite negativo diverso: il caso saturato è stato aggiunto al
test e tutti i blocchi consegnati sono rigenerati con il limite corretto.

Il confronto sui primi e ultimi blocchi di `o_Option_32.brstm`, per entrambi
i canali, usa il vero `Accelerator::ReadSample()` con il percorso nativo
disattivato. Il PCM preparato e i record del pacchetto coincidono con tale
output. Il test sintetico percorre tre blocchi nativi, 2.050 frame DSP,
task AX da 96 campioni, loop, saturazione e modifiche a metà frame; verifica
anche il parametro della voce finale.

FFmpeg 8.1.2 non è un riferimento bit exact per questo AX: tronca la
predizione ADPCM, mentre il port aggiunge l'arrotondamento usato anche
dall'acceleratore DSP di Dolphin. Una formula senza tale arrotondamento
riproduce esattamente l'output FFmpeg del primo blocco; la differenza è
documentata in `NATIVE_AUDIO_REFERENCE_VALIDATION.json`. Fonti:
[FFmpeg ADPCM](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/adpcm.c),
[Dolphin DSPAccelerator](https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/DSPAccelerator.cpp).
La saturazione inferiore del port è una scelta preesistente mantenuta,
non una nuova dichiarazione di emulazione DSP bit exact su hardware Wii.

## Verifiche di consegna

- 20 test host, incluso il nuovo contratto PSARC/indici/audio/AX.
- 18 test Aurora, con confronto pixel/layout, geometria dopo cambi di matrice,
  letture concorrenti dei record e barriere/ownership del frontend asincrono.
- 2 contratti eseguiti anche con AddressSanitizer e UndefinedBehaviorSanitizer:
  asset nativi e archivio PSARC.
- Controllo integrale del pacchetto e confronto dei 2.039 originali con gli hash
  della sorgente AVC. Il lettore C/C++ usato dal port rilegge e confronta anche
  ogni payload compresso con l'output del produttore.
- Audit ARM/GXM/runtime/dispatch/VPK, chiamate ai nuovi consumatori e percorso
  di avvio del worker GX asincrono nel binario collegato.
- SELF nel VPK identico a quello archiviato; ATTRIBUTE2=12 verificato nel SFO.

Il test host dei worker Aurora applicava una scadenza di tre secondi anche
ai worker inattivi. Durante la compilazione pesante questo produceva un
timeout senza un job perso. Lo shim ora mantiene l'attesa inattiva senza
scadenza, come la chiamata Vita con timeout nullo, e conserva il controllo
di tre secondi sul completamento dei job. Il pool del runtime non cambia.
Un altro caso assumeva che CPU3 ricevesse una fetta di un job molto breve
prima che gli altri thread lo esaurissero. I test di ammissione ora tengono
ferme le altre lane fino al primo task CPU3, con scadenza di tre secondi.
Non cambia la politica del runtime: uno scheduler dinamico può correttamente
far terminare il lavoro alle lane avviate prima. Le prove conservano anche
i risultati falliti, insieme al risultato finale.

## Spazio e artefatti

Il VPK prodotto è `build/vita-psarc/wiicompiled-vita-game.vpk`. Una copia
della candidata, comprensiva di simboli, sorgenti e prove, è archiviata in
una directory dedicata; `build/precompiled-assets-async-candidate-path.txt`
ne registra il percorso esatto. `DELIVERY.json`,
`NATIVE_ASSETS_LINKAGE.json`, `ASYNC_GX_LINKAGE.json`, `VALIDATION.json` e `CHECKSUMS.json` vincolano
binari, opzioni, dati e risultati dei test. La directory candidata conserva
ELF, map, VELF, SELF, VPK e una copia verificata dei sorgenti.

| Artefatto finale | Dimensione | SHA-256 |
| --- | ---: | --- |
| VPK async | 24.975.997 byte | `57135b272ac0c21a8c9b06a75d4ec3ad170f4b21b307de9621d447fd89e66f61` |
| SELF incluso nel VPK | 25.158.718 byte | `7e0c2e792808d50410cbfba4056014f58714e39b03cf9d96a15dc54bfae1ad53` |
| ELF con simboli | 74.346.756 byte | `84b4a2beca0e89c56f76075efb68a36980c8449e832462559a1ab5288e7ecf44` |
| PSARC nativo | 3.219.564.087 byte | `91445d13cb3096355d0eb2b4265c36cff970c1acc4628006ba976124c55750de` |

Il PSARC AVC di controllo occupa 1.406.613.154 byte. Il nuovo archivio è
3.219.564.087 byte (circa 3,22 GB): il risparmio di conversione sulla console richiede circa
1,81 GB aggiuntivi rispetto al pacchetto AVC. La ISO sorgente host occupa
4.699.979.776 byte. La preconversione estesa non è una riduzione ulteriore
del pacchetto AVC. Gli originali sono necessari alle strutture e ai decoder
del gioco; eliminarli richiederebbe una sostituzione di quei consumatori,
non una semplice cancellazione di file.

PSARC SHA-256: `91445d13cb3096355d0eb2b4265c36cff970c1acc4628006ba976124c55750de`.

## Uso e prova sulla Vita

La candidata non è installata né avviata durante la preparazione.
Per provare il percorso precalcolato occorrono **sia il nuovo VPK sia il
nuovo PSARC**. Installare il solo VPK con il vecchio PSARC attiva il fallback
e non verifica la preconversione. Il file dati va selezionato nello stesso
percorso di configurazione, normalmente
`ux0:data/wiicompiled-vita/mariokart.psarc`; sulla console basta un solo
PSARC attivo, senza ISO o file estratti. I salvataggi e la configurazione
esistenti devono essere conservati. Il pacchetto AVC e la build precedente
sono conservati sul computer per tornare al controllo.

Avviare manualmente e verificare avvertenza, card, selezione licenza,
"1 player", selezione personaggi e, se possibile, ingresso e completamento
di una gara. Il log deve contenere:

```text
[boot] build=psarc-precompiled-assets-async-20261008 ... precompiled_assets=1 gx_async=1
[gx-worker] requested=1 running=1
[native-assets] catalogue=7210 textures=5867 geometry=1343 ...
[native-audio] indexed_blocks=37960 ...
[native-assets] frame=... texture_hits_total=... geometry_hits_total=...
[native-audio] frame=... hits_total=... misses_total=... rejected_total=...
```

Gli hit devono crescere prima di attribuire un beneficio ai record offline.
Gli stessi menu e percorso del controllo servono per valutare copertura,
frame rate, memoria e stabilità. Le catture del framebuffer e le diagnostiche
con finish serializzanti devono restare distinte dalle misure senza quel costo.

## Lavoro che rimane

Il target di 30 FPS è aperto. Questa modifica sposta conversioni statiche
fuori dal dispositivo, ma non elimina il costo di creare e distruggere texture,
hash e copie, upload, attese GXM, esecuzione del codice PPC, animazione,
resampling/mix audio e traduzione degli stati TEV. Le texture video, EFB e
quelle generate durante l'esecuzione restano dinamiche. Anche le immagini
del Mii creato dal salvataggio non possono essere tutte precalcolate dalla ISO.

Il prossimo intervento dipende dagli hit e dai costi misurati della candidata:
il riuso verificato delle texture piccole e la geometria residente sono ora
implementati nella candidata del 2026-10-08 descritta sotto; la loro efficacia
sulla Vita e una politica di caricamento per area restano da verificare.
Il vecchio banco HomeButtonSe, i draw esclusi e la preparazione dei
programmi shader/materiali sono limiti espliciti della pipeline attuale.

## Candidata con risorse residenti, 2026-10-08

La richiesta è abilitare la residenza in CDRAM e riutilizzare anche le
texture che il gioco presenta ripetutamente come temporanee. La candidata
successiva include la correzione del crash durante il caricamento:
[analisi e correzione](VITA_CRASH_RACE_LOADING_2026-10-08.md). Il marker è
`psarc-resident-assets-async-oom-20261008`. GX asincrono e ATTRIBUTE2=12 restano
attivi. Il PSARC già installato non cambia: non occorre ricopiare i dati.

Il log della precedente candidata, conservato in
`device-runs/async-native-menu-20261008T070040Z`, mostra una finestra lenta
con 836,2 creazioni di texture per frame. Le ricorrenze principali sono
I8 64×4 e IA8 4×4. Questa misura conta le creazioni, non il tempo GPU:
la nuova cache interviene su quel costo, ma non dimostra un guadagno di FPS.

### Geometria

`WIC_RESIDENT_GEOMETRY=ON` attiva la cache immutabile esistente con un
budget di 8 MiB, la collocazione dei buffer in CDRAM e il percorso GXM
per le trasformazioni e l'illuminazione supportate. Vertici e indici degli
oggetti idonei vengono caricati una volta e conservati fino all'espulsione
per pressione sul budget. Matrici e uniformi correnti restano separati:
la residenza non congela l'animazione.

Restano i controlli esatti delle sorgenti, il fallback CPU per i draw non
supportati, il ritiro differito e la barriera GPU prima di liberare buffer
ancora utilizzabili. La cache è limitata e usa il riuso recente; non è
ancora una gestione dell'intera scena per area. I record geometrici offline
già presenti nel PSARC sono utilizzati quando compatibili.

### Texture piccole ricorrenti

Le texture native GXM erano già allocate in CDRAM. Mancava il mantenimento
di copie riutilizzabili per molte sorgenti marcate come temporanee.
`WIC_RESIDENT_TEXTURES=ON` aggiunge un indice per contenuto, con una quota
massima di 8 MiB **compresa** nel budget texture complessivo di 24 MiB.

Il descrittore, i pixel GX e la palette selezionano lo snapshot. L'hash è
solo un indice: prima di riusare l'handle si confrontano esattamente i
byte conservati. Un altro indirizzo guest, una nuova revisione o una
invalidazione globale con gli stessi pixel non impongono un nuovo upload.
Un cambiamento dei pixel o della palette richiede invece una copia diversa,
anche all'interno dello stesso frame. La copia GPU precedente resta
immutabile. Le invalidazioni degli indirizzi guest non distruggono queste
copie private; la ricerca successiva verifica il contenuto aggiornato.

Sono ammessi al massimo 1.024 snapshot, con sorgente più palette non oltre
4.096 byte, un solo livello mip e senza generazione di mip. Le copie CPU
dei pixel e delle palette sono quindi limitate a 4 MiB, oltre ai metadati.
Gli snapshot usati nel frame corrente non vengono espulsi. A saturazione
si eliminano quelli meno recenti; se non è possibile, resta il normale
upload temporaneo senza superare il budget texture. L'eliminazione passa
dall'ownership GPU e dalle sincronizzazioni esistenti.

Le texture grandi e con mip conservano la cache precedente; i piani video
grandi conservano l'anello di streaming. EFB, video e immagini generate
dal gioco non possono essere tutti preconvertiti dalla ISO. Una piccola
texture prodotta a runtime viene convertita al primo contenuto distinto,
poi riusata se i byte coincidono.

Il pool CDRAM del renderer resta quello già riservato di 64 MiB. Una lettura
di 71 MiB complessivi non equivale a 71 MiB di texture effettivamente
occupate: include pool e altre allocazioni. Le quote non vengono sommate
alla riserva per ogni texture; la geometria prende spazio libero nel pool.
Non viene aumentata automaticamente l'intera riserva a 112 MiB.

### Build e diagnostiche

La selezione della build precalcolata sopra viene integrata con:

```sh
-DWIC_RESIDENT_GEOMETRY=ON -DWIC_RESIDENT_TEXTURES=ON
```

Le opzioni arrivano dopo i valori di default del driver di compilazione.
Il valore iniziale delle due opzioni è OFF; una directory già configurata
può conservare opzioni precedenti nella cache CMake.
Per ricreare il controllo occorre specificare entrambe a OFF.

Il messaggio per ogni upload `compact_texture` passa da INFO a DEBUG.
Il livello normale conserva errori e contatori periodici. Si evita così
di scrivere una riga nel file di log per ogni piccola texture creata.

All'avvio il log registra `[resident-config]`; le finestre periodiche
`[resident-assets]` riportano hit, miss, upload, espulsioni, numero di
snapshot, byte GPU e copie sorgenti CPU, insieme a hit, miss e byte della
geometria. La lettura usa la diagnostica già pubblicata dal consumer:
non aggiunge una barriera GX per campionare la cache dal thread del gioco.

Il nuovo test della cache percorre 1.200 frame sintetici con gli stessi
pixel: una sola ammissione, con handle riutilizzato. Verifica anche aggiornamenti
nello stesso frame, palette, collisioni forzate, budget, limite di 1.024
entrate, espulsioni e modalità OFF. Questi sono controlli host; i contatori
della Vita devono confermare che gli upload diminuiscano nel menu reale.

`build/resident-assets-async-candidate-path.txt` identifica il pacchetto
archiviato con SELF, ELF, map, VELF, sorgenti e prove. La build precedente
rimane recuperabile. Installazione, resa dei modelli e delle texture,
stabilità e frame rate della nuova candidata richiedono una prova sulla Vita.
