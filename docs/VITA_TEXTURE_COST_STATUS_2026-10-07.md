# WiiCompiled Vita: progressione sbloccata, texture e tempi GX

## Risultato della candidata memoria/video/presentazione

L'utente ha superato "1 player" ed è arrivato alla selezione personaggi.
I modelli 3D renderizzano e le animazioni continuano ad aggiornarsi molto
lentamente. Il completamento di una gara resta da provare. La correzione
della memoria è quindi associata a un risultato positivo di progressione
in questa sessione, senza una dimostrazione del primo writer responsabile
della corruzione precedente. Flicker e verde non sono stati confermati
separatamente come risolti.

La sessione corrente è conservata in
`device-runs/memory-video-present-20261007T204622Z/`. Il SELF è stato riletto
dalla Vita e corrisponde alla candidata `psarc-memory-video-present-20261007`:

- SELF: `c64ee93c399d2b4593e87c58565b2606755e4116ee587e9450dc0a017a088752`.
- VPK: `e39d81c56d00445175dea132dd218bfaccd88de0862da4873251c831447b1c10`.
- Config conservata: `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
- GX asincrono: `AURORA_VITA_ASYNC_GX=OFF`.
- Diagnostica CPU e chiamate native attiva; due helper del pool Aurora.

La raccolta è in sola lettura: nessuna modifica sulla console e nessun
comando di avvio. `crash.txt` presente è storico; non viene attribuito alla
nuova sessione senza un marker corrente. Non sono stati raccolti nuovi
dump kernel. Il valore di CPU bassa viene dal riscontro dell'utente, non
da una nuova misura per thread della console.

## Evidenza sui frame

La finestra terminata al frame 469 completa 2 frame in 12.937.099 us:
**0,1546 FPS**, pari a 6.468,5 ms medi per frame. Le ultime finestre
completano circa 0,15–0,20 FPS. Non è un blocco completo della produzione
dei frame. Non è stato confermato quale indicatore visualizzi "0".

Nella stessa finestra il tempo delle richieste native è 12.388.070 us,
pari al 95,76% del tempo di parete. Il FIFO misura 12.167.827 us, pari al
94,05%. Sono scope annidati: **non vanno sommati e non sono tempo CPU
esclusivo o timestamp GPU**. Le attese VI/GX/GKI registrate sono nulle.
Le presentazioni campionate tra i frame iniziali hanno circa 100–129 us
tra fine scena e accodamento. Questi campioni non misurano la selezione
personaggi e non escludono attese GPU precedenti alla presentazione.

La classificazione generica `gpu_backpressure` compare in finestre della
coda display, ma include l'effetto del vblank. Non dimostra che il costo
dominante sia l'esecuzione degli shader. Mancano ancora i tempi esclusivi
di conversione, allocazione, log I/O, creazione e distruzione texture e
le sincronizzazioni GXM delle finestre lente.

## Caricamenti texture misurati e comportamento del codice

La sessione contiene **72.958** messaggi `compact_texture`, emessi dopo
una nuova allocazione e inizializzazione texture native riuscite:

| Formato e geometria | Creazioni/caricamenti osservati |
| --- | ---: |
| I8, 64×4, 256 byte | 41.607 |
| IA8, 4×4, 32 byte | 31.240 |

Questo dimostra ripetute creazioni di quelle geometrie. Il log attuale non
riporta indirizzo e identità del contenuto: non dimostra che tutti i
caricamenti della stessa geometria siano pixel identici o evitabili.

Il codice offre già un pool CDRAM, formati compatti I8/IA8 e un ring di
quattro frame per texture volatili. `gxm_facade.cpp` confronta però la
chiave di identità/revisione, non i byte correnti delle sorgenti volatili.
Un'ulteriore richiesta della stessa chiave nello stesso frame non usa il
ring perché `lastUse < frame` non è soddisfatto. Viene creata una texture
separata per evitare di modificare una risorsa già consumata da un draw.

`invalidate_static_texture_cache()` rende non cacheabili tutte le texture
caricate. La build video pubblica le tre scritture DMA e poi emette
`GXInvalidateTexAll`, che può portare texture statiche ancora caricate su
questo percorso. Servono invalidazioni precise per sorgente, TLUT ed EFB;
non è corretto rimuovere le invalidazioni senza conservare quel contratto.

`destroy_texture()` attende un finish globale quando una texture è ancora
in uso dalla GPU. Il numero e il costo di queste attese nelle finestre
lente non sono ancora disponibili. La creazione texture scrive inoltre
un messaggio per caricamento: i 72.958 messaggi introducono un costo I/O
non quantificato separatamente.

## Ordine degli interventi proposti

1. **Attribuzione con contatori aggregati ogni finestra**, anziché un log
   per caricamento: hit/miss/upload, texture/byte vivi, conversione,
   allocazione, invalidazione e delte dei finish per causa. Campioni
   diagnostici separati dalla misura senza log frequenti.
2. **Invalidazione per intervallo e riuso verificato nel frame**. Per
   sorgenti volatili, confrontare il contenuto necessario, inclusi palette
   e mip. Un hash seleziona il candidato; un confronto esatto evita di
   affidare la correttezza a collisioni. Se i byte cambiano, mantenere un
   nuovo snapshot GPU. Nessuna modifica di risorse ancora in uso.
3. **Allocazione e rilascio a basso costo**. Il pool CDRAM esiste già;
   aggiungere classi di dimensione per le piccole texture e ritiro differito
   degli snapshot dopo il completamento effettivo. Evitare finish globali
   per ogni risorsa, mantenendo un fallback sicuro alla saturazione.
4. **Preparazione anticipata degli asset statici**. Conservare layout e
   formati GXM nel PSARC, caricandoli direttamente nella memoria finale
   della texture. Asset originali e identificazione degli eseguibili devono
   restare compatibili; nessun asset estratto richiesto sulla Vita. BC1 solo
   per contenuti che rispettano il contratto di colori, alpha e filtro.
5. **Aggiornamenti dinamici mirati**. Convertire soltanto i blocchi cambiati,
   preparare i dati in memoria CPU cached e pubblicare scritture in blocco.
   Per video e buffer riciclati, rispettare il ring e i punti di consumo.
6. **Worker per la preparazione e successiva prova GX async**. I worker
   possono decodificare e convertire dati; il contesto GXM resta a un solo
   proprietario. L'async non elimina lavoro duplicato o sincronizzazioni.

Atlas UI e aggregazione di piccoli upload sono ulteriori esperimenti:
devono preservare coordinate, filtri, padding, mip e ordine dei draw.
Le modifiche sopra sono un piano attribuito al codice corrente, non
ottimizzazioni già implementate o misurate sulla console.

## Artefatti e prova successiva

L'analisi derivata è in
`build/gpu-texture-analysis-20261007T204755Z/FINDINGS.json`, con checksum.
I file diagnostici ausiliari conservati possono contenere sezioni di
sessioni precedenti; i conteggi e le finestre sopra derivano esclusivamente
dal log dopo il marker della candidata corrente.

La candidata stabile resta quella installata. Una prossima prova deve
confrontare la stessa schermata e lo stesso percorso, con un solo
intervento alla volta. Shader, risoluzione, CPU affinity e async GX non
devono essere variati contemporaneamente. Il target di 30 FPS e una gara
completa restano aperti.
