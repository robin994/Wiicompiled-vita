# Ottimizzazioni MH3 per WiiCompiled e Aurora Vita

10 ottobre 2026. La repo MH3 offre un riferimento concreto per sostituire i percorsi NW4r/GX con produttori nativi, ridurre il lavoro ripetuto dei materiali e gestire risorse e frame senza attese globali. I guadagni misurati su Monster Hunter Tri appartengono a quel port; il target WiiCompiled resta 60 FPS con gara, HUD e materiali corretti.

Sono consegnati un primo esperimento CPU disattivato di default e un inventario riproducibile dei chiamanti GX di Mario Kart. Le riscritture di modelli, shader, skinning e audio sono definite come ticket separati. Aurora embedded e Aurora standalone sono confrontati come checkout distinti.

- [Analisi e priorità](REPORT.md): differenze architetturali, evidenza locale, parti trasferibili e limiti.
- [Mappa delle 45 tecniche](TECHNIQUES.md): tutti i capitoli dell'indice MH3, destinazione e stato nel nostro port.
- [Ticket implementativi](TASKS.md): file, dipendenze, fallback e criteri separati di correttezza e prestazioni.
- [Verifiche ed esperimento CPU](VALIDATION.md): test host, istruzioni per il confronto e limiti della prova.
- [Inventario statico GX](GX_CALLERS.json): 29.637 funzioni tradotte, 341 target dichiarati, 473 chiamanti e 1.993 archi.
- [Identità e provenienza](EVIDENCE.json): commit, hash degli input e dei sorgenti consultati.
- [Richiesta originale](ORIGINAL_REQUEST.md).
- [Avanzamento implementativo WiiCompiled M02/M05/M09](../MH3_IMPLEMENTATION_M02_M05_M09_2026-10-10.md):
  ricette GX native immutabili, UI LYT in FIFO asincrono e selettori di matrici
  GXM corretti. L'audit delle 45 tecniche rimane il riferimento originale;
  questi sono sotto-ticket, non il completamento dei 18 macro-ticket.

Fonte congelata: [Grarak/mh3-wii, commit 04092f4](https://github.com/Grarak/mh3-wii/tree/04092f4e9fe3906212eb27281c66ee72236a91b6). La copia completa per ulteriori ispezioni è conservata in `build/mh3-reuse-20261010/source/`, ignorata da Git. Questo bundle non incorpora asset o codice guest del gioco.
