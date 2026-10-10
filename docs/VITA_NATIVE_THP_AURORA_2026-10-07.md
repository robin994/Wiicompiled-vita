# WiiCompiled: THP nativo, heap del layout e opzioni Aurora

## Risultato della prova FMA/layout

L'utente segnala ancora 1 FPS, Mii mancante nel save select e uscita dopo
"1 player". La cattura conservata è
`device-runs/cpu-fma-layout-20261007T133104Z`; il SELF riletto è
`2e1aa8c341520c0e619b734a626a287ec660a4ca310114da1c94451968610269`.
Config SHA-256 `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.

Le finestre finali hanno 0,938–0,946 draw/s, 1.057–1.066 ms/frame. Le stime
FMA sono il 39–40% del tempo di parete; i tempi paired contengono le FMA e
non vanno sommati. Il confronto non è quiet e non dimostra un beneficio
della FMA esatta. Obiettivo invariato: **30 FPS, 33,33 ms/frame**.

Due riferimenti al codice caricato determinano il bias `0x26000`. Gli otto
siti paired dominanti delle finestre lente sono nel codice tradotto di
`__THPInverseDCTNoYPos`, guest `0x801B4DA8`. Il principale chiamante della
matematica campionata è quindi il video dei menu.

Il layout `common_w023_rule_menu.brlyt` catturato dalla RAM ha lo stesso hash
e gli stessi 3.092 byte dell'originale nel PSARC. Falliscono 15 allocazioni
SDK, 3.356 byte complessivi. Layout::Build lascia root/groups nulli pur
restituendo 1; MainLayout::Build attraversa una lista nulla e termina con
LinkList.h:573 / PPCHalt. Nessun nuovo coredump kernel. La causa immediata
è verificata; esaurimento, frammentazione o corruzione dell'heap restano da
distinguere.

## Confronto effettivo delle opzioni

Fonti: cache immutabile della build WiiCompiled installata, cache locale
`strikersVita/smstrikers-port/build-vita-gxm-latest/CMakeCache.txt` e script
`build-vita-aurora-native.sh`, letti il 7 ottobre. Copie conservate in
`build/thp-analysis`. Aurora WiiCompiled HEAD `28512bd75531508276be1582882145ea021151eb`
con modifiche locali; Aurora embedded Strikers HEAD
`a1d9b6624353686f9a507863f28dcc13f1d75308` al momento della lettura.
Non sono la stessa revisione byte per byte. La cache Strikers descrive una configurazione locale,
**non prova l'identità del binario attualmente installato per Strikers**.

| Opzione | WiiCompiled FMA/layout | Cache Strikers | Implicazione |
| --- | --- | --- | --- |
| Renderer | GXM | GXM | Backend nativo in entrambi |
| ABI del frontend | SDK | GAMECUBE | Deve seguire il gioco/bridge; non copiare GAMECUBE su Mario Kart Wii |
| GXM_DIRECT_STREAM_WRITE | OFF | ON | Trasferimento di vertici da valutare separatamente |
| GXM_DIRECT_DRAW_SUBMIT | OFF | ON | Invio diretto dei draw non attivo in WiiCompiled |
| GXM_IMMEDIATE_DRAW_VIEW | OFF | ON | Percorso immediato non attivo in WiiCompiled |
| DISTINCT_CPU_CORES | OFF | ON | Due helper esistono, ma non lavorano sui quad dei menu |
| NATIVE_GX_TEXTURES | OFF | ON | Texture packed native non abilitate in WiiCompiled |
| NATIVE_CMPR | OFF | OFF | Nessuna parità da ottenere attivando CMPR insieme alle texture packed |
| LTO | OFF | ON | Ottimizzazione di link assente nella build WiiCompiled |
| ASYNC_GX | OFF | ON | Lo script Strikers esaminato imposta OFF; cache e script divergono |
| Budget geometria immutabile | 0 | default 8 MiB nel CMake Strikers | Scelta runtime distinta dalle chiavi del compilatore |
| FFmpeg/THP | Traduzione PPC | STRIKERS_FFMPEG=OFF | Il codice Strikers offre un decoder nativo, ma questo preset salta i video |

Non stiamo usando tutte le opzioni ottimizzate di Strikers. Attivarle insieme
non isola il costo trovato nel decoder video e non corregge le allocazioni
o le coordinate EFB del Mii. Le opzioni restano esperimenti separati da
validare con immagini e frame time associati al binario installato.

## Implementazione della candidata successiva

`WIC_NATIVE_THP`, OFF per default, sostituisce il percorso video completo
nel chiamante RMCP01 `0x80552C2C` con libjpeg nativo. Mantiene i filmati.
La lunghezza viene dalla tabella dei componenti del frame THP; non si cerca
il primo FF/D9, che può comparire anche nell'entropia non escapata.
La conversione inserisce FF/00 solo nei dati entropy, conserva l'EOI finale
e gestisce i tre byte di padding. Header baseline 4:2:0, dimensioni e
destinazioni vengono validati, comprese le dimensioni da cui PlayerSetBuffer
ha allocato i buffer.

libjpeg restituisce le tre plane Y/U/V. La preparazione delle texture GX I8
usa gli helper persistenti su intervalli di tile 8×4 disgiunti. Non vengono
eseguiti contesti PPC, FIFO, allocazioni guest o chiamate GXM sui worker.
La decodifica JPEG resta sincrona sul thread chiamante; la preparazione delle
texture è parallela. Una chiamata sconosciuta, un formato non supportato,
un errore JPEG o un buffer non valido mantiene la funzione PPC originale.
Registrazione, kind BaseTranslated e tabella dispatch non cambiano; l'audit
verifica il wrapper e il chiamante diretto, oltre al fallback.

Il diagnostico dell'allocatore aggiunge heap, alignment, limiti, flags,
lista libera e dimensione del blocco massimo. La lettura è limitata a 4.096
nodi e avviene sulla pila nativa. Non modifica liste, ritorni né asserzioni.
La candidata non contiene ancora una correzione dell'uscita dopo 1 player.

Il Mii continua a richiedere una correzione separata dell'EFB: la zona
temporanea Y=456 viene mappata a Y=544, fuori dal target visibile 960×544,
e la copia finisce con altezza zero. Questa candidata non altera il renderer.

## Validazione e limiti

- 13/13 test host core/runtime/input/matematica/THP passati. I test THP coprono
  frame parziali rispetto agli MCU, padding, input troncati, conversione GX I8,
  intervalli paralleli, buffer sovrapposti, guardia del codice e ABI/fallback.
  Il decoder standalone passa anche ASan/UBSan sui nove frame reali.
- Nove frame reali da title, top_menu e single_top decodificati e verificati
  sul computer. Nessuna dipendenza da file estratti sulla Vita.
- Confronto YUV con FFmpeg THP: differenza massima **1 su 255**, media assoluta
  0,00025–0,02409 sui campioni. Non è equivalenza bit per bit con il decoder
  PPC originale, né prova visiva sulla Vita.
- Dipendenza Vita: libjpeg del VitaSDK. Nella libreria esaminata non compaiono
  simboli NEON/jsimd; non si dichiara un decoder SIMD o hardware.
- Le chiavi Aurora, le frequenze, gli helper e il PSARC rimangono quelli del
  controllo FMA/layout. La modifica prestazionale è il percorso THP.
- Target 30 FPS, uscita, Mii, gare e sospensione restano gate hardware aperti.

Formato THP verificato nel [decoder ufficiale FFmpeg](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/mjpegdec.c);
uscita YUV raw secondo la [documentazione libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/doc/libjpeg.txt).
L'integrazione non copia codice del decoder FFmpeg.

Build, audit e consegna della candidata sono registrati nel manifest e
nel report di installazione associati; una compilazione riuscita non prova FPS.
