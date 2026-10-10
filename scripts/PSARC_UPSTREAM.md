# PSARC source provenance

`scripts/vita_psarc.py` and `scripts/LICENSE-CC0-PSARC.txt` were copied from
Strikers `smstrikers-port/tools/asset_pipeline/` on 2026-10-06 at commit `89d020269a45bdbf1282e3fe6ec45604c1634e13` (local modifications present). The container
module derives from the CC0 PSARC tooling by Rinnegatamante/re4.

`core/src/asset_archive.c` and `core/include/port/asset_archive.h` reuse the
Strikers portable PSARC reader. The project GPL-3.0 license applies. Local
adaptations rename platform defines and size Vita files through native 64-bit
I/O. The WiiCompiled Vita wrapper executes all archive I/O on a native thread
so guest libco stacks never enter the kernel for archive reads.

Original game blocks remain stored, with identical payload deduplication.
