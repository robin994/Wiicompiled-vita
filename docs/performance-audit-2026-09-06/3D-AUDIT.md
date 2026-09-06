# Audit 3D: riquadri neri, materiali e percorso GX

Data: 2026-09-06. Esame del codice corrente, incluse le modifiche locali, e dell'ultima sessione di `build/vita/runtime.log` (inizio riga 5092, configurazione riga 5128, `clip_w=1`). Nessuna modifica all'eseguibile e nessun nuovo test sulla console in questo audit.

## Conclusione

Il codice dimostra diverse perdite di semantica GX. Il log dimostra anche perdita di geometria e comandi EFB nella transizione pesante. È sufficiente per dichiarare la build inadatta come riferimento grafico completo. Non permette ancora di assegnare ogni riquadro nero a W, colore materiale, EFB o texture: manca la correlazione fra il rettangolo osservato, il draw e la copia campionata.

### 1. Il percorso compilato perde attributi e riduce il materiale a uno stadio

- `vita/gx_backend.cpp:321`: `RenderVertex` conserva XYZ, un colore RGBA, ST e W opzionale. Non conserva normali, binormali/tangenti, colore 1, texture coordinate 1–7 o indici matrice texture.
- `vita/gx_backend.cpp:2623`: `DecodeRawVertexPlanned` consuma lo stream secondo il layout, ma applica soltanto PNMTXIDX, POS, CLR0 e TEX0. Anche i valori indexed degli altri attributi vengono attraversati senza conservarne il contenuto.
- `vita/gx_backend.cpp:1446`: `CaptureDrawTextureState` sceglie il primo stadio rappresentabile che usa TEXCOORD0. Le equazioni custom non classificabili ricadono su MODULATE; uno stadio selezionato non equivale al risultato finale di un programma TEV di più stadi.
- `vita/gx_backend.cpp:5922`: `GXSetTevOrder` ignora l'argomento channel.
- `vita/aurora_packet_renderer.cpp:592`: `ConfigureTev` imposta sempre `stageCount=1` e raster Color0. Il generatore GLSL può descrivere molti più stadi, ma non riceve quel materiale dal packet bridge.

Questo spiega perché il flag “3D attivo” non assicura colori, illuminazione, riflessi e maschere corretti. Per un materiale reale che produce il colore finale tramite registri TEV/Konst o un altro canale, forzare `texture * color0` può produrre nero anche con texture valida. Il log corrente non identifica ancora tale materiale sul riquadro segnalato.

**Intervento:** conservare una descrizione immutabile completa del materiale per stato condiviso; instradare i materiali semplici verso la via compatta e i materiali complessi verso una rappresentazione che conserva gli attributi realmente usati. Trasmettere TEV, swap, raster source, texgen e texture multiple prima di ottimizzare il relativo shader. Non ampliare indiscriminatamente tutti i vertici UI al formato canonico massimo.

### 2. La nuova applicazione del colore materiale espone un default errato

`vita/gx_backend.cpp:817` inizializza `chanMat` a zero. I control register iniziali selezionano il colore del vertice, quindi non rendono da soli nero tutto il rendering. Tuttavia, quando viene selezionato un canale non illuminato con materiale da registro, `CaptureDrawTextureState` (`:1430`) e il worker (`:4258`) applicano quel registro, compresa l'eventuale alpha zero.

Il `GXInit` compilato (`vita/gx_backend.cpp:5484`) configura il FIFO. Il suo HLE (`runtime/src/hle/gx/gx_init.cpp:25`) inizializza parte di GXData, ma non inizializza i registri materiale del backend a bianco. Il riferimento vendored `aurora-main/lib/dolphin/gx/GXManage.cpp:169` inizializza entrambi i materiali a bianco. Il difetto nel default è dimostrato; la dipendenza dello specifico draw dai default non è provata dal log.

**Intervento minimo:** allineare la fase GXInit e i registri canale, distinguendo RGB e alpha, poi confrontare una sequenza di setter diretti con la sequenza XF equivalente. Non usare il bianco come sostituto permanente dei materiali validi neri. Aggiungere una traccia limitata per il primo materiale nero: control RGB/alpha, mat/ambient, numChans, sorgenti TEV, texture ID e tipo di proiezione. Il materiale va poi valutato con la sua vera equazione TEV.

### 3. W ripristina la prospettiva, ma i test esistenti non dimostrano la visibilità

L'ABI a 28 byte, l'offset W, l'attributo 11 e la ricostruzione `vec4(NDC * W, W)` risultano coerenti nell'esame statico:

- `vita/gx_backend.cpp:1842` / `:1868`;
- `vita/aurora_packet_renderer.cpp:462` e `:1028`;
- `aurora-main/platforms/vita/gfx/vita_gl_util.cpp:46`;
- `aurora-main/platforms/vita/gfx/vita_shader_gen.cpp:262`;
- `aurora-main/platforms/vita/gfx/vita_renderer.cpp:77`.

La libreria VitaGL usata dichiara 16 attributi (`../aurora-vita-max-prehardware/third_party/vitaGL-speedhack-src/source/shared.h:39`), quindi location 11 non eccede il limite. Non è stato trovato un motivo statico per cui questa location debba sempre fallire. La modifica di W lascia invariate le coordinate NDC finite; da sola non corregge un capovolgimento sull'asse Y. Cambia correttamente interpolazione e clipping omogeneo, quindi può rendere evidente una matrice prima mascherata da W=1.

Rimangono difetti separati:

1. **Proiezione diretta diversa da XF.** `GXSetProjection` (`gx_backend.cpp:5800`) copia tutti i 16 coefficienti e `TransformVertex` li usa. La semantica GX usa sei coefficienti e il tipo, come il riferimento vendored `aurora-main/lib/dolphin/gx/GXTransform.cpp:14`; la ricostruzione XF locale (`gx_backend.cpp:1227`) già fissa la riga W in base al tipo. Una matrice che differisce soltanto nei coefficienti non trasmessi a GX può quindi produrre un output differente fra HLE diretto e XF. Ricostruire entrambi dalla stessa rappresentazione a sei coefficienti; verificare l'equivalenza.
2. **Intervallo Z.** Le posizioni GX in Z coprono -1..0 dopo divisione. Il packet shader lascia Z invariata e `Renderer::draw` (`vita_renderer.cpp:73`) usa `glDepthRangef(near,far)`. VitaGL (`source/misc.c:359`) applica offset e scala di metà intervallo OpenGL. Con near=0 e far=1, Z=-1 arriva a 0 ma Z=0 arriva a 0,5. Manca la conversione completa dell'intervallo GX, con le relative regole di clipping e profondità. La monotonia resta in molti casi, quindi questo da solo non prova la sparizione di ogni modello. Il [generatore vertex ufficiale Dolphin](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexShaderGen.cpp) documenta e gestisce esplicitamente l'intervallo della console e il passaggio all'intervallo dell'API ospite.
3. **Diagnostica incompleta.** I contatori CPU di visibilità (`gx_backend.cpp:4235`) controllano NDC XYZ ma non il segno W; un vertice dietro la camera può risultare “inside” dopo divisione e poi essere correttamente scartato dalla GPU. Il fallback dei W quasi nulli rimpiazza un vertice con una posizione sentinella, senza clipping corretto dell'intero primitivo. Una verifica del solo numero di vertici “inside” non basta.

**Test hardware:** triangolo noto con W diversi; cubo con facce etichettate e culling; intersezione del near plane; griglia near/mid/far con depth test; stessa geometria via GXSetProjection diretto e XF. Per il modello reale registrare min/max W e quanti W sono negativi, non finiti o quasi nulli. Usare un'immagine prima della copia EFB e la stessa immagine campionata su un quad per localizzare l'eventuale flip.

### 4. Le coordinate texture proiettive vengono divise troppo presto

`vita/gx_backend.cpp:1920` calcola `s=pu/pq`, `t=pv/pq` sul vertice per MTX3x4 e scarta Q. Il packet ha soltanto ST e non imposta un `texgenCount` proiettivo. Il generatore generico ha già la divisione `v_tex.xy/v_tex.z` (`vita_shader_gen.cpp:41`), che qui non viene attivata.

Interpolare S/Q per vertice non equivale a interpolare S,T,Q e dividere nel fragment. Esempio senza prospettiva di posizione: S=(0,1), Q=(1,2); il punto medio corretto ha S/Q=1/3, mentre il codice attuale interpola (0,1/2) e produce 1/4. W di posizione non sostituisce Q della proiezione texture. Il difetto è dimostrato dal calcolo; la gravità sui materiali segnalati richiede un draw catturato.

**Intervento:** conservare Q nei soli layout che lo usano e attivare la divisione nel fragment shader. Testare prima una texture a scacchi proiettata su un quad inclinato, poi le EFB/light texture del menu.

### 5. EFB: perdita osservata di comandi e conversione formato mancante

Nel frame 1585, riga 5693 dell'ultimo log: 399 chiamate copy, 303 copy registrate, 209 destroy registrati, 512 comandi totali, 169 rifiuti per capacità. La geometria raggiunge 49.152 vertici con 21 raw-cap failures e 348 vertici dropped. `ReserveEfbCommand` (`gx_backend.cpp:6139`) rifiuta i comandi oltre il limite; se è una copy con clear, anche l'effetto clear non viene registrato. La contabilità non fornisce la sequenza esatta dei 169 comandi, quindi non prova quanti destroy siano persi o quali oggetti risultino trattenuti.

Successivamente il log mostra 43 entry EFB e 4.184.032/4.194.304 byte, con nuovi rifiuti di allocazione. Un rettangolo che campiona una copia assente può cadere nel percorso di decodifica della RAM guest (`vita/aurora_packet_renderer.cpp:1096`), ma il percorso resident non materializza la copia nel layout GX della RAM guest. Zero o dati vecchi sono quindi una conseguenza possibile concreta, da verificare per destinazione.

Un'altra lacuna è deterministica nel codice: `AuroraPacketRendererCopyEfb` traduce `copy.format` (`:1323`), ma il percorso residente attivo passa a `capture_resident` (`:1426`) senza alcun formato. `vita_efb.cpp:403` trasferisce RGBA senza trasformazione canali. Le conversioni I/IA/RGB565/R/G/B ecc. esistono nel blitter (`vita_efb.cpp:54`–`:127`), ma non nel percorso residente. Per esempio RGB565 dovrebbe eliminare l'alpha della sorgente, I8 dovrebbe produrre intensità; qui ricevono ancora RGBA originale. Non basta che una copia risulti riuscita per considerarla fedele.

**Intervento:** prima una coda che preserva tutti gli effetti in FIFO e un ciclo di vita verificabile delle destinazioni, poi copie residenti che mantengono il formato richiesto. Conservare esplicitamente lo stato “EFB prevista ma non disponibile”, evitando di interpretarlo come una normale texture guest valida. Non alzare il budget come unico rimedio e non riattivare il percorso FBO già associato al crash hardware senza una verifica isolata.

## Rapporto fra aurora-vita e il wrapper compilato

La copia `aurora-vita/` è un riferimento separato; non è il packet bridge attivo. In quella copia:

- `platforms/vita/gfx/vita_gfx_types.hpp:12` prevede 8 texture e 16 stadi TEV;
- `platforms/vita/gx/aurora_gx_bridge.cpp:205`–`:215` traduce texgen, canali, swap e programma TEV completo;
- `platforms/vita/gfx/vita_vertex_pipeline.cpp:36`–`:55` contiene la valutazione CPU delle luci; `:76`–`:107` conserva normali, canali e coordinate Q.

Il generatore e i tipi simili esistono anche sotto `aurora-main/platforms/vita`; il limite principale qui è la cattura/riduzione in `vita/gx_backend.cpp` e `vita/aurora_packet_renderer.cpp`. Non è corretto concludere che basta aggiornare la copia clonata per riparare il VPK, né che la presenza del codice completo sia una prova hardware. Il percorso completo CPU può aumentare i costi: recuperare fedeltà mantenendo le ottimizzazioni del formato semplice e introducendo layout/stati specializzati.

## Ordine di intervento e criteri di accettazione

1. Riprodurre lo stesso menu da boot pulito e dopo cicli avanti/indietro; identificare il draw e la destinazione EFB del primo riquadro nero. Aggiungere solo una cattura limitata del primo errore e contatori aggregati.
2. Eliminare overflow geometria/EFB e dimostrare che create/destroy, clear e sample conservano ordine e durata. Nessun contenuto scartato nel test completo.
3. Riparare default/materiali e conversioni EFB; isolare W/Z/proiezione/Q con le prove sopra. Due cambiamenti grafici indipendenti non devono essere valutati contemporaneamente nello stesso A/B.
4. Portare il materiale completo attraverso la cattura di stato, usando il codice del wrapper generico come riferimento verificabile. Contare per frame gli stadi e gli attributi ancora degradati; accettazione: nessuna degradazione nei menu/gara usati per il benchmark.
5. Solo a output corretto confrontare costi CPU/GPU e ottimizzare cache, copie e shader, mantenendo 3D e video attivi. Una build che mostra meno contenuto non è una misura valida del target 60 FPS.

I test host precedenti su ABI e formule possono impedire regressioni locali, ma non validano pixel, clipping, GXM o l'intero programma GX. L'esito visuale negativo riferito dall'utente resta il risultato hardware da risolvere.
