# Audit prestazioni WiiCompiled / Aurora Vita — target 60 fps

5 settembre 2026. Analisi del codice locale e dei log esistenti; nessuna nuova esecuzione su PS Vita. Il risultato di questa attività è il report, non una build ottimizzata o una certificazione dei 60 fps.

## Aggiornamento prioritario: modelli visibili, bianchi e capovolti; calo prestazioni

Durante l'audit l'utente ha fornito un log aggiornato e confermato su hardware che nell'ultima versione i modelli si vedono, ma sono bianchi e a testa in giù, con prestazioni peggiori. Il file ora contiene **cinque sessioni**: quella nuova inizia alla riga 6801 e termina alla 7506. SHA-256 aggiornato: 7d2bb4d30e7b7408da64808af795d7ef07260d0e73859fd0f15a0245a210bc1b. L'estrazione è in [evidence-updated.json]. Le sezioni successive conservano esplicitamente l'analisi della precedente sessione 4 come confronto diagnostico.

**Bianco: causa verificata nella configurazione e nel codice.** L'avvio alla riga 6837 ha perf_force_3d_solid=1. Il ramo in gx_backend.cpp imposta RGBA=255, z=0, texture disabilitata, PASSCLR, culling/blending/depth disabilitati per le draw prospettiche. Il bianco non dimostra quindi un fallimento dell'upload delle texture. Prima prova: disabilitare soltanto questo flag, preservando il resto della configurazione e osservando colori, texture e depth. Non basta per affermare che i materiali completi siano già supportati. [S18]

**Calo prestazioni: evidenza nuova concentrata sul percorso EFB.** Rispetto alla sessione 4, la 5 porta perf_skip_efb da 1 a 0, mantiene efb_gpu_blit=0 e spegne il probe Wii. I marker confermano path=readback; i frame 3D riportano 12 copie EFB e 16 attese esplicite contro le quattro comuni osservate senza copie. [S19]

| Campione a uguale conteggio geometria | Sessione 4, seriale 1200 | Sessione 5, seriale 900 |
|---|---:|---:|
| Draw GX / vertici | 1.561 / 10.220 | 1.561 / 10.220 |
| Draw fisiche | 104 | 105 |
| Render CPU | 42,847 ms | 234,345 ms |
| Submit | 40,784 ms | 232,356 ms |
| streamWrite | 17,537 ms | 17,586 ms |
| End frame | 1,813 ms | 1,727 ms |

Il render aumenta di 191,498 ms, circa 5,47 volte, mentre scrittura stream ed esecuzione finale restano simili. Non è un A/B controllato con stato guest identico, ma è una forte indicazione del costo aggiunto dentro il tratto consumer che ora esegue le copie EFB. Nei 27 frame 775–801, con 3.356–3.357 draw e 12 copie, l'intervallo producer mediano è 516,782 ms e le attese già accumulate sono 257,990 ms. Questo nuovo dato promuove EFB da lavoro successivo a **priorità immediata**.

Il percorso esegue FlushQueuedDraws → glReadPixels → eventuale downscale CPU → upload_efb_rgba. Nel vitaGL locale glReadPixels può terminare la scena e chiamare sceGxmFinish quando READBACKS_SPEEDHACK non è definito. L'archivio effettivamente linkato va verificato prima di attribuire ogni attesa a quella chiamata. Anche il downscale ha una divisione intera a 64 bit per ogni pixel sull'asse X: precomputare le mappe X/Y o usare incrementi razionali esatti evita lavoro ripetuto. Separare prima i timer di sincronizzazione, lettura, ridimensionamento e upload; non attribuire tutti i 191 ms alla GPU.

**Ribaltamento verticale: ipotesi principale da verificare nel passaggio EFB → texture.** Il readback dal framebuffer di default in vitaGL restituisce le righe in ordine bottom-up; il codice del bridge corregge la coordinata del rettangolo con glY, ma riutilizza poi il buffer senza inversione delle righe. L'upload EFB non le inverte e il sampling non applica una correzione UV specifica alla sorgente EFB. Questo può capovolgere un'anteprima 3D ricomposta in un quad UI. È un indizio concreto nel codice, non la prova che tutti i modelli segnalati passino da quella copia. [S20]

Prima verifica: immagine asimmetrica a quattro colori con un indicatore in alto, confrontata nel render diretto e nel quad che campiona la copia. Se l'errore è nella copia, correggere **una sola volta** le righe oppure la coordinata T per le texture EFB, conservando crop, scissor e orientamento delle texture ordinarie. Se anche il render diretto è capovolto, controllare invece matrice/proiezione e convenzione viewport. Una negazione globale della Y rischia di nascondere il problema e rompere UI o culling. La perdita di w descritta più avanti è un altro difetto del contratto 3D, non una spiegazione dimostrata del flip verticale.

**Ordine operativo aggiornato:** 1) separare il bianco diagnostico dalla resa dei materiali; 2) localizzare e correggere l'orientamento EFB; 3) misurare e ridurre readback/downscale/upload e sincronizzazioni delle copie, progettando il percorso GPU stabile; 4) proseguire con stato per ID, batch e cache della geometria. Disabilitare nuovamente EFB è utile solo per attribuire il costo, perché potrebbe eliminare proprio le anteprime 3D ora visibili.

## Valutazione

Il porting ha margini importanti, soprattutto sul lavoro CPU eseguito per ogni draw GX. Il target di 60 fps richiede però cambiamenti strutturali in **entrambi** i lati della pipeline: producer WiiCompiled e consumer Aurora. Il batching GPU è già efficace nei campioni diagnostici; il costo di preparazione rimane troppo alto.

Nella sessione 4, acquisita prima dell'aggiornamento sopra, un campione con 5.932 draw logiche diventa 43 draw fisiche, ma il consumer richiede 146,34 ms, dei quali 145,12 ms nel tratto chiamato submit. Una finestra contigua dello stesso carico ha un intervallo producer mediano di 443,73 ms, circa 2,25 nuovi pacchetti al secondo. Il budget finale è **16,67 ms**, quindi non basta una piccola ottimizzazione di vitaGL. Non è ancora possibile confermare la raggiungibilità dei 60 fps con grafica completa.

La prima implementazione consigliata è un ingresso per frame/batch con **stato condiviso per ID**, che eviti costruzione, copia, hash e confronto di strutture grandi per ogni primitiva. Subito dopo: cache dei vertici decodificati e decoder specializzati. In parallelo al disegno dell'architettura va risolto il contratto 3D: prospettiva, attributi e materiali GX completi.

## 1. Quale codice viene realmente usato

| Componente | Stato verificato |
|---|---|
| WiiCompiled | HEAD ab7e240cf2584dc0d2c770569f24510eeea62fcf; 11 file già modificati all'inizio dell'audit |
| Clone aurora-vita | HEAD 68f7bab91bced8ee4849820bc03a4de21f1e6944; worktree del clone pulito |
| Aurora compilato nel VPK | Sorgenti gfx in aurora-main/platforms/vita/gfx, scelti da Makefile.vita |
| Ingresso grafico del gioco | vita/gx_backend.cpp → vita/aurora_packet_renderer.cpp → Renderer/StreamingArena Aurora |
| vitaGL predefinito | Archivio esterno libvitaGL-m12_5-custom-heap.a nel progetto aurora-vita-max-prehardware |

Il clone aurora-vita **non è la directory compilata da questa Makefile**. Nove file sotto platforms/vita differiscono da aurora-main: buffer_pool.cpp, efb.cpp/hpp, renderer.cpp/hpp, streaming_arena.cpp/hpp, texture_cache.cpp/hpp. Un miglioramento applicato soltanto al clone non cambia il VPK attuale. [S1]

Il DrawSink GX generico del clone traduce più stato e offre diagnostica di copertura, ma non è l'ingresso attivo del bridge WiiCompiled. Va usato come riferimento per recuperare funzionalità, evitando di inserire una seconda decodifica completa nel percorso caldo. [S13]

Il piano PERFORMANCE_OPTIMIZATION_PLAN.md del 4 settembre è utile come storia, ma parte delle sue fasi è già implementata localmente: logging ridotto, vertici compatti, fusione dei draw, mapping diretto, tabelle di stato, due slot e template DL. I default della Makefile lasciano molti di questi flag a zero; le sessioni 4 e 5 li abilitano. La cache DL attuale è ancora una cache dei comandi, non dei vertici decodificati.

Alla prima lettura, lo stamp build/vita/mkwii_runtime/.native-config aveva già perf_skip_efb=0 e perf_inject_wii_triangle=0, mentre la sessione 4 aveva entrambi a 1. Il successivo log della sessione 5 conferma quei nuovi flag. Default, stamp e VPK installato vanno comunque identificati separatamente.

## 2. Evidenza misurata e suoi limiti

La prima acquisizione di build/vita/runtime.log conteneva **quattro avvii**, alle righe 1, 4077, 4877 e 5350. Per questa sezione numerica uso solo il quarto: righe 5350–6800. SHA-256 del file analizzato:

f185266aa1a5853d0e529a06e19b99498c4e0d4efe252ee3a6061cb38c138a8b

Le estrazioni con righe, configurazioni e statistiche sono conservate in [evidence.json]. Il file rr2_vita.log riguarda Real Racing 2 ed è stato escluso.

La sessione 4 abilita compact_vertex, frame_batcher, direct_stream_write, compact_frame_state, frame_queue_depth=2 e dl_template_cache. Disabilita filmati, THP nativo, EFB, billboard e light texture; forza il 3D solido e abilita il probe dei triangoli Wii. I clock registrati sono già 444/222/222/166 MHz. Questi campioni sono **diagnostici**: gli effetti esclusi sottostimano il carico finale, mentre il probe aggiunge lavoro CPU. Non costituiscono neppure un limite inferiore rigoroso del costo complessivo.

| Seriale consumer | Draw GX / fisiche | Vertici | Render CPU, ms | Submit, ms | End frame, ms | Swap, ms |
|---:|---:|---:|---:|---:|---:|---:|
| 600 | 49 / 14 | 196 | 13,330 | 1,202 | 11,945 | 0,126 |
| 900 | 665 / 110 | 2.660 | 18,099 | 15,691 | 2,180 | 0,154 |
| 1200 | 1.561 / 104 | 10.220 | 42,847 | 40,784 | 1,813 | 0,148 |
| 1500 | 2.307 / 66 | 15.064 | 58,824 | 57,324 | 1,253 | 0,140 |
| 1800 | 5.932 / 43 | 36.222 | 146,338 | 145,119 | 0,955 | 0,144 |

Il campione 900 è ortografico; gli ultimi tre includono geometria prospettica. Il log non dimostra da solo una gara completa correttamente visibile. I tempi di questa tabella sono singoli campioni, non mediane o p95 del renderer.

Per i producer_frame 1753–1803 esistono 51 campioni consecutivi, con 5.932–5.933 draw:

| Metrica | Mediana | p95 | Massimo |
|---|---:|---:|---:|
| Intervallo producer | 443,726 ms | 449,376 ms | 451,333 ms |
| Attese renderer già accumulate | 94,342 ms | 95,789 ms | 96,342 ms |
| Copia pacchetto corrente | 4,158 ms | 4,675 ms | 4,777 ms |
| Attesa di uno slot al submit | 2 µs | 2 µs | 2 µs |

p95 calcolato con nearest rank. Nella finestra 1590–1640, anch'essa di 51 campioni, l'intervallo mediano è 279,687 ms e la copia 1,581 ms.

Interpretazione corretta dei timer:

- submit_us include il loop consumer, le trasformazioni CPU e la preparazione Aurora; non misura solo una funzione né la GPU.
- Nel seriale 1800, profile_us indica indici 4,993 ms, packing canonico 0, texture 0,261 ms, pipeline 1,551 ms, streamWrite 65,619 ms, flush/execute 0,919 ms. **streamWrite comprende anche gli indici, la creazione del DrawPacket e il confronto per il batch**. Non sono 65,6 ms di solo trasferimento del buffer. [S2]
- L'intervallo producer contiene lavoro guest/HLE, attese e scheduling. Non va sommato al tempo consumer, perché i thread possono sovrapporsi. Sottrarre le attese non produce automaticamente un tempo CPU esclusivo.
- gx_cpu_perf frame, producer_frame e perf_summary serial non sono lo stesso contatore. Non accoppiare righe solo perché hanno lo stesso numero.
- Il p95 renderer non è ricavabile da cinque righe emesse ogni 300 frame. Le righe duplicate di alcuni altri marker richiedono inoltre attenzione nell'aggregazione.
- Swap breve non dimostra GPU libera: vitaGL accoda lavoro e presentazione. Le misure attuali sostengono una forte inefficienza CPU; non quantificano il margine GPU del gioco completo.

## 3. Interventi sul bridge WiiCompiled e su Aurora

### A. Eliminare lo stato generico per ogni draw — priorità massima

Il percorso compatto evita CanonicalVertex, ma AuroraPacketRendererSubmit costruisce ancora PipelineDesc e DrawUniforms, copia gli uniform dalla cache, crea un DrawPacket e confronta l'intero blocco uniform prima di fondere i draw. Anche un hit della cache percorre gran parte di questa sequenza. [S2], [S3]

Ho compilato una sonda con arm-vita-eabi-g++ e misurato gli oggetti tramite arm-vita-eabi-nm; dimensioni ARM32 effettive:

| Tipo | Byte |
|---|---:|
| CanonicalVertex | 168 |
| PipelineDesc | 840 |
| DrawUniforms | 1.516 |
| DrawPacket | 1.800 |
| Command | 1.848 |

Con 5.933 submit, un singolo passaggio su tutti i blocchi uniform equivale a circa 8,58 MiB di dati; il codice contiene più inizializzazioni/copie/confronti. Questo calcolo descrive il volume logico delle strutture, non misura traffico RAM reale: compilatore e cache possono eliminarne parte.

**Modifica proposta:** costruire un MaterialState immutabile per ogni combinazione effettiva di stato, con ID intero, pipeline risolta, texture/sampler, uniform necessari e revisione. Ogni draw conserva ID, primitiva e range. L'ID va propagato dal producer fino al consumer, invece di ricostruire una chiave a 64 bit da decine di campi. Raster/viewport/scissor e ordine EFB restano parte della compatibilità.

Riconoscere e fondere i run adiacenti prima di creare il comando Aurora. Costruire il DrawPacket generico solo alla chiusura del batch o per il fallback. Aggiungere un ingresso per frame o run, non soltanto un'altra cache intorno al submit per-draw.

**Validazione:** tempo per draw logica e per cambio stato, numero di costruzioni DrawPacket, hash, confronti e byte toccati; stesso ordine di blending, alpha test, scissor e copie EFB. I 5.890 merge del campione pesante dimostrano che esistono run lunghi da sfruttare; la percentuale va rimisurata con i materiali reali, perché il colore solido rende artificialmente molti stati uguali.

### B. Completare la deduplicazione nel producer — priorità alta

CaptureDrawState costruisce comunque tre snapshot per draw. CaptureDrawTransform copia proiezione e tutte e dieci le matrici; InternAdjacentState confronta gli array float con lo stato precedente. Gli ID attuali riducono il pacchetto, ma non evitano questa acquisizione. [S4]

Introdurre generazioni distinte per matrici, raster, TEV, texture e texgen. Aggiornarle in **tutti** i punti che mutano lo stato: GX HLE, CP/BP/XF, display list e caricamenti indicizzati. Uno snapshot viene materializzato solo quando la sua generazione cambia. Conservare un controllo comparativo diagnostico per trovare invalidazioni mancanti.

La dimensione packet_bytes=1.430.744 del log esclude gli heap dei vector di trasformazioni/raster/texture. Misurare size e capacity di queste tabelle e il totale dei pacchetti producer/ready/busy. Lo static_assert sotto 2 MiB non è un tetto alla memoria totale del frame.

### C. Scrivere il risultato nel buffer finale e ridimensionare l'arena — priorità alta

Oggi il consumer trasforma in g_renderVertices; il submit copia ancora quei vertici nel buffer mappato. Riservare un range per run e far scrivere direttamente il kernel di trasformazione/packing. Riutilizzare pattern di indici per quad; mantenere controlli su U16, topologia e winding. L'arena viene già mappata una volta per segmento: non è corretto proporre questo come lavoro ancora totalmente assente. [S2], [S5]

I VBO restano dimensionati per il vecchio formato: **8 MiB per slot, due slot**, più due IBO da 512 KiB. I 49.152 vertici compatti massimi occupano 1,125 MiB per slot. Un'arena esclusivamente a 24 byte potrebbe recuperare nominalmente **13,75 MiB** dai VBO. È un'opportunità di memoria, non un guadagno FPS misurato.

Dimensionare separatamente percorso UI, percorso 3D e fallback; non ridurre semplicemente il buffer condiviso, perché le primitive espanse e il formato canonico richiedono più spazio. Il futuro formato con xyzw avrà un costo diverso. Registrare overflow per percorso e prevedere segmentazione ordinata anziché scartare draw.

### D. Cache di vertici decodificati, non solo template DL — priorità massima sul producer

ReplayDlDrawTemplate salva offset, dimensioni e opcode, poi chiama ancora submit_raw_draw per ogni primitiva. DecodeRawDraw ripete per ogni vertice il loop dei descrittori, calcolo delle dimensioni, endian conversion, ricerca dell'elemento indicizzato e acquisizione dello stato. [S6], [S7]

Nel gx_cpu_perf frame=1200 della sessione 4: 180 chiamate DL, 90.368 byte, **82,071 ms** in DL; 1.936 draw raw corrette; 36 hit template con 1.628 draw. Il template è quindi utilizzato, ma non elimina la decodifica che interessa ottimizzare.

Due livelli proposti:

1. **Piano di decodifica per VCD/VAT:** elenco dei soli attributi attivi, offset e stride precomputati, funzioni specializzate per i layout frequenti; controllo di range in blocco dove dimostrabile, fallback completo per gli altri casi.
2. **Cache persistente in spazio oggetto:** vertici e indici già decodificati, riutilizzabili quando DL e array sorgente non cambiano. Le matrici e il materiale sono applicati separatamente. Per mesh ripetute valutare VBO persistenti e indici riusabili.

La chiave deve includere DL, VCD/VAT, array base/stride/formato, generazioni dei range realmente referenziati e indici matrice. Una DL immutata può puntare ad array di skinning modificati: validare soltanto i byte della DL sarebbe errato. Stato CP/BP/XF, liste annidate e ordinamento vanno conservati anche nel replay compilato.

Il tracking esistente ha granularità 64 KiB e notifiche di cache/DMA: non osserva automaticamente ogni scrittura C++. Prima di estenderne l'uso, verificare tutti i writer e contare le invalidazioni spurie. Sul percorso non tracciato mantenere digest/fallback. L'hash Vita del contenuto DL è ancora FNV byte per byte a 64 bit; ottimizzarlo è subordinato alla misura del costo residuo dopo i generation hit. [S8]

### E. UI 2D: glifi e quad devono arrivare come run — priorità alta

GlyphDrawer ha già un HLE nativo e invia un burst FIFO. Il passo successivo è evitare la catena impacchettamento big-endian → decoder raw → snapshot completo per ciascun glifo, mediante un writer di quad equivalenti che mantenga tutti gli effetti GX/ABI. [S9]

Aggregare solo glifi/quad adiacenti con texture, materiale, matrice, scissor e blend identici. Mantenere il percorso LYT fedele; non riattivare quello diretto come scorciatoia. I font/atlas già usati vanno conservati nella cache; inventare un nuovo atlas globale non è il primo intervento.

Il consumer del campione UI da 665 draw è già oltre il budget a 18,1 ms. Va misurato anche il producer della stessa schermata con un ID comune: ottimizzare solo i glifi non prova di aver risolto animazioni, traversal LYT, fader, caricamenti o scheduling.

## 4. Il percorso 3D deve conservare abbastanza informazione

### Prospettiva e trasformazioni

TransformVertex divide clipX/Y/Z per clipW e conserva solo xyz; il layout compatto manda tre float di posizione allo shader, che usa direttamente a_position. Il fallback canonico reinserisce w=1. Questo perde la w originaria: può alterare interpolazione prospettica e clipping vicino alla camera, anche se un triangolo solido appare corretto. [S4], [S10]

Prima soluzione verificabile: un formato 3D con **xyzw**, conservando le coordinate omogenee fino alla GPU. Successivamente confrontare trasformazione CPU in blocco contro posizione in spazio oggetto + matrice/palette nel vertex shader. La UI ortografica può mantenere il formato più piccolo.

Non spostare tutto in GPU senza misurare: oggi la trasformazione CPU permette di fondere draw con matrici diverse. Una matrice uniform per draw potrebbe aumentare drasticamente le draw fisiche. Valutare palette indicizzate per batch oppure trasformazione CPU NEON per run, mantenendo il vantaggio del batching. La scelta dipende dalla distribuzione delle matrici e dal costo GPU reale.

### Materiali, normali e texture

DecodeRawDraw consuma anche dati che poi non conserva: il vertice attivo contiene posizione, CLR0 e TEX0, oltre al riferimento matrice separato. Il bridge sceglie una texture/stadio rappresentabile e riduce i combiner custom non riconosciuti a MODULATE. Il ritorno a CanonicalVertex **non recupera** attributi già persi. [S7], [S11]

Il backend Aurora dispone di descrizioni TEV multistadio, texgen e di una pipeline vertex CPU più completa. Riutilizzarne la semantica e i test come riferimento; introdurre layout/materiali specializzati per le caratteristiche effettivamente richieste dal gioco, con fallback esplicito per quelle non coperte. Non dichiarare il 3D completo sulla base di raw_fail=0.

Conservare normali, più UV, colori/materiali, lighting, fog, alpha compare, depth e combinazioni TEV necessarie. Profilare le firme reali: poche classi frequenti possono ricevere kernel e shader dedicati. Le altre rimangono nel percorso generale corretto. Aggiungere contatori di funzionalità approssimate anche nella build performance. [S13]

Una volta eliminato il costo CPU dominante, misurare overdraw, pixel shading, bandwidth texture e costo degli effetti. Solo allora fare A/B della risoluzione interna 3D, ad esempio 960×544 contro 720×408 e 640×360, con UI a risoluzione nativa. La riduzione dei pixel non accelera la decodifica guest né lo stato per-draw.

## 5. Sincronizzazione, EFB, texture e shader

### Sovrapposizione producer/consumer

La coda a due slot esiste, ma WaitRender è ancora richiamato da GXDrawDone e aurora_wait_for_frame_worker; GXCopyDisp/GXCopyTex contengono attese. Nel carico pesante prior_wait_us è circa 94 ms, benché queue_wait_us sia quasi zero. Il problema non si identifica guardando solo l'attesa per uno slot. [S12]

Registrare il motivo di ogni attesa, seriale richiesto e raggiunto, occupazione della coda e durata. Distinguere pacchetto acquisito, submission CPU terminata e lavoro GPU completato: oggi completedSerial avanza dopo swap, senza essere di per sé una fence GPU. Ridurre soltanto le attese con contratto dimostrato superfluo; non eliminare GXDrawDone indiscriminatamente.

I pacchetti conservano puntatori alle texture guest. Se la generazione cambia, TextureSourceStillMatches può disabilitare il texturing; questo è una mitigazione osservabile, non una soluzione alla proprietà dei dati. Per aumentare l'overlap occorrono snapshot/risorse versionate o vincoli espliciti al riuso del guest buffer. Lo stesso vale per THP, EFB e array dinamici.

StreamingArena ricicla lo slot con frame modulo due. Nel vitaGL locale il mapping non gestisce last_frame e unmap non attende la GPU. Anche il [sorgente ufficiale vitaGL] mostra questa limitazione del mapping. L'assenza di corruzione nei frame lenti non prova sicurezza a 60 fps: verificare un protocollo di completamento GPU prima del riuso e delle distruzioni. Non risolverlo aggiungendo glFinish a ogni draw. [S5]

### EFB e copie

La configurazione predefinita usa readback CPU; i commenti locali documentano crash precedenti del percorso blit/FBO. Il benchmark della sessione 4 esclude interamente EFB; la sessione 5 descritta nell'aggiornamento lo riattiva e ne rende prioritaria l'analisi. [S2]

Separare copie campionate solo dalla GPU da copie lette dalla CPU guest. Per le prime progettare risorse GPU persistenti e ordine esplicito senza il vecchio cambio FBO problematico; per le seconde implementare il completamento richiesto dal comportamento del gioco. Differire una lettura è lecito solo se la dipendenza guest lo permette. Misurare numero di copie, area, formato, flush, readback, conversione e upload; mantenere un fallback corretto durante lo sviluppo.

### Texture e pipeline

TextureCache già dispone di pre-eviction e protegge risorse referenziate nel frame corrente; PipelineCache ha pin, budget e uniform cache. Conservare queste protezioni. Il percorso texture converte ogni formato a RGBA8; il bridge passa un solo livello mip e la cache evita glGenerateMipmap per problemi locali di allocazione. [S14]

Interventi successivi: risoluzione per ID/revisione, riuso di backing per texture dinamiche compatibili, decoder in blocco, eventuale conversione isolata su worker, caricamento dei mip originali per il 3D e formati nativi solo dopo verifica del layout. Misurare miss/eviction e memoria prima di aumentare budget.

PipelineCache associa attualmente la chiave a tutto lo stato pipeline, compresi raster e vertex layout. Valutare una cache dei programmi shader separata dalle descrizioni raster, così cambi di blend/depth non richiedano programmi equivalenti distinti. Gli hit del campione pesante e zero miss indicano che questo è soprattutto un lavoro su warm-up, transizioni e memoria, non la spiegazione principale dei suoi 145 ms. Precompilazione/prewarm delle firme frequenti e uniform ridotti per variante vengono dopo il percorso per-batch. [S15]

## 6. Recompiler e runtime ARM32

NEON è già presente negli helper paired-single; anche accessi RAM inline, byte-swap builtin, chiamate statiche note e cache del dispatch indiretto sono già implementati. Non riproporli come funzionalità mancanti. [S16]

Un punto specifico da profilare è **FMA**: gli helper Vita fanno std::fma per le lane paired-single e per operazioni scalari accurate. Una sonda compilata con arm-vita-eabi-g++ -O2 produce salti a fmaf/fma, non un'istruzione fused inline. Questo conferma un possibile costo per operazione, senza dimostrarne il peso sul frame.

Campionare PC host e stack, simbolizzare con l'ELF corrispondente e distinguere tempo in fmaf/fma, conversioni FP, accessi RAM, dispatch, guest G3D/LYT/RFL, allocator e scheduler. Il watchdog guest indica un'ultima posizione osservata; non sostituisce un profilo CPU del codice nativo.

Per le funzioni calde: HLE di routine matematiche o di generazione comandi con contratto noto; kernel bulk; riduzione dei salvataggi di stato ridondanti nel codegen; specializzazione dei layout; inlining selettivo. Non rimpiazzare globalmente FMA con multiply/add: cambiano arrotondamento, NaN e semantica PPC. Testare bit-pattern e comportamento guest dove la semantica deve restare esatta.

Mantenere -Os globale finché dimensione e loader lo richiedono. Provare -O2/-O3 solo su una lista misurata di funzioni/shard, con directory oggetti distinte e confronto di testo caricabile, memoria e tempo. Gli shard aggregati sono grandi: selezionare uno shard può coinvolgere molte funzioni fredde; intervenire nel generatore, senza modificare a mano generated/. Conservare le eccezioni richieste dal runtime.

Non abilitare globalmente state-free ABI o fast-math per inseguire i 60 fps. Le chiamate native hot con contratto verificato possono ottenere più beneficio della sostituzione generalizzata dell'ABI. RFL e caricamenti vanno misurati separatamente dal gameplay: un vecchio stall di inizializzazione non identifica il collo di bottiglia del frame di gara attuale.

## 7. Piano di esecuzione e criteri di uscita

Le soglie intermedie sono **obiettivi di progetto**, non stime di guadagno garantito. Non possono essere sommate come percentuali indipendenti.

| Ordine | Intervento | Esito richiesto |
|---:|---|---|
| 0 | Identità build e baseline pulita | Flag effettivi, hash VPK/ELF/vitaGL, sessione unica, scena ripetibile, contatori per frame |
| 1 | Eliminare logging/probe caldo residuo | Timer esclusivi e aggregati; misura del costo della telemetria; stesso risultato visivo |
| 2 | ID di stato + costruzione comando per batch | DrawPacket/hash/confronti proporzionali ai cambi stato; prima tappa submit pesante <50 ms, poi <10 ms |
| 3 | Generazioni GX + decoder per layout + cache mesh | DL <20 ms come tappa, poi compatibile col budget producer <12 ms complessivo |
| 4 | Run UI e scrittura finale diretta | Consumer UI <5 ms; producer UI <12 ms; testo e animazioni corretti |
| 5 | xyzw e materiali/attributi 3D | Correttezza prospettica, depth, texture e lighting; costo CPU/GPU nuovamente misurato |
| 6 | Proprietà risorse, attese e fence | Overlap effettivo senza race, letture stale o riuso GPU prematuro |
| 7 | EFB, mip, effetti, THP e audio completi | Nessun bypass diagnostico nella build accettata; costo separato per funzionalità |
| 8 | Hotspot recompiler e GPU | Ottimizzazione selettiva supportata da profilo; A/B risoluzione/GXM solo se motivato |

Per 60 fps sostenuti proporrei questi budget operativi, con margine da affinare sull'hardware:

- producer guest + HLE + decodifica: p95 entro 12 ms;
- consumer CPU complessivo: p95 entro 8–10 ms;
- lavoro GPU: entro circa 12–14 ms;
- handoff/copie: obiettivo sotto 0,5–1 ms, contabilizzati nello stadio pertinente;
- cadenza finale: 60 nuovi frame/s a velocità di gioco corretta, distribuzione dei frame-time e missed deadline espliciti.

Questi stadi devono sovrapporsi: la condizione di throughput riguarda lo stadio più lento e le dipendenze seriali, non la somma ingenua dei budget. Se rimangono attese che serializzano producer e renderer, rispettare i singoli budget non basta. Verificare inoltre il formato VI: IntervalForFormat gestisce PAL a 20 ms e altri formati a 16.666 µs. Per il target 60 usare un percorso guest coerente a 60 Hz, senza accelerare arbitrariamente timer, audio o fisica. [S17]

## 8. Misure e verifiche necessarie per il prossimo ciclo

Acquisire su Vita reale tre scenari identificati: menu con testo/animazioni, selezione con modelli 3D, stessa gara/replay con camera ripetibile. Warm-up separato, almeno 300 frame utili per scenario e prova prolungata 15–30 minuti. Registrare p50/p95/p99, frame mancati, input/audio, memoria e controlli visivi; le transizioni fredde meritano una tabella separata.

I flag performance non eliminano oggi tutto il logging: SubmitFrame scrive una riga per ogni frame con almeno 1.000 draw anche quando PERF_LOG=0; il probe Wii attraversa vertici/triangoli. Anche i timer per-draw restano attivi. Aggregare in memoria, emettere a intervalli e confrontare con una build di misura minima. Non fare un glFinish per frame per ottenere un numero GPU e poi usarlo come prestazione finale.

Aggiungere un solo frame ID propagato tra GXCopyDisp, pacchetto, submit e presentazione. Timer distinti per acquisizione stato, decoder, trasformazione/texgen, cache, inizializzazione/confronto uniform, scrittura VBO/IBO, shader compile, EFB, wait e pacing. Separare contatori cumulativi da delta del frame. Per GPU usare metriche/fence supportate dallo stack effettivo, con overhead verificato.

I test mirati che servono durante l'implementazione sono: replay GX equivalente; DL con array modificato; mutazioni CP/BP/XF; testo sovrapposto e alpha blending; triangolo che attraversa near plane con texture; cambi materiale; EFB letto dalla CPU; riuso texture/VBO con GPU in ritardo. La compilazione non sostituisce questi controlli.

Per gli A/B della libreria, correggere anche la riproducibilità della Makefile: l'archivio vitaGL è nelle LIBS ma non tra i prerequisiti normali dell'ELF; il check attuale ne verifica solo l'esistenza. Una libreria sostituita può non provocare relink. Lo stamp native-config esiste già, ma non identifica da solo toolchain, archivio e profilo dei tradotti. I nomi VPK non codificano tutti i flag. Serve un manifest unico e una directory distinta per profilo, senza cancellare gli oggetti NEON esistenti. [S1]

## 9. Stato lasciato e prima azione concreta

L'audit ha preservato le modifiche preesistenti e non ha modificato runtime, renderer, clone Aurora o vitaGL. Sono stati aggiunti questo report e due estrazioni JSON, una per ciascuna acquisizione del log. Le uniche compilazioni effettuate sono due sonde temporanee ARM32 per dimensioni delle strutture e lowering FMA; nessun VPK è stato costruito o installato.

Dopo il trattamento prioritario delle regressioni EFB e del bianco diagnostico descritto nell'aggiornamento, il primo cambiamento strutturale è **MaterialState per ID + submit per run**, accompagnato da timer esclusivi intorno al codice che oggi riempie e confronta DrawUniforms/DrawPacket. È il punto con evidenza più diretta di lavoro evitabile nel consumer già compatto. Il secondo è la cache in spazio oggetto dietro ReplayDlDrawTemplate, preceduta dal piano di decodifica per layout. Il formato 3D con w e la copertura dei materiali devono guidare queste modifiche, perché ottimizzare solo il caso solido darebbe una base incompleta.

Una riscrittura totale di Aurora in GXM non è giustificata dai dati attuali. Un eventuale backend GXM ristretto ai batch può essere valutato dopo aver eliminato il costo per-draw e misurato lo stack completo. I 60 fps restano il criterio finale, non un risultato dimostrato da questo audit.

## Riferimenti al codice esaminato

- [S1 — Makefile, selezione Aurora, flag e dipendenze][S1]
- [S2 — submit compatto, EFB e timer][S2]
- [S3 — strutture DrawUniforms/DrawPacket][S3]
- [S4 — snapshot, deduplicazione e trasformazione CPU][S4]
- [S5 — arena, mapping e riuso slot][S5]
- [S6 — replay del template DL][S6]
- [S7 — decoder raw][S7]
- [S8 — tracking scritture GX][S8]
- [S9 — HLE GlyphDrawer][S9]
- [S10 — shader vertex][S10]
- [S11 — acquisizione materiale/texture ridotta][S11]
- [S12 — handoff e WaitRender][S12]
- [S13 — DrawSink GX del clone Aurora][S13]
- [S14 — cache texture e upload][S14]
- [S15 — cache pipeline e uniform][S15]
- [S16 — helper PPC/FMA][S16]
- [S17 — formato e pacing VI][S17]

[evidence.json]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/docs/performance-60fps-2026-09-05/evidence.json
[S1]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/Makefile.vita:96
[S2]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/aurora_packet_renderer.cpp:822
[S3]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/aurora-main/platforms/vita/gfx/vita_gfx_types.hpp:297
[S4]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:1524
[S5]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/aurora-main/platforms/vita/gfx/vita_streaming_arena.cpp:152
[S6]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/gx/gx_dl.cpp:1174
[S7]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:2254
[S8]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/include/gx_guest_write.h:1
[S9]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/gx/gx_text.cpp:26
[S10]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/aurora-main/platforms/vita/gfx/vita_shader_gen.cpp:256
[S11]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:1328
[S12]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:4322
[S13]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/aurora-vita/platforms/vita/gx/aurora_vita_draw_sink.cpp:215
[S14]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/aurora-main/platforms/vita/gfx/vita_texture_cache.cpp:64
[S15]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/aurora-main/platforms/vita/gfx/vita_pipeline_cache.cpp:147
[S16]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/include/isa/ppc_isa_float.h:438
[S17]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/runtime/src/hle/vi.cpp:156
[sorgente ufficiale vitaGL]: https://github.com/Rinnegatamante/vitaGL/blob/master/source/buffers.c

[evidence-updated.json]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/docs/performance-60fps-2026-09-05/evidence-updated.json
[S18]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/gx_backend.cpp:3669
[S19]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/vita/runtime.log:7471
[S20]: /Users/robin994/Documents/Code/PSVita/wiicompiled-vita/vita/aurora_packet_renderer.cpp:1213
