"""Embedded runtime: restricted compiler files, exit statuses and resource roots."""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def check_jit_runtime(exe, base):
    exe = Path(exe).resolve()
    base = Path(base).resolve()
    base.mkdir(parents=True, exist_ok=True)
    # Build-tree and release layouts both keep tcclib at one of these levels.
    candidates = [Path(os.environ["GOOSE_TCCLIB"])] if "GOOSE_TCCLIB" in os.environ else []
    candidates += [p / "tcclib" for p in (exe.parent, exe.parent.parent)]
    stage = next((p for p in candidates if (p / "include/tccdefs.h").is_file()), None)
    if stage is None:
        raise RuntimeError("cannot locate the compiler's bundled TinyCC files")
    with tempfile.TemporaryDirectory(prefix="jit-runtime-", dir=base) as tmp:
        root = Path(tmp)
        minimal = root / "tcclib"
        (minimal / "include").mkdir(parents=True)
        shutil.copy2(stage / "include/tccdefs.h", minimal / "include/tccdefs.h")
        lib = Path("lib/libtcc1.a") if os.name == "nt" else Path("libtcc1.a")
        (minimal / lib).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(stage / lib, minimal / lib)
        env = dict(os.environ, GOOSE_TCCLIB=str(minimal))
        programs = []
        for name, body in (
            ("exit", 'fn main() { print("leaving"); exit(7); }'),
            ("abort", 'fn main() { abort("hosted failure"); }'),
            ("normal", 'import os; import math; fn main() { '
             'var a: i64[>..] = []; for i in 1000 { a.push(i); } '
             'print("after ", a.len, " ", sqrt(81.0)); print(resource_dir()); }'),
        ):
            path = root / name / "main.goose"
            path.parent.mkdir()
            path.write_text(body + "\n")
            programs.append(path)
        for code, path in zip((7, 1, 0), programs):
            result = subprocess.run([str(exe), "--jit", str(path)], cwd=root, env=env,
                                    capture_output=True, text=True, timeout=60)
            text = result.stdout + result.stderr
            if result.returncode != code:
                raise RuntimeError(f"incorrect program status {path}:\n{text}")
            if code == 1 and "hosted failure" not in result.stderr:
                raise RuntimeError(f"missing abort diagnostics:\n{text}")
            if code == 0:
                expected_root = path.parent.as_posix() + "/"
                if "after 1000 9" not in result.stdout or expected_root not in result.stdout:
                    raise RuntimeError(f"incorrect runtime output/resource root:\n{text}")
