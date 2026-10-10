# WiiCompiled Vita: attese sulla pila nativa e nuova build

Aggiornamento dopo l'avvio manuale: il dump `1791316902` conferma il passaggio
delle attese VI sulla pila nativa e un avvio che raggiunge l'audio. Il processo
si arresta poi nel mixer; l'analisi trova anche il gate pthread disattivato
nel binario. Vedere il [report audio e runtime](VITA_CRASH_AUDIO_2026-10-06.md).
Le sezioni seguenti conservano implementazione, verifiche e stato alla consegna.

La candidata `psarc-native-wait-20261006` è compilata, verificata e installata
sulla console `192.168.1.79:1337`, app `WICMKW001`. Non è stato inviato alcun
comando di avvio. La risoluzione del quarto crash richiede il prossimo test
manuale sulla Vita.

## Implementazione

Il [dump del retrace](VITA_CRASH_RETRACE_2026-10-06.md) fermava il thread in
`sceKernelDelayThread`, raggiunta da `usleep` durante il ciclo idle VI, con SP
fuori dalla pila nativa. La durata richiesta era positiva: 987 microsecondi.
Il nuovo intervento corregge il confine della sospensione host individuato
contro il SELF effettivamente installato e il suo ELF abbinato.

[HostWait](../runtime/src/host_wait.cpp) centralizza le attese in `SleepUntil`
e `SleepFor`. Sulla Vita il dispatcher sincrono `CallOnNativeStack` esegue
la sola attesa host sulla pila nativa dello stesso thread. Il chiamante
riprende nel proprio contesto, senza una nuova scelta del thread guest.
La scadenza assoluta viene mantenuta anche durante il passaggio di contesto
e ricontrollata prima di attendere. Scadenze già trascorse e durate non
positive ritornano senza dormire.

| Percorso | Attesa coperta | Contratto conservato |
| --- | --- | --- |
| VI/idle, `SleepPreciselyUntil` | Entrambi i rami di attesa host | Scadenze, intervalli VI, finestre finali di spin e callback |
| GX, `BeginNextAuroraFrameWithRetry` | Ritentativo da 1 ms | Timeout e stato del frame |
| Controller, `GKI_delay` | Pausa host | Zero trasformato in 1 ms, limite di 10 ms |
| `OS__Panic` | Pausa finale da 200 ms | Diagnostica e percorso di uscita preesistenti |

Il percorso Windows con timer ad alta risoluzione resta quello esistente.
Sui target host le attese continuano a usare la libreria standard.
Le attese dei worker nativi PSARC e dei monitor audio restano nei rispettivi
thread. Sono mantenute le precedenti correzioni GXM, DVD e logging.

Le prime quattro attese effettive per sito e thread scrivono
`[wait-native] site=vi|gx-retry|gki|panic wait=... stack=... remaining_us=...`
dalla pila nativa, prima della chiamata alla libreria. Il numero di marker è
limitato: non introduce un log continuo per frame. `remaining_us` è il tempo
residuo campionato prima del log, non una misura della durata della syscall.

## Verifiche completate

- **7/7 contratti host** superati: PSARC, ISO, indice, EFB, contesti, OSReport
  e nuovo contratto delle attese native.
- Il nuovo test compila `HostWait` di produzione con il percorso Vita attivo
  su host, usa il vero cambio di contesto e attese reali. Controlla l'indirizzo
  di pila del callback di produzione, le scadenze assolute e relative, i casi
  già scaduti/non positivi, i chiamanti nativi e la ripresa esatta delle
  coroutine senza rischedulare il peer durante la pausa. Non esegue il
  firmware Vita.
- Build Vita completata. Audit ELF/map: **44.949 simboli eseguibili**, draw e
  present GXM presenti, nessun GL/vitaGL/vita2d.
- Test delle istruzioni ARM del binario finale superato: `r4-r11`, `d8-d15`,
  SP e continuazione ARM/Thumb conservati.
- Controllo dei collegamenti dell'ELF finale superato: VI, GX retry, GKI e
  panic raggiungono `HostWait`; `SleepFor` raggiunge `SleepUntil`, che raggiunge
  il dispatcher nativo. Rimangono verificati i wrapper stdio e OSReport.
  L'audit risolve i literal dei veneer ARM/Thumb effettivi.
- SELF incluso nel VPK identico al SELF prodotto dalla build e riletto dopo
  l'installazione.

Il warning VitaSDK preesistente di `crtn.o` senza `.note.GNU-stack` rimane;
nessun errore di compilazione. Async GX, fast math, texture native
sperimentali, geometria residente e profiling guest sono OFF; backend GXM.

## Identità e archivio della candidata

| Artefatto | Byte | SHA-256 |
| --- | ---: | --- |
| VPK | 22.737.299 | `aab92d3b8e508fcfa6205ccbc90f460ae88618e7907650c22536cf3a16b8eae1` |
| SELF | 22.941.335 | `98861d6a8649fca0e3f2d4f25c2986e8c7df0458113326f591a3ff16ef4307b3` |
| ELF | 69.628.828 | `15b68a76d8a1b885a74e5681966e0bd6f7442d59457c38caaaa03769d85aed16` |
| Map | 43.743.304 | `93d020cc7e0f939351713279f15c326b310ec110013538770c6cf55dab7b5581` |
| VELF | 85.684.806 | `7dc86d936962ad26b26230f46e47fa6cedfcb5f9c1a8b71c24204b5a09e1b222` |

Digest sorgenti, input generati e strumenti:
`0b3fd3b0334ecd31825f4181914cc9f19610d694213c1f0f6b2016103cf610f2`.
Il manifest conserva HEAD, pin Aurora, modifiche locali, opzioni e identità
ISO/PSARC. Il working tree preesistente è conservato senza commit automatici.

Directory di consegna indipendente dagli output delle build successive:
`build/vita-psarc/install-native-wait-20261006T195636Z`.
Contiene VPK, SELF, ELF, map, VELF, manifest, copie dei sorgenti modificati,
log, disassemblazione e verifica dell'installazione.

- [VPK archiviato](../build/vita-psarc/install-native-wait-20261006T195636Z/wiicompiled-vita-game.vpk).
- [DELIVERY.json](../build/vita-psarc/install-native-wait-20261006T195636Z/DELIVERY.json).
- [VALIDATION.json](../build/vita-psarc/install-native-wait-20261006T195636Z/VALIDATION.json).
- [INSTALL.json](../build/vita-psarc/install-native-wait-20261006T195636Z/INSTALL.json).
- [Disassemblazione dei collegamenti](../build/vita-psarc/install-native-wait-20261006T195636Z/native-disassembly.txt).

## Installazione e sorgente dati

Aggiornamento del contenuto del pacchetto nell'app già registrata:
il solo `eboot.bin` differiva. Il SELF è stato caricato in un percorso
temporaneo, riletto e verificato prima della sostituzione. Tutti i **14 file**
del VPK riletti dopo l'aggiornamento coincidono con quelli archiviati.
Non è stato inviato alcun comando di avvio.

Il SELF precedente, SHA-256
`9557d79a1f1c14c50d910fc535fcea08d30ec0e6b6c53884a003755be5c35ab2`,
è conservato localmente e sulla console in
`ux0:/app/WICMKW001/eboot.bin.crashed-native-log-20261006T195636Z`.
La configurazione è riletta e identica, SHA-256
`9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.

Sulla Vita il gioco usa il solo `mariokart.psarc`, dimensione remota verificata
2.642.224.628 byte. La ISO è assente dalla directory dati; il suo eventuale
percorso in configurazione è ignorato con PSARC selezionato. Il PSARC non è
stato modificato né ritrasferito; l'hash dell'intero archivio remoto non è
stato ricalcolato. La ISO sul computer rimane la sorgente di preparazione.

## Prossimo test sul dispositivo

Avviare manualmente l'app e conservare il nuovo log della sessione.
Il marker atteso è `build=psarc-native-wait-20261006`.
Confrontare gli indirizzi `[wait-native] site=vi` con l'intervallo
`[boot] native-stack=... size=...` dello stesso avvio. Il caricamento deve
superare il precedente arresto nell'attesa del retrace, quindi raggiungere
menu e caricamento pista. Un nuovo dump deve essere analizzato contro gli
artefatti di questa candidata, senza riutilizzare l'ELF della build dei log.

La risoluzione del crash, menu, gara, correttezza visiva e prestazioni restano
da verificare sulla Vita. L'eliminazione di TEV e le altre ottimizzazioni
del motore nel piano iniziale non sono introdotte da questa correzione.
