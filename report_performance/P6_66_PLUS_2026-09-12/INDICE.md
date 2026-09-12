# Piano performance P6.66+ — Mario Kart Wii PAL RMCP01 / PS Vita

Data: 12 settembre 2026.

## Documenti

- [Report tecnico completo](REPORT_COMPLETO.md): copia integrale della risposta approvata per il salvataggio, incluse tabelle, pseudocodice, gate hardware, strategia dispatch e roadmap quantitativa.
- [Richiesta originale](RICHIESTA_ORIGINALE.md): vincoli, baseline dichiarata e requisiti di analisi forniti dall'utente.

## Contenuto del report

1. Diagnosi finale.
2. Budget frame corrente.
3. Piano P6.66 → P6.75 e candidati hot function.
4. Dettaglio delle prime tre patch e telemetria mirata del prebegin.
5. Strategia native-HLE senza stale indirect dispatch.
6. Strategia multicore, fasi successive, O3/LTO, vertex hash e testo doppio.
7. Roadmap quantitativa verso 30 FPS e limiti delle previsioni.
8. Primo intervento raccomandato: PSMTXRotTrig nell'entry generata.

## Stato e provenienza

Il report descrive un piano: nessuna patch proposta è stata implementata durante l'analisi o il salvataggio. Le modifiche locali preesistenti rimangono preservate.

Il VPK P6.65 verificato ha SHA-256 `cc82ad4ce00cffb837ec24d24479e25ad249c4fae851ce4edfaf25fee9cf0c89`. Il log locale `runtime2.txt` identifica P6.64R, marker `9E02AE3F`; le misure P6.65 nel piano sono quelle dichiarate nella richiesta. I guadagni e gli scenari futuri sono ipotesi da verificare su PS Vita reale.

Vincoli principali: direct vitaGL, nessuna reintroduzione di Aurora, nessun aumento della raw-mesh cache, nessuna alterazione arbitraria del timing VI/simulazione. Il salvataggio non autorizza l'implementazione delle patch.
