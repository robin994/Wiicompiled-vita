# P5.4: verifica di ripresa, 2026-09-06

## Stato
P5.4 implementata e offline tested nella sessione precedente; hardware validation ancora pendente. Nessuna nuova patch runtime o build in questa sessione. Gli artefatti esistenti corrispondono agli hash forniti. Nessuna Vita montata sotto /Volumes e nessun nuovo log P5.4 fornito.

## Log e baseline
`build/vita/runtime.log`, SHA-256 `7194ff4422eb991276733b91d643a0b96c3cc45ee60eae9304909bbb597f99f8`, contiene due boot. L'ultimo inizia alla riga 484; marker init alla riga 520: incremental_cache_eviction=1, wait_timing_service=1, nessun fiber_irq_state. Compatibile con P5.3, non prova di esecuzione P5.4. Non confondere i boot né usare timestamp come identificazione della build.
Ultimo vi_stall: deferred=21407/20838/135 (97,342% skipped), last_retrace_age_us=12363496, next_retrace_due_us=-12346830. Ultima riga: gx_cpu_perf frame=900, prebegin_us=80283 tail_us=4160 copydisp_us=2025.

| Metrica | P5.3 campione 1200 | P5.4 |
|---|---:|---|
| producer interval | 202496 us | non disponibile |
| wait_gx | 35510 us | non disponibile |
| queue wait | 1 us | non disponibile |
| packet copy | 1909 us | non disponibile |
| residual stimato | 165076 us | non disponibile |
| render / submit | 53130 / 51183 us | non disponibile |
| endframe / swap | 1713 / 128 us | non disponibile |
| EFB efb_us | 17075/0/0/0 | non disponibile |
| EFB path / native | 12/0/0/0 / 12 | non disponibile |
| tex_fail / retry wait | 0 / 0 us | non disponibile |

GX CPU frame=900 (non equiparare automaticamente al serial renderer 1200): DL=22673 us, prebegin=80283 us, tail=4160 us, copydisp=2025 us. Raw mesh 705628 byte, DL cache 127 entry / 58576 byte; nessuna eviction/clear/skip. Al producer frame 925 residual circa 6281828 us: anche il lavoro guest resta da misurare dopo P5.4. Non attribuire tutto il tempo WaitRender alla GPU senza correlazione dei campioni.

## Artefatti conservati
Profilo `full-content-p5_4-fiber-irq`.
- VPK `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_4-fiber-irq.vpk`, 41280970 byte, SHA-256 `8066d1a6cbce6ec78611d60dde20db8e3a2cfe697401e424b159e9cb7b650317`.
- ELF `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-p5_4-fiber-irq.elf`, 219297272 byte, SHA-256 `253bacb9128f2cef58c6541e6de1a45d6646d0f5abd50f03d6ded0822a48614f`.
- Manifest accanto al VPK, stesso basename `.manifest.txt`: verificati full content, clip_w=1, faithful LYT, native THP, P4.1/P5.1/P6/P7, timing service, incremental eviction, fiber_irq_state=1, depth=2, EFB cap=512, PERF_LOG=0, no hot shard, translated NEON -Os.

## Verifiche
PASS in questa sessione: SHA-256 VPK/ELF, dimensioni, manifest, unzip -t, verify-mkw-firstboot-vpk sul VPK esistente (make -o sul VPK per evitare rebuild), git diff --check. Compilazione/link/VELF/FSELF/package precedentemente documentati PASS, non rieseguiti perché nessun sorgente runtime è cambiato. Test hardware P5.4 non eseguito: manca il log/dispositivo. Graphics-check: risultato riportato sotto al termine.

## Modifiche della sessione
Solo questo handoff e note append-only in PORTING_STATUS.md e docs/performance-60fps-2026-09-05/IMPLEMENTATION.md. Patch P5.4 preesistenti lette e preservate.

## Blocker e prossimo test
1. Verifica causale P5.4: installare il VPK sopra, conservare/rinominare il vecchio runtime.log sulla Vita prima di avviare, ripetere la stessa sequenza menu/transizione/scena heavy P5.3, acquisire il nuovo log completo. Richiedere marker fiber_irq_state=1. Confrontare delta skipped/calls su finestre equivalenti, advanced e VI debt. Verificare mutex/message queue, sleep/wakeup, input/audio e assenza di deadlock; il solo boot non valida P5.4.
2. Se VI debt resta elevato: correlare fiber_slice/watchdog e reali sezioni IRQ-off in runtime/src/fiber_manager.cpp, runtime/src/hle/os/os_interrupt.cpp e VI deferred; nessuna forzatura degli IRQ.
3. Se corretto: calcolare producer residual e confrontare renderer/EFB/DL/texture per scegliere il blocker maggiore. Packet ownership/slot swap con depth 2, ownership esplicita, kill switch e telemetria solo se packet_copy resta significativo e non dominano stall maggiori. EFB fence soltanto dopo prova della dipendenza; non eliminare glFinish alla cieca.

Build quando necessaria:
```sh
python3 vita/tools/build_performance_profile.py full-content-p5_4-fiber-irq --jobs 8
```
A/B baseline: stesso wrapper con `full-content-p5_3-cache-eviction`. Nessuna build in corso alla chiusura.

## Esperimenti esclusi
Transient FBO/glBlitFramebuffer (crash SceGxm), TransferDownscale come nearest generico, billboard O2 senza nuovi dati (~0,6% storico), global O3/fast-math, aumenti arbitrari budget, contenuti disabilitati, Vita3K come prova prestazionale. Preservare staging translated NEON, cache e ottimizzazioni validate.

## Worktree
HEAD `01f530b500940eb2f64c0aff13e5507d6876dbb5`. Stato iniziale, tutto preesistente:
```
 M Makefile.vita
 M PORTING_STATUS.md
 M docs/performance-60fps-2026-09-05/IMPLEMENTATION.md
 M runtime/include/fiber_manager.h
 M runtime/include/hle_stubs.h
 M runtime/src/fiber_manager.cpp
 M runtime/src/hle/os/os_interrupt.cpp
 M vita/gx_backend.cpp
 M vita/tools/build_performance_profile.py
?? CLAUDE.md
?? aurora-vita/
?? vita/tools/__pycache__/
```
Nessun reset/clean/restore/checkout/revert/commit/push. `aurora-vita/` è un repository untracked nel padre, internamente pulito alla verifica; git submodule status del padre vuoto.

Graphics-check PASS (exit 0), frontend syntax check con configurazione make default; warning enum-enum preesistente in GXVert.cpp:536. Non è una prova runtime P5.4. Nessun test fallito.
