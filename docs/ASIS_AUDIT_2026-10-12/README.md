# Audit dello stato attuale di WiiCompiled Vita e Aurora Vita

Report del 12 ottobre 2026, basato sull'ispezione dei sorgenti e sull'acquisizione in sola lettura del dispositivo dell'11 ottobre. Il problema prestazionale è confermato: l'ultima sessione conservata sulla PS Vita contiene una gara attiva a **1,381 frame/s**, circa **723,93 ms/frame**. Le texture bianche e l'HUD mancante restano problemi aperti; la loro causa comune non è dimostrata.

La priorità è recuperare una coppia WiiCompiled/Aurora completa e riproducibile, rendere attendibile la verifica dei pixel e individuare il costo dominante sul percorso CPU/GX. Le ottimizzazioni già introdotte non dimostrano che il port possa raggiungere 60 FPS; il target richiede 16,667 ms/frame e resta un criterio di accettazione da provare.

- [Report tecnico](REPORT.md): stato reale, misure, difetti, interpretazione e limiti.
- [Piano di correzione](PLAN.md): ticket ordinati, file, dipendenze, fallback e criteri di accettazione.
- [Implementazione e verifiche](IMPLEMENTATION.md): recupero applicato dopo l'audit e stato delle prove.
- [Evidenze strutturate](EVIDENCE.json): identità, risultati numerici, verifiche e riferimenti ai file originali.

La richiesta iniziale è: «fai un audit sul'asis attuale, dobbiamo migliorare le performance e riuscire a raggiungere i. 60fps qualsiasi cosa facciamo peggiora le perforamnce non riusciamo a raggiungere il target raggiungibile, in gioco abbiamo 1 fps, alncune texture bianche , e hud mancante, dobbiamo fixare anche aurora-vita !». Il successivo incarico è continuare la scrittura del report.

La fase iniziale di audit aggiungeva soltanto documentazione ed evidenze locali. Dopo l'autorizzazione «implementiamo», sono state applicate le correzioni descritte in `IMPLEMENTATION.md`, completate le build WiiCompiled/Aurora e verificata la progressione del candidato su Vita3K. Sulla PS Vita l'utente conferma l'ingresso in partita del candidato, con texture bianche, HUD assente e forte lentezza; il log monitorato non contiene una nuova sessione. SELF e TOML originali sono stati ripristinati e verificati. Il candidato resta disponibile localmente; materiali, HUD e target 60 FPS rimangono aperti. Durante l'ispezione iniziale erano state osservate modifiche concorrenti nei sorgenti Aurora; il report distingue quei difetti dallo stato successivamente ricontrollato.

La cattura originale è in [`build/vita-psarc/device-runs/asis-audit-20261011-20261011T213533Z`](../../build/vita-psarc/device-runs/asis-audit-20261011-20261011T213533Z). Calcoli, log dei controlli e diff osservati sono in [`build/asis-audit-20261011`](../../build/asis-audit-20261011). Questi percorsi sotto `build/` sono locali e possono essere esclusi da Git: il riepilogo numerico durevole è in `EVIDENCE.json`. Non eliminare le evidenze locali prima di archiviarle insieme alla coppia di sorgenti e binari corrispondente.
