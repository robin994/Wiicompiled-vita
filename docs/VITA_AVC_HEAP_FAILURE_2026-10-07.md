# WiiCompiled Vita: video assenti e uscita dopo 1 player

La successiva prova AVC/buffering è documentata nella
[diagnosi aggiornata](VITA_AVC_GREEN_CPU_HEAP_2026-10-07.md): video visibili
con verde persistente, menu lenti e uscita ancora presente. Il seguito
mantiene i risultati e le identità della precedente prova senza output.

## Risultato sulla console

L'utente ha installato manualmente la prima candidata hardware AVC e il nuovo
PSARC. La prova resta negativa: i filmati non compaiono nelle card; dopo
"1 player" appare uno schermo nero e il gioco torna alla LiveArea.

Raccolta verificata: `device-runs/hardware-avc-20261007T155537Z/`.
Il SELF riletto dalla Vita corrisponde all'installazione
`build/vita-psarc/install-hardware-avc-20261007T155122Z/` e alla candidata
`build/vita-psarc/candidate-hardware-avc-20261007T151910Z/`:

- SELF: `5cea8d875402423f88c8dec3f9ed908d551f86b80e73738c2a05a30cb3a91c74`.
- VPK: `e50ec84a2a57abf31684b3fd2099472cce231883d883c99e91b9dbcd055eda38`.
- ELF: `72e7e24d9c27972afb74e7241a5b83ad783fc5351b9e2133e0fb2204ae358884`.
- Config: `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.

Il log emette `build=psarc-hardware-avc-20261007`, `hardware_avc=1` e verifica
gli hash degli eseguibili guest caricati dal PSARC. L'archivio attivo ha
1.406.613.154 byte. La sua identità completa sulla console non è stata
verificata con una rilettura di 1,41 GB: dimensione e bootstrap sono prove
parziali. Non è presente `mariokart.iso`; il backup PSARC lasciato dall'utente
non è stato modificato.

Non risultano nuovi dump del kernel. La sessione corrente termina con
`PPCHalt` dopo un'asserzione `LinkList.h:573`. `crash.txt` coincide con un
precedente file: la diagnosi corrente usa la sessione del log e il nuovo dump
dell'heap, legati al SELF verificato.

## Video: invio accettato senza immagine

Il decoder hardware inizializza correttamente modulo, memoria e stream:

```text
[avc-hardware] initialized max=704x768 refs=1 reference_bytes=5242880 raster_bytes=1048576 format=nv12
[avc-hardware] rejected output count=0 size=0x0 expected=512x192 pitch=704 format=0x20 crop=0,0,0,0
[video-native] avc-error source=0x80b0f8cc bytes=34100 reason=AVC output count/geometry/crop mismatch
```

`sceAvcdecDecode` restituisce successo, ma `numOfOutput` è zero. Il nostro
codice lo trattava come un errore geometrico, distruggeva il decoder e
restituiva un errore al player THP. La prova contiene un solo tentativo e
nessun fotogramma `[avc-native]` pubblicato.

La [documentazione VitaSDK](https://docs.vitasdk.org/group__SceVideoDecUser.html)
descrive separatamente il risultato della chiamata e il numero di immagini.
Anche [Moonlight Vita](https://github.com/xyzz/vita-moonlight/blob/vita/src/video/vita.c)
accetta il caso senza output senza segnalare un errore al chiamante. Questo
supporta la correzione del controllo; non dimostra che il nostro bitstream,
layout delle superfici e passaggio a GX producano già video sulla console.

La nuova implementazione distingue `Error`, `Pending`, `Picture`. Con
`Pending`, mantiene il decoder, lascia intatti i buffer immagine e restituisce
successo al player senza effettuare tiling o invalidazioni GX. Quando cambia
la geometria con una vecchia immagine ancora in attesa, ricrea il decoder per
impedire che l'immagine precedente sia scritta nel buffer di un altro filmato.

Ogni fotogramma contiene SPS/PPS e IDR indipendenti. La nuova build aggiunge
una NAL `end_of_sequence` al buffer nativo inviato al decoder per delimitare
la sequenza senza attendere la card successiva. Questa modifica riguarda
solo il buffer runtime: il PSARC esistente è compatibile, non occorre
riconvertire o ricopiare i video.

Il confronto host sul PSARC reale copre i primi tre fotogrammi di tutti i 27
filmati, 81 immagini: stessa quantità di output e pixel identici prima e dopo
la chiusura della sequenza. Non prova l'effetto della NAL sul firmware Vita.
Se il decoder continua a restituire soltanto `Pending`, occorre analizzare il
bitstream e il comportamento hardware; restituire successo da solo non rende
visibile un video.

## Uscita: blocco libero dell'heap azzerato

Dump completo `heap-failure-0.bin`: 30.749.692 byte,
SHA-256 `6b92b257daac69daa3a8eabe073ca6a7ebba85c6875a73c72a1eabc4969fc733`.
Analisi derivata: `build/avc-fix-analysis/HEAP_ANALYSIS.json`.

- Heap `EXPH` a `0x91a7fe54`, intervallo dati
  `0x91a7fea4..0x937d3250`, azzeramento delle nuove allocazioni abilitato.
- 17.749 blocchi usati con tag, dimensioni e collegamenti validi; payload
  complessivo 10.410.340 byte, blocco maggiore 8.388.864 byte.
- Primo blocco libero a `0x923e337c`: quattro byte, collegamento al successivo
  `0x924b2e30`.
- Il secondo blocco libero ha tutti i 16 byte di intestazione a zero, compresi
  tag, dimensione e collegamenti. Fisicamente restano 20.055.072 byte fino al
  termine dell'heap, ma l'allocatore non può utilizzare il blocco invalido.
- Ultima allocazione valida: header `0x924b2dc0`, payload `0x924b2dd0`,
  dimensione 96. Il suo termine coincide con l'intestazione libera corrotta.

I due oggetti precedenti hanno vtable `0x802737c0`, riconducibile a
`nw4r::lyt::AnimTransformBasic`. Il puntatore al payload da 96 byte e il campo
conteggio indicano una tabella di sei collegamenti di animazione, 16 byte
ciascuno. È un indizio sul proprietario del buffer adiacente; il dump finale
non identifica chi ha scritto i 16 byte dell'intestazione libera.

Il successivo layout fallisce tutte le 15 allocazioni, restituisce una root
nulla e porta all'asserzione. È corruzione dell'heap guest; la memoria libera
del processo Vita e del CDRAM non misura lo spazio utilizzabile di quell'heap.
La durata dello schermo nero può includere la scrittura del dump, senza
dimostrare che il gioco stia caricando correttamente il menu successivo.

## Verifiche e diagnostica della nuova build

Il replay esegue i sorgenti SDK effettivamente generati con i normali helper
Vita di memoria e ABI, su RAM host. L'harness fornisce la memoria, i fallback
scalari e un callback di allocazione; non emula il firmware o il compilatore
ARM e non esegue l'intero gioco.

- 5.121 divisioni di blocchi, compreso il caso da 96 byte del dump.
- 24.672 combinazioni di dimensione, allineamento e valore di `memset`, con
  canarini ai limiti e attraversamento della pagina da 1 MiB.
- 1.024 conteggi della tabella dei collegamenti di `AnimTransformBasic`, senza
  lookup delle texture esterne. Le inizializzazioni preservano il blocco libero.
- 18/18 test host e 4/4 contratti ASan/UBSan passati.
- Audit ARM di GXM, pthread/SceFiber, input/attese/stdio, wrapper, registry e
  tabelle di dispatch passati; `ATTRIBUTE2=12` nel pacchetto.

L'errore non è riprodotto da questi casi host. La build diagnostica controlla
l'ultimo blocco libero prima e dopo il callback SDK dell'heap e prima e dopo
`AnimTransformBasic::SetResource`. Conserva le ultime 32 allocazioni con
caller, dimensione, allineamento, risultato e indirizzo del blocco libero.
Al primo errore emette `[heap-watch-corruption]`, fase, metadata e cronologia.
Non modifica l'heap né sostituisce la root nulla con un oggetto inventato.

Il callback `0x80199b58` è intercettato tramite la registrazione e il dispatch
indiretto. Avvolgere soltanto `MEMAllocFromExpHeapEx` non avrebbe garantito
questa copertura: le chiamate dirette fra funzioni della stessa unità
compilata possono aggirare il wrapping del linker. L'audit verifica che il
callback e `SetResource` mantengano tipo `BaseTranslated` ed entry coincidenti
fra registry e dispatch, e che i wrapper raggiungano le implementazioni reali.

Questa diagnostica individua il primo errore osservato nelle chiamate coperte.
Se la fase è `alloc-before` o `animation-before`, un altro codice può aver
corrotto l'heap prima del controllo. La cronologia non basta a identificare
una scrittura arbitraria e non costituisce una correzione dell'uscita.

## Build installata e prossima verifica hardware

Installazione completata sulla Vita `192.168.1.79:1337`; tutti i 14 file
dell'app sono stati riletti e coincidono con il VPK. Il backup del SELF
precedente è verificato, `Config.toml` e SFO registrato sono identici, il
pacchetto mantiene `ATTRIBUTE2=12`. Nessuna scrittura al PSARC o avvio del
gioco è stato effettuato.

- Candidata immutabile:
  `build/vita-psarc/candidate-avc-buffer-heapwatch-20261007T191634Z/`.
- Installazione e prove:
  `build/vita-psarc/install-avc-buffer-heapwatch-20261007T191722Z/`.
- SELF: `3b99e20d282c59b1aca2abcb6495bb1ceeb0db6da4aa0f06d04706bea44cd746`.
- VPK: `2bdfdefc1083e7199e18e52a3c2adc2a345b2ea2fed83f2ea760cebfbeb068e6`.
- ELF: `70b445f26ba32307e1c816004b8cc929224d36f181f84c7bfc1110c0a4ad2f4e`.
- Sorgenti e input generati: 31.057 file verificati anche nel tar conservato,
  SHA-256 aggregato
  `b4baa9e1b4136c8f953816d5d05763b338b4532c7413702c18974ed633d77380`.

Vita Companion ha restituito un errore generico al comando di chiusura;
quella risposta non dimostra se il processo precedente fosse attivo.
L'aggiornamento ha usato upload temporaneo verificato e rinomina, con backup
del vecchio eseguibile. È necessario chiudere completamente la vecchia
sessione e riaprire manualmente prima di valutare la nuova build.

Marker: `psarc-avc-buffer-heapwatch-20261007`. Chiudere la vecchia sessione,
aprire manualmente il gioco, restare nel menu delle card e selezionare
"1 player". Nessun avvio remoto è autorizzato o previsto.

La nuova raccolta deve verificare SELF e marker, eventuali righe
`[avc-native]`, `accepted-without-picture` ed errori del decoder, quindi la
prima riga `[heap-watch-corruption]` e la cronologia relativa. Il nuovo dump
dell'heap e qualsiasi coredump devono restare abbinati a ELF/map/VELF/SELF
della build effettivamente installata.

Raccolta preparata: `build/capture-avc-buffer-heapwatch-run.py`; verifica la
nuova installazione e il marker prima di accettare il log. Le copie nei due
archivi sopra sono sigillate e conservano lo stato al momento della creazione;
la candidata preinstallazione non viene riscritta per aggiungere prove future.

Video visibili, superamento di "1 player", miglioramento degli FPS e Mii
restano da dimostrare sulla Vita. Target: 30 FPS. Questa è una build
diagnostica, non un benchmark senza strumentazione.
