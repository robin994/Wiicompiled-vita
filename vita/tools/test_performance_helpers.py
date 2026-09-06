#!/usr/bin/env python3
"""Host differential tests; deliberately not a substitute for Vita validation."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
gfx = "aurora-main/platforms/vita/gfx/"
cases = [
    ("clip-position", ["vita/tests/clip_position.cpp", gfx+"vita_shader_gen.cpp",
                        gfx+"vita_pipeline_key.cpp"], ["-DMKW_VITA_CLIP_W=1"]),
    ("efb-fifo", ["vita/tests/performance_helpers.cpp"], []),
    *[(f"texture-budget-{flag}", ["vita/tests/texture_budget.cpp", gfx+"vita_texture_cache.cpp",
       gfx+"vita_texture_decode.cpp", gfx+"vita_pipeline_key.cpp"],
       [f"-DMKW_VITA_TEXTURE_SHARED_HEADROOM={flag}"]) for flag in (0, 1)],
]
results = []
with tempfile.TemporaryDirectory(prefix="vita-perf-tests-") as directory:
    for name, sources, flags in cases:
        binary = str(Path(directory)/name)
        command = [os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O2",
                   "-fsanitize=address,undefined", "-I.", "-Ivita/include", *flags,
                   *sources, "-o", binary]
        subprocess.run(command, cwd=root, check=True)
        run = subprocess.run([binary], capture_output=True, text=True, check=True)
        print(run.stdout, end="")
        results.append(dict(name=name, command=command, exit_code=run.returncode,
                            stdout=run.stdout, stderr=run.stderr))
output = root/"build/vita/performance-helper-tests.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(dict(hardware_validation=False, tests=results), indent=2)+"\n")
