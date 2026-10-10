# WiiCompiled Vita: consegna THP nativo/heap del 7 ottobre 2026

Candidata installata su **192.168.1.79:1337** e provata dall'utente: video non
funzionanti, menu riferiti a 1 FPS e uscita dopo un player. I log e il SELF
corrispondente sono conservati in `device-runs/native-thp-heap-20261007T141115Z`.
Risultati, correzione della pubblicazione delle texture e successiva candidata
H.264 nel [rapporto AVC](VITA_HARDWARE_AVC_2026-10-07.md).

- Marcatore: `psarc-native-thp-heap-20261007`.
- SELF SHA-256: `a61db43b35e9befb5e186b595433b2c4e6ca094db4db82fb004996c602b376b0`.
- VPK SHA-256: `9323fb7041bc9101bfc0f173373d9212a180b158510ce33dee455173ccf2ecc4`.
- Sorgenti e input generati SHA-256: `f5b50ed5ae60693d5928f2759f56170c0679d2c0056fbed89c33a1b8ad1b4380`.
- Archivio candidata: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/vita-psarc/candidate-native-thp-heap-20261007T140347Z`.
- Archivio installazione: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/vita-psarc/install-native-thp-heap-20261007T140348Z`.
- 14/14 file del pacchetto riletti e identici al VPK; cambia solo eboot.bin.
- SFO identica: `ATTRIBUTE2=12`, `ATTRIBUTE=32768`; appmeta identica.
- Config SHA-256: `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`, preservata.
- PSARC: 2642224628 byte; nessuna scrittura, dimensioni/listing invariati.
  Il contenuto PSARC non è stato riletto integralmente. ISO assente.
- Backup dell'eseguibile precedente conservato e riletto con hash verificato.
- Nessun comando di avvio inviato al gioco.

Il comando mirato di stop non è stato confermato dal servizio Companion.
L'aggiornamento usa file temporaneo verificato e rename, con backup;
**chiudere e riaprire manualmente l'app** per caricare la nuova build.

13/13 test host, ASan/UBSan del decoder, nove frame reali (differenza massima
1/255 rispetto a FFmpeg). Audit ARM: wrapper/fallback, chiamante THP diretto,
registrazione e dispatch; collegamento GXM-only, SceFiber/pthread, input,
stdio e attese. I 18/18 test Aurora già passati nella sessione riguardano
sorgenti Aurora invariati in questa iterazione.

Questo verifica sorgenti, compilazione, collegamento e installazione.
Non dimostra FPS, resa video o uso effettivo dei worker sulla Vita.
L'uscita dopo 1 player e il Mii mancante **non sono ancora corretti**.
La candidata sostituisce il decoder THP e aggiunge la lettura dello heap.

Prova manuale: menu licenze per almeno 15 secondi, controllare video e
fluidità, poi selezionare 1 player. Recuperare log con il marker e SELF
corrispondenti. I nuovi record [thp-native] separano decode/packing e riportano
i tile su ciascuna lane; [layout-heap] distingue heap vuoto, frammentazione,
alignment e liste non valide. I tempi diagnostici non sono un A/B quiet.

Implementazione e confronto delle opzioni:
[THP nativo e Aurora/Strikers](VITA_NATIVE_THP_AURORA_2026-10-07.md).
