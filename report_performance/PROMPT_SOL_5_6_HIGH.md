# Prompt operativo da consegnare a Sol 5.6 high

Copiare il testo seguente nella task di implementazione, configurata con Sol 5.6 e ragionamento high. Questo documento non avvia una task né cambia il modello della conversazione.

---

Lavora in `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita` per rendere Mario Kart Wii giocabile a 30 FPS su PS Vita reale. Segui il piano salvato in `report_performance`; l'obiettivo richiede immagini nuove, simulazione a velocità corretta, grafica funzionale, input responsivo e audio continuo.

Leggi prima, in quest'ordine:

1. `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/STATO_E_CONTINUAZIONE.md`
2. `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/REPORT.md`
3. `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/PIANO_SOL_5_6_HIGH.md`
4. `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance/evidence/provenance.json` e il manifest P6.38 salvato accanto.

Controlla istruzioni applicabili, Git e hash dei file prima di modificare. Il checkout è intenzionalmente sporco: conserva tutto il lavoro preesistente. Niente reset, checkout/restore distruttivi, clean globale, commit, push, tag o release senza richiesta. Non ripristinare automaticamente Aurora: il piano riguarda il direct-vitaGL attuale. Conserva flag/profili di confronto e non alterare il VPK di baseline.

La prima unità di lavoro è **F01: telemetria esclusiva e a basso overhead**, completando gli elementi mancanti della F00 per la baseline hardware. Implementala, compila, collega e produci un VPK diagnostico distinto derivato dal profilo `full-content-p6_38-producer-io-yaz0`. Prepara anche il confronto con telemetria disattivata. Non cambiare contemporaneamente cache, resa, batching, budget o sincronizzazione.

Il nuovo log deve separare producer, preparazione USER_2 e render USER_1 con timestamp e serial coerenti. Per USER_1 misura separatamente lookup/verifica generazioni texture, decode, bake, allocazione, upload, stato/shader, submission e attese GPU. Per EFB separa source wait, transfer/finish, resize/conversioni CPU, destroy e clear; registra il motivo delle copy failure. Aggiungi working set univoco, miss reason, topologie primitive e motivi di mancato merge. Includi l'overhead fuori dai timer attuali. Non aggiungere glFinish per draw per fare profiling.

Usa aggregati temporali e burst limitati: il vecchio summary ogni 120 frame non descrive la gara a 1 FPS. Evita record intercalati/NUL. Misura l'overhead ON/OFF. Il piano contiene i dettagli dei campi e i criteri di accettazione.

Tieni presenti i fatti verificati:

- Ultimo log ricevuto: 13 avvii; quello coerente con P6.38 è alle righe originali 18180–19928. L'archivio è in `report_performance/evidence/runtime.original.log`.
- Il percorso originale sotto SmashMeleeVita è cambiato durante l'audit e ora può contenere un log Melee. Per i dati MKW usare la copia archiviata, controllando hash e marker; non confondere i due progetti né sovrascrivere il file vivo.
- 51 present consecutivi danno 0,949185 present/s; 40 campioni producer di gara hanno worker mediano 1013,535 ms. I campioni producer sono selezionati verso i frame lenti. Il log non prova che ogni present sia un'immagine nuova.
- Il P6.38 non ha un perf_summary dettagliato della gara. 79 upload, 77 eviction, 6157 draw fisici e 249 ms EFB vengono dal boot 9, non dall'ultimo run.
- `efb_us` include `glFinish` e quindi potenziale lavoro GPU precedente. `tev_draw` conta classificazioni, non tempo TEV. Non sommare tempi inclusivi, thread sovrapposti o serial diversi.
- La differenza storica white/rescue di 848,079 ms è un indizio, non una decomposizione causale controllata. Il run 480×272 non contiene una gara comparabile e non esclude il fill-rate in gara.
- Nel P6.38 la preparazione vertici raggiunge 40–43 ms; non diventa gratuita perché gira su USER_2. Il percorso rescue è ancora diagnostico: depth/cull/blend/alpha disattivati e Z neutralizzata non possono essere il risultato finale.
- Il producer 1211 accumula 512 comandi EFB e 169 capacity failure dopo 4,26 s, mentre il worker precedente era già completato. Separare accumulo producer ed esecuzione EFB. `0x80544A5C` è `RKSYS::Mgr::SaveCoreTask` nella MAP: non eliminare il salvataggio.

Esegui i controlli pertinenti del progetto e verifica configurazione OFF, package e hash. I controlli offline non sostituiscono la Vita. Se il dispositivo e gli strumenti del progetto sono già disponibili nel flusso autorizzato, prosegui col test; altrimenti consegna VPK, hash e sequenza precisa da eseguire, segnando «in attesa di hardware». Puoi continuare lavori indipendenti ma non promuovere ipotesi a risultati.

Dopo il log hardware aggiornato, confronta numericamente ON/OFF e la baseline nella stessa scena. Aggiorna REPORT, PIANO e STATO, poi passa alla fase giustificata dai tempi esclusivi. Procedi per incrementi concreti con build e prove, non fermarti a proporre ottimizzazioni. Mantieni il target 30 FPS come aperto finché F10 non passa; non sostituirlo con 30 swap, frame saltati o gameplay rallentato.

Prima che il contesto o i token si esauriscano, salva gli aggiornamenti sotto `/Users/robin994/Documents/Code/PSVita/wiicompiled-vita/report_performance`. Lascia un messaggio finale con: fase completata, fase in corso, file modificati, VPK/ELF/hash, test passati/falliti, cosa manca su Vita e prossimo comando preciso. Non affidare lo stato soltanto alla chat.

---
