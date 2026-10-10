# WiiCompiled Vita: verde nei video, limite CPU e uscita dopo 1 player

## Risultato corrente

La successiva candidata e le correzioni implementate sono documentate nel
[report memoria/video/presentazione](VITA_MEMORY_VIDEO_PRESENT_DELIVERY_2026-10-07.md).
I risultati di dispositivo riportati qui riguardano la build precedente.

La prova della candidata `psarc-avc-buffer-heapwatch-20261007` resta negativa
per la progressione e la fluidità. L'utente vede i filmati, ma i fotogrammi
verdi continuano ad alternarsi per tutta la permanenza nelle card. Dopo
"1 player" il gioco torna alla LiveArea. Il target resta 30 FPS.

La raccolta corrente è
`device-runs/avc-buffer-heapwatch-20261007T192746Z/`; l'analisi derivata è
`build/avc-green-cpu-analysis/FINDINGS.json`, con checksum e risultati dei test.
Il SELF riletto dalla console corrisponde alla candidata archiviata in
`build/vita-psarc/candidate-avc-buffer-heapwatch-20261007T191634Z/`:

- SELF: `3b99e20d282c59b1aca2abcb6495bb1ceeb0db6da4aa0f06d04706bea44cd746`.
- ELF: `70b445f26ba32307e1c816004b8cc929224d36f181f84c7bfc1110c0a4ad2f4e`.
- VPK: `2bdfdefc1083e7199e18e52a3c2adc2a345b2ea2fed83f2ea760cebfbeb068e6`.
- Config: `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.

Il PSARC attivo ha 1.406.613.154 byte; sono stati verificati dimensione e
bootstrap, non il suo hash completo con una nuova rilettura sulla console.
La ISO non è presente e non è stata richiesta. ATTRIBUTE2=12 è verificato
nella candidata installata. Questa analisi non avvia né installa un gioco.

## CPU: forte evidenza sul percorso principale, attribuzione incompleta

Le finestre sotto sono diagnostiche: non sono un confronto A/B senza
strumentazione e non misurano il tempo CPU esclusivo di ciascun thread.
Il valore per frame è la durata della finestra divisa per i frame completati,
non un percentile dei singoli frame. I tempi delle richieste native e del
FIFO sono annidati: non vanno sommati.

| Fine finestra | Frame | FPS diagnostici | Tempo medio per frame | Richieste native per frame | FIFO per frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1214 | 41 | 4,095 | 244,2 ms | 26,8 ms | 14,3 ms |
| 1256 | 42 | 4,163 | 240,2 ms | 22,6 ms | 14,3 ms |
| 1298 | 42 | 4,135 | 241,8 ms | 23,9 ms | 14,3 ms |
| 1330 | 32 | 3,161 | 316,4 ms | 24,2 ms | 14,3 ms |
| 1361 | 31 | 3,009 | 332,4 ms | 69,0 ms | 32,4 ms |
| 1379 | 18 | 1,741 | 574,2 ms | 88,6 ms | 43,2 ms |
| 1436 | 37 | 3,313 | 301,9 ms | 78,7 ms | 29,4 ms |

La finestra 1399 viene esclusa dall'attribuzione: contiene una cattura del
framebuffer che altera soprattutto il tempo delle richieste native.
Nelle altre finestre le attese VI/GX/GKI registrate sono nulle, eccetto
31.784 microsecondi VI sull'intera finestra 1436, pari a circa lo 0,28%.
Queste attese non spiegano centinaia di millisecondi per frame. Questo,
insieme al core saturo riferito dall'utente, indica un forte limite sul
percorso CPU principale. Non esclude altri costi GPU o attese non misurate.

Alla finestra 1379 circa 485,6 ms per frame restano fuori dal totale delle
richieste native. Quel residuo comprende codice tradotto, strumentazione
e qualsiasi tempo non attribuito: non è una misura esclusiva del codice
PPC. Serve una profilazione più completa prima di assegnarlo a una funzione.
Il budget a 30 FPS è 33,3 ms; le finestre osservate consumano da 7,2 a 17,2
volte quel budget. Questo non equivale a una previsione del guadagno possibile.

La simbolizzazione usa due indirizzi di codice emessi dal SELF corrente,
con bias di caricamento ELF `0x6f000`. I siti paired-single più caldi
risolvono a `func_80199D64`, cioè `PSMTXConcat`. Nella finestra 1379 sono
registrate 308.538 operazioni paired-madd e 620.028 chiamate `fmaf`.
Le stime campionate dei due principali siti `fmaf` corrispondono a circa
29,4 ms per frame. Sono stime di helper annidati, non il costo totale delle
matrici, e non spiegano da sole i 574,2 ms per frame.

## Worker: pool presente, lavoro sui vertici troppo piccolo

Alla finestra 1379 il pool registra due helper, tre lane e 11.952 chiamate.
La lane del chiamante esegue 47.808 elementi, quattro per chiamata; le due
lane helper registrano zero elementi e zero chunk. Il totale misurato del
pool è circa 9,5 ms per frame, non centinaia di millisecondi.

Quindi il multithreading esiste, ma questo lavoro non viene distribuito.
La strada da verificare è accumulare lavoro indipendente in lotti più grandi
o eliminare la preparazione ripetuta. Spostare indiscriminatamente thread
guest su core diversi introduce rischi di ordinamento e non divide le
dipendenze seriali del codice PPC. Non è giustificato attivare insieme
async GX, nuove affinità e approssimazioni matematiche.

## Video: output presente, alternanza verde ancora senza causa provata

Il decoder emette tre `accepted-without-picture`, poi il log raggiunge
`[avc-native] frame=120`. Nei campioni il decode impiega 7,65–8,06 ms e
tiling/pubblicazione circa 0,30–0,38 ms per immagine. Il tiling usa tutte
le tre lane, con 1.536 tile ciascuna: questo lavoro è effettivamente parallelo.
Queste misure riguardano immagini campionate, non un costo medio per frame
del gioco né tutte le dimensioni dei 27 video.

Il fotogramma salvato dalla console nel percorso annotato dal log,
`aurora/diagnostics/frame-1384.ppm`, mostra il menu e il video Single Player.
È conservato nell'analisi derivata insieme a una copia PNG per l'ispezione.
Una singola immagine non può verificare l'alternanza descritta dall'utente.

Con `Pending` il wrapper restituisce successo senza scrivere i buffer
destinazione. Questa condizione può presentare buffer non inizializzati
all'avvio, ma i soli tre eventi iniziali non spiegano il verde persistente.
Le prossime verifiche devono associare ogni buffer Y/U/V alla decodifica e
alla texture caricata: contenuto dei piani, riuso dei buffer, invalidazione
della cache e invio GXM. Non è stata applicata una modifica arbitraria
all'ordine U/V o al formato del decoder.

## Uscita: heap corrotto, scrittura responsabile non identificata

La sessione termina con `LinkList.h:573` e `PPCHalt`; non ci sono nuovi
coredump del kernel. La diagnosi corrente usa il log della sessione e il
nuovo dump dell'heap, evitando di attribuire riferimenti storici di
`crash.txt` al nuovo guasto.

Heap `0x91a7fe74`, intervallo utile `0x91a7fec4`–`0x937d3250`.
Il primo controllo che segnala danno è `alloc-after`, chiamante
`0x8007a4e4`, durante una richiesta da 32 byte che restituisce zero.
L'intestazione libera a `0x924b2e50` è interamente azzerata.

L'ultima richiesta riuscita, da 96 byte, restituisce `0x924b2df0` e lascia
il blocco libero a `0x924b2e50`. Il dump conserva quel blocco usato
(`0x924b2de0`, 96 byte) come ultimo della lista; la richiesta successiva
da 32 byte non ha aggiunto un blocco usato. La capacità che l'intestazione
libera dovrebbe descrivere è 20.055.024 byte. Il controllo osserva
l'intestazione valida prima della chiamata e invalida al ritorno: non
identifica da solo quale scrittura l'abbia azzerata.

Il fallimento impedisce di creare il layout: tutte le 15 allocazioni
registrate falliscono, il root pane è nullo e l'asserzione arresta il gioco.
Aumentare l'heap o ignorare l'asserzione non correggerebbe la corruzione.

La nuova prova host esegue le funzioni generate reali:
`80199B58` → `80198D88` → `8019899C` → `80198798` → zero fill.
Copre 5.115 combinazioni di dimensione e allineamento, comprese 32 e 96
byte, con flags=1 come sul dispositivo. Tutte passano, oltre ai precedenti
5.121 split, 24.672 limiti di memset e 1.024 conteggi di link animazione.
Passano 18/18 test host e la nuova prova completa con ASan/UBSan.

La prova non simula ARM32, il renderer, le letture EFB differite o scritture
concorrenti/DMA. `Memory::GetPointer`, usato dal controllo dell'heap, legge
RAM direttamente; le letture guest possono invece materializzare copie
EFB differite. Questa differenza è un'ipotesi concreta da osservare nel
runtime, non una causa già dimostrata sul dispositivo.

## Prossimi interventi e stato delle modifiche

1. Identificare la scrittura che azzera l'intestazione: osservazione mirata
   delle letture/copie differite e delle scritture native, con stato prima
   e dopo la chiamata fallita. Nessuna riparazione artificiale dell'heap.
2. Tracciare i buffer video dall'output del decoder al caricamento della
   texture per distinguere verde già presente nei piani da texture stale.
3. Dopo aver corretto stabilità e presentazione, misurare il tempo CPU del
   thread principale e i costi del codice tradotto con un controllo senza
   diagnostica; specializzare `PSMTXConcat` e accorpare lavoro GX soltanto
   come interventi separati, con verifica dell'ABI e del risultato.
4. Misurare la GPU separatamente prima di attribuire al renderer tutto il
   costo restante. Un tempo di invio CPU non è un tempo di esecuzione GPU.

In questo intervento cambiano la prova dell'allocatore e la documentazione.
Il runtime installato resta identico: non è stata preparata o installata una
nuova build e non viene dichiarato un miglioramento di FPS o progressione.
