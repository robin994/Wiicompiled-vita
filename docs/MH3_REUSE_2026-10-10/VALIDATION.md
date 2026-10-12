# Verifiche e confronto dell'esperimento dispatch

## Risultati verificati

Il primo trasferimento è nel runtime WiiCompiled, con flag OFF per default. Il renderer Aurora embedded e standalone non sono modificati. Questa attività non compila o installa SELF/VPK, non cambia la Vita o il suo config, non rigenera asset e non modifica la simulazione.

I risultati delle prove host e sanitizer sono registrati in `EVIDENCE.json`. Log completi: `build/mh3-reuse-host-build.log`, `build/mh3-reuse-host-tests.log`, `build/mh3-reuse-aurora-build.log`, `build/mh3-reuse-aurora-tests.log`, `build/mh3-reuse-sanitizers.log`.

- Suite host WiiCompiled: **29/29 PASS**. Include i quattro casi di dispatch con flag OFF/ON e tabella dinamica/generata, più il contratto dell'inventario.
- Inventario Python: **5/5 PASS**, su commenti/literal, schema, edge duplicati, funzioni duplicate e classificazione quad effetti.
- Suite host Aurora: **24/24 PASS**; sorgenti embedded e standalone invariati.
- AddressSanitizer e UndefinedBehaviorSanitizer: **4/4 esecuzioni PASS**, flag OFF/ON e tabella dinamica/generata. Questi controlli non sono un test ThreadSanitizer o una prova del modello di memoria ARM.

Il test del dispatch collega la vera implementazione del registro. Le sole stub sono il popup fatale e le due funzioni sintetiche che rappresentano i target. Non sostituisce `Finalize`, le cache o la selezione dei vincitori. Il registro è finalizzato mentre esistono sei thread lettori; l'inizializzazione del payload resta protetta dalle pubblicazioni reali. Le prove host non dimostrano la semantica ARM del binario finale o pixel/FPS Vita.

## Riprodurre su host

```sh
cmake -S . -B build/host -DWIC_AURORA_TESTS=OFF \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
cmake --build build/host --parallel 8
ctest --test-dir build/host --output-on-failure
python3 scripts/inventory_mkw_gx_callers.py \
  --output build/mh3-reuse-20261010/GX_CALLERS-current.json
```

La mappa salvata nel report è legata agli hash del metadata, della MAP e dei sorgenti GX. Rigenerarla dopo una modifica a questi input. Il file contiene metadati e simboli, non payload di texture, audio o istruzioni guest.

## Perché il fast path conserva la pubblicazione

1. Prima di `Finalize`, i memo non vengono riempiti e i verdetti non vengono fissati; un'override registrata più tardi resta visibile.
2. Una voce memo può essere riempita soltanto dopo acquire della pubblicazione finale. È locale al thread e punta a storage immutabile per tutta la vita del processo.
3. Un hit successivo dello stesso thread riusa quell'acquire precedente. Un miss ripete acquire e lookup come prima; un miss negativo è valido soltanto dopo freeze.
4. Il byte positivo/negativo del verdetto è l'intero dato condiviso. Leggerlo relaxed non pubblica payload; vedere ancora zero fa ricalcolare la stessa risposta con acquire del registro.
5. Le cache condivise che contengono puntatori, la tabella generata e le guardie ABI conservano il loro protocollo.

Se in futuro il registro diventa aggiornabile a runtime, questo esperimento richiede epoch e invalidazione oppure deve essere disattivato. Non estendere la stessa motivazione ai command buffer o alle risorse GPU.

## Confronto Vita manuale

Il runbook corrente resta [VITA_P0_NEXT_BUILD_2026-10-10.md](../VITA_P0_NEXT_BUILD_2026-10-10.md). Una build del solo nuovo esperimento deve usare tutte le stesse opzioni del controllo e aggiungere:

```text
-DWIC_IMMUTABLE_DISPATCH_FASTPATH=OFF  # controllo
-DWIC_IMMUTABLE_DISPATCH_FASTPATH=ON   # candidata separata
```

Usare directory e manifest distinti; ricompilare runtime e tutti gli shard che includono `abi_bridge.h`. Nessuna rigenerazione di codice guest è richiesta da questa modifica perché non cambia il contratto del traduttore o i vincitori.

Prima di attribuire un risultato alla modifica, verificare `CMakeCache`, compile definitions, hash SELF/ELF/config, marker `[cpu-dispatch-config] immutable_fastpath=1` e disassembly del vero ELF ARM. Contare i `dmb` per funzioni calde e controllare che gli acquire restino sui miss. Un conteggio totale delle barriere non sostituisce la frequenza dinamica dei siti.

Gate di correttezza: menu, Time Trial attivo non in pausa, controller `0x124`, audio continuo e medesimi modelli/HUD. Tre run quiet identici per variante dopo warmup, shader/PSARC identici, stessi clocks e affinità, probe OFF per FPS. Sorgenti e gate M00 si applicano anche a questa candidata. La modifica non è presentata come soluzione al bianco/scuro o al costo EFB.

Rollback: ricompilare e ripristinare il controllo con flag OFF usando l'installer con backup già esistente. Per il percorso corrente, `ResolveDirectCpuTargetInfoCached` mantiene acquire sul puntatore condiviso in entrambe le varianti.
