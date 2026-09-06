# Ripristino del carico grafico — 6 settembre 2026

La nuova richiesta dà priorità a 3D, texture e contenuti animati prima di ulteriori ottimizzazioni. La priorità “performance prima dei materiali” nel testo allegato è quindi superata. Per “motionpng” è stato assunto THP/Motion-JPEG in attesa di chiarimento; nessun sottosistema PNG animato è stato identificato nel codice esaminato.

## Evidenza dal nuovo log

Il file contiene **due sessioni**: la prima con `perf_log=1`, l'ultima con `perf_log=0`. Nell'ultima 3D, texture, EFB, billboard, light texture, movies e native THP erano **già abilitati**: `perf_skip_* = 0`, `perf_force_3d_solid=0`, `movies_disabled=0`, `native_thp=1`. I problemi visivi non erano quindi spiegati da questi interruttori diagnostici.

| Seriale dell'ultima sessione | Producer ms | Renderer ms | EFB sync ms | EFB copy ms | GPU / CPU copie |
|---|---:|---:|---:|---:|---|
| 1200 | 158,846 | 39,904 | 16,426 | 4,347 | 12 / 0 |
| 1500 | 175,224 | 47,071 | 17,129 | 4,156 | 12 / 0 |
| 1800 | 202,473 | 50,245 | 17,192 | 4,294 | 12 / 0 |

Il percorso native-resolution elimina il resize CPU in questi campioni; la memoria EFB arriva a 4.174.656/4.194.304 byte. Non si deve confrontare seriale 900 fra sessioni: nell'ultima è UI, con 96 draw e nessuna geometria prospettica. Queste misure non sono ancora un benchmark di un renderer GX completo.

La scena pesante termina al seriale 1996: producer 1.492.447 µs, wait GX 1.130.782 µs, 6159 draw, 38708 vertici. Esistono errori di allocazione texture 256x256 con budget 10 MiB. Non c'è il campione 2100, né un resource_summary della parte finale: non si può quantificare il risultato del safe retry nella scena pesante dai soli campioni 1200–1800. Nessun marker `thp: native decode` nell'ultima sessione: abilitazione del decoder e riproduzione effettiva restano fatti distinti.

Il dettaglio estratto, hash del log e ultima riga sono in `hardware-log.json`. Il log originale non è stato modificato.

## Modifiche implementate

1. **W prospettico conservato**, flag `MKW_VITA_CLIP_W=1`. Il vertice compatto passa da 24 a 28 byte conservando gli offset precedenti. Il producer conserva il W originale insieme alle coordinate NDC; lo shader compatto ricostruisce la posizione omogenea prima del clipping/interpolazione GPU. Anche il fallback canonico conserva W. UI e probe hanno W predefinito 1. I test controllano ABI, separazione delle pipeline e interpolazione prospettica. Rimane la gestione preesistente dei vertici con W quasi zero: non dichiarare risolti tutti i casi di clipping.
2. **Texture con coordinate generate dalla posizione**: non sono più escluse perché manca TEX0. L'attributo è richiesto soltanto quando la sorgente texgen è effettivamente TEX0.
3. **Colori materiale non illuminati**: `GXSetChanCtrl` non è più vuoto; sono memorizzati i controlli colore/alpha, gestiti gli identificatori combinati e i registri XF 0x1009–0x1011. I draw conservano il colore materiale e applicano RGB/alpha da registro quando il canale non usa illuminazione. Le modifiche invalidano lo stato per non riutilizzare colori vecchi.
4. **Diagnostica filmati limitata**: MovieManager registra apertura e preparazione; il decoder THP registra ingresso ed errori distinguendo header JPEG, pixel e destinazione guest. Logging limitato ai primi eventi/potenze di due, senza trace per draw. Non è stata inventata una conversione THP senza conoscere la dimensione corretta del frame o disporre di un campione fallito.
5. **Profilo `full-content-3d`**: tutti i contenuti già supportati attivi, P4.1/P5.1/P6/P7 conservati, `perf_log=0`, nessun hot shard. Nessun transient FBO, aumento dei budget, O3 globale o fast-math.

Lavoro sul backend effettivamente compilato `aurora-main/platforms/vita/gfx`; il clone separato `aurora-vita/` non viene usato dal VPK. Non sono stati effettuati commit, reset o modifiche all'archivio vitaGL.

## Verifiche

PASS: compilazione/link ARM32, VELF/FSELF, VPK e verifica ZIP, manifest e hash; `graphics-check`; test host ASan/UBSan per posizione W, resample/FIFO EFB e budget texture; `git diff --check`. I sorgenti coincidono con gli hash nell'evidenza della build. Nessun test della nuova build su Vita reale eseguito qui.

Riproduzione:

```sh
python3 vita/tools/test_performance_helpers.py
python3 vita/tools/build_performance_profile.py full-content-3d
```

## Limiti ancora aperti: non chiamare questa build “GX completo”

- Il bridge continua a rappresentare una texture/stadio selezionato e a ridurre combinatori custom a preset: TEV multistadio e multitexture completi non sono implementati.
- Illuminazione da normali/luci e texgen da normale o altri set UV non sono completati. La patch dei colori copre soltanto i canali non illuminati, non tutta la causa possibile dei modelli bianchi.
- Pressione texture nella scena pesante ancora presente; il nuovo log deve correlare errori e retry prima di cambiare policy o budget.
- Filmati attivi per configurazione, ma nessuna prova di apertura, decode e presentazione nel log ricevuto. Il prossimo log deve distinguere `movie: open`, `movie: prepare`, `thp: decode_enter`, `thp: decode_error` e `thp: native decode`.
- Nessuna prova che orientamento dei modelli, tutti i materiali, clipping limite e 60 FPS siano risolti.

Il percorso THP è stato anche confrontato come riferimento con il [decoder MJPEG/THP di FFmpeg](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/mjpegdec.c); non è stato copiato codice né dedotta la causa dei filmati mancanti da quel solo riferimento.

## Continuazione

Ripartire da questo report, `PORTING_STATUS.md` e dall'evidenza del VPK sottostante, preservando il worktree dirty. La priorità richiesta è correttezza/carico completo, non nuovi speedhack. Prima confrontare visivamente la nuova build nella stessa selezione/modello/menu e acquisire log nuovo; seguire i marker movie/THP per distinguere una mancata chiamata da un errore decoder. Il successivo lavoro di implementazione è il trasporto dello stato TEV completo, di tutte le texture/coordinate necessarie e di normali/luci dal GX producer al backend Aurora. Non affermare che il semplice fatto di avere tutti i flag attivi completi questo lavoro. Non riabilitare vecchi FBO o lo shard O2 senza nuova evidenza.

## Artefatti verificati

- `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.vpk` — 41285446 byte; SHA-256 `84a5c216ad7bcbd0626c79c188a2a108e8c5233f917ec4e8b09b87449795c112`.
- `build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.manifest.txt` — 1605 byte; SHA-256 `9c4bb44c69dc4037fe307bd7936e49713b1000167c3de87e133584607fd6f2d4`.
- `build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.elf` — 219266808 byte; SHA-256 `b5d3a0569d5ddd8d5be7711c2e485ba8fcf6b1c3b5c2a492c307c23b740a91ed`.

Manifest: tutti gli skip diagnostici a zero, movies attivi, THP nativo, clip_w=1, P5.1, safe retry, coda 2, cap EFB 512; `translated_hot_shards=` vuoto; NEON `-Os`.
