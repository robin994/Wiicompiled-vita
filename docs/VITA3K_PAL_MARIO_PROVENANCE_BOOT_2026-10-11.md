# WiiCompiled — prova Vita3K PAL Mario texture-provenance, 11 ottobre 2026

## Obiettivo
Confrontare i TEX0/TEV del BRRES PAL di Mario con le texture effettivamente
associate ai draw GXM del frame 300 tramite la nuova tabella di provenance
`frame-300-textures.csv`. Il test riguarda solo Vita3K; **nessuna modifica
alla PS Vita fisica**.

## Build candidata
- SELF: `build/vita-psarc/game-eboot.bin`, compilato 2026-10-11 11:13 CEST.
- SHA-256: `7e35227b1190bb6cbc1298c12dc3b6386fd492ab104379650fb3d3160c18559c`.
- DELIVERY.json del progetto e SELF all'interno del VPK: SHA identici.
- Il nuovo ELF contiene `frame-%llu-textures.csv` e `texture_table frame=`:
  la build richiesta era **già disponibile** e non è stata ricompilata.
- PSARC emulato preesistente: 3.219.563.021 byte. Non copiato né modificato.

## Prove e riscontri

| Tentativo | SELF | Diagnostica frame300 | CPU pool | Esito |
|---|---|---|---:|---|
| 1, ~11:19 | Nuovo 7e35227b | Abilitata | 10 | Arresto durante boot, frame 0 |
| 2, ~11:20 | Nuovo 7e35227b | Abilitata | 10 | Stesso arresto, frame 0 |
| 3, ~11:21 | Precedente 6cafee5f | Abilitata | 10 | Stesso arresto, frame 0 |
| 4, ~11:25 | Precedente 6cafee5f | Disabilitata | 10 | Stesso arresto, frame 0 |
| 5, ~11:27 | Precedente 6cafee5f | Disabilitata | 4 | Stesso arresto, frame 0 |

Tutti terminano circa 20–27 secondi dopo il lancio. L'ultima riga del
runtime log è prossima a `EXIUnlock_80169260 called`, dopo
`EXISelect`/`EXIImm`/`EXIDma`; non viene raggiunta alcuna
`[race-progress]` né il frame 300. I crash report macOS
`~/Library/Logs/DiagnosticReports/Vita3K-2026-10-11-11*.ips`
mostrano tutti `EXC_BAD_ACCESS / SIGTRAP` sul thread Vita3K che
esegue `call_import(EmuEnvState&, CPUState&, unsigned int, int)`.
Il simbolo `call_import+1108` è l'indirizzo di ritorno da una
chiamata di import: **non prova da solo che il dispatcher delle
importazioni sia la causa primaria**. Un report con stack simile
era già presente alle 02:10.

Conseguenze:
- L'esito non è una regressione esclusiva della nuova build:
  anche il SELF precedente e una sessione senza diagnostica falliscono.
- La variazione reversibile `cpu-pool-size: 10 → 4` non ha eliminato il crash.
- Nessun nuovo `frame-300-textures.csv` è stato generato.
  Le vecchie `frame-300-draws.csv` e `frame-300.ppm` non vanno
  scambiate per artefatti delle prove odierne: sono state ripristinate.
- **Non è ancora possibile attribuire le texture bianche a un TEX0,
  lightmap, UV o TEV sulla base di questa prova.**
- Non è stata effettuata alcuna nuova misurazione FPS attendibile.

## Ripristino, hash e tracce

Le prove hanno usato runner isolati sotto:
- `build/vita3k-trace/pal-mario-provenance-20261011/`
- `build/vita3k-trace/pal-mario-provenance-retry-20261011/`
- `build/vita3k-trace/pal-mario-boot-control-20261011/`
- `build/vita3k-trace/pal-mario-boot-no-diag-20261011/`
- `build/vita3k-trace/pal-mario-boot-pool4-20261011/`

Al termine è stato verificato SHA-256 identico per i cinque
file originali emulatore (Config.toml, config.yml, eboot.bin,
frame-300.ppm e frame-300-draws.csv).
L'originale emulatore `eboot.bin` è
`6cafee5f7c3269a65b67f2ec5e9d350b8df06a4f0d2b1bd363be46cbc34495f8`.
Vita3K non è in esecuzione. Nessuna installazione PS Vita,
nessuna nuova build, nessun reset del PSARC, nessun commit o push.

## Prossima verifica raccomandata

Il blocco si presenta durante il boot Wii/guest di Vita3K e precede
qualunque draw campionabile. Riprodurre con un'istanza Vita3K isolata
e aggiornata o ottenere lo stack dell'importazione che solleva
`SIGTRAP`, **senza cancellare prima la directory dati dell'emulatore**.
Quando il boot torna stabile, rieseguire la build nuova con
`[diagnostics] enabled=true, capture_frame=300` e correlare
`frame-300-draws.csv` + `frame-300-textures.csv` mediante
`scripts/audit_mkw_brres_materials.py`.

Non trasferire questi artefatti di diagnostica sulla PS Vita reale
come se fossero una build di rendering corretta: il crash Vita3K
non consente ancora un confronto valido.
