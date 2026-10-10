# WiiCompiled Vita: crash durante il caricamento della gara

Il coredump più recente identifica una terminazione C++ per `std::bad_alloc`
nel caricatore degli asset preconvertiti. Il punto fallito è la copia di un
payload texture già letto e verificato; la nuova candidata CDRAM preparata
prima dell'analisi conteneva ancora questo percorso. È stata conservata e
sostituita da una candidata che trasferisce il buffer senza duplicarlo.

## Cattura e identità

Console `192.168.1.79:1337`, applicazione `WICMKW001`. Dump recuperato:
`psp2core-1791446436-0x0010d62e2d-eboot.bin.psp2dmp`, 216.000 byte,
SHA-256 `6b2ca5c7c9ba1f5af8afb6242455a33ed8d998191016b82b382dd35ff3b276a8`.

Il SELF riletto dalla console è
`7e0c2e792808d50410cbfba4056014f58714e39b03cf9d96a15dc54bfae1ad53`,
identico alla build `psarc-precompiled-assets-async-20261008`.
L'ELF corrispondente ha SHA-256
`84b4a2beca0e89c56f76075efb68a36980c8449e832462559a1ab5288e7ecf44`.
Il marker dell'ultima sessione e due indirizzi di codice registrati nel log
confermano la rilocazione RX di `0x24000` rilevata nel dump.

La [cattura](../device-runs/race-loading-core-20261008T080258Z/CAPTURE.json)
conserva dump, SELF, SFO, configurazione e runtime.log completo.
[MATCHED_ARTIFACTS.json](../device-runs/race-loading-core-20261008T080258Z/MATCHED_ARTIFACTS.json)
vincola l'analisi all'ELF, map, VELF e VPK archiviati della build installata.
Il file crash.txt sul dispositivo risale al giorno precedente e non è
utilizzato per attribuire questo arresto. Non sono stati inviati comandi
di installazione o avvio e non sono stati modificati file sulla Vita.

## Punto di arresto

| Dato | Valore |
| --- | --- |
| Thread fermato | `melee_gx_frontend`, consumer GX |
| Motivo kernel grezzo | `0x30002` |
| PC runtime / ELF | `0x82d6aa7a` / `0x82d46a7a` |
| Funzione al PC | `_kill_r`, percorso di terminazione dopo abort |
| Registro del segnale | `r2=6`, SIGABRT |
| Pila del thread | `[0x8d001000, 0x8d101000)` |
| SP | `0x8d0ffff0`, dentro la pila registrata |

Il log termina con `terminate called after throwing an instance of
'std::bad_alloc'` e `what(): std::bad_alloc`. I ritorni salvati nella pila
sono stati verificati contro cinque chiamate nel codice ARM:

```text
DrawSink::submit
  Renderer::create_texture
    TextureCache::get_or_upload
      load_native_texture
        record_payload
          operator new -> throw -> terminate -> abort -> _kill_r
```

È una ricostruzione da indirizzi salvati e chiamate verificate, non un
unwind completo. La chiamata a operator new è a `0x82c918d6`, con ritorno
salvato `0x82c918da`. Essa segue i controlli di dimensione, confronto esatto
della sorgente e checksum del payload. Precede l'upload della texture in GXM.

Evidenze: [analisi](../device-runs/race-loading-core-20261008T080258Z/analysis/analysis.txt),
[chiamate verificate](../device-runs/race-loading-core-20261008T080258Z/analysis/CALL_SITE_EVIDENCE.json),
[disassemblazione della copia](../device-runs/race-loading-core-20261008T080258Z/analysis/payload-copy-disassembly.txt)
e [causa e limiti](../device-runs/race-loading-core-20261008T080258Z/analysis/CRASH_CAUSE.json).

## Texture coinvolta e memoria

Il dump contiene l'intestazione AVNR del record a `0x8c763600` e il
descrittore della sorgente: CMPR 1024×1024, un mip, nessuna palette,
524.288 byte GX. Il payload nativo contiene 16 byte di intestazione e
4.194.304 byte RGBA. La richiesta di copia è quindi **4 MiB più 16 byte**.
Il registro r11 contiene `0x400010`, coerente con la dimensione del payload.
L'intestazione è conservata in
[RECORD_HEADERS.json](../device-runs/race-loading-core-20261008T080258Z/analysis/RECORD_HEADERS.json).

Il vecchio record_payload costruiva un nuovo vector con tutto il payload,
mentre il record completo era ancora allocato. Successivamente
load_native_texture costruiva un altro vector con i pixel. I due picchi
avvenivano in momenti diversi: non vanno sommati come copie tutte simultanee.

L'ultimo campione globale prima del crash indica 39.845.888 byte CDRAM e
139.460.608 byte user RAM liberi. Questi dati non misurano lo spazio
allocabile nell'heap newlib dell'app, configurato a 144 MiB. Il dump prova
il fallimento dell'allocazione; non contiene uno snapshot delle liste
malloc che distingua esaurimento da frammentazione. Questo arresto avviene
prima dell'allocazione GPU della texture specifica e non dimostra un guasto GPU.

## Correzione e verifica

Il caricatore ora verifica lo stesso formato, sorgente e checksum, rimuove
le intestazioni nel buffer posseduto e trasferisce quel buffer fino alla
preparazione dell'upload GXM. Scompaiono la copia temporanea del payload e
la successiva allocazione di una seconda copia dei pixel. Rimangono la
memoria del record letto e la copia finale verso la texture GPU.

La costruzione della richiesta riserva correttamente 36 byte di intestazione,
evitando la riallocazione causata dalla precedente riserva di 32 byte.
Le entrate native texture e geometria intercettano bad_alloc e restituiscono
un miss al chiamante, con contatore `oom_fallback_total`. Le eccezioni sono
abilitate solo nel file del lettore opzionale: le altre unità del renderer
mantengono la politica precedente. Questo protegge il percorso nativo;
non certifica che ogni altra allocazione del gioco possa riuscire sotto pressione.

Un test CMPR 1024×1024 nega esplicitamente allocazioni di 4 MiB e di
4 MiB più 16 byte. La versione corretta carica pixel identici al riferimento
e restituisce lo stesso indirizzo del buffer del reader. Il controllo con
il vecchio consumatore termina con bad_alloc nello stesso caso. Altri casi
negano l'allocazione della richiesta o fanno fallire il reader: il percorso
texture e geometria restituisce false e aggiorna il contatore.

La candidata conserva GX asincrono, 8 MiB di geometria CDRAM, la quota
snapshot texture di 8 MiB compresa nei 24 MiB texture e ATTRIBUTE2=12.
Il PSARC resta invariato. Marker:
`psarc-resident-assets-async-oom-20261008`.

Il pacchetto con simboli e prove è identificato da
`build/resident-assets-async-candidate-path.txt`. La ripetizione del
caricamento della gara sulla Vita resta necessaria per confermare la
stabilità. La console non viene aggiornata durante questa analisi.
