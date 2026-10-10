# WiiCompiled Vita: video H.264, stato CPU e arresto dopo 1 player

**Aggiornamento successivo alla prova manuale:** la prima candidata H.264 è
stata installata; video assenti e uscita dopo un player sono confermati.
Il nuovo log mostra un invio AVC accettato senza output, trattato erroneamente
come errore dal wrapper, e il dump completo identifica l'intestazione azzerata
del blocco libero finale dell'heap. Diagnosi corrente, correzione del buffering
e prossima build: [video/heap del 7 ottobre](VITA_AVC_HEAP_FAILURE_2026-10-07.md).
Le sezioni seguenti conservano lo stato e le prove precedenti a questa prova.

Richiesta: sostituire i THP non funzionanti con video decodificabili dalla Vita,
ridurre lo spazio nel PSARC, continuare la diagnosi dei menu a 1 FPS e
dell'uscita alla LiveArea dopo la selezione di un player. Target: **30 FPS**.

## Esito della candidata THP nativa

La prova dell'utente è negativa: video non funzionanti, menu ancora percepiti
a 1 FPS, uscita invariata. Non è una candidata validata per il port.

Evidenza conservata in
`device-runs/native-thp-heap-20261007T141115Z/`. Il SELF riletto dalla console
coincide con la consegna:
`a61db43b35e9befb5e186b595433b2c4e6ca094db4db82fb004996c602b376b0`.
Configurazione:
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
Il kernel non ha prodotto nuovi dump; `crash.txt` e la sessione delimitata
dal marker della build conservano l'arresto PPC.

Il decoder JPEG nativo interviene almeno sul filmato 512x192: circa 13,6–14,4 ms
di decodifica e 0,29–0,35 ms di preparazione delle texture. I tre percorsi di
esecuzione elaborano ciascuno 1.536 tile. Questo dimostra l'intervento del
decoder e degli helper, **non la correttezza dell'immagine sullo schermo**.

È stato trovato un errore nel nuovo percorso: dopo le scritture dirette alle
texture mancava `GxNotifyGuestRamDmaWrite`. Il percorso SDK originale pubblica
queste scritture attraverso le operazioni di locked-cache DMA. La notifica
aggiorna le cache GX e Aurora. La correzione viene eseguita dopo il completamento
di tutti i lavori di preparazione delle texture, sia per JPEG sia per AVC.
È coperta dal test del wrapper; l'effetto visivo sulla Vita resta da verificare.

## Conversione implementata

`scripts/convert_mkw_video.py` legge il PSARC originale e produce un nuovo
archivio. Converte tutte le 27 componenti video THP in **H.264 Constrained
Baseline, livello 3.2, YUV420 full-range**, con un IDR indipendente per ogni
fotogramma. Compressione con perdita, CRF 18, risoluzione originale.

L'involucro mantiene intestazioni, conteggio/frequenza dei fotogrammi, metadati,
componenti audio ADPCM e sequenza/loop del player. Ogni componente video porta
un'intestazione esplicita `WAVC` v1, con dimensioni e lunghezza controllate.
Non sono normali file THP JPEG: richiedono la build `WIC_HARDWARE_AVC=ON`.
La Vita continua a usare un solo PSARC, senza ISO duplicata o cartelle di
asset estratti. I file temporanei della conversione esistono soltanto sul Mac.

La scelta di mantenere l'involucro del player evita la sostituzione della
sincronizzazione e del sistema di texture con un lettore MP4 separato.
Gli IDR indipendenti permettono salti, loop e alternanza di video senza
dipendenze tra fotogrammi di filmati diversi; comprimono meno di una sequenza
H.264 con riferimenti temporali. Tale variante resta un'ottimizzazione futura.

Le dimensioni dei filmati e le tabelle dei componenti vengono rigenerate.
FST e indice disco conservano gli offset originali e aggiornano le lunghezze.
Una conversione che supera l'estensione originale è rifiutata. Il formato
include `disc/native-video-profile.bin`; una build senza AVC lo rifiuta
prima dell'avvio. Eseguibili e altri 2.009 asset sono invariati.

| Dato | Risultato host verificato |
|---|---:|
| PSARC originale | 2.642.224.628 byte |
| PSARC AVC | 1.406.613.154 byte |
| Risparmio | 1.235.611.474 byte, 46,76% |
| Filmati convertiti | 27 |
| Fotogrammi decodificati su host | 55.143 |
| Frequenze originali/SPS controllate | 50 / 59,94 FPS |
| Audio dei filmati | Componenti identiche byte per byte |

PSARC originale SHA-256:
`95f9b1151ccd10e697ffd5e62b8c1eff9b9bd328a3c9a26128a8d1fc47883f1c`.
PSARC convertito SHA-256:
`c92993c58cacc1a90223f9213173e2053e04165ec7115a8cd8867f420ce2f743`.

Archivio: `build/vita-data/mariokart-avc.psarc`.
Provenienza: `build/vita-data/mariokart-avc.psarc.json`.
Verifica codec: `build/avc-analysis/FULL_CODEC_VERIFY.json`.
La verifica include tutti i payload del nuovo PSARC con il lettore nativo C,
tutti i fotogrammi con FFmpeg, numero/dimensioni/profilo/range e clock SPS.
Il clock SPS è controllato direttamente: la frequenza inferita dal demuxer
di un flusso Annex B può descrivere i tick o usare un valore predefinito.
La sincronizzazione del gioco usa l'intestazione THP originale.

## Lettore Vita implementato

`vita/avc_hardware.cpp` usa le API pubblicate da
[VitaSDK SceVideoDec](https://docs.vitasdk.org/group__SceVideoDecUser.html):
inizializzazione AVCDEC, query memoria, creazione del decoder e
`sceAvcdecDecode`. Una sola istanza hardware, serializzata su pila nativa,
con un riferimento e memoria contigua non cached. Il limite di lavoro
704x768 copre tutti i filmati del disco, inclusi quelli 672x752.

Il decoder richiede output YUV420 packed raster. Il percorso prepara le tre
componenti Y/U/V e le texture GX I8 attraverso il pool già presente, quindi
pubblica le scritture. Gli errori non inviano una componente AVC al decoder
JPEG PPC: vengono restituiti al player con il normale esito SDK nonzero.
Allocazioni, import e geometria sono controllati, con rilascio delle risorse
quando la creazione o la decodifica fallisce.

**Resta una candidata sperimentale:** interpretazione dell'output hardware,
stride, crop, colore, alternanza delle risoluzioni e resa GXM devono essere
verificati sul dispositivo. I test di ciclo di vita simulano le API; la
compilazione ARM usa gli header reali VitaSDK. Nessuno di questi controlli
dimostra che il decoder della console abbia già restituito un'immagine valida.

Le opzioni Aurora restano quelle del controllo GXM. Non vengono attivati
async GX, nuove affinità CPU, texture native/CMPR, geometria residente o
approssimazioni matematiche. Cambia il percorso video: `WIC_NATIVE_THP=OFF`,
`WIC_HARDWARE_AVC=ON`. Diagnostica CPU e native-call attiva: nessuna misura
quiet A/B e nessuna prova di 30 FPS.

## Perché i menu restano lenti

I due indirizzi di codice della sessione THP nativa danno un bias di caricamento
`0x15000` rispetto al suo ELF conservato. Tutti i siti paired-math campionati
si risolvono a `0x80199D64`, **MTX::PSMTXConcat**. Questo è un risultato della
nuova build: nella build FMA precedente il chiamante caldo era il decoder IDCT
THP. Riutilizzare la vecchia attribuzione sarebbe errato.

Le finestre successive riportano 2,787, 1,868 e 3,285 presentazioni al secondo,
con scene e interazioni diverse. Sono dati diagnostici, non confronti quiet
equivalenti e non dimostrano un aumento degli FPS. L'esito dell'utente rimane
menu a 1 FPS e video errati. La finestra da 33 frame/10.046.901 us include
1.136.718 chiamate `fmaf`, 565.653 operazioni paired madd,
3.423.407 us di richieste native e 1.440.908 us FIFO.
**Le durate sono annidate e non vanno sommate.**

H.264 alleggerisce il percorso video e riduce lo spazio. Il costo delle matrici,
delle chiamate al frontend GX e del resto del codice tradotto resta da ridurre.
Gli helper ricevono lavoro video; i piccoli lavori grafici di quattro vertici
continuano a essere elaborati dal chiamante. Aumentare il numero di thread
senza cambiare questi lavori non li distribuisce automaticamente.

## Arresto dopo 1 player: stato preciso

Falliscono tutte le 15 allocazioni SDK del layout, per 3.356 byte richiesti.
Allocator `0x80afda10`, callback `0x80199b58`, heap `0x91a7fe54`, alignment **4**:
l'ipotesi dell'allineamento zero è esclusa da questa sessione.

La firma dell'heap è `EXPH`, estensione `0x91a7fea4..0x937d3250`.
Il primo blocco libero `0x923e337c` contiene 4 byte e punta a `0x924b2e30`.
Il backlink del secondo nodo non corrisponde al primo. Il riepilogo è
`nodes=1 free_bytes=4 links_valid=0 truncated=1`. **Quattro byte non sono il
totale dell'heap libero:** la traversata si ferma al collegamento incoerente.
Non è dimostrato un esaurimento della RAM; non viene aumentata l'area guest.

Il layout originale nel PSARC coincide con quello salvato dalla RAM,
SHA-256 `0ff7833de156feb637328ac43896d8dbdf0ed668b2fd9161af5bc783688c963e`.
Il layout restituisce root nulla, `MainLayout::Build` prosegue e la lista nulla
causa `LinkList.h:573` e `PPCHalt`. Non viene soppressa l'asserzione e non vengono
fabbricati pane o allocazioni per nascondere il difetto.

La nuova diagnostica conserva anche il nodo incoerente e, al primo errore,
una copia integrale e limitata dell'heap in `heap-failure-0.bin`. Il file
include intestazione, blocchi liberi e usati. `scripts/analyze_guest_heap.py`
controlla estensioni, firme, backlink, cicli, sovrapposizioni e possibilità
di soddisfare la richiesta, senza modificare il dump o la console.
Il dump va recuperato soltanto se la sua scrittura è registrata nella sessione
del SELF corrispondente. Resta da trovare la scrittura o la gestione dell'heap
che produce il collegamento incoerente.

## Validazione e prosecuzione

- 16/16 test core/runtime/input/FMA/video passati su host.
- Tre contratti video passati con AddressSanitizer/UndefinedBehaviorSanitizer.
- Audit ARM: wrapper, fallback, registry e dispatch coerenti, import AVC,
  GXM nativo, SceFiber/pthread, stdio, attese, input e SELF nel VPK.
- SFO `ATTRIBUTE2=12`, `ATTRIBUTE=32768`.
- Conversione/verifica completa del PSARC e dei 55.143 fotogrammi su host.
- Build/PSARC nuovi **non installati**, nessun avvio remoto del gioco.
- La console conserva la candidata THP nativa già testata negativamente.

La prossima prova richiede la consegna abbinata di VPK e PSARC AVC. Nel nome
di destinazione del gioco si usa `mariokart.psarc`; non si copiano anche ISO
o directory estratte. Prima della sostituzione conservare il controllo e le
sue identità; il PSARC originale resta disponibile sul Mac per il ripristino.
Avvio manuale, verifica colore/video/loop e raccolta dei marker `[avc-native]`
e degli eventuali errori `[avc-hardware]`, quindi selezione di un player e
recupero del dump dell'heap se prodotto.

Priorità successive: risolvere la gestione dell'heap al cambio di pagina;
misurare separatamente matrici e frontend GX dopo avere video validi;
preparare un percorso nativo per `PSMTXConcat` mantenendo semantica e contratto
di dispatch; affrontare il Mii mancante con un EFB fuori schermo completo e
ritaglio di presentazione corretto. Il Mii resta un difetto separato: le copie
a Y=456 sono ritagliate a zero righe nel target fisico 960x544.

## Identità della candidata conservata

Archivio locale: `build/vita-psarc/candidate-hardware-avc-20261007T151910Z`. Nessuna installazione o
nuova esecuzione sulla console è inclusa in questa identità.

SELF SHA-256: `5cea8d875402423f88c8dec3f9ed908d551f86b80e73738c2a05a30cb3a91c74`.

VPK SHA-256: `e50ec84a2a57abf31684b3fd2099472cce231883d883c99e91b9dbcd055eda38`.

ELF SHA-256: `72e7e24d9c27972afb74e7241a5b83ad783fc5351b9e2133e0fb2204ae358884`.

PSARC abbinato SHA-256: `c92993c58cacc1a90223f9213173e2053e04165ec7115a8cd8867f420ce2f743`.

Aggregato sorgenti e input generati: `537fb2d4b51ba1087b55399a1ccd2937526b45f48f6ec7042987ff18aa29e876`
(31,052 file). L'archivio conserva questi sorgenti in
`SOURCE.tar.gz`, le identità individuali in `SOURCE_DIGESTS.json`, ELF, map,
VELF, SELF, VPK, opzioni, audit, test e prove della conversione.
Il PSARC resta nel percorso dati indicato da `DELIVERY.json`; non è duplicato
nell'archivio della build. `CHECKSUMS.json` sigilla i file della candidata.
