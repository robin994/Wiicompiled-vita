# WiiCompiled Vita: consegna FMA/layout del 7 ottobre 2026

Candidata installata su `192.168.1.79:1337`, titolo `WICMKW001`, senza avvio automatico. Il controllo precedente resta a circa 1 FPS e termina dopo 1 player. La nuova build non e ancora stata provata dal giocatore.

- Marcatore: `psarc-cpu-fma-layout-20261007`.
- SELF installato e riletto: `2e1aa8c341520c0e619b734a626a287ec660a4ca310114da1c94451968610269`.
- VPK: `18546ae332a424d92b2bf4ae66e0b2505a21bf86a53b48025ac44161222ca7a9`.
- Sorgenti e input generati: `f74e666823d09f7427ceb4deb9bc8cd7928cb58d7ad89e4ea9bcfb6495d20354` (31035 file).
- Pacchetto: **14/14** file riletti e identici al VPK. Cambia soltanto eboot.bin.
- SFO identica: `ATTRIBUTE2=12`, `ATTRIBUTE=32768`; appmeta protetta invariata.
- Config identica: `9d6de0c071038fc7b5e5b723e6aeef4bde40e7940f96cc2a495bed23b99d2b65`.
- PSARC: 2642224628 byte, nessuna scrittura, dimensione e metadati invariati. I contenuti non sono stati riletti integralmente. La ISO resta assente.

La build abilita WIC_EXACT_FLOAT_FMA: prodotto float64 esatto per input float32, somma e restrizione con fallback libm sui punti medi e sui casi speciali. WIC_FAST_PPC_MATH resta OFF. La diagnostica campiona i chiamanti delle operazioni paired, conta i pane e le allocazioni SDK e salva layout-failure-*.brlyt al fallimento. Asserzioni, ABI guest e tabelle di dispatch originali sono conservate. L'assegnazione distinta dei core resta OFF; i due helper Aurora e l'I/O/audio rimangono quelli del controllo.

Validazione: **11/11** test core/runtime/input/CPU e oltre quattro milioni di confronti float32 bit per bit, inclusa cancellazione; **18/18** Aurora gia passati nella stessa sessione e sorgenti Aurora invariati in questa iterazione. Audit ARM su matematica, fallback e ingressi condivisi Layout/BuildPane/Allocator; collegamento GXM-only, SceFiber, pthread, input, stdio e attese verificato. Prova minima ARM del solo percorso matematico senza contatori CPU compilata e linkata; non e un VPK completo senza diagnostica e non e una misura FPS sulla Vita.

Il comando di arresto mirato della app ha risposto con un errore generico: **arresto non confermato**. L'aggiornamento e reversibile, eseguito tramite file temporaneo riletto e rinomina, con backup remoto dell'eseguibile precedente verificato. Chiudere completamente la app e riaprirla per caricare la nuova build. Nessun comando di avvio e stato inviato.

Il target di **30 FPS non e ancora raggiunto**. Il miglioramento matematico non e ancora misurato sulla Vita, l'uscita dopo 1 player non e corretta e i Mii restano da completare. La nuova diagnostica deve distinguere corpo BRLYT errato, allocazioni fallite o costruzione incompleta; non maschera l'errore.

Prova manuale: restare almeno 15 secondi nel menu licenze, poi scegliere 1 player. Raccogliere runtime.log, crash.txt e layout-failure-*.brlyt con il SELF corrispondente. La prova include diagnostica: un successivo A/B senza contatori deve misurare distribuzione dei tempi e rispetto del budget di 33,33 ms.

Artefatti conservati: [install-cpu-fma-layout-20261007T131728Z](../build/vita-psarc/install-cpu-fma-layout-20261007T131728Z/REPORT.md). Stato e misure: [VITA_CPU_MENU_STATUS_2026-10-07.md](VITA_CPU_MENU_STATUS_2026-10-07.md).
