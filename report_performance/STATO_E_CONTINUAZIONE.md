# Stato salvato e continuazione

Aggiornato: 11 settembre 2026. Versione piano: 1.0.

## Risultato di questa task

Richiesta soddisfatta: report verificato e piano a più fasi pronti per Sol 5.6 high, salvati sotto `report_performance`. Nessuna modifica ai sorgenti del gioco, nessuna nuova build, nessun nuovo test hardware, nessuna task Sol avviata. È stato aggiunto soltanto il dossier con documenti, copie degli allegati ed estrattore delle evidenze.

| Fase | Stato | Prossimo criterio da soddisfare |
|---|---|---|
| F00 | Audit offline completato; benchmark hardware da completare | Identificare scena, capture, ripetizioni e relazione tick/present |
| F01 | Da implementare, prossima azione | VPK diagnostico con timer esclusivi, serial e overhead misurato |
| F02 | Aperta | Texture statiche senza churn ricorrente |
| F03 | Aperta | Copy EFB necessarie corrette, zero overflow, lifetime GPU sicuro |
| F04 | Aperta | Resa corretta senza rescue globale |
| F05 | Aperta | Meno draw fisici con primitive/ordine equivalenti |
| F06 | Aperta | Producer nel budget con guest/save corretti |
| F07 | Aperta | Prep/copie/contesa entro il budget |
| F08 | Aperta | 30 immagini nuove/s e simulazione alla velocità corretta |
| F09 | Aperta | Audio continuo e loading/save verificati |
| F10 | Aperta | Matrice hardware e criteri finali superati |

## Baseline e percorsi

- Root: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita`.
- HEAD letto: `5bdb6a63d4f495db7b086cd91059313f72cdfd8f`; 9 file tracciati erano già modificati e CLAUDE.md era non tracciato. Snapshot/hash in `evidence/provenance.json` e `evidence/git-status-at-audit.txt`.
- Profilo: `full-content-p6_38-producer-io-yaz0`.
- VPK: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_38-producer-io-yaz0.vpk`.
- VPK verificato: 44.391.665 byte, SHA-256 `050448ccb4eabb29779f3fbc374d85e6ac544cf023f6a7fdff69a7868c40b32b`.
- ELF indicato dal manifest: `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p6_38-producer-io-yaz0.elf`; SHA-256 riportato nel manifest `ba8f10daa00a2dd0d3ba934ffb1f3d4bb27fcfaed617ecf9d1f315f877656719`. In questo audit il VPK è stato hashato direttamente; non assumere ricompilato o verificato l'ELF dal solo manifest.
- VitaGL locale: `/Users/robin994/Documents/Code/PSVita/aurora-vita-max-prehardware/third_party/vitaGL-speedhack-src/libvitaGL-m12_5-custom-heap.a`, SHA-256 verificato `de04978951a09cdc28da1710face48051530be17358df7f50273d7a9a262600d`.
- Ultimo boot archiviato: righe 18180–19928; configurazione coerente con P6.38, senza identificatore crittografico della build nel log.
- Attenzione alla provenienza: durante questa task il file vivo nel percorso SmashMeleeVita è cambiato ed è diventato un log Melee v3.28 da 10.071 byte. Usare `report_performance/evidence/runtime.original.log` per riprodurre i numeri MKW; il log successivo è isolato in `runtime.observed-later.log` e non entra nelle metriche. Non ripristinare né modificare il file vivo.
- Baseline: 0,949185 present/s su 51 intervalli; worker mediano 1013,535 ms su 40 serial osservati; grafica ancora rescue, audio discontinuo. Nessuna prova dei 30 FPS.

## Verifiche completate

- Copie byte-identiche degli allegati e hash SHA-256 salvati.
- Parser eseguito sul log completo: 13 boot, 40 campioni producer gara, 40 worker univoci, 51 present consecutivi nella finestra; 109 record metrici esclusi, 10 duplicati, zero conflitti accettati.
- Dati numerici principali riscontrati sulle righe originali e significato dei timer/contatori verificato nel codice.
- VPK P6.38 locale: hash conforme al manifest e `unzip -tq` senza errori. Nessuna compilazione eseguita in questa task di pianificazione.
- Collegamenti locali verificati; hash delle fonti del gioco e degli artefatti esaminati invariati rispetto all'inizio dell'audit. La differenza del log vivo è registrata a parte, senza alterare la copia originale.

## Prossima azione concreta

Consegnare [PROMPT_SOL_5_6_HIGH.md](/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/PROMPT_SOL_5_6_HIGH.md) a Sol 5.6 high. Implementare F01 su un nuovo profilo che eredita P6.38; prima ricontrollare Git/fonti per eventuale lavoro sopraggiunto. Per riprodurre l'audit da root:

```sh
python3 report_performance/extract_evidence.py
```

Il primo gate operativo è ottenere una baseline di gara con breakdown render/prep/producer e motivi di failure EFB. Non ottimizzare «848 ms di TEV» come se fossero già misurati esclusivamente. Non rimuovere fence, copie EFB, callback o salvataggi sulla base del vecchio confronto.

## Modello di aggiornamento per la prossima consegna

Registrare data, fase/subfase, ipotesi, file modificati, profilo e flag, hash fonti/VPK/ELF/libreria, comandi e codici di uscita, scenario e serial del nuovo log, misure prima/dopo con N e unità, overhead, risultato visivo/audio/input, regressioni e azione successiva. Per ogni fase usare uno stato preciso: da implementare / in corso / verificata offline / in attesa di hardware / verificata su hardware / scartata per regressione.

Il piano va aggiornato dopo ogni risultato, non solo a fine task. Prima di esaurire il contesto salvare qui la posizione esatta e il prossimo comando; il risultato hardware mancante deve rimanere esplicito.
